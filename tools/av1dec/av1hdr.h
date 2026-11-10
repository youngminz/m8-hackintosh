// AV1 OBU / sequence header / frame header parser (AV1 spec section 5), enough to drive a hardware
// decoder: every syntax element of the uncompressed header plus tile data offsets.
#pragma once
#include <stdint.h>
#include <stddef.h>

enum { AV1_KEY = 0, AV1_INTER = 1, AV1_INTRA_ONLY = 2, AV1_SWITCH = 3 };
enum { AV1_REFS = 7, AV1_NUM_REF = 8, AV1_MAX_TILES = 256, AV1_PRIMARY_NONE = 7 };

typedef struct {
    int profile, still_picture, reduced_still_picture_header;
    int timing_info_present, equal_picture_interval, decoder_model_info_present;
    int buffer_delay_length, buffer_removal_time_length, frame_presentation_time_length;
    int operating_points_cnt, operating_point_idc[32], seq_level_idx[32], seq_tier[32], decoder_model_present_for_op[32];
    int frame_width_bits, frame_height_bits, max_frame_width, max_frame_height;
    int frame_id_numbers_present, delta_frame_id_length, additional_frame_id_length;
    int use_128x128_superblock, enable_filter_intra, enable_intra_edge_filter, enable_interintra_compound,
        enable_masked_compound, enable_warped_motion, enable_dual_filter, enable_order_hint, enable_jnt_comp,
        enable_ref_frame_mvs, seq_force_screen_content_tools, seq_force_integer_mv, order_hint_bits,
        enable_superres, enable_cdef, enable_restoration, film_grain_params_present;
    int bit_depth, mono_chrome, color_description_present, color_primaries, transfer_characteristics,
        matrix_coefficients, color_range, subsampling_x, subsampling_y, chroma_sample_position, separate_uv_delta_q,
        num_planes;
} Av1Seq;

typedef struct {
    int apply_grain, grain_seed, update_grain, film_grain_params_ref_idx;
    int num_y_points, point_y_value[14], point_y_scaling[14], chroma_scaling_from_luma;
    int num_cb_points, point_cb_value[10], point_cb_scaling[10], num_cr_points, point_cr_value[10], point_cr_scaling[10];
    int grain_scaling_minus_8, ar_coeff_lag, ar_coeffs_y_plus_128[24], ar_coeffs_cb_plus_128[25], ar_coeffs_cr_plus_128[25];
    int ar_coeff_shift_minus_6, grain_scale_shift, cb_mult, cb_luma_mult, cb_offset, cr_mult, cr_luma_mult, cr_offset;
    int overlap_flag, clip_to_restricted_range;
} Av1FilmGrain;

typedef struct {
    int show_existing_frame, frame_to_show_map_idx, frame_type, frame_is_intra, show_frame, showable_frame,
        error_resilient_mode, disable_cdf_update, allow_screen_content_tools, force_integer_mv, current_frame_id,
        frame_size_override_flag, order_hint, primary_ref_frame, refresh_frame_flags;
    int ref_order_hint[AV1_NUM_REF];
    int frame_width, frame_height, upscaled_width, render_width, render_height, use_superres, superres_denom,
        mi_cols, mi_rows;
    int allow_intrabc, ref_frame_idx[AV1_REFS], allow_high_precision_mv, interpolation_filter,
        is_motion_mode_switchable, use_ref_frame_mvs, disable_frame_end_update_cdf;
    int order_hints[AV1_NUM_REF];  // OrderHints[LAST_FRAME + i] at index 1 + i
    // tile info
    int uniform_tile_spacing_flag, tile_cols, tile_rows, tile_cols_log2, tile_rows_log2, context_update_tile_id,
        tile_size_bytes;
    int mi_col_starts[65], mi_row_starts[65];
    // quantization
    int base_q_idx, delta_q_y_dc, delta_q_u_dc, delta_q_u_ac, delta_q_v_dc, delta_q_v_ac, using_qmatrix, qm_y, qm_u, qm_v;
    // segmentation
    int segmentation_enabled, segmentation_update_map, segmentation_temporal_update, segmentation_update_data;
    int feature_enabled[8][8], feature_data[8][8], seg_id_pre_skip, last_active_seg_id;
    int delta_q_present, delta_q_res, delta_lf_present, delta_lf_res, delta_lf_multi;
    int coded_lossless, all_lossless;
    // loop filter
    int loop_filter_level[4], loop_filter_sharpness, loop_filter_delta_enabled, loop_filter_delta_update;
    int loop_filter_ref_deltas[8], loop_filter_mode_deltas[2];
    // cdef
    int cdef_damping, cdef_bits, cdef_y_pri_strength[8], cdef_y_sec_strength[8], cdef_uv_pri_strength[8], cdef_uv_sec_strength[8];
    // loop restoration
    int lr_type[3], uses_lr, lr_unit_shift, lr_uv_shift, loop_restoration_size[3];
    int tx_mode, reference_select, skip_mode_allowed, skip_mode_present, skip_mode_frame[2], allow_warped_motion, reduced_tx_set;
    int gm_type[AV1_NUM_REF];
    int32_t gm_params[AV1_NUM_REF][6];
    Av1FilmGrain film_grain;
    // tile data: offsets from the start of the buffer handed to av1_parse_temporal_unit
    int num_tiles_found;
    uint32_t tile_offset[AV1_MAX_TILES], tile_size[AV1_MAX_TILES];
    uint32_t frame_header_offset, frame_header_bytes;  // OBU payload of the frame header (for decoders that want it)
} Av1Frame;

typedef struct {
    int valid, frame_id, upscaled_width, frame_width, frame_height, render_width, render_height, frame_type, order_hint;
    int saved_order_hints[AV1_NUM_REF];
    int32_t gm_params[AV1_NUM_REF][6];
    int loop_filter_ref_deltas[8], loop_filter_mode_deltas[2];
    int feature_enabled[8][8], feature_data[8][8];
    Av1FilmGrain film_grain;
} Av1Ref;

typedef struct {
    Av1Seq seq;
    int have_seq;
    Av1Ref ref[AV1_NUM_REF];
    int temporal_id, spatial_id;
    int seen_frame_header;
    Av1Frame cur;
} Av1Parser;

// Parses the OBUs of one temporal unit / sample (low overhead format, obu_has_size_field = 1).
// Calls cb for every frame to decode or show (frame header with its tile groups complete, or
// show_existing_frame). Returns 0, or a negative error.
typedef void (*Av1FrameCb)(void *ctx, Av1Parser *p, const Av1Frame *f, const uint8_t *data);
int av1_parse_temporal_unit(Av1Parser *p, const uint8_t *data, size_t size, Av1FrameCb cb, void *ctx);
// Reference update process (7.20) for the frame just handed to cb; call after decoding it.
void av1_update_refs(Av1Parser *p, const Av1Frame *f);
