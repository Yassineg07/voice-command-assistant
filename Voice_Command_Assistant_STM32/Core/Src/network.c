/**
  ******************************************************************************
  * @file    network.c
  * @author  AST Embedded Analytics Research Platform
  * @date    2026-07-31T02:31:33+0000
  * @brief   AI Tool Automatic Code Generator for Embedded NN computing
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  ******************************************************************************
  */

#include "ai_lite_inspect.h"
#include "ai_platform_interface.h"
#include "layers.h"
#include "core_convert.h"
#include "network.h"
#include "network_details.h"
#include "network_data.h"
#include "stai_events.h"

#include "lite_operators.h"

#include "ai_lite_inspect.h"
/*****************************************************************************/
#define STAI_INTERNAL_API_MAJOR               (1)
#define STAI_INTERNAL_API_MINOR               (0)
#define STAI_INTERNAL_API_MICRO               (0)

#define STAI_MAGIC                            (0xB1C00100)

/*****************************************************************************/
#define _STAI_CONCAT_ARG(a, b)     a ## b
#define STAI_CONCAT(a, b)         _STAI_CONCAT_ARG(a, b)

/*!  STAI_CAST SECTION                       *********************************/
#define STAI_CAST(type, expr) \
  ((type)(expr))


/*****************************************************************************/
#define STAI_SIZE(_size) \
  ((stai_size)(_size))

/*****************************************************************************/
#define STAI_INIT_BUFFER(_flags, _size, _address) \
  { \
    .size = (_size), \
    .address = (uintptr_t)(_address), \
    .flags = (_flags), \
  }

#define STAI_INIT_TENSOR(_name, _flags, _fmt, _size_bytes, _shape, _scale, _zeropoint) \
  { \
    .size_bytes = (_size_bytes), \
    .flags = (_flags), \
    .format = (stai_format)(_fmt), \
    .shape = STAI_PACK(_shape), \
    .scale = STAI_PACK(_scale), \
    .zeropoint = STAI_PACK(_zeropoint), \
    .name = (_name) \
  }

#define STAI_INIT_ARRAY(_size, _ptr) \
  { .size = STAI_SIZE(_size), .data = STAI_PACK(_ptr) }


#define STAI_CAST_ARRAY(_type, _size, _ptr) \
  { .size = STAI_SIZE(_size), .data = (_type)STAI_PACK(_ptr) }


#define STAI_DECLARE_ARRAY(_type, _size, ...) \
  { .size = STAI_SIZE(_size), .data = (_type[_size]) { STAI_PACK(__VA_ARGS__) } }


#define STAI_EMPTY_ARRAY() \
  { .size = 0, .data = NULL }


#define STAI_INIT_VERSION(_major, _minor, _micro) \
  { .major = (_major), .minor = (_minor), .micro = (_micro), .reserved = 0x0 }

/*****************************************************************************/
/**  Getters and setters  **/

#define STAI_GET_ARRAY_SIZE(nd_array) \
  (nd_array.size)


#define STAI_GET_ARRAY_ELEM(nd_array, pos) \
  (nd_array.data[(pos)])

#define _STAI_SET_ERROR(net_ctx, cond, value, exit) { \
  if (!(net_ctx)) { return STAI_ERROR_NETWORK_INVALID_CONTEXT_HANDLE; } \
  if (((uintptr_t)net_ctx) & (_STAI_CONTEXT_ALIGNMENT-1)) { return STAI_ERROR_NETWORK_INVALID_CONTEXT_ALIGNMENT; } \
  if (((value) >= STAI_ERROR_GENERIC) && (cond)) { \
    if ((net_ctx)->_return_code == STAI_SUCCESS) { \
      (net_ctx)->_return_code = (value); \
    } \
    return (exit); \
  } \
}

/*****************************************************************************/
/* TODO REMOVE THESE TWO MACROS */
#define STAI_EVENT_NODE_START_CB
#define STAI_EVENT_NODE_STOP_CB

#ifdef STAI_EVENT_NODE_START_CB
#ifndef _STAI_NETWORK_EVENT_NODE_START_CB
  #define _STAI_NETWORK_EVENT_NODE_START_CB(_node_id, _buffers_size, ...) \
  if (net_ctx->_callback) { \
    const stai_event_node_start_stop _start_event = { \
      .node_id=(_node_id), \
      .buffers={ \
        .size=(_buffers_size), \
        .data=(stai_ptr const*)(const stai_ptr[_buffers_size])STAI_PACK(__VA_ARGS__) \
      } \
    }; \
    net_ctx->_callback(net_ctx->_callback_cookie, STAI_EVENT_NODE_START, (const void*)&_start_event); \
  }
#endif
#else
  #define _STAI_NETWORK_EVENT_NODE_START_CB(_node_id, _buffers_size, ...) \
    do { /* _STAI_NETWORK_EVENT_NODE_START_CB() */ } while(0);
#endif      /* STAI_EVENT_NODE_START_CB */

#ifdef STAI_EVENT_NODE_STOP_CB
#ifndef _STAI_NETWORK_EVENT_NODE_STOP_CB
  #define _STAI_NETWORK_EVENT_NODE_STOP_CB(_node_id, _buffers_size, ...) \
  if (net_ctx->_callback) { \
    const stai_event_node_start_stop _stop_event = { \
      .node_id=(_node_id), \
      .buffers={ \
        .size=(_buffers_size), \
        .data=(stai_ptr const*)(stai_ptr[_buffers_size])STAI_PACK(__VA_ARGS__) \
      } \
    }; \
    net_ctx->_callback(net_ctx->_callback_cookie, STAI_EVENT_NODE_STOP, (const void*)&_stop_event); \
  }
#endif
#else
  #define _STAI_NETWORK_EVENT_NODE_STOP_CB(_node_id, _buffers_size, ...) \
    do { /* _STAI_NETWORK_EVENT_NODE_STOP_CB() */ } while(0);
#endif      /* STAI_EVENT_NODE_STOP_CB */


/*****************************************************************************/
#define _STAI_NETWORK_MODEL_SIGNATURE     "0xbc10b9e7864dd41c465c5a344cfebcd2"
#define _STAI_NETWORK_DATETIME            "2026-07-31T02:31:33+0000"
#define _STAI_NETWORK_COMPILE_DATETIME    __DATE__ " " __TIME__

#define _STAI_CONTEXT_ALIGNMENT        STAI_NETWORK_CONTEXT_ALIGNMENT

/*****************************************************************************/
#define g_network_activations_1     (NULL)




#if defined(HAVE_NETWORK_INFO)
/*****************************************************************************/
static const stai_network_info g_network_info = {
  .model_signature = _STAI_NETWORK_MODEL_SIGNATURE,
  .c_compile_datetime = _STAI_NETWORK_COMPILE_DATETIME,
  .c_model_name = STAI_NETWORK_MODEL_NAME,
  .c_model_datetime = _STAI_NETWORK_DATETIME,
  .c_model_signature = 0x0,
  .runtime_version = STAI_INIT_VERSION(12, 0, 1),
  .tool_version = STAI_INIT_VERSION(4, 0, 1),
  .api_version = STAI_INIT_VERSION(1, 0, 0),
  .n_macc = STAI_NETWORK_MACC_NUM,
  .n_nodes = STAI_NETWORK_NODES_NUM,
  .flags = STAI_NETWORK_FLAGS,
  .n_inputs = STAI_NETWORK_IN_NUM,
  .n_outputs = STAI_NETWORK_OUT_NUM,
  .n_activations = STAI_NETWORK_ACTIVATIONS_NUM,
  .n_weights = STAI_NETWORK_WEIGHTS_NUM,
  .n_states = STAI_NETWORK_STATES_NUM,
  .inputs = (stai_tensor[STAI_NETWORK_IN_NUM]) {
    STAI_INIT_TENSOR(
      STAI_NETWORK_IN_1_NAME,
      STAI_NETWORK_IN_1_FLAGS,
      STAI_NETWORK_IN_1_FORMAT,
      STAI_NETWORK_IN_1_SIZE_BYTES,
      STAI_DECLARE_ARRAY(int32_t, 4, 1, 49, 10, 1),
      STAI_DECLARE_ARRAY(float, 1, 0.04888138920068741f),
      STAI_DECLARE_ARRAY(int16_t, 1, 11)),
    },
    .outputs = (stai_tensor[STAI_NETWORK_OUT_NUM]) {
    STAI_INIT_TENSOR(
      STAI_NETWORK_OUT_1_NAME,
      STAI_NETWORK_OUT_1_FLAGS,
      STAI_NETWORK_OUT_1_FORMAT,
      STAI_NETWORK_OUT_1_SIZE_BYTES,
      STAI_DECLARE_ARRAY(int32_t, 2, 1, 4),
      STAI_DECLARE_ARRAY(float, 1, 0.00390625f),
      STAI_DECLARE_ARRAY(int16_t, 1, -128)),
    },
  .activations = (stai_tensor[STAI_NETWORK_ACTIVATIONS_NUM]) {
    STAI_INIT_TENSOR(
      (NULL),
      STAI_NETWORK_ACTIVATION_1_FLAGS,
      STAI_FORMAT_U8,
      STAI_NETWORK_ACTIVATION_1_SIZE_BYTES,
      STAI_DECLARE_ARRAY(int32_t, 1, 17232),
      STAI_EMPTY_ARRAY(),
      STAI_EMPTY_ARRAY()),
    },
  .weights = (stai_tensor[STAI_NETWORK_WEIGHTS_NUM]) {
    STAI_INIT_TENSOR(
      (NULL),
      STAI_NETWORK_WEIGHT_1_FLAGS,
      STAI_FORMAT_U8,
      STAI_NETWORK_WEIGHT_1_SIZE_BYTES,
      STAI_DECLARE_ARRAY(int32_t, 1, 102512),
      STAI_EMPTY_ARRAY(),
      STAI_EMPTY_ARRAY()),
    },

  .states = NULL
};
#endif

#define _STAI_CONTEXT_ACQUIRE(_net_ctx, _net_handle) \
  _stai_network_context* _net_ctx = (_stai_network_context*)(_net_handle); \
  STAI_ASSERT(_net_ctx != NULL) \
  _STAI_SET_ERROR(_net_ctx, _net_ctx->_magic != STAI_MAGIC, \
                  STAI_ERROR_NETWORK_INVALID_CONTEXT_HANDLE, _net_ctx->_return_code)


/*****************************************************************************/
static
void _stai_network_check(_stai_network_context* net_ctx)
{
  stai_size idx;

// Check activations status
  for (idx=0; idx<STAI_NETWORK_ACTIVATIONS_NUM; idx++) {
    if (net_ctx->_activations[idx] == NULL) break;
  }
  net_ctx->_flags |= (idx == STAI_NETWORK_ACTIVATIONS_NUM) ? STAI_FLAG_ACTIVATIONS : STAI_FLAG_NONE;
// Check inputs status
  for (idx=0; idx<STAI_NETWORK_IN_NUM; idx++) {
    if (net_ctx->_inputs[idx] == NULL) break;
  }
  net_ctx->_flags |= (idx == STAI_NETWORK_IN_NUM) ? STAI_FLAG_INPUTS : STAI_FLAG_NONE;

  // Check outputs status
  for (idx=0; idx<STAI_NETWORK_OUT_NUM; idx++) {
    if (net_ctx->_outputs[idx] == NULL) break;
  }
  net_ctx->_flags |= (idx == STAI_NETWORK_OUT_NUM) ? STAI_FLAG_OUTPUTS : STAI_FLAG_NONE;

// Check weights status
  for (idx=0; idx<STAI_NETWORK_WEIGHTS_NUM; idx++) {
    if (net_ctx->_weights[idx] == NULL) break;
  }
  net_ctx->_flags |= (idx == STAI_NETWORK_WEIGHTS_NUM) ? STAI_FLAG_WEIGHTS : STAI_FLAG_NONE;
STAI_PRINT("  [_stai_network_check] flags: 0x%08x\n", net_ctx->_flags)
}


/*****************************************************************************/
STAI_API_ENTRY
stai_return_code stai_network_init(
  stai_network* network)
{
  /* Memory where to store internal context is provided by applications as a raw byte buffer */
  _stai_network_context* net_ctx = (_stai_network_context*)(network);
  net_ctx->_return_code = STAI_SUCCESS;
  STAI_PRINT("[Entering Network Init] network(%p) context_size(%d)\n", net_ctx, (int32_t)sizeof(_stai_network_context))

  _STAI_SET_ERROR(net_ctx, STAI_NETWORK_CONTEXT_SIZE != sizeof(_stai_network_context),
                 STAI_ERROR_NETWORK_INVALID_CONTEXT_SIZE, net_ctx->_return_code)

  {
    const _stai_network_context _network_context = {
      ._magic = STAI_MAGIC,
      ._signature = STAI_NETWORK_MODEL_SIGNATURE,
      ._flags = STAI_NETWORK_FLAGS,
      ._return_code = STAI_SUCCESS,
      ._callback = NULL,
      ._callback_cookie = NULL,
      ._activations = {
      (stai_ptr)g_network_activations_1
      },
      ._weights = {
      (stai_ptr)g_network_weights_array
      },
      ._inputs = {
    NULL},
      ._outputs = {
    NULL},
    };

    // Deep copy of internal context to opaque buffer provided by app
    *net_ctx = _network_context;

    _stai_network_check(net_ctx);
  }

  return net_ctx->_return_code;
}


STAI_API_ENTRY
stai_return_code stai_network_deinit(
  stai_network* network)
{
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)

  /*  Reset flags to initial state  */
  net_ctx->_flags = STAI_NETWORK_FLAGS;
  return net_ctx->_return_code;
}

/*****************************************************************************/



/* Int quant #0 */
AI_INTQ_INFO_LIST_OBJ_DECLARE(conv2d_0_output_array_intq, AI_STATIC,
  AI_BUFFER_META_FLAG_SCALE_FLOAT|AI_BUFFER_META_FLAG_ZEROPOINT_S8, 1,
  AI_PACK_INTQ_INFO(
    AI_PACK_INTQ_SCALE(0.06193135678768158f),
    AI_PACK_INTQ_ZP(-128)))

/* Int quant #1 */
AI_INTQ_INFO_LIST_OBJ_DECLARE(eltwise_1_output_array_intq, AI_STATIC,
  AI_BUFFER_META_FLAG_SCALE_FLOAT|AI_BUFFER_META_FLAG_ZEROPOINT_S8, 1,
  AI_PACK_INTQ_INFO(
    AI_PACK_INTQ_SCALE(0.0648680031299591f),
    AI_PACK_INTQ_ZP(-116)))

/* Int quant #2 */
AI_INTQ_INFO_LIST_OBJ_DECLARE(sequential_1_batch_normalization_1_batchnorm_mul_4D_array_intq, AI_STATIC,
  AI_BUFFER_META_FLAG_SCALE_FLOAT|AI_BUFFER_META_FLAG_ZEROPOINT_S8, 1,
  AI_PACK_INTQ_INFO(
    AI_PACK_INTQ_SCALE(0.04053095355629921f),
    AI_PACK_INTQ_ZP(-128)))

/* Int quant #3 */
AI_INTQ_INFO_LIST_OBJ_DECLARE(eltwise_2_output_array_intq, AI_STATIC,
  AI_BUFFER_META_FLAG_SCALE_FLOAT|AI_BUFFER_META_FLAG_ZEROPOINT_S8, 1,
  AI_PACK_INTQ_INFO(
    AI_PACK_INTQ_SCALE(0.06316056102514267f),
    AI_PACK_INTQ_ZP(-116)))

/* Int quant #4 */
AI_INTQ_INFO_LIST_OBJ_DECLARE(sequential_1_batch_normalization_1_batchnorm_sub_4D_array_intq, AI_STATIC,
  AI_BUFFER_META_FLAG_SCALE_FLOAT|AI_BUFFER_META_FLAG_ZEROPOINT_S8, 1,
  AI_PACK_INTQ_INFO(
    AI_PACK_INTQ_SCALE(0.002936646109446883f),
    AI_PACK_INTQ_ZP(127)))

/* Int quant #5 */
AI_INTQ_INFO_LIST_OBJ_DECLARE(conv2d_4_output_array_intq, AI_STATIC,
  AI_BUFFER_META_FLAG_SCALE_FLOAT|AI_BUFFER_META_FLAG_ZEROPOINT_S8, 1,
  AI_PACK_INTQ_INFO(
    AI_PACK_INTQ_SCALE(0.07023479044437408f),
    AI_PACK_INTQ_ZP(-128)))

/* Int quant #6 */
AI_INTQ_INFO_LIST_OBJ_DECLARE(eltwise_5_output_array_intq, AI_STATIC,
  AI_BUFFER_META_FLAG_SCALE_FLOAT|AI_BUFFER_META_FLAG_ZEROPOINT_S8, 1,
  AI_PACK_INTQ_INFO(
    AI_PACK_INTQ_SCALE(0.07314389944076538f),
    AI_PACK_INTQ_ZP(-118)))

/* Int quant #7 */
AI_INTQ_INFO_LIST_OBJ_DECLARE(sequential_1_batch_normalization_1_2_batchnorm_mul_4D_array_intq, AI_STATIC,
  AI_BUFFER_META_FLAG_SCALE_FLOAT|AI_BUFFER_META_FLAG_ZEROPOINT_S8, 1,
  AI_PACK_INTQ_INFO(
    AI_PACK_INTQ_SCALE(0.011335570365190506f),
    AI_PACK_INTQ_ZP(-128)))

/* Int quant #8 */
AI_INTQ_INFO_LIST_OBJ_DECLARE(eltwise_6_output_array_intq, AI_STATIC,
  AI_BUFFER_META_FLAG_SCALE_FLOAT|AI_BUFFER_META_FLAG_ZEROPOINT_S8, 1,
  AI_PACK_INTQ_INFO(
    AI_PACK_INTQ_SCALE(0.07080087065696716f),
    AI_PACK_INTQ_ZP(-118)))

/* Int quant #9 */
AI_INTQ_INFO_LIST_OBJ_DECLARE(sequential_1_batch_normalization_1_2_batchnorm_sub_4D_array_intq, AI_STATIC,
  AI_BUFFER_META_FLAG_SCALE_FLOAT|AI_BUFFER_META_FLAG_ZEROPOINT_S8, 1,
  AI_PACK_INTQ_INFO(
    AI_PACK_INTQ_SCALE(0.0029091069009155035f),
    AI_PACK_INTQ_ZP(127)))

/* Int quant #10 */
AI_INTQ_INFO_LIST_OBJ_DECLARE(conv2d_8_output_array_intq, AI_STATIC,
  AI_BUFFER_META_FLAG_SCALE_FLOAT|AI_BUFFER_META_FLAG_ZEROPOINT_S8, 1,
  AI_PACK_INTQ_INFO(
    AI_PACK_INTQ_SCALE(0.08150845766067505f),
    AI_PACK_INTQ_ZP(-128)))

/* Int quant #11 */
AI_INTQ_INFO_LIST_OBJ_DECLARE(eltwise_9_output_array_intq, AI_STATIC,
  AI_BUFFER_META_FLAG_SCALE_FLOAT|AI_BUFFER_META_FLAG_ZEROPOINT_S8, 1,
  AI_PACK_INTQ_INFO(
    AI_PACK_INTQ_SCALE(0.08737324178218842f),
    AI_PACK_INTQ_ZP(-111)))

/* Int quant #12 */
AI_INTQ_INFO_LIST_OBJ_DECLARE(sequential_1_batch_normalization_2_1_batchnorm_mul_4D_array_intq, AI_STATIC,
  AI_BUFFER_META_FLAG_SCALE_FLOAT|AI_BUFFER_META_FLAG_ZEROPOINT_S8, 1,
  AI_PACK_INTQ_INFO(
    AI_PACK_INTQ_SCALE(0.017084894701838493f),
    AI_PACK_INTQ_ZP(-128)))

/* Int quant #13 */
AI_INTQ_INFO_LIST_OBJ_DECLARE(eltwise_10_output_array_intq, AI_STATIC,
  AI_BUFFER_META_FLAG_SCALE_FLOAT|AI_BUFFER_META_FLAG_ZEROPOINT_S8, 1,
  AI_PACK_INTQ_INFO(
    AI_PACK_INTQ_SCALE(0.08602751791477203f),
    AI_PACK_INTQ_ZP(-111)))

/* Int quant #14 */
AI_INTQ_INFO_LIST_OBJ_DECLARE(sequential_1_batch_normalization_2_1_batchnorm_sub_4D_array_intq, AI_STATIC,
  AI_BUFFER_META_FLAG_SCALE_FLOAT|AI_BUFFER_META_FLAG_ZEROPOINT_S8, 1,
  AI_PACK_INTQ_INFO(
    AI_PACK_INTQ_SCALE(0.00586478179320693f),
    AI_PACK_INTQ_ZP(127)))

/* Int quant #15 */
AI_INTQ_INFO_LIST_OBJ_DECLARE(pool_11_output_array_intq, AI_STATIC,
  AI_BUFFER_META_FLAG_SCALE_FLOAT|AI_BUFFER_META_FLAG_ZEROPOINT_S8, 1,
  AI_PACK_INTQ_INFO(
    AI_PACK_INTQ_SCALE(0.01911201700568199f),
    AI_PACK_INTQ_ZP(-69)))

/* Int quant #16 */
AI_INTQ_INFO_LIST_OBJ_DECLARE(gemm_12_output_array_intq, AI_STATIC,
  AI_BUFFER_META_FLAG_SCALE_FLOAT|AI_BUFFER_META_FLAG_ZEROPOINT_S8, 1,
  AI_PACK_INTQ_INFO(
    AI_PACK_INTQ_SCALE(0.021514512598514557f),
    AI_PACK_INTQ_ZP(-128)))

/* Int quant #17 */
AI_INTQ_INFO_LIST_OBJ_DECLARE(gemm_12_weights_array_intq, AI_STATIC,
  AI_BUFFER_META_FLAG_SCALE_FLOAT|AI_BUFFER_META_FLAG_ZEROPOINT_S8, 64,
  AI_PACK_INTQ_INFO(
    AI_PACK_INTQ_SCALE(0.0019539962522685528f, 0.001789175788871944f, 0.0020945097785443068f, 0.0023194490931928158f, 0.0018464408349245787f, 0.0018198314355686307f, 0.001778792473487556f, 0.002046172972768545f, 0.0020372169092297554f, 0.001795112737454474f, 0.0017221878515556455f, 0.0019162432290613651f, 0.0019586344715207815f, 0.0018265583785250783f, 0.0018650536658242345f, 0.001733817858621478f, 0.0017967487219721079f, 0.0019496328895911574f, 0.0018846536986529827f, 0.001901049050502479f, 0.00180251884739846f, 0.001741635031066835f, 0.001949277357198298f, 0.00198318250477314f, 0.0018879570998251438f, 0.001676167594268918f, 0.001722755841910839f, 0.0018711216980591416f, 0.0019130941946059465f, 0.0019654284697026014f, 0.0019117516931146383f, 0.0018361406400799751f, 0.002068737056106329f, 0.0019263430731371045f, 0.002028056653216481f, 0.0018004165031015873f, 0.0019187112338840961f, 0.00168738909997046f, 0.0018811655463650823f, 0.0018891511717811227f, 0.0018677698681131005f, 0.0016497180331498384f, 0.002006437396630645f, 0.0017757273744791746f, 0.002068514935672283f, 0.0018186387605965137f, 0.001656451029703021f, 0.002104568760842085f, 0.0017535971710458398f, 0.0018267033156007528f, 0.002298654755577445f, 0.0017992764478549361f, 0.001719313208013773f, 0.0017893909243866801f, 0.0017542272107675672f, 0.001895608496852219f, 0.0019447440281510353f, 0.0023086308501660824f, 0.0016302511794492602f, 0.0014442183310166001f, 0.0019763282034546137f, 0.0017579608829692006f, 0.001704787602648139f, 0.0018411526689305902f),
    AI_PACK_INTQ_ZP(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)))

/* Int quant #18 */
AI_INTQ_INFO_LIST_OBJ_DECLARE(gemm_13_output_array_intq, AI_STATIC,
  AI_BUFFER_META_FLAG_SCALE_FLOAT|AI_BUFFER_META_FLAG_ZEROPOINT_S8, 1,
  AI_PACK_INTQ_INFO(
    AI_PACK_INTQ_SCALE(0.059266526252031326f),
    AI_PACK_INTQ_ZP(-3)))

/* Int quant #19 */
AI_INTQ_INFO_LIST_OBJ_DECLARE(gemm_13_weights_array_intq, AI_STATIC,
  AI_BUFFER_META_FLAG_SCALE_FLOAT|AI_BUFFER_META_FLAG_ZEROPOINT_S8, 4,
  AI_PACK_INTQ_INFO(
    AI_PACK_INTQ_SCALE(0.0027994606643915176f, 0.002751985564827919f, 0.0026335229631513357f, 0.003037418005988002f),
    AI_PACK_INTQ_ZP(0, 0, 0, 0)))



/* Array#0 */
AI_ARRAY_OBJ_DECLARE(
  conv2d_0_output_array, AI_ARRAY_FORMAT_S8,
  NULL, NULL, 15680, AI_STATIC)

/* Array#1 */
AI_ARRAY_OBJ_DECLARE(
  eltwise_1_output_array, AI_ARRAY_FORMAT_S8,
  NULL, NULL, 15680, AI_STATIC)

/* Array#2 */
AI_ARRAY_OBJ_DECLARE(
  sequential_1_batch_normalization_1_batchnorm_mul_4D_array, AI_ARRAY_FORMAT_S8,
  NULL, NULL, 32, AI_STATIC)

/* Array#3 */
AI_ARRAY_OBJ_DECLARE(
  eltwise_2_output_array, AI_ARRAY_FORMAT_S8,
  NULL, NULL, 15680, AI_STATIC)

/* Array#4 */
AI_ARRAY_OBJ_DECLARE(
  sequential_1_batch_normalization_1_batchnorm_sub_4D_array, AI_ARRAY_FORMAT_S8,
  NULL, NULL, 32, AI_STATIC)

/* Array#5 */
AI_ARRAY_OBJ_DECLARE(
  conv2d_4_output_array, AI_ARRAY_FORMAT_S8,
  NULL, NULL, 7680, AI_STATIC)

/* Array#6 */
AI_ARRAY_OBJ_DECLARE(
  eltwise_5_output_array, AI_ARRAY_FORMAT_S8,
  NULL, NULL, 7680, AI_STATIC)

/* Array#7 */
AI_ARRAY_OBJ_DECLARE(
  sequential_1_batch_normalization_1_2_batchnorm_mul_4D_array, AI_ARRAY_FORMAT_S8,
  NULL, NULL, 64, AI_STATIC)

/* Array#8 */
AI_ARRAY_OBJ_DECLARE(
  eltwise_6_output_array, AI_ARRAY_FORMAT_S8,
  NULL, NULL, 7680, AI_STATIC)

/* Array#9 */
AI_ARRAY_OBJ_DECLARE(
  sequential_1_batch_normalization_1_2_batchnorm_sub_4D_array, AI_ARRAY_FORMAT_S8,
  NULL, NULL, 64, AI_STATIC)

/* Array#10 */
AI_ARRAY_OBJ_DECLARE(
  conv2d_8_output_array, AI_ARRAY_FORMAT_S8,
  NULL, NULL, 3072, AI_STATIC)

/* Array#11 */
AI_ARRAY_OBJ_DECLARE(
  eltwise_9_output_array, AI_ARRAY_FORMAT_S8,
  NULL, NULL, 3072, AI_STATIC)

/* Array#12 */
AI_ARRAY_OBJ_DECLARE(
  sequential_1_batch_normalization_2_1_batchnorm_mul_4D_array, AI_ARRAY_FORMAT_S8,
  NULL, NULL, 128, AI_STATIC)

/* Array#13 */
AI_ARRAY_OBJ_DECLARE(
  eltwise_10_output_array, AI_ARRAY_FORMAT_S8,
  NULL, NULL, 3072, AI_STATIC)

/* Array#14 */
AI_ARRAY_OBJ_DECLARE(
  sequential_1_batch_normalization_2_1_batchnorm_sub_4D_array, AI_ARRAY_FORMAT_S8,
  NULL, NULL, 128, AI_STATIC)

/* Array#15 */
AI_ARRAY_OBJ_DECLARE(
  pool_11_output_array, AI_ARRAY_FORMAT_S8,
  NULL, NULL, 128, AI_STATIC)

/* Array#16 */
AI_ARRAY_OBJ_DECLARE(
  gemm_12_output_array, AI_ARRAY_FORMAT_S8,
  NULL, NULL, 64, AI_STATIC)

/* Array#17 */
AI_ARRAY_OBJ_DECLARE(
  gemm_12_weights_array, AI_ARRAY_FORMAT_S8,
  NULL, NULL, 8192, AI_STATIC)

/* Array#18 */
AI_ARRAY_OBJ_DECLARE(
  gemm_12_bias_array, AI_ARRAY_FORMAT_S32,
  NULL, NULL, 64, AI_STATIC)

/* Array#19 */
AI_ARRAY_OBJ_DECLARE(
  gemm_12_scratch0_array, AI_ARRAY_FORMAT_S16,
  NULL, NULL, 448, AI_STATIC)

/* Array#20 */
AI_ARRAY_OBJ_DECLARE(
  gemm_13_output_array, AI_ARRAY_FORMAT_S8,
  NULL, NULL, 4, AI_STATIC)

/* Array#21 */
AI_ARRAY_OBJ_DECLARE(
  gemm_13_weights_array, AI_ARRAY_FORMAT_S8,
  NULL, NULL, 256, AI_STATIC)

/* Array#22 */
AI_ARRAY_OBJ_DECLARE(
  gemm_13_bias_array, AI_ARRAY_FORMAT_S32,
  NULL, NULL, 4, AI_STATIC)

/* Array#23 */
AI_ARRAY_OBJ_DECLARE(
  gemm_13_scratch0_array, AI_ARRAY_FORMAT_S16,
  NULL, NULL, 84, AI_STATIC)



/* Tensor #0 */
AI_TENSOR_OBJ_DECLARE(
  conv2d_0_output, AI_STATIC,
  1, 0x1,
  AI_SHAPE_INIT(4, 1, 32, 10, 49), AI_STRIDE_INIT(4, 1, 1, 32, 320),
  1, &conv2d_0_output_array, &conv2d_0_output_array_intq)

/* Tensor #1 */
AI_TENSOR_OBJ_DECLARE(
  eltwise_1_output, AI_STATIC,
  15, 0x1,
  AI_SHAPE_INIT(4, 1, 32, 10, 49), AI_STRIDE_INIT(4, 1, 1, 32, 320),
  1, &eltwise_1_output_array, &eltwise_1_output_array_intq)

/* Tensor #2 */
AI_TENSOR_OBJ_DECLARE(
  sequential_1_batch_normalization_1_batchnorm_mul_4D, AI_STATIC,
  35, 0x1,
  AI_SHAPE_INIT(4, 1, 32, 1, 1), AI_STRIDE_INIT(4, 1, 1, 32, 32),
  1, &sequential_1_batch_normalization_1_batchnorm_mul_4D_array, &sequential_1_batch_normalization_1_batchnorm_mul_4D_array_intq)

/* Tensor #3 */
AI_TENSOR_OBJ_DECLARE(
  eltwise_2_output, AI_STATIC,
  16, 0x1,
  AI_SHAPE_INIT(4, 1, 32, 10, 49), AI_STRIDE_INIT(4, 1, 1, 32, 320),
  1, &eltwise_2_output_array, &eltwise_2_output_array_intq)

/* Tensor #4 */
AI_TENSOR_OBJ_DECLARE(
  sequential_1_batch_normalization_1_batchnorm_sub_4D, AI_STATIC,
  36, 0x1,
  AI_SHAPE_INIT(4, 1, 32, 1, 1), AI_STRIDE_INIT(4, 1, 1, 32, 32),
  1, &sequential_1_batch_normalization_1_batchnorm_sub_4D_array, &sequential_1_batch_normalization_1_batchnorm_sub_4D_array_intq)

/* Tensor #5 */
AI_TENSOR_OBJ_DECLARE(
  conv2d_4_output, AI_STATIC,
  5, 0x1,
  AI_SHAPE_INIT(4, 1, 64, 5, 24), AI_STRIDE_INIT(4, 1, 1, 64, 320),
  1, &conv2d_4_output_array, &conv2d_4_output_array_intq)

/* Tensor #6 */
AI_TENSOR_OBJ_DECLARE(
  eltwise_5_output, AI_STATIC,
  17, 0x1,
  AI_SHAPE_INIT(4, 1, 64, 5, 24), AI_STRIDE_INIT(4, 1, 1, 64, 320),
  1, &eltwise_5_output_array, &eltwise_5_output_array_intq)

/* Tensor #7 */
AI_TENSOR_OBJ_DECLARE(
  sequential_1_batch_normalization_1_2_batchnorm_mul_4D, AI_STATIC,
  33, 0x1,
  AI_SHAPE_INIT(4, 1, 64, 1, 1), AI_STRIDE_INIT(4, 1, 1, 64, 64),
  1, &sequential_1_batch_normalization_1_2_batchnorm_mul_4D_array, &sequential_1_batch_normalization_1_2_batchnorm_mul_4D_array_intq)

/* Tensor #8 */
AI_TENSOR_OBJ_DECLARE(
  eltwise_6_output, AI_STATIC,
  18, 0x1,
  AI_SHAPE_INIT(4, 1, 64, 5, 24), AI_STRIDE_INIT(4, 1, 1, 64, 320),
  1, &eltwise_6_output_array, &eltwise_6_output_array_intq)

/* Tensor #9 */
AI_TENSOR_OBJ_DECLARE(
  sequential_1_batch_normalization_1_2_batchnorm_sub_4D, AI_STATIC,
  34, 0x1,
  AI_SHAPE_INIT(4, 1, 64, 1, 1), AI_STRIDE_INIT(4, 1, 1, 64, 64),
  1, &sequential_1_batch_normalization_1_2_batchnorm_sub_4D_array, &sequential_1_batch_normalization_1_2_batchnorm_sub_4D_array_intq)

/* Tensor #10 */
AI_TENSOR_OBJ_DECLARE(
  conv2d_8_output, AI_STATIC,
  10, 0x1,
  AI_SHAPE_INIT(4, 1, 128, 2, 12), AI_STRIDE_INIT(4, 1, 1, 128, 256),
  1, &conv2d_8_output_array, &conv2d_8_output_array_intq)

/* Tensor #11 */
AI_TENSOR_OBJ_DECLARE(
  eltwise_9_output, AI_STATIC,
  19, 0x1,
  AI_SHAPE_INIT(4, 1, 128, 2, 12), AI_STRIDE_INIT(4, 1, 1, 128, 256),
  1, &eltwise_9_output_array, &eltwise_9_output_array_intq)

/* Tensor #12 */
AI_TENSOR_OBJ_DECLARE(
  sequential_1_batch_normalization_2_1_batchnorm_mul_4D, AI_STATIC,
  37, 0x1,
  AI_SHAPE_INIT(4, 1, 128, 1, 1), AI_STRIDE_INIT(4, 1, 1, 128, 128),
  1, &sequential_1_batch_normalization_2_1_batchnorm_mul_4D_array, &sequential_1_batch_normalization_2_1_batchnorm_mul_4D_array_intq)

/* Tensor #13 */
AI_TENSOR_OBJ_DECLARE(
  eltwise_10_output, AI_STATIC,
  14, 0x1,
  AI_SHAPE_INIT(4, 1, 128, 2, 12), AI_STRIDE_INIT(4, 1, 1, 128, 256),
  1, &eltwise_10_output_array, &eltwise_10_output_array_intq)

/* Tensor #14 */
AI_TENSOR_OBJ_DECLARE(
  sequential_1_batch_normalization_2_1_batchnorm_sub_4D, AI_STATIC,
  38, 0x1,
  AI_SHAPE_INIT(4, 1, 128, 1, 1), AI_STRIDE_INIT(4, 1, 1, 128, 128),
  1, &sequential_1_batch_normalization_2_1_batchnorm_sub_4D_array, &sequential_1_batch_normalization_2_1_batchnorm_sub_4D_array_intq)

/* Tensor #15 */
AI_TENSOR_OBJ_DECLARE(
  pool_11_output, AI_STATIC,
  30, 0x1,
  AI_SHAPE_INIT(4, 1, 128, 1, 1), AI_STRIDE_INIT(4, 1, 1, 128, 128),
  1, &pool_11_output_array, &pool_11_output_array_intq)

/* Tensor #16 */
AI_TENSOR_OBJ_DECLARE(
  gemm_12_bias, AI_STATIC,
  20, 0x0,
  AI_SHAPE_INIT(4, 1, 64, 1, 1), AI_STRIDE_INIT(4, 4, 4, 256, 256),
  1, &gemm_12_bias_array, NULL)

/* Tensor #17 */
AI_TENSOR_OBJ_DECLARE(
  gemm_12_output, AI_STATIC,
  21, 0x1,
  AI_SHAPE_INIT(4, 1, 64, 1, 1), AI_STRIDE_INIT(4, 1, 1, 64, 64),
  1, &gemm_12_output_array, &gemm_12_output_array_intq)

/* Tensor #18 */
AI_TENSOR_OBJ_DECLARE(
  gemm_12_scratch0, AI_STATIC,
  22, 0x0,
  AI_SHAPE_INIT(4, 1, 448, 1, 1), AI_STRIDE_INIT(4, 2, 2, 896, 896),
  1, &gemm_12_scratch0_array, NULL)

/* Tensor #19 */
AI_TENSOR_OBJ_DECLARE(
  gemm_12_weights, AI_STATIC,
  23, 0x1,
  AI_SHAPE_INIT(4, 128, 64, 1, 1), AI_STRIDE_INIT(4, 1, 128, 8192, 8192),
  1, &gemm_12_weights_array, &gemm_12_weights_array_intq)

/* Tensor #20 */
AI_TENSOR_OBJ_DECLARE(
  gemm_13_bias, AI_STATIC,
  24, 0x0,
  AI_SHAPE_INIT(4, 1, 4, 1, 1), AI_STRIDE_INIT(4, 4, 4, 16, 16),
  1, &gemm_13_bias_array, NULL)

/* Tensor #21 */
AI_TENSOR_OBJ_DECLARE(
  gemm_13_output, AI_STATIC,
  25, 0x1,
  AI_SHAPE_INIT(4, 1, 4, 1, 1), AI_STRIDE_INIT(4, 1, 1, 4, 4),
  1, &gemm_13_output_array, &gemm_13_output_array_intq)

/* Tensor #22 */
AI_TENSOR_OBJ_DECLARE(
  gemm_13_scratch0, AI_STATIC,
  26, 0x0,
  AI_SHAPE_INIT(4, 1, 84, 1, 1), AI_STRIDE_INIT(4, 2, 2, 168, 168),
  1, &gemm_13_scratch0_array, NULL)

/* Tensor #23 */
AI_TENSOR_OBJ_DECLARE(
  gemm_13_weights, AI_STATIC,
  27, 0x1,
  AI_SHAPE_INIT(4, 64, 4, 1, 1), AI_STRIDE_INIT(4, 1, 64, 256, 256),
  1, &gemm_13_weights_array, &gemm_13_weights_array_intq)


AI_TENSOR_CHAIN_OBJ_DECLARE(
  eltwise_1_chain, AI_STATIC_CONST, 4,
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 2, &conv2d_0_output, &sequential_1_batch_normalization_1_batchnorm_mul_4D),
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 1, &eltwise_1_output),
  AI_TENSOR_LIST_OBJ_EMPTY,
  AI_TENSOR_LIST_OBJ_EMPTY
)

AI_LAYER_OBJ_DECLARE(
  eltwise_1_layer, 1,
  ELTWISE_INTEGER_TYPE, 0x0, NULL,
  eltwise_integer, forward_eltwise_integer_INT8,
  &eltwise_1_chain,
  NULL, &eltwise_1_layer, AI_STATIC, 
  .operation = ai_mul_f32, 
  .buffer_operation = ai_mul_buffer_INT8, 
)

AI_TENSOR_CHAIN_OBJ_DECLARE(
  eltwise_2_chain, AI_STATIC_CONST, 4,
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 2, &eltwise_1_output, &sequential_1_batch_normalization_1_batchnorm_sub_4D),
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 1, &eltwise_2_output),
  AI_TENSOR_LIST_OBJ_EMPTY,
  AI_TENSOR_LIST_OBJ_EMPTY
)

AI_LAYER_OBJ_DECLARE(
  eltwise_2_layer, 2,
  ELTWISE_INTEGER_TYPE, 0x0, NULL,
  eltwise_integer, forward_eltwise_integer_INT8,
  &eltwise_2_chain,
  NULL, &eltwise_2_layer, AI_STATIC, 
  .operation = ai_sum_f32, 
  .buffer_operation = ai_sum_buffer_INT8, 
)

AI_TENSOR_CHAIN_OBJ_DECLARE(
  eltwise_5_chain, AI_STATIC_CONST, 4,
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 2, &conv2d_4_output, &sequential_1_batch_normalization_1_2_batchnorm_mul_4D),
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 1, &eltwise_5_output),
  AI_TENSOR_LIST_OBJ_EMPTY,
  AI_TENSOR_LIST_OBJ_EMPTY
)

AI_LAYER_OBJ_DECLARE(
  eltwise_5_layer, 5,
  ELTWISE_INTEGER_TYPE, 0x0, NULL,
  eltwise_integer, forward_eltwise_integer_INT8,
  &eltwise_5_chain,
  NULL, &eltwise_5_layer, AI_STATIC, 
  .operation = ai_mul_f32, 
  .buffer_operation = ai_mul_buffer_INT8, 
)

AI_TENSOR_CHAIN_OBJ_DECLARE(
  eltwise_6_chain, AI_STATIC_CONST, 4,
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 2, &eltwise_5_output, &sequential_1_batch_normalization_1_2_batchnorm_sub_4D),
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 1, &eltwise_6_output),
  AI_TENSOR_LIST_OBJ_EMPTY,
  AI_TENSOR_LIST_OBJ_EMPTY
)

AI_LAYER_OBJ_DECLARE(
  eltwise_6_layer, 6,
  ELTWISE_INTEGER_TYPE, 0x0, NULL,
  eltwise_integer, forward_eltwise_integer_INT8,
  &eltwise_6_chain,
  NULL, &eltwise_6_layer, AI_STATIC, 
  .operation = ai_sum_f32, 
  .buffer_operation = ai_sum_buffer_INT8, 
)

AI_TENSOR_CHAIN_OBJ_DECLARE(
  eltwise_9_chain, AI_STATIC_CONST, 4,
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 2, &conv2d_8_output, &sequential_1_batch_normalization_2_1_batchnorm_mul_4D),
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 1, &eltwise_9_output),
  AI_TENSOR_LIST_OBJ_EMPTY,
  AI_TENSOR_LIST_OBJ_EMPTY
)

AI_LAYER_OBJ_DECLARE(
  eltwise_9_layer, 9,
  ELTWISE_INTEGER_TYPE, 0x0, NULL,
  eltwise_integer, forward_eltwise_integer_INT8,
  &eltwise_9_chain,
  NULL, &eltwise_9_layer, AI_STATIC, 
  .operation = ai_mul_f32, 
  .buffer_operation = ai_mul_buffer_INT8, 
)

AI_TENSOR_CHAIN_OBJ_DECLARE(
  eltwise_10_chain, AI_STATIC_CONST, 4,
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 2, &eltwise_9_output, &sequential_1_batch_normalization_2_1_batchnorm_sub_4D),
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 1, &eltwise_10_output),
  AI_TENSOR_LIST_OBJ_EMPTY,
  AI_TENSOR_LIST_OBJ_EMPTY
)

AI_LAYER_OBJ_DECLARE(
  eltwise_10_layer, 10,
  ELTWISE_INTEGER_TYPE, 0x0, NULL,
  eltwise_integer, forward_eltwise_integer_INT8,
  &eltwise_10_chain,
  NULL, &eltwise_10_layer, AI_STATIC, 
  .operation = ai_sum_f32, 
  .buffer_operation = ai_sum_buffer_INT8, 
)

AI_TENSOR_CHAIN_OBJ_DECLARE(
  pool_11_chain, AI_STATIC_CONST, 4,
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 1, &eltwise_10_output),
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 1, &pool_11_output),
  AI_TENSOR_LIST_OBJ_EMPTY,
  AI_TENSOR_LIST_OBJ_EMPTY
)

AI_LAYER_OBJ_DECLARE(
  pool_11_layer, 11,
  POOL_TYPE, 0x0, NULL,
  pool, forward_ap_integer_INT8,
  &pool_11_chain,
  NULL, &pool_11_layer, AI_STATIC, 
  .pool_size = AI_SHAPE_2D_INIT(2, 12), 
  .pool_stride = AI_SHAPE_2D_INIT(2, 12), 
  .pool_pad = AI_SHAPE_INIT(4, 0, 0, 0, 0), 
)

AI_TENSOR_CHAIN_OBJ_DECLARE(
  gemm_12_chain, AI_STATIC_CONST, 4,
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 1, &pool_11_output),
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 1, &gemm_12_output),
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 2, &gemm_12_weights, &gemm_12_bias),
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 1, &gemm_12_scratch0)
)

AI_LAYER_OBJ_DECLARE(
  gemm_12_layer, 12,
  DENSE_TYPE, 0x0, NULL,
  dense, forward_dense_integer_SSSA_ch,
  &gemm_12_chain,
  NULL, &gemm_12_layer, AI_STATIC, 
)

AI_TENSOR_CHAIN_OBJ_DECLARE(
  gemm_13_chain, AI_STATIC_CONST, 4,
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 1, &gemm_12_output),
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 1, &gemm_13_output),
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 2, &gemm_13_weights, &gemm_13_bias),
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 1, &gemm_13_scratch0)
)

AI_LAYER_OBJ_DECLARE(
  gemm_13_layer, 13,
  DENSE_TYPE, 0x0, NULL,
  dense, forward_dense_integer_SSSA_ch,
  &gemm_13_chain,
  NULL, &gemm_13_layer, AI_STATIC, 
)
/**  Hybrid layers declarations section  *************************************/
void forward_lite_eltwise_integer_INT8_eltwise_1(_stai_network_context* net_ctx)
{
  conv2d_0_output_array.data = AI_PTR(net_ctx->_activations[0] + 0);
  conv2d_0_output_array.data_start = AI_PTR(net_ctx->_activations[0] + 0);
  sequential_1_batch_normalization_1_batchnorm_mul_4D_array.data = AI_PTR(net_ctx->_weights[0] + 0);
  sequential_1_batch_normalization_1_batchnorm_mul_4D_array.data_start = AI_PTR(net_ctx->_weights[0] + 0);
  eltwise_1_output_array.data = AI_PTR(net_ctx->_activations[0] + 0);
  eltwise_1_output_array.data_start = AI_PTR(net_ctx->_activations[0] + 0);
  _STAI_NETWORK_EVENT_NODE_START_CB(1, 2, { conv2d_0_output.data->data,sequential_1_batch_normalization_1_batchnorm_mul_4D.data->data});
  forward_eltwise_integer_INT8(&eltwise_1_layer);
  _STAI_NETWORK_EVENT_NODE_STOP_CB(1, 1, { eltwise_1_output.data->data});
}
void forward_lite_eltwise_integer_INT8_eltwise_2(_stai_network_context* net_ctx)
{
  eltwise_1_output_array.data = AI_PTR(net_ctx->_activations[0] + 0);
  eltwise_1_output_array.data_start = AI_PTR(net_ctx->_activations[0] + 0);
  sequential_1_batch_normalization_1_batchnorm_sub_4D_array.data = AI_PTR(net_ctx->_weights[0] + 32);
  sequential_1_batch_normalization_1_batchnorm_sub_4D_array.data_start = AI_PTR(net_ctx->_weights[0] + 32);
  eltwise_2_output_array.data = AI_PTR(net_ctx->_activations[0] + 0);
  eltwise_2_output_array.data_start = AI_PTR(net_ctx->_activations[0] + 0);
  _STAI_NETWORK_EVENT_NODE_START_CB(2, 2, { eltwise_1_output.data->data,sequential_1_batch_normalization_1_batchnorm_sub_4D.data->data});
  forward_eltwise_integer_INT8(&eltwise_2_layer);
  _STAI_NETWORK_EVENT_NODE_STOP_CB(2, 1, { eltwise_2_output.data->data});
}
void forward_lite_eltwise_integer_INT8_eltwise_5(_stai_network_context* net_ctx)
{
  conv2d_4_output_array.data = AI_PTR(net_ctx->_activations[0] + 1216);
  conv2d_4_output_array.data_start = AI_PTR(net_ctx->_activations[0] + 1216);
  sequential_1_batch_normalization_1_2_batchnorm_mul_4D_array.data = AI_PTR(net_ctx->_weights[0] + 64);
  sequential_1_batch_normalization_1_2_batchnorm_mul_4D_array.data_start = AI_PTR(net_ctx->_weights[0] + 64);
  eltwise_5_output_array.data = AI_PTR(net_ctx->_activations[0] + 8896);
  eltwise_5_output_array.data_start = AI_PTR(net_ctx->_activations[0] + 8896);
  _STAI_NETWORK_EVENT_NODE_START_CB(5, 2, { conv2d_4_output.data->data,sequential_1_batch_normalization_1_2_batchnorm_mul_4D.data->data});
  forward_eltwise_integer_INT8(&eltwise_5_layer);
  _STAI_NETWORK_EVENT_NODE_STOP_CB(5, 1, { eltwise_5_output.data->data});
}
void forward_lite_eltwise_integer_INT8_eltwise_6(_stai_network_context* net_ctx)
{
  eltwise_5_output_array.data = AI_PTR(net_ctx->_activations[0] + 8896);
  eltwise_5_output_array.data_start = AI_PTR(net_ctx->_activations[0] + 8896);
  sequential_1_batch_normalization_1_2_batchnorm_sub_4D_array.data = AI_PTR(net_ctx->_weights[0] + 128);
  sequential_1_batch_normalization_1_2_batchnorm_sub_4D_array.data_start = AI_PTR(net_ctx->_weights[0] + 128);
  eltwise_6_output_array.data = AI_PTR(net_ctx->_activations[0] + 0);
  eltwise_6_output_array.data_start = AI_PTR(net_ctx->_activations[0] + 0);
  _STAI_NETWORK_EVENT_NODE_START_CB(6, 2, { eltwise_5_output.data->data,sequential_1_batch_normalization_1_2_batchnorm_sub_4D.data->data});
  forward_eltwise_integer_INT8(&eltwise_6_layer);
  _STAI_NETWORK_EVENT_NODE_STOP_CB(6, 1, { eltwise_6_output.data->data});
}
void forward_lite_eltwise_integer_INT8_eltwise_9(_stai_network_context* net_ctx)
{
  conv2d_8_output_array.data = AI_PTR(net_ctx->_activations[0] + 12800);
  conv2d_8_output_array.data_start = AI_PTR(net_ctx->_activations[0] + 12800);
  sequential_1_batch_normalization_2_1_batchnorm_mul_4D_array.data = AI_PTR(net_ctx->_weights[0] + 192);
  sequential_1_batch_normalization_2_1_batchnorm_mul_4D_array.data_start = AI_PTR(net_ctx->_weights[0] + 192);
  eltwise_9_output_array.data = AI_PTR(net_ctx->_activations[0] + 0);
  eltwise_9_output_array.data_start = AI_PTR(net_ctx->_activations[0] + 0);
  _STAI_NETWORK_EVENT_NODE_START_CB(9, 2, { conv2d_8_output.data->data,sequential_1_batch_normalization_2_1_batchnorm_mul_4D.data->data});
  forward_eltwise_integer_INT8(&eltwise_9_layer);
  _STAI_NETWORK_EVENT_NODE_STOP_CB(9, 1, { eltwise_9_output.data->data});
}
void forward_lite_eltwise_integer_INT8_eltwise_10(_stai_network_context* net_ctx)
{
  eltwise_9_output_array.data = AI_PTR(net_ctx->_activations[0] + 0);
  eltwise_9_output_array.data_start = AI_PTR(net_ctx->_activations[0] + 0);
  sequential_1_batch_normalization_2_1_batchnorm_sub_4D_array.data = AI_PTR(net_ctx->_weights[0] + 320);
  sequential_1_batch_normalization_2_1_batchnorm_sub_4D_array.data_start = AI_PTR(net_ctx->_weights[0] + 320);
  eltwise_10_output_array.data = AI_PTR(net_ctx->_activations[0] + 3072);
  eltwise_10_output_array.data_start = AI_PTR(net_ctx->_activations[0] + 3072);
  _STAI_NETWORK_EVENT_NODE_START_CB(10, 2, { eltwise_9_output.data->data,sequential_1_batch_normalization_2_1_batchnorm_sub_4D.data->data});
  forward_eltwise_integer_INT8(&eltwise_10_layer);
  _STAI_NETWORK_EVENT_NODE_STOP_CB(10, 1, { eltwise_10_output.data->data});
}
void forward_lite_ap_integer_INT8_pool_11(_stai_network_context* net_ctx)
{
  eltwise_10_output_array.data = AI_PTR(net_ctx->_activations[0] + 3072);
  eltwise_10_output_array.data_start = AI_PTR(net_ctx->_activations[0] + 3072);
  pool_11_output_array.data = AI_PTR(net_ctx->_activations[0] + 0);
  pool_11_output_array.data_start = AI_PTR(net_ctx->_activations[0] + 0);
  _STAI_NETWORK_EVENT_NODE_START_CB(11, 1, { eltwise_10_output.data->data});
  forward_ap_integer_INT8(&pool_11_layer);
  _STAI_NETWORK_EVENT_NODE_STOP_CB(11, 1, { pool_11_output.data->data});
}
void forward_lite_dense_integer_SSSA_ch_gemm_12(_stai_network_context* net_ctx)
{
  pool_11_output_array.data = AI_PTR(net_ctx->_activations[0] + 0);
  pool_11_output_array.data_start = AI_PTR(net_ctx->_activations[0] + 0);
  gemm_12_weights_array.data = AI_PTR(net_ctx->_weights[0] + 93792);
  gemm_12_weights_array.data_start = AI_PTR(net_ctx->_weights[0] + 93792);
  gemm_12_bias_array.data = AI_PTR(net_ctx->_weights[0] + 101984);
  gemm_12_bias_array.data_start = AI_PTR(net_ctx->_weights[0] + 101984);
  gemm_12_scratch0_array.data = AI_PTR(net_ctx->_activations[0] + 128);
  gemm_12_scratch0_array.data_start = AI_PTR(net_ctx->_activations[0] + 128);
  gemm_12_output_array.data = AI_PTR(net_ctx->_activations[0] + 1024);
  gemm_12_output_array.data_start = AI_PTR(net_ctx->_activations[0] + 1024);
  _STAI_NETWORK_EVENT_NODE_START_CB(12, 1, { pool_11_output.data->data});
  forward_dense_integer_SSSA_ch(&gemm_12_layer);
  _STAI_NETWORK_EVENT_NODE_STOP_CB(12, 1, { gemm_12_output.data->data});
}
void forward_lite_dense_integer_SSSA_ch_gemm_13(_stai_network_context* net_ctx)
{
  gemm_12_output_array.data = AI_PTR(net_ctx->_activations[0] + 1024);
  gemm_12_output_array.data_start = AI_PTR(net_ctx->_activations[0] + 1024);
  gemm_13_weights_array.data = AI_PTR(net_ctx->_weights[0] + 102240);
  gemm_13_weights_array.data_start = AI_PTR(net_ctx->_weights[0] + 102240);
  gemm_13_bias_array.data = AI_PTR(net_ctx->_weights[0] + 102496);
  gemm_13_bias_array.data_start = AI_PTR(net_ctx->_weights[0] + 102496);
  gemm_13_scratch0_array.data = AI_PTR(net_ctx->_activations[0] + 0);
  gemm_13_scratch0_array.data_start = AI_PTR(net_ctx->_activations[0] + 0);
  gemm_13_output_array.data = AI_PTR(net_ctx->_activations[0] + 168);
  gemm_13_output_array.data_start = AI_PTR(net_ctx->_activations[0] + 168);
  _STAI_NETWORK_EVENT_NODE_START_CB(13, 1, { gemm_12_output.data->data});
  forward_dense_integer_SSSA_ch(&gemm_13_layer);
  _STAI_NETWORK_EVENT_NODE_STOP_CB(13, 1, { gemm_13_output.data->data});
}

/*****************************************************************************/


static const ai_u16 conv2d_0_t_in_0_shape_w_const_u16 = 10;
static const ai_u16 conv2d_0_t_in_0_shape_h_const_u16 = 49;
static const ai_u16 conv2d_0_t_in_0_shape_ch_const_u16 = 1;
static const ai_u16 conv2d_0_t_out_0_shape_ch_const_u16 = 32;
static const ai_u16 conv2d_0_t_weight_0_shape_w_const_u16 = 3;
static const ai_u16 conv2d_0_t_weight_0_shape_h_const_u16 = 3;
static const ai_u16 conv2d_0_l_stride_1_const_u16 = 1;
static const ai_u16 conv2d_0_l_stride_0_const_u16 = 1;
static const ai_i32 conv2d_0_l_pad_W_0_const_s32 = 1;
static const ai_i32 conv2d_0_l_pad_H_0_const_s32 = 1;
static const ai_i8 conv2d_0_t_in_0_fmt_zero_const_s8 = 11;
static const ai_i8 conv2d_0_t_out_0_fmt_zero_const_s8 = -128;
static const ai_float conv2d_0_t_in_0_fmt_scale_const_f32 = 0.04888138920068741f;
static const ai_float conv2d_0_t_out_0_fmt_scale_const_f32 = 0.06193135678768158f;
static const ai_float conv2d_0_t_weight_0_fmt_scale_const_f32[] = LITE_ARRAY_VALUES(0.001231226371601224f, 0.0008157773991115391f, 0.0009852705989032984f, 0.0010892398422583938f, 0.0010886343661695719f, 0.0011425785487517715f, 0.001097018364816904f, 0.0010930432472378016f, 0.0011641993187367916f, 0.0012715405318886042f, 0.001139740808866918f, 0.0011101391864940524f, 0.0008043579873628914f, 0.0011157231638208032f, 0.0011536498786881566f, 0.0008746371604502201f, 0.001153887715190649f, 0.0015048339264467359f, 0.0012485776096582413f, 0.001173660159111023f, 0.0010160909732803702f, 0.001170010888017714f, 0.0012525671627372503f, 0.0011603282764554024f, 0.001167616923339665f, 0.0011961273849010468f, 0.0010314093669876456f, 0.0011168156052008271f, 0.0010894964216277003f, 0.001142352819442749f, 0.0007591270259581506f, 0.0008719608886167407f);
static const ai_layer_format_type conv2d_0_l_out_ch_format_const_layer_format_type = AI_LAYER_FORMAT_CHANNEL_LAST_VALID;
static const ai_u16 conv2d_0_t_out_0_shape_w_const_u16 = 10;
static const ai_u16 conv2d_0_t_out_0_shape_h_const_u16 = 49;



static const ai_u16 pool_3_t_in_0_shape_w_const_u16 = 10;
static const ai_u16 pool_3_t_in_0_shape_h_const_u16 = 49;
static const ai_u16 pool_3_t_in_0_shape_ch_const_u16 = 32;
static const ai_u16 pool_3_l_pool_size_1_const_u16 = 2;
static const ai_u16 pool_3_l_pool_size_0_const_u16 = 2;
static const ai_u16 pool_3_l_legacy_pool_pad_1_const_u16 = 0;
static const ai_u16 pool_3_l_legacy_pool_pad_0_const_u16 = 0;
static const ai_u16 pool_3_l_pool_stride_1_const_u16 = 2;
static const ai_u16 pool_3_l_pool_stride_0_const_u16 = 2;
static const ai_u16 pool_3_t_out_0_shape_w_const_u16 = 5;
static const ai_u16 pool_3_t_out_0_shape_h_const_u16 = 24;
static const ai_i8 pool_3_t_in_0_fmt_zero_const_s8 = -116;
static const ai_i8 pool_3_t_out_0_fmt_zero_const_s8 = -116;

static const ai_i8 conv2d_4_pad_before_v_pad_constant_value_const_s8[] = LITE_ARRAY_VALUES(-116);
static const ai_i16 conv2d_4_pad_before_t_in_0_fmt_bitsize_const_s16 = 8;
static const ai_u32 conv2d_4_pad_before_t_in_0_shape_h_const_u32 = 24;

static const ai_u16 conv2d_4_t_in_0_shape_w_const_u16 = 7;
static const ai_u16 conv2d_4_t_in_0_shape_h_const_u16 = 26;
static const ai_u16 conv2d_4_t_in_0_shape_ch_const_u16 = 32;
static const ai_u16 conv2d_4_t_out_0_shape_ch_const_u16 = 64;
static const ai_i8 conv2d_4_t_in_0_fmt_zero_const_s8 = -116;
static const ai_i8 conv2d_4_t_out_0_fmt_zero_const_s8 = -128;
static const ai_float conv2d_4_t_in_0_fmt_scale_const_f32 = 0.06316056102514267f;
static const ai_float conv2d_4_t_out_0_fmt_scale_const_f32 = 0.07023479044437408f;
static const ai_float conv2d_4_t_weight_0_fmt_scale_const_f32[] = LITE_ARRAY_VALUES(0.00126814900431782f, 0.0010441901395097375f, 0.0010014561703428626f, 0.0012809757608920336f, 0.0011490845354273915f, 0.0011025083949789405f, 0.0009539746097289026f, 0.0010588520672172308f, 0.0010401471517980099f, 0.0011536950478330255f, 0.0011746721575036645f, 0.0010571688180789351f, 0.0010632348712533712f, 0.001065628370270133f, 0.001018195180222392f, 0.0010050057899206877f, 0.0009786427253857255f, 0.0012148359091952443f, 0.0012428989866748452f, 0.0010941457003355026f, 0.0009192585130222142f, 0.0010723916348069906f, 0.0010560466907918453f, 0.0010055657476186752f, 0.0013231032062321901f, 0.0010590542806312442f, 0.0011067412560805678f, 0.0011240106541663408f, 0.0010566959390416741f, 0.0010129136499017477f, 0.0011014838237315416f, 0.0013239989057183266f, 0.0009591903071850538f, 0.0010323255555704236f, 0.0009993136627599597f, 0.001110286102630198f, 0.001026804675348103f, 0.001133685582317412f, 0.0011285168584436178f, 0.0010649109026417136f, 0.0010193142807111144f, 0.0010579918744042516f, 0.0011884437408298254f, 0.0011731230188161135f, 0.0010600493988022208f, 0.0010956277837976813f, 0.0008619963773526251f, 0.0014033173210918903f, 0.001064782147295773f, 0.0010849935933947563f, 0.0010299439309164882f, 0.0009553400450386107f, 0.0010299936402589083f, 0.001102699781768024f, 0.001005790545605123f, 0.0010438328608870506f, 0.001210548565723002f, 0.0010284100426360965f, 0.0009313555201515555f, 0.0010978304781019688f, 0.0010941751534119248f, 0.0010544473771005869f, 0.001152936602011323f, 0.0010033713188022375f);
static const ai_layer_format_type conv2d_4_l_out_ch_format_const_layer_format_type = AI_LAYER_FORMAT_CHANNEL_LAST_VALID;
static const ai_u16 conv2d_4_t_out_0_shape_w_const_u16 = 5;
static const ai_u16 conv2d_4_t_out_0_shape_h_const_u16 = 24;



static const ai_u16 pool_7_t_in_0_shape_w_const_u16 = 5;
static const ai_u16 pool_7_t_in_0_shape_h_const_u16 = 24;
static const ai_u16 pool_7_t_in_0_shape_ch_const_u16 = 64;
static const ai_u16 pool_7_l_pool_size_1_const_u16 = 2;
static const ai_u16 pool_7_l_pool_size_0_const_u16 = 2;
static const ai_u16 pool_7_l_legacy_pool_pad_1_const_u16 = 0;
static const ai_u16 pool_7_l_legacy_pool_pad_0_const_u16 = 0;
static const ai_u16 pool_7_l_pool_stride_1_const_u16 = 2;
static const ai_u16 pool_7_l_pool_stride_0_const_u16 = 2;
static const ai_u16 pool_7_t_out_0_shape_w_const_u16 = 2;
static const ai_u16 pool_7_t_out_0_shape_h_const_u16 = 12;
static const ai_i8 pool_7_t_in_0_fmt_zero_const_s8 = -118;
static const ai_i8 pool_7_t_out_0_fmt_zero_const_s8 = -118;

static const ai_i8 conv2d_8_pad_before_v_pad_constant_value_const_s8[] = LITE_ARRAY_VALUES(-118);
static const ai_i16 conv2d_8_pad_before_t_in_0_fmt_bitsize_const_s16 = 8;
static const ai_u32 conv2d_8_pad_before_t_in_0_shape_h_const_u32 = 12;

static const ai_u16 conv2d_8_t_in_0_shape_w_const_u16 = 4;
static const ai_u16 conv2d_8_t_in_0_shape_h_const_u16 = 14;
static const ai_u16 conv2d_8_t_in_0_shape_ch_const_u16 = 64;
static const ai_u16 conv2d_8_t_out_0_shape_ch_const_u16 = 128;
static const ai_i8 conv2d_8_t_in_0_fmt_zero_const_s8 = -118;
static const ai_i8 conv2d_8_t_out_0_fmt_zero_const_s8 = -128;
static const ai_float conv2d_8_t_in_0_fmt_scale_const_f32 = 0.07080087065696716f;
static const ai_float conv2d_8_t_out_0_fmt_scale_const_f32 = 0.08150845766067505f;
static const ai_float conv2d_8_t_weight_0_fmt_scale_const_f32[] = LITE_ARRAY_VALUES(0.0007925272220745683f, 0.001003748970106244f, 0.0008283288916572928f, 0.0010582981631159782f, 0.0008096141391433775f, 0.0007661686395294964f, 0.0011026260908693075f, 0.0009283795952796936f, 0.0009439016575925052f, 0.0010413367999717593f, 0.0008084932924248278f, 0.0008087223977781832f, 0.0009666361729614437f, 0.0008215886191464961f, 0.0009260078659281135f, 0.0008514836081303656f, 0.000776188971940428f, 0.0010136663913726807f, 0.0008817444904707372f, 0.0008631795644760132f, 0.0008697614539414644f, 0.0010329801589250565f, 0.001017884467728436f, 0.0008250122773461044f, 0.0007875653100199997f, 0.000917882367502898f, 0.0008567213080823421f, 0.0006532297120429575f, 0.0009312504553236067f, 0.0009113315027207136f, 0.0009468491771258414f, 0.000857359787914902f, 0.0009745933930389583f, 0.0009428209741599858f, 0.0008318975451402366f, 0.0008311847341246903f, 0.0008426482672803104f, 0.0008293690043501556f, 0.0009570599650032818f, 0.000957327603828162f, 0.0009636549511924386f, 0.000925095344427973f, 0.000990077736787498f, 0.0010004814248532057f, 0.0010204489808529615f, 0.0008395119803026319f, 0.0008403356187045574f, 0.0009967002552002668f, 0.0008806741097941995f, 0.0008941285777837038f, 0.0008748728432692587f, 0.0009441832662560046f, 0.0008948238100856543f, 0.0009281106176786125f, 0.0007715955143794417f, 0.0008044227142818272f, 0.0008788262493908405f, 0.0008910440956242383f, 0.0010865040821954608f, 0.0007777138380333781f, 0.0009233049349859357f, 0.0008515665540471673f, 0.001180227380245924f, 0.0009149980032816529f, 0.000921557133551687f, 0.0009008250199258327f, 0.0008588530472479761f, 0.0009045430342666805f, 0.0008053426281549037f, 0.0008818209171295166f, 0.000925215077586472f, 0.0008492151973769069f, 0.000744415505323559f, 0.0007489967392757535f, 0.0008909348980523646f, 0.0008677885634824634f, 0.000858730694744736f, 0.0009049000218510628f, 0.001017472823150456f, 0.0009552340488880873f, 0.0007706025498919189f, 0.0009008170454762876f, 0.000980093376711011f, 0.0008686528308317065f, 0.0009819363476708531f, 0.0008738904143683612f, 0.0009042416349984705f, 0.0010228942846879363f, 0.0008188606007024646f, 0.0008261982002295554f, 0.0007779457955621183f, 0.0007877282332628965f, 0.0007865301449783146f, 0.0010231025516986847f, 0.0009306527790613472f, 0.0008028706070035696f, 0.0008857134962454438f, 0.0008863628027029335f, 0.0010495205642655492f, 0.000791342172306031f, 0.0008539656409993768f, 0.0012070507509633899f, 0.0008739532204344869f, 0.0009297411888837814f, 0.0008922363049350679f, 0.0009255826007574797f, 0.0008831334416754544f, 0.0008888447191566229f, 0.0007139539229683578f, 0.0008203368633985519f, 0.0008520348928868771f, 0.0008631368982605636f, 0.000942845712415874f, 0.000988415558822453f, 0.0008076062658801675f, 0.0009834695374593139f, 0.0008978117257356644f, 0.0007930900319479406f, 0.000915993528906256f, 0.0009705898119136691f, 0.0013468959368765354f, 0.0009651139262132347f, 0.0010956400074064732f, 0.0009232270531356335f, 0.0010764339240267873f, 0.0008003644179552794f, 0.0008198948344215751f, 0.0008510923362337053f);
static const ai_layer_format_type conv2d_8_l_out_ch_format_const_layer_format_type = AI_LAYER_FORMAT_CHANNEL_LAST_VALID;
static const ai_u16 conv2d_8_t_out_0_shape_w_const_u16 = 2;
static const ai_u16 conv2d_8_t_out_0_shape_h_const_u16 = 12;






static const ai_u32 nl_14_t_in_0_shape_ch_prod_const_u32 = 4;
STAI_API_ENTRY
stai_return_code stai_network_run(
  stai_network* network,
  const stai_run_mode mode)
{
   STAI_UNUSED(mode)
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)

  _STAI_SET_ERROR(net_ctx, (net_ctx->_flags & STAI_FLAG_ACTIVATIONS) != STAI_FLAG_ACTIVATIONS,
        STAI_ERROR_NETWORK_INVALID_ACTIVATIONS_PTR, net_ctx->_return_code)

  _STAI_SET_ERROR(net_ctx, (net_ctx->_flags & STAI_FLAG_INPUTS) != STAI_FLAG_INPUTS,
                  STAI_ERROR_NETWORK_INVALID_IN_PTR, net_ctx->_return_code)
  _STAI_SET_ERROR(net_ctx, (net_ctx->_flags & STAI_FLAG_OUTPUTS) != STAI_FLAG_OUTPUTS,
                  STAI_ERROR_NETWORK_INVALID_OUT_PTR, net_ctx->_return_code)

  _STAI_SET_ERROR(net_ctx, (net_ctx->_flags & STAI_FLAG_WEIGHTS) != STAI_FLAG_WEIGHTS,
                  STAI_ERROR_NETWORK_INVALID_WEIGHTS_PTR, net_ctx->_return_code)


  /* LITE_KERNEL_SECTION BEGIN conv2d_0 */
  {
      const ai_i8* conv2d_0_t_in_0_ptr_const_s8 = (ai_i8*)(net_ctx->_inputs[0] + 0);
    const ai_i8* conv2d_0_t_weight_0_ptr_const_s8 = (ai_i8*)(net_ctx->_weights[0] + 448);
    const ai_i32* conv2d_0_t_weight_1_ptr_const_s32 = (ai_i32*)(net_ctx->_weights[0] + 736);
    ai_i8* conv2d_0_t_out_0_ptr_s8 = (ai_i8*)(net_ctx->_activations[0] + 0);
    ai_i16* conv2d_0_t_scratch_0_ptr_s16 = (ai_i16*)(net_ctx->_activations[0] + 15680);
  
  _STAI_NETWORK_EVENT_NODE_START_CB(0, 1, {(stai_ptr) conv2d_0_t_in_0_ptr_const_s8});
    
  forward_lite_conv2d_sssa8_ch(conv2d_0_t_in_0_ptr_const_s8, conv2d_0_t_in_0_shape_w_const_u16, conv2d_0_t_in_0_shape_h_const_u16, conv2d_0_t_in_0_shape_ch_const_u16, conv2d_0_t_weight_0_ptr_const_s8, conv2d_0_t_out_0_shape_ch_const_u16, conv2d_0_t_weight_0_shape_w_const_u16, conv2d_0_t_weight_0_shape_h_const_u16, conv2d_0_l_stride_1_const_u16, conv2d_0_l_stride_0_const_u16, conv2d_0_l_pad_W_0_const_s32, conv2d_0_l_pad_H_0_const_s32, conv2d_0_t_weight_1_ptr_const_s32, conv2d_0_t_in_0_fmt_zero_const_s8, conv2d_0_t_out_0_fmt_zero_const_s8, conv2d_0_t_in_0_fmt_scale_const_f32, conv2d_0_t_out_0_fmt_scale_const_f32, conv2d_0_t_weight_0_fmt_scale_const_f32, conv2d_0_l_out_ch_format_const_layer_format_type, conv2d_0_t_out_0_ptr_s8, conv2d_0_t_out_0_shape_w_const_u16, conv2d_0_t_out_0_shape_h_const_u16, 1, 1060, conv2d_0_t_scratch_0_ptr_s16);
    
  _STAI_NETWORK_EVENT_NODE_STOP_CB(0, 1, {(stai_ptr) conv2d_0_t_out_0_ptr_s8});
  }
  /* LITE_KERNEL_SECTION END conv2d_0 */
  /* LITE_KERNEL_SECTION BEGIN eltwise_1 */
  {
    
  forward_lite_eltwise_integer_INT8_eltwise_1(net_ctx);
  }
  /* LITE_KERNEL_SECTION END eltwise_1 */
  /* LITE_KERNEL_SECTION BEGIN eltwise_2 */
  {
    
  forward_lite_eltwise_integer_INT8_eltwise_2(net_ctx);
  }
  /* LITE_KERNEL_SECTION END eltwise_2 */
  /* LITE_KERNEL_SECTION BEGIN pool_3 */
  {
      const ai_i8* pool_3_t_in_0_ptr_const_s8 = (ai_i8*)(net_ctx->_activations[0] + 0);
    ai_i8* pool_3_t_out_0_ptr_s8 = (ai_i8*)(net_ctx->_activations[0] + 0);
  
  _STAI_NETWORK_EVENT_NODE_START_CB(3, 1, {(stai_ptr) pool_3_t_in_0_ptr_const_s8});
    
  forward_lite_maxpool_is8os8_scalepos(pool_3_t_in_0_ptr_const_s8, pool_3_t_out_0_ptr_s8, pool_3_t_in_0_shape_w_const_u16, pool_3_t_in_0_shape_h_const_u16, pool_3_t_in_0_shape_ch_const_u16, pool_3_l_pool_size_1_const_u16, pool_3_l_pool_size_0_const_u16, pool_3_l_legacy_pool_pad_1_const_u16, pool_3_l_legacy_pool_pad_0_const_u16, pool_3_l_pool_stride_1_const_u16, pool_3_l_pool_stride_0_const_u16, pool_3_t_out_0_shape_w_const_u16, pool_3_t_out_0_shape_h_const_u16, 1.0f, pool_3_t_in_0_fmt_zero_const_s8, pool_3_t_out_0_fmt_zero_const_s8);
    
  _STAI_NETWORK_EVENT_NODE_STOP_CB(3, 1, {(stai_ptr) pool_3_t_out_0_ptr_s8});
  }
  /* LITE_KERNEL_SECTION END pool_3 */
  /* LITE_KERNEL_SECTION BEGIN conv2d_4_pad_before */
  {
      const ai_ptr conv2d_4_pad_before_t_in_0_ptr_const_ptr = (ai_ptr)(net_ctx->_activations[0] + 0);
    ai_ptr conv2d_4_pad_before_t_out_0_ptr_ptr = (ai_ptr)(net_ctx->_activations[0] + 3840);
  
  _STAI_NETWORK_EVENT_NODE_START_CB(4, 1, {(stai_ptr) conv2d_4_pad_before_t_in_0_ptr_const_ptr});
    
  forward_lite_pad_constant(conv2d_4_pad_before_t_in_0_ptr_const_ptr, conv2d_4_pad_before_t_out_0_ptr_ptr, (ai_handle)(conv2d_4_pad_before_v_pad_constant_value_const_s8), conv2d_4_pad_before_t_in_0_fmt_bitsize_const_s16, conv2d_4_pad_before_t_in_0_shape_h_const_u32, (ai_i32)(1), (ai_i32)(160), (ai_i32)(224), (ai_i32)(224), (ai_i32)(32), (ai_i32)(32));
    
  _STAI_NETWORK_EVENT_NODE_STOP_CB(4, 1, {(stai_ptr) conv2d_4_pad_before_t_out_0_ptr_ptr});
  }
  /* LITE_KERNEL_SECTION END conv2d_4_pad_before */
  /* LITE_KERNEL_SECTION BEGIN conv2d_4 */
  {
      const ai_i8* conv2d_4_t_in_0_ptr_const_s8 = (ai_i8*)(net_ctx->_activations[0] + 3840);
    const ai_i8* conv2d_4_t_weight_0_ptr_const_s8 = (ai_i8*)(net_ctx->_weights[0] + 864);
    const ai_i32* conv2d_4_t_weight_1_ptr_const_s32 = (ai_i32*)(net_ctx->_weights[0] + 19296);
    ai_i8* conv2d_4_t_out_0_ptr_s8 = (ai_i8*)(net_ctx->_activations[0] + 1216);
    ai_i16* conv2d_4_t_scratch_0_ptr_s16 = (ai_i16*)(net_ctx->_activations[0] + 9664);
  
  _STAI_NETWORK_EVENT_NODE_START_CB(4, 1, {(stai_ptr) conv2d_4_t_in_0_ptr_const_s8});
    
  forward_lite_conv2d_deep_3x3_sssa8_ch(conv2d_4_t_in_0_ptr_const_s8, conv2d_4_t_in_0_shape_w_const_u16, conv2d_4_t_in_0_shape_h_const_u16, conv2d_4_t_in_0_shape_ch_const_u16, conv2d_4_t_weight_0_ptr_const_s8, conv2d_4_t_out_0_shape_ch_const_u16, conv2d_4_t_weight_1_ptr_const_s32, conv2d_4_t_in_0_fmt_zero_const_s8, conv2d_4_t_out_0_fmt_zero_const_s8, conv2d_4_t_in_0_fmt_scale_const_f32, conv2d_4_t_out_0_fmt_scale_const_f32, conv2d_4_t_weight_0_fmt_scale_const_f32, conv2d_4_l_out_ch_format_const_layer_format_type, conv2d_4_t_out_0_ptr_s8, conv2d_4_t_out_0_shape_w_const_u16, conv2d_4_t_out_0_shape_h_const_u16, 1, 7168, conv2d_4_t_scratch_0_ptr_s16);
    
  _STAI_NETWORK_EVENT_NODE_STOP_CB(4, 1, {(stai_ptr) conv2d_4_t_out_0_ptr_s8});
  }
  /* LITE_KERNEL_SECTION END conv2d_4 */
  /* LITE_KERNEL_SECTION BEGIN eltwise_5 */
  {
    
  forward_lite_eltwise_integer_INT8_eltwise_5(net_ctx);
  }
  /* LITE_KERNEL_SECTION END eltwise_5 */
  /* LITE_KERNEL_SECTION BEGIN eltwise_6 */
  {
    
  forward_lite_eltwise_integer_INT8_eltwise_6(net_ctx);
  }
  /* LITE_KERNEL_SECTION END eltwise_6 */
  /* LITE_KERNEL_SECTION BEGIN pool_7 */
  {
      const ai_i8* pool_7_t_in_0_ptr_const_s8 = (ai_i8*)(net_ctx->_activations[0] + 0);
    ai_i8* pool_7_t_out_0_ptr_s8 = (ai_i8*)(net_ctx->_activations[0] + 7680);
  
  _STAI_NETWORK_EVENT_NODE_START_CB(7, 1, {(stai_ptr) pool_7_t_in_0_ptr_const_s8});
    
  forward_lite_maxpool_is8os8_scalepos(pool_7_t_in_0_ptr_const_s8, pool_7_t_out_0_ptr_s8, pool_7_t_in_0_shape_w_const_u16, pool_7_t_in_0_shape_h_const_u16, pool_7_t_in_0_shape_ch_const_u16, pool_7_l_pool_size_1_const_u16, pool_7_l_pool_size_0_const_u16, pool_7_l_legacy_pool_pad_1_const_u16, pool_7_l_legacy_pool_pad_0_const_u16, pool_7_l_pool_stride_1_const_u16, pool_7_l_pool_stride_0_const_u16, pool_7_t_out_0_shape_w_const_u16, pool_7_t_out_0_shape_h_const_u16, 1.0f, pool_7_t_in_0_fmt_zero_const_s8, pool_7_t_out_0_fmt_zero_const_s8);
    
  _STAI_NETWORK_EVENT_NODE_STOP_CB(7, 1, {(stai_ptr) pool_7_t_out_0_ptr_s8});
  }
  /* LITE_KERNEL_SECTION END pool_7 */
  /* LITE_KERNEL_SECTION BEGIN conv2d_8_pad_before */
  {
      const ai_ptr conv2d_8_pad_before_t_in_0_ptr_const_ptr = (ai_ptr)(net_ctx->_activations[0] + 7680);
    ai_ptr conv2d_8_pad_before_t_out_0_ptr_ptr = (ai_ptr)(net_ctx->_activations[0] + 0);
  
  _STAI_NETWORK_EVENT_NODE_START_CB(8, 1, {(stai_ptr) conv2d_8_pad_before_t_in_0_ptr_const_ptr});
    
  forward_lite_pad_constant(conv2d_8_pad_before_t_in_0_ptr_const_ptr, conv2d_8_pad_before_t_out_0_ptr_ptr, (ai_handle)(conv2d_8_pad_before_v_pad_constant_value_const_s8), conv2d_8_pad_before_t_in_0_fmt_bitsize_const_s16, conv2d_8_pad_before_t_in_0_shape_h_const_u32, (ai_i32)(1), (ai_i32)(128), (ai_i32)(256), (ai_i32)(256), (ai_i32)(64), (ai_i32)(64));
    
  _STAI_NETWORK_EVENT_NODE_STOP_CB(8, 1, {(stai_ptr) conv2d_8_pad_before_t_out_0_ptr_ptr});
  }
  /* LITE_KERNEL_SECTION END conv2d_8_pad_before */
  /* LITE_KERNEL_SECTION BEGIN conv2d_8 */
  {
      const ai_i8* conv2d_8_t_in_0_ptr_const_s8 = (ai_i8*)(net_ctx->_activations[0] + 0);
    const ai_i8* conv2d_8_t_weight_0_ptr_const_s8 = (ai_i8*)(net_ctx->_weights[0] + 19552);
    const ai_i32* conv2d_8_t_weight_1_ptr_const_s32 = (ai_i32*)(net_ctx->_weights[0] + 93280);
    ai_i8* conv2d_8_t_out_0_ptr_s8 = (ai_i8*)(net_ctx->_activations[0] + 12800);
    ai_i16* conv2d_8_t_scratch_0_ptr_s16 = (ai_i16*)(net_ctx->_activations[0] + 3584);
  
  _STAI_NETWORK_EVENT_NODE_START_CB(8, 1, {(stai_ptr) conv2d_8_t_in_0_ptr_const_s8});
    
  forward_lite_conv2d_deep_3x3_sssa8_ch(conv2d_8_t_in_0_ptr_const_s8, conv2d_8_t_in_0_shape_w_const_u16, conv2d_8_t_in_0_shape_h_const_u16, conv2d_8_t_in_0_shape_ch_const_u16, conv2d_8_t_weight_0_ptr_const_s8, conv2d_8_t_out_0_shape_ch_const_u16, conv2d_8_t_weight_1_ptr_const_s32, conv2d_8_t_in_0_fmt_zero_const_s8, conv2d_8_t_out_0_fmt_zero_const_s8, conv2d_8_t_in_0_fmt_scale_const_f32, conv2d_8_t_out_0_fmt_scale_const_f32, conv2d_8_t_weight_0_fmt_scale_const_f32, conv2d_8_l_out_ch_format_const_layer_format_type, conv2d_8_t_out_0_ptr_s8, conv2d_8_t_out_0_shape_w_const_u16, conv2d_8_t_out_0_shape_h_const_u16, 1, 9216, conv2d_8_t_scratch_0_ptr_s16);
    
  _STAI_NETWORK_EVENT_NODE_STOP_CB(8, 1, {(stai_ptr) conv2d_8_t_out_0_ptr_s8});
  }
  /* LITE_KERNEL_SECTION END conv2d_8 */
  /* LITE_KERNEL_SECTION BEGIN eltwise_9 */
  {
    
  forward_lite_eltwise_integer_INT8_eltwise_9(net_ctx);
  }
  /* LITE_KERNEL_SECTION END eltwise_9 */
  /* LITE_KERNEL_SECTION BEGIN eltwise_10 */
  {
    
  forward_lite_eltwise_integer_INT8_eltwise_10(net_ctx);
  }
  /* LITE_KERNEL_SECTION END eltwise_10 */
  /* LITE_KERNEL_SECTION BEGIN pool_11 */
  {
    
  forward_lite_ap_integer_INT8_pool_11(net_ctx);
  }
  /* LITE_KERNEL_SECTION END pool_11 */
  /* LITE_KERNEL_SECTION BEGIN gemm_12 */
  {
    
  forward_lite_dense_integer_SSSA_ch_gemm_12(net_ctx);
  }
  /* LITE_KERNEL_SECTION END gemm_12 */
  /* LITE_KERNEL_SECTION BEGIN gemm_13 */
  {
    
  forward_lite_dense_integer_SSSA_ch_gemm_13(net_ctx);
  }
  /* LITE_KERNEL_SECTION END gemm_13 */
  /* LITE_KERNEL_SECTION BEGIN nl_14 */
  {
      ai_i8* nl_14_t_out_0_ptr_s8 = (ai_i8*)(net_ctx->_outputs[0] + 0);
    const ai_i8* nl_14_t_in_0_ptr_const_s8 = (ai_i8*)(net_ctx->_activations[0] + 168);
    ai_i32* nl_14_t_scratch_0_ptr_s32 = (ai_i32*)(net_ctx->_activations[0] + 172);
  
  _STAI_NETWORK_EVENT_NODE_START_CB(14, 1, {(stai_ptr) nl_14_t_in_0_ptr_const_s8});
    
  forward_lite_nl_softmax_is8os8(nl_14_t_out_0_ptr_s8, nl_14_t_in_0_ptr_const_s8, nl_14_t_in_0_shape_ch_prod_const_u32, 1, 4, 2036382336, 22, -496, nl_14_t_scratch_0_ptr_s32);
    
  _STAI_NETWORK_EVENT_NODE_STOP_CB(14, 1, {(stai_ptr) nl_14_t_out_0_ptr_s8});
  }
  /* LITE_KERNEL_SECTION END nl_14 */
  return net_ctx->_return_code;
}

/*****************************************************************************/
/*  Getters APIs Section  */
STAI_API_ENTRY
stai_size stai_network_get_context_size()
{
  return (stai_size)STAI_NETWORK_CONTEXT_SIZE;
}

#if defined(HAVE_NETWORK_INFO)
STAI_API_ENTRY
stai_return_code stai_network_get_info(
  stai_network* network,
  stai_network_info* info)
{
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)
  _STAI_SET_ERROR(net_ctx, info==NULL, STAI_ERROR_NETWORK_INVALID_INFO, net_ctx->_return_code)

  // Copy of network info struct
  *info = g_network_info;

  return STAI_SUCCESS;
}
#endif


STAI_API_ENTRY
stai_return_code stai_network_get_activations(
  stai_network* network, stai_ptr* activations, stai_size* n_activations)
{
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)

  _STAI_SET_ERROR(net_ctx, !n_activations, STAI_ERROR_NETWORK_INVALID_API_ARGUMENTS, net_ctx->_return_code)
  *n_activations = STAI_NETWORK_ACTIVATIONS_NUM;
for (stai_size idx=0; activations && (idx<STAI_NETWORK_ACTIVATIONS_NUM); idx++) {
    // get address of the activations buffers
    activations[idx] = net_ctx->_activations[idx];
  }return net_ctx->_return_code;
}


STAI_API_ENTRY
stai_return_code stai_network_get_weights(
  stai_network* network, stai_ptr* weights, stai_size* n_weights)
{
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)
  _STAI_SET_ERROR(net_ctx, !n_weights, STAI_ERROR_NETWORK_INVALID_API_ARGUMENTS, net_ctx->_return_code)
  *n_weights = STAI_NETWORK_WEIGHTS_NUM;
for (stai_size idx=0; weights && (idx<STAI_NETWORK_WEIGHTS_NUM); idx++) {
    // get address of the weights buffers
    weights[idx] = net_ctx->_weights[idx];
  }return net_ctx->_return_code;
}


STAI_API_ENTRY
stai_return_code stai_network_get_inputs(
  stai_network* network, stai_ptr* inputs, stai_size* n_inputs)
{
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)
  _STAI_SET_ERROR(net_ctx, !n_inputs, STAI_ERROR_NETWORK_INVALID_API_ARGUMENTS, net_ctx->_return_code)
  *n_inputs = STAI_NETWORK_IN_NUM;
  for (stai_size idx=0; inputs && (idx<STAI_NETWORK_IN_NUM); idx++) {
    inputs[idx] = net_ctx->_inputs[idx];
  }
  return net_ctx->_return_code;
}


STAI_API_ENTRY
stai_return_code stai_network_get_outputs(
  stai_network* network, stai_ptr* outputs, stai_size* n_outputs)
{
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)
  _STAI_SET_ERROR(net_ctx, !n_outputs, STAI_ERROR_NETWORK_INVALID_API_ARGUMENTS, net_ctx->_return_code)
  *n_outputs = STAI_NETWORK_OUT_NUM;
  for (stai_size idx=0; outputs && (idx<STAI_NETWORK_OUT_NUM); idx++) {
    outputs[idx] = net_ctx->_outputs[idx];
  }
  return net_ctx->_return_code;
}


STAI_API_ENTRY
stai_return_code stai_network_get_error(
  stai_network* network)
{
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)

  /* return 1st generated error or STAI_SUCCESS if no errors so far */
  return net_ctx->_return_code;
}


STAI_API_ENTRY
stai_return_code stai_network_get_states(
  stai_network* network, stai_ptr* states, stai_size* n_states)
{
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)
  _STAI_SET_ERROR(net_ctx, !n_states, STAI_ERROR_NETWORK_INVALID_API_ARGUMENTS, net_ctx->_return_code)
  /* get the number of internals states (supporting multi-heap also for internal states) */
  *n_states = STAI_NETWORK_STATES_NUM;

  STAI_UNUSED(states)
return net_ctx->_return_code;
}


/*****************************************************************************/
/*  Setters APIs Section  */

STAI_API_ENTRY
stai_return_code stai_network_set_activations(
  stai_network* network,
  const stai_ptr* activations,
  const stai_size n_activations)
{
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)
const uintptr_t _activations_alignment[] = STAI_NETWORK_ACTIVATIONS_ALIGNMENTS;
  STAI_PRINT("  [stai_network_set_activations] network(%p) activations[%d]: %p\n\n", net_ctx, n_activations, activations)
  _STAI_SET_ERROR(net_ctx, !activations,
                  STAI_ERROR_NETWORK_INVALID_API_ARGUMENTS, net_ctx->_return_code)
  _STAI_SET_ERROR(net_ctx, n_activations!=STAI_NETWORK_ACTIVATIONS_NUM,
                  STAI_ERROR_NETWORK_INVALID_ACTIVATIONS_NUM, net_ctx->_return_code)

  for (stai_size idx=0; activations && idx<STAI_NETWORK_ACTIVATIONS_NUM; idx++) {
    STAI_PRINT("  activation[%d]: %p\n", idx, activations[idx])
    _STAI_SET_ERROR(net_ctx, activations[idx]==NULL,
                    STAI_ERROR_NETWORK_INVALID_ACTIVATIONS_PTR, net_ctx->_return_code)
    _STAI_SET_ERROR(net_ctx, ((uintptr_t)activations[idx]) & (_activations_alignment[idx]-1),
                    STAI_ERROR_INVALID_BUFFER_ALIGNMENT, net_ctx->_return_code)
    net_ctx->_activations[idx] = activations[idx];
  }
  net_ctx->_inputs[0] = activations[0] + 16740;

  net_ctx->_outputs[0] = activations[0] + 0;
_stai_network_check(net_ctx);
  return net_ctx->_return_code;
}


STAI_API_ENTRY
stai_return_code stai_network_set_weights(
  stai_network* network,
  const stai_ptr* weights,
  const stai_size n_weights)
{
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)
const uintptr_t _weights_alignment[] = STAI_NETWORK_WEIGHTS_ALIGNMENTS;
  _STAI_SET_ERROR(net_ctx, !weights,
                  STAI_ERROR_NETWORK_INVALID_API_ARGUMENTS, net_ctx->_return_code)
  _STAI_SET_ERROR(net_ctx, n_weights!=STAI_NETWORK_WEIGHTS_NUM,
                  STAI_ERROR_NETWORK_INVALID_WEIGHTS_NUM, net_ctx->_return_code)
  for (stai_size idx=0; weights && idx<STAI_NETWORK_WEIGHTS_NUM; idx++) {
    STAI_PRINT("  weight[%d]: %p\n", idx, weights[idx])
    _STAI_SET_ERROR(net_ctx, weights[idx]==NULL,
                    STAI_ERROR_NETWORK_INVALID_WEIGHTS_PTR, net_ctx->_return_code)
    _STAI_SET_ERROR(net_ctx, ((uintptr_t)weights[idx]) & (_weights_alignment[idx]-1),
                    STAI_ERROR_INVALID_BUFFER_ALIGNMENT, net_ctx->_return_code)
    net_ctx->_weights[idx] = weights[idx];
  }_stai_network_check(net_ctx);
  return net_ctx->_return_code;
}


STAI_API_ENTRY
stai_return_code stai_network_set_inputs(
  stai_network* network,
  const stai_ptr* inputs,
  const stai_size n_inputs)
{
  const uintptr_t _inputs_alignment[] = STAI_NETWORK_IN_ALIGNMENTS;
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)
  _STAI_SET_ERROR(net_ctx, !inputs,
                  STAI_ERROR_NETWORK_INVALID_API_ARGUMENTS, net_ctx->_return_code)
  _STAI_SET_ERROR(net_ctx, n_inputs!=STAI_NETWORK_IN_NUM,
                  STAI_ERROR_NETWORK_INVALID_IN_NUM, net_ctx->_return_code)

  for (stai_size idx=0; inputs && idx<STAI_NETWORK_IN_NUM; idx++) {
    STAI_PRINT("  input[%d]: %p\n", idx, inputs[idx])
    _STAI_SET_ERROR(net_ctx, inputs[idx]==NULL,
                    STAI_ERROR_NETWORK_INVALID_IN_PTR, net_ctx->_return_code)
    _STAI_SET_ERROR(net_ctx, ((uintptr_t)inputs[idx]) & (_inputs_alignment[idx]-1),
                    STAI_ERROR_INVALID_BUFFER_ALIGNMENT, net_ctx->_return_code)
    net_ctx->_inputs[idx] = inputs[idx];
  }

  _stai_network_check(net_ctx);
  return net_ctx->_return_code;
}


STAI_API_ENTRY
stai_return_code stai_network_set_outputs(
  stai_network* network,
  const stai_ptr* outputs,
  const stai_size n_outputs)
{
  const uintptr_t _outputs_alignment[] = STAI_NETWORK_OUT_ALIGNMENTS;
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)
  _STAI_SET_ERROR(net_ctx, !outputs,
                  STAI_ERROR_NETWORK_INVALID_API_ARGUMENTS, net_ctx->_return_code)
  _STAI_SET_ERROR(net_ctx, n_outputs!=STAI_NETWORK_OUT_NUM,
                  STAI_ERROR_NETWORK_INVALID_OUT_NUM, net_ctx->_return_code)

  for (stai_size idx=0; outputs && idx<n_outputs; idx++) {
    STAI_PRINT("  output[%d]: %p\n", idx, outputs[idx])
    _STAI_SET_ERROR(net_ctx, outputs[idx]==NULL,
                    STAI_ERROR_NETWORK_INVALID_OUT_PTR, net_ctx->_return_code)
    _STAI_SET_ERROR(net_ctx, ((uintptr_t)outputs[idx]) & (_outputs_alignment[idx]-1),
                    STAI_ERROR_INVALID_BUFFER_ALIGNMENT, net_ctx->_return_code)
    net_ctx->_outputs[idx] = outputs[idx];
  }

  _stai_network_check(net_ctx);
  return net_ctx->_return_code;
}


STAI_API_ENTRY
stai_return_code stai_network_set_states(
  stai_network* network,
  const stai_ptr* states,
  const stai_size n_states)
{
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)

  STAI_UNUSED(states)
  STAI_UNUSED(n_states)
_stai_network_check(net_ctx);
  return net_ctx->_return_code;
}

STAI_API_ENTRY
stai_return_code stai_network_set_callback(
  stai_network* network, const stai_event_cb cb, void* cb_cookie)
{
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)
  STAI_PRINT("  set_callback %p cb %p cookie %p\n", net_ctx, cb, cb_cookie)
  // _STAI_SET_ERROR(net_ctx, cb==NULL, STAI_ERROR_NETWORK_INVALID_CALLBACK, net_ctx->_return_code)
  net_ctx->_callback = cb;
  net_ctx->_callback_cookie = cb_cookie;
  return net_ctx->_return_code;
}

#undef _STAI_SET_ERROR
#undef _STAI_CONTEXT_ALIGNMENT
#undef _STAI_CONTEXT_ACQUIRE
#undef _STAI_NETWORK_EVENT_NODE_START_CB
#undef _STAI_NETWORK_EVENT_NODE_STOP_CB
#undef _STAI_NETWORK_MODEL_SIGNATURE
#undef _STAI_NETWORK_DATETIME
#undef _STAI_NETWORK_COMPILE_DATETIME

