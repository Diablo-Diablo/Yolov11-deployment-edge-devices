from pathlib import Path
import json
import time

from ultralytics import YOLO

ROOT = Path(__file__).resolve().parents[1]
IMAGE = ROOT / "countach.jpg"
OUT_DIR = ROOT / "runs" / "yolo11_baseline"
MODELS_DIR = ROOT / "models" / "yolo11"
MODELS_DIR.mkdir(parents=True, exist_ok=True)
OUT_DIR.mkdir(parents=True, exist_ok=True)

MODEL_NAMES = ["yolo11n.pt", "yolo11s.pt"]
summary = []

for model_name in MODEL_NAMES:
    model_path = MODELS_DIR / model_name
    model_ref = str(model_path) if model_path.exists() else model_name
    model = YOLO(model_ref)

    if not model_path.exists():
        downloaded = Path(model.ckpt_path) if getattr(model, "ckpt_path", None) else None
        if downloaded and downloaded.exists():
            model_path.write_bytes(downloaded.read_bytes())

    # warmup
    model.predict(source=str(IMAGE), imgsz=640, device="cpu", verbose=False)

    timings = []
    last_result = None
    for _ in range(5):
        start = time.perf_counter()
        results = model.predict(source=str(IMAGE), imgsz=640, device="cpu", verbose=False)
        timings.append((time.perf_counter() - start) * 1000)
        last_result = results[0]

    avg_ms = sum(timings) / len(timings)
    boxes = last_result.boxes
    detections = []
    for box in boxes:
        cls_id = int(box.cls.item())
        detections.append({
            "class_id": cls_id,
            "class_name": model.names.get(cls_id, str(cls_id)),
            "confidence": round(float(box.conf.item()), 4),
            "xyxy": [round(float(v), 2) for v in box.xyxy[0].tolist()],
        })

    save_dir = OUT_DIR / model_name.replace(".pt", "")
    last_result.save(filename=str(save_dir / "countach_detected.jpg"))

    actual_model_path = model_path if model_path.exists() else Path(getattr(model, "ckpt_path", ""))
    size_mb = actual_model_path.stat().st_size / (1024 * 1024) if actual_model_path.exists() else None
    summary.append({
        "model": model_name,
        "model_path": str(actual_model_path),
        "model_size_mb": round(size_mb, 2) if size_mb is not None else None,
        "image": str(IMAGE),
        "imgsz": 640,
        "device": "cpu",
        "runs": len(timings),
        "avg_latency_ms_python_e2e": round(avg_ms, 2),
        "min_latency_ms_python_e2e": round(min(timings), 2),
        "max_latency_ms_python_e2e": round(max(timings), 2),
        "speed_breakdown_ms": {k: round(float(v), 2) for k, v in last_result.speed.items()},
        "num_detections": len(detections),
        "detections": detections,
        "visualization": str(save_dir / "countach_detected.jpg"),
    })

summary_path = OUT_DIR / "summary.json"
summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
print(json.dumps(summary, indent=2))
print(f"Saved summary to {summary_path}")
