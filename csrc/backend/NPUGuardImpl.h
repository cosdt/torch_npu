#pragma once

#include <c10/core/impl/DeviceGuardImplInterface.h>
#include <c10/macros/Macros.h>
#include <cassert>

#include "csrc/aten/generated/NPUNativeFunctions.h"
#include "csrc/backend/NPUFunctions.h"
#include "csrc/backend/NPUStream.h"
#include "csrc/core/guard/PrivateUse1GuardImpl.h"

// TODO(FFFrog):
// Remove later
#include "core/NPUException.h"

namespace c10::backend {
namespace impl {
struct NPUGuardImpl final : public PrivateUse1GuardImpl {
  NPUGuardImpl() = default;

  explicit NPUGuardImpl(c10::DeviceType t) {
    TORCH_INTERNAL_ASSERT(
        t == static_type,
        "DeviceType must be 'c10::DeviceType::PrivateUse1'. Actual DeviceType is: ",
        t,
        PTA_ERROR(ErrCode::PARAM));
  }
  c10::Device exchangeDevice(c10::Device d) const override {
    TORCH_INTERNAL_ASSERT(
        d.type() == c10::DeviceType::PrivateUse1,
        "DeviceType must be NPU. Actual DeviceType is: ",
        d.type(),
        PTA_ERROR(ErrCode::PARAM));
    c10::Device old_device = getDevice();
    if (old_device.index() != d.index()) {
      NPU_CHECK_ERROR(c10::backend::SetDevice(d.index()));
    }
    return old_device;
  }
  c10::Device getDevice() const override {
    c10::DeviceIndex device = 0;
    NPU_CHECK_ERROR(c10::backend::GetDevice(&device));
    return c10::Device(c10::DeviceType::PrivateUse1, device);
  }
  void setDevice(c10::Device d) const override {
    TORCH_INTERNAL_ASSERT(
        d.type() == c10::DeviceType::PrivateUse1,
        "DeviceType must be 'c10::DeviceType::PrivateUse1'. Actual DeviceType is: ",
        d.type(),
        PTA_ERROR(ErrCode::PARAM));
    NPU_CHECK_ERROR(c10::backend::SetDevice(d.index()));
  }
  void uncheckedSetDevice(c10::Device d) const noexcept override {
    NPU_CHECK_WARN(c10::backend::SetDevice(d.index()));
  }
  c10::Stream getStream(c10::Device d) const noexcept override {
    return c10::backend::getCurrentNPUStream(d.index()).unwrap();
  }
  c10::Stream getDefaultStream(c10::Device d) const override {
    return c10::backend::getDefaultNPUStream(d.index());
  }
  c10::Stream getStreamFromGlobalPool(
      c10::Device d,
      bool isHighPriority = false) const override {
    return c10::backend::getStreamFromPool(isHighPriority, d.index());
  }
  // NB: These do NOT set the current device
  c10::Stream exchangeStream(c10::Stream s) const noexcept override {
    NPUStream cs(s);
    auto old_stream = c10::backend::getCurrentNPUStream(s.device().index());
    c10::backend::setCurrentNPUStream(cs);
    return old_stream.unwrap();
  }
  c10::DeviceIndex deviceCount() const noexcept override {
    static c10::DeviceIndex count = c10::backend::device_count();
    return count;
  }

  // Event-related functions
  void createEvent(aclrtEvent* acl_event, const c10::EventFlag flag) const {
    auto flag_ = ACL_EVENT_SYNC;
    NPU_CHECK_ERROR(aclrtCreateEventWithFlag(acl_event, flag_));
  }

  void destroyEvent(void* event, const c10::DeviceIndex device_index)
      const noexcept override {
    if (!event)
      return;
    auto acl_event = static_cast<aclrtEvent>(event);
    c10::DeviceIndex orig_device{-1};
    NPU_CHECK_WARN(c10::backend::GetDevice(&orig_device));
    NPU_CHECK_WARN(c10::backend::SetDevice(device_index));
    NPU_CHECK_WARN(aclrtDestroyEvent(acl_event));
    NPU_CHECK_WARN(c10::backend::SetDevice(orig_device));
  }

  void record(
      void** event,
      const c10::Stream& stream,
      const c10::DeviceIndex device_index,
      const c10::EventFlag flag) const override {
    TORCH_CHECK(
        device_index == -1 || device_index == stream.device_index(),
        "Event device index ",
        device_index,
        " does not match recording stream's device index ",
        stream.device_index(),
        ".",
        PTA_ERROR(ErrCode::PARAM));

    aclrtEvent npu_event = static_cast<aclrtEvent>(*event);
    NPUStream npu_stream{stream};

    // Moves to stream's device to record
    const auto orig_device = getDevice();
    setDevice(stream.device());

    // Creates the event (lazily)
    if (!npu_event) {
      auto flag_ = ACL_EVENT_SYNC;
      NPU_CHECK_ERROR(aclrtCreateEventWithFlag(&npu_event, flag_));
    }
    NPU_CHECK_ERROR(aclrtRecordEvent(npu_event, npu_stream));
    // Makes the void* point to the (possibly just allocated) NPU event
    *event = npu_event;

    // Resets device
    setDevice(orig_device);
  }

  void block(void* event, const c10::Stream& stream) const override {
    if (!event)
      return;
    aclrtEvent npu_event = static_cast<aclrtEvent>(event);
    NPUStream npu_stream{stream};
    const auto orig_device = getDevice();
    setDevice(stream.device());
    NPU_CHECK_ERROR(aclrtStreamWaitEvent(npu_stream, npu_event));
    setDevice(orig_device);
  }

  // May be called from any device
  bool queryEvent(void* event) const override {
    if (!event)
      return true;
    aclrtEvent npu_event = static_cast<aclrtEvent>(event);
    aclrtEventRecordedStatus status = ACL_EVENT_RECORDED_STATUS_NOT_READY;
    NPU_CHECK_ERROR(aclrtQueryEventStatus(npu_event, &status));
    return (status == ACL_EVENT_RECORDED_STATUS_COMPLETE);
  }

  void synchronizeEvent(void* event) const override {
    if (!event)
      return;
    aclrtEvent npu_event = static_cast<aclrtEvent>(event);
    NPU_CHECK_ERROR(aclrtSynchronizeEvent(npu_event));
  }
};

} // namespace impl
} // namespace c10::backend
