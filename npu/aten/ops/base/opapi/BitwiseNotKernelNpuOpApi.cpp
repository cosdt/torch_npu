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

at::Tensor& bitwise_not_out(const at::Tensor& self, at::Tensor& result) {
  DO_COMPATIBILITY(aclnnBitwiseNot, acl_op::bitwise_not_out(self, result));
  npu_preparation::check_tensor({self}, result, self);
  EXEC_NPU_CMD(aclnnBitwiseNot, self, result);
  return result;
}

at::Tensor bitwise_not(const at::Tensor& self) {
  DO_COMPATIBILITY(aclnnBitwiseNot, acl_op::bitwise_not(self));
  at::Tensor result = npu_preparation::apply_tensor_without_format(self);
  EXEC_NPU_CMD(aclnnBitwiseNot, self, result);
  return result;
}

at::Tensor& bitwise_not_(at::Tensor& self) {
  DO_COMPATIBILITY(aclnnBitwiseNot, acl_op::bitwise_not_(self));
  EXEC_NPU_CMD(aclnnBitwiseNot, self, self);
  return self;
}

}  // namespace op_api
