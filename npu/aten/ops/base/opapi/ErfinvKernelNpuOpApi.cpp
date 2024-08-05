// Copyright (c) 2023 Huawei Technologies Co., Ltd
// All rights reserved.
//
// Licensed under the BSD 3-Clause License  (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "npu/aten/AclOpsInterface.h"
#include "npu/aten/OpApiInterface.h"
#include "npu/aten/utils/op_api_common.h"

namespace op_api {
using npu_preparation = at_npu::native::OpPreparation;

at::Tensor& erfinv_out(const at::Tensor& self, at::Tensor& result) {
  DO_COMPATIBILITY(aclnnErfinv, acl_op::erfinv_out(self, result));
  result.resize_(self.sizes());
  EXEC_NPU_CMD(aclnnErfinv, self, result);
  return result;
}

at::Tensor& erfinv_(at::Tensor& self) {
  DO_COMPATIBILITY(aclnnInplaceErfinv, acl_op::erfinv_(self));
  EXEC_NPU_CMD(aclnnInplaceErfinv, self);
  return self;
}

at::Tensor erfinv(const at::Tensor& self) {
  DO_COMPATIBILITY(aclnnErfinv, acl_op::erfinv(self));
  at::Tensor result = npu_preparation::apply_tensor_without_format(self);
  if (!at::isFloatingType(self.scalar_type())) {
    result = npu_preparation::apply_tensor_without_format(self.sizes(), self.options().dtype(at::kFloat));
  }
  EXEC_NPU_CMD(aclnnErfinv, self, result);
  return result;
}
} // namespace op_api
