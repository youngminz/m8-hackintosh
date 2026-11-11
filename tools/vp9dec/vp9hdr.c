#include "vp9hdr.h"
#include <string.h>

typedef struct { const uint8_t *d; size_t size, pos; int err; } BR;  // pos는 비트 단위
static uint32_t f(BR *b, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        if (b->pos >= b->size * 8) { b->err = 1; return 0; }
        v = (v << 1) | ((b->d[b->pos >> 3] >> (7 - (b->pos & 7))) & 1);
        b->pos++;
    }
    return v;
}
// su(n): 크기 n비트 뒤에 부호 1비트
static int su(BR *b, int n) { int v = (int)f(b, n); return f(b, 1) ? -v : v; }

void vp9_parser_init(Vp9Parser *p) { memset(p, 0, sizeof *p); }

// 6.2.2 color_config
static int color_config(Vp9Parser *p, BR *b, int profile) {
    p->bit_depth = profile >= 2 ? (f(b, 1) ? 12 : 10) : 8;
    p->color_space = (int)f(b, 3);
    if (p->color_space != VP9_CS_RGB) {
        p->color_range = (int)f(b, 1);
        if (profile == 1 || profile == 3) {
            p->subsampling_x = (int)f(b, 1); p->subsampling_y = (int)f(b, 1);
            if (f(b, 1)) return VP9_ERR_RESERVED;
        } else {
            p->subsampling_x = p->subsampling_y = 1;
        }
    } else {
        p->color_range = 1;
        p->subsampling_x = p->subsampling_y = 0;
        if (profile == 1 || profile == 3) { if (f(b, 1)) return VP9_ERR_RESERVED; }
    }
    return 0;
}

// 7.2 frame_size의 compute_image_size
static void compute_image_size(Vp9Frame *h) {
    h->mi_cols = (h->frame_width + 7) >> 3; h->mi_rows = (h->frame_height + 7) >> 3;
    h->sb64_cols = (h->mi_cols + 7) >> 3; h->sb64_rows = (h->mi_rows + 7) >> 3;
}
static void frame_size(Vp9Frame *h, BR *b) {
    h->frame_width = (int)f(b, 16) + 1; h->frame_height = (int)f(b, 16) + 1;
    compute_image_size(h);
}
static void render_size(Vp9Frame *h, BR *b) {
    h->render_and_frame_size_different = (int)f(b, 1);
    if (h->render_and_frame_size_different) { h->render_width = (int)f(b, 16) + 1; h->render_height = (int)f(b, 16) + 1; }
    else { h->render_width = h->frame_width; h->render_height = h->frame_height; }
}

// setup_past_independence 중 비압축 헤더가 다루는 상태(루프 필터 델타, 세그멘테이션 특징)
static void setup_past_independence(Vp9Parser *p) {
    memset(p->feature_enabled, 0, sizeof p->feature_enabled);
    memset(p->feature_data, 0, sizeof p->feature_data);
    p->segmentation_abs_or_delta_update = 0;
    p->loop_filter_ref_deltas[0] = 1; p->loop_filter_ref_deltas[1] = 0;
    p->loop_filter_ref_deltas[2] = -1; p->loop_filter_ref_deltas[3] = -1;
    p->loop_filter_mode_deltas[0] = p->loop_filter_mode_deltas[1] = 0;
}

int vp9_parse_frame_header(Vp9Parser *p, const uint8_t *data, size_t size, Vp9Frame *h) {
    static const int seg_bits[VP9_SEG_LVL_MAX] = {8, 6, 2, 0}, seg_signed[VP9_SEG_LVL_MAX] = {1, 1, 0, 0};
    static const int literal_to_type[4] = {VP9_EIGHTTAP_SMOOTH, VP9_EIGHTTAP, VP9_EIGHTTAP_SHARP, VP9_BILINEAR};
    BR br = {data, size, 0, 0}, *b = &br;
    int r;
    memset(h, 0, sizeof *h);
    h->frame_size = (uint32_t)size;

    if (f(b, 2) != 2) return b->err ? VP9_ERR_TRUNCATED : VP9_ERR_FRAME_MARKER;
    h->profile = (int)f(b, 1); h->profile |= (int)f(b, 1) << 1;
    if (h->profile == 3 && f(b, 1)) return VP9_ERR_RESERVED;
    h->show_existing_frame = (int)f(b, 1);
    if (h->show_existing_frame) {
        h->frame_to_show_map_idx = (int)f(b, 3);
        if (b->err) return VP9_ERR_TRUNCATED;
        if (!p->ref[h->frame_to_show_map_idx].valid) return VP9_ERR_REF_MISSING;
        const Vp9Ref *rf = &p->ref[h->frame_to_show_map_idx];
        h->show_frame = 1;  // 규격: header_size_in_bytes = 0, refresh_frame_flags = 0, loop_filter_level = 0, show_frame = 1
        h->frame_width = h->render_width = rf->frame_width; h->frame_height = h->render_height = rf->frame_height;
        h->bit_depth = rf->bit_depth; h->subsampling_x = rf->subsampling_x; h->subsampling_y = rf->subsampling_y;
        compute_image_size(h);
        h->uncompressed_header_size = (int)((b->pos + 7) >> 3);
        return 0;
    }
    p->profile = h->profile;
    h->frame_type = (int)f(b, 1);
    h->show_frame = (int)f(b, 1);
    h->error_resilient_mode = (int)f(b, 1);
    if (h->frame_type == VP9_KEY_FRAME) {
        if (f(b, 24) != 0x498342) return b->err ? VP9_ERR_TRUNCATED : VP9_ERR_SYNC_CODE;
        if ((r = color_config(p, b, h->profile)) < 0) return r;
        frame_size(h, b);
        render_size(h, b);
        h->refresh_frame_flags = 0xff;
        h->frame_is_intra = 1;
    } else {
        h->intra_only = h->show_frame ? 0 : (int)f(b, 1);
        h->frame_is_intra = h->intra_only;
        h->reset_frame_context = h->error_resilient_mode ? 0 : (int)f(b, 2);
        if (h->intra_only) {
            if (f(b, 24) != 0x498342) return b->err ? VP9_ERR_TRUNCATED : VP9_ERR_SYNC_CODE;
            if (h->profile > 0) {
                if ((r = color_config(p, b, h->profile)) < 0) return r;
            } else {
                p->color_space = 2 /* CS_BT_601 */; p->color_range = 0;
                p->subsampling_x = p->subsampling_y = 1; p->bit_depth = 8;
            }
            h->refresh_frame_flags = (int)f(b, 8);
            frame_size(h, b);
            render_size(h, b);
        } else {
            h->refresh_frame_flags = (int)f(b, 8);
            for (int i = 0; i < VP9_REFS_PER_FRAME; i++) {
                h->ref_frame_idx[i] = (int)f(b, 3);
                h->ref_frame_sign_bias[i] = (int)f(b, 1);
            }
            if (b->err) return VP9_ERR_TRUNCATED;
            for (int i = 0; i < VP9_REFS_PER_FRAME; i++)
                if (!p->ref[h->ref_frame_idx[i]].valid) return VP9_ERR_REF_MISSING;
            // frame_size_with_refs
            for (int i = 0; i < VP9_REFS_PER_FRAME; i++) {
                if (f(b, 1)) {
                    const Vp9Ref *rf = &p->ref[h->ref_frame_idx[i]];
                    h->found_ref = i + 1;
                    h->frame_width = rf->frame_width; h->frame_height = rf->frame_height;
                    break;
                }
            }
            if (!h->found_ref) frame_size(h, b); else compute_image_size(h);
            render_size(h, b);
            h->allow_high_precision_mv = (int)f(b, 1);
            // read_interpolation_filter
            h->is_filter_switchable = (int)f(b, 1);
            if (h->is_filter_switchable) h->interpolation_filter = VP9_SWITCHABLE;
            else { h->raw_interpolation_filter = (int)f(b, 2); h->interpolation_filter = literal_to_type[h->raw_interpolation_filter]; }
        }
    }
    h->bit_depth = p->bit_depth; h->color_space = p->color_space; h->color_range = p->color_range;
    h->subsampling_x = p->subsampling_x; h->subsampling_y = p->subsampling_y;
    h->size_changed = h->frame_width != p->last_width || h->frame_height != p->last_height;

    if (!h->error_resilient_mode) {
        h->refresh_frame_context = (int)f(b, 1);
        h->frame_parallel_decoding_mode = (int)f(b, 1);
    } else {
        h->refresh_frame_context = 0;
        h->frame_parallel_decoding_mode = 1;
    }
    h->frame_context_idx = (int)f(b, 2);
    if (h->frame_is_intra || h->error_resilient_mode) setup_past_independence(p);

    // loop_filter_params
    h->loop_filter_level = (int)f(b, 6);
    h->loop_filter_sharpness = (int)f(b, 3);
    h->loop_filter_delta_enabled = (int)f(b, 1);
    if (h->loop_filter_delta_enabled) {
        h->loop_filter_delta_update = (int)f(b, 1);
        if (h->loop_filter_delta_update) {
            for (int i = 0; i < 4; i++) {
                h->update_ref_delta[i] = (int)f(b, 1);
                if (h->update_ref_delta[i]) p->loop_filter_ref_deltas[i] = su(b, 6);
            }
            for (int i = 0; i < 2; i++) {
                h->update_mode_delta[i] = (int)f(b, 1);
                if (h->update_mode_delta[i]) p->loop_filter_mode_deltas[i] = su(b, 6);
            }
        }
    }
    memcpy(h->loop_filter_ref_deltas, p->loop_filter_ref_deltas, sizeof h->loop_filter_ref_deltas);
    memcpy(h->loop_filter_mode_deltas, p->loop_filter_mode_deltas, sizeof h->loop_filter_mode_deltas);

    // quantization_params (부호화 순서: y_dc, uv_dc, uv_ac)
    h->base_q_idx = (int)f(b, 8);
    h->delta_q_y_dc = f(b, 1) ? su(b, 4) : 0;
    h->delta_q_uv_dc = f(b, 1) ? su(b, 4) : 0;
    h->delta_q_uv_ac = f(b, 1) ? su(b, 4) : 0;
    h->lossless = h->base_q_idx == 0 && h->delta_q_y_dc == 0 && h->delta_q_uv_dc == 0 && h->delta_q_uv_ac == 0;

    // segmentation_params
    for (int i = 0; i < 7; i++) h->segmentation_tree_probs[i] = 255;
    for (int i = 0; i < 3; i++) h->segmentation_pred_probs[i] = 255;
    h->segmentation_enabled = (int)f(b, 1);
    if (h->segmentation_enabled) {
        h->segmentation_update_map = (int)f(b, 1);
        if (h->segmentation_update_map) {
            for (int i = 0; i < 7; i++) h->segmentation_tree_probs[i] = f(b, 1) ? (int)f(b, 8) : 255;
            h->segmentation_temporal_update = (int)f(b, 1);
            for (int i = 0; i < 3; i++)
                h->segmentation_pred_probs[i] = h->segmentation_temporal_update ? (f(b, 1) ? (int)f(b, 8) : 255) : 255;
        }
        h->segmentation_update_data = (int)f(b, 1);
        if (h->segmentation_update_data) {
            p->segmentation_abs_or_delta_update = (int)f(b, 1);
            for (int i = 0; i < VP9_MAX_SEGMENTS; i++) {
                for (int j = 0; j < VP9_SEG_LVL_MAX; j++) {
                    int value = 0, enabled = (int)f(b, 1);
                    p->feature_enabled[i][j] = enabled;
                    if (enabled) {
                        value = (int)f(b, seg_bits[j]);
                        if (seg_signed[j] && f(b, 1)) value = -value;
                    }
                    p->feature_data[i][j] = value;
                }
            }
        }
    }
    h->segmentation_abs_or_delta_update = p->segmentation_abs_or_delta_update;
    memcpy(h->feature_enabled, p->feature_enabled, sizeof h->feature_enabled);
    memcpy(h->feature_data, p->feature_data, sizeof h->feature_data);

    // tile_info
    while ((64 << h->min_log2_tile_cols) < h->sb64_cols) h->min_log2_tile_cols++;
    h->max_log2_tile_cols = 1;
    while ((h->sb64_cols >> h->max_log2_tile_cols) >= 4) h->max_log2_tile_cols++;
    h->max_log2_tile_cols--;
    h->tile_cols_log2 = h->min_log2_tile_cols;
    while (h->tile_cols_log2 < h->max_log2_tile_cols) {
        if (f(b, 1)) h->tile_cols_log2++; else break;
    }
    h->tile_rows_log2 = (int)f(b, 1);
    if (h->tile_rows_log2) h->tile_rows_log2 += (int)f(b, 1);

    h->header_size_in_bytes = (int)f(b, 16);
    if (b->err) return VP9_ERR_TRUNCATED;
    h->uncompressed_header_size = (int)((b->pos + 7) >> 3);  // trailing_bits로 바이트 경계까지
    if (h->header_size_in_bytes == 0 || (size_t)h->uncompressed_header_size + (size_t)h->header_size_in_bytes > size)
        return VP9_ERR_HEADER_SIZE;
    return 0;
}

void vp9_update_refs(Vp9Parser *p, const Vp9Frame *h) {
    if (h->show_existing_frame) return;
    for (int i = 0; i < VP9_NUM_REF; i++) {
        if (!((h->refresh_frame_flags >> i) & 1)) continue;
        Vp9Ref *rf = &p->ref[i];
        rf->valid = 1; rf->frame_width = h->frame_width; rf->frame_height = h->frame_height;
        rf->bit_depth = h->bit_depth; rf->subsampling_x = h->subsampling_x; rf->subsampling_y = h->subsampling_y;
    }
    p->last_width = h->frame_width; p->last_height = h->frame_height;
    p->last_show_frame = h->show_frame; p->last_intra_only = h->intra_only;
    p->frames_decoded++;
}

int vp9_split_superframe(const uint8_t *data, size_t size, uint32_t offset[VP9_MAX_SUPERFRAME],
                         uint32_t frame_size[VP9_MAX_SUPERFRAME]) {
    if (size == 0) return 0;
    uint8_t marker = data[size - 1];
    if ((marker & 0xe0) == 0xc0) {
        int frames = (marker & 7) + 1, mag = ((marker >> 3) & 3) + 1;
        size_t index_size = 2 + (size_t)mag * (size_t)frames;
        if (size >= index_size && data[size - index_size] == marker) {
            const uint8_t *x = data + size - index_size + 1;
            size_t pos = 0, limit = size - index_size;
            int n = 0;
            for (int i = 0; i < frames; i++) {
                uint32_t sz = 0;
                for (int j = 0; j < mag; j++) sz |= (uint32_t)*x++ << (8 * j);
                if (sz > limit - pos) return 0;
                if (sz) { offset[n] = (uint32_t)pos; frame_size[n] = sz; n++; }
                pos += sz;
            }
            return n;
        }
    }
    offset[0] = 0; frame_size[0] = (uint32_t)size;
    return 1;
}
