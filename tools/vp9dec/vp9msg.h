// 파싱한 VP9 헤더(vp9hdr.h)에서 VCN 펌웨어용 rvcn_dec_message_vp9_t와 rvcn_dec_vp9_probs_segment_t(세그먼트 구역)를
// 채운다. Mesa 25.0.0 radeonsi의 get_vp9_msg()(src/gallium/drivers/radeonsi/radeon_vcn_dec.c)가
// ffmpeg의 VAAPI 매개변수(libavcodec/vaapi_vp9.c, vp9.c, n7.1)로 불렸을 때와 같은 값이 나오도록
// 값이 흘러가는 길 전체를 파서 값에서 직접 계산하는 식으로 옮겼다:
//
//   ffmpeg vp9.c(VP9SharedContext) -> vaapi_vp9.c(VADecPictureParameterBufferVP9, VASliceParameterBufferVP9)
//     -> Mesa frontends/va/picture_vp9.c(pipe_vp9_picture_desc; 일부 필드는 Mesa가 비압축 헤더를 직접 다시 파싱)
//     -> radeon_vcn_dec.c get_vp9_msg()
//
// ffmpeg와 Mesa는 "이번 프레임에 부호화되지 않은 필드"를 지우지 않고 이전 값을 그대로 둔다. 그 묵은 값까지
// 같게 하려고 Vp9MsgState에 두 쪽의 유지 상태를 따로 흉내 낸다. 규격상 쓰이지 않는 값들이라 디코드 결과에는
// 영향이 없어야 하지만, Linux에서 뜬 메시지 덤프와 바이트 단위로 견줄 수 있게 하려는 것이다.
//
// 일부러 Mesa와 다르게 한 곳(README의 "불확실한 점" 참고):
//   1. Mesa의 재파서는 profile 0의 intra_only 프레임에서 color_config 4비트를 잘못 읽어 그 뒤 필드
//      (델타, base_qindex, 세그먼트 값)가 틀어진다. 여기서는 규격대로 파싱한 값을 쓴다.
#pragma once
#include <string.h>
#include "vcn_vp9.h"
#include "vp9hdr.h"

// Mesa는 양자화 델타를 비트스트림 순서(y_dc, uv_dc, uv_ac)로 읽어 구조체 필드 순서(y_dc, uv_ac, uv_dc)에 넣는다.
// 그래서 메시지의 uv_ac_delta_q에는 규격의 delta_q_uv_dc가, uv_dc_delta_q에는 delta_q_uv_ac가 들어간다.
// 기본값 0은 Mesa와 같은 배치. 1로 두면 이름대로(규격 뜻대로) 넣는다. 실제 스트림에서는 둘 다 거의 항상 0이다.
#ifndef VP9MSG_UV_DELTA_BY_NAME
#define VP9MSG_UV_DELTA_BY_NAME 0
#endif

// ffmpeg는 이번 프레임에 부호화되지 않은 값을 지우지 않아서, 다음 묵은 값이 VAAPI를 거쳐 메시지까지 간다.
//   - update_map이 0인 프레임의 segmentation_temporal_update와 pred_probs(이전 프레임 값이 남는다)
//   - intra_only 프레임 바로 뒤 키 프레임의 intra_only 비트와 reset_frame_context
// 기본값 0은 ffmpeg + Mesa와 똑같이 묵은 값을 낸다. 1로 두면 이 세 가지를 규격 값(0, 255)으로 낸다
// (gstreamer처럼 규격대로 채우는 VAAPI 클라이언트가 Mesa에 주는 값에 해당).
#ifndef VP9MSG_SPEC_CLEAN
#define VP9MSG_SPEC_CLEAN 0
#endif

#define VP9MSG_NO_REF 0x7f  // 빈 참조 칸을 뜻하는 DPB 번호(Mesa와 같음)

typedef struct {
    // --- Mesa 디코더 객체(struct radeon_decoder)가 들고 있는 것 ---
    int show_frame;             // 직전 디코드 프레임의 show_frame(dec->show_frame)
    int last_width, last_height;
    // Mesa VA 프런트엔드의 재파서가 문맥에 남겨 두는 세그먼트 값(초기화나 리셋 없음)
    int mesa_abs_delta, mesa_alt_quant_enabled[8], mesa_alt_quant[8], mesa_alt_lf_enabled[8], mesa_alt_lf[8];
    // --- ffmpeg VP9SharedContext가 들고 있는 것 ---
    int ff_intraonly, ff_resetctx, ff_refidx[3], ff_signbias[3], ff_highprecisionmvs, ff_filtermode;
    int ff_seg_temporal, ff_seg_prob[7], ff_seg_pred_prob[3], ff_seg_absolute_vals;
    struct { int lf_enabled, lf_val, ref_enabled, ref_val, skip_enabled; unsigned char lflvl[4][2]; } ff_feat[8];
} Vp9MsgState;

static inline void vp9_msg_state_init(Vp9MsgState *st) { memset(st, 0, sizeof *st); }

static inline int vp9msg_clip_uintp2(int v, int bits) { int m = (1 << bits) - 1; return v < 0 ? 0 : v > m ? m : v; }

// 디코드할 프레임 하나(show_existing_frame이 아닌 것)의 VP9 메시지와 세그먼트 구역을 채운다.
//   st             프레임 사이 유지 상태. 디코드하는 프레임마다 순서대로 한 번씩 불러야 한다.
//                  show_existing_frame은 펌웨어에 보내지 않으므로(ffmpeg도 hwaccel을 부르지 않는다) 부르지 않는다.
//   h              vp9_parse_frame_header 결과
//   bs_size        펌웨어에 넘기는 비트스트림 바이트 수 = 프레임 전체(비압축 헤더 첫 바이트부터 마지막 타일 끝까지,
//                  슈퍼프레임 색인 제외). 비트스트림 버퍼에는 이 뒤를 128 배수까지 0으로 채워 둔다.
//   ref_frame_map  VP9 참조 슬롯 8개 각각이 들어 있는 DPB 칸 번호(이 프레임 디코드 전 상태). 빈 슬롯은 VP9MSG_NO_REF.
//   curr_pic_idx   이 프레임을 써 넣을 DPB 칸 번호. Mesa는 "아직 어느 슬롯에서도 참조되지 않는 가장 작은 번호"를 쓴다.
//   p010_output    profile 2에서 출력 표면이 P010이면 1, NV12(8비트로 내려 받기)이면 0. profile 0에서는 무시.
//   msg            RDECODE_MESSAGE_VP9 본문(120바이트)
//   prbs           확률 버퍼(2560바이트). 뒤쪽 256바이트(세그먼트 구역)만 쓴다. 앞 2304바이트는 vp9_init_probs_buffer가
//                  한 번 채워 둔 기본 확률을 그대로 둔다(Mesa도 프레임마다 다시 쓰지 않는다).
static inline void vp9_fill_msg(Vp9MsgState *st, const Vp9Frame *h, uint32_t bs_size, const uint8_t ref_frame_map[8],
                                uint8_t curr_pic_idx, int p010_output, rvcn_dec_message_vp9_t *msg,
                                rvcn_dec_vp9_probs_segment_t *prbs) {
    rvcn_dec_message_vp9_t result;
    int keyframe = h->frame_type == VP9_KEY_FRAME, i, j;
    memset(&result, 0, sizeof(result));

    // ===== ffmpeg decode_frame_header()의 상태 갱신 흉내 =====
    if (!keyframe) {
        st->ff_intraonly = h->intra_only;
        st->ff_resetctx = h->reset_frame_context;  // error_resilient이면 0
        if (!h->intra_only) {
            for (i = 0; i < 3; i++) {
                st->ff_refidx[i] = h->ref_frame_idx[i];
                st->ff_signbias[i] = h->ref_frame_sign_bias[i] && !h->error_resilient_mode;
            }
            st->ff_highprecisionmvs = h->allow_high_precision_mv;
            st->ff_filtermode = h->is_filter_switchable ? 4 : h->raw_interpolation_filter;
        }
    }
    if (keyframe || h->error_resilient_mode || st->ff_intraonly) memset(st->ff_feat, 0, sizeof st->ff_feat);
    if (h->segmentation_enabled) {
        if (h->segmentation_update_map) {
            for (i = 0; i < 7; i++) st->ff_seg_prob[i] = h->segmentation_tree_probs[i];
            st->ff_seg_temporal = h->segmentation_temporal_update;
            if (st->ff_seg_temporal) for (i = 0; i < 3; i++) st->ff_seg_pred_prob[i] = h->segmentation_pred_probs[i];
        }
        if (h->segmentation_update_data) {
            st->ff_seg_absolute_vals = h->segmentation_abs_or_delta_update;
            st->mesa_abs_delta = h->segmentation_abs_or_delta_update;
            for (i = 0; i < 8; i++) {
                if ((st->ff_feat[i].lf_enabled = h->feature_enabled[i][VP9_SEG_ALT_L])) st->ff_feat[i].lf_val = h->feature_data[i][VP9_SEG_ALT_L];
                if ((st->ff_feat[i].ref_enabled = h->feature_enabled[i][VP9_SEG_REF_FRAME])) st->ff_feat[i].ref_val = h->feature_data[i][VP9_SEG_REF_FRAME];
                st->ff_feat[i].skip_enabled = h->feature_enabled[i][VP9_SEG_SKIP];
                // Mesa 재파서(vlVaDecoderVP9BitstreamHeader)가 문맥에 남기는 값
                if ((st->mesa_alt_quant_enabled[i] = h->feature_enabled[i][VP9_SEG_ALT_Q])) st->mesa_alt_quant[i] = h->feature_data[i][VP9_SEG_ALT_Q];
                if ((st->mesa_alt_lf_enabled[i] = h->feature_enabled[i][VP9_SEG_ALT_L])) st->mesa_alt_lf[i] = h->feature_data[i][VP9_SEG_ALT_L];
            }
        }
    } else {
        st->ff_seg_temporal = 0;
    }
    // 세그먼트별 루프 필터 레벨 lflvl[세그먼트][참조 0..3][모드 0..1] (ffmpeg vp9.c). 세그멘테이션이 꺼져 있으면 0번만 갱신
    for (i = 0; i < (h->segmentation_enabled ? 8 : 1); i++) {
        int lflvl, sh = h->loop_filter_level >= 32;
        if (h->segmentation_enabled && st->ff_feat[i].lf_enabled) {
            if (st->ff_seg_absolute_vals) lflvl = vp9msg_clip_uintp2(st->ff_feat[i].lf_val, 6);
            else lflvl = vp9msg_clip_uintp2(h->loop_filter_level + st->ff_feat[i].lf_val, 6);
        } else {
            lflvl = h->loop_filter_level;
        }
        if (h->loop_filter_delta_enabled) {
            st->ff_feat[i].lflvl[0][0] = st->ff_feat[i].lflvl[0][1] =
                (unsigned char)vp9msg_clip_uintp2(lflvl + (h->loop_filter_ref_deltas[0] * (1 << sh)), 6);
            for (j = 1; j < 4; j++) {
                st->ff_feat[i].lflvl[j][0] = (unsigned char)vp9msg_clip_uintp2(
                    lflvl + ((h->loop_filter_ref_deltas[j] + h->loop_filter_mode_deltas[0]) * (1 << sh)), 6);
                st->ff_feat[i].lflvl[j][1] = (unsigned char)vp9msg_clip_uintp2(
                    lflvl + ((h->loop_filter_ref_deltas[j] + h->loop_filter_mode_deltas[1]) * (1 << sh)), 6);
            }
        } else {
            memset(st->ff_feat[i].lflvl, lflvl, sizeof(st->ff_feat[i].lflvl));
        }
    }

    // VAAPI 매개변수 단계의 값(vaapi_vp9.c). VP9MSG_SPEC_CLEAN이면 묵은 값 대신 규격 값
    int va_intra_only = VP9MSG_SPEC_CLEAN ? h->intra_only : st->ff_intraonly;
    int va_reset_ctx = VP9MSG_SPEC_CLEAN ? h->reset_frame_context : st->ff_resetctx;
    int va_seg_temporal = VP9MSG_SPEC_CLEAN ? h->segmentation_temporal_update : st->ff_seg_temporal;

    // ===== Mesa get_vp9_msg() =====
    /* segment table */
    if (h->segmentation_enabled) {
        for (i = 0; i < 8; ++i) {
            prbs->seg.feature_data[i] = ((unsigned)st->mesa_alt_quant[i] & 0xffff) | (((unsigned)st->mesa_alt_lf[i] & 0xff) << 16) |
                                        (((unsigned)st->ff_feat[i].ref_val & 0xf) << 24);
            prbs->seg.feature_mask[i] = (unsigned char)((st->mesa_alt_quant_enabled[i] << 0) | (st->mesa_alt_lf_enabled[i] << 1) |
                                                        (st->ff_feat[i].ref_enabled << 2) | (st->ff_feat[i].skip_enabled << 3));
        }
        for (i = 0; i < 7; ++i) prbs->seg.tree_probs[i] = (unsigned char)st->ff_seg_prob[i];
        // vaapi_vp9.c: temporal이 아니면 255로 채운다
        for (i = 0; i < 3; ++i) prbs->seg.pred_probs[i] = va_seg_temporal ? (unsigned char)st->ff_seg_pred_prob[i] : 255;
        prbs->seg.abs_delta = (unsigned char)st->mesa_abs_delta;
    } else {
        memset(prbs->segment_data, 0, sizeof(prbs->segment_data));
    }

#define VP9MSG_FLAG(v, name) (((unsigned)(v) << RDECODE_FRAME_HDR_INFO_VP9_##name##_SHIFT) & RDECODE_FRAME_HDR_INFO_VP9_##name##_MASK)
    result.frame_header_flags = VP9MSG_FLAG(!keyframe, FRAME_TYPE);  // VAAPI frame_type = !keyframe
    result.frame_header_flags |= VP9MSG_FLAG(h->error_resilient_mode, ERROR_RESILIENT_MODE);
    result.frame_header_flags |= VP9MSG_FLAG(va_intra_only, INTRA_ONLY);
    result.frame_header_flags |= VP9MSG_FLAG(keyframe ? 0 : st->ff_highprecisionmvs, ALLOW_HIGH_PRECISION_MV);
    result.frame_header_flags |= VP9MSG_FLAG(h->frame_parallel_decoding_mode, FRAME_PARALLEL_DECODING_MODE);
    result.frame_header_flags |= VP9MSG_FLAG(h->refresh_frame_context, REFRESH_FRAME_CONTEXT);
    result.frame_header_flags |= VP9MSG_FLAG(h->segmentation_enabled, SEGMENTATION_ENABLED);
    result.frame_header_flags |= VP9MSG_FLAG(h->segmentation_enabled && h->segmentation_update_map, SEGMENTATION_UPDATE_MAP);
    result.frame_header_flags |= VP9MSG_FLAG(h->segmentation_enabled && va_seg_temporal, SEGMENTATION_TEMPORAL_UPDATE);
    result.frame_header_flags |= VP9MSG_FLAG(h->loop_filter_delta_enabled, MODE_REF_DELTA_ENABLED);
    result.frame_header_flags |= VP9MSG_FLAG(h->loop_filter_delta_enabled && h->loop_filter_delta_update, MODE_REF_DELTA_UPDATE);
    // 직전 디코드 프레임이 보이는 프레임이었고, 이번이 error_resilient이 아니며, 크기가 같을 때
    result.frame_header_flags |= VP9MSG_FLAG(st->show_frame && !h->error_resilient_mode && st->last_width == h->frame_width &&
                                             st->last_height == h->frame_height, USE_PREV_IN_FIND_MV_REFS);
    st->show_frame = h->show_frame;
    result.frame_header_flags |= VP9MSG_FLAG(1, USE_UNCOMPRESSED_HEADER);  // 항상 1
    // SHOW_EXISTING_FRAME(비트 0)과 SEGMENTATION_UPDATE_DATA(비트 10)는 Mesa가 세우지 않는다
#undef VP9MSG_FLAG

    result.interp_filter = (unsigned char)(st->ff_filtermode ^ (st->ff_filtermode <= 1));  // mcomp_filter_type
    result.frame_context_idx = (unsigned char)((keyframe || va_intra_only) ? 0 : h->frame_context_idx);
    result.reset_frame_context = (unsigned char)va_reset_ctx;

    result.filter_level = (unsigned char)h->loop_filter_level;
    result.sharpness_level = (unsigned char)h->loop_filter_sharpness;

    for (i = 0; i < 8; ++i) memcpy(result.lf_adj_level[i], st->ff_feat[i].lflvl, 4 * 2);

    if (h->lossless) {
        result.base_qindex = 0; result.y_dc_delta_q = 0; result.uv_ac_delta_q = 0; result.uv_dc_delta_q = 0;
    } else {
        result.base_qindex = (unsigned char)h->base_q_idx;
        result.y_dc_delta_q = (signed char)h->delta_q_y_dc;
#if VP9MSG_UV_DELTA_BY_NAME
        result.uv_ac_delta_q = (signed char)h->delta_q_uv_ac;
        result.uv_dc_delta_q = (signed char)h->delta_q_uv_dc;
#else
        result.uv_ac_delta_q = (signed char)h->delta_q_uv_dc;  // Mesa와 같은 배치(위 설명 참고)
        result.uv_dc_delta_q = (signed char)h->delta_q_uv_ac;
#endif
    }

    result.log2_tile_cols = (unsigned char)h->tile_cols_log2;
    result.log2_tile_rows = (unsigned char)h->tile_rows_log2;
    result.chroma_format = 1;
    result.bit_depth_luma_minus8 = result.bit_depth_chroma_minus8 = (unsigned char)(h->bit_depth - 8);
    // tx_mode, reference_mode, frame_to_show는 Mesa가 채우지 않는다(0). 펌웨어가 압축 헤더에서 직접 읽는다.

    result.vp9_frame_size = (bs_size + 127) & ~127u;  // align(dec->bs_size, 128)
    result.uncompressed_header_size = (unsigned)h->uncompressed_header_size;
    result.compressed_header_size = (unsigned)h->header_size_in_bytes;

    result.curr_pic_idx = curr_pic_idx;
    // 키 프레임이면 Mesa VA 프런트엔드가 참조를 전부 비운다(0x7f)
    for (i = 0; i < 8; i++) result.ref_frame_map[i] = keyframe ? VP9MSG_NO_REF : ref_frame_map[i];
    for (i = 0; i < 3; i++) {
        result.frame_refs[i] = result.ref_frame_map[st->ff_refidx[i]];
        result.ref_frame_sign_bias[i] = (unsigned char)st->ff_signbias[i];
    }

    if (h->profile == 2) {
        if (p010_output) {
            result.p010_mode = 1; result.msb_mode = 1;
        } else {
            result.p010_mode = 0; result.luma_10to8 = 1; result.chroma_10to8 = 1;
        }
    }

    st->last_width = h->frame_width;
    st->last_height = h->frame_height;
    *msg = result;
}

// VCN 3.x(동적 DPB tier 2)에서 Mesa가 VP9 디코드 메시지를 배치하는 방식 그대로 머리와 색인을 채운다.
// msg_buf는 메시지 구역(FB_BUFFER_OFFSET = 0x2000바이트)의 시작. 배치:
//   0   rvcn_dec_message_header_t(40, 안에 index[0] = DECODE)
//   40  index[1] = VP9 코덱 메시지
//   56  index[2] = DYNAMIC_DPB
//   72  rvcn_dec_message_decode_t(188)
//   260 rvcn_dec_message_dynamic_dpb_t2_t(168)
//   428 rvcn_dec_message_vp9_t(120)        total_size = 548
// 암호화(DRM)와 HEVC 참조 목록 색인은 VP9 비보호 재생에는 없다. 돌려주는 포인터의 내용은 호출자가 채운다.
// decode는 0으로 지워져 있고 stream_type만 채워 준다.
typedef struct {
    rvcn_dec_message_header_t *header;
    rvcn_dec_message_decode_t *decode;
    rvcn_dec_message_dynamic_dpb_t2_t *dynamic_dpb_t2;
    rvcn_dec_message_vp9_t *vp9;
} Vp9MsgLayout;

static inline Vp9MsgLayout vp9_msg_layout(void *msg_buf, unsigned stream_handle, unsigned frame_number) {
    Vp9MsgLayout l;
    unsigned char *base = (unsigned char *)msg_buf;
    unsigned sizes = 0, offset_decode, offset_dynamic_dpb, offset_codec;
    rvcn_dec_message_index_t *index_codec, *index_dynamic_dpb;

    l.header = (rvcn_dec_message_header_t *)base;
    sizes += sizeof(rvcn_dec_message_header_t);
    index_codec = (rvcn_dec_message_index_t *)(base + sizes); sizes += sizeof(rvcn_dec_message_index_t);
    index_dynamic_dpb = (rvcn_dec_message_index_t *)(base + sizes); sizes += sizeof(rvcn_dec_message_index_t);
    offset_decode = sizes; l.decode = (rvcn_dec_message_decode_t *)(base + sizes); sizes += sizeof(rvcn_dec_message_decode_t);
    offset_dynamic_dpb = sizes; l.dynamic_dpb_t2 = (rvcn_dec_message_dynamic_dpb_t2_t *)(base + sizes);
    sizes += sizeof(rvcn_dec_message_dynamic_dpb_t2_t);
    offset_codec = sizes; l.vp9 = (rvcn_dec_message_vp9_t *)(base + sizes);

    memset(base, 0, sizes);
    l.header->header_size = sizeof(rvcn_dec_message_header_t);
    l.header->total_size = sizes + sizeof(rvcn_dec_message_vp9_t);
    l.header->msg_type = RDECODE_MSG_DECODE;
    l.header->stream_handle = stream_handle;
    l.header->status_report_feedback_number = frame_number;

    l.header->index[0].message_id = RDECODE_MESSAGE_DECODE;
    l.header->index[0].offset = offset_decode;
    l.header->index[0].size = sizeof(rvcn_dec_message_decode_t);
    l.header->index[0].filled = 0;

    index_codec->message_id = RDECODE_MESSAGE_VP9;
    index_codec->offset = offset_codec;
    index_codec->size = sizeof(rvcn_dec_message_vp9_t);
    index_codec->filled = 0;

    index_dynamic_dpb->message_id = RDECODE_MESSAGE_DYNAMIC_DPB;
    index_dynamic_dpb->offset = offset_dynamic_dpb;
    index_dynamic_dpb->size = sizeof(rvcn_dec_message_dynamic_dpb_t2_t);
    index_dynamic_dpb->filled = 0;

    l.header->num_buffers = 3;
    l.decode->stream_type = RDECODE_CODEC_VP9;
    return l;
}
