/**************************************************************************
 *
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 **************************************************************************/

// AMD VCN 디코더 메시지 정의 중 VP9 디코드에 필요한 부분만 Mesa에서 그대로 옮긴 헤더.
//
// 출처: Mesa 태그 mesa-25.0.0 (커밋 4fa244fddfebb21378042556862e197284ef65ac)
//   src/amd/common/ac_vcn_dec.h                     상수와 구조체 전부 (아래 "ac_vcn_dec.h에서" 구역)
//   src/gallium/drivers/radeonsi/radeon_vcn_dec.c   버퍼 배치 상수 (아래쪽 구역, radeon_vcn_dec.h의 NUM_BUFFERS 포함)
// 구조체와 상수는 원본 줄을 기계적으로 잘라 붙였고 손으로 고치지 않았다. 원본 저작권 고지(위, MIT)를 유지한다.
// 크기와 오프셋은 Mesa 원본 헤더를 직접 컴파일해 sizeof와 offsetof로 얻은 값을 _Static_assert로 고정했다.
// 모든 필드는 리틀엔디언이고 unsigned int는 32비트라고 가정한다(x86_64와 arm64 macOS 모두 해당).

#ifndef VCN_VP9_H
#define VCN_VP9_H

#include <stddef.h>

/* ===== ac_vcn_dec.h에서 ===== */

#define RDECODE_IB_PARAM_DECODE_BUFFER                               (0x00000001)
#define RDECODE_IB_PARAM_QUERY_BUFFER                                (0x00000002)
#define RDECODE_IB_PARAM_PREDICATION_BUFFER                          (0x00000003)
#define RDECODE_IB_PARAM_UMD_64BIT_FENCE                             (0x00000005)
#define RDECODE_IB_PARAM_UMD_RECORD_TIMESTAMP                        (0x00000006)
#define RDECODE_IB_PARAM_UMD_REPORT_EVENT_STATUS                     (0x00000007)
#define RDECODE_IB_PARAM_UMD_COPY_MEMORY                             (0x00000008)
#define RDECODE_IB_PARAM_UMD_WRITE_MEMORY                            (0x00000009)
#define RDECODE_IB_PARAM_FEEDBACK_BUFFER                             (0x0000000A)

#define RDECODE_CMDBUF_FLAGS_MSG_BUFFER                              (0x00000001)
#define RDECODE_CMDBUF_FLAGS_DPB_BUFFER                              (0x00000002)
#define RDECODE_CMDBUF_FLAGS_BITSTREAM_BUFFER                        (0x00000004)
#define RDECODE_CMDBUF_FLAGS_DECODING_TARGET_BUFFER                  (0x00000008)
#define RDECODE_CMDBUF_FLAGS_FEEDBACK_BUFFER                         (0x00000010)
#define RDECODE_CMDBUF_FLAGS_PICTURE_PARAM_BUFFER                    (0x00000020)
#define RDECODE_CMDBUF_FLAGS_MB_CONTROL_BUFFER                       (0x00000040)
#define RDECODE_CMDBUF_FLAGS_IDCT_COEF_BUFFER                        (0x00000080)
#define RDECODE_CMDBUF_FLAGS_PREEMPT_BUFFER                          (0x00000100)
#define RDECODE_CMDBUF_FLAGS_IT_SCALING_BUFFER                       (0x00000200)
#define RDECODE_CMDBUF_FLAGS_SCALER_TARGET_BUFFER                    (0x00000400)
#define RDECODE_CMDBUF_FLAGS_CONTEXT_BUFFER                          (0x00000800)
#define RDECODE_CMDBUF_FLAGS_PROB_TBL_BUFFER                         (0x00001000)
#define RDECODE_CMDBUF_FLAGS_QUERY_BUFFER                            (0x00002000)
#define RDECODE_CMDBUF_FLAGS_PREDICATION_BUFFER                      (0x00004000)
#define RDECODE_CMDBUF_FLAGS_SCLR_COEF_BUFFER                        (0x00008000)
#define RDECODE_CMDBUF_FLAGS_RECORD_TIMESTAMP                        (0x00010000)
#define RDECODE_CMDBUF_FLAGS_REPORT_EVENT_STATUS                     (0x00020000)
#define RDECODE_CMDBUF_FLAGS_RESERVED_SIZE_INFO_BUFFER               (0x00040000)
#define RDECODE_CMDBUF_FLAGS_LUMA_HIST_BUFFER                        (0x00080000)
#define RDECODE_CMDBUF_FLAGS_SESSION_CONTEXT_BUFFER                  (0x00100000)

#define RDECODE_CMD_MSG_BUFFER                              0x00000000
#define RDECODE_CMD_DPB_BUFFER                              0x00000001
#define RDECODE_CMD_DECODING_TARGET_BUFFER                  0x00000002
#define RDECODE_CMD_FEEDBACK_BUFFER                         0x00000003
#define RDECODE_CMD_PROB_TBL_BUFFER                         0x00000004
#define RDECODE_CMD_SESSION_CONTEXT_BUFFER                  0x00000005
#define RDECODE_CMD_BITSTREAM_BUFFER                        0x00000100
#define RDECODE_CMD_IT_SCALING_TABLE_BUFFER                 0x00000204
#define RDECODE_CMD_CONTEXT_BUFFER                          0x00000206
#define RDECODE_CMD_WRITE_MEMORY                            0x00000800

#define RDECODE_MSG_CREATE                                  0x00000000
#define RDECODE_MSG_DECODE                                  0x00000001
#define RDECODE_MSG_DESTROY                                 0x00000002

#define RDECODE_CODEC_H264                                  0x00000000
#define RDECODE_CODEC_VC1                                   0x00000001
#define RDECODE_CODEC_MPEG2_VLD                             0x00000003
#define RDECODE_CODEC_MPEG4                                 0x00000004
#define RDECODE_CODEC_H264_PERF                             0x00000007
#define RDECODE_CODEC_JPEG                                  0x00000008
#define RDECODE_CODEC_H265                                  0x00000010
#define RDECODE_CODEC_VP9                                   0x00000011
#define RDECODE_CODEC_AV1                                   0x00000013
#define RDECODE_MESSAGE_HEVC_DIRECT_REF_LIST                0x00000015

#define RDECODE_ARRAY_MODE_LINEAR                           0x00000000
#define RDECODE_ARRAY_MODE_MACRO_LINEAR_MICRO_TILED         0x00000001
#define RDECODE_ARRAY_MODE_1D_THIN                          0x00000002
#define RDECODE_ARRAY_MODE_2D_THIN                          0x00000004
#define RDECODE_ARRAY_MODE_MACRO_TILED_MICRO_LINEAR         0x00000004
#define RDECODE_ARRAY_MODE_MACRO_TILED_MICRO_TILED          0x00000005

#define RDECODE_ARRAY_MODE_ADDRLIB_SEL_GFX10                0x00000000
#define RDECODE_ARRAY_MODE_ADDRLIB_SEL_GFX9                 0x00000001
#define RDECODE_ARRAY_MODE_ADDRLIB_SEL_GFX8                 0x00000002
#define RDECODE_ARRAY_MODE_ADDRLIB_SEL_GFX11                0x00000003

#define RDECODE_SW_MODE_LINEAR                              0x00000000
/* for legacy VCN generations */
#define RDECODE_256B_S                                      0x00000001
#define RDECODE_256B_D                                      0x00000002
#define RDECODE_4KB_S                                       0x00000005
#define RDECODE_4KB_D                                       0x00000006
#define RDECODE_64KB_S                                      0x00000009
#define RDECODE_64KB_D                                      0x0000000A
#define RDECODE_4KB_S_X                                     0x00000015
#define RDECODE_4KB_D_X                                     0x00000016
#define RDECODE_64KB_S_X                                    0x00000019
#define RDECODE_64KB_D_X                                    0x0000001A
/* for VCN5 */
#define RDECODE_VCN5_256B_D                                 0x00000001

#define RDECODE_MESSAGE_NOT_SUPPORTED                       0x00000000
#define RDECODE_MESSAGE_CREATE                              0x00000001
#define RDECODE_MESSAGE_DECODE                              0x00000002
#define RDECODE_MESSAGE_DRM                                 0x00000003
#define RDECODE_MESSAGE_AVC                                 0x00000006
#define RDECODE_MESSAGE_VC1                                 0x00000007
#define RDECODE_MESSAGE_MPEG2_VLD                           0x0000000A
#define RDECODE_MESSAGE_MPEG4_ASP_VLD                       0x0000000B
#define RDECODE_MESSAGE_HEVC                                0x0000000D
#define RDECODE_MESSAGE_VP9                                 0x0000000E
#define RDECODE_MESSAGE_DYNAMIC_DPB                         0x00000010
#define RDECODE_MESSAGE_AV1                                 0x00000011

#define RDECODE_FEEDBACK_PROFILING                          0x00000001

#define RDECODE_VP9_PROBS_DATA_SIZE                         2304

/* *** decode flags *** */
#define RDECODE_FLAGS_USE_DYNAMIC_DPB_MASK                  0x00000001
#define RDECODE_FLAGS_USE_PAL_MASK                          0x00000008
#define RDECODE_FLAGS_DPB_RESIZE_MASK                       0x00000100

/* VP9 Frame header flags */
#define RDECODE_FRAME_HDR_INFO_VP9_USE_UNCOMPRESSED_HEADER_SHIFT      (14)
#define RDECODE_FRAME_HDR_INFO_VP9_USE_PREV_IN_FIND_MV_REFS_SHIFT     (13)
#define RDECODE_FRAME_HDR_INFO_VP9_MODE_REF_DELTA_UPDATE_SHIFT        (12)
#define RDECODE_FRAME_HDR_INFO_VP9_MODE_REF_DELTA_ENABLED_SHIFT       (11)
#define RDECODE_FRAME_HDR_INFO_VP9_SEGMENTATION_UPDATE_DATA_SHIFT     (10)
#define RDECODE_FRAME_HDR_INFO_VP9_SEGMENTATION_TEMPORAL_UPDATE_SHIFT (9)
#define RDECODE_FRAME_HDR_INFO_VP9_SEGMENTATION_UPDATE_MAP_SHIFT      (8)
#define RDECODE_FRAME_HDR_INFO_VP9_SEGMENTATION_ENABLED_SHIFT         (7)
#define RDECODE_FRAME_HDR_INFO_VP9_FRAME_PARALLEL_DECODING_MODE_SHIFT (6)
#define RDECODE_FRAME_HDR_INFO_VP9_REFRESH_FRAME_CONTEXT_SHIFT        (5)
#define RDECODE_FRAME_HDR_INFO_VP9_ALLOW_HIGH_PRECISION_MV_SHIFT      (4)
#define RDECODE_FRAME_HDR_INFO_VP9_INTRA_ONLY_SHIFT                   (3)
#define RDECODE_FRAME_HDR_INFO_VP9_ERROR_RESILIENT_MODE_SHIFT         (2)
#define RDECODE_FRAME_HDR_INFO_VP9_FRAME_TYPE_SHIFT                   (1)
#define RDECODE_FRAME_HDR_INFO_VP9_SHOW_EXISTING_FRAME_SHIFT          (0)


#define RDECODE_FRAME_HDR_INFO_VP9_USE_UNCOMPRESSED_HEADER_MASK      (0x00004000)
#define RDECODE_FRAME_HDR_INFO_VP9_USE_PREV_IN_FIND_MV_REFS_MASK     (0x00002000)
#define RDECODE_FRAME_HDR_INFO_VP9_MODE_REF_DELTA_UPDATE_MASK        (0x00001000)
#define RDECODE_FRAME_HDR_INFO_VP9_MODE_REF_DELTA_ENABLED_MASK       (0x00000800)
#define RDECODE_FRAME_HDR_INFO_VP9_SEGMENTATION_UPDATE_DATA_MASK     (0x00000400)
#define RDECODE_FRAME_HDR_INFO_VP9_SEGMENTATION_TEMPORAL_UPDATE_MASK (0x00000200)
#define RDECODE_FRAME_HDR_INFO_VP9_SEGMENTATION_UPDATE_MAP_MASK      (0x00000100)
#define RDECODE_FRAME_HDR_INFO_VP9_SEGMENTATION_ENABLED_MASK         (0x00000080)
#define RDECODE_FRAME_HDR_INFO_VP9_FRAME_PARALLEL_DECODING_MODE_MASK (0x00000040)
#define RDECODE_FRAME_HDR_INFO_VP9_REFRESH_FRAME_CONTEXT_MASK        (0x00000020)
#define RDECODE_FRAME_HDR_INFO_VP9_ALLOW_HIGH_PRECISION_MV_MASK      (0x00000010)
#define RDECODE_FRAME_HDR_INFO_VP9_INTRA_ONLY_MASK                   (0x00000008)
#define RDECODE_FRAME_HDR_INFO_VP9_ERROR_RESILIENT_MODE_MASK         (0x00000004)
#define RDECODE_FRAME_HDR_INFO_VP9_FRAME_TYPE_MASK                   (0x00000002)
#define RDECODE_FRAME_HDR_INFO_VP9_SHOW_EXISTING_FRAME_MASK          (0x00000001)

typedef struct rvcn_dec_message_index_s {
   unsigned int message_id;
   unsigned int offset;
   unsigned int size;
   unsigned int filled;
} rvcn_dec_message_index_t;

typedef struct rvcn_dec_message_header_s {
   unsigned int header_size;
   unsigned int total_size;
   unsigned int num_buffers;
   unsigned int msg_type;
   unsigned int stream_handle;
   unsigned int status_report_feedback_number;

   rvcn_dec_message_index_t index[1];
} rvcn_dec_message_header_t;

typedef struct rvcn_dec_message_create_s {
   unsigned int stream_type;
   unsigned int session_flags;
   unsigned int width_in_samples;
   unsigned int height_in_samples;
} rvcn_dec_message_create_t;

typedef struct rvcn_dec_message_decode_s {
   unsigned int stream_type;
   unsigned int decode_flags;
   unsigned int width_in_samples;
   unsigned int height_in_samples;

   unsigned int bsd_size;
   unsigned int dpb_size;
   unsigned int dt_size;
   unsigned int sct_size;
   unsigned int sc_coeff_size;
   unsigned int hw_ctxt_size;
   unsigned int sw_ctxt_size;
   unsigned int pic_param_size;
   unsigned int mb_cntl_size;
   unsigned int reserved0[4];
   unsigned int decode_buffer_flags;

   unsigned int db_pitch;
   unsigned int db_aligned_height;
   unsigned int db_tiling_mode;
   unsigned int db_swizzle_mode;
   unsigned int db_array_mode;
   unsigned int db_field_mode;
   unsigned int db_surf_tile_config;

   unsigned int dt_pitch;
   unsigned int dt_uv_pitch;
   unsigned int dt_tiling_mode;
   unsigned int dt_swizzle_mode;
   unsigned int dt_array_mode;
   unsigned int dt_field_mode;
   unsigned int dt_out_format;
   unsigned int dt_surf_tile_config;
   unsigned int dt_uv_surf_tile_config;
   unsigned int dt_luma_top_offset;
   unsigned int dt_luma_bottom_offset;
   unsigned int dt_chroma_top_offset;
   unsigned int dt_chroma_bottom_offset;
   unsigned int dt_chromaV_top_offset;
   unsigned int dt_chromaV_bottom_offset;

   unsigned int mif_wrc_en;
   unsigned int db_pitch_uv;

   unsigned char reserved1[20];
} rvcn_dec_message_decode_t;

typedef struct rvcn_dec_message_drm_s {
   unsigned int	drm_key[4];
   unsigned int	drm_counter[4];
   unsigned int	drm_wrapped_key[4];
   unsigned int	drm_offset;
   unsigned int	drm_cmd;
   unsigned int	drm_cntl;
   unsigned int	drm_reserved;
} rvcn_dec_message_drm_t;

typedef struct rvcn_dec_message_dynamic_dpb_s {
   unsigned int dpbConfigFlags;
   unsigned int dpbLumaPitch;
   unsigned int dpbLumaAlignedHeight;
   unsigned int dpbLumaAlignedSize;
   unsigned int dpbChromaPitch;
   unsigned int dpbChromaAlignedHeight;
   unsigned int dpbChromaAlignedSize;

   unsigned char dpbArraySize;
   unsigned char dpbCurArraySlice;
   unsigned char dpbRefArraySlice[16];
   unsigned char dpbReserved0[2];

   unsigned int dpbCurrOffset;
   unsigned int dpbAddrOffset[16];
} rvcn_dec_message_dynamic_dpb_t;

typedef struct rvcn_dec_message_dynamic_dpb_t2_s {
    unsigned int dpbConfigFlags;
    unsigned int dpbLumaPitch;
    unsigned int dpbLumaAlignedHeight;
    unsigned int dpbLumaAlignedSize;
    unsigned int dpbChromaPitch;
    unsigned int dpbChromaAlignedHeight;
    unsigned int dpbChromaAlignedSize;
    unsigned int dpbArraySize;

    unsigned int dpbCurrLo;
    unsigned int dpbCurrHi;
    unsigned int dpbAddrLo[16];
    unsigned int dpbAddrHi[16];
} rvcn_dec_message_dynamic_dpb_t2_t;

typedef struct rvcn_dec_message_vp9_s {
   unsigned int frame_header_flags;

   unsigned char frame_context_idx;
   unsigned char reset_frame_context;

   unsigned char curr_pic_idx;
   unsigned char interp_filter;

   unsigned char filter_level;
   unsigned char sharpness_level;
   unsigned char lf_adj_level[8][4][2];
   unsigned char base_qindex;
   signed char y_dc_delta_q;
   signed char uv_ac_delta_q;
   signed char uv_dc_delta_q;

   unsigned char log2_tile_cols;
   unsigned char log2_tile_rows;
   unsigned char tx_mode;
   unsigned char reference_mode;
   unsigned char chroma_format;

   unsigned char ref_frame_map[8];

   unsigned char frame_refs[3];
   unsigned char ref_frame_sign_bias[3];
   unsigned char frame_to_show;
   unsigned char bit_depth_luma_minus8;
   unsigned char bit_depth_chroma_minus8;

   unsigned char p010_mode;
   unsigned char msb_mode;
   unsigned char luma_10to8;
   unsigned char chroma_10to8;

   unsigned int vp9_frame_size;
   unsigned int compressed_header_size;
   unsigned int uncompressed_header_size;
   unsigned char reserved[2];
} rvcn_dec_message_vp9_t;

typedef struct rvcn_dec_feature_index_s {
   unsigned int feature_id;
   unsigned int offset;
   unsigned int size;
   unsigned int filled;
} rvcn_dec_feature_index_t;

typedef struct rvcn_dec_feedback_header_s {
   unsigned int header_size;
   unsigned int total_size;
   unsigned int num_buffers;
   unsigned int status_report_feedback_number;
   unsigned int status;
   unsigned int value;
   unsigned int errorBits;
   rvcn_dec_feature_index_t index[1];
} rvcn_dec_feedback_header_t;

typedef struct rvcn_dec_vp9_nmv_ctx_mask_s {
   unsigned short classes_mask[2];
   unsigned short bits_mask[2];
   unsigned char joints_mask;
   unsigned char sign_mask[2];
   unsigned char class0_mask[2];
   unsigned char class0_fp_mask[2];
   unsigned char fp_mask[2];
   unsigned char class0_hp_mask[2];
   unsigned char hp_mask[2];
   unsigned char reserve[11];
} rvcn_dec_vp9_nmv_ctx_mask_t;

typedef struct rvcn_dec_vp9_nmv_component_s {
   unsigned char sign;
   unsigned char classes[10];
   unsigned char class0[1];
   unsigned char bits[10];
   unsigned char class0_fp[2][3];
   unsigned char fp[3];
   unsigned char class0_hp;
   unsigned char hp;
} rvcn_dec_vp9_nmv_component_t;

typedef struct rvcn_dec_vp9_probs_s {
   rvcn_dec_vp9_nmv_ctx_mask_t nmvc_mask;
   unsigned char coef_probs[4][2][2][6][6][3];
   unsigned char y_mode_prob[4][9];
   unsigned char uv_mode_prob[10][9];
   unsigned char single_ref_prob[5][2];
   unsigned char switchable_interp_prob[4][2];
   unsigned char partition_prob[16][3];
   unsigned char inter_mode_probs[7][3];
   unsigned char mbskip_probs[3];
   unsigned char intra_inter_prob[4];
   unsigned char comp_inter_prob[5];
   unsigned char comp_ref_prob[5];
   unsigned char tx_probs_32x32[2][3];
   unsigned char tx_probs_16x16[2][2];
   unsigned char tx_probs_8x8[2][1];
   unsigned char mv_joints[3];
   rvcn_dec_vp9_nmv_component_t mv_comps[2];
} rvcn_dec_vp9_probs_t;

typedef struct rvcn_dec_vp9_probs_segment_s {
   union {
      rvcn_dec_vp9_probs_t probs;
      unsigned char probs_data[RDECODE_VP9_PROBS_DATA_SIZE];
   };

   union {
      struct {
         unsigned int feature_data[8];
         unsigned char tree_probs[7];
         unsigned char pred_probs[3];
         unsigned char abs_delta;
         unsigned char feature_mask[8];
      } seg;
      unsigned char segment_data[256];
   };
} rvcn_dec_vp9_probs_segment_t;

#define RDECODE_SESSION_CONTEXT_SIZE (128 * 1024)

/* ===== radeon_vcn_dec.c, radeon_vcn_dec.h에서 (버퍼 배치 상수) ===== */

/* 메시지, 피드백, 확률 표는 한 버퍼를 나눠 쓴다: [0, 0x2000) 메시지, [0x2000, 0x2800) 피드백, [0x2800, 0x3200) 확률 표 */
#define FB_BUFFER_OFFSET             0x2000
#define FB_BUFFER_SIZE               2048
#define VP9_PROBS_TABLE_SIZE         (RDECODE_VP9_PROBS_DATA_SIZE + 256)
#define NUM_VP9_REFS   8
#define NUM_BUFFERS                                         4

/* ===== 여기부터는 Mesa에 없는 검증 코드 ===== */
/* Mesa 원본 ac_vcn_dec.h(mesa-25.0.0)를 컴파일해 sizeof와 offsetof로 뽑은 값 */
_Static_assert(sizeof(rvcn_dec_message_index_t) == 16, "rvcn_dec_message_index_t");
_Static_assert(sizeof(rvcn_dec_message_header_t) == 40, "rvcn_dec_message_header_t");
_Static_assert(offsetof(rvcn_dec_message_header_t, msg_type) == 12, "rvcn_dec_message_header_t.msg_type");
_Static_assert(offsetof(rvcn_dec_message_header_t, stream_handle) == 16, "rvcn_dec_message_header_t.stream_handle");
_Static_assert(offsetof(rvcn_dec_message_header_t, status_report_feedback_number) == 20, "rvcn_dec_message_header_t.status_report_feedback_number");
_Static_assert(offsetof(rvcn_dec_message_header_t, index) == 24, "rvcn_dec_message_header_t.index");
_Static_assert(sizeof(rvcn_dec_message_create_t) == 16, "rvcn_dec_message_create_t");
_Static_assert(sizeof(rvcn_dec_message_decode_t) == 188, "rvcn_dec_message_decode_t");
_Static_assert(offsetof(rvcn_dec_message_decode_t, bsd_size) == 16, "rvcn_dec_message_decode_t.bsd_size");
_Static_assert(offsetof(rvcn_dec_message_decode_t, dpb_size) == 20, "rvcn_dec_message_decode_t.dpb_size");
_Static_assert(offsetof(rvcn_dec_message_decode_t, dt_size) == 24, "rvcn_dec_message_decode_t.dt_size");
_Static_assert(offsetof(rvcn_dec_message_decode_t, hw_ctxt_size) == 36, "rvcn_dec_message_decode_t.hw_ctxt_size");
_Static_assert(offsetof(rvcn_dec_message_decode_t, sw_ctxt_size) == 40, "rvcn_dec_message_decode_t.sw_ctxt_size");
_Static_assert(offsetof(rvcn_dec_message_decode_t, decode_buffer_flags) == 68, "rvcn_dec_message_decode_t.decode_buffer_flags");
_Static_assert(offsetof(rvcn_dec_message_decode_t, db_pitch) == 72, "rvcn_dec_message_decode_t.db_pitch");
_Static_assert(offsetof(rvcn_dec_message_decode_t, db_aligned_height) == 76, "rvcn_dec_message_decode_t.db_aligned_height");
_Static_assert(offsetof(rvcn_dec_message_decode_t, db_swizzle_mode) == 84, "rvcn_dec_message_decode_t.db_swizzle_mode");
_Static_assert(offsetof(rvcn_dec_message_decode_t, db_array_mode) == 88, "rvcn_dec_message_decode_t.db_array_mode");
_Static_assert(offsetof(rvcn_dec_message_decode_t, dt_pitch) == 100, "rvcn_dec_message_decode_t.dt_pitch");
_Static_assert(offsetof(rvcn_dec_message_decode_t, dt_uv_pitch) == 104, "rvcn_dec_message_decode_t.dt_uv_pitch");
_Static_assert(offsetof(rvcn_dec_message_decode_t, dt_swizzle_mode) == 112, "rvcn_dec_message_decode_t.dt_swizzle_mode");
_Static_assert(offsetof(rvcn_dec_message_decode_t, dt_array_mode) == 116, "rvcn_dec_message_decode_t.dt_array_mode");
_Static_assert(offsetof(rvcn_dec_message_decode_t, dt_luma_top_offset) == 136, "rvcn_dec_message_decode_t.dt_luma_top_offset");
_Static_assert(offsetof(rvcn_dec_message_decode_t, dt_chroma_top_offset) == 144, "rvcn_dec_message_decode_t.dt_chroma_top_offset");
_Static_assert(offsetof(rvcn_dec_message_decode_t, mif_wrc_en) == 160, "rvcn_dec_message_decode_t.mif_wrc_en");
_Static_assert(offsetof(rvcn_dec_message_decode_t, db_pitch_uv) == 164, "rvcn_dec_message_decode_t.db_pitch_uv");
_Static_assert(offsetof(rvcn_dec_message_decode_t, reserved1) == 168, "rvcn_dec_message_decode_t.reserved1");
_Static_assert(sizeof(rvcn_dec_message_drm_t) == 64, "rvcn_dec_message_drm_t");
_Static_assert(sizeof(rvcn_dec_message_dynamic_dpb_t) == 116, "rvcn_dec_message_dynamic_dpb_t");
_Static_assert(offsetof(rvcn_dec_message_dynamic_dpb_t, dpbArraySize) == 28, "rvcn_dec_message_dynamic_dpb_t.dpbArraySize");
_Static_assert(offsetof(rvcn_dec_message_dynamic_dpb_t, dpbCurrOffset) == 48, "rvcn_dec_message_dynamic_dpb_t.dpbCurrOffset");
_Static_assert(sizeof(rvcn_dec_message_dynamic_dpb_t2_t) == 168, "rvcn_dec_message_dynamic_dpb_t2_t");
_Static_assert(offsetof(rvcn_dec_message_dynamic_dpb_t2_t, dpbArraySize) == 28, "rvcn_dec_message_dynamic_dpb_t2_t.dpbArraySize");
_Static_assert(offsetof(rvcn_dec_message_dynamic_dpb_t2_t, dpbCurrLo) == 32, "rvcn_dec_message_dynamic_dpb_t2_t.dpbCurrLo");
_Static_assert(offsetof(rvcn_dec_message_dynamic_dpb_t2_t, dpbAddrLo) == 40, "rvcn_dec_message_dynamic_dpb_t2_t.dpbAddrLo");
_Static_assert(offsetof(rvcn_dec_message_dynamic_dpb_t2_t, dpbAddrHi) == 104, "rvcn_dec_message_dynamic_dpb_t2_t.dpbAddrHi");
_Static_assert(sizeof(rvcn_dec_message_vp9_t) == 120, "rvcn_dec_message_vp9_t");
_Static_assert(offsetof(rvcn_dec_message_vp9_t, frame_context_idx) == 4, "rvcn_dec_message_vp9_t.frame_context_idx");
_Static_assert(offsetof(rvcn_dec_message_vp9_t, curr_pic_idx) == 6, "rvcn_dec_message_vp9_t.curr_pic_idx");
_Static_assert(offsetof(rvcn_dec_message_vp9_t, interp_filter) == 7, "rvcn_dec_message_vp9_t.interp_filter");
_Static_assert(offsetof(rvcn_dec_message_vp9_t, filter_level) == 8, "rvcn_dec_message_vp9_t.filter_level");
_Static_assert(offsetof(rvcn_dec_message_vp9_t, lf_adj_level) == 10, "rvcn_dec_message_vp9_t.lf_adj_level");
_Static_assert(offsetof(rvcn_dec_message_vp9_t, base_qindex) == 74, "rvcn_dec_message_vp9_t.base_qindex");
_Static_assert(offsetof(rvcn_dec_message_vp9_t, y_dc_delta_q) == 75, "rvcn_dec_message_vp9_t.y_dc_delta_q");
_Static_assert(offsetof(rvcn_dec_message_vp9_t, log2_tile_cols) == 78, "rvcn_dec_message_vp9_t.log2_tile_cols");
_Static_assert(offsetof(rvcn_dec_message_vp9_t, tx_mode) == 80, "rvcn_dec_message_vp9_t.tx_mode");
_Static_assert(offsetof(rvcn_dec_message_vp9_t, chroma_format) == 82, "rvcn_dec_message_vp9_t.chroma_format");
_Static_assert(offsetof(rvcn_dec_message_vp9_t, ref_frame_map) == 83, "rvcn_dec_message_vp9_t.ref_frame_map");
_Static_assert(offsetof(rvcn_dec_message_vp9_t, frame_refs) == 91, "rvcn_dec_message_vp9_t.frame_refs");
_Static_assert(offsetof(rvcn_dec_message_vp9_t, ref_frame_sign_bias) == 94, "rvcn_dec_message_vp9_t.ref_frame_sign_bias");
_Static_assert(offsetof(rvcn_dec_message_vp9_t, frame_to_show) == 97, "rvcn_dec_message_vp9_t.frame_to_show");
_Static_assert(offsetof(rvcn_dec_message_vp9_t, bit_depth_luma_minus8) == 98, "rvcn_dec_message_vp9_t.bit_depth_luma_minus8");
_Static_assert(offsetof(rvcn_dec_message_vp9_t, p010_mode) == 100, "rvcn_dec_message_vp9_t.p010_mode");
_Static_assert(offsetof(rvcn_dec_message_vp9_t, luma_10to8) == 102, "rvcn_dec_message_vp9_t.luma_10to8");
_Static_assert(offsetof(rvcn_dec_message_vp9_t, vp9_frame_size) == 104, "rvcn_dec_message_vp9_t.vp9_frame_size");
_Static_assert(offsetof(rvcn_dec_message_vp9_t, compressed_header_size) == 108, "rvcn_dec_message_vp9_t.compressed_header_size");
_Static_assert(offsetof(rvcn_dec_message_vp9_t, uncompressed_header_size) == 112, "rvcn_dec_message_vp9_t.uncompressed_header_size");
_Static_assert(offsetof(rvcn_dec_message_vp9_t, reserved) == 116, "rvcn_dec_message_vp9_t.reserved");
_Static_assert(sizeof(rvcn_dec_feedback_header_t) == 44, "rvcn_dec_feedback_header_t");
_Static_assert(offsetof(rvcn_dec_feedback_header_t, status) == 16, "rvcn_dec_feedback_header_t.status");
_Static_assert(offsetof(rvcn_dec_feedback_header_t, errorBits) == 24, "rvcn_dec_feedback_header_t.errorBits");
_Static_assert(offsetof(rvcn_dec_feedback_header_t, index) == 28, "rvcn_dec_feedback_header_t.index");
_Static_assert(sizeof(rvcn_dec_vp9_nmv_ctx_mask_t) == 32, "rvcn_dec_vp9_nmv_ctx_mask_t");
_Static_assert(sizeof(rvcn_dec_vp9_nmv_component_t) == 33, "rvcn_dec_vp9_nmv_component_t");
_Static_assert(sizeof(rvcn_dec_vp9_probs_t) == 2072, "rvcn_dec_vp9_probs_t");
_Static_assert(offsetof(rvcn_dec_vp9_probs_t, coef_probs) == 32, "rvcn_dec_vp9_probs_t.coef_probs");
_Static_assert(offsetof(rvcn_dec_vp9_probs_t, y_mode_prob) == 1760, "rvcn_dec_vp9_probs_t.y_mode_prob");
_Static_assert(offsetof(rvcn_dec_vp9_probs_t, uv_mode_prob) == 1796, "rvcn_dec_vp9_probs_t.uv_mode_prob");
_Static_assert(offsetof(rvcn_dec_vp9_probs_t, single_ref_prob) == 1886, "rvcn_dec_vp9_probs_t.single_ref_prob");
_Static_assert(offsetof(rvcn_dec_vp9_probs_t, switchable_interp_prob) == 1896, "rvcn_dec_vp9_probs_t.switchable_interp_prob");
_Static_assert(offsetof(rvcn_dec_vp9_probs_t, partition_prob) == 1904, "rvcn_dec_vp9_probs_t.partition_prob");
_Static_assert(offsetof(rvcn_dec_vp9_probs_t, inter_mode_probs) == 1952, "rvcn_dec_vp9_probs_t.inter_mode_probs");
_Static_assert(offsetof(rvcn_dec_vp9_probs_t, mbskip_probs) == 1973, "rvcn_dec_vp9_probs_t.mbskip_probs");
_Static_assert(offsetof(rvcn_dec_vp9_probs_t, intra_inter_prob) == 1976, "rvcn_dec_vp9_probs_t.intra_inter_prob");
_Static_assert(offsetof(rvcn_dec_vp9_probs_t, comp_inter_prob) == 1980, "rvcn_dec_vp9_probs_t.comp_inter_prob");
_Static_assert(offsetof(rvcn_dec_vp9_probs_t, comp_ref_prob) == 1985, "rvcn_dec_vp9_probs_t.comp_ref_prob");
_Static_assert(offsetof(rvcn_dec_vp9_probs_t, tx_probs_32x32) == 1990, "rvcn_dec_vp9_probs_t.tx_probs_32x32");
_Static_assert(offsetof(rvcn_dec_vp9_probs_t, tx_probs_16x16) == 1996, "rvcn_dec_vp9_probs_t.tx_probs_16x16");
_Static_assert(offsetof(rvcn_dec_vp9_probs_t, tx_probs_8x8) == 2000, "rvcn_dec_vp9_probs_t.tx_probs_8x8");
_Static_assert(offsetof(rvcn_dec_vp9_probs_t, mv_joints) == 2002, "rvcn_dec_vp9_probs_t.mv_joints");
_Static_assert(offsetof(rvcn_dec_vp9_probs_t, mv_comps) == 2005, "rvcn_dec_vp9_probs_t.mv_comps");
_Static_assert(sizeof(rvcn_dec_vp9_probs_segment_t) == 2560, "rvcn_dec_vp9_probs_segment_t");
_Static_assert(offsetof(rvcn_dec_vp9_probs_segment_t, probs_data) == 0, "rvcn_dec_vp9_probs_segment_t.probs_data");
_Static_assert(offsetof(rvcn_dec_vp9_probs_segment_t, segment_data) == 2304, "rvcn_dec_vp9_probs_segment_t.segment_data");
_Static_assert(offsetof(rvcn_dec_vp9_probs_segment_t, seg.feature_data) == 2304, "rvcn_dec_vp9_probs_segment_t.seg.feature_data");
_Static_assert(offsetof(rvcn_dec_vp9_probs_segment_t, seg.tree_probs) == 2336, "rvcn_dec_vp9_probs_segment_t.seg.tree_probs");
_Static_assert(offsetof(rvcn_dec_vp9_probs_segment_t, seg.pred_probs) == 2343, "rvcn_dec_vp9_probs_segment_t.seg.pred_probs");
_Static_assert(offsetof(rvcn_dec_vp9_probs_segment_t, seg.abs_delta) == 2346, "rvcn_dec_vp9_probs_segment_t.seg.abs_delta");
_Static_assert(offsetof(rvcn_dec_vp9_probs_segment_t, seg.feature_mask) == 2347, "rvcn_dec_vp9_probs_segment_t.seg.feature_mask");
_Static_assert(sizeof(unsigned int) == 4, "unsigned int must be 32 bits");
_Static_assert(VP9_PROBS_TABLE_SIZE == sizeof(rvcn_dec_vp9_probs_segment_t), "probs buffer size");

#endif /* VCN_VP9_H */
