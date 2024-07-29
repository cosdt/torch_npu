#pragma once

#include "csrc/core/Macros.h"
#include "csrc/npu/NPUDeviceProp.h"

TORCH_BACKEND_API void RegisterNPUDeviceProperties(PyObject* module);
TORCH_BACKEND_API void BindGetDeviceProperties(PyObject* module);

PyObject* THNPModule_getDevice_wrap(PyObject* self);
PyObject* THNPModule_setDevice_wrap(PyObject* self, PyObject* arg);
