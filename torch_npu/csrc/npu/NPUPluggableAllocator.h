#pragma once

#include <c10/core/Allocator.h>
#include <c10/core/Device.h>

#include "csrc/npu/NPUCachingAllocator.h"

#include <array>
#include <mutex>

namespace torch::npu::NPUPluggableAllocator {

using streamType = c10::Stream;

std::shared_ptr<c10_backend::CachingAllocator::CachingAllocator>
getCurrentAllocator();
std::shared_ptr<c10_backend::CachingAllocator::CachingAllocator>
createCustomAllocator(
    std::function<void*(size_t, int, aclrtStream)> alloc_fn,
    std::function<void(void*, size_t, int, aclrtStream)> free_fn);
void changeCurrentAllocator(
    const std::shared_ptr<c10_backend::CachingAllocator::CachingAllocator>&
        allocator);

struct _AllocationMetadata {
  _AllocationMetadata();
  _AllocationMetadata(
      size_t size,
      c10::DeviceIndex device_idx,
      aclrtStream stream);
  size_t size;
  c10::DeviceIndex device_idx;
  aclrtStream stream;
};

struct NPUPluggableAllocator
    : public c10_backend::CachingAllocator::CachingAllocator {
  NPUPluggableAllocator(
      std::function<void*(size_t, int, aclrtStream)> alloc_fn,
      std::function<void(void*, size_t, int, aclrtStream)> free_fn);

  NPUPluggableAllocator(NPUPluggableAllocator& other);

  void set_init_fn(std::function<void(int)> init_fn);
  void set_reset_fn(std::function<void(bool)> reset_fn);
  void set_memory_fraction_fn(
      std::function<void(double, int)> memory_fraction_fn);
  void set_base_alloc_fn(std::function<void*(void*, size_t*)> base_alloc_fn);
  void set_record_stream_fn(
      std::function<void(void* ptr, aclrtStream stream)> record_stream_fn);
  void set_erase_stream_fn(
      std::function<void(void* ptr, aclrtStream stream)> erase_stream_fn);
  void* malloc(size_t size, c10::DeviceIndex device, aclrtStream stream);

  c10::DataPtr allocate(size_t size) override;
  c10::DeleterFnPtr raw_deleter() const override;

  void* raw_alloc(size_t nbytes) override;
  void* raw_alloc_with_stream(size_t nbytes, aclrtStream stream) override;
  void raw_delete(void* ptr) override;
  void init(int device_count) override;
  bool initialized() override;
  void setMemoryFraction(double fraction, c10::DeviceIndex device) override;
  void emptyCache(bool check_error) override;
  void cacheInfo(
      c10::DeviceIndex device,
      size_t* cachedAndFree,
      size_t* largestBlock) override;
  void* getBaseAllocation(void* ptr, size_t* size) override;
  void recordStream(const c10::DataPtr&, streamType stream) override;
  void eraseStream(const c10::DataPtr&, streamType stream) override;
  c10_backend::CachingAllocator::DeviceStats getDeviceStats(
      c10::DeviceIndex device) override;
  void resetAccumulatedStats(c10::DeviceIndex device) override;
  void resetPeakStats(c10::DeviceIndex device) override;
  c10_backend::CachingAllocator::SnapshotInfo snapshot() override;
  void emptyDeviceCache(c10::DeviceIndex device) override;
  std::string name() override;
  void copy_data(void* dest, const void* src, std::size_t count) const final;
  void recordHistory(
      bool enabled,
      c10_backend::CachingAllocator::CreateContextFn context_recorder,
      size_t alloc_trace_max_entries,
      c10_backend::CachingAllocator::RecordContext when) override;
  void attachOutOfMemoryObserver(
      c10_backend::CachingAllocator::OutOfMemoryObserver observer) override;

 protected:
  std::function<void*(size_t, c10::DeviceIndex, aclrtStream)> alloc_fn_;
  std::function<void(void*, size_t, c10::DeviceIndex, aclrtStream)> free_fn_;
  std::function<void(int)> init_fn_;
  std::function<void(bool)> reset_fn_;
  std::function<void(double, int)> memory_fraction_fn_;
  std::function<void*(void*, size_t*)> base_alloc_fn_;
  std::function<void(void* ptr, aclrtStream stream)> record_stream_fn_;
  std::function<void(void* ptr, aclrtStream stream)> erase_stream_fn_;
  std::mutex allocator_mutex_;
  // We do the bookeeping here in order to simplify custom allocators
  std::unordered_map<void*, _AllocationMetadata> allocation_metadata_;

  bool initialized_ = false;
};
} // namespace torch::npu::NPUPluggableAllocator