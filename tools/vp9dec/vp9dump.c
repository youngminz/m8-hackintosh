// vp9dump FILE.ivf [N] [-t] [-m]
// ivf 파일을 읽어 프레임마다 비압축 헤더의 주요 필드를 출력한다(슈퍼프레임은 풀어서 프레임 단위로).
//   N   처음 N개 프레임만 출력(파싱은 끝까지 한다)
//   -t  대조용 출력: 부호화된 구문 요소를 이름=값으로 전부 찍는다(compare_trace.py가 ffmpeg trace_headers와 견준다)
//   -m  VCN 메시지(vp9msg.h)를 채워 주요 필드와 16진 덤프를 함께 찍는다. DPB 칸 번호는 Mesa 방식으로 흉내 낸다
#include "vp9hdr.h"
#include "vp9msg.h"
#include "vp9_probs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int n, errors, limit = 1000000, trace_mode, msg_mode;
static Vp9Parser parser;
static Vp9MsgState msg_state;
static uint8_t dpb_slot[8];  // VP9 참조 슬롯마다 들어 있는 DPB 칸 번호
static rvcn_dec_vp9_probs_segment_t probs;

static uint32_t rl32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static void print_trace(const Vp9Frame *f) {
    printf(" profile=%d show_existing_frame=%d", f->profile, f->show_existing_frame);
    if (f->show_existing_frame) { printf(" frame_to_show_map_idx=%d", f->frame_to_show_map_idx); return; }
    printf(" frame_type=%d show_frame=%d error_resilient_mode=%d intra_only=%d reset_frame_context=%d refresh_frame_flags=%d",
           f->frame_type, f->show_frame, f->error_resilient_mode, f->intra_only, f->reset_frame_context, f->refresh_frame_flags);
    int color_coded = f->frame_type == VP9_KEY_FRAME || (f->intra_only && f->profile > 0);
    if (color_coded) {
        printf(" bit_depth=%d color_space=%d", f->bit_depth, f->color_space);
        if (f->color_space != VP9_CS_RGB) printf(" color_range=%d", f->color_range);
        if (f->profile == 1 || f->profile == 3) printf(" subsampling_x=%d subsampling_y=%d", f->subsampling_x, f->subsampling_y);
    }
    if (!f->frame_is_intra) {
        for (int i = 0; i < 3; i++) printf(" ref_frame_idx%d=%d sign_bias%d=%d", i, f->ref_frame_idx[i], i, f->ref_frame_sign_bias[i]);
        printf(" found_ref=%d allow_high_precision_mv=%d is_filter_switchable=%d", f->found_ref, f->allow_high_precision_mv, f->is_filter_switchable);
        if (!f->is_filter_switchable) printf(" raw_interpolation_filter=%d", f->raw_interpolation_filter);
    }
    printf(" frame_width=%d frame_height=%d render_and_frame_size_different=%d", f->frame_width, f->frame_height, f->render_and_frame_size_different);
    if (f->render_and_frame_size_different) printf(" render_width=%d render_height=%d", f->render_width, f->render_height);
    printf(" refresh_frame_context=%d frame_parallel_decoding_mode=%d frame_context_idx=%d", f->refresh_frame_context,
           f->frame_parallel_decoding_mode, f->frame_context_idx);
    printf(" loop_filter_level=%d loop_filter_sharpness=%d loop_filter_delta_enabled=%d loop_filter_delta_update=%d", f->loop_filter_level,
           f->loop_filter_sharpness, f->loop_filter_delta_enabled, f->loop_filter_delta_update);
    for (int i = 0; i < 4; i++) { printf(" update_ref_delta%d=%d", i, f->update_ref_delta[i]); if (f->update_ref_delta[i]) printf(" ref_delta%d=%d", i, f->loop_filter_ref_deltas[i]); }
    for (int i = 0; i < 2; i++) { printf(" update_mode_delta%d=%d", i, f->update_mode_delta[i]); if (f->update_mode_delta[i]) printf(" mode_delta%d=%d", i, f->loop_filter_mode_deltas[i]); }
    printf(" base_q_idx=%d delta_q_y_dc=%d delta_q_uv_dc=%d delta_q_uv_ac=%d", f->base_q_idx, f->delta_q_y_dc, f->delta_q_uv_dc, f->delta_q_uv_ac);
    printf(" segmentation_enabled=%d segmentation_update_map=%d segmentation_temporal_update=%d segmentation_update_data=%d",
           f->segmentation_enabled, f->segmentation_update_map, f->segmentation_temporal_update, f->segmentation_update_data);
    if (f->segmentation_update_map) {
        for (int i = 0; i < 7; i++) printf(" tree_prob%d=%d", i, f->segmentation_tree_probs[i]);
        for (int i = 0; i < 3; i++) printf(" pred_prob%d=%d", i, f->segmentation_pred_probs[i]);
    }
    if (f->segmentation_update_data) {
        printf(" segmentation_abs_or_delta_update=%d", f->segmentation_abs_or_delta_update);
        for (int i = 0; i < 8; i++) for (int j = 0; j < 4; j++) {
            printf(" feature_enabled%d_%d=%d", i, j, f->feature_enabled[i][j]);
            if (f->feature_enabled[i][j] && j < 3) printf(" feature_data%d_%d=%d", i, j, f->feature_data[i][j]);
        }
    }
    printf(" tile_cols_log2=%d tile_rows_log2=%d header_size_in_bytes=%d uncompressed_header_size=%d", f->tile_cols_log2, f->tile_rows_log2,
           f->header_size_in_bytes, f->uncompressed_header_size);
}

static void print_summary(const Vp9Frame *f) {
    if (f->show_existing_frame) { printf(" show_existing %d (%dx%d)", f->frame_to_show_map_idx, f->frame_width, f->frame_height); return; }
    printf(" %s%s show %d errres %d %dx%d depth %d refresh %02x", f->frame_type == VP9_KEY_FRAME ? "KEY" : f->intra_only ? "INTRA" : "inter",
           f->found_ref ? "(size from ref)" : "", f->show_frame, f->error_resilient_mode, f->frame_width, f->frame_height, f->bit_depth, f->refresh_frame_flags);
    if (!f->frame_is_intra)
        printf(" refs %d %d %d bias %d%d%d hpmv %d interp %d", f->ref_frame_idx[0], f->ref_frame_idx[1], f->ref_frame_idx[2], f->ref_frame_sign_bias[0],
               f->ref_frame_sign_bias[1], f->ref_frame_sign_bias[2], f->allow_high_precision_mv, f->interpolation_filter);
    printf(" ctx %d reset %d refreshctx %d par %d q %d (%d %d %d) lf %d/%d delta %d%d [%d %d %d %d | %d %d] seg %d%d%d%d tiles 2^%dx2^%d hdr %d+%d",
           f->frame_context_idx, f->reset_frame_context, f->refresh_frame_context, f->frame_parallel_decoding_mode, f->base_q_idx, f->delta_q_y_dc,
           f->delta_q_uv_dc, f->delta_q_uv_ac, f->loop_filter_level, f->loop_filter_sharpness, f->loop_filter_delta_enabled, f->loop_filter_delta_update,
           f->loop_filter_ref_deltas[0], f->loop_filter_ref_deltas[1], f->loop_filter_ref_deltas[2], f->loop_filter_ref_deltas[3],
           f->loop_filter_mode_deltas[0], f->loop_filter_mode_deltas[1], f->segmentation_enabled, f->segmentation_update_map,
           f->segmentation_temporal_update, f->segmentation_update_data, f->tile_cols_log2, f->tile_rows_log2, f->uncompressed_header_size,
           f->header_size_in_bytes);
}

// Mesa 방식 DPB 칸 배정: 어느 참조 슬롯에서도 쓰이지 않는 가장 작은 번호(키 프레임이면 전부 비운 것으로 본다)
static uint8_t pick_dpb_index(int keyframe) {
    for (uint8_t idx = 0; idx < 32; idx++) {
        int used = 0;
        if (!keyframe) for (int i = 0; i < 8; i++) if (dpb_slot[i] == idx) used = 1;
        if (!used) return idx;
    }
    return 0;
}

static void print_msg(const Vp9Frame *f, int print) {
    rvcn_dec_message_vp9_t m;
    int key = f->frame_type == VP9_KEY_FRAME;
    uint8_t cur = pick_dpb_index(key);
    vp9_fill_msg(&msg_state, f, f->frame_size, dpb_slot, cur, 1, &m, &probs);
    if (key) memset(dpb_slot, VP9MSG_NO_REF, sizeof dpb_slot);
    for (int i = 0; i < 8; i++) if ((f->refresh_frame_flags >> i) & 1) dpb_slot[i] = cur;
    if (!print) return;
    printf("\n    msg flags %04x ctx %d reset %d cur %d interp %d lf %d/%d q %d (%d %d %d) tiles %d %d map", m.frame_header_flags, m.frame_context_idx,
           m.reset_frame_context, m.curr_pic_idx, m.interp_filter, m.filter_level, m.sharpness_level, m.base_qindex, m.y_dc_delta_q, m.uv_ac_delta_q,
           m.uv_dc_delta_q, m.log2_tile_cols, m.log2_tile_rows);
    for (int i = 0; i < 8; i++) printf(" %02x", m.ref_frame_map[i]);
    printf(" refs %02x %02x %02x bias %d%d%d depth %d sizes %u/%u/%u lf_adj[0]", m.frame_refs[0], m.frame_refs[1], m.frame_refs[2], m.ref_frame_sign_bias[0],
           m.ref_frame_sign_bias[1], m.ref_frame_sign_bias[2], m.bit_depth_luma_minus8 + 8, m.vp9_frame_size, m.uncompressed_header_size,
           m.compressed_header_size);
    for (int j = 0; j < 4; j++) printf(" %d,%d", m.lf_adj_level[0][j][0], m.lf_adj_level[0][j][1]);
    printf("\n    msg hex ");
    for (size_t i = 0; i < sizeof m; i++) printf("%02x", ((const uint8_t *)&m)[i]);
    if (f->segmentation_enabled) {
        printf("\n    seg hex ");
        for (int i = 0; i < 51; i++) printf("%02x", probs.segment_data[i]);
    }
}

int main(int argc, char **argv) {
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-t")) trace_mode = 1;
        else if (!strcmp(argv[i], "-m")) msg_mode = 1;
        else if (!path) path = argv[i];
        else limit = atoi(argv[i]);
    }
    if (!path) { fprintf(stderr, "usage: vp9dump FILE.ivf [N] [-t] [-m]\n"); return 2; }
    FILE *fp = fopen(path, "rb"); if (!fp) { perror(path); return 1; }
    fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)sz);
    if (!d || fread(d, 1, (size_t)sz, fp) != (size_t)sz) return 1;
    fclose(fp);
    if (sz < 32 || memcmp(d, "DKIF", 4)) { fprintf(stderr, "not an ivf file\n"); return 1; }
    printf("ivf fourcc %.4s %dx%d, %u packets in header\n", d + 8, d[12] | d[13] << 8, d[14] | d[15] << 8, rl32(d + 24));

    vp9_parser_init(&parser); vp9_msg_state_init(&msg_state);
    memset(dpb_slot, VP9MSG_NO_REF, sizeof dpb_slot);
    vp9_init_probs_buffer(&probs);
    size_t pos = (size_t)(d[6] | d[7] << 8);
    int packets = 0, superframes = 0;
    while (pos + 12 <= (size_t)sz) {
        uint32_t psize = rl32(d + pos);
        pos += 12;
        if (psize > (size_t)sz - pos) { printf("packet %d: truncated\n", packets); errors++; break; }
        uint32_t off[VP9_MAX_SUPERFRAME], fsz[VP9_MAX_SUPERFRAME];
        int count = vp9_split_superframe(d + pos, psize, off, fsz);
        if (count == 0) { printf("packet %d: bad superframe index\n", packets); errors++; }
        if (count > 1) superframes++;
        for (int i = 0; i < count; i++) {
            Vp9Frame f;
            int r = vp9_parse_frame_header(&parser, d + pos + off[i], fsz[i], &f);
            int print = n < limit;
            if (r < 0) { printf("frame %d packet %d: PARSE ERROR %d\n", n, packets, r); errors++; n++; continue; }
            if (print) {
                printf("frame %d packet %d size %u:", n, packets, fsz[i]);
                if (trace_mode) print_trace(&f); else print_summary(&f);
            }
            if (msg_mode && !f.show_existing_frame) print_msg(&f, print);
            if (print) printf("\n");
            vp9_update_refs(&parser, &f);
            n++;
        }
        pos += psize;
        packets++;
    }
    printf("result: %d packets, %d frames, %d superframes, %d errors\n", packets, n, superframes, errors);
    return errors ? 1 : 0;
}
