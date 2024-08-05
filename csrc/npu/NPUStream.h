#pragma once

#include <c10/core/DeviceGuard.h>
#include <c10/core/Stream.h>
#include <c10/util/SmallVector.h>
#include <c10/core/impl/GPUTrace.h>
#include <cstdint>
#include <mutex>

#include "csrc/aten/generated/NPUNativeFunctions.h"
#include "csrc/core/Macros.h"
#include "csrc/core/Macros.h"
#include "npu/acl/include/acl/acl.h"
#include "npu/acl/include/acl/acl_op.h"
#include "npu/core/NPUException.h"
#include "npu/core/npu_log.h"

/*
 * Stream pool note.
 *
 * A NPUStream is an abstraction of an actual cuStream on the GPU. NPUStreams
 * are backed by cuStreams, but they use several pools to minimize the costs
 * associated with creating, retaining, and destroying cuStreams.
 *
 * There are three pools per device, and a device's pools are lazily created.
 *
 * The first pool contains only the default stream. When the default stream
 * is requested it's returned.
 *
 * The second pool is the "low priority" or "default priority" streams. In
 * HIP builds there is no distinction between streams in this pool and streams
 * in the third pool (below). There are 32 of these streams per device, and
 * when a stream is requested one of these streams is returned round-robin.
 * That is, the first stream requested is at index 0, the second at index 1...
 * to index 31, then index 0 again.
 *
 * This means that if 33 low priority streams are requested, the first and
 * last streams requested are actually the same stream (under the covers)
 * and kernels enqueued on them cannot run concurrently.
 *
 * The third pool is the "high priority" streams. The third pool acts like
 * the second pool except the streams are created with a higher priority.
 *
 * These pools suggest that stream users should prefer many short-lived streams,
 * as the cost of acquiring and releasing streams is effectively zero. If
 * many longer-lived streams are required in performance critical scenarios
 * then the functionality here may need to be extended to allow, for example,
 * "reserving" a subset of the pool so that other streams do not accidentally
 * overlap the performance critical streams.
 *
 * Note: although the notion of "current stream for device" is thread local
 * (every OS thread has a separate current stream, as one might expect),
 * the stream pool is global across all threads; stream 0 is always stream 0
 * no matter which thread you use it on.  Multiple threads can synchronize
 * on the same stream.  Although the NPU documentation is not very clear
 * on the matter, streams are thread safe; e.g., it is safe to enqueue
 * a kernel on the same stream from two different threads.
 */

namespace c10_npu {

static constexpr int max_compile_time_stream_priorities = 2;

// Value object representing a NPU stream.  This is just a wrapper
// around c10::Stream, but it comes with a little extra NPU-specific
// functionality (conversion to aclrtStream), and a guarantee that
// the wrapped c10::Stream really is a NPU stream.
class C10_BACKEND_API NPUStream {
 public:
  enum Unchecked { UNCHECKED };

  /// Construct a NPUStream from a Stream.  This construction is checked,
  /// and will raise an error if the Stream is not, in fact, a NPU stream.
  explicit NPUStream(c10::Stream stream) : stream_(stream) {
    TORCH_CHECK(stream_.device_type() == c10::DeviceType::PrivateUse1,
        PTA_ERROR(ErrCode::TYPE));
  }

  /// Construct a NPUStream from a Stream with no error checking.
  /// This constructor uses the "named" constructor idiom, and can
  /// be invoked as: NPUStream(NPUStream::UNCHECKED, stream)
  explicit NPUStream(Unchecked, c10::Stream stream) : stream_(stream) {}

  bool operator==(const NPUStream& other) const noexcept {
    return unwrap() == other.unwrap();
  }

  bool operator!=(const NPUStream& other) const noexcept {
    return unwrap() != other.unwrap();
  }

  // Implicit conversion to aclrtStream.
  operator aclrtStream() const {
    return stream();
  }

  // Implicit conversion to pytorch Stream. (a.k.a., forget that the stream is a
  /// NPU stream).
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

  /// Return the stream ID corresponding to this particular stream.
  c10::StreamId id() const {
    return stream_.id();
  }

  bool query() const {
    c10::DeviceGuard guard{stream_.device()};
    aclrtStreamStatus status = ACL_STREAM_STATUS_RESERVED;
    NPU_CHECK_ERROR(aclrtStreamQuery(stream(), &status));
    if (status == ACL_STREAM_STATUS_COMPLETE) {
      return true;
    }
    return false;
  }

  void synchronize() const {
    c10::DeviceGuard guard{stream_.device()};
    NPU_CHECK_ERROR(aclrtSynchronizeStreamWithTimeout(stream(), -1));
  }

  // Explicit conversion to aclrtStream.
  aclrtStream stream() const;

  // Explicit conversion to Stream.
  c10::Stream unwrap() const {
    return stream_;
  }

  /// Reversibly pack a NPUStream into a struct representation.
  /// Previously the stream's data was packed into a single int64_t,
  /// as it was assumed the fields would not require more than
  /// 64 bits of storage in total.
  /// See https://github.com/pytorch/pytorch/issues/75854
  /// for more information regarding newer platforms that may violate
  /// this assumption.
  ///
  /// The NPUStream can be unpacked using unpack().
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

 private:
  c10::Stream stream_;
};

/**
 * Get a new stream from the NPU stream pool.  You can think of this
 * as "creating" a new stream, but no such creation actually happens;
 * instead, streams are preallocated from the pool and returned in a
 * round-robin fashion.
 *
 * You can request a stream from the high priority pool by setting
 * isHighPriority to true, or a stream for a specific device by setting device
 * (defaulting to the current NPU stream.)
 */
C10_BACKEND_API NPUStream getStreamFromPool(const bool isHighPriority = false, c10::DeviceIndex device = -1);

C10_BACKEND_API NPUStream getStreamFromPool(const int priority, c10::DeviceIndex device = -1);


/**
 * Get a NPUStream from a externally allocated one.
 *
 * This is mainly for interoperability with different libraries where we
 * want to operate on a non-torch allocated stream for data exchange or similar
 * purposes
 */
C10_BACKEND_API NPUStream getStreamFromExternal(aclrtStream ext_stream, c10::DeviceIndex device_index);

/**
 * Get the default NPU stream, for the passed NPU device, or for the
 * current device if no device index is passed.  The default stream is
 * where most computation occurs when you aren't explicitly using
 * streams.
 */
C10_BACKEND_API NPUStream getDefaultNPUStream(c10::DeviceIndex device_index = -1);

/**
 * Get the current NPU stream, for the passed NPU device, or for the
 * current device if no device index is passed.  The current NPU stream
 * will usually be the default NPU stream for the device, but it may
 * be different if someone called 'setCurrentNPUStream' or used 'StreamGuard'
 * or 'NPUStreamGuard'.
 */
C10_BACKEND_API NPUStream getCurrentNPUStream(c10::DeviceIndex device_index = -1);


/**
 * Set the current stream on the device of the passed in stream to be
 * the passed in stream.  Yes, you read that right: this function
 * has *nothing* to do with the current device: it toggles the current
 * stream of the device of the passed stream.
 *
 * Confused?  Avoid using this function; prefer using 'NPUStreamGuard' instead
 * (which will switch both your current device and current stream in the way you
 * expect, and reset it back to its original state afterwards).
 */
C10_BACKEND_API void setCurrentNPUStream(NPUStream stream);

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
