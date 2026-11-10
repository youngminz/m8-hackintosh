# AvdVcnAv1DecDataProcessor 입력 구조체(AV1 그림 매개변수) 역분석

대상: macOS 15.8.2 x86_64 dyld 공유 캐시 안 `AMDRadeonVADriver2`. 정적 디스어셈블(`tools/re/dsc.py`)만 사용했고 M8에서는 아무것도 실행하지 않았다.

Mesa 참조: `src/amd/common/ac_vcn_dec.h`(main, chaotic-cx 미러에서 받음)와 `src/gallium/drivers/radeonsi/radeon_vcn_dec.c`(mesa-23.3.6 태그, gitlab.freedesktop.org에서 받음). 과제에 적힌 `drivers/radeon/` 경로는 지금 404이고 파일이 위 두 경로로 옮겨져 있다. Mesa 구조체 오프셋은 헤더를 직접 컴파일해 `offsetof`로 뽑았다(기억에 의존하지 않음).

## 0. 결론 요약

- 입력은 DXVA_PicParams_AV1이 아니다. 포인터 세 개를 가진 작은 최상위 구조체이고, 그 포인터들이 가리키는 것은 AV1 규격의 구문 요소를 규격 등장 순서대로 전부 `uint32_t` 하나씩으로 펼친 "시퀀스 헤더 구조체"와 "프레임 헤더 구조체", 그리고 "타일과 비트스트림 구조체"다. AMD 내부 파서의 출력 형식으로 보인다.
- 드라이버가 만드는 AV1 메시지는 Mesa `rvcn_dec_message_av1_t`와 오프셋이 전부 일치한다(크기 0xdf8, 즉 Mesa 구조체에서 맨 끝 `reserved[3]`과 `av1_intrabc_workaround`만 없는 판).
- 참조 프레임은 표면 포인터가 아니라 드라이버 내부 DPB 버퍼의 칸 번호(바이트)로 넘긴다.
- CDF 문맥은 호출자가 관리하지 않는다. 드라이버가 문맥 버퍼를 만들 때 기본 확률 표 4벌을 써 넣고, 이후 갱신과 저장은 펌웨어가 문맥 버퍼 안에서 한다.
- `ProcessPictureDecodeParam`은 AV1에서 아무 일도 하지 않고 1을 돌려준다. 따로 설정할 매개변수는 없다.

## 1. 호출 경로와 인자

```
VADecoder::DecodePicture(void *param, VAVendorTexture *bitstreamTex, VAVendorTexture *targetTex)   0x7ffb0860a2c2
  -> processor->needDecodeAndCopy(param, targetTex, &rect)         vtable+0x50 (caps[0x96] bit0 일 때만)
  -> processor->Process(param, bitstreamTex, targetTex, internalBitstreamTex, &buildInfo)  vtable+0x20
       -> SetBitstreamFields(param, bitstreamTex, internalBitstreamTex, info)   vtable+0x68  0x7ffb0864564e
       -> SetDecodeMessageFields(param, targetTex, info, 0, 0)                  vtable+0x70  0x7ffb08645728
       -> SetDecodeFeedbackFields(info)                                         vtable+0x78  0x7ffb08617e36
  -> ctx[0xc]==1 이면 DPB(ctx+0x38)와 문맥 버퍼(ctx+0x1d0)를 다시 만들고, 종류 6이면 initHWContext() 호출
  -> 명령 제출, 내부 버퍼 고리(비트스트림 8개, 메시지 8개, 피드백 16개, seg/fg 16개) 다음 칸으로 전진
```

AV1 처리기 vtable(`__ZTV25AvdVcnAv1DecDataProcessor`, 0x7ffb412b0a60): +0x10 ProcessCreateDecodeSession, +0x18 ProcessDestroyDecodeSession, +0x20 Process, +0x28 ProcessPictureDecodeParam, +0x30 ProcessDecodeStatus(기반), +0x38 getMacroblockQPs(기반), +0x40 CalculateDPBSize, +0x48 CalculateContextSize, +0x50 needDecodeAndCopy(기반), +0x58 getDecodeTargetArea, +0x60 initHWContext, +0x68 SetBitstreamFields, +0x70 SetDecodeMessageFields, +0x78 SetDecodeFeedbackFields(기반).

입력 `param`을 읽는 곳은 `SetBitstreamFields`, `SetDecodeMessageFields`, `getDecodeTargetArea` 셋뿐이다. 따라서 아래 "읽음"으로 표시한 필드만 채우면 되고 나머지는 0으로 두어도 드라이버 동작에는 영향이 없다.

## 2. 입력 구조체의 C 정의

표기: `[확실]`은 디스어셈블에서 읽는 것을 직접 확인했고 Mesa 메시지 필드와 대응이 맞은 것, `[추정]`은 드라이버가 읽지 않아서 규격 순서와 주변 오프셋 간격으로 짐작한 것이다.

### 2.1 최상위 구조체 (크기 최소 0x68)

```c
struct AvdAv1DecodeParam {
    struct AvdAv1SeqHdr   *seq;        // 0x00 [확실] 시퀀스 헤더 구조체 포인터
    struct AvdAv1FrameHdr *frame;      // 0x08 [확실] 프레임 헤더 구조체 포인터 (드라이버가 일부 필드에 써 넣으므로 쓰기 가능해야 함)
    uint8_t  curr_pic_idx;             // 0x10 [확실] 현재 프레임이 들어갈 DPB 칸 번호 -> msg.curr_pic_idx
    uint8_t  pad11[0x0f];              // 0x11 읽지 않음
    uint32_t ref_frame_map[8];         // 0x20 [확실] ref_frame_map 각 칸의 DPB 칸 번호(하위 바이트만 사용) -> msg.ref_frame_map[8]
    uint32_t frame_refs[7];            // 0x40 [확실] LAST..ALTREF 7개 참조의 DPB 칸 번호(하위 바이트만 사용) -> msg.frame_refs[7]
    uint32_t pad5c;                    // 0x5c 읽지 않음
    struct AvdAv1TileData *tiles;      // 0x60 [확실] 타일과 비트스트림 구조체 포인터
};
```

### 2.2 타일과 비트스트림 구조체

```c
struct AvdAv1TileData {
    uint8_t  unk00[0x10];              // 0x00 읽지 않음 (내용 모름)
    uint32_t bitstream_size;           // 0x10 [확실] 비트스트림 바이트 수 (memcpy 길이, msg.bsd_size)
    struct { uint32_t offset, size; } tile[/*TileRows*/][64];   // 0x14 [확실] 행 간격 0x200(=64칸*8바이트)
};  // 최대 0x14 + 64*0x200 = 0x8014
```

### 2.3 시퀀스 헤더 구조체 (모든 필드 uint32_t, 크기 최소 0x534, 추정 0x55c)

```c
struct AvdAv1SeqHdr {
    uint32_t seq_profile;                      // 0x000 [확실] -> msg.profile, 재할당 판정 키
    uint32_t still_picture;                    // 0x004 [추정]
    uint32_t reduced_still_picture_header;     // 0x008 [추정]
    uint32_t timing_info_present_flag;         // 0x00c [추정]
    uint32_t timing_info[4];                   // 0x010 [추정] num_units_in_display_tick, time_scale, equal_picture_interval, num_ticks_per_picture_minus_1
    uint32_t decoder_model_info_present_flag;  // 0x020 [추정]
    uint32_t decoder_model_info[4];            // 0x024 [추정]
    uint32_t initial_display_delay_present_flag; // 0x034 [추정]
    uint32_t operating_points_cnt_minus_1;     // 0x038 [추정]
    uint32_t operating_point_fields[9][32];    // 0x03c [추정] 동작점 32개 x 필드 9개 = 0x480바이트 (배열 묶는 방식은 모름)
    uint32_t frame_width_bits_minus_1;         // 0x4bc [추정]
    uint32_t frame_height_bits_minus_1;        // 0x4c0 [추정]
    uint32_t max_frame_width_minus_1;          // 0x4c4 [확실] +1 -> msg.max_width
    uint32_t max_frame_height_minus_1;         // 0x4c8 [확실] +1 -> msg.max_height
    uint32_t frame_id_numbers_present_flag;    // 0x4cc [추정]
    uint32_t delta_frame_id_length_minus_2;    // 0x4d0 [추정]
    uint32_t additional_frame_id_length_minus_1; // 0x4d4 [추정]
    uint32_t use_128x128_superblock;           // 0x4d8 [확실] -> msg.sb_size, 타일 시작 단위 변환
    uint32_t enable_filter_intra;              // 0x4dc [확실] 플래그 비트 9
    uint32_t enable_intra_edge_filter;         // 0x4e0 [확실] 비트 10
    uint32_t enable_interintra_compound;       // 0x4e4 [확실] 비트 11
    uint32_t enable_masked_compound;           // 0x4e8 [확실] 비트 12
    uint32_t enable_warped_motion;             // 0x4ec [추정] 읽지 않음
    uint32_t enable_dual_filter;               // 0x4f0 [확실] 비트 14
    uint32_t enable_order_hint;                // 0x4f4 [확실] 비트 15
    uint32_t enable_jnt_comp;                  // 0x4f8 [확실] 비트 16
    uint32_t enable_ref_frame_mvs;             // 0x4fc [확실] 0이면 비트 31(DISABLE_REF_FRAME_MVS)
    uint32_t seq_choose_screen_content_tools;  // 0x500 [추정]
    uint32_t seq_force_screen_content_tools;   // 0x504 [추정]
    uint32_t seq_choose_integer_mv;            // 0x508 [추정]
    uint32_t seq_force_integer_mv;             // 0x50c [추정]
    uint32_t order_hint_bits_minus_1;          // 0x510 [추정]
    uint32_t OrderHintBits;                    // 0x514 [확실] -> msg.order_hint_bits (minus_1이 아니라 실제 비트 수)
    uint32_t enable_superres;                  // 0x518 [추정]
    uint32_t enable_cdef;                      // 0x51c [추정]
    uint32_t enable_restoration;               // 0x520 [추정]
    uint32_t high_bitdepth;                    // 0x524 [확실] 0이면 8비트
    uint32_t twelve_bit;                       // 0x528 [확실] high_bitdepth!=0일 때 0이면 10비트, 아니면 12비트
    uint32_t BitDepth;                         // 0x52c [추정] 읽지 않음
    uint32_t mono_chrome;                      // 0x530 [확실] 비트 6, msg.chroma_format = (mono_chrome==0)
    uint32_t color_config_rest[10];            // 0x534 [추정] color_description_present_flag ... film_grain_params_present
};
```

0x3c에서 0x4bc까지 간격이 정확히 9*32*4바이트이고 그 뒤 필드들이 규격 순서와 한 칸도 어긋나지 않으므로, 읽지 않는 필드의 추정도 꽤 믿을 만하다.

### 2.4 프레임 헤더 구조체 (모든 스칼라 uint32_t, 크기 최소 0xbd0)

```c
struct AvdAv1FrameHdr {
    uint32_t show_existing_frame;        // 0x000 [추정]
    uint32_t unk004[3];                  // 0x004 [추정] frame_to_show_map_idx 등
    uint32_t frame_type;                 // 0x010 [확실] -> msg.frame_type (0 KEY, 1 INTER, 2 INTRA_ONLY, 3 SWITCH)
    uint32_t unk014;                     // 0x014
    uint32_t show_frame;                 // 0x018 [확실] 비트 0
    uint32_t showable_frame;             // 0x01c [추정]
    uint32_t error_resilient_mode;       // 0x020 [추정]
    uint32_t disable_cdf_update;         // 0x024 [확실] 비트 1
    uint32_t allow_screen_content_tools; // 0x028 [확실] 비트 18
    uint32_t force_integer_mv;           // 0x02c [확실] 비트 19
    uint32_t current_frame_id;           // 0x030 [확실] -> msg.current_frame_id
    uint32_t unk034[3];                  // 0x034 [추정] frame_size_override_flag 등
    uint32_t order_hint;                 // 0x040 [확실] -> msg.frame_offset
    uint32_t unk044[8];                  // 0x044
    uint32_t primary_ref_frame;          // 0x064 [확실] -> msg.primary_ref_frame
    uint32_t unk068[33];                 // 0x068 [추정] buffer_removal_time_present_flag + buffer_removal_time[32]
    uint32_t refresh_frame_flags;        // 0x0ec [확실] 0이면 비트 30(SKIP_REFERENCE_UPDATE)
    uint32_t unk0f0[17];                 // 0x0f0 [추정] ref_order_hint[8] 등
    uint32_t frame_width_minus_1;        // 0x134 [확실] +1이 재할당 판정 키와 getDecodeTargetArea의 폭
    uint32_t FrameWidth;                 // 0x138 [확실] -> msg.width (초해상도 쓰면 축소된 폭)
    uint32_t frame_height_minus_1;       // 0x13c [확실] +1이 높이
    uint32_t FrameHeight;                // 0x140 [확실] -> msg.height
    uint32_t UpscaledWidth;              // 0x144 [확실] -> msg.superres_upscaled_width
    uint32_t use_superres;               // 0x148 [추정]
    uint32_t SuperresDenom;              // 0x14c [확실] -> msg.superres_scale_denominator
    uint32_t unk150[7];                  // 0x150 [추정] render_and_frame_size_different, render 크기 등
    uint32_t allow_intrabc;              // 0x16c [확실] 비트 4
    uint32_t unk170[18];                 // 0x170 [추정] frame_refs_short_signaling, last/gold_frame_idx, ref_frame_idx[7], delta_frame_id_minus_1[7] 등
    uint32_t allow_high_precision_mv;    // 0x1b8 [확실] 비트 5
    uint32_t is_filter_switchable;       // 0x1bc [추정]
    uint32_t interpolation_filter;       // 0x1c0 [확실] -> msg.interp_filter
    uint32_t is_motion_mode_switchable;  // 0x1c4 [확실] 비트 29
    uint32_t use_ref_frame_mvs;          // 0x1c8 [확실] 비트 17
    uint32_t disable_frame_end_update_cdf; // 0x1cc [확실] 0이면 비트 2(REFRESH_FRAME_CONTEXT)
    uint32_t uniform_tile_spacing_flag;  // 0x1d0 [추정]
    uint32_t TileColsLog2;               // 0x1d4 [확실] msg.tile_cols = 1 << 이 값
    uint32_t TileRowsLog2;               // 0x1d8 [확실] msg.tile_rows = 1 << 이 값
    uint32_t unk1dc[2];                  // 0x1dc
    uint32_t MiColStarts[65];            // 0x1e4 [확실] 4x4 단위(MI). 드라이버가 올림하여 SB 단위로 바꿔 msg.tile_col_start_sb
    uint32_t MiRowStarts[65];            // 0x2e8 [확실] 위와 같음 -> msg.tile_row_start_sb
    uint32_t TileCols;                   // 0x3ec [확실] 타일 표 복사 때 열 수
    uint32_t TileRows;                   // 0x3f0 [확실] 타일 표 복사 때 행 수
    uint32_t unk3f4[2];                  // 0x3f4
    uint32_t context_update_tile_id;     // 0x3fc [확실] -> msg.context_update_tile_id
    uint32_t unk400[2];                  // 0x400 [추정] tile_size_bytes_minus_1 등
    uint32_t base_q_idx;                 // 0x408 [확실] -> msg.base_qindex
    uint32_t unk40c[2];                  // 0x40c
    int32_t  DeltaQYDc;                  // 0x414 [확실] -> msg.y_dc_delta_q
    uint32_t diff_uv_delta;              // 0x418 [추정]
    int32_t  DeltaQUDc;                  // 0x41c [확실] -> msg.u_dc_delta_q
    int32_t  DeltaQUAc;                  // 0x420 [확실] -> msg.u_ac_delta_q
    int32_t  DeltaQVDc;                  // 0x424 [확실] -> msg.v_dc_delta_q
    int32_t  DeltaQVAc;                  // 0x428 [확실] -> msg.v_ac_delta_q
    uint32_t using_qmatrix;              // 0x42c [확실] 0이면 드라이버가 아래 qm 셋에 0xff를 써 넣음
    uint32_t qm_y;                       // 0x430 [확실] 하위 바이트 -> msg.qm_y, (값 != 0xff)가 비트 8
    uint32_t qm_u;                       // 0x434 [확실] -> msg.qm_u
    uint32_t qm_v;                       // 0x438 [확실] -> msg.qm_v
    uint32_t segmentation_enabled;       // 0x43c [확실] 비트 25
    uint32_t segmentation_update_map;    // 0x440 [확실] 비트 26
    uint32_t segmentation_temporal_update; // 0x444 [확실] 비트 27
    uint32_t segmentation_update_data;   // 0x448 [추정]
    uint32_t unk44c;                     // 0x44c
    uint32_t FeatureEnabled[8][8];       // 0x450 [확실] 0 아니면 켜짐 -> msg.feature_mask[seg]의 비트 j
    uint32_t unk550;                     // 0x550
    int16_t  FeatureData[8][8];          // 0x554 [확실] int16 -> msg.feature_data[8][8](int로 부호 확장), seg/fg 버퍼 앞 128바이트로도 그대로 복사
    uint32_t unk5d4[2];                  // 0x5d4 [추정] SegIdPreSkip, LastActiveSegId (드라이버는 읽지 않고 직접 계산)
    uint32_t delta_q_present;            // 0x5dc [확실] 비트 22
    uint32_t delta_q_res;                // 0x5e0 [확실] log2 값. msg.delta_q_res = 1 << 이 값
    uint32_t delta_lf_present;           // 0x5e4 [확실] 비트 23
    uint32_t delta_lf_res;               // 0x5e8 [확실] log2 값. msg.delta_lf_res = 1 << 이 값
    uint32_t delta_lf_multi;             // 0x5ec [확실] 비트 28
    uint32_t unk5f0[34];                 // 0x5f0 (CodedLossless, LosslessArray, SegQMLevel 따위로 짐작, 모름)
    uint32_t loop_filter_level[2];       // 0x678 [확실] -> msg.filter_level[0..1]
    uint32_t loop_filter_level_u;        // 0x680 [확실] -> msg.filter_level_u
    uint32_t loop_filter_level_v;        // 0x684 [확실] -> msg.filter_level_v
    uint32_t loop_filter_sharpness;      // 0x688 [확실] -> msg.sharpness_level
    uint32_t loop_filter_delta_enabled;  // 0x68c [확실] 비트 20
    uint32_t loop_filter_delta_update;   // 0x690 [확실] 비트 21
    uint32_t unk694;                     // 0x694
    int32_t  loop_filter_ref_deltas[8];  // 0x698 [확실] -> msg.ref_deltas[8]
    uint32_t unk6b8;                     // 0x6b8
    int32_t  loop_filter_mode_deltas[2]; // 0x6bc [확실] -> msg.mode_deltas[2]
    uint32_t cdef_damping_minus_3;       // 0x6c4 [추정] 읽지 않음
    uint32_t cdef_bits;                  // 0x6c8 [확실] -> msg.cdef_bits
    uint32_t cdef_y_lo[8];               // 0x6cc [확실] msg.cdef_strengths[i] = cdef_y_lo[i] | (cdef_y_hi[i] << 6)
    uint32_t cdef_y_hi[8];               // 0x6ec [확실] (이름은 추정: lo는 pri_strength, hi는 sec_strength로 보임)
    uint32_t cdef_uv_lo[8];              // 0x70c [확실] msg.cdef_uv_strengths[i] = cdef_uv_lo[i] | (cdef_uv_hi[i] << 6)
    uint32_t cdef_uv_hi[8];              // 0x72c [확실]
    uint32_t CdefDamping;                // 0x74c [확실] -> msg.cdef_damping (minus_3이 아닌 실제 값 3..6)
    uint32_t FrameRestorationType[3];    // 0x750 [확실] -> msg.frame_restoration_type[3]
    uint32_t unk75c[4];                  // 0x75c [추정] lr_unit_shift 등
    uint32_t LoopRestorationSize[3];     // 0x76c [확실] 화소 단위 크기. 0이 아니면 msg.log2_restoration_unit_size_minus5 = log2(값) - 5
    uint32_t unk778[2];                  // 0x778 [추정] tx_mode_select 등
    uint32_t TxMode;                     // 0x780 [확실] -> msg.tx_mode
    uint32_t reference_select;           // 0x784 [확실] 1이면 msg.reference_mode = 2, 아니면 0
    uint32_t unk788[2];                  // 0x788
    uint32_t skip_mode_present;          // 0x790 [확실] 비트 7
    uint32_t allow_warped_motion;        // 0x794 [확실] 비트 13
    uint32_t reduced_tx_set;             // 0x798 [확실] 비트 24
    uint32_t gm_type[8];                 // 0x79c [확실, 단 [0]은 읽지 않음] gm_type[1..7] -> msg.global_motion[1..7].wmtype
    int32_t  gm_params[8][6];            // 0x7bc [확실, 단 [0]은 읽지 않음] gm_params[1..7][0..5] -> msg.global_motion[1..7].wmmat[0..5]
    uint32_t unk87c[51];                 // 0x87c 모름 (읽지 않음)
    uint32_t apply_grain;                // 0x948 [확실] -> film_grain.apply_grain. 0이면 아래는 읽지 않음
    uint32_t grain_seed;                 // 0x94c [확실] 16비트 -> film_grain.random_seed
    uint32_t update_grain;               // 0x950 [추정]
    uint32_t film_grain_params_ref_idx;  // 0x954 [추정]
    uint32_t num_y_points;               // 0x958 [확실]
    uint32_t point_y_value[14];          // 0x95c [확실] -> scaling_points_y[i][0]
    uint32_t point_y_scaling[14];        // 0x994 [확실] -> scaling_points_y[i][1]
    uint32_t chroma_scaling_from_luma;   // 0x9cc [확실]
    uint32_t num_cb_points;              // 0x9d0 [확실]
    uint32_t num_cr_points;              // 0x9d4 [확실]
    uint32_t point_cb_value[10];         // 0x9d8 [확실]
    uint32_t point_cb_scaling[10];       // 0xa00 [확실]
    uint32_t point_cr_value[10];         // 0xa28 [확실]
    uint32_t point_cr_scaling[10];       // 0xa50 [확실]
    uint32_t grain_scaling_minus_8;      // 0xa78 [확실] +8 -> film_grain.scaling_shift
    uint32_t ar_coeff_lag;               // 0xa7c [확실]
    uint32_t ar_coeffs_y_plus_128[24];   // 0xa80 [확실] 드라이버가 128을 빼서 부호 있는 바이트로 저장
    uint32_t ar_coeffs_cb_plus_128[25];  // 0xae0 [확실]
    uint32_t ar_coeffs_cr_plus_128[25];  // 0xb44 [확실]
    uint32_t ar_coeff_shift_minus_6;     // 0xba8 [확실] +6 -> film_grain.ar_coeff_shift
    uint32_t grain_scale_shift;          // 0xbac [확실]
    uint32_t cb_mult;                    // 0xbb0 [확실]
    uint32_t cb_luma_mult;               // 0xbb4 [확실]
    uint32_t cb_offset;                  // 0xbb8 [확실] 16비트
    uint32_t cr_mult;                    // 0xbbc [확실]
    uint32_t cr_luma_mult;               // 0xbc0 [확실]
    uint32_t cr_offset;                  // 0xbc4 [확실] 16비트
    uint32_t overlap_flag;               // 0xbc8 [확실]
    uint32_t clip_to_restricted_range;   // 0xbcc [확실]
};  // 끝 0xbd0
```

### 2.5 전체 크기

- 최상위: 0x68 이상.
- 시퀀스 헤더: 읽는 마지막 필드가 0x530이므로 0x534 이상, 규격 순서대로면 0x55c 근처(추정).
- 프레임 헤더: 0xbd0 이상(필름 그레인이 맨 끝이라 이 값이 실제 크기일 가능성이 높다, 추정).
- 타일 구조체: 0x14 + TileRows*0x200, 최대 0x8014.

안전하게는 시퀀스 0x600, 프레임 0xc00, 타일 0x8014바이트를 0으로 채워 잡으면 된다.

## 3. 입력 오프셋에서 메시지 필드로 가는 대응 표

S는 시퀀스 구조체, F는 프레임 구조체, P는 최상위, T는 타일 구조체. 메시지 오프셋은 AV1 메시지(색인 종류 0x11, 크기 0xdf8) 기준이다.

### 3.1 frame_header_flags (msg 0x000, 조립 위치 0x7ffb08645aeb부터 0x7ffb08645c77)

| 비트 | Mesa 이름 | 입력 |
|---|---|---|
| 0 | SHOW_FRAME | F 0x018 |
| 1 | DISABLE_CDF_UPDATE | F 0x024 |
| 2 | REFRESH_FRAME_CONTEXT | F 0x1cc == 0 |
| 3 | INTRA_ONLY | 설정하지 않음 (Mesa는 frame_type==2일 때 설정) |
| 4 | ALLOW_INTRABC | F 0x16c |
| 5 | ALLOW_HIGH_PRECISION_MV | F 0x1b8 |
| 6 | MONOCHROME | S 0x530 |
| 7 | SKIP_MODE_FLAG | F 0x790 |
| 8 | USING_QMATRIX | F 0x430 != 0xff (F 0x42c가 0이면 0xff로 덮어쓴 뒤 판정) |
| 9 | ENABLE_FILTER_INTRA | S 0x4dc |
| 10 | ENABLE_INTRA_EDGE_FILTER | S 0x4e0 |
| 11 | ENABLE_INTERINTRA_COMPOUND | S 0x4e4 |
| 12 | ENABLE_MASKED_COMPOUND | S 0x4e8 |
| 13 | ALLOW_WARPED_MOTION | F 0x794 |
| 14 | ENABLE_DUAL_FILTER | S 0x4f0 |
| 15 | ENABLE_ORDER_HINT | S 0x4f4 |
| 16 | ENABLE_JNT_COMP | S 0x4f8 |
| 17 | ALLOW_REF_FRAME_MVS | F 0x1c8 |
| 18 | ALLOW_SCREEN_CONTENT_TOOLS | F 0x028 |
| 19 | CUR_FRAME_FORCE_INTEGER_MV | F 0x02c |
| 20 | MODE_REF_DELTA_ENABLED | F 0x68c |
| 21 | MODE_REF_DELTA_UPDATE | F 0x690 |
| 22 | DELTA_Q_PRESENT_FLAG | F 0x5dc |
| 23 | DELTA_LF_PRESENT_FLAG | F 0x5e4 |
| 24 | REDUCED_TX_SET_USED | F 0x798 |
| 25 | SEGMENTATION_ENABLED | F 0x43c |
| 26 | SEGMENTATION_UPDATE_MAP | F 0x440 |
| 27 | SEGMENTATION_TEMPORAL_UPDATE | F 0x444 |
| 28 | DELTA_LF_MULTI | F 0x5ec |
| 29 | SWITCHABLE_SKIP_MODE (is_motion_mode_switchable) | F 0x1c4 |
| 30 | SKIP_REFERENCE_UPDATE | F 0x0ec == 0 |
| 31 | DISABLE_REF_FRAME_MVS | S 0x4fc == 0 |

플래그 값은 시프트만 하고 마스크하지 않으므로 모든 불리언 필드는 반드시 0 또는 1이어야 한다.

### 3.2 나머지 필드

| msg 오프셋 | Mesa 필드 | 입력과 변환 | 주소 |
|---|---|---|---|
| 0x004 | current_frame_id | F 0x030 | 0x7ffb08645c8b |
| 0x008 | frame_offset | F 0x040 | 0x7ffb08645ca3 |
| 0x00c | profile | S 0x000 (바이트) | 0x7ffb08645c83 |
| 0x00d | is_annexb | 상수 0 | 0x7ffb08645c9d |
| 0x00e | frame_type | F 0x010 | 0x7ffb08645aeb |
| 0x00f | primary_ref_frame | F 0x064 | 0x7ffb08645d52 |
| 0x010 | curr_pic_idx | P 0x10 (바이트) | 0x7ffb08645c94 |
| 0x011 | sb_size | S 0x4d8 | 0x7ffb08645cac |
| 0x012 | interp_filter | F 0x1c0 | 0x7ffb08645cb8 |
| 0x013, 0x014 | filter_level[2] | F 0x678, 0x67c | 0x7ffb08645ec3 |
| 0x015 | filter_level_u | F 0x680 | |
| 0x016 | filter_level_v | F 0x684 | |
| 0x017 | sharpness_level | F 0x688 | |
| 0x018 | ref_deltas[8] | F 0x698 + 4i | 0x7ffb08645f17 |
| 0x020 | mode_deltas[2] | F 0x6bc, 0x6c0 | 0x7ffb08645f2d |
| 0x022 | base_qindex | F 0x408 | 0x7ffb08645f8d |
| 0x023 | y_dc_delta_q | F 0x414 | |
| 0x024 | u_dc_delta_q | F 0x41c | 0x7ffb08645fa5 (pshufb 마스크 00 08 04 0c) |
| 0x025 | v_dc_delta_q | F 0x424 | |
| 0x026 | u_ac_delta_q | F 0x420 | |
| 0x027 | v_ac_delta_q | F 0x428 | |
| 0x028, 0x029, 0x02a | qm_y, qm_u, qm_v | F 0x430, 0x434, 0x438 | 0x7ffb08645fd4 |
| 0x02b | delta_q_res | 1 << F 0x5e0 | 0x7ffb08645fbe |
| 0x02c | delta_lf_res | 1 << F 0x5e8 | 0x7ffb08645eff |
| 0x02d | tile_cols | 1 << F 0x1d4 | 0x7ffb08645de2 |
| 0x02e | tile_rows | 1 << F 0x1d8 | 0x7ffb08645df8 |
| 0x02f | tx_mode | F 0x780 | 0x7ffb08645cc4 |
| 0x030 | reference_mode | (F 0x784 == 1) ? 2 : 0 | 0x7ffb08645cfa |
| 0x031 | chroma_format | S 0x530 == 0 | 0x7ffb08645c7b |
| 0x034 | tile_size_bytes | 상수 0xff | 0x7ffb08645dcd |
| 0x038 | context_update_tile_id | F 0x3fc | 0x7ffb08645dd6 |
| 0x03c | tile_col_start_sb[i], i = 0..tile_cols | (F 0x1e4[i] + (1<<s) - 1) >> s, s = sb128 ? 5 : 4 | 0x7ffb08645e2e |
| 0x140 | tile_row_start_sb[i], i = 0..tile_rows | (F 0x2e8[i] + (1<<s) - 1) >> s | 0x7ffb08645e53 |
| 0x244, 0x248 | max_width, max_height | S 0x4c4 + 1, S 0x4c8 + 1 | 0x7ffb08645cd0 |
| 0x24c | width | F 0x138 | 0x7ffb08645ceb |
| 0x250 | height | F 0x140 | 0x7ffb08645d0c |
| 0x254 | superres_upscaled_width | F 0x144 | |
| 0x258 | superres_scale_denominator | F 0x14c | 0x7ffb08645d1b |
| 0x259 | order_hint_bits | S 0x514 | 0x7ffb08645d2a |
| 0x25a | ref_frame_map[8] | P 0x20 + 4i (바이트) | 0x7ffb08645d5d |
| 0x264 | ref_frame_offset[8] | 쓰지 않음(0) | |
| 0x284 | frame_refs[7] | P 0x40 + 4i (바이트) | 0x7ffb08645d8c |
| 0x28b | ref_frame_sign_bias[7] | 쓰지 않음(0) | |
| 0x292, 0x293 | bit_depth_luma_minus8, chroma | 비트 깊이 - 8 (S 0x524, 0x528에서 유도) | 0x7ffb08645d39 |
| 0x294 | feature_data[8][8] | F 0x554 int16을 int로 | 0x7ffb08646100 |
| 0x394 | feature_mask[8] | F 0x450[seg][j] != 0 이면 비트 j | 0x7ffb086460a5 |
| 0x39c | cdef_damping | F 0x74c | 0x7ffb08645ff8 |
| 0x39d | cdef_bits | F 0x6c8 | 0x7ffb08646007 |
| 0x39e | cdef_strengths[8] | F 0x6cc[i] 또는 (F 0x6ec[i] << 6) | 0x7ffb08646016 |
| 0x3be | cdef_uv_strengths[8] | F 0x70c[i] 또는 (F 0x72c[i] << 6) | 0x7ffb0864604a |
| 0x3de | frame_restoration_type[3] | F 0x750 + 4i | 0x7ffb08645f4b |
| 0x3e1 | log2_restoration_unit_size_minus5[3] | F 0x76c + 4i: 0이면 0, 아니면 log2 - 5 | 0x7ffb08645f5b |
| 0x3e4, 0x3e5 | p010_mode, msb_mode | 비트 깊이 9 이상이면 둘 다 1 | 0x7ffb08646553 |
| 0x3e8 | preskip_segid | 드라이버 계산 (특징 5 이상이 켜져 있으면 1) | 0x7ffb0864616c |
| 0x3e9 | last_active_segid | 드라이버 계산 | |
| 0x3ea | seg_lossless_flag | 드라이버 계산 (Mesa와 같은 식) | 0x7ffb086461bf |
| 0x3eb | coded_lossless | 쓰지 않음(0) | |
| 0x3ec부터 0x491 | film_grain | F 0x948부터 0xbcc (2.4절 참조) | 0x7ffb08646254 |
| 0x48c | film_grain.bit_depth_minus_8 | 비트 깊이 - 8 | 0x7ffb08646336 |
| 0x494 | uncompressed_header_size | 상수 0 | 0x7ffb08646539 |
| 0x498 + 0x2c*k (k = 1..7) | global_motion[k].wmtype | F 0x79c + 4k | 0x7ffb08645d99 |
| 0x49c + 0x2c*k | global_motion[k].wmmat[0..5] | F 0x7bc + 0x18k | 0x7ffb08645dae |
| 0x5f8 + 8n | tile_info[n] {offset, size} | T 0x14 + 0x200*row + 8*col, n = row*TileCols + col | 0x7ffb08645e72 |

### 3.3 공통 디코드 메시지(색인 종류 2, 크기 0xbc)에서 입력과 관련된 것

stream_type = 0x13(AV1), width_in_samples와 height_in_samples는 세션 생성 때의 폭과 높이(decoder+0x7c, +0x80), bsd_size = T 0x10, dpb_size = ctx+0x30, hw_ctxt_size = ctx+0x1c8, db_pitch = 폭을 64로 올림(비트 깊이 9 이상) 등. 메시지 머리에는 색인 항목 4개가 들어간다: 종류 2(크기 0xbc), 종류 0x11(AV1, 크기 0xdf8), 종류 3(DRM, 크기 0x44), 종류 0xf(크기 0xa8). 주소 0x7ffb0864587b부터.

## 4. 질문별 답

### 4.1 타일과 비트스트림

- 타일 열과 행 수는 F의 `TileColsLog2`(0x1d4), `TileRowsLog2`(0x1d8), `TileCols`(0x3ec), `TileRows`(0x3f0)와 `MiColStarts[65]`(0x1e4), `MiRowStarts[65]`(0x2e8)로 넘긴다. 시작 위치는 SB 단위가 아니라 4x4(MI) 단위로 넣어야 한다. 드라이버가 SB 크기에 맞춰 4 또는 5비트 올림 시프트를 한다. 메시지에 `tile_cols + 1`개, `tile_rows + 1`개 항목을 쓰므로 끝 경계 값(MiCols, MiRows)까지 넣어야 한다.
- 타일별 오프셋과 크기는 T 0x14부터 `tile[row][col]`(행마다 64칸 고정)로 넣는다. 드라이버가 `row*TileCols + col` 순서로 메시지 `tile_info[]`에 8바이트씩 그대로 복사한다. 앞 4바이트가 offset, 뒤 4바이트가 size라는 것은 Mesa 구조체 순서에서 온 추정이다(드라이버는 8바이트 통째로 복사).
- 비트스트림: `SetBitstreamFields`(0x7ffb0864564e)는 T 0x10의 길이만큼 `bitstreamTex`의 CPU 주소(텍스처+0x98)에서 내부 비트스트림 버퍼로 memcpy할 뿐 내용을 해석하지 않는다(caps[0x94] 비트 7이 켜져 있으면 복사 없이 호출자 텍스처를 그대로 쓴다). 길이가 decoder+0x8c(비트스트림 버퍼 크기)보다 크면 오류 1. 패딩은 DRM일 때만 한다.
- 따라서 OBU 전체를 줘도 되고 타일 그룹 데이터만 줘도 된다. 조건은 `tile[].offset`이 버퍼 시작 기준으로 각 타일의 부호화 데이터 첫 바이트(타일 크기 필드 다음)를 가리키고 `size`가 그 데이터 길이라는 것뿐이다. 드라이버가 `uncompressed_header_size = 0`, `tile_size_bytes = 0xff`로 고정하는 것이 Mesa와 같으므로 펌웨어는 헤더를 읽지 않는다고 본다(Mesa와 VA-API 관례에서 온 추정).

### 4.2 참조 프레임

- 표면 포인터가 아니라 색인이다. 참조 프레임 화소는 드라이버가 만드는 단일 내부 DPB 버퍼(ctx+0x38) 안에 펌웨어가 보관하고, 호출자는 칸 번호만 넘긴다. `targetTex`는 출력(디코드 대상) 전용이다.
- P 0x10: 현재 프레임이 저장될 칸. P 0x20[8]: ref_frame_map 각 칸이 가리키는 DPB 칸. P 0x40[7]: 이 프레임이 쓰는 7개 참조의 DPB 칸. 드라이버는 P 0x40을 변환 없이 `msg.frame_refs`에 복사하므로 `ref_frame_idx[i]` 값 자체가 아니라 `ref_frame_map[ref_frame_idx[i]]`를 호출자가 미리 풀어서 넣어야 한다(Mesa `get_av1_msg`와 같은 의미, 의미 부분은 Mesa에서 온 추정).
- DPB 크기는 9프레임 분량(4.4절)이므로 칸 번호는 0..8, 현재 프레임 칸은 ref_frame_map에서 쓰이지 않는 칸을 골라야 한다. 비어 있는 ref_frame_map 칸에 Mesa는 0x7f를 넣는다.
- `ref_frame_offset[8]`과 `ref_frame_sign_bias[7]`는 드라이버가 채우지 않는다(Mesa도 채우지 않음).

### 4.3 세그멘테이션, 루프 필터, CDEF, 루프 복원, 양자화, 전역 움직임, 필름 그레인

2.4절과 3.2절 참조. 주의할 점만 적는다.

- 세그멘테이션: `FeatureEnabled`는 uint32 [8][8], `FeatureData`는 int16 [8][8]로 형이 다르고 사이에 4바이트(0x550)가 낀다. preskip, last_active, lossless는 드라이버가 계산한다. FeatureData 128바이트와 계산한 마스크 8바이트는 seg/fg 버퍼(Mesa의 `dec->probs`)에도 들어간다.
- CDEF: 메시지 값 = lo | (hi << 6). Mesa는 VA 값(pri << 2 | sec)을 그대로 넣는다. 두 포장이 다르므로 Mesa와 같은 바이트를 만들려면 lo에 (pri << 2 | sec)를 넣고 hi는 0으로 두면 된다. 드라이버 식 자체는 DXVA의 primary 6비트, secondary 2비트 포장과 같은 모양이다. 어느 쪽이 펌웨어가 기대하는 것인지는 정적 분석으로 확정하지 못했다.
- 양자화 행렬: 드라이버는 qm 값을 그대로 바이트로 쓴다. Mesa는 `level | 0xf0`을 쓴다. Mesa와 같게 하려면 using_qmatrix=1일 때 qm_y, qm_u, qm_v에 `level | 0xf0`을 넣는다(level 15면 0xff가 되어 비트 8이 꺼지므로 Mesa와 일치). using_qmatrix=0이면 드라이버가 0xff를 써 넣는다.
- 루프 복원: `FrameRestorationType`은 규격의 재배열된 값(0 없음, 1 위너, 2 SGRPROJ, 3 전환)으로 보이고(추정), `LoopRestorationSize`는 화소 단위다.
- 전역 움직임: `gm_type[8]`, `gm_params[8][6]` 가운데 [1]..[7]만 읽는다. alpha, beta, gamma, delta는 채우지 않는다(Mesa도 같음).
- 필름 그레인: `apply_grain`이 0이 아니면 드라이버가 `av1SdbInitGrainBlock`(0x7ffb08646626)과 `av1SdbInitScalingFunction`(0x7ffb08647034)으로 seg/fg 버퍼의 +0x100(그레인 블록), +0x4900, +0x4b00, +0x4d00(스케일링 LUT)을 채운다. Mesa `rvcn_dec_av1_fg_init_buf_t` 오프셋과 일치. 호출자가 할 일은 없다. `ar_coeffs_*`는 plus_128 원시 값(0..255)으로 넣는다.

### 4.4 ProcessPictureDecodeParam, SetPictureDecodeParam, initHWContext, CalculateDPBSize, CalculateContextSize

- `VADecoder::SetPictureDecodeParam(unsigned, void*)`(0x7ffb0860ac3c)는 처리기의 vtable+0x28로 그대로 넘긴다. AV1의 `ProcessPictureDecodeParam`(0x7ffb086455ce)은 `return 1`뿐이다(HEVC도 같음). 따로 설정할 매개변수 종류가 없고, 부르면 1(실패로 보임)만 돌아온다.
- `initHWContext`(0x7ffb0864710c): 입력을 읽지 않는다. 문맥 버퍼(ctx+0x1d0의 CPU 주소)를 ctx+0x1c8 바이트만큼 0으로 지우고, 오프셋 0, 0x5800, 0xb000, 0x10800의 네 칸에 `initModeProbs`, `av1InitMvProbs`, `av1DefaultCoefProbs(칸 번호)`로 기본 CDF를 써 넣는다. `DecodePicture`가 문맥 버퍼를 새로 만든 직후(디코더 종류 6일 때) 부른다.
- `CalculateDPBSize`(0x7ffb086455da): 입력을 읽지 않는다. 세션의 폭과 높이(decoder+0x7c, +0x80)를 정렬해(비트 깊이 9 이상이면 64, 아니면 this+0x2c 배수) `폭*높이*3/2 * (this[0x28]*3 + 3) / 2`. this[0x28]은 생성자에서 8이므로 13.5프레임, 즉 10비트 기준 9프레임 분량. 비트 깊이(this+0x40)는 직전 `SetDecodeMessageFields`가 S 0x524, 0x528에서 정한 값(생성자 기본 8)이다.
- `CalculateContextSize`(0x7ffb08645642): 상수 0x22b9a00.
- `getDecodeTargetArea`(0x7ffb08647ec0): rect = {0, 0, F 0x134 + 1, F 0x13c + 1}.
- 재할당: `SetDecodeMessageFields` 앞부분(0x7ffb086457b0)이 {S 0x000, F 0x134 + 1, F 0x13c + 1, 비트 깊이}를 ctx+0x10의 이전 값과 비교해, 달라졌고 필요한 DPB 또는 문맥 크기가 커졌으면 ctx[0xc] = 1로 표시한다. 그러면 `DecodePicture`가 DPB와 문맥 버퍼를 다시 만들고 `initHWContext`를 부른다.

### 4.5 프레임 사이에 호출자가 유지할 상태

- CDF: 유지할 필요 없음. 기본값 초기화는 드라이버, 프레임별 저장과 불러오기는 펌웨어가 문맥 버퍼 안에서 한다(Mesa와 같은 구조). 호출자는 `primary_ref_frame`, `disable_cdf_update`, `disable_frame_end_update_cdf`, `context_update_tile_id`와 올바른 DPB 칸 번호만 주면 된다.
- 호출자가 유지해야 하는 것: (1) ref_frame_map 8칸과 DPB 칸 번호의 대응, `refresh_frame_flags`에 따른 갱신, 빈 칸 고르기. (2) 헤더 파싱 상태 전부(시퀀스 헤더, 이전 프레임에서 물려받는 세그멘테이션 값, 루프 필터 델타, 전역 움직임 매개변수, 필름 그레인 `load_grain` 등). 드라이버는 구조체에 든 최종 값만 본다. (3) `show_existing_frame` 처리는 이 경로에 없다. 호출자가 직접 해야 한다.
- 드라이버가 유지하는 것: 이전 {profile, 폭, 높이, 비트 깊이}, DPB, 문맥 버퍼, 버퍼 고리 위치, 프레임 번호(this+0x20).

### 4.6 DXVA_PicParams_AV1과의 일치 여부

일치하지 않는다. DXVA 구조체는 비트 필드와 UCHAR, USHORT를 섞은 단일 구조체인데, 이 입력은 포인터로 나뉜 세 구조체이고 거의 모든 필드가 4바이트이며 순서가 AV1 규격 구문 순서(동작점 32개 배열, buffer_removal_time 배열 포함)를 따른다. `ar_coeffs_*_plus_128`, `grain_scaling_minus_8`, `MiColStarts`처럼 규격 파서의 원시 변수 모양을 그대로 갖고 있어 AMD 자체 OBU 파서의 출력 구조체로 판단한다. CDEF 강도의 lo | hi << 6 포장만 DXVA와 닮았다.

## 5. VAFactory::createDecodeDataProcessor 점프 표 (0x7ffb0860f930)

| 종류 | 분기 주소 | 처리기 | new 크기 |
|---|---|---|---|
| 0 | 0x7ffb0860f7e8 | AVC (caps[0x94] 비트 16이면 AvdVcnAvcDecDataProcessor, 아니면 AvdAvcDecDataProcessor) | 0x48 |
| 1 | 0x7ffb0860f87d | VC1 (AvdVc1DecDataProcessor) | 0x40 |
| 2 | 0x7ffb0860f83f | MPEG2 (AvdMpeg2DecDataProcessor) | 0x48 |
| 3 | 0x7ffb0860f85c | MPEG2 VLD (AvdMpeg2VLDDecDataProcessor) | 0x48 |
| 4 | 0x7ffb0860f822 | JPEG (AvdJpegDecDataProcessor) | 0x40 |
| 5 | 0x7ffb0860f897 | HEVC (caps 비트 16이면 AvdVcnHevcDecDataProcessor, 아니면 AvdHevcDecDataProcessor) | 0x3d8 |
| 6 | 0x7ffb0860f8ca | AV1 (AvdVcnAv1DecDataProcessor, 조건 없음) | 0x48 |

과제에 적힌 순서(AVC, JPEG, MPEG2, MPEG2 VLD, VC1, HEVC, AV1)와 달리 1번이 VC1, 4번이 JPEG이다. `VAFactory::createDecoder`(점프 표 0x7ffb0860f7a4)도 같은 번호를 쓰고, 6번은 항상 `VAVcnDecoder`를 만들며 decoder+0x78에 종류 번호를 저장한다. `AvdVcnAv1DecDataProcessor` 생성자(0x7ffb08645470)는 this[8] = 6, this[0x28] = 8, this[0x40] = 8로 둔다. 세션 생성 메시지 전체 크기 표(0x7ffb0868e9f0)에서 종류 6은 0x1008이다.

## 6. Mesa와 다른 점 (동작에 영향 줄 수 있는 것)

1. 플래그 비트 3(INTRA_ONLY)을 드라이버가 세우지 않는다. frame_type 바이트는 따로 전달된다. 인트라 전용 프레임에서 문제가 되는지 모른다.
2. `tile_cols`, `tile_rows`를 1 << log2로 만든다. 타일 수가 2의 거듭제곱이 아닌 스트림(예: 3열)에서는 Mesa가 넣는 실제 개수와 달라진다. 타일 표 복사는 실제 `TileCols`, `TileRows`를 쓴다.
3. CDEF 강도 포장(4.3절).
4. qm 값에 0xf0을 OR하지 않는다(4.3절).
5. 드라이버가 입력 구조체에 쓴다: using_qmatrix가 0이면 F 0x430, 0x434, 0x438에 0xff.

## 7. 모르는 것

- T 0x00부터 0x0f, P 0x11부터 0x1f와 0x5c의 용도. 이 처리기는 읽지 않는다.
- F 0x87c부터 0x947(51개 dword), F 0x5f0부터 0x677의 내용. 읽지 않는다.
- `[추정]`으로 표시한 모든 필드 이름. 오프셋 간격과 규격 순서로 맞췄을 뿐 검증할 호출자가 없다.
- 시퀀스와 프레임 구조체의 실제 전체 크기.
- 펌웨어가 `tile_info.offset`을 어디 기준으로 해석하는지, P 0x40의 의미(DPB 칸 번호)는 Mesa 동작에서 가져온 것이고 이 드라이버만으로는 확인할 수 없다.
- Rembrandt(VCN 3.1.2) 펌웨어가 이 드라이버가 만드는 메시지 판(0xdf8, 끝의 `av1_intrabc_workaround` 없음)을 그대로 받는지. Mesa는 0xe00 크기 구조체를 쓴다.
- `ProcessPictureDecodeParam`의 반환값 1이 "미지원"인지 "성공"인지. 다른 코덱 구현과 비교하지 않았다.
