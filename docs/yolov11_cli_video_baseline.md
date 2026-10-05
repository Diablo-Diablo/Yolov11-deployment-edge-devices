# YOLOv11 CLI Video Baseline

## Environment

| Item | Value |
|---|---|
| OS | Windows 11 |
| CPU | Intel Core Ultra 5 225H |
| Python | 3.10.20 |
| PyTorch | 2.13.0+cpu |
| Ultralytics | 8.4.166 |
| ONNX Runtime | 1.23.2 |
| Device | CPU |
| Video | `assets/opencv_sample.avi` |
| Video info | 768x576, 25 FPS, 100 frames, about 4 seconds |

## Test Scope

This note records quick CLI baseline results for YOLOv11 video inference. The goal is to compare `YOLOv11n` and `YOLOv11s` in `.pt` and `.onnx` formats before writing a custom video inference script.

The commands were run through Ultralytics CLI, for example:

```powershell
yolo predict model=yolo11\yolo11n.pt source=assets\opencv_sample.avi imgsz=640 device=cpu
```

## Baseline Results

| Model | Format | Input Shape | Preprocess | Inference | Postprocess | Total | Estimated FPS | Last-frame Observation |
|---|---|---|---:|---:|---:|---:|---:|---|
| YOLOv11n | `.pt` | 480x640 | 1.1 ms | 22.4 ms | 0.7 ms | 24.2 ms | 41.3 FPS | Fastest run; some low-resolution objects may be misclassified |
| YOLOv11s | `.pt` | 480x640 | 1.0 ms | 43.0 ms | 0.6 ms | 44.6 ms | 22.4 FPS | 7 persons, 1 car, 1 truck |
| YOLOv11n | `.onnx` | 640x640 | 83.4 ms | 24.0 ms | 56.4 ms | 163.8 ms | 6.1 FPS | 6 persons, 1 car, 1 truck |
| YOLOv11s | `.onnx` | 640x640 | 78.8 ms | 83.5 ms | 58.6 ms | 220.9 ms | 4.5 FPS | 6 persons, 1 car, 1 truck, 1 traffic light, 1 kite |

`Total` is calculated as:

```text
preprocess + inference + postprocess
```

`Estimated FPS` is calculated as:

```text
1000 / Total(ms)
```

## Notes

- `YOLOv11n.pt` is the current best baseline for real-time video inference on CPU. It reached about 41 FPS on this short sample video.
- `YOLOv11s.pt` is slower, with inference latency roughly 1.9x that of `YOLOv11n.pt`, but may detect some objects more stably.
- The ONNX models ran successfully, but the Ultralytics ONNX CLI path showed large preprocessing and postprocessing overhead.
- ONNX CLI end-to-end speed should not be treated as final board-side deployment speed, because the final Rockchip deployment path will use RKNN rather than ONNX Runtime.
- For the next stage, use `YOLOv11n.pt` as the main video pipeline baseline and keep `YOLOv11s` as the larger-model comparison.

## Next Step

Write a simple custom `video_infer.py` script to control frame reading, display, saving, latency logging, and later multi-threading or frame-dropping strategies.
