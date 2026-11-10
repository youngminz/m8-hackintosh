// VP9 비압축 헤더(uncompressed header, VP9 규격 6.2) 파서. 하드웨어 디코더를 구동하는 데 필요한 만큼:
// 비압축 헤더의 모든 구문 요소, 프레임 사이에 유지되는 상태, 슈퍼프레임 색인 분해.
// 표준 C99만 쓴다(플랫폼 의존 기능 없음).
#pragma once
#include <stdint.h>
#include <stddef.h>

enum { VP9_KEY_FRAME = 0, VP9_NON_KEY_FRAME = 1 };
enum { VP9_NUM_REF = 8, VP9_REFS_PER_FRAME = 3, VP9_MAX_SEGMENTS = 8, VP9_SEG_LVL_MAX = 4, VP9_MAX_SUPERFRAME = 8 };
// 세그먼트 특징 번호
enum { VP9_SEG_ALT_Q = 0, VP9_SEG_ALT_L = 1, VP9_SEG_REF_FRAME = 2, VP9_SEG_SKIP = 3 };
// interpolation_filter 값(규격의 literal_to_type 적용 뒤, libvpx와 VAAPI의 mcomp_filter_type과 같은 번호)
enum { VP9_EIGHTTAP = 0, VP9_EIGHTTAP_SMOOTH = 1, VP9_EIGHTTAP_SHARP = 2, VP9_BILINEAR = 3, VP9_SWITCHABLE = 4 };
enum { VP9_CS_RGB = 7 };
// 오류 값
enum {
    VP9_ERR_TRUNCATED = -1,      // 데이터가 모자람
    VP9_ERR_FRAME_MARKER = -2,   // frame_marker가 2가 아님
    VP9_ERR_RESERVED = -3,       // reserved_zero가 0이 아님
    VP9_ERR_SYNC_CODE = -4,      // frame_sync_code 불일치
    VP9_ERR_REF_MISSING = -5,    // 참조하려는 슬롯이 비어 있음
    VP9_ERR_HEADER_SIZE = -6,    // header_size_in_bytes가 0이거나 프레임보다 큼
};

typedef struct {
    // --- 프레임 머리 ---
    int profile;                  // 0..3
    int show_existing_frame, frame_to_show_map_idx;
    int frame_type;               // VP9_KEY_FRAME / VP9_NON_KEY_FRAME
    int show_frame, error_resilient_mode;
    int intra_only;               // 키 프레임이면 0
    int frame_is_intra;           // FrameIsIntra = 키 프레임 || intra_only
    int reset_frame_context;      // 부호화되지 않으면 0
    // --- color_config (부호화되지 않은 프레임에서는 유지되는 값) ---
    int bit_depth, color_space, color_range, subsampling_x, subsampling_y;
    // --- 참조 ---
    int refresh_frame_flags;      // 키 프레임이면 0xff
    int ref_frame_idx[VP9_REFS_PER_FRAME];     // LAST, GOLDEN, ALTREF 순. 인터 프레임에서만 의미 있음(아니면 0)
    int ref_frame_sign_bias[VP9_REFS_PER_FRAME]; // 같은 순서. 비트스트림에 적힌 값 그대로
    // --- 프레임 크기 ---
    int found_ref;                // 0: 크기를 직접 부호화, 1..3: ref_frame_idx[found_ref - 1]의 크기를 물려받음
    int frame_width, frame_height;
    int render_and_frame_size_different, render_width, render_height;
    int mi_cols, mi_rows, sb64_cols, sb64_rows;
    int size_changed;             // 직전에 디코드한 프레임과 크기가 다름(LastWidth, LastHeight 기준)
    // --- 인터 예측 ---
    int allow_high_precision_mv;
    int is_filter_switchable, raw_interpolation_filter;
    int interpolation_filter;     // VP9_EIGHTTAP.. / VP9_SWITCHABLE
    // --- 문맥 ---
    int refresh_frame_context, frame_parallel_decoding_mode;
    int frame_context_idx;        // 비트스트림에 적힌 값 그대로(규격은 인트라나 error_resilient이면 0으로 덮는다)
    // --- 루프 필터 ---
    int loop_filter_level, loop_filter_sharpness;
    int loop_filter_delta_enabled, loop_filter_delta_update;
    int update_ref_delta[4], update_mode_delta[2];
    int loop_filter_ref_deltas[4], loop_filter_mode_deltas[2];  // 이 프레임에 적용되는 값(유지 규칙 반영 뒤)
    // --- 양자화 ---
    int base_q_idx, delta_q_y_dc, delta_q_uv_dc, delta_q_uv_ac;
    int lossless;
    // --- 세그멘테이션 ---
    int segmentation_enabled, segmentation_update_map, segmentation_temporal_update, segmentation_update_data;
    int segmentation_tree_probs[7];  // update_map이 1일 때만 새로 정해진다(부호화 안 된 항목은 255). 아니면 255
    int segmentation_pred_probs[3];  // temporal_update가 1일 때만 부호화된다. 아니면 255
    int segmentation_abs_or_delta_update;                         // 유지 규칙 반영 뒤의 값
    int feature_enabled[VP9_MAX_SEGMENTS][VP9_SEG_LVL_MAX];       // 유지 규칙 반영 뒤의 값
    int feature_data[VP9_MAX_SEGMENTS][VP9_SEG_LVL_MAX];          // 유지 규칙 반영 뒤의 값(꺼진 특징은 0)
    // --- 타일 ---
    int min_log2_tile_cols, max_log2_tile_cols, tile_cols_log2, tile_rows_log2;
    // --- 크기 ---
    int header_size_in_bytes;       // 압축 헤더(compressed header) 크기
    int uncompressed_header_size;   // 비압축 헤더의 바이트 크기(trailing_bits 포함). 압축 헤더는 프레임의 이 오프셋에서 시작
    uint32_t frame_size;            // 파서에 넘긴 프레임 전체 크기
} Vp9Frame;

typedef struct {
    int valid, frame_width, frame_height, bit_depth, subsampling_x, subsampling_y;
} Vp9Ref;

// 프레임 사이에 유지되는 상태
typedef struct {
    Vp9Ref ref[VP9_NUM_REF];        // 참조 슬롯 8개의 크기와 비트 깊이
    int profile, bit_depth, color_space, color_range, subsampling_x, subsampling_y;  // 마지막 color_config
    int loop_filter_ref_deltas[4], loop_filter_mode_deltas[2];
    int segmentation_abs_or_delta_update;
    int feature_enabled[VP9_MAX_SEGMENTS][VP9_SEG_LVL_MAX], feature_data[VP9_MAX_SEGMENTS][VP9_SEG_LVL_MAX];
    int last_width, last_height;    // 직전에 디코드한 프레임의 크기(LastWidth, LastHeight)
    int last_show_frame;            // 직전에 디코드한 프레임의 show_frame
    int last_intra_only;            // 직전에 디코드한 프레임의 intra_only
    int frames_decoded;
} Vp9Parser;

// 파서 상태 초기화(스트림 시작이나 탐색 뒤에 부른다).
void vp9_parser_init(Vp9Parser *p);

// 프레임 하나(슈퍼프레임이면 vp9_split_superframe으로 나눈 뒤의 한 프레임)의 비압축 헤더를 파싱한다.
// 0 또는 음수 오류(VP9_ERR_*)를 돌려준다. 루프 필터 델타와 세그멘테이션 값의 유지 규칙은 이 함수 안에서
// p에 반영된다. 참조 슬롯 갱신은 vp9_update_refs가 한다.
int vp9_parse_frame_header(Vp9Parser *p, const uint8_t *data, size_t size, Vp9Frame *f);

// 참조 갱신 과정(규격 8.10): 방금 파싱한 프레임을 디코드한 뒤에 부른다.
// refresh_frame_flags에 해당하는 슬롯에 크기와 비트 깊이를 기록하고 LastWidth 등을 갱신한다.
// show_existing_frame이면 아무것도 바꾸지 않는다.
void vp9_update_refs(Vp9Parser *p, const Vp9Frame *f);

// 슈퍼프레임 색인(규격 부록 B) 분해. mp4나 ivf의 한 샘플이 프레임 여러 개(보통 숨은 프레임 + 보이는 프레임)를
// 담을 수 있다. 프레임 수(1..8)를 돌려주고 offset[i], frame_size[i]에 샘플 안에서의 위치를 쓴다.
// 색인이 없으면 샘플 전체를 프레임 하나로 본다(1을 돌려줌). 색인이 깨졌으면 0. 크기 0인 항목은 건너뛴다.
int vp9_split_superframe(const uint8_t *data, size_t size, uint32_t offset[VP9_MAX_SUPERFRAME],
                         uint32_t frame_size[VP9_MAX_SUPERFRAME]);
