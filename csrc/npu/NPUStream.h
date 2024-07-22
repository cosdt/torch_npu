#pragma once

#include <c10/core/DeviceGuard.h>
#include <c10/core/Stream.h>
#include <c10/util/SmallVector.h>
#include <cstdint>
#include <mutex>

#include "csrc/aten/generated/NPUNativeFunctions.h"
#include "npu/acl/include/acl/acl.h"
#include "npu/acl/include/acl/acl_op.h"
#include "npu/core/NPUException.h"
#include "npu/core/NPUMacros.h"
#include "npu/core/NPUQueue.h"
#include "npu/core/npu_log.h"

namespace c10_npu {

class C10_NPU_API NPUStream {
 public:
  enum Unchecked { UNCHECKED };

  explicit NPUStream(c10::Stream stream) : stream_(stream) {
    TORCH_CHECK(
        stream_.device_type() == c10::DeviceType::PrivateUse1,
        PTA_ERROR(ErrCode::TYPE));
  }

  explicit NPUStream(Unchecked, c10::Stream stream) : stream_(stream) {}

  ~NPUStream() {}

  bool operator==(const NPUStream& other) const noexcept {
    return unwrap() == other.unwrap();
  }

  bool operator!=(const NPUStream& other) const noexcept {
    return unwrap() != other.unwrap();
  }

  // Implicit conversion to rtStream_t.
  operator aclrtStream() const {
    return stream();
  }

  // Implicit conversion to pytorch Stream.
  operator c10::Stream() const {
    return unwrap();
  }

  // Used to avoid baking in device type explicitly to Python-side API.
  c10::DeviceType device_type() const {
    return c10::DeviceType::PrivateUse1;
  }

  // Get the NPU device index that this stream is associated with.
  c10::DeviceIndex device_index() const {
    return stream_.device_index();
  }

  // Get the full Device that this stream is associated with.  The Device
  // is guaranteed to be a NPU device.
  c10::Device device() const {
    return c10::Device(c10::DeviceType::PrivateUse1, device_index());
  }

  c10::StreamId id() const {
    return stream_.id();
  }

  bool query() const {
    c10::DeviceGuard guard{stream_.device()};
    acl::aclrtStreamStatus status = acl::ACL_STREAM_STATUS_RESERVED;
    NPU_CHECK_ERROR(acl::AclrtStreamQuery(stream(), &status));
    if (status == acl::ACL_STREAM_STATUS_COMPLETE) {
      return true;
    }
    return false;
  }

  void synchronize() const {
    c10::DeviceGuard guard{stream_.device()};
    NPU_CHECK_ERROR(c10_npu::acl::AclrtSynchronizeStreamWithTimeout(stream()));
  }

  // Explicit conversion to rtStream_t.
  aclrtStream stream() const;

  // Explicit conversion to Stream.
  c10::Stream unwrap() const {
    return stream_;
  }

  // The NPUStream can be unpacked using unpack().
  struct c10::StreamData3 pack3() const {
    return stream_.pack3();
  }

  // Unpack a NPUStream from the 3 fields generated by pack().
  static NPUStream unpack3(
      c10::StreamId stream_id,
      c10::DeviceIndex device_index,
      c10::DeviceType device_type) {
    return NPUStream(
        c10::Stream::unpack3(stream_id, device_index, device_type));
  }

  void setDataPreprocessStream(bool is_data_preprocess_stream);

  bool isDataPreprocessStream();

  // Explicit conversion to rtStream_t， with out empty taskQ.
  aclrtStream stream(const bool need_empty) const;

 private:
  c10::Stream stream_;
};

C10_NPU_API NPUStream getNPUStreamFromPool(c10::DeviceIndex device = -1);

C10_NPU_API NPUStream getDefaultNPUStream(c10::DeviceIndex device_index = -1);

NPUStream getStreamFromPool(
    const bool isHighPriority,
    c10::DeviceIndex device_index);

C10_NPU_API NPUStream getCurrentNPUStream(c10::DeviceIndex device_index = -1);

NPUStream getCurrentSecondaryStream(c10::DeviceIndex device_index = -1);

aclrtStream getCurrentNPUStreamNoWait(c10::DeviceIndex device_index = -1);

NPUStatus emptyAllNPUStream();

std::string getRepoInfo();

C10_NPU_API bool npuSynchronizeDevice(bool check_error = true);

void enCurrentNPUStream(void* cur_paras, c10::DeviceIndex device_index = -1);

C10_NPU_API bool npuSynchronizeUsedDevices(bool check_error = true);

C10_NPU_API void setCurrentNPUStream(NPUStream stream);

std::ostream& operator<<(std::ostream& stream, const NPUStream& s);

aclError DestroyUsedStreams();
} // namespace c10_npu

namespace std {
template <>
struct hash<c10_npu::NPUStream> {
  size_t operator()(c10_npu::NPUStream s) const noexcept {
    return std::hash<c10::Stream>{}(s.unwrap());
  }
};
} // namespace std
