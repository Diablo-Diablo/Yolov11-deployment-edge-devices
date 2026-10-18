import time
import cv2
from ultralytics import YOLO


def main():
    # 1. 加载模型（你可以换成 yolo11n.pt 或你的 onnx）
    model_path = "yolo11/yolo11n.onnx"
    print(f"Loading model: {model_path}...")
    model = YOLO(model_path)

    # 2. 打开笔记本默认摄像头 (index 0)
    # 如果打不开，可以尝试换成 1
    cap = cv2.VideoCapture(0)

    if not cap.isOpened():
        print("Error: Could not open laptop webcam.")
        return

    print("Webcam opened successfully. Press 'q' to exit.")

    frame_count = 0
    fps_smooth = 0.0

    while True:
        ret, frame = cap.read()
        if not ret:
            print("Error: Failed to grab frame.")
            break

        start_time = time.perf_counter()

        # 3. 对当前帧进行推理
        # imgsz 可以设为 640 或 480，尺寸越小速度越快
        results = model.predict(source=frame, imgsz=480, device="cpu", verbose=False)

        inference_time = (time.perf_counter() - start_time) * 1000
        current_fps = 1000.0 / inference_time if inference_time > 0 else 0.0

        # 平滑 FPS 显示
        fps_smooth = 0.9 * fps_smooth + 0.1 * current_fps if fps_smooth > 0 else current_fps

        # 4. 获取检测结果并画框
        result = results[0]
        annotated_frame = result.plot()
        num_det = 0 if result.boxes is None else len(result.boxes)

        # 5. 在画面上打印统计信息
        info_text = f"Detections: {num_det} | Latency: {inference_time:.1f}ms | FPS: {fps_smooth:.1f}"
        cv2.putText(
            annotated_frame,
            info_text,
            (20, 40),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.7,
            (0, 255, 0),
            2,
            cv2.LINE_AA,
        )

        # 6. 显示实时画面
        cv2.imshow("YOLOv11 Webcam Live Inference", annotated_frame)

        # 按 'q' 键退出
        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

        frame_count += 1

    cap.release()
    cv2.destroyAllWindows()
    print("Webcam inference closed.")


if __name__ == "__main__":
    main()
