// Copyright (c) 2024 Huawei Technologies Co., Ltd
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

at::Tensor npu_scatter_nd_update(
    const at::Tensor &self,
    const at::Tensor &indice,
    const at::Tensor &updates)
{
    at::Tensor result = self.clone();
    EXEC_NPU_CMD(aclnnScatterNdUpdate, result, indice, updates);
    return result;
}

at::Tensor &npu_scatter_nd_update_(
    at::Tensor &self,
    const at::Tensor &indice,
    const at::Tensor &updates)
{
    EXEC_NPU_CMD(aclnnScatterNdUpdate, self, indice, updates);
    return self;
}

} // namespace op_api
