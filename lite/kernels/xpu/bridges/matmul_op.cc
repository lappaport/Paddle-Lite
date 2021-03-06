// Copyright (c) 2019 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "lite/kernels/npu/bridges/registry.h"
#include "lite/kernels/xpu/bridges/graph.h"
#include "lite/kernels/xpu/bridges/utility.h"

namespace paddle {
namespace lite {
namespace subgraph {
namespace xpu {

int MatmulConverter(void* ctx, OpLite* op, KernelBase* kernel) {
  CHECK(ctx != nullptr);
  CHECK(op != nullptr);
  auto graph = static_cast<Graph*>(ctx);
  auto op_info = op->op_info();
  auto op_type = op_info->Type();
  auto scope = op->scope();
  VLOG(3) << "[XPU] Converting " + op_type + "...";

  // Get input and output vars and op attributes
  auto x_name = op_info->Input("X").front();
  auto x_type = kernel->GetInputDeclType("X");
  CHECK(x_type->precision() == PRECISION(kFloat));
  CHECK(x_type->layout() == DATALAYOUT(kNCHW));
  auto x = scope->FindMutableTensor(x_name);
  auto x_dims = x->dims();

  auto y_name = op_info->Input("Y").front();
  auto y_type = kernel->GetInputDeclType("Y");
  CHECK(y_type->precision() == PRECISION(kFloat));
  CHECK(y_type->layout() == DATALAYOUT(kNCHW));
  auto y = scope->FindMutableTensor(y_name);
  auto y_dims = y->dims();

  auto out_name = op_info->Output("Out").front();
  auto out_type = kernel->GetOutputDeclType("Out");
  CHECK(out_type->precision() == PRECISION(kFloat));
  CHECK(out_type->layout() == DATALAYOUT(kNCHW));
  auto out = scope->FindMutableTensor(out_name);
  auto out_dims = out->dims();

  auto transpose_x = op_info->GetAttr<bool>("transpose_X");
  auto transpose_y = op_info->GetAttr<bool>("transpose_Y");
  auto alpha = op_info->GetAttr<float>("alpha");

  // X node
  std::shared_ptr<xtcl::xExpr> x_node = nullptr;
  if (graph->HasNode(x_name)) {
    x_node = graph->GetNode(x_name);
  } else {
    x_node = graph->AddNode(x_name, x_dims);
  }

  // Y node
  std::shared_ptr<xtcl::xExpr> y_node = nullptr;
  if (graph->HasNode(y_name)) {
    y_node = graph->GetNode(y_name);
  } else {
    y_node = graph->AddNode(y_name, y_dims);
  }

  // Matmul node
  if (x_dims.size() > 2 && y_dims.size() >= 2) {
    // x: [B, ..., M, K], y: [B, ..., K, N], out: [B, ..., M, N]
    // x: [B, M, K], y: [K, N], out: [B, M, N]
    // Reshape and transposed X node
    if (x_dims.size() != 3) {
      auto m = static_cast<int>(x_dims[x_dims.size() - 2]);
      auto k = static_cast<int>(x_dims[x_dims.size() - 1]);
      x_node =
          graph->AddNode(x_name + "/reshape",
                         graph->builder_.CreateReshape(*x_node, {-1, m, k}));
      if (transpose_x) {
        x_node =
            graph->AddNode(x_name + "/reshape/transpose",
                           graph->builder_.CreateTranspose(*x_node, {0, 2, 1}));
      }
    }
    // Reshape and transposed Y node
    if (y_dims.size() != 3) {
      auto k = static_cast<int>(y_dims[y_dims.size() - 2]);
      auto n = static_cast<int>(y_dims[y_dims.size() - 1]);
      y_node =
          graph->AddNode(y_name + "/reshape",
                         graph->builder_.CreateReshape(*y_node, {-1, k, n}));
      if (!transpose_y) {
        y_node =
            graph->AddNode(y_name + "/reshape/transpose",
                           graph->builder_.CreateTranspose(*y_node, {0, 2, 1}));
      }
    }
    // Matmul node
    auto matmul_node = graph->AddNode(
        out_name, graph->builder_.CreateBatchMatmul(*x_node, *y_node));
    if (fabs(alpha - 1) > 1e-6f) {
      matmul_node = graph->AddNode(
          out_name, graph->builder_.CreateScale(*matmul_node, alpha));
    }
    if (out_dims.size() != 3) {
      graph->AddNode(out_name,
                     graph->builder_.CreateReshape(
                         *matmul_node, CvtShape<xtcl::Integer>(out_dims)));
    }
  } else if (x_dims.size() == 2 && y_dims.size() == 2) {
    // x: [M, K], y: [K, N], out: [M, N]
    if (transpose_x) {
      x_node = graph->AddNode(x_name + "/transpose",
                              graph->builder_.CreateTranspose(*x_node, {1, 0}));
    }
    auto matmul_node = graph->AddNode(
        out_name,
        graph->builder_.CreateMatmul2D(*x_node, *y_node, transpose_y));
    if (fabs(alpha - 1) > 1e-6f) {
      matmul_node = graph->AddNode(
          out_name, graph->builder_.CreateScale(*matmul_node, alpha));
    }
  } else if (x_dims.size() == 1 && y_dims.size() == 1) {
    // x: [K], y: [K], out: [1]
    // x: [M], y: [N], x_transpose: true, y_transpose: true, out: [M, N]
    LOG(FATAL) << "[XPU] Not supported.";
    return FAILED;
  }
  return REBUILD_WHEN_SHAPE_CHANGED;
}

}  // namespace xpu
}  // namespace subgraph
}  // namespace lite
}  // namespace paddle

REGISTER_SUBGRAPH_BRIDGE(XPU,
                         matmul,
                         paddle::lite::subgraph::xpu::MatmulConverter);
