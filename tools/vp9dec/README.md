# vp9dec: VCN용 VP9 디코드 메시지 만들기

macOS의 AMD 유저랜드 드라이버에는 VP9 메시지 생성기가 없다. 그래서 VCN 펌웨어가 받는 VP9 디코드 메시지와
확률 버퍼를 직접 만든다. 형식은 Mesa(radeonsi의 VCN 디코더)를 그대로 따랐다. 이 폴더에는 메시지를 만드는
데 필요한 헤더, 파서, 파서 시험 도구까지만 있다. 실제로 VCN에 명령을 넣는 디코더(av1dec의 `av1dec.c`에 해당)는
아직 없다.

## 파일

| 파일 | 내용 |
|---|---|
| `vcn_vp9.h` | Mesa `ac_vcn_dec.h`에서 VP9에 필요한 상수와 구조체를 줄 단위로 그대로 잘라 옮긴 헤더. 메시지 머리와 색인, create, 공통 decode, DRM, 동적 DPB(tier 1, tier 2), VP9 메시지, 피드백 머리, 확률과 세그먼트 버퍼. 끝에 Mesa 원본을 컴파일해 얻은 크기와 오프셋 `_Static_assert` 91개 |
| `vp9_probs.h` | Mesa `vl_probs_table.h`의 VP9 기본 확률 표 전부와 `fill_probs_table()`. `vp9_init_hw_context(void *ctx_buf)`, `vp9_init_probs_buffer(void *probs_buf)`, `VP9_HW_CONTEXT_SIZE(is_profile2)` |
| `vp9hdr.h`, `vp9hdr.c` | VP9 비압축 헤더 파서(규격 6.2 전체), 프레임 사이 상태, 슈퍼프레임 색인 분해 |
| `vp9msg.h` | 파서 결과에서 `rvcn_dec_message_vp9_t`와 세그먼트 구역을 채우는 `vp9_fill_msg()`, 메시지 배치를 잡는 `vp9_msg_layout()` |
| `vp9dump.c` | ivf를 읽어 프레임마다 헤더를 출력하는 시험 도구. `-t`는 대조용 출력, `-m`은 메시지까지 채워 출력 |
| `compare_trace.py` | ffmpeg `trace_headers` 출력과 `vp9dump -t` 출력을 구문 요소 단위로 대조 |
| `test-vp9.ivf` | `../vtprobe/test-vp9.mp4`를 `ffmpeg -c copy -f ivf`로 바꾼 것(1920x1080, profile 0, 120프레임) |
| `test-trace.txt`, `test-dump.txt` | `make test`가 남기는 대조 입력(ffmpeg 쪽, vp9dump 쪽) |
| `Makefile` | `make`(vp9dump 빌드), `make test`(대조), `make check-x86`(x86_64, C99 pedantic 구문 검사) |

코드는 표준 C만 쓴다. 단 `_Static_assert`와 Mesa 구조체의 이름 없는 union은 C11 문법이라 `-std=c99 -pedantic`에서는
`-Wno-c11-extensions`가 필요하다(clang은 C99 모드에서도 둘 다 받아 준다).

## Mesa 출처

모두 Mesa 태그 `mesa-25.0.0`(커밋 `4fa244fddfebb21378042556862e197284ef65ac`)을
`gitlab.freedesktop.org/mesa/mesa/-/raw/mesa-25.0.0/...`에서 직접 받아 읽었다. 기억에 의존한 부분은 없다.

| Mesa 파일 | 쓴 곳 |
|---|---|
| `src/amd/common/ac_vcn_dec.h` | `vcn_vp9.h`의 상수와 구조체 |
| `src/gallium/drivers/radeonsi/radeon_vcn_dec.c` | `get_vp9_msg()`, `fill_probs_table()`, `rvcn_dec_message_decode()`, `rvcn_dec_dynamic_dpb_t2_message()`, `send_cmd_dec()`, `calc_dpb_size()`, 버퍼 배치 상수 |
| `src/gallium/drivers/radeonsi/radeon_vcn_dec.h` | `NUM_BUFFERS` |
| `src/gallium/auxiliary/vl/vl_probs_table.h` | VP9 기본 확률 표 |
| `src/gallium/frontends/va/picture_vp9.c`, `picture.c` | VAAPI 매개변수가 pipe 구조체로 옮겨지는 길, Mesa가 비압축 헤더를 다시 파싱하는 부분 |

VAAPI 매개변수를 만드는 쪽은 FFmpeg 태그 `n7.1`의 `libavcodec/vaapi_vp9.c`, `vp9.c`, `vp9shared.h`를 읽었다.

과제 설명에 있던 `rvcn_init_mode_probs`, `rvcn_init_mv_probs`, `rvcn_vp9_default_coef_probs`, `ac_vcn_vp9_default.h`는
Mesa 25.0.0에 없다. 그 계열 이름은 AV1용(`ac_vcn_av1_init_mode_probs` 등, `ac_vcn_dec.c`)이다. VP9는
`radeon_vcn_dec.c`의 `fill_probs_table()` 하나가 `vl_probs_table.h`의 표를 `rvcn_dec_vp9_probs_t`에 memcpy하는 것이 전부다.

## 펌웨어에 넘기는 비트스트림의 범위

- **프레임 전체**를 넘긴다. 비압축 헤더의 첫 바이트(frame_marker가 있는 바이트)부터 마지막 타일 끝까지다.
  ffmpeg가 `decode_slice(avctx, pkt->data, pkt->size)`로 패킷 전체를 주고, Mesa는 그것을 그대로 비트스트림 버퍼에 복사한다.
  압축 헤더 뒤부터 잘라 주는 것이 아니다.
- 슈퍼프레임은 미리 풀어서 프레임 하나씩 넘긴다(ffmpeg는 디코더 앞에서 `vp9_superframe_split`으로 나눈다).
  슈퍼프레임 색인은 넘기지 않는다. `vp9_split_superframe()`이 이 일을 한다.
- `show_existing_frame` 프레임은 펌웨어에 보내지 않는다. 이미 디코드된 참조 슬롯의 그림을 출력하기만 한다.
- 오프셋은 메시지의 두 필드로 알려 준다. 둘 다 프레임 시작 기준이다.
  - `uncompressed_header_size`: 비압축 헤더의 바이트 수(trailing_bits 포함). 압축 헤더가 시작하는 오프셋이다. `Vp9Frame.uncompressed_header_size`
  - `compressed_header_size`: 비압축 헤더 끝의 `header_size_in_bytes`. 첫 타일은 `uncompressed_header_size + compressed_header_size`에서 시작한다
- 크기: 비트스트림 버퍼에서 프레임 뒤를 128바이트 배수까지 0으로 채운다. 공통 메시지의 `bsd_size`와 VP9 메시지의
  `vp9_frame_size` 모두 `align(프레임 크기, 128)`이다(정렬 전 실제 크기를 넣는 필드는 없다).
- `frame_header_flags`의 `USE_UNCOMPRESSED_HEADER`(비트 14)를 항상 1로 넣는다. 압축 헤더(tx_mode, reference_mode, 확률 갱신)는
  메시지에 풀어 넣지 않고 펌웨어가 비트스트림에서 직접 읽는다. 그래서 메시지의 `tx_mode`, `reference_mode`는 0으로 둔다.

## 버퍼 목록과 명령

Mesa는 프레임마다 아래 순서로 명령을 넣는다(`send_msg_buf()`, `send_cmd_dec()`). VCN 3.x는 레지스터 방식
(`RDECODE_VCN2_5_GPCOM_VCPU_DATA0/DATA1/CMD`)이고 끝에 `ENGINE_CNTL`에 1을 쓴다.

| 순서 | 명령 | 값 | 버퍼 | 크기 | 비고 |
|---|---|---|---|---|---|
| 1 | `RDECODE_CMD_SESSION_CONTEXT_BUFFER` | 0x005 | 세션 문맥 | `RDECODE_SESSION_CONTEXT_SIZE` = 128 * 1024 | 디코더당 하나. `sw_ctxt_size`에 같은 값 |
| 2 | `RDECODE_CMD_MSG_BUFFER` | 0x000 | 메시지 버퍼, 오프셋 0 | 메시지 구역 0x2000 | 머리 + 색인 + decode + 동적 DPB + VP9 메시지 |
| 3 | `RDECODE_CMD_DPB_BUFFER` | 0x001 | DPB | 아래 계산식 | **tier 2에서는 보내지 않는다**(DPB 주소가 메시지 안에 있다) |
| 4 | `RDECODE_CMD_CONTEXT_BUFFER` | 0x206 | ctx(hw context) | `VP9_HW_CONTEXT_SIZE` | 스트림당 하나, 읽고 쓰기. 처음에 기본 확률로 초기화 |
| 5 | `RDECODE_CMD_BITSTREAM_BUFFER` | 0x100 | 비트스트림 | `align(프레임 크기, 128)` 이상 | 초기 크기는 `align(폭 * 높이 / 32, 128)`, 모자라면 키운다 |
| 6 | `RDECODE_CMD_DECODING_TARGET_BUFFER` | 0x002 | 출력 표면 | `dt_size` | NV12(profile 2는 P010 또는 NV12) |
| 7 | `RDECODE_CMD_FEEDBACK_BUFFER` | 0x003 | 메시지 버퍼, 오프셋 0x2000 | 2048 | `rvcn_dec_feedback_header_t`: header_size = total_size = 44, num_buffers = 0으로 써 둔다 |
| 8 | `RDECODE_CMD_PROB_TBL_BUFFER` | 0x004 | 메시지 버퍼, 오프셋 0x2800 | 2560 = 2304 + 256 | `rvcn_dec_vp9_probs_segment_t`. VP9와 AV1만. H.264와 HEVC는 이 자리에 `IT_SCALING_TABLE_BUFFER`(0x204) |

- 메시지, 피드백, 확률 표는 **한 버퍼**(0x2000 + 2048 + 2560 = 12800바이트)를 나눠 쓴다. Mesa는 이 버퍼와 비트스트림 버퍼를
  `NUM_BUFFERS` = 4벌 만들어 프레임마다 돌려 쓴다.
- 확률 버퍼 앞 2304바이트(`rvcn_dec_vp9_probs_t`, 실제로 쓰는 것은 2071바이트)는 디코더를 만들 때 4벌 모두 `fill_probs_table()`로
  한 번 채우고 다시 쓰지 않는다. 뒤 256바이트(세그먼트 구역)는 프레임마다 쓴다. 세그멘테이션이 꺼진 프레임은 256바이트를 0으로 지운다.
- 스트림을 열 때 `RDECODE_MSG_CREATE` 메시지(머리 40 + `rvcn_dec_message_create_t` 16, stream_type = 0x11, 폭, 높이)를
  `SESSION_CONTEXT_BUFFER` + `MSG_BUFFER` 두 명령만으로 한 번 보낸다. 닫을 때는 `RDECODE_MSG_DESTROY`.

### 디코드 메시지 배치(VCN 3.x, 동적 DPB tier 2, 암호화 없음)

`vp9_msg_layout()`이 이대로 잡는다.

| 오프셋 | 크기 | 내용 |
|---|---|---|
| 0 | 40 | `rvcn_dec_message_header_t`. msg_type = `RDECODE_MSG_DECODE`, num_buffers = 3, total_size = 548. 안의 index[0] = `RDECODE_MESSAGE_DECODE`(0x2), offset 72, size 188 |
| 40 | 16 | index[1] = `RDECODE_MESSAGE_VP9`(0xE), offset 428, size 120 |
| 56 | 16 | index[2] = `RDECODE_MESSAGE_DYNAMIC_DPB`(0x10), offset 260, size 168 |
| 72 | 188 | `rvcn_dec_message_decode_t` |
| 260 | 168 | `rvcn_dec_message_dynamic_dpb_t2_t` |
| 428 | 120 | `rvcn_dec_message_vp9_t` |

`status_report_feedback_number`에는 1부터 세는 프레임 번호를 넣는다.

## ctx 크기

`vp9_probs.h`의 `VP9_HW_CONTEXT_SIZE`. 해상도와 무관한 고정 크기다(VCN 2.0 이상).

```
ctx_size  = 2304 * 5                기본 확률 + 확률 데이터           11,520
ctx_size += 32 * 2 * 128 * 68       SRE collocated context data      557,056
ctx_size += 9 * 64 * 2 * 128 * 68   SMP collocated context data   10,027,008
ctx_size += 8 * 2 * 2 * 8192        SDB left tile pixel              262,144
profile 2이면 += 8 * 2 * 4096                                          65,536
```

profile 0은 10,857,728바이트, profile 2는 10,923,264바이트. ctx를 처음 만들 때 `fill_probs_table()`이 맨 앞
`[0, 2071)`만 채운다(`[0, 32)`는 0, `[32, 2071)`은 기본 확률). 나머지는 손대지 않으므로 0으로 지운 버퍼에
`vp9_init_hw_context()`를 부르면 된다. 공통 메시지의 `hw_ctxt_size`에는 이 버퍼의 크기를 넣는다.

## DPB 크기

Mesa는 VCN 버전에 따라 방식이 다르다. Rembrandt(VCN 3.1.2)는 첫 번째다.

**VCN 3.0 이상: 동적 DPB tier 2** (VP9, AV1 공통)
- 큰 DPB 버퍼 하나가 없다. 참조 그림마다 표면을 따로 잡는다. 표면 크기는 `align(폭, 64)` x `align(높이, 64)`,
  형식은 NV12(10비트는 P010).
- `dpb_size` = 0, `RDECODE_CMD_DPB_BUFFER`를 보내지 않는다. `decode_flags` = 1(`RDECODE_FLAGS_USE_DYNAMIC_DPB_MASK`).
- 주소는 `rvcn_dec_message_dynamic_dpb_t2_t`에 넣는다. `dpbCurrLo/Hi`는 이번 프레임을 써 넣을 표면의 GPU 가상 주소,
  `dpbAddrLo/Hi[i]`는 **VP9 참조 슬롯 i**(`ref_frame_map[i]`와 같은 순서, DPB 칸 번호 순서가 아니다)가 가리키는 표면의 주소.
  `dpbArraySize`는 채운 항목 수(= 비어 있지 않은 참조 슬롯 수). 빈 항목이 남으면 Mesa는 목록 첫 표면의 주소로 메운다.
  `dpbLumaPitch`, `dpbLumaAlignedHeight`, `dpbLumaAlignedSize`, `dpbChroma*`는 표면의 실제 피치, 높이, 평면 크기.
  `dpbConfigFlags` = 0. `db_swizzle_mode`에는 DPB 표면의 swizzle 모드를 넣는다.
- 해상도가 바뀌면 크기가 다른 표면은 버리고 새로 잡는다.

**VCN 2.6 이하에서 VP9: 동적 DPB tier 1 또는 고정 버퍼** (참고용)
- `max_references = max(참조 수 + 1, 9)` = 9
- tier 1: `dpb_size = align(폭, 64) * align(높이, 64) * 3 / 2 * 9`
- 고정(DPB_MAX_RES): `dpb_size = 8192 * 4320 * 3 / 2 * 9`(VCN 2.0 이상), `4096 * 3000 * 3 / 2 * 9`(VCN 1.x)
- profile 2이면 위 값에 다시 `* 3 / 2`
- 이때는 `RDECODE_CMD_DPB_BUFFER`를 보내고 `dpb_size`에 버퍼 크기를 넣는다.

**DPB 칸 번호**(`curr_pic_idx`, `ref_frame_map[8]`, `frame_refs[3]`)
- Mesa는 `render_pic_list[32]`에서 "지금 어느 참조 슬롯에서도 쓰이지 않는 가장 작은 번호"를 이번 프레임에 준다.
  키 프레임이면 목록을 전부 비운 뒤 고른다(보통 0). 그 번호가 `curr_pic_idx`.
- `ref_frame_map[i]`는 참조 슬롯 i에 들어 있는 그림의 칸 번호, 빈 슬롯과 키 프레임의 모든 슬롯은 0x7f.
- `frame_refs[k]` = `ref_frame_map[ref_frame_idx[k]]`(LAST, GOLDEN, ALTREF 순).
- 디코드 뒤 `refresh_frame_flags`의 비트마다 슬롯에 `curr_pic_idx`를 기록한다. `vp9dump.c`의 `pick_dpb_index()`와 `print_msg()`가 이 방식의 예다.

## 공통 디코드 메시지에서 코덱별 차이

`rvcn_dec_message_decode()`가 `rvcn_dec_message_decode_t`에 넣는 값. VCN 3.x 기준. 적지 않은 필드는 세 코덱이 같다
(sct_size = sc_coeff_size = 0, sw_ctxt_size = 128 * 1024, db_surf_tile_config = 0, dt_tiling_mode = 0,
db_array_mode = dt_array_mode = `RDECODE_ARRAY_MODE_LINEAR`, mif_wrc_en = 1, dt_pitch와 dt_uv_pitch와 dt_*_offset은 출력 표면 값).

| 필드 | VP9 | AV1 | HEVC |
|---|---|---|---|
| `stream_type` | `RDECODE_CODEC_VP9` 0x11 | `RDECODE_CODEC_AV1` 0x13 | `RDECODE_CODEC_H265` 0x10 |
| 코덱 메시지 색인 | `RDECODE_MESSAGE_VP9` 0xE, 120바이트 | `RDECODE_MESSAGE_AV1` 0x11 | `RDECODE_MESSAGE_HEVC` 0xD. 추가로 `HEVC_DIRECT_REF_LIST`(0x15) 색인과 본문이 붙는다 |
| `width_in_samples`, `height_in_samples` | **프레임마다** 그 프레임의 frame_width, frame_height(VA 프런트엔드가 VP9만 매 그림 갱신한다) | 디코더를 만들 때의 크기 | 디코더를 만들 때의 크기 |
| `bsd_size` | `align(프레임 전체 크기, 128)`. 버퍼에는 비압축 헤더부터 들어 있다 | `align(크기, 128)`. 버퍼에는 타일 그룹 데이터가 들어 있고 타일 위치는 AV1 메시지의 `tile_info[]`에 있다 | `align(크기, 128)`, 슬라이스 NAL들 |
| `dpb_size` | 0(tier 2) | 0(tier 2) | tier 2가 아니면 DPB 버퍼 크기 |
| `decode_flags` | 1(tier 2) | 1(tier 2) | 0 또는 1 |
| `hw_ctxt_size` | 10,857,728(profile 2는 10,923,264) | `ac_vcn_dec_calc_ctx_size_av1(av1_version)` | `calc_ctx_size_h265_main` 또는 `_main10`(해상도에 따라 다름) |
| ctx 초기화 | `fill_probs_table()`로 앞 2071바이트 | `ac_vcn_av1_init_probs()`로 CDF 표 전체 | 없음 |
| ctx를 만드는 시점 | 첫 프레임, 코덱 메시지를 채우기 전 | 첫 프레임, AV1 메시지를 채운 뒤 | 첫 프레임 |
| `db_pitch` | `align(폭, 64)` | `align(폭, 64)` | Main은 `align(폭, 32)`, Main10은 64 |
| `db_aligned_height` | `align(높이, 64)` | `align(높이, 64)` | Main10만 `align(높이, 64)`, Main은 0 |
| `db_pitch_uv` | **0(채우지 않음)** | 출력 표면의 크로마 피치 | 0 |
| `db_swizzle_mode` | DPB 표면의 swizzle 모드(tier 2) | 같음 | 같음 |
| 출력 표면(`dt_*`) | 디코드 대상 표면 | 필름 그레인이 켜져 있으면 필름 그레인 출력 표면 | 디코드 대상 표면 |
| 확률 버퍼 명령 | `PROB_TBL_BUFFER`, 2560바이트(확률 2304 + 세그먼트 256) | `PROB_TBL_BUFFER`, `rvcn_dec_av1_segment_fg_t`(세그먼트 + 필름 그레인) | `IT_SCALING_TABLE_BUFFER`(스케일링 목록 992바이트) |
| 프레임 번호와 표면 연결 | 칸 번호를 `render_pic_list`로 관리 | 같음 | 프레임 번호를 표면에 붙여 관리 |

VP9 메시지(`rvcn_dec_message_vp9_t`) 안에서 Mesa가 넣는 고정 값: `chroma_format` = 1, `USE_UNCOMPRESSED_HEADER` = 1,
`tx_mode` = `reference_mode` = `frame_to_show` = 0, `SHOW_EXISTING_FRAME`과 `SEGMENTATION_UPDATE_DATA` 비트는 항상 0.
profile 2에서 출력이 P010이면 `p010_mode` = `msb_mode` = 1, NV12로 내려 받으면 `luma_10to8` = `chroma_10to8` = 1.

## VP9 메시지 필드가 만들어지는 길

`vp9_fill_msg()`가 옮긴 계산. 왼쪽은 메시지 필드, 오른쪽은 파서 값에서의 계산.

| 메시지 필드 | 값 |
|---|---|
| flags `FRAME_TYPE` | `frame_type`(키 프레임 0, 그 외 1) |
| flags `ERROR_RESILIENT_MODE`, `FRAME_PARALLEL_DECODING_MODE`, `REFRESH_FRAME_CONTEXT`, `SEGMENTATION_ENABLED` | 같은 이름의 헤더 값(error_resilient이면 parallel = 1, refresh = 0) |
| flags `INTRA_ONLY` | `intra_only` |
| flags `ALLOW_HIGH_PRECISION_MV` | 키 프레임이면 0, 아니면 헤더 값 |
| flags `SEGMENTATION_UPDATE_MAP`, `SEGMENTATION_TEMPORAL_UPDATE` | 세그멘테이션이 켜져 있을 때의 헤더 값 |
| flags `MODE_REF_DELTA_ENABLED`, `MODE_REF_DELTA_UPDATE` | `loop_filter_delta_enabled`, `loop_filter_delta_update` |
| flags `USE_PREV_IN_FIND_MV_REFS` | 직전에 디코드한 프레임의 show_frame이 1이고, 이번이 error_resilient이 아니고, 직전 프레임과 폭 높이가 같을 때 1 |
| `interp_filter` | `interpolation_filter`(0 EIGHTTAP, 1 SMOOTH, 2 SHARP, 3 BILINEAR, 4 SWITCHABLE). 비트스트림의 2비트 값이 아니라 literal_to_type을 거친 값 |
| `frame_context_idx` | 키 프레임이나 intra_only이면 0, 아니면 헤더 값 |
| `reset_frame_context`, `filter_level`, `sharpness_level`, `log2_tile_cols`, `log2_tile_rows` | 헤더 값 |
| `lf_adj_level[세그먼트][참조][모드]` | ffmpeg가 계산한 세그먼트별 루프 필터 레벨. 기준 레벨(세그먼트 ALT_L 특징 반영, 0..63으로 자름)에 `ref_deltas[참조]`(참조 1..3은 `+ mode_deltas[모드]`)를 `filter_level >= 32`이면 2배 해서 더하고 0..63으로 자른다. 델타가 꺼져 있으면 8칸 모두 기준 레벨. 세그멘테이션이 꺼져 있으면 0번 세그먼트만 갱신된다 |
| `base_qindex`, `y_dc_delta_q`, `uv_ac_delta_q`, `uv_dc_delta_q` | `base_q_idx`, `delta_q_y_dc`, 그리고 uv 둘은 아래 "불확실한 점 2" 참고. lossless이면 넷 다 0 |
| `chroma_format` | 1 |
| `bit_depth_luma_minus8`, `bit_depth_chroma_minus8` | `bit_depth - 8` |
| `vp9_frame_size` | `align(프레임 크기, 128)` |
| `uncompressed_header_size`, `compressed_header_size` | `uncompressed_header_size`, `header_size_in_bytes` |
| `curr_pic_idx`, `ref_frame_map[8]`, `frame_refs[3]` | 위 "DPB 칸 번호" |
| `ref_frame_sign_bias[3]` | 헤더 값. 단 error_resilient이면 0(ffmpeg가 그렇게 만든다) |
| 세그먼트 구역 `feature_data[i]` | `(ALT_Q 값 & 0xffff) \| (ALT_L 값 & 0xff) << 16 \| (REF_FRAME 값 & 0xf) << 24` |
| 세그먼트 구역 `feature_mask[i]` | 비트 0 ALT_Q, 비트 1 ALT_L, 비트 2 REF_FRAME, 비트 3 SKIP 켜짐 여부 |
| 세그먼트 구역 `tree_probs[7]`, `pred_probs[3]`, `abs_delta` | 트리 확률, 예측 확률(temporal_update가 아니면 255), `segmentation_abs_or_delta_update` |

## 시험

```
make test
```

`test-vp9.ivf`의 120프레임 전부를 ffmpeg `-bsf:v trace_headers`(CBS 파서) 출력과 대조했다. 프레임 타입,
refresh_frame_flags, ref_frame_idx, sign_bias, found_ref, base_q_idx, 루프 필터 레벨과 델타, 타일, header_size_in_bytes,
비압축 헤더 바이트 크기 등 구문 요소 51종, 값 5,394개가 모두 일치했고 파싱 오류는 없었다.

이 파일은 세그멘테이션도 숨은 프레임도 없는 단순한 스트림이라, 저장소에 넣지 않은 추가 스트림으로도 같은 대조를 했다(모두 불일치 0).

| 스트림 | 내용 | 프레임 |
|---|---|---|
| libvpx 2패스, aq-mode 1, alt-ref | 세그멘테이션(트리 확률, 예측 확률, 특징 데이터), 숨은 프레임, 슈퍼프레임 6개 | 96 |
| libvpx aq-mode 3, alt-ref | 세그멘테이션 | 70 |
| profile 2(10비트 4:2:0), aq-mode 1 | ten_or_twelve_bit, 세그멘테이션 | 70 |
| error-resilient, aq-mode 2 | error_resilient_mode | 70 |
| realtime, error-resilient, frame-parallel | 슈퍼프레임 3개 | 73 |
| lossless, 타일 행 | lossless, tile_rows_log2 | 70 |
| profile 1(4:4:4), profile 3(12비트 4:2:2), RGB | color_config의 나머지 분기 | 각 70 |
| 4096x2176, 타일 열 16 | 타일 열 증가 비트 | 6 |
| 손으로 만든 7프레임 | profile 0과 2의 intra_only, 크기를 직접 부호화한 인터 프레임, 참조에서 크기 물려받기, render_size, 양자화 델타, 모드 델타, 보간 필터 2비트 값, show_existing_frame, 8192 폭 | 7 |

대조한 것은 **파서**다. `vp9_fill_msg()`가 만드는 메시지는 Mesa와 ffmpeg 소스를 읽고 옮긴 것이고, 실제 Mesa가 만든
메시지 덤프와 견주어 보지는 못했다(리눅스 실기가 없다). `vp9dump -m` 출력으로 값이 식대로 나오는지만 눈으로 확인했다.

## 불확실한 점

1. **`USE_UNCOMPRESSED_HEADER` = 1의 뜻.** 이름으로 보아 펌웨어가 비트스트림의 비압축 헤더도 직접 읽을 가능성이 있다.
   그렇다면 메시지의 많은 필드는 참고용일 뿐이고, 아래 2, 3, 4번이 리눅스에서 문제로 드러나지 않는 이유가 설명된다.
   확인된 사실은 아니다. 펌웨어가 메시지 값을 쓴다고 보고 모든 필드를 정확히 채우는 쪽이 안전하다.
2. **uv 양자화 델타의 순서.** 비트스트림 순서는 y_dc, uv_dc, uv_ac인데 Mesa의 재파서는 그 순서로 읽은 값을
   구조체의 y_dc, uv_ac, uv_dc 필드에 차례로 넣는다. 그래서 `uv_ac_delta_q`에 규격의 delta_q_uv_dc가 들어간다.
   Mesa의 실수인지 펌웨어 필드 이름이 뒤바뀐 것인지 알 수 없다. 기본값은 Mesa와 같게 했고
   `-DVP9MSG_UV_DELTA_BY_NAME=1`로 이름대로 넣을 수 있다. 일반 스트림에서는 둘 다 0이라 차이가 없다.
3. **ffmpeg의 묵은 값.** ffmpeg는 이번 프레임에 부호화되지 않은 필드를 지우지 않는다. 눈에 띄는 것은 세그멘테이션이
   켜져 있고 update_map이 0인 프레임에서 `SEGMENTATION_TEMPORAL_UPDATE` 비트와 pred_probs에 이전 프레임 값이 남는 것이다
   (aq-mode 스트림에서 실제로 나온다). 기본값은 ffmpeg + Mesa와 똑같이 내고, `-DVP9MSG_SPEC_CLEAN=1`이면 규격 값(0, 255)을 낸다.
   세그멘테이션이 있는 스트림에서 그림이 깨지면 이 스위치를 먼저 바꿔 볼 것.
4. **Mesa 재파서의 오류, 따르지 않았다.** Mesa `vlVaDecoderVP9BitstreamHeader()`는 profile 0의 intra_only 프레임에서
   있지도 않은 color_config 4비트를 읽어 그 뒤 값(base_qindex, 델타, 세그먼트 값, 델타 플래그)이 틀어진다. 여기서는 규격대로 파싱한 값을 넣는다.
   반대로 Mesa가 세그먼트 ALT_Q, ALT_L 값과 켜짐 여부, abs_delta를 키 프레임에서도 지우지 않고 들고 있는 것은 그대로 흉내 냈다
   (세그멘테이션을 켠 채 update_data 없이 키 프레임이 오는 비정상 스트림에서만 규격과 달라진다).
5. **`USE_PREV_IN_FIND_MV_REFS`.** Mesa는 직전 프레임의 show_frame, error_resilient, 크기만 본다. libvpx는 직전 프레임이
   intra_only였는지도 본다. Mesa를 따랐다. 펌웨어가 이 비트를 어떻게 쓰는지는 모른다.
6. **`hw_ctxt_size`.** Mesa는 계산한 크기가 아니라 할당된 버퍼 객체의 크기를 넣는다. 리눅스 winsys가 페이지 단위로
   올림한다면 10,858,496(0xA5B000)일 수 있다. 계산값 10,857,728보다 크게 잡고 실제 할당 크기를 넣으면 안전하다.
7. **ctx와 DPB 표면의 초기 내용.** Mesa는 ctx의 `[2071, 끝)`과 새 DPB 표면을 지우지 않는다. 새 GPU 버퍼가 0으로 차 있다는
   전제로 보인다. macOS에서는 직접 0으로 지우는 편이 안전하다.
8. **macOS 쪽 DPB 방식.** Mesa는 VCN 3.1.2에서 VP9에 동적 DPB tier 2를 쓴다. macOS 드라이버의 명령 경로가 tier 2
   메시지(GPU 가상 주소를 메시지에 직접 넣는 방식)를 그대로 통과시키는지는 AV1 구현에서 쓴 방식에 맞춰 확인해야 한다.
   tier 2가 안 되면 고정 DPB(`decode_flags` = 0, `DPB_BUFFER` 명령, 동적 DPB 색인 없이 num_buffers = 2) 배치를 따로 만들어야 하고,
   그 경우 펌웨어가 `curr_pic_idx`로 DPB 안의 칸을 직접 고른다. 이 배치는 이 폴더의 `vp9_msg_layout()`에 없다.
9. **12비트.** Mesa는 bit_depth가 10을 넘으면 디코드를 거부한다. 파서는 12비트와 profile 1, 3 헤더도 읽지만
   `vp9_fill_msg()`는 걸러 주지 않으니 호출자가 profile 0과 2(10비트)만 통과시켜야 한다.
10. **해상도 변경과 참조 스케일링.** 참조와 크기가 다른 인터 프레임(found_ref = 0)은 파서와 메시지 채우기까지는 되지만,
    펌웨어가 크기가 다른 참조를 받는 조건(tier 2에서 표면 크기가 섞이는 경우)은 Mesa 코드만으로는 확실하지 않다.
