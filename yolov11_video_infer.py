from __future__ import annotations

import argparse
import csv
import time
from pathlib import Path
from typing import Union

import cv2
from ultralytics import YOLO


ImageSource = Union[str, int]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="YOLOv11 image/video/camera inference")
    parser.add_argument("--model", type=str, default="yolo11/yolo11s.pt", help="Path to .pt or .onnx model")
    parser.add_argument("--source", type=str, required=True, help="Image/video path or camera index, e.g. 0")
    parser.add_argument("--imgsz", type=int, default=640, help="Inference image size")
    parser.add_argument("--device", type=str, default="cpu", help="cpu, cuda, or GPU index if available")
    parser.add_argument("--conf", type=float, default=0.25, help="Confidence threshold")
    parser.add_argument("--iou", type=float, default=0.45, help="NMS IoU threshold")
    parser.add_argument("--save", type=str, default="", help="Output video/image path. Empty means no file save")
    parser.add_argument("--show", action="store_true", help="Show a live OpenCV window")
    parser.add_argument("--max-frames", type=int, default=0, help="Maximum frames to process; 0 means no limit")
    parser.add_argument("--warmup-frames", type=int, default=3, help="Frames excluded from latency summary")
    parser.add_argument("--log-csv", type=str, default="", help="Optional CSV path for per-frame latency logs")
    parser.add_argument("--no-plot", action="store_true", help="Skip drawing boxes to measure inference loop only")
    return parser.parse_args()


def resolve_source(source: str) -> ImageSource:
    return int(source) if source.isdigit() else source


def is_image_file(path: str) -> bool:
    suffix = Path(path).suffix.lower()
    return suffix in {".jpg", ".jpeg", ".png", ".bmp", ".webp", ".tif", ".tiff"}


def run_image(args: argparse.Namespace, model: YOLO) -> None:
    start = time.perf_counter()
    results = model.predict(
        source=args.source,
        imgsz=args.imgsz,
        device=args.device,
        conf=args.conf,
        iou=args.iou,
        verbose=False,
    )
    latency_ms = (time.perf_counter() - start) * 1000
    result = results[0]
    num_det = 0 if result.boxes is None else len(result.boxes)

    if args.save:
        save_path = Path(args.save)
        save_path.parent.mkdir(parents=True, exist_ok=True)
        if args.no_plot:
            image = cv2.imread(args.source)
        else:
            image = result.plot()
        if image is None:
            raise RuntimeError(f"failed to read image for saving: {args.source}")
        cv2.imwrite(str(save_path), image)

    print(f"source={args.source}")
    print(f"detections={num_det}")
    print(f"latency_ms={latency_ms:.2f}")
    print(f"fps={1000.0 / latency_ms:.2f}")


def open_capture(source: ImageSource) -> cv2.VideoCapture:
    cap = cv2.VideoCapture(source)
    if not cap.isOpened():
        raise RuntimeError(f"failed to open source: {source}")
    return cap


def make_writer(path: str, fps: float, width: int, height: int) -> cv2.VideoWriter:
    output_path = Path(path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fourcc = cv2.VideoWriter_fourcc(*"mp4v")
    return cv2.VideoWriter(str(output_path), fourcc, fps if fps > 1 else 25.0, (width, height))


def run_stream(args: argparse.Namespace, model: YOLO) -> None:
    source = resolve_source(args.source)
    cap = open_capture(source)

    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    input_fps = float(cap.get(cv2.CAP_PROP_FPS) or 0.0)
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0)

    writer = make_writer(args.save, input_fps, width, height) if args.save else None

    csv_file = None
    csv_writer = None
    if args.log_csv:
        log_path = Path(args.log_csv)
        log_path.parent.mkdir(parents=True, exist_ok=True)
        csv_file = log_path.open("w", newline="", encoding="utf-8")
        csv_writer = csv.writer(csv_file)
        csv_writer.writerow(["frame_id", "latency_ms", "fps", "detections"])

    latencies = []
    measured_latencies = []
    frame_id = 0

    print(f"model={args.model}")
    print(f"source={args.source}")
    print(f"resolution={width}x{height}")
    print(f"input_fps={input_fps:.2f}")
    print(f"total_frames={total_frames}")

    try:
        while True:
            ok, frame = cap.read()
            if not ok:
                break

            start = time.perf_counter()
            results = model.predict(
                source=frame,
                imgsz=args.imgsz,
                device=args.device,
                conf=args.conf,
                iou=args.iou,
                verbose=False,
            )
            latency_ms = (time.perf_counter() - start) * 1000
            latencies.append(latency_ms)
            if frame_id >= args.warmup_frames:
                measured_latencies.append(latency_ms)

            result = results[0]
            num_det = 0 if result.boxes is None else len(result.boxes)
            fps = 1000.0 / latency_ms if latency_ms > 0 else 0.0

            if args.no_plot:
                output_frame = frame
            else:
                output_frame = result.plot()
                cv2.putText(
                    output_frame,
                    f"frame={frame_id} det={num_det} latency={latency_ms:.1f}ms fps={fps:.1f}",
                    (20, 40),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.8,
                    (0, 255, 0),
                    2,
                    cv2.LINE_AA,
                )

            if writer is not None:
                writer.write(output_frame)

            if csv_writer is not None:
                csv_writer.writerow([frame_id, f"{latency_ms:.3f}", f"{fps:.3f}", num_det])

            if args.show:
                cv2.imshow("yolov11-infer", output_frame)
                if (cv2.waitKey(1) & 0xFF) == ord("q"):
                    break

            frame_id += 1
            if args.max_frames > 0 and frame_id >= args.max_frames:
                break
    finally:
        cap.release()
        if writer is not None:
            writer.release()
        if csv_file is not None:
            csv_file.close()
        cv2.destroyAllWindows()

    if not latencies:
        print("frames=0")
        return

    summary_latencies = measured_latencies if measured_latencies else latencies
    avg = sum(summary_latencies) / len(summary_latencies)
    sorted_latencies = sorted(summary_latencies)
    p95 = sorted_latencies[int(0.95 * (len(sorted_latencies) - 1))]

    print("--- summary ---")
    print(f"frames={len(latencies)}")
    print(f"warmup_frames={min(args.warmup_frames, len(latencies))}")
    print(f"measured_frames={len(summary_latencies)}")
    print(f"avg_latency_ms={avg:.2f}")
    print(f"avg_fps={1000.0 / avg:.2f}")
    print(f"min_latency_ms={min(summary_latencies):.2f}")
    print(f"max_latency_ms={max(summary_latencies):.2f}")
    print(f"p95_latency_ms={p95:.2f}")
    if args.save:
        print(f"saved={args.save}")
    if args.log_csv:
        print(f"csv={args.log_csv}")


def main() -> None:
    args = parse_args()
    model = YOLO(args.model)

    if not args.source.isdigit() and is_image_file(args.source):
        run_image(args, model)
    else:
        run_stream(args, model)


if __name__ == "__main__":
    main()
