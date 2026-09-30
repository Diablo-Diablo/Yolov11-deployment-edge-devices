# YOLOv11 Baseline Deployment Test

## 1. Test Scope

本次测试用于建立 YOLOv11n 在 Windows CPU 环境下的基础部署结果，并初步比较 PyTorch checkpoint (`.pt`) 与 ONNX (`.onnx`) 两种模型格式的推理表现。

本次测试尚未涉及：

- 目标硬件 NPU 部署；
- RKNN、TensorRT 或其他硬件专用 runtime；
- INT8 量化；
- 目标场景数据集上的 mAP 评估；
- 摄像头实时输入和完整端到端视频 pipeline。

## 2. Test Environment

| Item | Value |
|---|---|
| OS | Windows 11 |
| CPU | Intel Core Ultra 5 225H |
| Python | 3.10.20 |
| PyTorch | 2.13.0+cpu |
| Ultralytics | 8.4.166 |
| ONNX Runtime | 1.23.2 |
| ONNX execution provider | `CPUExecutionProvider` |
| Model | YOLOv11n pretrained COCO model |
| Input image | `calib_91.jpg` |
| Input resolution used for comparison | `640 x 640` |
| Device argument | `cpu` |

## 3. Model Files

The `.pt` model was downloaded automatically by Ultralytics when the model name was passed to the CLI and the file was not found locally. It was not exported by a project training script.

The ONNX model was exported from the `.pt` model using Ultralytics.

```text
yolo11n.pt
    -> Ultralytics ONNX export
    -> yolo11n.onnx
```

The two files represent the same YOLOv11n model in different formats:

- `.pt`: PyTorch / Ultralytics checkpoint;
- `.onnx`: ONNX computation graph with model weights, suitable for ONNX Runtime and further backend conversion.

For the exported model, Ultralytics reported:

```text
Layers:     100
Parameters: 2,616,248
GFLOPs:     6.5
Output:     (1, 84, 8400)
```

## 4. CLI Commands

### 4.1 PyTorch `.pt` inference

The square-input comparison was run with:

```powershell
& "C:\Users\sujin_cyjip4t\.conda\envs\edgeai\Scripts\yolo.exe" predict `
    model="yolo11\yolo11n.pt" `
    source="calib_91.jpg" `
    imgsz=640 `
    rect=False `
    device=cpu
```

### 4.2 ONNX inference

```powershell
& "C:\Users\sujin_cyjip4t\.conda\envs\edgeai\Scripts\yolo.exe" predict `
    model="yolo11\yolo11n.onnx" `
    source="calib_91.jpg" `
    imgsz=640 `
    device=cpu
```

The ONNX run explicitly reported:

```text
Loading yolo11\yolo11n.onnx for ONNX Runtime inference...
Using ONNX Runtime 1.23.2 with CPUExecutionProvider
```

## 5. Detection Results

Both runs produced the same high-level detection count:

```text
7 persons, 2 trains
```

The image is a Lisbon tram street scene. The pretrained COCO model identifies the trams under the general `train` class. It does not distinguish the special class `tram` because the standard COCO label set does not provide that fine-grained class for this image.

The annotated output images were saved by Ultralytics under directories similar to:

```text
runs\detect\predict-4
runs\detect\predict-5
```

The exact directory depends on the order in which the CLI commands were run.

## 6. Performance Comparison

Both measurements below used a square input of `640 x 640`.

| Stage | PyTorch `.pt` | ONNX `.onnx` |
|---|---:|---:|
| Preprocess | 1.7 ms | 101.2 ms |
| Inference | 57.4 ms | 48.3 ms |
| Postprocess | 1.2 ms | 94.1 ms |
| Sum of reported stages | 60.3 ms | 243.6 ms |
| Approximate pipeline FPS | 16.58 FPS | 4.11 FPS |

Calculations:

```text
PyTorch total:
1.7 + 57.4 + 1.2 = 60.3 ms
1000 / 60.3 = approximately 16.58 FPS

ONNX total:
101.2 + 48.3 + 94.1 = 243.6 ms
1000 / 243.6 = approximately 4.11 FPS
```

Relative comparison:

- ONNX forward inference was approximately 15.9% faster than the `.pt` run:

  ```text
  57.4 ms -> 48.3 ms
  ```

- The complete reported ONNX pipeline was approximately 4.04 times slower than the `.pt` run:

  ```text
  243.6 ms / 60.3 ms = approximately 4.04
  ```

- The ONNX preprocessing and postprocessing times were much higher in this single-image CLI run. Therefore, the complete pipeline result should not be treated as a final ONNX performance benchmark yet.

## 7. YOLO Input and Output Format

### 7.1 Model input

After preprocessing, the model receives a tensor with shape:

```text
(1, 3, 640, 640)
```

The dimensions are:

```text
1     batch size
3     RGB channels
640   image height
640   image width
```

Typical preprocessing is:

```text
image
    -> resize / letterbox
    -> RGB conversion
    -> normalize pixel values
    -> HWC to CHW
    -> add batch dimension
    -> model input
```

### 7.2 Raw model output

For the COCO pretrained model with 80 classes, the exported model reports:

```text
(1, 84, 8400)
```

This can be interpreted as:

```text
1       batch size
84      prediction values per candidate
8400    candidate locations
```

For YOLOv11 in this export:

```text
84 = 4 box values + 80 class scores
```

The 4 box values represent the predicted bounding box parameters, commonly decoded as:

```text
x_center, y_center, width, height
```

The 80 class scores correspond to the COCO classes. The `8400` candidate locations come from three feature-map scales:

```text
80 x 80 = 6400
40 x 40 = 1600
20 x 20 = 400
--------------------------------
Total    = 8400
```

The raw tensor is not a finished image and does not directly contain the text `7 persons, 2 trains`.

### 7.3 Postprocessing

Ultralytics converts the raw tensor into final detections through a process similar to:

```text
raw output
    -> reshape / transpose
    -> decode bounding boxes
    -> select class scores
    -> confidence thresholding
    -> convert box format
    -> Non-Maximum Suppression (NMS)
    -> map coordinates back to the original image
    -> draw boxes and labels
```

A final detection can be represented conceptually as:

```text
{
    class_id: 0,
    class_name: "person",
    confidence: 0.82,
    xyxy: [x1, y1, x2, y2]
}
```

The saved annotated image is produced by the postprocessing and visualization code. The ONNX model itself outputs numerical tensors, not a rendered image.

## 8. Interpretation of the Current Results

The current test confirms the following:

1. The official YOLOv11n `.pt` checkpoint can run successfully on the CPU.
2. The `.pt` model can be exported to ONNX.
3. The exported `.onnx` model can run with ONNX Runtime.
4. Both formats produce the same high-level result on this image: 7 persons and 2 trains.
5. The ONNX forward pass is faster than the PyTorch forward pass in this test.
6. The current Ultralytics ONNX CLI run has unusually large preprocessing and postprocessing costs.

The current result does **not** yet prove:

- that ONNX is generally slower than PyTorch;
- that the ONNX model is unsuitable for deployment;
- that INT8 quantization will improve the complete pipeline;
- that the model meets the remote-driving application requirements;
- that the model has acceptable detection accuracy on the sponsor's data.

## 9. Limitations and Next Steps

The comparison is based on one image and one CPU environment. A more reliable benchmark should:

1. Use a set of representative images rather than one image.
2. Include warm-up iterations.
3. Run multiple measured iterations.
4. Report mean, standard deviation, minimum, maximum, and percentile latency.
5. Compare the same preprocessing and postprocessing implementation.
6. Verify bounding-box coordinates and confidence values, not only object counts.
7. Measure model file size, peak RAM, CPU utilization, and power where possible.
8. Establish an accuracy baseline using a labeled detection dataset.
9. Only then evaluate FP16 or INT8 quantization.
10. After the target hardware is confirmed, convert and benchmark the model using the corresponding hardware runtime.

The next conceptual step is to inspect the raw YOLO output tensor and understand how it becomes multiple final detections. The next engineering step is to establish a repeatable benchmark before attempting quantization.
