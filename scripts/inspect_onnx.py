#!/usr/bin/env python3
"""Print ONNX model I/O metadata (names, shapes, types) for wiring the backend.

Usage (project-local env, never the system interpreter):
  uv venv && uv pip install onnxruntime     # or: python3 -m venv .venv && source .venv/bin/activate && pip install onnxruntime
  uv run python scripts/inspect_onnx.py <model.onnx>
Paste the full output back so the DeepFilterNet backend can be written
against the real tensor names — no guessing.
"""
import sys

import onnxruntime as ort


def main() -> None:
    path = sys.argv[1]
    sess = ort.InferenceSession(path, providers=["CPUExecutionProvider"])
    print("providers:", sess.get_providers())
    print("--- inputs ---")
    for i in sess.get_inputs():
        print(f"{i.name}  shape={i.shape}  type={i.type}")
    print("--- outputs ---")
    for o in sess.get_outputs():
        print(f"{o.name}  shape={o.shape}  type={o.type}")


if __name__ == "__main__":
    main()
