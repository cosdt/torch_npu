// Copyright (c) 2023 Huawei Technologies Co., Ltd
// Copyright (c) 2019, Facebook CORPORATION.
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

#ifndef __TORCH_NPU_OP_PLUGIN_sparse_INTERFACE__
#define __TORCH_NPU_OP_PLUGIN_sparse_INTERFACE__

#include <ATen/Tensor.h>
#include <ATen/ATen.h>
#include "aten/utils/Export.h"

namespace sparse {
OP_PLUGIN_HIDDEN at::Tensor & add_out_sparse(const at::Tensor & self, const at::Tensor & other, const at::Scalar & alpha, at::Tensor & out);
OP_PLUGIN_HIDDEN at::Tensor _coalesce_sparse(const at::Tensor & self);
OP_PLUGIN_HIDDEN at::Tensor max_sparse(const at::Tensor & self);
OP_PLUGIN_HIDDEN at::Tensor & max_out_sparse(const at::Tensor & self, const at::Tensor & other, at::Tensor & out);
}  // namespace acl_op
#endif
