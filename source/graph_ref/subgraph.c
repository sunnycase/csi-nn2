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

/* CSI-NN2 version 2.0.x */

#include "shl_gref.h"

void shl_subgraph_alloc(struct shl_node *node, struct shl_ref_graph *ograph,
                        struct shl_ref_graph *ggraph)
{
    int node_input_num = 0;
    for (int i = 0; i < node->in_num; i++) {
        struct csinn_tensor *node_in = node->in[i]->data;
        if (!node_in->is_const) {
            node_input_num++;
        }
    }
    struct shl_ref_graph *sgraph = shl_mem_alloc(sizeof(struct shl_ref_graph));
    sgraph->input_num = node_input_num;
    sgraph->output_num = node->out_num;
    sgraph->input = shl_mem_alloc(sgraph->input_num * sizeof(struct shl_node *));
    sgraph->output = shl_mem_alloc(sgraph->output_num * sizeof(struct shl_node *));
    shl_gref_graph_insert(node, sgraph);

    struct shl_node *sg_in =
        shl_node_alloc(CSINN_SUBGRAPH, "graph_in", node_input_num, node_input_num, sgraph);
    shl_gref_graph_insert(sg_in, ggraph);
    sg_in->subgraph_idx = ggraph->layer_index - 1;
    node->subgraph_idx = ggraph->layer_index - 1;
    for (int i = 0; i < node_input_num; i++) {
        sg_in->in[i] = node->in[i];
        struct csinn_tensor *sg_in_tensor = csinn_alloc_tensor(NULL);
        csinn_tensor_copy(sg_in_tensor, node->in[i]->data);
        struct shl_node *sg_in_node = shl_node_var_alloc("graph_in_tensor", sg_in_tensor);
        sg_in_node->subgraph_idx = ggraph->layer_index - 1;
        node->in[i] = sg_in_node;
        sg_in_node->out[0] = node;

        sgraph->input[i] = sg_in_node;
    }

    // sgraph->input[0] = node->in[0];
    // sgraph->output[0] = node->out[0];

    struct shl_node *sg_out = shl_node_alloc(CSINN_SUBGRAPH_RETURN, "graph_out", node->out_num,
                                             node->out_num, ggraph->layer[ggraph->layer_index]);
    shl_gref_graph_insert(sg_out, sgraph);
    sg_out->subgraph_idx = ggraph->layer_index - 1;
    for (int i = 0; i < node->out_num; i++) {
        sg_out->out[i] = node->out[i];
        node->out[i]->in[0] = sg_out;
        struct csinn_tensor *sg_out_tensor = csinn_alloc_tensor(NULL);
        csinn_tensor_copy(sg_out_tensor, node->out[i]->data);
        struct shl_node *sg_out_node = shl_node_var_alloc("graph_out_tensor", sg_out_tensor);
        sg_out_node->subgraph_idx = ggraph->layer_index - 1;
        node->out[i] = sg_out_node;
        sg_out_node->in[0] = node;
        sg_out->in[i] = sg_out_node;

        sgraph->output[i] = sg_out->out[i];
    }
}

static void set_sub_session(struct csinn_session *sub_sess, struct csinn_params_base *params,
                            struct shl_ref_graph *graph)
{
    struct csinn_session *base_sess = params->sess;
    sub_sess->base_api = params->api;
    if (params->api == CSINN_LIGHT) {
        sub_sess->base_dtype = base_sess->base_dtype;
        sub_sess->debug_level = base_sess->debug_level;
        sub_sess->base_run_mode = CSINN_RM_NPU_GRAPH;
        if (params->quant_type != CSINN_QUANT_UNSET) {
            sub_sess->base_quant_type = params->quant_type;
        } else {
            sub_sess->base_quant_type = base_sess->base_quant_type;
        }

        if (params->quant_type == CSINN_QUANT_INT16_SYM) {
            sub_sess->base_dtype = CSINN_DTYPE_INT16;
        } else if (params->quant_type == CSINN_QUANT_INT8_ASYM ||
                   params->quant_type == CSINN_QUANT_INT8_SYM) {
            sub_sess->base_dtype = CSINN_DTYPE_INT8;
        } else if (params->quant_type == CSINN_QUANT_UINT8_ASYM ||
                   params->quant_type == CSINN_QUANT_UINT8_SYM) {
            sub_sess->base_dtype = CSINN_DTYPE_UINT8;
        } else if (params->quant_type == CSINN_QUANT_INT4_SYM) {
            sub_sess->base_dtype = CSINN_DTYPE_INT4;
        }
    } else if (params->api = CSINN_ASP) {
        sub_sess->base_dtype = base_sess->base_dtype;
        sub_sess->debug_level = base_sess->debug_level;
        sub_sess->base_quant_type = base_sess->base_quant_type;
        sub_sess->td = shl_mem_alloc(sizeof(struct shl_gref_target_data));
        /* ASP: reuse gref graph */
        struct shl_gref_target_data *td = sub_sess->td;
        td->graph = graph;
    } else {
        shl_debug_error("sub session api unsupport\n");
    }
}

int shl_subgraph_setup(struct shl_node *n)
{
    struct shl_ref_graph *sgraph = n->data;
    struct shl_node *init_node = sgraph->layer[0];
    struct csinn_params_base *init_params = init_node->data;
    struct csinn_session *sub_sess = csinn_alloc_session();
    set_sub_session(sub_sess, init_params, sgraph);
    csinn_session_init(sub_sess);

    csinn_set_input_number(sgraph->input_num, sub_sess);
    csinn_set_output_number(sgraph->output_num, sub_sess);

    /* set input tensor */
    for (int i = 0; i < sgraph->input_num; i++) {
        struct csinn_tensor *input_t;
        input_t = sgraph->input[i]->data;
        input_t->sess = sub_sess;
        csinn_set_tensor_entry(input_t, sub_sess);
        csinn_set_input(i, input_t, sub_sess);
    }

    int ret = CSINN_TRUE;
    for (int idx = 0; idx < sgraph->layer_index; idx++) {
        struct shl_node *node = sgraph->layer[idx];
        if (node->type == CSINN_SUBGRAPH_RETURN) continue;

        struct csinn_params_base *params = node->data;
        params->sess = sub_sess;
        int (*func)();
        struct csinn_tensor *input0, *output, *kernel, *bias;
        input0 = node->in[0]->data;
        input0->sess = sub_sess;

        shl_op_callback_map(params, node->type, input0->dtype);
        struct csinn_callback *cb = params->cb;
        func = cb->est;

        switch (node->type) {
            case CSINN_OP_ABS:
            case CSINN_OP_ACOS:
            case CSINN_OP_ACOSH:
            case CSINN_OP_ANY:
            case CSINN_OP_ARGMAX:
            case CSINN_OP_ARGMIN:
            case CSINN_OP_ASIN:
            case CSINN_OP_ASINH:
            case CSINN_OP_ATAN:
            case CSINN_OP_ATANH:
            case CSINN_OP_AVGPOOL2D:
            case CSINN_OP_AVGPOOL3D:
            case CSINN_OP_BATCH_TO_SPACE:
            case CSINN_OP_BATCH_TO_SPACE_ND:
            case CSINN_OP_BROADCOST:
            case CSINN_OP_CEIL:
            case CSINN_OP_CLIP:
            case CSINN_OP_COL2IM:
            case CSINN_OP_COS:
            case CSINN_OP_COSH:
            case CSINN_OP_CROP:
            case CSINN_OP_CUMPROD:
            case CSINN_OP_CUMSUM:
            case CSINN_OP_DEPTH_TO_SPACE:
            case CSINN_OP_ELU:
            case CSINN_OP_ERF:
            case CSINN_OP_EXP:
            case CSINN_OP_EXPAND_DIMS:
            case CSINN_OP_EXPM1:
            case CSINN_OP_FLATTEN:
            case CSINN_OP_FLOOR:
            case CSINN_OP_GLOBAL_AVGPOOL2D:
            case CSINN_OP_GLOBAL_MAXPOOL2D:
            case CSINN_OP_HARD_SIGMOID:
            case CSINN_OP_IM2COL:
            case CSINN_OP_ISNAN:
            case CSINN_OP_L2N:
            case CSINN_OP_L2POOL2D:
            case CSINN_OP_LEAKY_RELU:
            case CSINN_OP_LOG_SOFTMAX:
            case CSINN_OP_LOG:
            case CSINN_OP_LOG1P:
            case CSINN_OP_LOGICAL_NOT:
            case CSINN_OP_LRN:
            case CSINN_OP_MAX:
            case CSINN_OP_MAXPOOL2D:
            case CSINN_OP_MAXPOOL2D_LOCAT:
            case CSINN_OP_MAXPOOL3D:
            case CSINN_OP_MEAN:
            case CSINN_OP_MIN:
            case CSINN_OP_NDARRAY_SIZE:
            case CSINN_OP_NEGATIIVE:
            case CSINN_OP_NOT:
            case CSINN_OP_PAD:
            case CSINN_OP_PROD:
            case CSINN_OP_REDUCE_LOGSUMEXP:
            case CSINN_OP_REDUCE_MAX:
            case CSINN_OP_REDUCE_MEAN:
            case CSINN_OP_REDUCE_MIN:
            case CSINN_OP_REDUCE_PROD:
            case CSINN_OP_REDUCE_SUM:
            case CSINN_OP_RELU:
            case CSINN_OP_RELU1:
            case CSINN_OP_RELU6:
            case CSINN_OP_RELUN:
            case CSINN_OP_REORG:
            case CSINN_OP_RESHAPE:
            case CSINN_OP_RESIZE:
            case CSINN_OP_REVERSE:
            case CSINN_OP_ROUND:
            case CSINN_OP_RSQRT:
            case CSINN_OP_SHAPE:
            case CSINN_OP_SHUFFLE_CHANNEL:
            case CSINN_OP_SIGMOID:
            case CSINN_OP_SIGN:
            case CSINN_OP_SIN:
            case CSINN_OP_SINH:
            case CSINN_OP_SLICE:
            case CSINN_OP_SOFTMAX:
            case CSINN_OP_SOFTPLUS:
            case CSINN_OP_SOFTRELU:
            case CSINN_OP_SOFTSIGN:
            case CSINN_OP_SPACE_TO_BATCH:
            case CSINN_OP_SPACE_TO_BATCH_ND:
            case CSINN_OP_SPACE_TO_DEPTH:
            case CSINN_OP_SQRT:
            case CSINN_OP_SQUARE:
            case CSINN_OP_SQUEEZE:
            case CSINN_OP_STACK:
            case CSINN_OP_STRIDED_SLICE:
            case CSINN_OP_SUM:
            case CSINN_OP_TAN:
            case CSINN_OP_TANH:
            case CSINN_OP_THRESHOLD_RELU:
            case CSINN_OP_TILE:
            case CSINN_OP_TRANSPOSE:
            case CSINN_OP_TRUNC:
            case CSINN_OP_UNPOOLING:
            case CSINN_OP_UNSTACK:
            case CSINN_OP_YUV_RGB_SCALE:
                output = node->out[0]->data;
                output->sess = sub_sess;
                ret = func(input0, output, params);
                break;
            case CSINN_OP_ADD:
            case CSINN_OP_MUL: {
                output = node->out[0]->data;
                output->sess = sub_sess;
                struct csinn_tensor *rhs = node->in[1]->data;
                rhs->sess = sub_sess;
                ret = func(input0, rhs, output, params);
                break;
            }
            case CSINN_OP_CONV2D:
            case CSINN_OP_CONV2D_RELU:
            case CSINN_OP_CONV2D_RELU6:
            case CSINN_OP_CONV2D_CHANNEL:
            case CSINN_OP_CONV2D_CHANNEL_RELU:
            case CSINN_OP_CONV2D_CHANNEL_RELU6:
            case CSINN_OP_DEPTHWISE_CONV2D:
            case CSINN_OP_DEPTHWISE_CONV2D_RELU:
            case CSINN_OP_DEPTHWISE_CONV2D_RELU6:
            case CSINN_OP_DEPTHWISE_CONV2D_CHANNEL:
            case CSINN_OP_DEPTHWISE_CONV2D_CHANNEL_RELU:
            case CSINN_OP_DEPTHWISE_CONV2D_CHANNEL_RELU6:
            case CSINN_OP_GROUP_CONV2D:
            case CSINN_OP_GROUP_CONV2D_RELU:
            case CSINN_OP_GROUP_CONV2D_RELU6:
            case CSINN_OP_GROUP_CONV2D_CHANNEL:
            case CSINN_OP_GROUP_CONV2D_CHANNEL_RELU:
            case CSINN_OP_CONV3D:
            case CSINN_OP_DECONV2D:
            case CSINN_OP_DEPTHWISE_DECONV2D:
            case CSINN_OP_DECONV3D:
            case CSINN_OP_FULLYCONNECTED:
                output = node->out[0]->data;
                output->sess = sub_sess;
                kernel = node->in[1]->data;
                kernel->sess = sub_sess;
                bias = node->in[2]->data;
                bias->sess = sub_sess;
                ret = func(input0, output, kernel, bias, params);
                break;
            case CSINN_OP_SPLIT: {
                struct csinn_tensor **split_output =
                    shl_mem_alloc(sizeof(struct csinn_tensor *) * node->out_num);
                for (int i = 0; i < node->out_num; i++) {
                    split_output[i] = node->out[i]->data;
                    split_output[i]->sess = sub_sess;
                }
                ret = func(input0, split_output, params);
                break;
            }
            case CSINN_OP_CONCAT: {
                struct csinn_tensor **concat_input =
                    shl_mem_alloc(sizeof(struct csinn_tensor *) * node->in_num);
                for (int i = 0; i < node->in_num; i++) {
                    concat_input[i] = node->in[i]->data;
                    concat_input[i]->sess = sub_sess;
                }
                output = node->out[0]->data;
                output->sess = sub_sess;
                ret = func(concat_input, output, params);
                break;
            }
            default:
                shl_debug_error("%s unknown op\n", __func__);
                return CSINN_FALSE;
        }
    }
    /* set output tensor */
    int i = 0;
    for (i = 0; i < sgraph->layer_index; i++) {
        if (sgraph->layer[i]->type == CSINN_SUBGRAPH_RETURN) {
            break;
        }
    }
    struct shl_node *return_node = sgraph->layer[i];
    for (int i = 0; i < return_node->in_num; i++) {
        struct csinn_tensor *output_t;
        output_t = return_node->in[i]->data;
        output_t->sess = sub_sess;
        csinn_set_output(i, output_t, sub_sess);
    }

    csinn_session_setup(sub_sess);

    return ret;
}

int shl_subgraph_deinit(struct shl_node *n)
{
    struct shl_ref_graph *sgraph = n->data;
    struct shl_node *node = sgraph->layer[0];
    struct csinn_params_base *params = node->data;
    csinn_session_deinit(params->sess);
    return 0;
}

static int shl_subgraph_entry(struct shl_node *n)
{
    struct shl_ref_graph *sgraph = n->data;

    for (int i = 0; i < n->in_num; i++) {
        struct csinn_tensor *tsrc = n->in[i]->data;
        struct csinn_tensor *tdst = sgraph->input[i]->data;

        if (tdst->sess->base_api == CSINN_LIGHT &&
            (tdst->sess->base_quant_type == CSINN_QUANT_INT16_SYM ||
             tdst->sess->base_quant_type == CSINN_QUANT_INT8_SYM)) {
            struct csinn_tensor *tdst_cp = csinn_alloc_tensor(NULL);
            csinn_tensor_copy(tdst_cp, tdst);
            tdst_cp->data = shl_mem_alloc(csinn_tensor_byte_size(tdst_cp));
            csinn_tensor_data_convert(tdst_cp, tsrc);

            tdst->data = tdst_cp->data;
        } else {
            tdst->data = tsrc->data;
        }
        // if (tdst->data == NULL) {
        // tdst->data = tsrc->data;
        // } else if (tdst->data != tsrc->data) {
        //     memcpy(tdst->data, tsrc->data, csinn_tensor_byte_size(tsrc));
        // }
    }
    for (int i = 0; i < sgraph->output_num; i++) {
        struct csinn_tensor *out = sgraph->output[i]->data;
        out->data = NULL;
    }
    return CSINN_TRUE;
}

static int shl_subgraph_return(struct shl_ref_graph *graph, struct shl_node *ret_node)
{
    for (int i = 0; i < graph->output_num; i++) {
        struct csinn_tensor *tsrc = ret_node->in[i]->data;
        struct csinn_tensor *tdst = graph->output[i]->data;

        if (tsrc->sess->base_api == CSINN_LIGHT &&
            (tsrc->sess->base_quant_type == CSINN_QUANT_INT16_SYM ||
             tsrc->sess->base_quant_type == CSINN_QUANT_INT8_SYM)) {
            struct csinn_tensor *tdst_cp = csinn_alloc_tensor(NULL);
            csinn_tensor_copy(tdst_cp, tdst);
            tdst_cp->data = shl_mem_alloc(csinn_tensor_byte_size(tdst_cp));
            csinn_tensor_data_convert(tdst_cp, tsrc);

            tdst->data = tdst_cp->data;
        } else {
            tdst->data = tsrc->data;
        }

        // if (tdst->data == NULL) {
        // tdst->data = tsrc->data;
        // } else if (tdst->data != tsrc->data) {
        //     memcpy(tdst->data, tsrc->data, csinn_tensor_byte_size(tsrc));
        // }
    }
    return CSINN_TRUE;
}

int shl_subgraph_run_init(struct shl_node *n) { shl_subgraph_entry(n); }

int shl_subgraph_run_deinit(struct shl_node *n) {}

int shl_subgraph_run(struct shl_node *n)
{
    struct shl_ref_graph *sgraph = n->data;
    struct shl_node *node = sgraph->layer[0];
    struct csinn_params_base *params = node->data;
    int ret = CSINN_TRUE;
    struct csinn_tensor **inputs;
    struct csinn_tensor **outputs;

    for (int i = 0; i < sgraph->input_num; i++) {
        csinn_update_input(i, sgraph->input[i]->data, params->sess);
    }
    csinn_session_run(params->sess);

    int i;
    for (i = 0; i < sgraph->layer_index; i++) {
        if (sgraph->layer[i]->type == CSINN_SUBGRAPH_RETURN) {
            break;
        }
    }
    struct shl_node *return_node = sgraph->layer[i];

    for (int i = 0; i < return_node->in_num; i++) {
        csinn_get_output(i, return_node->in[i]->data, params->sess);
    }

    /* CSINN_SUBGRAPH_RETURN */
    shl_subgraph_return(sgraph, return_node);
    return ret;
}

struct shl_node *shl_gref_get_input_subgraph(struct shl_ref_graph *graph, struct shl_node *node,
                                             int index)
{
    struct shl_node *next_node = node->in[index]->in[0];
    if (next_node && next_node->type == CSINN_SUBGRAPH_RETURN) {
        next_node = graph->layer[next_node->subgraph_idx];
    }
    return next_node;
}

int shl_subgraph_get_device(struct shl_node *node)
{
    int device = -1;
    struct csinn_params_base *params;
    if (node->type == CSINN_SUBGRAPH) {
        struct shl_ref_graph *sgraph = node->data;
        params = sgraph->layer[0]->data;
        device = params->api;
    } else if (node->type >= 0 && node->type < CSINN_OP_SIZE) {
        params = node->data;
        device = params->api;
    } else {
        shl_debug_error("unknown node type.\n");
    }
    return device;
}

void shl_subgraph_fvisit_print(struct shl_ref_graph *graph, struct shl_node *node)
{
    printf("%s\n", node->name);
}

int shl_is_restricted_by_node(int subgraph_idx, struct shl_node *node, struct shl_ref_graph *graph)
{
    int find_flag = 0;

    int queue_size = 32;
    struct shl_node **node_queue = shl_mem_alloc(sizeof(struct shl_node *) * queue_size);
    int queue_left = 0;
    int queue_right = 0;
    /* add current node into queue */
    node_queue[queue_right++] = node;
    while (queue_right > queue_left) {
        struct shl_node *curr_node = node_queue[queue_left];
        queue_left++;
        /* determine whether subgraph_idx is restricted by node */
        for (int i = 0; i < curr_node->restricted_map_num; i++) {
            if (subgraph_idx == curr_node->restricted_map[i]) {
                find_flag = 1;
                /* break loop */
                queue_left = queue_right;
                break;
            }
        }
        /* add input nodes of curr_node into queue. */
        /* FIXME(@chenf) it's possible to add node into queue repeatly. */
        int input_num = 0;
        if (curr_node->type == CSINN_SUBGRAPH) {
            input_num = ((struct shl_ref_graph *)curr_node->data)->input_num;
        } else {
            input_num = curr_node->in_num;
        }
        for (int i = 0; i < input_num; i++) {
            struct shl_node *next_node = NULL;
            if (curr_node->type == CSINN_SUBGRAPH) {
                if (((struct shl_ref_graph *)curr_node->data)->input[i]->in) {
                    next_node = ((struct shl_ref_graph *)curr_node->data)->input[i]->in[0];
                }
            } else {
                if (curr_node->in[i]->in) {
                    next_node = curr_node->in[i]->in[0];
                }
            }
            if (next_node) {
                next_node = graph->layer[next_node->subgraph_idx];
            }

            if (next_node) {
                if (queue_right >= queue_size) {
                    queue_size += 32;
                    node_queue =
                        shl_mem_realloc(node_queue, sizeof(struct shl_node *) * queue_size);
                }
                node_queue[queue_right++] = next_node;
            }
        }
    }
    shl_mem_free(node_queue);
    return find_flag;
}

void shl_subgraph_fvisit_fuse(struct shl_ref_graph *graph, struct shl_node *node)
{
    /* CPU nodes needn't be added into subgraph. */
    struct csinn_params_base *params = node->data;
    if (params->api == params->sess->base_api) {
        node->subgraph_idx = graph->layer_index;
        shl_gref_graph_insert(node, graph);

        for (int m = 0; m < shl_node_get_non_const_in_number(node); m++) {
            struct shl_node *m_node = shl_gref_get_input_subgraph(graph, node, m);
            if (m_node) {
                shl_node_restrict_map_insert(m_node->subgraph_idx,
                                             graph->layer[node->subgraph_idx]);
            }
        }
        return;
    }
    if (shl_gref_is_root_node(graph, node)) {
        /* create subgraph node */
        struct shl_ref_graph *sgraph = shl_mem_alloc(sizeof(struct shl_ref_graph));
        struct shl_node *sg_in = shl_node_alloc(CSINN_SUBGRAPH, "graph_in", 0, 0, sgraph);
        node->subgraph_idx = graph->layer_index;
        sg_in->subgraph_idx = graph->layer_index;
        shl_gref_graph_insert(node, sgraph);
        shl_gref_graph_insert(sg_in, graph);

        shl_gref_update_input_output(graph, sg_in->subgraph_idx);
        return;
    }
    int i;
    int can_fuse = 0;
    for (i = 0; i < shl_node_get_non_const_in_number(node); i++) {
        struct shl_node *i_node = shl_gref_get_input_subgraph(graph, node, i);
        if (!i_node) continue;

        int i_device = shl_subgraph_get_device(i_node);
        int curr_device = shl_subgraph_get_device(node);
        if (i_device == curr_device) {
            int is_restrict = 0;
            /* determine whether the i-th input subgraph is restricted by other input subgraph. */
            for (int j = 0; j < shl_node_get_non_const_in_number(node); j++) {
                if (i == j) continue;
                struct shl_node *j_node = shl_gref_get_input_subgraph(graph, node, j);
                if (!j_node) continue;
                int find_flag = 0;

                struct shl_node *j_subgraph = graph->layer[j_node->subgraph_idx];
                // if (j_subgraph->restricted_map_num == 0) break;

                // for (int k = 0; k < j_subgraph->restricted_map_num; k++) {
                //     if (i_node->subgraph_idx == j_subgraph->restricted_map[k]) {
                //         find_flag = 1;
                //         break;
                //     }
                // }

                find_flag = shl_is_restricted_by_node(i_node->subgraph_idx, j_subgraph, graph);

                if (find_flag) {
                    is_restrict = 1;
                    break;
                }
            }
            if (!is_restrict) {
                /* add current node into its i-th input subgraph. */
                node->subgraph_idx = i_node->subgraph_idx;
                struct shl_ref_graph *sgraph = graph->layer[i_node->subgraph_idx]->data;
                shl_gref_graph_insert(node, sgraph);

                shl_gref_update_input_output(graph, i_node->subgraph_idx);
                can_fuse = 1;
                break;
            }
        }
    }

    if (can_fuse) {
        /* Try to fuse input subgraph into current subgraph. */
        for (int m = 0; m < shl_node_get_non_const_in_number(node); m++) {
            if (m == i) continue;
            struct shl_node *m_node = shl_gref_get_input_subgraph(graph, node, m);
            if (!m_node) continue;
            if (m_node->subgraph_idx == node->subgraph_idx) continue;
            int curr_device = shl_subgraph_get_device(node);
            int m_device = shl_subgraph_get_device(m_node);

            if (curr_device == m_device) {
                /* fusing subgraphs. */
                struct shl_node *m_subgraph = graph->layer[m_node->subgraph_idx];
                struct shl_ref_graph *sgraph = m_subgraph->data;
                shl_gref_update_input_output(graph, m_node->subgraph_idx);

                int is_restrict = 0;
                for (int n = 0; n < sgraph->input_num; n++) {
                    if (sgraph->input[n]->in[0] == NULL) {
                        // m_node has no subgraph input.
                        continue;
                    }
                    int in_m_subgraph_index = sgraph->input[n]->in[0]->subgraph_idx;
                    int find_flag = 0;
                    // for (int nr = 0; nr < graph->layer[in_m_subgraph_index]->restricted_map_num;
                    //      nr++) {
                    //     if (node->subgraph_idx ==
                    //         graph->layer[in_m_subgraph_index]->restricted_map[nr]) {
                    //         find_flag = 1;
                    //         break;
                    //     }
                    // }
                    find_flag = shl_is_restricted_by_node(node->subgraph_idx,
                                                          graph->layer[in_m_subgraph_index], graph);
                    if (find_flag) {
                        is_restrict = 1;
                        break;
                    }
                }

                struct shl_ref_graph *curr_sgraph = graph->layer[node->subgraph_idx]->data;
                shl_gref_update_input_output(graph, node->subgraph_idx);

                int is_restrict2 = 0;
                for (int n = 0; n < curr_sgraph->input_num; n++) {
                    if (curr_sgraph->input[n]->in[0] == NULL) {
                        // curr_node has no subgraph input.
                        continue;
                    }
                    int in_m_subgraph_index = curr_sgraph->input[n]->in[0]->subgraph_idx;
                    int find_flag = 0;
                    // for (int nr = 0; nr < graph->layer[in_m_subgraph_index]->restricted_map_num;
                    //      nr++) {
                    //     if (m_node->subgraph_idx ==
                    //         graph->layer[in_m_subgraph_index]->restricted_map[nr]) {
                    //         find_flag = 1;
                    //         break;
                    //     }
                    // }
                    find_flag = shl_is_restricted_by_node(m_node->subgraph_idx,
                                                          graph->layer[in_m_subgraph_index], graph);
                    if (find_flag) {
                        is_restrict2 = 1;
                        break;
                    }
                }

                if (!is_restrict && !is_restrict2) {
                    /* can fuse subgraph into current subgraph. */
                    for (int n = 0; n < sgraph->layer_index; n++) {
                        struct shl_node *subgraph_node = sgraph->layer[n];
                        subgraph_node->subgraph_idx = node->subgraph_idx;
                        shl_gref_graph_insert(subgraph_node, curr_sgraph);

                        shl_gref_update_input_output(graph, node->subgraph_idx);
                    }
                    for (int n = 0; n < m_subgraph->restricted_map_num; n++) {
                        shl_node_restrict_map_insert(m_subgraph->restricted_map[n],
                                                     graph->layer[node->subgraph_idx]);
                    }
                    sgraph->layer_index = 0;
                    sgraph->layer_size = 0;
                } else {
                    shl_node_restrict_map_insert(node->subgraph_idx, m_subgraph);
                }
            } else {
                shl_node_restrict_map_insert(m_node->subgraph_idx,
                                             graph->layer[node->subgraph_idx]);
            }
        }
    } else {
        /* current node is restricted from being fused into input subgraph by other subgraph.
         * so create new subgraph and update its restricted_map.
         */
        struct shl_ref_graph *sgraph = shl_mem_alloc(sizeof(struct shl_ref_graph));
        struct shl_node *sg_in = shl_node_alloc(CSINN_SUBGRAPH, "graph_in", 1, 1, sgraph);
        node->subgraph_idx = graph->layer_index;
        sg_in->subgraph_idx = graph->layer_index;
        shl_gref_graph_insert(node, sgraph);
        shl_gref_graph_insert(sg_in, graph);

        shl_gref_update_input_output(graph, sg_in->subgraph_idx);

        for (int m = 0; m < shl_node_get_non_const_in_number(node); m++) {
            struct shl_node *m_node = shl_gref_get_input_subgraph(graph, node, m);
            if (m_node) {
                shl_node_restrict_map_insert(m_node->subgraph_idx,
                                             graph->layer[node->subgraph_idx]);
            }
        }
    }
    return;
}

struct shl_ref_graph *shl_subgraph_generate(struct shl_ref_graph *ograph)
{
    struct shl_ref_graph *ggraph = shl_mem_alloc(sizeof(struct shl_ref_graph));
    ggraph->input = ograph->input;
    ggraph->output = ograph->output;
    ggraph->input_num = ograph->input_num;
    ggraph->output_num = ograph->output_num;

    shl_gref_post_dfs(ggraph, shl_subgraph_fvisit_fuse);

    return ggraph;
}

void shl_subgraph_topology_sort_internal(struct shl_ref_graph *new_graph,
                                         struct shl_ref_graph *old_graph)
{
    int stack_size = 32;
    struct shl_node **node_stack = shl_mem_alloc(sizeof(struct shl_node *) * stack_size);
    int *input_idx_stack = shl_mem_alloc(sizeof(int) * stack_size);
    int stack_top = -1;

    struct shl_node *curr_node;
    for (int i = 0; i < new_graph->output_num; i++) {
        struct csinn_tensor *ot = new_graph->output[i]->data;
        if (ot->is_const) continue;
        curr_node = new_graph->output[i]->in[0];
        if (curr_node->subgraph_idx != -1 &&
            old_graph->layer[curr_node->subgraph_idx]->type == CSINN_SUBGRAPH) {
            // curr_node is subgraph node.
            curr_node = old_graph->layer[curr_node->subgraph_idx];
        }
        if (curr_node->visited == 0) {
            ++stack_top;
            if (stack_top >= stack_size) {
                stack_size += 32;
                node_stack = shl_mem_realloc(node_stack, sizeof(struct shl_node *) * stack_size);
                input_idx_stack = shl_mem_realloc(input_idx_stack, sizeof(int) * stack_size);
            }
            node_stack[stack_top] = curr_node;
            input_idx_stack[stack_top] = 0;
            curr_node->visited = 1;
        }
        while (stack_top != -1) {
            curr_node = node_stack[stack_top];
            if (input_idx_stack[stack_top] == shl_node_get_non_const_in_number(curr_node) ||
                shl_gref_is_root_node(new_graph, curr_node)) {
                shl_gref_graph_insert(curr_node, new_graph);

                --stack_top;
            } else {
                struct shl_node *next_node = curr_node->in[input_idx_stack[stack_top]]->in[0];
                if (next_node && next_node->subgraph_idx != -1 &&
                    old_graph->layer[next_node->subgraph_idx]->type == CSINN_SUBGRAPH) {
                    next_node = old_graph->layer[next_node->subgraph_idx];
                }
                input_idx_stack[stack_top] += 1;
                if (next_node && next_node->visited == 0) {
                    ++stack_top;
                    if (stack_top >= stack_size) {
                        stack_size += 32;
                        node_stack =
                            shl_mem_realloc(node_stack, sizeof(struct shl_node *) * stack_size);
                        input_idx_stack =
                            shl_mem_realloc(input_idx_stack, sizeof(int) * stack_size);
                    }
                    node_stack[stack_top] = next_node;
                    input_idx_stack[stack_top] = 0;
                    next_node->visited = 1;
                }
            }
        }
    }

    shl_mem_free(node_stack);
    shl_mem_free(input_idx_stack);
}

struct shl_ref_graph *shl_subgraph_topology_sort(struct shl_ref_graph *graph)
{
    struct shl_ref_graph *sorted_graph = shl_mem_alloc(sizeof(struct shl_ref_graph));
    sorted_graph->input = graph->input;
    sorted_graph->output = graph->output;
    sorted_graph->input_num = graph->input_num;
    sorted_graph->output_num = graph->output_num;

    shl_subgraph_topology_sort_internal(sorted_graph, graph);
    shl_gref_reset_graph_visit(sorted_graph);

    return sorted_graph;
}

struct shl_ref_graph *shl_subgraph_rebuild(struct shl_ref_graph *subgraph)
{
    struct shl_ref_graph *splited_graph = shl_mem_alloc(sizeof(struct shl_ref_graph));
    splited_graph->input = subgraph->input;
    splited_graph->output = subgraph->output;
    splited_graph->input_num = subgraph->input_num;
    splited_graph->output_num = subgraph->output_num;
    for (int i = 0; i < subgraph->layer_index; i++) {
        struct shl_node *node = subgraph->layer[i];
        if (node->type == CSINN_SUBGRAPH) {
            struct shl_ref_graph *sgraph = node->data;
            if (sgraph->layer_size == 0) continue;

            /* split graph */
            /* for input formal parameters */
            node->in = shl_mem_realloc(node->in, sgraph->input_num * sizeof(struct shl_node *));
            node->in_num = sgraph->input_num;
            for (int in_idx = 0; in_idx < sgraph->input_num; in_idx++) {
                struct shl_node *in_tensor_node = sgraph->input[in_idx];
                node->in[in_idx] = in_tensor_node;

                struct csinn_tensor *sg_in_tensor = csinn_alloc_tensor(NULL);
                csinn_tensor_copy(sg_in_tensor, in_tensor_node->data);
                struct shl_node *sg_in_node = shl_node_var_alloc("graph_in_tensor", sg_in_tensor);
                sgraph->input[in_idx] = sg_in_node;

                for (int l_idx = 0; l_idx < sgraph->layer_index; l_idx++) {
                    struct shl_node *curr_node = sgraph->layer[l_idx];
                    int index = shl_node_find(curr_node->in, curr_node->in_num, in_tensor_node);
                    if (index > -1) {
                        curr_node->in[index] = sg_in_node;
                    }
                }
            }
            /* for output formal parameters */
            struct shl_node *sg_out = shl_node_alloc(CSINN_SUBGRAPH_RETURN, "graph_out",
                                                     sgraph->output_num, sgraph->output_num, NULL);
            for (int out_idx = 0; out_idx < sgraph->output_num; out_idx++) {
                struct shl_node *out_tensor_node = sgraph->output[out_idx];
                sg_out->in[out_idx] = out_tensor_node;

                for (int l_idx = 0; l_idx < sgraph->layer_index; l_idx++) {
                    struct shl_node *curr_node = sgraph->layer[l_idx];
                    int index = shl_node_find(curr_node->out, curr_node->out_num, out_tensor_node);
                    if (index > -1) {
                        struct csinn_tensor *sg_out_tensor = csinn_alloc_tensor(NULL);
                        csinn_tensor_copy(sg_out_tensor, curr_node->out[index]->data);
                        struct shl_node *sg_out_node =
                            shl_node_var_alloc("graph_out_tensor", sg_out_tensor);

                        sg_out->out[out_idx] = sg_out_node;
                    }
                }
            }
            shl_gref_graph_insert(sg_out, sgraph);

            /* update subgraph_idx */
            int curr_subgraph_idx = splited_graph->layer_index;
            for (int idx = 0; idx < sgraph->layer_index; idx++) {
                sgraph->layer[idx]->subgraph_idx = curr_subgraph_idx;
            }
            node->subgraph_idx = curr_subgraph_idx;
            shl_gref_graph_insert(node, splited_graph);
        } else {
            /* update subgraph_idx */
            node->subgraph_idx = splited_graph->layer_index;
            shl_gref_graph_insert(node, splited_graph);
        }
    }
    return splited_graph;
}
