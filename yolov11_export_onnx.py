from pathlib import Path
import json

from ultralytics import YOLO

ROOT = Path(__file__).resolve().parents[1]
MODELS_DIR = ROOT / "models" / "yolo11"
OUT_DIR = ROOT / "runs" / "yolo11_export"
OUT_DIR.mkdir(parents=True, exist_ok=True)

summary = []
for model_file in [MODELS_DIR / "yolo11n.pt", MODELS_DIR / "yolo11s.pt"]:
    model = YOLO(str(model_file))
    exported = model.export(format="onnx", imgsz=640, opset=12, simplify=False, dynamic=False, device="cpu")
    exported_path = Path(exported)
    target_path = MODELS_DIR / exported_path.name
    if exported_path.resolve() != target_path.resolve():
        target_path.write_bytes(exported_path.read_bytes())
    summary.append({
        "source": str(model_file),
        "source_size_mb": round(model_file.stat().st_size / (1024 * 1024), 2),
        "onnx": str(target_path),
        "onnx_size_mb": round(target_path.stat().st_size / (1024 * 1024), 2),
        "imgsz": 640,
        "opset": 12,
        "dynamic": False,
    })

summary_path = OUT_DIR / "onnx_export_summary.json"
summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
print(json.dumps(summary, indent=2))
print(f"Saved summary to {summary_path}")
