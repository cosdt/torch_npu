// Copyright (c) 2023-2024 Huawei Technologies Co., Ltd
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

#include "npu/aten/OpApiInterface.h"
#include "npu/aten/AclOpsInterface.h"
#include "npu/aten/utils/op_api_common.h"

namespace op_api {
using npu_preparation = at_npu::native::OpPreparation;

std::tuple<at::Tensor, at::Tensor, at::Tensor, at::Tensor> npu_add_layer_norm(
    const at::Tensor& x1,
    const at::Tensor& x2,
    const at::Tensor& gamma,
    const at::Tensor& beta,
    double epsilon,
    bool additional_output)
{
    DO_COMPATIBILITY(aclnnAddLayerNorm, acl_op::npu_add_layer_norm(x1, x2, gamma, beta, epsilon, additional_output));
    at::SmallVector<int64_t, SIZE> shape;
    for (int64_t index = 0; index < x1.dim() - gamma.dim(); index++) {
        shape.emplace_back(x1.size(index));
    }
    shape.emplace_back(1);

    at::Tensor y;
    at::Tensor x;
    if (x1.dtype() == x2.dtype()) {
        y = npu_preparation::apply_tensor(x1);
        x = npu_preparation::apply_tensor(x1);
    } else {
        y = npu_preparation::apply_tensor(x1.sizes(), x1.options().dtype(at::kFloat), x1);
        x = npu_preparation::apply_tensor(x1.sizes(), x1.options().dtype(at::kFloat), x1);
    }

    at::Tensor mean = npu_preparation::apply_tensor(shape, x1.options().dtype(at::kFloat), x1);
    at::Tensor rstd = npu_preparation::apply_tensor(shape, x1.options().dtype(at::kFloat), x1);
    const at::Tensor& bias = at::Tensor();

    EXEC_NPU_CMD(aclnnAddLayerNorm, x1, x2, gamma, beta, bias, epsilon, additional_output, y, mean, rstd, x);
    return std::make_tuple(y, mean, rstd, x);
}
} // namespace op_api