/*
 * Copyright (C) 2016-2020 C-SKY Limited. All rights reserved.
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

#include "csi_nn.h"
#include "csi_utils.h"

static int csi_unsorted_segment_mean_f32(struct csi_tensor *input,
                            struct csi_tensor *segment_ids,
                            struct csi_tensor *output,
                            struct segment_params *params)
{
    float *input_data  = input->data;
    int *segment_data  = segment_ids->data;
    float *output_data = output->data;

    int input_dim    = input->dim_count;
    int num_segments = params->num_segments;
    int index[input->dim[0]];

    for(int n = 0; n < num_segments; n++) {
        /* init the outputdata data */
        for(int h = 0; h < input->dim[1]; h++) {
            for(int w = 0; w < input->dim[2]; w++) {
                for(int c = 0; c < input->dim[3]; c++) {
                    int32_t output_index = csi_get_index(input->dim, n, h, w, c);
                    output_data[output_index] = 0;
                }
            }
        }
        int num = 0;

        for(int i = 0; i < input->dim[0]; i++) {
            if (segment_data[i] == n) {
                index[num] = i;
                num++;
            }
        }
        int mean_n = num;
        if(num > 0) {
            for(int h = 0; h < input->dim[1]; h++) {
                for(int w = 0; w < input->dim[2]; w++) {
                    for(int c = 0; c < input->dim[3]; c++) {
                        int32_t output_index = csi_get_index(input->dim, n, h, w, c);
                        for(int k = 0; k < num; k++) {
                            int32_t input_index = csi_get_index(input->dim, index[k], h, w, c);
                            output_data[output_index] +=  input_data[input_index];
                        }
                        output_data[output_index] /= mean_n;
                    }
                }
            }
        }
    }

    return CSINN_TRUE;
}

static int csi_segment_mean_f32(struct csi_tensor *input,
                            struct csi_tensor *segment_ids,
                            struct csi_tensor *output,
                            struct segment_params *params)
{
    float *input_data  = input->data;
    int *segment_data  = segment_ids->data;
    float *output_data = output->data;

    int input_dim    = input->dim_count;
    int num_segments = params->num_segments;
    int index[input->dim[0]];
    int i = 0;

    for(int n = 0; n < num_segments; n++) {
        /* init the outputdata data */
        for(int h = 0; h < input->dim[1]; h++) {
            for(int w = 0; w < input->dim[2]; w++) {
                for(int c = 0; c < input->dim[3]; c++) {
                    int32_t output_index = csi_get_index(input->dim, n, h, w, c);
                    output_data[output_index] = 0;
                }
            }
        }
        int num = 0;
        for(; i < input->dim[0]; i++) {
            if (segment_data[i] == n) {
                index[num] = i;
                num++;
            } else {
                break;
            }
        }
        int mean_n = num;
        if(num > 0) {
            for(int h = 0; h < input->dim[1]; h++) {
                for(int w = 0; w < input->dim[2]; w++) {
                    for(int c = 0; c < input->dim[3]; c++) {
                        int32_t output_index = csi_get_index(input->dim, n, h, w, c);
                        for(int k = 0; k < num; k++) {
                            int32_t input_index = csi_get_index(input->dim, index[k], h, w, c);
                            output_data[output_index] +=  input_data[input_index];
                        }
                        output_data[output_index] /= mean_n;
                    }
                }
            }
        }
    }

    return CSINN_TRUE;
}

static int csi_unsorted_segment_mean_u8(struct csi_tensor *input,
                            struct csi_tensor *segment_ids,
                            struct csi_tensor *output,
                            struct segment_params *params)
{
    uint8_t *input_data  = input->data;
    int *segment_data  = segment_ids->data;
    uint8_t *output_data = output->data;

    int input_dim    = input->dim_count;
    int num_segments = params->num_segments;
    int index[input->dim[0]];
    int size = input->dim[1] * input->dim[2] * input->dim[3];

    for(int n = 0; n < num_segments; n++) {
        /* init the outputdata data */
        for(int h = 0; h < input->dim[1]; h++) {
            for(int w = 0; w < input->dim[2]; w++) {
                for(int c = 0; c < input->dim[3]; c++) {
                    int output_index = csi_get_index(input->dim, n, h, w, c);
                    output_data[output_index] = csi_quantize_f32(0, output->offset,
                                                output->multiplier, output->shift);
                }
            }
        }
        int num = 0;

        for(int i = 0; i < input->dim[0]; i++) {
            if (segment_data[i] == n) {
                index[num] = i;
                num++;
            }
        }
        int mean_n = num;
        if (mean_n > 0) {
            for(int h = 0; h < input->dim[1]; h++) {
                for(int w = 0; w < input->dim[2]; w++) {
                    for(int c = 0; c < input->dim[3]; c++) {
                        int32_t output_index = csi_get_index(input->dim, n, h, w, c);
                        float temp_sum = 0;
                        for(int k = 0; k < num; k++) {
                            int32_t input_index = csi_get_index(input->dim, index[k], h, w, c);
                            float input_value = csi_dequantize_f32(input_data[input_index], input->offset,
                                                input->multiplier, input->shift);
                            temp_sum += input_value;
                        }
                        float mean_value = temp_sum / mean_n;
                        output_data[output_index] = csi_quantize_f32(mean_value, output->offset,
                                                    output->multiplier, output->shift);
                    }
                }
            }

        }
    }

    return CSINN_TRUE;
}

static int csi_segment_mean_u8(struct csi_tensor *input,
                            struct csi_tensor *segment_ids,
                            struct csi_tensor *output,
                            struct segment_params *params)
{
    uint8_t *input_data  = input->data;
    int *segment_data  = segment_ids->data;
    uint8_t *output_data = output->data;

    int input_dim    = input->dim_count;
    int num_segments = params->num_segments;
    int index[input->dim[0]];
    int i = 0;
    int size = input->dim[1] * input->dim[2] * input->dim[3];

    for(int n = 0; n < num_segments; n++) {
        /* init the outputdata data */
        for(int h = 0; h < input->dim[1]; h++) {
            for(int w = 0; w < input->dim[2]; w++) {
                for(int c = 0; c < input->dim[3]; c++) {
                    int output_index = csi_get_index(input->dim, n, h, w, c);
                    output_data[output_index] = csi_quantize_f32(0, output->offset,
                                                output->multiplier, output->shift);
                }
            }
        }
        int num = 0;
        for(; i < input->dim[0]; i++) {
            if (segment_data[i] == n) {
                index[num] = i;
                num++;
            } else {
                break;
            }
        }
        int mean_n = num;
        if(num > 0) {
            for(int h = 0; h < input->dim[1]; h++) {
                for(int w = 0; w < input->dim[2]; w++) {
                    for(int c = 0; c < input->dim[3]; c++) {
                        int32_t output_index = csi_get_index(input->dim, n, h, w, c);
                        float temp_sum = 0;
                        for(int k = 0; k < num; k++) {
                            int32_t input_index = csi_get_index(input->dim, index[k], h, w, c);
                            float input_value = csi_dequantize_f32(input_data[input_index], input->offset,
                                                input->multiplier, input->shift);
                            temp_sum += input_value;
                        }
                        float mean_value = temp_sum / mean_n;
                        output_data[output_index] = csi_quantize_f32(mean_value, output->offset,
                                                    output->multiplier, output->shift);
                    }
                }
            }
        }
    }

    return CSINN_TRUE;
}

int csi_segment_mean_init(struct csi_tensor *input0,
                     struct csi_tensor *input1,
                     struct csi_tensor *output,
                     struct segment_params *params)
{
    if (input0->dtype == CSINN_DTYPE_UINT8) {
        if (params->unsorted == CSINN_TRUE) {
            params->bc = csi_unsorted_segment_mean_u8;
        } else {
            params->bc = csi_segment_mean_u8;
        }
    } else if (input0->dtype == CSINN_DTYPE_FLOAT32) {
        if (params->unsorted == CSINN_TRUE) {
            params->bc = csi_unsorted_segment_mean_f32;
        } else {
            params->bc = csi_segment_mean_f32;
        }
    } else {
        return CSINN_UNSUPPORT_DTYPE;
    }
    return CSINN_TRUE;
}

int csi_segment_mean(struct csi_tensor *input0,
                struct csi_tensor *input1,
                struct csi_tensor *output,
                struct segment_params *params)
{
    if (params->bc != NULL) {
        params->bc(input0, input1, output, params);
    } else {
        return CSINN_CALLBACK_UNSET;
    }
    return CSINN_TRUE;
}