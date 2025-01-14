/*
 * Copyright (C) 2016-2022 T-Head Semiconductor Co., Ltd. All rights reserved.
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

/* ----------------------------------------------------------------------
 * Title:        shl_depthwise_separable_conv_HWC_q7.c
 * Description:  Q7 depthwise separable convolution function
 *
 * -------------------------------------------------------------------- */

#include "i805_ref_function.h"

/**
 * @brief Q7 depthwise separable convolution function
 * @param[in]       Im_in       pointer to input tensor
 * @param[in]       dim_im_in   input tensor dimention
 * @param[in]       ch_im_in    number of input tensor channels
 * @param[in]       wt          pointer to kernel weights
 * @param[in]       ch_im_out   number of filters, i.e., output tensor channels
 * @param[in]       dim_kernel  filter kernel size
 * @param[in]       padding     padding sizes
 * @param[in]       stride      convolution stride
 * @param[in]       bias        pointer to bias
 * @param[in]       bias_shift  amount of left-shift for bias
 * @param[in]       out_shift   amount of right-shift for output
 * @param[in,out]   Im_out      pointer to output tensor
 * @param[in]       dim_im_out  output tensor dimension
 * @param[in,out]   bufferA     pointer to buffer space for input
 * @return     The function returns either
 * <code>CSI_MATH_SIZE_MISMATCH</code> or <code>CSI_MATH_SUCCESS</code> based on the outcome of size
 * checking.
 *
 * @details
 *
 * <b>Buffer size:</b>
 *
 * bufferA size: 2*ch_im_in*dim_kernel*dim_kernel
 *
 * <b>Input dimension constraints:</b>
 *
 * ch_im_in equals ch_im_out
 *
 * Implementation:
 * There are 3 nested loop here:
 * Inner loop: calculate each output value with MAC instruction over an accumulator
 * Mid   loop: loop over different output channel
 * Outer loop: loop over different output (x, y)
 */

void shl_depthwise_separable_conv_HWC_q7(const q7_t* Im_in, const uint16_t dim_im_in,
                                         const uint16_t ch_im_in, const q7_t* wt,
                                         const uint16_t ch_im_out, const uint16_t dim_kernel,
                                         const uint16_t padding, const uint16_t stride,
                                         const q7_t* bias, const uint16_t bias_shift,
                                         const uint16_t out_shift, q7_t* Im_out,
                                         const uint16_t dim_im_out, q15_t* bufferA)
{
    int i_out_y, i_out_x, i_ch_out, i_ker_x, i_ker_y;
    int conv_out;

    /* do some checking here, basically ch_im_in == ch_im_out */
    if (ch_im_in != ch_im_out) {
        return;
    }

    for (i_out_y = 0; i_out_y < dim_im_out; i_out_y++) {
        for (i_out_x = 0; i_out_x < dim_im_out; i_out_x++) {
            for (i_ch_out = 0; i_ch_out < ch_im_out; i_ch_out++) {
                // for each output
                conv_out = ((q31_t)(bias[i_ch_out]) << bias_shift) + NN_ROUND(out_shift);
                for (i_ker_y = 0; i_ker_y < dim_kernel; i_ker_y++) {
                    for (i_ker_x = 0; i_ker_x < dim_kernel; i_ker_x++) {
                        int in_row = stride * i_out_y + i_ker_y - padding;
                        int in_col = stride * i_out_x + i_ker_x - padding;
                        if (in_row >= 0 && in_col >= 0 && in_row < dim_im_in &&
                            in_col < dim_im_in) {
                            conv_out += Im_in[(in_row * dim_im_in + in_col) * ch_im_in + i_ch_out] *
                                        wt[(i_ker_y * dim_kernel + i_ker_x) * ch_im_out + i_ch_out];
                        }
                    }
                }
                Im_out[(i_out_y * dim_im_out + i_out_x) * ch_im_out + i_ch_out] =
                    (q7_t)__SSAT((conv_out >> out_shift), 8);
            }
        }
    }

    return;
}
