#include "av1hdr.h"
#include <string.h>

typedef struct { const uint8_t *d; size_t size, pos; int err; } BR;  // pos in bits
static uint32_t f(BR *b, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        if (b->pos >= b->size * 8) { b->err = 1; return 0; }
        v = (v << 1) | ((b->d[b->pos >> 3] >> (7 - (b->pos & 7))) & 1);
        b->pos++;
    }
    return v;
}
static int su(BR *b, int n) { int v = f(b, n), m = 1 << (n - 1); return (v & m) ? v - 2 * m : v; }
static uint32_t ns(BR *b, int n) {
    int w = 0, x = n;
    while (x) { w++; x >>= 1; }
    int m = (1 << w) - n;
    uint32_t v = f(b, w - 1);
    if ((int)v < m) return v;
    return (v << 1) - m + f(b, 1);
}
static uint32_t uvlc(BR *b) {
    int lz = 0;
    while (!f(b, 1)) { if (++lz >= 32 || b->err) return 0xFFFFFFFF; }
    return f(b, lz) + (1u << lz) - 1;
}
static uint64_t leb128(const uint8_t *d, size_t size, size_t *len) {
    uint64_t v = 0; size_t i = 0;
    for (; i < 8 && i < size; i++) { v |= (uint64_t)(d[i] & 0x7f) << (i * 7); if (!(d[i] & 0x80)) { i++; break; } }
    *len = i;
    return v;
}
static int tile_log2(int blk, int target) { int k = 0; while ((blk << k) < target) k++; return k; }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int clip3(int lo, int hi, int v) { return v < lo ? lo : v > hi ? hi : v; }

static int parse_seq(Av1Seq *s, BR *b) {
    memset(s, 0, sizeof *s);
    s->profile = f(b, 3); s->still_picture = f(b, 1); s->reduced_still_picture_header = f(b, 1);
    if (s->reduced_still_picture_header) {
        s->operating_points_cnt = 1; s->seq_level_idx[0] = f(b, 5);
    } else {
        s->timing_info_present = f(b, 1);
        if (s->timing_info_present) {
            f(b, 32); f(b, 32);
            s->equal_picture_interval = f(b, 1);
            if (s->equal_picture_interval) uvlc(b);
            s->decoder_model_info_present = f(b, 1);
            if (s->decoder_model_info_present) {
                s->buffer_delay_length = f(b, 5) + 1; f(b, 32);
                s->buffer_removal_time_length = f(b, 5) + 1; s->frame_presentation_time_length = f(b, 5) + 1;
            }
        }
        int initial_display_delay_present = f(b, 1);
        s->operating_points_cnt = f(b, 5) + 1;
        for (int i = 0; i < s->operating_points_cnt; i++) {
            s->operating_point_idc[i] = f(b, 12); s->seq_level_idx[i] = f(b, 5);
            if (s->seq_level_idx[i] > 7) s->seq_tier[i] = f(b, 1);
            if (s->decoder_model_info_present) {
                s->decoder_model_present_for_op[i] = f(b, 1);
                if (s->decoder_model_present_for_op[i]) { f(b, s->buffer_delay_length); f(b, s->buffer_delay_length); f(b, 1); }
            }
            if (initial_display_delay_present && f(b, 1)) f(b, 4);
        }
    }
    s->frame_width_bits = f(b, 4) + 1; s->frame_height_bits = f(b, 4) + 1;
    s->max_frame_width = f(b, s->frame_width_bits) + 1; s->max_frame_height = f(b, s->frame_height_bits) + 1;
    if (!s->reduced_still_picture_header) s->frame_id_numbers_present = f(b, 1);
    if (s->frame_id_numbers_present) { s->delta_frame_id_length = f(b, 4) + 2; s->additional_frame_id_length = f(b, 3) + 1; }
    s->use_128x128_superblock = f(b, 1); s->enable_filter_intra = f(b, 1); s->enable_intra_edge_filter = f(b, 1);
    s->seq_force_screen_content_tools = 2; s->seq_force_integer_mv = 2;
    if (!s->reduced_still_picture_header) {
        s->enable_interintra_compound = f(b, 1); s->enable_masked_compound = f(b, 1); s->enable_warped_motion = f(b, 1);
        s->enable_dual_filter = f(b, 1); s->enable_order_hint = f(b, 1);
        if (s->enable_order_hint) { s->enable_jnt_comp = f(b, 1); s->enable_ref_frame_mvs = f(b, 1); }
        if (!f(b, 1)) s->seq_force_screen_content_tools = f(b, 1);
        if (s->seq_force_screen_content_tools > 0) { if (!f(b, 1)) s->seq_force_integer_mv = f(b, 1); }
        if (s->enable_order_hint) s->order_hint_bits = f(b, 3) + 1;
    }
    s->enable_superres = f(b, 1); s->enable_cdef = f(b, 1); s->enable_restoration = f(b, 1);
    // color_config
    int high = f(b, 1);
    s->bit_depth = 8;
    if (s->profile == 2 && high) s->bit_depth = f(b, 1) ? 12 : 10; else if (high) s->bit_depth = 10;
    s->mono_chrome = s->profile == 1 ? 0 : f(b, 1);
    s->num_planes = s->mono_chrome ? 1 : 3;
    s->color_description_present = f(b, 1);
    s->color_primaries = 2; s->transfer_characteristics = 2; s->matrix_coefficients = 2;
    if (s->color_description_present) { s->color_primaries = f(b, 8); s->transfer_characteristics = f(b, 8); s->matrix_coefficients = f(b, 8); }
    if (s->mono_chrome) {
        s->color_range = f(b, 1); s->subsampling_x = s->subsampling_y = 1;
    } else {
        if (s->color_primaries == 1 && s->transfer_characteristics == 13 && s->matrix_coefficients == 0) {
            s->color_range = 1; s->subsampling_x = s->subsampling_y = 0;
        } else {
            s->color_range = f(b, 1);
            if (s->profile == 0) s->subsampling_x = s->subsampling_y = 1;
            else if (s->profile == 1) s->subsampling_x = s->subsampling_y = 0;
            else if (s->bit_depth == 12) { s->subsampling_x = f(b, 1); s->subsampling_y = s->subsampling_x ? f(b, 1) : 0; }
            else { s->subsampling_x = 1; s->subsampling_y = 0; }
            if (s->subsampling_x && s->subsampling_y) s->chroma_sample_position = f(b, 2);
        }
        s->separate_uv_delta_q = f(b, 1);
    }
    s->film_grain_params_present = f(b, 1);
    return b->err ? -1 : 0;
}

static int rel_dist(const Av1Seq *s, int a, int b) {
    if (!s->enable_order_hint) return 0;
    int diff = a - b, m = 1 << (s->order_hint_bits - 1);
    return (diff & (m - 1)) - (diff & m);
}

static void superres_and_size(const Av1Seq *s, Av1Frame *h, BR *b) {
    h->use_superres = s->enable_superres ? f(b, 1) : 0;
    h->superres_denom = h->use_superres ? f(b, 3) + 9 : 8;
    h->upscaled_width = h->frame_width;
    h->frame_width = (h->upscaled_width * 8 + h->superres_denom / 2) / h->superres_denom;
    h->mi_cols = 2 * ((h->frame_width + 7) >> 3); h->mi_rows = 2 * ((h->frame_height + 7) >> 3);
}
static void frame_size(const Av1Seq *s, Av1Frame *h, BR *b) {
    if (h->frame_size_override_flag) { h->frame_width = f(b, s->frame_width_bits) + 1; h->frame_height = f(b, s->frame_height_bits) + 1; }
    else { h->frame_width = s->max_frame_width; h->frame_height = s->max_frame_height; }
    superres_and_size(s, h, b);
}
static void render_size(Av1Frame *h, BR *b) {
    if (f(b, 1)) { h->render_width = f(b, 16) + 1; h->render_height = f(b, 16) + 1; }
    else { h->render_width = h->upscaled_width; h->render_height = h->frame_height; }
}

static int decode_subexp(BR *b, int numSyms) {
    int i = 0, mk = 0, k = 3;
    for (;;) {
        int b2 = i ? k + i - 1 : k, a = 1 << b2;
        if (numSyms <= mk + 3 * a) return ns(b, numSyms - mk) + mk;
        if (f(b, 1)) { i++; mk += a; } else return f(b, b2) + mk;
        if (b->err) return 0;
    }
}
static int inverse_recenter(int r, int v) { return v > 2 * r ? v : (v & 1) ? r - ((v + 1) >> 1) : r + (v >> 1); }
static int decode_unsigned_subexp_with_ref(BR *b, int mx, int r) {
    int v = decode_subexp(b, mx);
    return (r << 1) <= mx ? inverse_recenter(r, v) : mx - 1 - inverse_recenter(mx - 1 - r, v);
}
static void read_global_param(Av1Frame *h, BR *b, const int32_t prev[AV1_NUM_REF][6], int type, int ref, int idx) {
    int absBits = 12, precBits = 15;
    if (idx < 2) {
        if (type == 1) { absBits = 9 - !h->allow_high_precision_mv; precBits = 3 - !h->allow_high_precision_mv; }
        else { absBits = 12; precBits = 6; }
    }
    int precDiff = 16 - precBits, round = (idx % 3) == 2 ? 1 << 16 : 0, sub = (idx % 3) == 2 ? 1 << precBits : 0, mx = 1 << absBits;
    int r = (prev[ref][idx] >> precDiff) - sub;
    int v = decode_unsigned_subexp_with_ref(b, 2 * mx + 1, r + mx) - mx;
    h->gm_params[ref][idx] = (v << precDiff) + round;
}

static void film_grain(const Av1Seq *s, Av1Parser *p, Av1Frame *h, BR *b) {
    Av1FilmGrain *g = &h->film_grain;
    memset(g, 0, sizeof *g);
    if (!s->film_grain_params_present || (!h->show_frame && !h->showable_frame)) return;
    g->apply_grain = f(b, 1);
    if (!g->apply_grain) return;
    g->grain_seed = f(b, 16);
    g->update_grain = h->frame_type == AV1_INTER ? f(b, 1) : 1;
    if (!g->update_grain) {
        int idx = f(b, 3), seed = g->grain_seed;
        *g = p->ref[idx].film_grain;
        g->grain_seed = seed; g->film_grain_params_ref_idx = idx; g->update_grain = 0; g->apply_grain = 1;
        return;
    }
    g->num_y_points = f(b, 4);
    for (int i = 0; i < g->num_y_points && i < 14; i++) { g->point_y_value[i] = f(b, 8); g->point_y_scaling[i] = f(b, 8); }
    g->chroma_scaling_from_luma = s->mono_chrome ? 0 : f(b, 1);
    if (!(s->mono_chrome || g->chroma_scaling_from_luma || (s->subsampling_x == 1 && s->subsampling_y == 1 && g->num_y_points == 0))) {
        g->num_cb_points = f(b, 4);
        for (int i = 0; i < g->num_cb_points && i < 10; i++) { g->point_cb_value[i] = f(b, 8); g->point_cb_scaling[i] = f(b, 8); }
        g->num_cr_points = f(b, 4);
        for (int i = 0; i < g->num_cr_points && i < 10; i++) { g->point_cr_value[i] = f(b, 8); g->point_cr_scaling[i] = f(b, 8); }
    }
    g->grain_scaling_minus_8 = f(b, 2); g->ar_coeff_lag = f(b, 2);
    int numPosLuma = 2 * g->ar_coeff_lag * (g->ar_coeff_lag + 1), numPosChroma = numPosLuma;
    if (g->num_y_points) { numPosChroma = numPosLuma + 1; for (int i = 0; i < numPosLuma; i++) g->ar_coeffs_y_plus_128[i] = f(b, 8); }
    if (g->chroma_scaling_from_luma || g->num_cb_points) for (int i = 0; i < numPosChroma; i++) g->ar_coeffs_cb_plus_128[i] = f(b, 8);
    if (g->chroma_scaling_from_luma || g->num_cr_points) for (int i = 0; i < numPosChroma; i++) g->ar_coeffs_cr_plus_128[i] = f(b, 8);
    g->ar_coeff_shift_minus_6 = f(b, 2); g->grain_scale_shift = f(b, 2);
    if (g->num_cb_points) { g->cb_mult = f(b, 8); g->cb_luma_mult = f(b, 8); g->cb_offset = f(b, 9); }
    if (g->num_cr_points) { g->cr_mult = f(b, 8); g->cr_luma_mult = f(b, 8); g->cr_offset = f(b, 9); }
    g->overlap_flag = f(b, 1); g->clip_to_restricted_range = f(b, 1);
}

static const int defaultRefDeltas[8] = {1, 0, 0, 0, -1, 0, -1, -1};

// uncompressed_header(); returns 0, or negative on error / unsupported syntax.
static int parse_frame_header(Av1Parser *p, Av1Frame *h, BR *b) {
    const Av1Seq *s = &p->seq;
    memset(h, 0, sizeof *h);
    int idLen = s->frame_id_numbers_present ? s->additional_frame_id_length + s->delta_frame_id_length : 0;
    if (s->reduced_still_picture_header) {
        h->frame_type = AV1_KEY; h->frame_is_intra = 1; h->show_frame = 1;
    } else {
        h->show_existing_frame = f(b, 1);
        if (h->show_existing_frame) {
            h->frame_to_show_map_idx = f(b, 3);
            if (s->decoder_model_info_present && !s->equal_picture_interval) f(b, s->frame_presentation_time_length);
            if (s->frame_id_numbers_present) f(b, idLen);
            const Av1Ref *r = &p->ref[h->frame_to_show_map_idx];
            h->frame_type = r->frame_type; h->show_frame = 1;
            h->frame_width = r->frame_width; h->frame_height = r->frame_height; h->upscaled_width = r->upscaled_width;
            h->render_width = r->render_width; h->render_height = r->render_height; h->order_hint = r->order_hint;
            h->film_grain = r->film_grain;
            h->refresh_frame_flags = h->frame_type == AV1_KEY ? 0xff : 0;
            return b->err ? -1 : 0;
        }
        h->frame_type = f(b, 2);
        h->frame_is_intra = h->frame_type == AV1_INTRA_ONLY || h->frame_type == AV1_KEY;
        h->show_frame = f(b, 1);
        if (h->show_frame && s->decoder_model_info_present && !s->equal_picture_interval) f(b, s->frame_presentation_time_length);
        h->showable_frame = h->show_frame ? h->frame_type != AV1_KEY : (int)f(b, 1);
        h->error_resilient_mode = (h->frame_type == AV1_SWITCH || (h->frame_type == AV1_KEY && h->show_frame)) ? 1 : (int)f(b, 1);
    }
    if (h->frame_type == AV1_KEY && h->show_frame)
        for (int i = 0; i < AV1_NUM_REF; i++) { p->ref[i].valid = 0; p->ref[i].order_hint = 0; }
    h->disable_cdf_update = f(b, 1);
    h->allow_screen_content_tools = s->seq_force_screen_content_tools == 2 ? (int)f(b, 1) : s->seq_force_screen_content_tools;
    if (h->allow_screen_content_tools) h->force_integer_mv = s->seq_force_integer_mv == 2 ? (int)f(b, 1) : s->seq_force_integer_mv;
    if (h->frame_is_intra) h->force_integer_mv = 1;
    if (s->frame_id_numbers_present) h->current_frame_id = f(b, idLen);
    h->frame_size_override_flag = h->frame_type == AV1_SWITCH ? 1 : s->reduced_still_picture_header ? 0 : (int)f(b, 1);
    h->order_hint = f(b, s->order_hint_bits);
    h->primary_ref_frame = (h->frame_is_intra || h->error_resilient_mode) ? AV1_PRIMARY_NONE : (int)f(b, 3);
    if (s->decoder_model_info_present && f(b, 1)) {
        for (int op = 0; op < s->operating_points_cnt; op++)
            if (s->decoder_model_present_for_op[op]) {
                int idc = s->operating_point_idc[op];
                if (idc == 0 || (((idc >> p->temporal_id) & 1) && ((idc >> (p->spatial_id + 8)) & 1))) f(b, s->buffer_removal_time_length);
            }
    }
    h->refresh_frame_flags = (h->frame_type == AV1_SWITCH || (h->frame_type == AV1_KEY && h->show_frame)) ? 0xff : (int)f(b, 8);
    if ((!h->frame_is_intra || h->refresh_frame_flags != 0xff) && h->error_resilient_mode && s->enable_order_hint)
        for (int i = 0; i < AV1_NUM_REF; i++) {
            h->ref_order_hint[i] = f(b, s->order_hint_bits);
            if (h->ref_order_hint[i] != p->ref[i].order_hint) { p->ref[i].valid = 0; p->ref[i].order_hint = h->ref_order_hint[i]; }
        }
    if (h->frame_is_intra) {
        frame_size(s, h, b); render_size(h, b);
        if (h->allow_screen_content_tools && h->upscaled_width == h->frame_width) h->allow_intrabc = f(b, 1);
    } else {
        int short_sig = s->enable_order_hint ? (int)f(b, 1) : 0;
        if (short_sig) return -10;  // frame_refs_short_signaling: set_frame_refs() not implemented
        for (int i = 0; i < AV1_REFS; i++) {
            h->ref_frame_idx[i] = f(b, 3);
            if (s->frame_id_numbers_present) f(b, s->delta_frame_id_length);
        }
        if (h->frame_size_override_flag && !h->error_resilient_mode) {
            int found = 0;
            for (int i = 0; i < AV1_REFS && !found; i++)
                if ((found = f(b, 1))) {
                    const Av1Ref *r = &p->ref[h->ref_frame_idx[i]];
                    h->upscaled_width = r->upscaled_width; h->frame_width = h->upscaled_width; h->frame_height = r->frame_height;
                    h->render_width = r->render_width; h->render_height = r->render_height;
                }
            if (!found) { frame_size(s, h, b); render_size(h, b); } else superres_and_size(s, h, b);
        } else { frame_size(s, h, b); render_size(h, b); }
        h->allow_high_precision_mv = h->force_integer_mv ? 0 : (int)f(b, 1);
        h->interpolation_filter = f(b, 1) ? 4 : (int)f(b, 2);
        h->is_motion_mode_switchable = f(b, 1);
        h->use_ref_frame_mvs = (h->error_resilient_mode || !s->enable_ref_frame_mvs) ? 0 : (int)f(b, 1);
        for (int i = 0; i < AV1_REFS; i++) h->order_hints[1 + i] = p->ref[h->ref_frame_idx[i]].order_hint;
    }
    h->disable_frame_end_update_cdf = (s->reduced_still_picture_header || h->disable_cdf_update) ? 1 : (int)f(b, 1);
    // setup_past_independence / load_previous
    int32_t prevGm[AV1_NUM_REF][6];
    for (int r = 0; r < AV1_NUM_REF; r++) for (int i = 0; i < 6; i++) prevGm[r][i] = (i % 3 == 2) ? 1 << 16 : 0;
    memcpy(h->loop_filter_ref_deltas, defaultRefDeltas, sizeof defaultRefDeltas);
    if (h->primary_ref_frame != AV1_PRIMARY_NONE) {
        const Av1Ref *r = &p->ref[h->ref_frame_idx[h->primary_ref_frame]];
        memcpy(prevGm, r->gm_params, sizeof prevGm);
        memcpy(h->loop_filter_ref_deltas, r->loop_filter_ref_deltas, sizeof h->loop_filter_ref_deltas);
        memcpy(h->loop_filter_mode_deltas, r->loop_filter_mode_deltas, sizeof h->loop_filter_mode_deltas);
        memcpy(h->feature_enabled, r->feature_enabled, sizeof h->feature_enabled);
        memcpy(h->feature_data, r->feature_data, sizeof h->feature_data);
    }
    // tile_info
    int sbCols = s->use_128x128_superblock ? (h->mi_cols + 31) >> 5 : (h->mi_cols + 15) >> 4;
    int sbRows = s->use_128x128_superblock ? (h->mi_rows + 31) >> 5 : (h->mi_rows + 15) >> 4;
    int sbShift = s->use_128x128_superblock ? 5 : 4, sbSize = sbShift + 2;
    int maxTileWidthSb = 4096 >> sbSize, maxTileAreaSb = (4096 * 2304) >> (2 * sbSize);
    int minLog2TileCols = tile_log2(maxTileWidthSb, sbCols), maxLog2TileCols = tile_log2(1, imin(sbCols, 64));
    int maxLog2TileRows = tile_log2(1, imin(sbRows, 64));
    int minLog2Tiles = imax(minLog2TileCols, tile_log2(maxTileAreaSb, sbRows * sbCols));
    h->uniform_tile_spacing_flag = f(b, 1);
    int i;
    if (h->uniform_tile_spacing_flag) {
        h->tile_cols_log2 = minLog2TileCols;
        while (h->tile_cols_log2 < maxLog2TileCols && f(b, 1)) h->tile_cols_log2++;
        int tileWidthSb = (sbCols + (1 << h->tile_cols_log2) - 1) >> h->tile_cols_log2;
        for (i = 0; i * tileWidthSb < sbCols && i < 64; i++) h->mi_col_starts[i] = (i * tileWidthSb) << sbShift;
        h->mi_col_starts[i] = h->mi_cols; h->tile_cols = i;
        int minLog2TileRows = imax(minLog2Tiles - h->tile_cols_log2, 0);
        h->tile_rows_log2 = minLog2TileRows;
        while (h->tile_rows_log2 < maxLog2TileRows && f(b, 1)) h->tile_rows_log2++;
        int tileHeightSb = (sbRows + (1 << h->tile_rows_log2) - 1) >> h->tile_rows_log2;
        for (i = 0; i * tileHeightSb < sbRows && i < 64; i++) h->mi_row_starts[i] = (i * tileHeightSb) << sbShift;
        h->mi_row_starts[i] = h->mi_rows; h->tile_rows = i;
    } else {
        int widest = 0, startSb = 0;
        for (i = 0; startSb < sbCols && i < 64; i++) {
            h->mi_col_starts[i] = startSb << sbShift;
            int sizeSb = ns(b, imin(sbCols - startSb, maxTileWidthSb)) + 1;
            widest = imax(widest, sizeSb); startSb += sizeSb;
        }
        h->mi_col_starts[i] = h->mi_cols; h->tile_cols = i; h->tile_cols_log2 = tile_log2(1, h->tile_cols);
        maxTileAreaSb = minLog2Tiles > 0 ? (sbRows * sbCols) >> (minLog2Tiles + 1) : sbRows * sbCols;
        int maxTileHeightSb = imax(maxTileAreaSb / imax(widest, 1), 1);
        startSb = 0;
        for (i = 0; startSb < sbRows && i < 64; i++) {
            h->mi_row_starts[i] = startSb << sbShift;
            startSb += ns(b, imin(sbRows - startSb, maxTileHeightSb)) + 1;
        }
        h->mi_row_starts[i] = h->mi_rows; h->tile_rows = i; h->tile_rows_log2 = tile_log2(1, h->tile_rows);
    }
    if (h->tile_cols_log2 > 0 || h->tile_rows_log2 > 0) {
        h->context_update_tile_id = f(b, h->tile_rows_log2 + h->tile_cols_log2);
        h->tile_size_bytes = f(b, 2) + 1;
    }
    // quantization_params
    h->base_q_idx = f(b, 8);
    h->delta_q_y_dc = f(b, 1) ? su(b, 7) : 0;
    if (s->num_planes > 1) {
        int diff = s->separate_uv_delta_q ? (int)f(b, 1) : 0;
        h->delta_q_u_dc = f(b, 1) ? su(b, 7) : 0; h->delta_q_u_ac = f(b, 1) ? su(b, 7) : 0;
        if (diff) { h->delta_q_v_dc = f(b, 1) ? su(b, 7) : 0; h->delta_q_v_ac = f(b, 1) ? su(b, 7) : 0; }
        else { h->delta_q_v_dc = h->delta_q_u_dc; h->delta_q_v_ac = h->delta_q_u_ac; }
    }
    h->using_qmatrix = f(b, 1);
    if (h->using_qmatrix) { h->qm_y = f(b, 4); h->qm_u = f(b, 4); h->qm_v = s->separate_uv_delta_q ? (int)f(b, 4) : h->qm_u; }
    // segmentation_params
    static const int segBits[8] = {8, 6, 6, 6, 6, 3, 0, 0}, segMax[8] = {255, 63, 63, 63, 63, 7, 0, 0}, segSigned[8] = {1, 1, 1, 1, 1, 0, 0, 0};
    h->segmentation_enabled = f(b, 1);
    if (h->segmentation_enabled) {
        if (h->primary_ref_frame == AV1_PRIMARY_NONE) { h->segmentation_update_map = 1; h->segmentation_update_data = 1; }
        else {
            h->segmentation_update_map = f(b, 1);
            if (h->segmentation_update_map) h->segmentation_temporal_update = f(b, 1);
            h->segmentation_update_data = f(b, 1);
        }
        if (h->segmentation_update_data)
            for (int sg = 0; sg < 8; sg++) for (int j = 0; j < 8; j++) {
                int v = 0, en = f(b, 1);
                h->feature_enabled[sg][j] = en;
                if (en) v = segSigned[j] ? clip3(-segMax[j], segMax[j], su(b, 1 + segBits[j])) : clip3(0, segMax[j], (int)f(b, segBits[j]));
                h->feature_data[sg][j] = v;
            }
    } else { memset(h->feature_enabled, 0, sizeof h->feature_enabled); memset(h->feature_data, 0, sizeof h->feature_data); }
    for (int sg = 0; sg < 8; sg++) for (int j = 0; j < 8; j++)
        if (h->feature_enabled[sg][j]) { h->last_active_seg_id = sg; if (j >= 5) h->seg_id_pre_skip = 1; }
    // delta_q_params, delta_lf_params
    if (h->base_q_idx > 0) h->delta_q_present = f(b, 1);
    if (h->delta_q_present) h->delta_q_res = f(b, 2);
    if (h->delta_q_present && !h->allow_intrabc) {
        h->delta_lf_present = f(b, 1);
        if (h->delta_lf_present) { h->delta_lf_res = f(b, 2); h->delta_lf_multi = f(b, 1); }
    }
    h->coded_lossless = 1;
    for (int sg = 0; sg < 8; sg++) {
        int q = h->base_q_idx;
        if (h->segmentation_enabled && h->feature_enabled[sg][0]) q = clip3(0, 255, q + h->feature_data[sg][0]);
        if (!(q == 0 && !h->delta_q_y_dc && !h->delta_q_u_ac && !h->delta_q_u_dc && !h->delta_q_v_ac && !h->delta_q_v_dc)) h->coded_lossless = 0;
    }
    h->all_lossless = h->coded_lossless && h->frame_width == h->upscaled_width;
    // loop_filter_params
    if (h->coded_lossless || h->allow_intrabc) {
        memcpy(h->loop_filter_ref_deltas, defaultRefDeltas, sizeof defaultRefDeltas);
        memset(h->loop_filter_mode_deltas, 0, sizeof h->loop_filter_mode_deltas);
    } else {
        h->loop_filter_level[0] = f(b, 6); h->loop_filter_level[1] = f(b, 6);
        if (s->num_planes > 1 && (h->loop_filter_level[0] || h->loop_filter_level[1])) { h->loop_filter_level[2] = f(b, 6); h->loop_filter_level[3] = f(b, 6); }
        h->loop_filter_sharpness = f(b, 3);
        h->loop_filter_delta_enabled = f(b, 1);
        if (h->loop_filter_delta_enabled) {
            h->loop_filter_delta_update = f(b, 1);
            if (h->loop_filter_delta_update) {
                for (int k = 0; k < 8; k++) if (f(b, 1)) h->loop_filter_ref_deltas[k] = su(b, 7);
                for (int k = 0; k < 2; k++) if (f(b, 1)) h->loop_filter_mode_deltas[k] = su(b, 7);
            }
        }
    }
    // cdef_params
    h->cdef_damping = 3;
    if (!(h->coded_lossless || h->allow_intrabc || !s->enable_cdef)) {
        h->cdef_damping = f(b, 2) + 3; h->cdef_bits = f(b, 2);
        for (int k = 0; k < (1 << h->cdef_bits); k++) {
            h->cdef_y_pri_strength[k] = f(b, 4); h->cdef_y_sec_strength[k] = f(b, 2);
            if (h->cdef_y_sec_strength[k] == 3) h->cdef_y_sec_strength[k] = 4;
            if (s->num_planes > 1) {
                h->cdef_uv_pri_strength[k] = f(b, 4); h->cdef_uv_sec_strength[k] = f(b, 2);
                if (h->cdef_uv_sec_strength[k] == 3) h->cdef_uv_sec_strength[k] = 4;
            }
        }
    }
    // lr_params
    if (!(h->all_lossless || h->allow_intrabc || !s->enable_restoration)) {
        static const int remap[4] = {0, 3, 1, 2};  // NONE, SWITCHABLE, WIENER, SGRPROJ
        int usesChromaLr = 0;
        for (int k = 0; k < s->num_planes; k++) {
            h->lr_type[k] = remap[f(b, 2)];
            if (h->lr_type[k]) { h->uses_lr = 1; if (k > 0) usesChromaLr = 1; }
        }
        if (h->uses_lr) {
            if (s->use_128x128_superblock) h->lr_unit_shift = f(b, 1) + 1;
            else { h->lr_unit_shift = f(b, 1); if (h->lr_unit_shift) h->lr_unit_shift += f(b, 1); }
            if (s->subsampling_x && s->subsampling_y && usesChromaLr) h->lr_uv_shift = f(b, 1);
            h->loop_restoration_size[0] = 256 >> (2 - h->lr_unit_shift);
            h->loop_restoration_size[1] = h->loop_restoration_size[2] = h->loop_restoration_size[0] >> h->lr_uv_shift;
        }
    }
    h->tx_mode = h->coded_lossless ? 0 : f(b, 1) ? 2 : 1;
    h->reference_select = h->frame_is_intra ? 0 : (int)f(b, 1);
    // skip_mode_params
    if (!(h->frame_is_intra || !h->reference_select || !s->enable_order_hint)) {
        int fwd = -1, bwd = -1, fwdHint = 0, bwdHint = 0;
        for (int k = 0; k < AV1_REFS; k++) {
            int rh = p->ref[h->ref_frame_idx[k]].order_hint;
            if (rel_dist(s, rh, h->order_hint) < 0) { if (fwd < 0 || rel_dist(s, rh, fwdHint) > 0) { fwd = k; fwdHint = rh; } }
            else if (rel_dist(s, rh, h->order_hint) > 0) { if (bwd < 0 || rel_dist(s, rh, bwdHint) < 0) { bwd = k; bwdHint = rh; } }
        }
        if (fwd >= 0 && bwd >= 0) { h->skip_mode_allowed = 1; h->skip_mode_frame[0] = 1 + imin(fwd, bwd); h->skip_mode_frame[1] = 1 + imax(fwd, bwd); }
        else if (fwd >= 0) {
            int sec = -1, secHint = 0;
            for (int k = 0; k < AV1_REFS; k++) {
                int rh = p->ref[h->ref_frame_idx[k]].order_hint;
                if (rel_dist(s, rh, fwdHint) < 0 && (sec < 0 || rel_dist(s, rh, secHint) > 0)) { sec = k; secHint = rh; }
            }
            if (sec >= 0) { h->skip_mode_allowed = 1; h->skip_mode_frame[0] = 1 + imin(fwd, sec); h->skip_mode_frame[1] = 1 + imax(fwd, sec); }
        }
    }
    h->skip_mode_present = h->skip_mode_allowed ? (int)f(b, 1) : 0;
    h->allow_warped_motion = (h->frame_is_intra || h->error_resilient_mode || !s->enable_warped_motion) ? 0 : (int)f(b, 1);
    h->reduced_tx_set = f(b, 1);
    // global_motion_params
    for (int r = 0; r < AV1_NUM_REF; r++) for (int k = 0; k < 6; k++) h->gm_params[r][k] = (k % 3 == 2) ? 1 << 16 : 0;
    if (!h->frame_is_intra)
        for (int r = 1; r <= 7; r++) {
            int type = 0;
            if (f(b, 1)) type = f(b, 1) ? 2 : f(b, 1) ? 1 : 3;
            h->gm_type[r] = type;
            if (type >= 2) {
                read_global_param(h, b, prevGm, type, r, 2); read_global_param(h, b, prevGm, type, r, 3);
                if (type == 3) { read_global_param(h, b, prevGm, type, r, 4); read_global_param(h, b, prevGm, type, r, 5); }
                else { h->gm_params[r][4] = -h->gm_params[r][3]; h->gm_params[r][5] = h->gm_params[r][2]; }
            }
            if (type >= 1) { read_global_param(h, b, prevGm, type, r, 0); read_global_param(h, b, prevGm, type, r, 1); }
        }
    film_grain(s, p, h, b);
    return b->err ? -1 : 0;
}

void av1_update_refs(Av1Parser *p, const Av1Frame *f) {
    if (f->show_existing_frame && f->frame_type != AV1_KEY) return;
    const Av1Ref *src = f->show_existing_frame ? &p->ref[f->frame_to_show_map_idx] : NULL;
    Av1Ref n;
    if (src) n = *src;
    else {
        memset(&n, 0, sizeof n);
        n.valid = 1; n.frame_id = f->current_frame_id; n.upscaled_width = f->upscaled_width; n.frame_width = f->frame_width;
        n.frame_height = f->frame_height; n.render_width = f->render_width; n.render_height = f->render_height;
        n.frame_type = f->frame_type; n.order_hint = f->order_hint;
        memcpy(n.saved_order_hints, f->order_hints, sizeof n.saved_order_hints);
        memcpy(n.gm_params, f->gm_params, sizeof n.gm_params);
        memcpy(n.loop_filter_ref_deltas, f->loop_filter_ref_deltas, sizeof n.loop_filter_ref_deltas);
        memcpy(n.loop_filter_mode_deltas, f->loop_filter_mode_deltas, sizeof n.loop_filter_mode_deltas);
        memcpy(n.feature_enabled, f->feature_enabled, sizeof n.feature_enabled);
        memcpy(n.feature_data, f->feature_data, sizeof n.feature_data);
        n.film_grain = f->film_grain;
    }
    for (int i = 0; i < AV1_NUM_REF; i++) if ((f->refresh_frame_flags >> i) & 1) p->ref[i] = n;
}

int av1_parse_temporal_unit(Av1Parser *p, const uint8_t *data, size_t size, Av1FrameCb cb, void *ctx) {
    size_t pos = 0;
    int tilesTotal = 0;
    while (pos < size) {
        int hdr = data[pos++];
        int type = (hdr >> 3) & 15, ext = (hdr >> 2) & 1, hasSize = (hdr >> 1) & 1;
        if (ext) { if (pos >= size) return -2; p->temporal_id = data[pos] >> 5; p->spatial_id = (data[pos] >> 3) & 3; pos++; }
        else p->temporal_id = p->spatial_id = 0;
        size_t len = 0, obuSize = size - pos;
        if (hasSize) { obuSize = leb128(data + pos, size - pos, &len); pos += len; }
        if (obuSize > size - pos) return -2;
        BR b = {data + pos, obuSize, 0, 0};
        if (type == 2) p->seen_frame_header = 0;                         // temporal delimiter
        else if (type == 1) { if (parse_seq(&p->seq, &b)) return -3; p->have_seq = 1; }
        else if (type == 3 || type == 6 || type == 4) {                  // frame header, frame, tile group
            if (!p->have_seq) return -4;
            Av1Frame *h = &p->cur;
            if (type != 4) {
                if (p->seen_frame_header) { pos += obuSize; continue; }  // duplicate frame header copy
                int r = parse_frame_header(p, h, &b);
                if (r) return r;
                h->frame_header_offset = (uint32_t)pos;
                h->frame_header_bytes = (uint32_t)((b.pos + 7) / 8);
                tilesTotal = h->tile_cols * h->tile_rows;
                if (h->show_existing_frame) { cb(ctx, p, h, data); pos += obuSize; continue; }
                p->seen_frame_header = 1;
                if (type == 3) { pos += obuSize; continue; }
                b.pos = (b.pos + 7) & ~7ULL;                             // byte_alignment before the tile group
            } else if (!p->seen_frame_header) return -5;
            else tilesTotal = h->tile_cols * h->tile_rows;
            // tile_group_obu
            int tgStart = 0, tgEnd = tilesTotal - 1;
            if (tilesTotal > 1 && f(&b, 1)) { int bits = h->tile_cols_log2 + h->tile_rows_log2; tgStart = f(&b, bits); tgEnd = f(&b, bits); }
            b.pos = (b.pos + 7) & ~7ULL;
            size_t o = b.pos / 8;
            for (int t = tgStart; t <= tgEnd && t < AV1_MAX_TILES; t++) {
                size_t tsz;
                if (t == tgEnd) tsz = obuSize - o;
                else {
                    if (o + h->tile_size_bytes > obuSize) return -6;
                    tsz = 0;
                    for (int k = 0; k < h->tile_size_bytes; k++) tsz |= (size_t)b.d[o + k] << (8 * k);
                    tsz += 1; o += h->tile_size_bytes;
                }
                if (o + tsz > obuSize) return -6;
                h->tile_offset[t] = (uint32_t)(pos + o); h->tile_size[t] = (uint32_t)tsz;
                h->num_tiles_found++;
                o += tsz;
            }
            if (tgEnd == tilesTotal - 1) { cb(ctx, p, h, data); p->seen_frame_header = 0; }
        }
        pos += obuSize;
    }
    return 0;
}
