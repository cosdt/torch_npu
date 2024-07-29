#include <deque>
#include <vector>

#include <c10/util/CallOnce.h>
#include "csrc/npu/NPUContext.h"

namespace c10_npu {
namespace {

/*
 * Currently, there is one device properties pool containing the information and
 * capability about each compute-device.
 *
 * Device properties are lazily initialized when the first time properties are
 * requested for a device.
 */
c10::DeviceIndex num_gpus = -1;
c10::once_flag init_flag;
std::deque<c10::once_flag> device_prop_flags;
std::vector<NPUDeviceProp> device_properties;

void initNPUContextVectors() {
  num_gpus = c10_npu::device_count();
  device_prop_flags.resize(num_gpus);
  device_properties.resize(num_gpus);
}

void initDeviceProperty(c10::DeviceIndex device) {
  c10_npu::get_device_properties(&device_properties[device], device);
}

inline void check_device(c10::DeviceIndex device) {
  TORCH_CHECK(
      device >= 0 && device < num_gpus,
      "device is out of range, device is ",
      static_cast<int>(device),
      ", total number of device is ",
      static_cast<int>(num_gpus),
      ".");
}
} // anonymous namespace

NPUDeviceProp* getDeviceProperties(c10::DeviceIndex device) {
  c10::call_once(init_flag, initNPUContextVectors);
  if (device == -1)
    device = c10_npu::current_device();
  check_device(device);
  c10::call_once(device_prop_flags[device], initDeviceProperty, device);
  return &device_properties[device];
}

} // namespace c10_npu
