# AMDRadeonVADriver2 AVD 디코드 명령 구조 역분석 (HEVC 한 프레임 기준)

대상: macOS 15.8.2 x86_64 dyld 공유 캐시 안의 `AMDRadeonVADriver2`. 정적 주소는 캐시 기준(`7ffb086xxxxx`), 추적 로그의 실행 시 주소는 슬라이드 `+0x4b79000`이 붙어 있다(예: 실행 시 vtable `7ffb45e29cc0` 은 정적 `7ffb412b0cc0` = `vtable for VAVcnDecoder` + 0x10).

표기: **[확실]** 은 디스어셈블과 추적 로그가 둘 다 맞는 것, **[정적]** 은 디스어셈블만으로 본 것, **[추정]** 은 값의 모양으로 짐작한 것.

추적 로그 줄 번호는 `logs/avdtrace-hevc-d2.txt`(깊이 2) 기준이고, d1 이라고 적은 것만 `avdtrace-hevc-d1.txt` 이다.

---

## 0. 가장 중요한 결론

1. HEVC 의 "그림 매개변수"는 한 덩어리의 바이트열이 **아니다**. `AVD_CmdInfo+0x20` 이 가리키는 작은 구조체(아래 `AVD_HevcDecodeParam`) 안에 포인터 세 개가 있고, 각각 **이미 파싱된** SPS 구조체(`hevc_sequence_parameter_set_t`, 최소 0x8e82 바이트), PPS 구조체(`hevc_picture_parameter_set_t`, 최소 0x19e5 바이트), 슬라이스 항목 배열(항목당 0x328 바이트, 그 안에 파싱된 슬라이스 헤더 포인터)을 가리킨다. 드라이버가 이 필드들을 읽어 VCN 디코드 메시지를 직접 조립한다. 즉 비트스트림 파싱(SPS, PPS, 슬라이스 헤더, RPS)은 호출자(AppleGVA) 몫이다.
2. 추적된 HEVC(1920x1080, 출력 NV12)는 subId **0x800** 으로 컨텍스트를 만들었다. `GetVAFunctionType` 에서 0x400 은 내부 타입 4, 0x800 은 타입 5 이고, 추적 속 디코더 객체는 `VAVcnDecoder` + `AvdVcnHevcDecDataProcessor` 였다. avdprobe.c 주석의 "0x800 = HEVC 10 bit"은 맞지 않을 가능성이 크다(0x800 이 VCN 세대 HEVC, 0x400 은 다른 인자 배치를 쓰는 옛 경로로 보인다). 아래 배치는 전부 0x800 경로다.
3. 출력 표면은 **IOSurfaceRef 포인터**를 그대로 넘긴다(IOSurfaceID 도 CVPixelBuffer 도 아님). 드라이버가 `IOSurfaceGetID`, `IOSurfaceBindAccel`, `IOAccelResourceCreate` 로 텍스처를 만든다.
4. 비트스트림 버퍼는 **드라이버가 할당**한다(AGP 텍스처). `AllocateBitstreamBufferPool` 이 호출자가 준 배열에 CPU 주소를 써 주고, 호출자는 거기에 Annex B(시작 코드 00 00 00 01) 형식 슬라이스 NAL 을 써넣는다.
5. 참조 프레임은 표면으로 넘기지 않는다. 드라이버가 내부 DPB 텍스처와 16칸짜리 (그림 번호, POC) 표를 스스로 관리하고, 호출자는 프레임마다 "현재 그림 번호"와 "현재 POC"만 준다. 슬라이스 항목 안에 참조 IOSurface 목록이 실려 오지만 0x800 HEVC 경로에서 드라이버가 읽는 흔적은 찾지 못했다.
6. `GetDriverParameter`, `SetContextParameter`, `GetBitstreamBufferInfo` 는 디코딩에 필수가 아니다(4절).

---

## 1. 비트스트림 버퍼

### 1.1 GetBitstreamBufferInfo(driver, AVD_DriverBitstreamBufferRequirementInfo*)

`AvdVAInterface::GetBitstreamBufferInfo` 7ffb08660222. 드라이버 상태를 바꾸지 않는 순수 조회다. 구조체는 u32 만으로 이뤄져 있다(추적에서 스택 주소가 ...fc 로 4바이트 정렬).

```c
struct AVD_DriverBitstreamBufferRequirementInfo {   // 0x34
    uint32_t funcId;      // +0x00 입력. 1, 2, 0x80 또는 0x20 만 허용 [확실]. 추적 값 1
    uint32_t unk04;       // +0x04 추적 값 8 (드라이버가 읽지 않음) [추정: 비트 깊이]
    uint32_t hwId;        // +0x08 추적 값 4 (vcn0 엔진 번호, 읽지 않음) [추정]
    uint32_t unk0c;       // +0x0c 입력. 1 이면 오류 1 을 돌려준다 [확실]. 추적 값 0
    uint32_t size;        // +0x10 추적 값 0x01800000 (읽지 않음)
    uint32_t format;      // +0x14 추적 값 'rawb' (읽지 않음)
    uint32_t count;       // +0x18 추적 값 1 (읽지 않음)
    uint32_t out[6];      // +0x1c 출력. funcId 1,2,0x80: {0x40, 0x80, 0, 0, 0, 0x80}
                          //            funcId 0x20:     {8, 8, 0, 0, 0, 0}
};
```

근거: 7ffb0866025d 에서 상수(7ffb0869f860 = `40 00 00 00 80 00 00 00 ...`) 16바이트를 +0x1c 에, 0x8000000000 을 +0x2c 에 쓴다. 추적 2524행(d1 1836행) 전후 비교에서 +0x1c=0x40, +0x20=0x80, +0x30=0x80 로 바뀐다. 출력 값의 의미(정렬, 여유 공간 등)는 모른다. AppleGVA 는 이 호출 뒤에 크기 0x01800000(24MB), 개수 1 로 풀을 만들었는데 이 숫자는 드라이버가 준 것이 아니라 AppleGVA 가 넣은 값이다.

### 1.2 AllocateBitstreamBufferPool(driver, AVD_DriverBitstreamBufferPoolInfo*)

`AvdVAInterface::AllocateBitstreamBufferPool` 7ffb08660296 에서 `AvdVAContext::AllocateBitStreamBufferPool(unsigned poolIndex, info)` 7ffb08662062 로 간다.

```c
struct AVD_DriverBitstreamBufferPoolInfo {   // 0x20
    uint32_t poolType;    // +0x00 0 또는 0x16 이면 풀 0, 5 면 풀 1, 그 밖은 오류 5 [확실]. 추적 값 0
    uint32_t unk04;       // +0x04 추적 값 0 (읽지 않음)
    uint32_t size;        // +0x08 버퍼 하나의 바이트 크기 [확실]. 추적 값 0x01800000
    uint32_t format;      // +0x0c 추적 값 'rawb' (읽지 않음)
    uint32_t unk10;       // +0x10 추적 값 1 (읽지 않음)
    uint32_t count;       // +0x14 버퍼 개수, 1 이상 32 이하 [확실]. 추적 값 1
    void   **addrs;       // +0x18 호출자가 준비한 void*[count] 배열. NULL 이면 오류 5.
                          //       드라이버가 버퍼마다 CPU 주소를 써 준다 [확실]
};
```

동작 [확실]:
- 풀은 드라이버 객체 안에 2개: `driver + 0x20 + poolIndex*0x110` 에 `{u32 allocated; u32 poolType; u32 size; u32 count; VAVendorTexture *tex[32];}`.
- 이미 할당돼 있으면 먼저 해제한다.
- 버퍼마다 `VAVendorTexture::createAgpTexture(ioConnect, &tex[i], size, 1, 4)` (7ffb08662132), 그리고 `addrs[i] = tex[i]->cpuAddr` (텍스처 +0x98, 7ffb08662156).
- `poolType == 0` 이면 `vaContext + 0x12550 = size` 로 기록하고, 디코더는 첫 DecodePicture 때 이 값을 "비트스트림 최대 크기"(`decoder+0x8c`)로 읽는다. 0 이면 DecodePicture 가 3 을 돌려주므로 **첫 디코드 전에 풀 0 을 반드시 만들어야 한다**(7ffb0860a41b).
- 반환: 0 성공, 5 인자 오류, 1 드라이버 미초기화, 7 텍스처 생성 실패.

추적(3181행, d1 2011행): 호출 전 `addrs[0] = 0`, 호출 후 `addrs[0] = 0x1035d9000`(d1 에서는 0x103097000). 이후 그 주소의 내용이 `00 00 00 01 28 01 af 08 ...`(시작 코드 + NAL 타입 20 IDR_N_LP)로 채워져 있다(d1 6570행 부근).

### 1.3 FreeBitstreamBufferPool(driver, uint32_t poolType)

7ffb086602d2. 둘째 인자는 구조체가 아니라 **값**이다. 0 또는 0x16 이면 풀 0, 5 면 풀 1. 텍스처를 지우고 allocated 를 0 으로. 추적(26199행): `(driver, 0, ...)`.

---

## 2. ScheduleAVDCommands(driver, AVD_CmdList*, AVD_RefConList*)

`AvdVAInterface::ScheduleAVDCommands` 7ffb08660378 는 `AvdVAContext::ScheduleCommandList` 7ffb08662896 로 바로 점프한다.

```c
struct AVD_CmdList {            // 0x10
    uint32_t      count;        // +0x00
    uint32_t      pad;
    AVD_CmdInfo **cmds;         // +0x08 포인터 배열
};

struct AVD_RefConList {         // 0x10
    uint32_t         count;     // +0x00 cmdList.count 와 다르면 오류 5 [확실]
    uint32_t         pad;
    _CommandRefCon **refCons;   // +0x08 출력. 명령마다 드라이버 소유 refcon 포인터가 써진다 [확실]
};

struct AVD_CmdInfo {            // 0x38 [크기는 추정: AppleGVA 객체에서 +0x38 부터 다른 내용]
    uint32_t funcId;            // +0x00 1 = 디코드. (0x80 = 보호 콘텐츠 디코드 포장, 2 = 후처리, 0x10 = 복사, 0x20 = 인코드)
    uint32_t pad;
    uint64_t subId;             // +0x08 0x800 (HEVC). 컨텍스트 생성 때와 같은 값
    uint32_t engineId;          // +0x10 추적 값 4 (능력 목록의 "vcn0 engine" id). 드라이버가 읽는 흔적 없음 [추정]
    uint32_t pad2;
    void    *context;           // +0x18 CreateContexts 가 돌려준 컨텍스트 핸들(= VAVcnDecoder*) [확실]
    void    *param;             // +0x20 코덱별 매개변수 구조체(아래) [확실]
    AVD_CmdInfo *dependsOn;     // +0x28 추적 값 0. 0 이 아니면 목록에서 찾기만 하고 결과는 안 쓴다 [정적]
    AVD_CmdInfo *next;          // +0x30 추적 값 0. funcId 0x80 일 때 안쪽 명령 [정적]
};
```

근거:
- 목록 순회와 refcon 출력: 7ffb086628d4 `cmd = cmds[i]`, 7ffb08662902 `&refCons[i]` 를 넷째 인자로 `ScheduleCommand` 호출, 7ffb08662aa6 `*out = refcon`.
- `ScheduleCommand` 7ffb08662930: `cmd->funcId` 1 또는 0x80 이면 `DecodePicture(cmd, refcon)`.
- 추적 10733행: `arg2+0` = `{1, cmds}`, `cmds[0]` = `arg2+0x20` 을 가리키고 그 내용이 `{1, 0x800, 4, ctx 7f928b709a40, param 600002d25b00, 0, 0}`. AppleGVA 는 CmdList, QueryList, CmdInfo 를 한 객체 안에 이어 붙여 두고 프레임마다 재사용한다.
- `arg3` = `{1, refCons}`, 호출 후 `refCons[0] = 7f928c7434e0`(11934행 뒤).

주의 [확실]: `QueryAVDCommandStatus` 가 `refcon->cmd->context` 를 따라가므로 **AVD_CmdInfo 는 조회가 끝날 때까지 살아 있어야 한다**(스택에 두고 반환하면 안 됨).

### 2.1 _CommandRefCon (드라이버 소유, 호출자는 포인터만 보관)

`VAContext::getRefCon()` 7ffb08608e22 가 `vaContext+0x4e0` 의 0x400칸 원형 배열(칸당 0x48)에서 하나 꺼내 준다. `VAContext::setRefCon(refcon, tex, cmd)` 7ffb08608ef0 가 채운다.

```c
struct _CommandRefCon {         // 0x48, 드라이버 내부
    VAVendorTexture *tex;       // +0x00 디코더의 상태(피드백) 텍스처 = decoder+0xf0
    uint32_t         unk08;
    uint32_t         unk0c;     // +0x0c 0
    AVD_CmdInfo     *cmd;       // +0x10 스케줄 때의 명령 포인터
    ...
};
```

추적(13163행 뒤 `[+0]-> 7f928c7434e0`): `+0 = 7f928b70a700`, `+0x10 = 7f928f246828`(= 명령 주소).

### 2.2 HEVC(subId 0x800) 매개변수 구조체: AVD_CmdInfo.param

`AvdVAContext::DecodePicture` 7ffb08662abc 의 0x800 분기(7ffb08662d79), `AvdHevcDecDataProcessor::Process` 7ffb0861bb1c, `parseNalUnitHeader` 7ffb0861bbb8, `decodeRPS` 7ffb0861bc84, `SetBitstreamFields` 7ffb0861c26e, `AvdVcnHevcDecDataProcessor::SetDecodeMessageFields` 7ffb086446fa, `getDecodeTargetArea` 7ffb0861ce7a 에서 읽는 오프셋을 모았다.

```c
struct AVD_HevcDecodeParam {            // 최소 0x74
    uint32_t  curPicIdx;                // +0x00 현재 그림 번호. 하위 1바이트가 디코드 메시지에 실리고
                                        //       드라이버 DPB 표에 (번호, POC)로 기록된다 [확실]. 추적: 0, 1, 2
    uint32_t  curPoc;                   // +0x04 현재 그림의 POC [확실: decodeRPS 가 델타 POC 에 더한다]. 추적: 0, 3, 2
    uint32_t  unk08;                    // +0x08 DPB 표 칸의 "사용 중" 자리에 그대로 복사된다. 추적 값 0. 0 으로 둘 것
    uint32_t  pad0c;
    IOSurfaceRef output;                // +0x10 출력 표면 [확실]
    void     *sps;                      // +0x18 hevc_sequence_parameter_set_t* (파싱된 SPS) [확실]
    void     *pps;                      // +0x20 hevc_picture_parameter_set_t* (파싱된 PPS) [확실]
    uint32_t  sliceCount;               // +0x28 slices 항목 수 [확실]. 추적 값 1. 0 이면 오류 3
    uint32_t  pad2c;
    AVD_HevcSlice *slices;              // +0x30 항목당 0x328 바이트 배열 [확실]
    uint32_t  drm38, drm3c, drm40;      // +0x38 암호화(부분 복호) 설정. decoder+0x9c 가 켜졌을 때만 읽음 [정적]
    uint8_t   drmIv[16];                // +0x44 위와 같음 [정적]
    uint32_t  pad54;
    uint32_t  targetX;                  // +0x58 디코드 대상 영역 원점 x [정적]. 추적 값 0
    uint32_t  targetY;                  // +0x5c 원점 y [정적]. 추적 값 0
    uint32_t  nalInfo[4];               // +0x60 암호화 경로에서 NAL 헤더 정보를 직접 줄 때만 [정적]
    uint32_t  nalTid;                   // +0x70 위와 같음 [정적]
};

struct AVD_HevcRefEntry {               // 0x18 [추정: 추적 값의 모양]
    uint32_t  picIdx;                   // 참조 그림 번호
    uint32_t  poc;                      // 참조 POC
    uint32_t  valid;                    // 1
    uint32_t  pad;
    IOSurfaceRef surface;               // 참조 그림의 IOSurface
};

struct AVD_HevcSlice {                  // 0x328 [크기 확실: imul 0x328]
    void     *bufferBase;               // +0x000 비트스트림 버퍼의 CPU 주소(풀 addrs[i] 와 같은 값). 드라이버는 안 읽음 [추정]
    uint32_t  bufferIndex;              // +0x008 풀 안의 버퍼 번호. 첫 항목 것만 쓴다 [확실]. 추적 값 0
    uint32_t  pad0c;
    uint32_t  offset;                   // +0x010 버퍼 안 시작 오프셋. 첫 항목 것만 쓴다(qword 로 읽음) [확실]. 추적 값 0
    uint32_t  pad14;
    uint32_t  size;                     // +0x018 이 슬라이스의 바이트 수. 전 항목 합이 복사 길이 [확실]
                                        //        추적: 0xa42b, 0x919e, 0x3c25 (= 각 샘플 크기 42027, 37278, 15397)
    uint32_t  unk1c;                    // +0x01c 추적: 0x27, 0x29, 0x26. 드라이버가 읽는 흔적 없음 [모름]
    void     *sliceHeader;              // +0x020 파싱된 슬라이스 헤더 구조체. 첫 항목 것만 쓴다 [확실]
    AVD_HevcRefEntry refList0[16];      // +0x028 [추정]. 추적 프레임 1: {0, 0, 1, 0, 프레임0 표면}
    AVD_HevcRefEntry refList1[16];      // +0x1a8 [추정]. 추적 프레임 2: {1, 3, 1, 0, 프레임1 표면}
};
```

드라이버가 실제로 읽는 필드(0x800 경로) [확실]:

| 구조체 | 오프셋 | 쓰임 |
|---|---|---|
| sps | +0x04 | u32, sps_max_sub_layers_minus1 로 보임. `sps[0xe3c + 4*이 값] + 1` 이 DPB 크기 |
| sps | +0x14, +0x19, +0x1a | chroma 관련 판정. 추적: +0x14 = 1, +0x19 = 1, +0x1a = 1 |
| sps | +0xe08, +0xe0c | 플래그 바이트 |
| sps | +0xe10 (u32 2개) | 너비, 높이. DecodePicture 가 `decoder+0x7c` 에 복사(추적: 0x780, 0x438) |
| sps | +0xe2c, +0xe30 | bit_depth_luma_minus8, bit_depth_chroma_minus8 로 보임(0 이 아니면 10비트 처리) |
| sps | +0xe34 | log2_max_pic_order_cnt_lsb_minus4 (decodeRPS 에서 +4 해서 시프트) |
| sps | +0xe3c[] | max_dec_pic_buffering_minus1 배열로 보임 |
| sps | +0x113c 부터 u32 4개, +0x1154 | 블록 크기 로그 값들과 플래그 |
| sps | +0x2a04..+0x2a4c | 플래그와 PCM, 필터 관련 값 |
| sps | +0x2b34 + 0x188*n | 단기 RPS 배열(항목 0x188)의 한 바이트 |
| sps | +0x8dd8, +0x8ddc, +0x8e80, +0x8e81 | 플래그. **가장 먼 접근이 +0x8e81 이므로 SPS 구조체는 최소 0x8e82 바이트** |
| pps | +0x08..+0x40, +0x44[], +0x94[] | 플래그, 타일 열과 행 크기 배열 |
| pps | +0xec..+0xfc, +0x19ac..+0x19e4 | 플래그, 스케일링 관련. **최소 0x19e5 바이트** |
| sliceHeader | +0x20 | slice_pic_order_cnt_lsb (DPB 표에 기록) |
| sliceHeader | +0x24, +0x30 | short_term_ref_pic_set_sps_flag, 그 인덱스 |
| sliceHeader | +0x108, +0x10c | num_negative_pics, num_positive_pics |
| sliceHeader | +0x114[], +0x124[] | used_by_curr_pic_s0, s1 (바이트) |
| sliceHeader | +0x134[], +0x174[] | delta_poc_s0, s1 (i32) |
| sliceHeader | +0x1b4, +0x1b8, +0x24c[], +0x7bc[], +0x7fc[], +0x80c[] | 장기 참조 관련. **최소 0x84c 바이트** |

SPS, PPS, 슬라이스 헤더 구조체의 정확한 전체 크기와 나머지 필드 의미는 이번 범위 밖이라 해석하지 않았다. 필드 이름 대응은 값의 쓰임에서 붙인 [추정]이다. 추적 덤프는 구조체 앞 0x200 바이트만 담고 있어서(대부분 0) 전체 내용을 로그에서 복원할 수 없다.

DecodePicture 에서 VADecoder::DecodePicture 로 넘어가는 경로 [확실]:

```
AvdVAContext::DecodePicture(cmd, refcon)                    7ffb08662abc
  decoder = cmd->context                                    (+0x18)
  p       = cmd->param                                      (+0x20)
  decoder+0x7c(8바이트) = *(u64*)(p->sps + 0xe10)           7ffb08662d85
  bsIdx   = p->slices[0].bufferIndex                        7ffb08663108
  풀 선택: driver+0x24(풀0 타입)==0 이면 풀 0,
          아니면 driver+0x134(풀1 타입)==0 일 때 풀 1, 그 밖은 오류 5
  bsTex   = pool.tex[bsIdx]
  VAContext::getIOSurfaceBuffer(vaCtx, p->output, &outTex, flag, NULL, true)   7ffb0866313f
  decoder->vtable[0x48](decoder, p, bsTex, outTex)          7ffb08663166
      = VADecoder::DecodePicture(void* p, VAVendorTexture* bsTex, VAVendorTexture* outTex)  7ffb0860a2c2
  VAContext::setRefCon(refcon, decoder+0xf0 의 텍스처, cmd) 7ffb0866317a
```

즉 `VADecoder::DecodePicture` 의 `void*` 는 **cmd->param 자체**(AVD_HevcDecodeParam*), 둘째 인자는 풀의 비트스트림 텍스처, 셋째 인자는 출력 IOSurface 로 만든 텍스처다. 그 안에서 `decoder+0x110`(데이터 처리기)의 `Process(p, bsTex, outTex, dstTex, buildInfo)` 를 부른다(7ffb0860a50d).

비트스트림 처리 [확실]:
- `SetBitstreamFields`: `total = sum(slices[i].size)`, `total > decoder+0x8c(풀 size)` 면 오류. `src = bsTex->cpuAddr + slices[0].offset`. 하드웨어 설정 비트(0x80)가 꺼져 있으면 `memcpy(내부 텍스처, src, total)`.
- `parseNalUnitHeader`: `src` 부터 `00 00 01` 을 찾아 그 뒤 2바이트에서 nal_unit_type, layer id, temporal id 를 뽑는다. 따라서 **버퍼 내용은 Annex B 형식**이어야 한다. 추적에서는 길이 접두 4바이트를 `00 00 00 01` 로 바꾼 것과 크기가 일치한다(샘플 42027바이트 = slices[0].size 0xa42b).
- nal_unit_type 이 16..23(IRAP)이면 드라이버 DPB 표를 비우고, 19 또는 20(IDR)이면 RPS 계산을 건너뛴다(7ffb0861bc9a).

참조 프레임 [확실 + 일부 추정]:
- 드라이버는 처리기 안 `+0x54` 에 16칸 표 `{u8 picIdx; u32 poc; u32 inUse; u32 pocLsb}` 를 둔다. 매 프레임 `decodeRPS` 가 슬라이스 헤더의 RPS 와 `curPoc` 로 참조 POC 들을 계산해 표에서 찾고, 찾은 칸의 picIdx 를 디코드 메시지의 참조 목록에 넣는다. 프레임 끝에 빈 칸에 `{curPicIdx, curPoc, unk08, pocLsb}` 를 기록한다(7ffb0864534c).
- 실제 참조 화소는 드라이버 내부 DPB 텍스처(`decoder+0x58` 아래, 크기는 `CalculateDPBSize`)에 있다. 호출자가 참조 표면을 넘길 필요는 없어 보인다 [추정: 0x800 경로에서 slices[].refList 를 읽는 코드를 못 찾음].
- 따라서 호출자는 **디코드 순서대로**, 서로 겹치지 않는 `curPicIdx`(DPB 크기 미만)와 올바른 `curPoc` 를 줘야 한다.

출력 표면 [확실]:
- `getIOSurfaceBuffer` 7ffb08608f7c: `IOSurfaceGetID`, `IOSurfaceBindAccel(surface, vaCtx+0x24, 0)`, ID 로 캐시 조회, 없으면 `IOSurfaceGetWidth/Height/BytesPerRowOfPlane/BytesPerElementOfPlane` 으로 `VAVendorTexture` 를 만들고 `CreateIOSurfaceTexture` 7ffb086075f8 (`IOAccelResourceCreate`). 평면 수가 2 이상인지에 따라 내부 형식 코드가 갈린다.
- 추적의 표면은 1920x1080, 평면 2개(1920x1080, 1920x540 으로 보이는 값)짜리였다. 화소 형식은 컨텍스트 생성 때 준 'NV12' 에 대응하는 2평면 8비트 4:2:0 로 보면 된다 [추정: '420v' 인지 '420f' 인지는 확인 못 함].

### 2.3 참고: 다른 코덱의 param 배치 (정적 분석만, 추적 없음)

`AvdVAContext::DecodePicture` 분기에서 읽는 것만 적는다 [정적].

- **AV1 (0x1000000)**, 7ffb08662cd7:
  - `param+0x00` 포인터: 시퀀스 헤더 구조체(`+0x00` 프로필, `+0x524`, `+0x528` 로 8/10/12비트 판정)
  - `param+0x08` 포인터: 프레임 헤더 구조체. `+0x134`, `+0x13c` 는 너비-1, 높이-1 로 쓰이고, `+0x138`, `+0x140` 이 `decoder+0x7c`, `+0x80` 으로 복사된다
  - `param+0x18`: 출력 IOSurfaceRef
  - `param+0x60` 포인터: 비트스트림 참조 `{u32 bufferIndex(+0); ...; u32 size(+0x10)}`. 오프셋 없이 버퍼 처음부터 `size` 바이트를 쓴다(`AvdVcnAv1DecDataProcessor::SetBitstreamFields` 7ffb0864564e)
  - 풀 0 의 poolType 이 0 이어야 한다
- **HEVC 옛 경로 (0x400)**, 7ffb08662bb3: `param+0x00` 출력 IOSurfaceRef, `param+0x10` 포인터가 `{u32 poolType; u32 bufferIndex}`.
- **H.264 (8)**: 비트스트림 `{poolType(param+0x270), index(param+0x274)}`, 출력 표면은 `cmd->next->param+0x10`(다음 명령의 매개변수)에서 가져온다.

---

## 3. QueryAVDCommandStatus(driver, AVD_QueryList*)

`AvdVAInterface::QueryAVDCommandStatus` 7ffb08660382 에서 `AvdVAContext::QueryCommandStatus` 7ffb08663b0e.

```c
struct AVD_QueryEntry {         // 0x18 [확실: 반복 간격 0x18]
    _CommandRefCon *refCon;     // +0x00 ScheduleAVDCommands 가 refCons[i] 에 써 준 포인터
    uint32_t engineId;          // +0x08 추적 값 4. 드라이버는 안 읽음 [추정]
    uint32_t wait;              // +0x0c 0 이 아니면 끝날 때까지 막고 결과 코드까지 받음. 0 이면 즉시 확인만
    uint32_t status;            // +0x10 출력
    uint32_t result;            // +0x14 출력
};

struct AVD_QueryList {          // 0x10
    uint32_t        count;      // +0x00
    uint32_t        pad;
    AVD_QueryEntry *entries;    // +0x08 항목 배열(포인터 배열이 아니라 항목 자체). NULL 이면 오류 5
};
```

동작 [확실]:
- 항목마다 `decoder = entry.refCon->cmd->context`. 셋 중 하나라도 NULL 이면 오류 5.
- `wait != 0`: 먼저 `status = 3, result = 4` 로 채우고 `VADecoder::waitForStatusEvent(decoder, refCon, &s, &r)` 7ffb0860aa8e 호출. 여기서 상태 텍스처의 이벤트를 **기다린 뒤** 하드웨어 피드백을 읽는다. 성공하면 `status = 1`, `result = 표[하드웨어 상태]`, 표(7ffb0868e32c)는 `{0, 5, 6, 7, 7, 4, 1, 3}`, 범위를 벗어나면 3. **result 0 이 정상 디코드**다.
- `wait == 0`: `VADecoder::checkForStatusEvent` 7ffb0860ab66. 끝났으면 `status = 1`, 아니면 `status = 2`, `result = 0`.
- 함수 반환값은 0(성공), 1(드라이버 미초기화), 5(인자 오류).

추적(13163행): 호출 전 항목 `{refCon 7f928c7434e0, 4, wait 1, 0, 0}`, 호출 후 `status = 1, result = 0`(14249행 뒤). AppleGVA 는 스케줄과 다른 스레드(0x70000340a000)에서 `wait = 1` 로 한 번 부른다. 직접 호출 때는 같은 스레드에서 스케줄 직후 불러도 된다 [추정: 드라이버 코드에 스레드 제약은 안 보임].

---

## 4. GetDriverParameter, SetContextParameter 의 필요성

- **GetDriverParameter** 7ffb0866064c: `AVD_DriverParamList { u32 count; pad; struct { u32 id; pad; void *value; } *items; }`(항목 0x10). 추적(5811행)은 `count = 1, id = 0xe`. id 0xe 는 점프 표(7ffb08660754)에서 7ffb086606d2 로 가고, `AvdCapability::refreshCapabilityList()` 를 부른 뒤 `value = 능력 객체 + 8`(= CreateAccelerator 가 돌려준 능력 목록과 같은 포인터)을 써 준다. 추적에서도 호출 후 value 가 `capability + 8` 로 바뀐다. **디코딩에 필수 아님** [확실: 능력 목록 재조회일 뿐이고 CreateContexts 가 내부에서 같은 갱신을 한다]. 다른 id: 1 = 기기 정보, 2 와 8 = 키 교환, 9 = 기능 가용성, 0xd = 디코더 대역폭 한도.
- **SetContextParameter** 7ffb086603a0: 항목 0x30 바이트 `{u32 funcId; ...; void *ctx(+0x18); u32 paramId(+0x20); void *value(+0x28)}`. funcId 1 은 콘텐츠 키 설정(paramId 1) 또는 `SetPictureDecodeParam`, 0x20 은 인코더 설정. HEVC 처리기의 `ProcessPictureDecodeParam` 은 아무것도 안 하고 1 을 돌려준다(7ffb0861c134). 추적에서 한 번도 불리지 않았다. **필수 아님** [확실].
- **GetBitstreamBufferInfo** 도 순수 조회라 건너뛰어도 된다 [확실].

---

## 5. DeleteContexts, DeleteDriver

```c
struct AVD_DriverContextDelete {    // 0x10
    uint32_t count;                 // +0x00
    uint32_t pad;
    void   **contexts;              // +0x08 컨텍스트 핸들 배열
};
```

- `DeleteContexts(driver, AVD_DriverContextDelete*)` 7ffb086601c4: 핸들마다 `AvdVAContext::DeleteContext` 를 부르고 능력 목록을 갱신. 추적(28183행): `{1, contexts}`, `contexts[0] = 7f928b709a40`(= cmd->context 와 같은 값).
- `DeleteDriver(driver)` 7ffb086600b4: **인자 하나**. CP 해제, `VAContext::Terminate`, 객체 삭제. 추적의 둘째 인자 이후는 쓰레기 값.
- 추적의 해제 순서: FreeBitstreamBufferPool(driver, 0), DeleteContexts, DeleteDriver.

---

## 6. HEVC 한 프레임 디코딩 호출 순서

추적에서 본 순서: CreateAccelerator, CreateDriver, GetBitstreamBufferInfo, AllocateBitstreamBufferPool, GetDriverParameter(0xe), CreateContexts, [프레임마다 ScheduleAVDCommands, QueryAVDCommandStatus], FreeBitstreamBufferPool, DeleteContexts, DeleteDriver.

직접 호출용 최소 순서와 값:

1. CreateAccelerator, CreateDriver: avdprobe.c 그대로.
2. CreateContexts: avdprobe.c 그대로, `id = 1, sub = 0x800, hw = 4, bandwidth = 0x058fd400(추적 값), width, height, inFmt 'rawb', inCnt 1, outFmt 'NV12', outCnt 1`(7909행의 값).
3. AllocateBitstreamBufferPool (CreateContexts 앞이든 뒤든 첫 디코드 전이면 됨 [추정: 추적은 앞]):
   ```c
   void *bsAddr[1] = {0};
   struct AVD_DriverBitstreamBufferPoolInfo bi = {0, 0, 0x01800000, 'rawb', 1, 1, bsAddr};
   tbl[6](driver, &bi);          // 함수 표 [6] = AllocateBitstreamBufferPool
   uint8_t *bs = bsAddr[0];
   ```
4. 프레임마다:
   ```c
   // 비트스트림: 샘플의 각 NAL 길이 접두 4바이트를 00 00 00 01 로 바꿔 bs 에 복사
   memcpy(bs, annexb, len);

   static AVD_HevcSlice slice;                 // 0x328, 0 으로 초기화
   slice.bufferBase  = bs;
   slice.bufferIndex = 0;
   slice.offset      = 0;
   slice.size        = len;
   slice.sliceHeader = parsedSliceHeader;      // 파싱된 슬라이스 헤더 구조체 (최소 0x84c)

   static AVD_HevcDecodeParam p;               // 0 으로 초기화
   p.curPicIdx  = picIdx;                      // 디코드 순서대로 비는 번호
   p.curPoc     = poc;
   p.output     = ioSurface;                   // IOSurfaceRef, NV12 2평면
   p.sps        = parsedSps;                   // 최소 0x8e82
   p.pps        = parsedPps;                   // 최소 0x19e5
   p.sliceCount = 1;
   p.slices     = &slice;

   static AVD_CmdInfo cmd;                     // 조회가 끝날 때까지 유지
   cmd.funcId = 1; cmd.subId = 0x800; cmd.engineId = 4;
   cmd.context = ctxs[0]; cmd.param = &p;
   AVD_CmdInfo *cmds[1] = {&cmd};
   AVD_CmdList cl = {1, 0, cmds};
   _CommandRefCon *rc[1] = {0};
   AVD_RefConList rl = {1, 0, rc};
   tbl[11](driver, &cl, &rl);                  // [11] = ScheduleAVDCommands, 0 이면 성공

   AVD_QueryEntry qe = {rc[0], 4, 1, 0, 0};    // wait = 1
   AVD_QueryList ql = {1, 0, &qe};
   tbl[12](driver, &ql);                       // [12] = QueryAVDCommandStatus
   // qe.status == 1 && qe.result == 0 이면 ioSurface 에 그림이 있다
   ```
   함수 표 번호는 avdprobe.c 의 `tbl[1] = CreateDriver` 기준(가속기 구조체 +8 이 `tbl[1]`): CreateDriver 1, DeleteDriver 2, CreateContexts 3, DeleteContexts 4, GetBitstreamBufferInfo 5, AllocateBitstreamBufferPool 6, FreeBitstreamBufferPool 7, ScheduleAVDCommands 11, QueryAVDCommandStatus 12, GetDriverParameter 16.
5. 끝: `tbl[7](driver, 0)`, `tbl[4](driver, &(AVD_DriverContextDelete){1, 0, ctxs})`, `tbl[2](driver)`.

반환 코드(공통): 0 성공, 1 드라이버 미초기화 또는 일반 실패, 3 디코드 준비 실패(풀 없음 포함), 5 인자 오류, 7 할당 실패.

---

## 7. 아직 모르는 것

- `hevc_sequence_parameter_set_t`, `hevc_picture_parameter_set_t`, 슬라이스 헤더 구조체의 **전체 필드 배치와 정확한 크기**. 위 표의 오프셋만 확인했다. 실제로 HEVC 를 직접 돌리려면 드라이버가 읽는 모든 오프셋(특히 `SetDecodeMessageFields` 의 벡터 연산 구간 7ffb08644c80..7ffb08644de0 과 `setupScalingListBuffer` 7ffb0861ccd0)을 마저 풀거나, 추적 도구로 이 세 구조체를 0x9000, 0x1a00, 0x900 바이트씩 통째로 덤프해 재사용해야 한다. 지금 로그에는 앞 0x200 바이트만 있다.
- `AVD_HevcSlice.unk1c`(0x27, 0x29, 0x26)의 의미. 드라이버가 읽지 않는 것으로 보인다.
- `slices[].refList0/1` 의 참조 IOSurface 를 드라이버가 정말 안 쓰는지. 0x800 HEVC 경로의 주요 함수에서는 접근이 없었지만 `VADecoder::DecodePicture` 뒷부분(7ffb0860a88f 이후)과 `Vcn3DecCommand` 쪽은 다 읽지 않았다.
- `needDecodeAndCopy` 경로(대상 영역이 표면보다 클 때 중간 텍스처로 디코드 후 복사)가 언제 켜지는지. 추적에서는 `targetX/Y = 0` 이고 표면 크기가 그림 크기와 같았다.
- GetBitstreamBufferInfo 출력 값(0x40, 0x80, 0x80)의 의미.
- `AVD_CmdInfo.engineId`, `AVD_QueryEntry.engineId` 가 정말 무시되는지(읽는 코드는 못 찾았다).
- 출력 IOSurface 의 정확한 화소 형식 요구('420v' 대 '420f'), 그리고 조회가 끝난 뒤 표면 내용이 바로 유효한지(추가 동기화가 필요한지).
- subId 0x400 과 0x800 의 차이가 "세대"인지 "비트 깊이"인지. 이 GPU 의 능력 목록에 둘 다 나오는지는 avdprobe 출력으로 확인할 것.
- AV1 절(2.3)은 정적 분석뿐이다. AV1 시퀀스 헤더와 프레임 헤더 구조체의 배치는 `AvdVcnAv1DecDataProcessor::SetDecodeMessageFields` 7ffb08645728 을 풀어야 한다. AppleGVA 는 심볼이 28개뿐이라 호출자 쪽에서 구조체를 채우는 코드를 이름으로 찾을 수 없었다.
