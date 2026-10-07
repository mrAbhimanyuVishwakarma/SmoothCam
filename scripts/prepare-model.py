"""Prepares the person-segmentation model shipped with SmoothCam.

    python scripts/prepare-model.py

Source: MediaPipe Selfie Segmentation (landscape, 144x256), Apache-2.0, as
exported to ONNX by the onnx-community project on Hugging Face.

The export uses ONNX opset 14 and IR version 10. Windows ML as built into the
first Windows 11 releases only understands older models, so this script:
  * replaces HardSwish (opset 14+) with x * HardSigmoid(x), which is identical,
  * gives Resize the explicit (empty) roi/scales inputs opset 11-12 require,
  * marks the graph as opset 12 / IR version 7 (no other operator it uses
    changed meaning between 12 and 14),
  * fixes the batch size to 1 (static shapes load faster and more reliably),
then checks with onnxruntime that the result matches the original.

Needs: pip install onnx onnxruntime numpy
"""

import hashlib
import urllib.request
from pathlib import Path

import numpy as np
import onnx
from onnx import helper, numpy_helper

SOURCE_URL = ("https://huggingface.co/onnx-community/mediapipe_selfie_segmentation_landscape/"
              "resolve/main/onnx/model.onnx")
ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "assets" / "models" / "selfie_segmenter.onnx"


def replace_hardswish(model: onnx.ModelProto) -> None:
    nodes = []
    for node in model.graph.node:
        if node.op_type != "HardSwish":
            nodes.append(node)
            continue
        gate = node.output[0] + "_hardsigmoid"
        nodes.append(helper.make_node("HardSigmoid", [node.input[0]], [gate], alpha=1.0 / 6.0, beta=0.5,
                                      name=node.name + "_hardsigmoid"))
        nodes.append(helper.make_node("Mul", [node.input[0], gate], [node.output[0]], name=node.name + "_mul"))
    del model.graph.node[:]
    model.graph.node.extend(nodes)


def fix_resize(model: onnx.ModelProto) -> None:
    empty = numpy_helper.from_array(np.zeros((0,), dtype=np.float32), "resize_empty")
    model.graph.initializer.append(empty)
    for node in model.graph.node:
        if node.op_type == "Resize":
            inputs = list(node.input) + [""] * (4 - len(node.input))
            inputs[1] = inputs[1] or "resize_empty"  # roi
            inputs[2] = inputs[2] or "resize_empty"  # scales (sizes is used)
            del node.input[:]
            node.input.extend(inputs)


def fix_batch(model: onnx.ModelProto) -> None:
    for value in list(model.graph.input) + list(model.graph.output):
        dim = value.type.tensor_type.shape.dim[0]
        dim.Clear()
        dim.dim_value = 1


def main() -> None:
    cache = ROOT / "build" / "model-source.onnx"
    cache.parent.mkdir(parents=True, exist_ok=True)
    if not cache.exists():
        print("downloading", SOURCE_URL)
        urllib.request.urlretrieve(SOURCE_URL, cache)
    original = onnx.load(str(cache))

    model = onnx.load(str(cache))
    replace_hardswish(model)
    fix_resize(model)
    for opset in model.opset_import:
        if opset.domain in ("", "ai.onnx"):
            opset.version = 12
    fix_batch(model)
    model.ir_version = 7
    onnx.checker.check_model(model)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(model, str(OUT))

    # The converted model must give the same mask as the original.
    import onnxruntime as ort
    rng = np.random.default_rng(1)
    x = rng.random((1, 3, 144, 256), dtype=np.float32)
    a = ort.InferenceSession(original.SerializeToString()).run(None, {"pixel_values": x})[0]
    b = ort.InferenceSession(str(OUT)).run(None, {"pixel_values": x})[0]
    print("max difference:", float(np.abs(a - b).max()))
    assert np.allclose(a, b, atol=1e-4)

    digest = hashlib.sha256(OUT.read_bytes()).hexdigest()
    print(f"wrote {OUT} ({OUT.stat().st_size} bytes, opset 12, sha256 {digest})")


if __name__ == "__main__":
    main()
