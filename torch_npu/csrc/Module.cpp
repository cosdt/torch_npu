#include <ATen/Parallel.h>
#include <Python.h>
#include <torch/csrc/profiler/python/combined_traceback.h>
#include <torch/csrc/utils.h>

#include "torch_npu/csrc/core/python_tensor.h"
#include "torch_npu/csrc/npu/Device.h"
#include "torch_npu/csrc/npu/Event.h"
#include "torch_npu/csrc/npu/Init.h"
#include "torch_npu/csrc/npu/Lock.h"
#include "torch_npu/csrc/npu/Memory.h"
#include "torch_npu/csrc/npu/Stream.h"

PyObject* module;
static std::vector<PyMethodDef> methods;

extern "C" C10_EXPORT PyObject* initModule();
PyObject* initModule() {
  at::internal::lazy_init_num_threads();

  THPUtils_addPyMethodDefs(methods, torch::backend::init::python_functions());
  THPUtils_addPyMethodDefs(methods, torch::backend::lock::python_functions());
  THPUtils_addPyMethodDefs(methods, torch::backend::device::python_functions());
  THPUtils_addPyMethodDefs(methods, torch::backend::memory::python_functions());
  THPUtils_addPyMethodDefs(methods, torch::backend::stream::python_functions());
  THPUtils_addPyMethodDefs(methods, torch::backend::tensor::python_functions());

  static struct PyModuleDef torchnpu_module = {
      PyModuleDef_HEAD_INIT, "torch_npu._C", nullptr, -1, methods.data()};
  module = PyModule_Create(&torchnpu_module);

  THNPStream_init(module);
  THNPEvent_init(module);

  RegisterNPUDeviceProperties(module);
  BindGetDeviceProperties(module);
  torch::installCapturedTracebackPython();
  return module;
}
