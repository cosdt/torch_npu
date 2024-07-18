#include <chrono>
#include <sstream>
#include <thread>
#include <unordered_map>

#include <torch/csrc/Exceptions.h>
#include <torch/csrc/Generator.h>
#include <torch/csrc/THP.h>
#include <torch/csrc/autograd/generated/VariableType.h>
#include <torch/csrc/profiler/python/combined_traceback.h>
#include <torch/csrc/utils/device_lazy_init.h>
#include <torch/csrc/utils/pybind.h>
#include <torch/csrc/utils/python_arg_parser.h>
#include <torch/csrc/utils/python_numbers.h>
#include <torch/csrc/utils/python_strings.h>

#include "csrc/npu/NPUGeneratorImpl.h"
#include "npu/aten/common/SetNpu.h"
#include "csrc/aten/generated/python_functions.h"
#include "npu/core/OverflowUtils.h"
#include "csrc/npu/NPUCachingAllocator.h"
#include "npu/core/NPUException.h"
#include "csrc/npu/NPUFunctions.h"
#include "csrc/npu/NPUStream.h"
#include "npu/core/NpuVariables.h"
#include "npu/core/register/OptionRegister.h"
#include "npu/acl/include/acl/acl.h"
#include "torch_npu/csrc/npu/Module.h"
#include "torch_npu/csrc/npu/NPUPluggableAllocator.h"
#include "torch_npu/csrc/npu/memory_snapshot.h"

void RegisterNpuPluggableAllocator(PyObject* module) {
  auto m = py::handle(module).cast<py::module>();

  py::class_<
      c10_npu::NPUCachingAllocator::NPUAllocator,
      std::shared_ptr<c10_npu::NPUCachingAllocator::NPUAllocator>>(
      m, "_npu_NPUAllocator");
  m.def("_npu_getAllocator", []() {
    return py::cast(torch::npu::NPUPluggableAllocator::getCurrentAllocator());
  });

  m.def(
      "_npu_changeCurrentAllocator",
      [](std::shared_ptr<c10_npu::NPUCachingAllocator::NPUAllocator>
             allocator) {
        torch::npu::NPUPluggableAllocator::changeCurrentAllocator(allocator);
      });
  py::class_<
      torch::npu::NPUPluggableAllocator::NPUPluggableAllocator,
      c10_npu::NPUCachingAllocator::NPUAllocator,
      std::shared_ptr<
          torch::npu::NPUPluggableAllocator::NPUPluggableAllocator>>(
      m, "_NPUPluggableAllocator")
      .def(
          "set_init_fn",
          [](torch::npu::NPUPluggableAllocator::NPUPluggableAllocator& self,
             uint64_t func_ptr) {
            using FuncType = void(int);
            std::function<FuncType> func =
                reinterpret_cast<FuncType*>(func_ptr);
            self.set_init_fn(func);
          })
      .def(
          "set_reset_fn",
          [](torch::npu::NPUPluggableAllocator::NPUPluggableAllocator& self,
             uint64_t func_ptr) {
            using FuncType = void(bool);
            std::function<FuncType> func =
                reinterpret_cast<FuncType*>(func_ptr);
            self.set_reset_fn(func);
          })
      .def(
          "set_memory_fraction_fn",
          [](torch::npu::NPUPluggableAllocator::NPUPluggableAllocator& self,
             uint64_t func_ptr) {
            using FuncType = void(double, int);
            std::function<FuncType> func =
                reinterpret_cast<FuncType*>(func_ptr);
            self.set_memory_fraction_fn(func);
          })
      .def(
          "set_base_alloc_fn",
          [](torch::npu::NPUPluggableAllocator::NPUPluggableAllocator& self,
             uint64_t func_ptr) {
            using FuncType = void*(void*, size_t*);
            std::function<FuncType> func =
                reinterpret_cast<FuncType*>(func_ptr);
            self.set_base_alloc_fn(func);
          })
      .def(
          "set_record_stream_fn",
          [](torch::npu::NPUPluggableAllocator::NPUPluggableAllocator& self,
             uint64_t func_ptr) {
            using FuncType = void(void*, aclrtStream);
            std::function<FuncType> func =
                reinterpret_cast<FuncType*>(func_ptr);
            self.set_record_stream_fn(func);
          })
      .def(
          "set_erase_stream_fn",
          [](torch::npu::NPUPluggableAllocator::NPUPluggableAllocator& self,
             uint64_t func_ptr) {
            using FuncType = void(void*, aclrtStream);
            std::function<FuncType> func =
                reinterpret_cast<FuncType*>(func_ptr);
            self.set_erase_stream_fn(func);
          });
  m.def("_npu_customAllocator", [](uint64_t malloc_ptr, uint64_t free_ptr) {
    using MallocFuncType = void*(size_t, int, aclrtStream);
    using FreeFuncType = void(void*, size_t, int, aclrtStream);
    std::function<MallocFuncType> malloc_fn =
        reinterpret_cast<MallocFuncType*>(malloc_ptr);
    std::function<FreeFuncType> free_fn =
        reinterpret_cast<FreeFuncType*>(free_ptr);
    return torch::npu::NPUPluggableAllocator::createCustomAllocator(
        malloc_fn, free_fn);
  });
}

static PyObject* THNPModule_initExtension(PyObject* self, PyObject* noargs) {
  HANDLE_TH_ERRORS {
    pybind11::gil_scoped_release no_gil;
    at::globalContext().lazyInitPrivateUse1();
  }
  auto m = THPObjectPtr(PyImport_ImportModule("torch.npu"));
  if (!m) {
    throw python_error();
  }

  auto set_module_attr = [&](const char* name, PyObject* v) {
    // PyObject_SetAttrString doesn't steal reference. So no need to incref.
    if (PyObject_SetAttrString(m, name, v) < 0) {
      throw python_error();
    }
  };
  c10::DeviceIndex num_npus = c10_npu::device_count();
  auto default_npu_generators = PyTuple_New(static_cast<Py_ssize_t>(num_npus));
  for (c10::DeviceIndex i = 0; i < num_npus; i++) {
    auto gen = at_npu::detail::getDefaultNPUGenerator(i);
    auto cast_gen = (THPGenerator*)THPGenerator_initDefaultGenerator(gen);
    // This reference is meant to be given away, so no need to incref here.
    PyTuple_SetItem(default_npu_generators, i, (PyObject*)cast_gen);
  }
  at_npu::autograd::generated::initialize_autogenerated_functions(m);
  set_module_attr("default_generators", default_npu_generators);

  Py_RETURN_NONE;
  END_HANDLE_TH_ERRORS
}

PyObject* THNPModule_getCurrentStream_wrap(
    PyObject* /* unused */,
    PyObject* device_index) {
  HANDLE_TH_ERRORS
  TORCH_CHECK(
      THPUtils_checkLong(device_index),
      "invalid argument to getCurrentStream",
      PTA_ERROR(ErrCode::PARAM));
  c10::DeviceIndex device = THPUtils_unpackDeviceIndex(device_index);
  auto stream = c10_npu::getCurrentNPUStream(device);
  PyObject* output_tuple = PyTuple_New(3);
  PyTuple_SetItem(
      output_tuple, 0, THPUtils_packInt64(static_cast<int64_t>(stream.id())));
  PyTuple_SetItem(
      output_tuple,
      1,
      THPUtils_packInt64(static_cast<int64_t>(stream.device_index())));
  PyTuple_SetItem(
      output_tuple,
      2,
      THPUtils_packInt64(static_cast<int64_t>(stream.device_type())));
  return output_tuple;
  END_HANDLE_TH_ERRORS
}

PyObject* THNPModule_getDefaultStream_wrap(
    PyObject* self /* unused */,
    PyObject* device_index) {
  HANDLE_TH_ERRORS
  TORCH_CHECK(
      THPUtils_checkLong(device_index),
      "invalid argument to getDefaultStream",
      PTA_ERROR(ErrCode::PARAM));
  c10::DeviceIndex device = THPUtils_unpackDeviceIndex(device_index);
  auto stream = c10_npu::getDefaultNPUStream(device);
  PyObject* output_tuple = PyTuple_New(3);
  PyTuple_SetItem(
      output_tuple, 0, THPUtils_packInt64(static_cast<int64_t>(stream.id())));
  PyTuple_SetItem(
      output_tuple,
      1,
      THPUtils_packInt64(static_cast<int64_t>(stream.device_index())));
  PyTuple_SetItem(
      output_tuple,
      2,
      THPUtils_packInt64(static_cast<int64_t>(stream.device_type())));
  return output_tuple;
  END_HANDLE_TH_ERRORS
}

PyObject* THNPModule_setStream_wrap(
    PyObject* self,
    PyObject* args,
    PyObject* kwargs) {
  HANDLE_TH_ERRORS
  int64_t stream_id = 0;
  int64_t device_index = 0;
  int64_t device_type = 0;

  // NOLINTNEXTLINE(modernize-avoid-c-arrays,cppcoreguidelines-avoid-c-arrays)
  constexpr const char* kwlist[] = {
      "stream_id", "device_index", "device_type", nullptr};
  if (!PyArg_ParseTupleAndKeywords(
          args,
          kwargs,
          "|LLL",
          const_cast<char**>(kwlist),
          &stream_id,
          &device_index,
          &device_type)) {
  }

  auto stream = c10_npu::NPUStream::unpack3(
      stream_id,
      static_cast<c10::DeviceIndex>(device_index),
      static_cast<c10::DeviceType>(device_type));

  auto device = c10_npu::current_device();
  if (device != stream.device_index()) {
    c10_npu::set_device(stream.device_index());
  }
  c10_npu::setCurrentNPUStream(stream);
  Py_RETURN_NONE;
  END_HANDLE_TH_ERRORS
}

PyObject* THNPModule_is_jit_compile_false_wrap(
    PyObject* self,
    PyObject* noargs) {
  HANDLE_TH_ERRORS
  pybind11::gil_scoped_release no_gil;
  static const std::string jit_compile_option_name = "jitCompile";
  auto option_value = c10_npu::option::GetOption(jit_compile_option_name);
  if (option_value.has_value() && (option_value.value() == "disable")) {
    Py_RETURN_TRUE;
  } else {
    Py_RETURN_FALSE;
  }
  END_HANDLE_TH_ERRORS
}

PyObject* THNPModule_setMemoryFraction(PyObject* _unused, PyObject* args) {
  HANDLE_TH_ERRORS
  PyObject* fraction_o = nullptr;
  PyObject* device_o = nullptr;
  if (!PyArg_ParseTuple(args, "OO", &fraction_o, &device_o)) {
    THPUtils_invalidArguments(
        args,
        nullptr,
        "set_memory_fraction",
        1,
        "(double fraction, int device);");
    return nullptr;
  }
  double fraction = PyFloat_AsDouble(fraction_o);
  int64_t device = PyLong_AsLongLong(device_o);

  c10_npu::NPUCachingAllocator::setMemoryFraction(fraction, device);
  END_HANDLE_TH_ERRORS
  Py_RETURN_NONE;
}

PyObject* THNPModule_emptyCache(PyObject* _unused, PyObject* noargs) {
  HANDLE_TH_ERRORS
  c10_npu::NPUCachingAllocator::emptyCache();
  END_HANDLE_TH_ERRORS
  Py_RETURN_NONE;
}

PyObject* THNPModule_memoryStats(PyObject* _unused, PyObject* arg) {
  HANDLE_TH_ERRORS
  TORCH_CHECK(
      THPUtils_checkLong(arg),
      "invalid argument to memory_allocated",
      PTA_ERROR(ErrCode::PARAM));
  const int device = (int)THPUtils_unpackLong(arg);

  using c10_npu::NPUCachingAllocator::DeviceStats;
  using c10_npu::NPUCachingAllocator::Stat;
  using c10_npu::NPUCachingAllocator::StatArray;
  using c10_npu::NPUCachingAllocator::StatType;

  const auto statToDict = [](const Stat& stat) {
    py::dict dict;

    dict["current"] = stat.current;
    dict["peak"] = stat.peak;
    dict["allocated"] = stat.allocated;
    dict["freed"] = stat.freed;
    return dict;
  };

  const auto statArrayToDict = [=](const StatArray& statArray) {
    const std::array<const char*, static_cast<size_t>(StatType::NUM_TYPES)>
        statTypeNames = {"all", "small_pool", "large_pool"};
    py::dict dict;
    for (size_t i = 0; i < statTypeNames.size(); ++i) {
      dict[statTypeNames[i]] = statToDict(statArray[i]);
    }
    return dict;
  };

  const DeviceStats stats =
      c10_npu::NPUCachingAllocator::getDeviceStats(device);

  py::dict result;
  result["num_alloc_retries"] = stats.num_alloc_retries;
  result["num_ooms"] = stats.num_ooms;
  result["max_split_size"] = stats.max_split_size;
  result["allocation"] = statArrayToDict(stats.allocation);
  result["segment"] = statArrayToDict(stats.segment);
  result["active"] = statArrayToDict(stats.active);
  result["inactive_split"] = statArrayToDict(stats.inactive_split);
  result["allocated_bytes"] = statArrayToDict(stats.allocated_bytes);
  result["reserved_bytes"] = statArrayToDict(stats.reserved_bytes);
  result["active_bytes"] = statArrayToDict(stats.active_bytes);
  result["inactive_split_bytes"] = statArrayToDict(stats.inactive_split_bytes);
  result["oversize_allocations"] = statToDict(stats.oversize_allocations);
  result["oversize_segments"] = statToDict(stats.oversize_segments);

  return result.release().ptr();
  END_HANDLE_TH_ERRORS
}

PyObject* THNPModule_resetAccumulatedMemoryStats(
    PyObject* _unused,
    PyObject* arg) {
  HANDLE_TH_ERRORS
  TORCH_CHECK(
      THPUtils_checkLong(arg),
      "invalid argument to reset_accumulated_memory_stats",
      PTA_ERROR(ErrCode::PARAM));
  const int device = (int)THPUtils_unpackLong(arg);
  c10_npu::NPUCachingAllocator::resetAccumulatedStats(device);
  END_HANDLE_TH_ERRORS
  Py_RETURN_NONE;
}

PyObject* THNPModule_resetPeakMemoryStats(PyObject* _unused, PyObject* arg) {
  HANDLE_TH_ERRORS
  TORCH_CHECK(
      THPUtils_checkLong(arg),
      "invalid argument to reset_peak_memory_stats",
      PTA_ERROR(ErrCode::PARAM));
  const int device = (int)THPUtils_unpackLong(arg);
  c10_npu::NPUCachingAllocator::resetPeakStats(device);
  END_HANDLE_TH_ERRORS
  Py_RETURN_NONE;
}

torch::CapturedTraceback* getFromContext(
    const std::shared_ptr<c10::GatheredContext>& x) {
  if (torch::CapturedTraceback* sc =
          dynamic_cast<torch::CapturedTraceback*>(x.get())) {
    return sc;
  }
  TORCH_CHECK(
      false,
      "attempting to gather stack context from the wrong StackContext type.");
}

PyObject* THNPModule_memorySnapshot(PyObject* _unused, PyObject* noargs) {
  HANDLE_TH_ERRORS

  using c10_npu::NPUCachingAllocator::BlockInfo;
  using c10_npu::NPUCachingAllocator::SegmentInfo;

  py::str device_s = "device";
  py::str address_s = "address";
  py::str total_size_s = "total_size";
  py::str allocated_size_s = "allocated_size";
  py::str active_size_s = "active_size";
  py::str requested_size_s = "requested_size";
  py::str stream_s = "stream";
  py::str segment_type_s = "segment_type";
  py::str large_s = "large";
  py::str small_s = "small";
  py::str size_s = "size";
  py::str state_s = "state";
  py::str active_allocated_s = "active_allocated";
  py::str active_pending_free_s = "active_pending_free";
  py::str inactive_s = "inactive";
  py::str addr_s = "addr";
  py::str cpp_frames_s = "cpp_frames";
  py::str blocks_s = "blocks";
  py::str is_expandable_s = "is_expandable";
  py::str frames_s = "frames";

  py::list empty_frames;
  std::vector<torch::CapturedTraceback*> to_gather_frames;
  std::vector<py::dict> to_gather_dest;

  auto add_frame_key = [&](const py::dict& d,
                           const std::shared_ptr<c10::GatheredContext>& ctx) {
    if (ctx) {
      auto sc = getFromContext(ctx);
      to_gather_frames.emplace_back(sc);
      to_gather_dest.emplace_back(d);
    } else {
      d[frames_s] = empty_frames;
    }
  };

  const auto segmentInfoToDict = [&](const SegmentInfo& segmentInfo) {
    py::dict segmentDict;
    segmentDict[device_s] = segmentInfo.device;
    segmentDict[address_s] = segmentInfo.address;
    segmentDict[total_size_s] = segmentInfo.total_size;
    segmentDict[allocated_size_s] = segmentInfo.allocated_size;
    segmentDict[active_size_s] = segmentInfo.active_size;
    segmentDict[requested_size_s] = segmentInfo.requested_size;
    // we want the python objects to pickle easily so use an int to
    // represent the stream rather than a torch.cuda.stream object
    segmentDict[stream_s] = int64_t(segmentInfo.stream);
    segmentDict[segment_type_s] = (segmentInfo.is_large ? large_s : small_s);
    segmentDict[is_expandable_s] = segmentInfo.is_expandable;
    add_frame_key(segmentDict, segmentInfo.context_when_allocated);

    auto address = segmentInfo.address;
    py::list blocks;
    for (const auto& blockInfo : segmentInfo.blocks) {
      py::dict blockDict;
      blockDict[address_s] = address;
      blockDict[size_s] = blockInfo.size;
      blockDict[requested_size_s] = blockInfo.requested_size;
      blockDict[state_s] =
          (blockInfo.allocated
               ? active_allocated_s
               : (blockInfo.active ? active_pending_free_s : inactive_s));
      add_frame_key(blockDict, blockInfo.context_when_allocated);
      blocks.append(blockDict);
      address += blockInfo.size;
    }
    segmentDict[blocks_s] = blocks;

    return segmentDict;
  };

  auto snapshot = c10_npu::NPUCachingAllocator::snapshot();
  py::list segments;

  for (const auto& segmentInfo : snapshot.segments) {
    segments.append(segmentInfoToDict(segmentInfo));
  }

  py::list traces;
  py::str action_s = "action";
  py::str alloc_s = "alloc";
  py::str free_requested_s = "free_requested";
  py::str free_completed_s = "free_completed";
  py::str segment_alloc_s = "segment_alloc";
  py::str segment_free_s = "segment_free";
  py::str segment_map_s = "segment_map";
  py::str segment_unmap_s = "segment_unmap";

  py::str snapshot_s = "snapshot";
  py::str oom_s = "oom";
  py::str device_free_s = "device_free";

  using namespace c10_npu::NPUCachingAllocator;

  auto action_to_str = [&](TraceEntry::Action action) {
    switch (action) {
      case TraceEntry::ALLOC:
        return alloc_s;
      case TraceEntry::FREE_REQUESTED:
        return free_requested_s;
      case TraceEntry::FREE_COMPLETED:
        return free_completed_s;
      case TraceEntry::SEGMENT_ALLOC:
        return segment_alloc_s;
      case TraceEntry::SEGMENT_FREE:
        return segment_free_s;
      case TraceEntry::OOM:
        return oom_s;
      case TraceEntry::SNAPSHOT:
        return snapshot_s;
      case TraceEntry::SEGMENT_UNMAP:
        return segment_unmap_s;
      case TraceEntry::SEGMENT_MAP:
        return segment_map_s;
      default:
        AT_ERROR("invalid TraceEntry action");
    }
    throw std::runtime_error("unreachable");
  };

  for (const auto& traceInfo : snapshot.device_traces) {
    py::list trace;
    for (const auto& te : traceInfo) {
      py::dict trace_entry;
      if (te.context_) {
        // without further compression frames can get really large on dump
        auto sc = getFromContext(te.context_);
        to_gather_frames.emplace_back(sc);
        to_gather_dest.emplace_back(trace_entry);
      }
      trace_entry[action_s] = action_to_str(te.action_);
      trace_entry[TraceEntry::OOM == te.action_ ? device_free_s : addr_s] =
          te.addr_;
      trace_entry[size_s] = te.size_;
      trace_entry[stream_s] = int64_t(te.stream_);
      trace.append(trace_entry);
    }
    traces.append(trace);
  }

  py::dict result;
  result["segments"] = segments;
  result["device_traces"] = traces;

  auto frames = torch::py_symbolize(to_gather_frames);
  for (auto i : c10::irange(frames.size())) {
    to_gather_dest.at(i)[frames_s] = frames.at(i);
  }
  return result.release().ptr();
  END_HANDLE_TH_ERRORS
}

PyObject* THNPModule_attachOutOfMemoryObserver(
    PyObject* _unused,
    PyObject* observer) {
  HANDLE_TH_ERRORS
  Py_XINCREF(observer);
  auto obs = [observer](
                 int64_t device,
                 int64_t alloc,
                 int64_t device_allocated,
                 int64_t device_free) {
    py::gil_scoped_acquire g;
    PyObject* result = PyObject_CallFunction(
        observer, "LLLL", device, alloc, device_allocated, device_free);
    if (!result) {
      throw py::error_already_set();
    }
    Py_XDECREF(result);
  };
  torch::utils::device_lazy_init(at::kPrivateUse1);
  c10_npu::NPUCachingAllocator::attachOutOfMemoryObserver(std::move(obs));
  Py_RETURN_NONE;
  END_HANDLE_TH_ERRORS
}

PyObject* THNPModule_npuCachingAllocator_raw_alloc(
    PyObject* _unused,
    PyObject* args) {
  HANDLE_TH_ERRORS
  PyObject* size_o = nullptr;
  PyObject* stream_o = nullptr;
  if (!PyArg_ParseTuple(args, "OO", &size_o, &stream_o)) {
    THPUtils_invalidArguments(
        args,
        nullptr,
        "caching_allocator_alloc",
        1,
        "(ssize_t size, intptr_t stream);");
    return nullptr;
  }
  ssize_t size = PyLong_AsSsize_t(size_o);
  aclrtStream stream = static_cast<aclrtStream>(PyLong_AsVoidPtr(stream_o));
  void* mem = c10_npu::NPUCachingAllocator::raw_alloc_with_stream(size, stream);
  return PyLong_FromVoidPtr(mem);
  END_HANDLE_TH_ERRORS
}

PyObject* THNPModule_npuCachingAllocator_raw_delete(
    PyObject* _unused,
    PyObject* obj) {
  HANDLE_TH_ERRORS
  void* mem_ptr = PyLong_AsVoidPtr(obj);
  c10_npu::NPUCachingAllocator::raw_delete(mem_ptr);
  Py_RETURN_NONE;
  END_HANDLE_TH_ERRORS
}

PyObject* THNPModule_getAllocatorBackend(PyObject* _unused, PyObject* noargs) {
  HANDLE_TH_ERRORS
  return THPUtils_packString(c10_npu::NPUCachingAllocator::name());
  END_HANDLE_TH_ERRORS
}

// We need to ensure that as long as a thread will NEVER loose the GIL as long
// as it holds the NPU mutex. Otherwise another thread might be scheduled and
// try to e.g. allocate a new tensor which will cause a deadlock. It's enough to
// have a single global, because it can be only set once (npuMutex is not
// recursive) by the thread that owns the mutex (obviously there can be only one
// such thread).
static PyGILState_STATE npuMutexGILState;

PyObject* THNPModule_npuLockMutex(PyObject* module, PyObject* noargs) {
  auto mutex = c10_npu::NPUCachingAllocator::getFreeMutex();
  // This has to be a busy loop because we **absolutely need to** hold the GIL
  // or it's a recipe for a deadlock otherwise (if we let other Python threads
  // run while we have the cudaMutex, but not the GIL, they might try to e.g.
  // free a CUDA tensor and acquire the cudaMutex without giving up the GIL,
  // because it happens deep within THC).
  while (true) {
    if (mutex->try_lock()) {
      break;
    }
    {
      pybind11::gil_scoped_release no_gil;
      std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
  }

  npuMutexGILState = PyGILState_Ensure();
  Py_RETURN_NONE;
}

PyObject* THNPModule_npuUnlockMutex(PyObject* module, PyObject* noargs) {
  auto mutex = c10_npu::NPUCachingAllocator::getFreeMutex();
  PyGILState_Release(npuMutexGILState);
  mutex->unlock();
  Py_RETURN_NONE;
}

PyObject* THNPModule_setOption_wrap(PyObject* self, PyObject* arg) {
  HANDLE_TH_ERRORS

  if (!PyDict_Check(arg)) {
    throw torch::TypeError(
        "npu option must be a dict." + PTA_ERROR(ErrCode::TYPE));
  }

  PyObject* key = nullptr;
  PyObject* value = nullptr;
  Py_ssize_t pos = 0;
  std::map<std::string, std::string> option;

  while (PyDict_Next(arg, &pos, &key, &value)) {
    if (key == nullptr || !PyUnicode_Check(key)) {
      throw torch::TypeError(
          "option name is nullptr or is not string." +
          PTA_ERROR(ErrCode::TYPE));
    }

    if (value == nullptr || !PyUnicode_Check(value)) {
      throw torch::TypeError(
          "option value is nullptr or is not string." +
          PTA_ERROR(ErrCode::TYPE));
    }

    const char* pKey = PyUnicode_AsUTF8(key);
    const char* pValue = PyUnicode_AsUTF8(value);
    option[pKey] = pValue;
  }
  torch::utils::device_lazy_init(at::kPrivateUse1);
  {
    pybind11::gil_scoped_release no_gil;
    c10_npu::option::SetOption(option);
  }
  Py_RETURN_NONE;
  END_HANDLE_TH_ERRORS
}

PyObject* THNPModule_set_run_yet_variable_to_false_wrap(
    PyObject* self,
    PyObject* noargs) {
  HANDLE_TH_ERRORS
  torch::utils::set_requires_device_init(at::DeviceType::PrivateUse1, true);
  Py_RETURN_NONE;
  END_HANDLE_TH_ERRORS
}

PyObject* THNPModule_npu_get_soc_version(PyObject* self, PyObject* noargs) {
  HANDLE_TH_ERRORS
  return PyLong_FromLong(static_cast<long>(c10_npu::GetSocVersion()));
  END_HANDLE_TH_ERRORS
}

PyObject* THNPModule_npu_is_support_inf_nan(PyObject* self, PyObject* noargs) {
  HANDLE_TH_ERRORS
  if (c10_npu::IsSupportInfNan()) {
    Py_RETURN_TRUE;
  } else {
    Py_RETURN_FALSE;
  }
  END_HANDLE_TH_ERRORS
}

PyObject* THNPModule_npu_is_bf16_supported(PyObject* self, PyObject* noargs) {
  HANDLE_TH_ERRORS
  if (c10_npu::IsBF16Supported()) {
    Py_RETURN_TRUE;
  } else {
    Py_RETURN_FALSE;
  }
  END_HANDLE_TH_ERRORS
}

PyObject* THNPModule_enable_overflow_npu(PyObject* self, PyObject* noargs) {
  HANDLE_TH_ERRORS
  torch_npu::utils::OverflowUtil::GetInstance()->EnableOverflowNpu();
  Py_RETURN_NONE;
  END_HANDLE_TH_ERRORS
}

PyObject* THNPModule_check_overflow_npu(PyObject* self, PyObject* noargs) {
  HANDLE_TH_ERRORS
  auto has_overflow =
      torch_npu::utils::OverflowUtil::GetInstance()->CheckOverflowNpu();
  if (has_overflow) {
    Py_RETURN_TRUE;
  } else {
    Py_RETURN_FALSE;
  }
  END_HANDLE_TH_ERRORS
}

PyObject* THNPModule_clear_overflow_npu(PyObject* self, PyObject* noargs) {
  HANDLE_TH_ERRORS
  torch_npu::utils::OverflowUtil::GetInstance()->ClearOverflowNpu();
  Py_RETURN_NONE;
  END_HANDLE_TH_ERRORS
}

PyObject* THNPModule_getOption_wrap(PyObject* self, PyObject* option_type) {
  HANDLE_TH_ERRORS
  TORCH_CHECK(
      THPUtils_checkString(option_type),
      "invalid argument to option_type,option_type must string!",
      PTA_ERROR(ErrCode::PARAM));
  std::string option_type_str = THPUtils_unpackString(option_type);
  auto option_key = c10_npu::option::GetOption(option_type_str);
  if (option_key.has_value()) {
    return PyBytes_FromString(option_key.value().c_str());
  }
  Py_RETURN_NONE;
  END_HANDLE_TH_ERRORS
}

PyObject* THNPModule_npu_set_sync_debug_mode(PyObject* _unused, PyObject* arg) {
  HANDLE_TH_ERRORS
  TORCH_NPU_WARN_ONCE(
      "Synchronization debug mode is a prototype feature and does not yet detect all "
      "synchronizing operations");
  TORCH_CHECK(
      THPUtils_checkLong(arg),
      "invalid argument to set_sync_debug_mode, debug_mode type must long",
      PTA_ERROR(ErrCode::PARAM));
  int64_t debug_mode = THPUtils_unpackLong(arg);
  TORCH_CHECK(
      debug_mode >= 0 && debug_mode <= 2,
      "invalid value of debug_mode, expected one of 0,1,2",
      PTA_ERROR(ErrCode::VALUE));
  c10_npu::SyncDebugMode level;
  switch (debug_mode) {
    case 0:
      level = c10_npu::SyncDebugMode::L_DISABLED;
      break;
    case 1:
      level = c10_npu::SyncDebugMode::L_WARN;
      break;
    case 2:
      level = c10_npu::SyncDebugMode::L_ERROR;
      break;
    default:
      level = c10_npu::SyncDebugMode::L_DISABLED;
      break;
  }
  c10_npu::warning_state().set_sync_debug_mode(level);
  Py_RETURN_NONE;
  END_HANDLE_TH_ERRORS
}

PyObject* THNPModule_npu_get_sync_debug_mode(PyObject* self, PyObject* noargs) {
  HANDLE_TH_ERRORS
  auto debug_mode = c10_npu::warning_state().get_sync_debug_mode();
  switch (debug_mode) {
    case c10_npu::SyncDebugMode::L_DISABLED:
      return THPUtils_packInt32(0);
    case c10_npu::SyncDebugMode::L_WARN:
      return THPUtils_packInt32(1);
    case c10_npu::SyncDebugMode::L_ERROR:
      return THPUtils_packInt32(2);
    default:
      return THPUtils_packInt32(-1); // can't happen
  }
  END_HANDLE_TH_ERRORS
}

PyObject* THNPModule_tensor_construct_from_storage(
    PyObject* self,
    PyObject* args) {
  HANDLE_TH_ERRORS
  static torch::PythonArgParser parser(
      {
          "set_storage_with_format_(Storage source)",
      },
      /* traceable= */ false);

  torch::ParsedArgs<1> parsed_args;
  auto _r = parser.parse(args, nullptr, parsed_args);

  at::ScalarType storage_scalar_type;
  bool is_typed_storage = true;
  c10::Storage storage = _r.storage(0, storage_scalar_type, is_typed_storage);
  return THPVariable_Wrap(
      at_npu::native::set_tensor_with_storage_format(storage));

  END_HANDLE_TH_ERRORS
}

static struct PyMethodDef THNPModule_methods[] = {
    {"_npu_init", (PyCFunction)THNPModule_initExtension, METH_NOARGS, nullptr},
    {"_npu_set_run_yet_variable_to_false",
     (PyCFunction)THNPModule_set_run_yet_variable_to_false_wrap,
     METH_NOARGS,
     nullptr},
    {"_npu_getCurrentStream",
     (PyCFunction)THNPModule_getCurrentStream_wrap,
     METH_O,
     nullptr},
    {"_npu_getDefaultStream",
     (PyCFunction)THNPModule_getDefaultStream_wrap,
     METH_O,
     nullptr},
    {"_npu_setStream",
     (PyCFunction)THNPModule_setStream_wrap,
     METH_VARARGS | METH_KEYWORDS,
     nullptr},
    {"_npu_is_jit_compile_false",
     (PyCFunction)THNPModule_is_jit_compile_false_wrap,
     METH_NOARGS,
     nullptr},
    {"_npu_setMemoryFraction",
     (PyCFunction)THNPModule_setMemoryFraction,
     METH_VARARGS,
     nullptr},
    {"_npu_emptyCache",
     (PyCFunction)THNPModule_emptyCache,
     METH_NOARGS,
     nullptr},
    {"_npu_memoryStats", (PyCFunction)THNPModule_memoryStats, METH_O, nullptr},
    {"_npu_resetAccumulatedMemoryStats",
     (PyCFunction)THNPModule_resetAccumulatedMemoryStats,
     METH_O,
     nullptr},
    {"_npu_resetPeakMemoryStats",
     (PyCFunction)THNPModule_resetPeakMemoryStats,
     METH_O,
     nullptr},
    {"_npu_memorySnapshot",
     (PyCFunction)THNPModule_memorySnapshot,
     METH_NOARGS,
     nullptr},
    {"_npu_attach_out_of_memory_observer",
     THNPModule_attachOutOfMemoryObserver,
     METH_O,
     nullptr},
    {"_npu_npuCachingAllocator_raw_alloc",
     (PyCFunction)THNPModule_npuCachingAllocator_raw_alloc,
     METH_VARARGS,
     nullptr},
    {"_npu_npuCachingAllocator_raw_delete",
     (PyCFunction)THNPModule_npuCachingAllocator_raw_delete,
     METH_O,
     nullptr},
    {"_npu_getAllocatorBackend",
     (PyCFunction)THNPModule_getAllocatorBackend,
     METH_NOARGS,
     nullptr},
    {"_npu_lock_mutex",
     (PyCFunction)THNPModule_npuLockMutex,
     METH_NOARGS,
     nullptr},
    {"_npu_unlock_mutex",
     (PyCFunction)THNPModule_npuUnlockMutex,
     METH_NOARGS,
     nullptr},
    {"_npu_setOption", (PyCFunction)THNPModule_setOption_wrap, METH_O, nullptr},
    {"_npu_get_soc_version",
     (PyCFunction)THNPModule_npu_get_soc_version,
     METH_NOARGS,
     nullptr},
    {"_enable_overflow_npu",
     (PyCFunction)THNPModule_enable_overflow_npu,
     METH_NOARGS,
     nullptr},
    {"_npu_is_support_inf_nan",
     (PyCFunction)THNPModule_npu_is_support_inf_nan,
     METH_NOARGS,
     nullptr},
    {"_npu_is_bf16_supported",
     (PyCFunction)THNPModule_npu_is_bf16_supported,
     METH_NOARGS,
     nullptr},
    {"_check_overflow_npu",
     (PyCFunction)THNPModule_check_overflow_npu,
     METH_NOARGS,
     nullptr},
    {"_clear_overflow_npu",
     (PyCFunction)THNPModule_clear_overflow_npu,
     METH_NOARGS,
     nullptr},
    {"_npu_getOption", (PyCFunction)THNPModule_getOption_wrap, METH_O, nullptr},
    {"_npu_set_sync_debug_mode",
     (PyCFunction)THNPModule_npu_set_sync_debug_mode,
     METH_O,
     nullptr},
    {"_npu_get_sync_debug_mode",
     (PyCFunction)THNPModule_npu_get_sync_debug_mode,
     METH_NOARGS,
     nullptr},
    {"_tensor_construct_from_storage",
     (PyCFunction)THNPModule_tensor_construct_from_storage,
     METH_VARARGS,
     nullptr},
    {nullptr}};

TORCH_NPU_API PyMethodDef* THNPModule_get_methods() {
  return THNPModule_methods;
}
