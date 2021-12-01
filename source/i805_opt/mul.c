/*
 * Copyright (C) 2016-2021 C-SKY Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* CSI-NN2 version 1.10.x */

#include "csi_i805.h"


int csi_i805_mul_init_u8(struct csi_tensor *input0,
                         struct csi_tensor *input1,
                         struct csi_tensor *output,
                         struct diso_params *params)
{
    // compute out multiplier and shift for scale_in/scale_out
    float real_scale = input0->qinfo->scale * input1->qinfo->scale / output->qinfo->scale;
    csi_quantize_multiplier(real_scale, &output->qinfo->multiplier, &output->qinfo->shift);
    params->base.bc = csi_i805_mul_u8;
    return CSINN_TRUE;
}

int csi_i805_mul_u8(struct csi_tensor *input0,
                    struct csi_tensor *input1,
                    struct csi_tensor *output,
                    struct diso_params *params)
{
    uint8_t *input0_data = (uint8_t *)input0->data;
    uint8_t *input1_data = (uint8_t *)input1->data;
    uint8_t *output_data = (uint8_t *)output->data;

    int32_t size = csi_tensor_size(input0);

    csi_i805_elementwise_mul_opt_u8(input0_data, input1_data, output_data, size, -input0->qinfo->zero_point, -input1->qinfo->zero_point,
                                    output->qinfo->zero_point, output->qinfo->multiplier, -output->qinfo->shift);
    return CSINN_TRUE;
}