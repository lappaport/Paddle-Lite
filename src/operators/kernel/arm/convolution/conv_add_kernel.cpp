/* Copyright (c) 2018 PaddlePaddle Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#ifdef FUSION_CONVADD_OP

#include "operators/kernel/conv_add_kernel.h"
#include "operators/kernel/arm/convolution/conv_common.h"
#include "operators/kernel/central-arm-func/conv_arm_func.h"
#include "operators/math/channel_wise.h"

namespace paddle_mobile {
namespace operators {

template <>
bool ConvAddKernelCpu<float>::Init(FusionConvAddParam *param) {
  InitBaseConvKernel(param);
  return true;
}

template <>
void ConvAddKernelCpu<float>::Compute(const FusionConvAddParam &param) {
  switch (param.ExecMode()) {
    case ConvParam::EXEC_DEPTHWISE3x3S1_FLOAT:
      break;
    case ConvParam::EXEC_DEPTHWISE3x3S2_FLOAT:
      math::DepthwiseConv3x3S2<float, float>(
          *(param.Input()->InnerLoDTensor()), *param.Filter()->InnerLoDTensor(),
          param.Paddings(), param.Output()->InnerLoDTensor());
      break;
    case ConvParam::EXEC_DEPTHWISE5x5_FLOAT:
      DepthwiseConv5x5<float, float>(param);
      break;
    case ConvParam::EXEC_WINOGRAD3X3_FLOAT:
      WinogradConv3x3<8, 3>(param);
      break;
    case ConvParam::EXEC_GEMM_FLOAT:
      GemmConv<float, float>(param);
      break;
    default:
      PADDLE_MOBILE_THROW_EXCEPTION("Invalid convolution execute mode %d",
                                    param.ExecMode());
  }
  math::AddChannelWise<IDENTITY>(param.Output()->InnerLoDTensor(),
                                 param.Bias()->InnerLoDTensor(),
                                 param.Output()->InnerLoDTensor());
}

template class ConvAddKernelCpu<float>;

}  // namespace operators
}  // namespace paddle_mobile

#endif