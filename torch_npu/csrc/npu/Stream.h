#pragma once

#include <torch/csrc/Stream.h>
#include <torch/csrc/python_headers.h>
#include "csrc/core/Macros.h"
#include "csrc/npu/NPUStream.h"

struct THNPStream : THPStream {
  c10::npu::NPUStream npu_stream;
};

TORCH_BACKEND_API void THNPStream_init(PyObject* module);
TORCH_BACKEND_API PyMethodDef* THNPModule_stream_methods();
