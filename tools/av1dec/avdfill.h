// Fills the AMD VA driver's AV1 sequence / frame header structs (AvdAv1SeqHdr, AvdAv1FrameHdr; see the
// side note avd-av1-picparam-layout.md) from the parsed headers.
#pragma once
#include "av1hdr.h"

static void fillSeq(uint32_t *S, const Av1Seq *s) {
#define SQ(off) S[(off) / 4]
    SQ(0x000) = s->profile; SQ(0x004) = s->still_picture; SQ(0x008) = s->reduced_still_picture_header;
    SQ(0x4bc) = s->frame_width_bits - 1; SQ(0x4c0) = s->frame_height_bits - 1;
    SQ(0x4c4) = s->max_frame_width - 1; SQ(0x4c8) = s->max_frame_height - 1;
    SQ(0x4d8) = s->use_128x128_superblock; SQ(0x4dc) = s->enable_filter_intra; SQ(0x4e0) = s->enable_intra_edge_filter;
    SQ(0x4e4) = s->enable_interintra_compound; SQ(0x4e8) = s->enable_masked_compound; SQ(0x4ec) = s->enable_warped_motion;
    SQ(0x4f0) = s->enable_dual_filter; SQ(0x4f4) = s->enable_order_hint; SQ(0x4f8) = s->enable_jnt_comp;
    SQ(0x4fc) = s->enable_ref_frame_mvs; SQ(0x510) = s->order_hint_bits ? s->order_hint_bits - 1 : 0; SQ(0x514) = s->order_hint_bits;
    SQ(0x518) = s->enable_superres; SQ(0x51c) = s->enable_cdef; SQ(0x520) = s->enable_restoration;
    SQ(0x524) = s->bit_depth > 8; SQ(0x528) = s->bit_depth == 12; SQ(0x52c) = s->bit_depth; SQ(0x530) = s->mono_chrome;
}

static void fillFrame(uint32_t *F, const Av1Seq *s, const Av1Frame *f) {
#define FR(off) F[(off) / 4]
    FR(0x010) = f->frame_type; FR(0x018) = f->show_frame; FR(0x01c) = f->showable_frame; FR(0x020) = f->error_resilient_mode;
    FR(0x024) = f->disable_cdf_update; FR(0x028) = f->allow_screen_content_tools; FR(0x02c) = f->force_integer_mv;
    FR(0x030) = f->current_frame_id; FR(0x040) = f->order_hint; FR(0x064) = f->primary_ref_frame; FR(0x0ec) = f->refresh_frame_flags;
    FR(0x134) = f->upscaled_width - 1; FR(0x138) = f->frame_width; FR(0x13c) = f->frame_height - 1; FR(0x140) = f->frame_height;
    FR(0x144) = f->upscaled_width; FR(0x148) = f->use_superres; FR(0x14c) = f->superres_denom; FR(0x16c) = f->allow_intrabc;
    FR(0x1b8) = f->allow_high_precision_mv; FR(0x1bc) = f->interpolation_filter == 4; FR(0x1c0) = f->interpolation_filter;
    FR(0x1c4) = f->is_motion_mode_switchable; FR(0x1c8) = f->use_ref_frame_mvs; FR(0x1cc) = f->disable_frame_end_update_cdf;
    FR(0x1d0) = f->uniform_tile_spacing_flag; FR(0x1d4) = f->tile_cols_log2; FR(0x1d8) = f->tile_rows_log2;
    for (int i = 0; i < 65; i++) { FR(0x1e4 + 4 * i) = f->mi_col_starts[i]; FR(0x2e8 + 4 * i) = f->mi_row_starts[i]; }
    FR(0x3ec) = f->tile_cols; FR(0x3f0) = f->tile_rows; FR(0x3fc) = f->context_update_tile_id;
    FR(0x408) = f->base_q_idx; FR(0x414) = f->delta_q_y_dc; FR(0x41c) = f->delta_q_u_dc; FR(0x420) = f->delta_q_u_ac;
    FR(0x424) = f->delta_q_v_dc; FR(0x428) = f->delta_q_v_ac;
    FR(0x42c) = f->using_qmatrix; FR(0x430) = f->qm_y | 0xf0; FR(0x434) = f->qm_u | 0xf0; FR(0x438) = f->qm_v | 0xf0;
    FR(0x43c) = f->segmentation_enabled; FR(0x440) = f->segmentation_update_map; FR(0x444) = f->segmentation_temporal_update;
    FR(0x448) = f->segmentation_update_data;
    int16_t *fd = (int16_t *)((uint8_t *)F + 0x554);
    for (int i = 0; i < 8; i++) for (int j = 0; j < 8; j++) { FR(0x450 + 4 * (i * 8 + j)) = f->feature_enabled[i][j]; fd[i * 8 + j] = f->feature_data[i][j]; }
    FR(0x5dc) = f->delta_q_present; FR(0x5e0) = f->delta_q_res; FR(0x5e4) = f->delta_lf_present; FR(0x5e8) = f->delta_lf_res;
    FR(0x5ec) = f->delta_lf_multi;
    FR(0x678) = f->loop_filter_level[0]; FR(0x67c) = f->loop_filter_level[1]; FR(0x680) = f->loop_filter_level[2];
    FR(0x684) = f->loop_filter_level[3]; FR(0x688) = f->loop_filter_sharpness; FR(0x68c) = f->loop_filter_delta_enabled;
    FR(0x690) = f->loop_filter_delta_update;
    for (int i = 0; i < 8; i++) FR(0x698 + 4 * i) = f->loop_filter_ref_deltas[i];
    FR(0x6bc) = f->loop_filter_mode_deltas[0]; FR(0x6c0) = f->loop_filter_mode_deltas[1];
    FR(0x6c4) = f->cdef_damping - 3; FR(0x6c8) = f->cdef_bits; FR(0x74c) = f->cdef_damping;
    for (int i = 0; i < 8; i++) {  // Mesa packing (pri << 2 | raw 2-bit sec) in the low field, high field 0
        int ys = f->cdef_y_sec_strength[i] == 4 ? 3 : f->cdef_y_sec_strength[i], us = f->cdef_uv_sec_strength[i] == 4 ? 3 : f->cdef_uv_sec_strength[i];
        FR(0x6cc + 4 * i) = f->cdef_y_pri_strength[i] << 2 | ys; FR(0x70c + 4 * i) = f->cdef_uv_pri_strength[i] << 2 | us;
    }
    for (int i = 0; i < 3; i++) { FR(0x750 + 4 * i) = f->lr_type[i]; FR(0x76c + 4 * i) = f->loop_restoration_size[i]; }
    FR(0x780) = f->tx_mode; FR(0x784) = f->reference_select; FR(0x790) = f->skip_mode_present; FR(0x794) = f->allow_warped_motion;
    FR(0x798) = f->reduced_tx_set;
    for (int r = 0; r < 8; r++) { FR(0x79c + 4 * r) = f->gm_type[r]; for (int k = 0; k < 6; k++) FR(0x7bc + 0x18 * r + 4 * k) = f->gm_params[r][k]; }
    const Av1FilmGrain *g = &f->film_grain;
    FR(0x948) = g->apply_grain; FR(0x94c) = g->grain_seed; FR(0x950) = g->update_grain; FR(0x954) = g->film_grain_params_ref_idx;
    FR(0x958) = g->num_y_points;
    for (int i = 0; i < 14; i++) { FR(0x95c + 4 * i) = g->point_y_value[i]; FR(0x994 + 4 * i) = g->point_y_scaling[i]; }
    FR(0x9cc) = g->chroma_scaling_from_luma; FR(0x9d0) = g->num_cb_points; FR(0x9d4) = g->num_cr_points;
    for (int i = 0; i < 10; i++) { FR(0x9d8 + 4 * i) = g->point_cb_value[i]; FR(0xa00 + 4 * i) = g->point_cb_scaling[i]; FR(0xa28 + 4 * i) = g->point_cr_value[i]; FR(0xa50 + 4 * i) = g->point_cr_scaling[i]; }
    FR(0xa78) = g->grain_scaling_minus_8; FR(0xa7c) = g->ar_coeff_lag;
    for (int i = 0; i < 24; i++) FR(0xa80 + 4 * i) = g->ar_coeffs_y_plus_128[i];
    for (int i = 0; i < 25; i++) { FR(0xae0 + 4 * i) = g->ar_coeffs_cb_plus_128[i]; FR(0xb44 + 4 * i) = g->ar_coeffs_cr_plus_128[i]; }
    FR(0xba8) = g->ar_coeff_shift_minus_6; FR(0xbac) = g->grain_scale_shift; FR(0xbb0) = g->cb_mult; FR(0xbb4) = g->cb_luma_mult;
    FR(0xbb8) = g->cb_offset; FR(0xbbc) = g->cr_mult; FR(0xbc0) = g->cr_luma_mult; FR(0xbc4) = g->cr_offset;
    FR(0xbc8) = g->overlap_flag; FR(0xbcc) = g->clip_to_restricted_range;
}

