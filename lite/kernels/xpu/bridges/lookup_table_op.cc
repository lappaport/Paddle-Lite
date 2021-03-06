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

int LookupTableConverter(void* ctx, OpLite* op, KernelBase* kernel) {
  CHECK(ctx != nullptr);
  CHECK(op != nullptr);
  auto graph = static_cast<Graph*>(ctx);
  auto op_info = op->op_info();
  auto op_type = op_info->Type();
  auto scope = op->scope();
  VLOG(3) << "[XPU] Converting " + op_type + "...";

  // Get input and output vars and op attributes
  auto ids_name = op_info->Input("Ids").front();
  auto ids_type = kernel->GetInputDeclType("Ids");
  CHECK(ids_type->precision() == PRECISION(kInt64));
  CHECK(ids_type->layout() == DATALAYOUT(kNCHW));
  auto ids = scope->FindMutableTensor(ids_name);
  auto ids_dims = ids->dims();
  auto w_name = op_info->Input("W").front();
  auto w_type = kernel->GetInputDeclType("W");
  CHECK(w_type->precision() == PRECISION(kFloat));
  CHECK(w_type->layout() == DATALAYOUT(kNCHW));
  auto w = scope->FindMutableTensor(w_name);
  auto w_dims = w->dims();
  CHECK_EQ(w_dims.size(), 2);
  auto out_name = op_info->Output("Out").front();
  auto out_type = kernel->GetOutputDeclType("Out");
  CHECK(out_type->precision() == PRECISION(kFloat));
  CHECK(out_type->layout() == DATALAYOUT(kNCHW));
  auto out = scope->FindMutableTensor(out_name);
  auto out_dims = out->dims();
  auto padding_idx = op_info->GetAttr<int64_t>("padding_idx");
  if (padding_idx != -1) {
    LOG(WARNING) << "[XPU] Only padding_idx=-1 is supported.";
    return FAILED;
  }

  // Ids node
  std::shared_ptr<xtcl::xExpr> ids_node = nullptr;
  if (graph->HasNode(ids_name)) {
    ids_node = graph->GetNode(ids_name);
  } else {
    ids_node = graph->AddNode(
        ids_name, ids_dims, ids_type->precision(), ids_type->layout());
  }
  // Flatten Ids node
  if (ids_dims.size() != 1) {
    ids_node = graph->AddNode(ids_name + "/reshape",
                              graph->builder_.CreateReshape(*ids_node, {-1}),
                              ids_type->precision(),
                              ids_type->layout());
  }
  auto w_const_node = graph->AddNode(w_name, *w);

  // Reshape the gather node with the inferred shape as the output node
  auto gather_node = graph->AddNode(
      out_name,
      graph->builder_.CreateGather(*w_const_node, *ids_node, /* axis= */ 0));
  if (out_dims.size() != 2) {
    graph->AddNode(out_name,
                   graph->builder_.CreateReshape(
                       *gather_node, CvtShape<xtcl::Integer>(out_dims)));
  }
  return SUCCESS;
}

}  // namespace xpu
}  // namespace subgraph
}  // namespace lite
}  // namespace paddle

REGISTER_SUBGRAPH_BRIDGE(XPU,
                         lookup_table,
                         paddle::lite::subgraph::xpu::LookupTableConverter);
