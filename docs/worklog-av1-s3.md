# M8 VCN VP9, AV1, JPEG 하드웨어 디코딩 조사 노트 (2026-11-10, 진행 중)

## 도구
- M8 dyld 공유 캐시 사본: `dl/dsc/` (gitignore 대상, 4.9GB). 로컬 dsc_extractor는 서명 검사로 거부해서 직접 파서를 씀.
- 파서와 디스어셈블러: `tools/re/dsc.py` (images, syms, str, dis 하위 명령). capstone이 필요하다.
- 커널 확장은 기존 `kcdis.py` (accel, hwlibs, hwsvc).

## 확인된 사실 (근거: 심볼, 디스어셈블, ioreg)
1. VideoToolbox VP9 하드웨어 디코더는 `/System/Library/Video/Plug-Ins/AppleGVAVPXDecoder.bundle`이고 Info.plist의 `IORegistryRequiredKey = IOGVAXDecode`로 걸린다. 이 키는 KC 안에서 AppleIntelICLGraphics, AppleIntelKBLGraphics 퍼스낼리티에만 있다(값 "2"). M8 ioreg에는 `IOGVACodec = AMDVCN2`, `IOGVAHEVCDecode` 등만 있고 `IOGVAXDecode`는 없다.
2. AppleGVA에는 VP9 구문 해석기(`Flow_VP9Decoder`, `VP9_Syntax`, `VP9_RLM`, `VP9_Core`)가 있으나 AV1 문자열은 없다.
3. `AMDRadeonVADriver2`(유저랜드, 공유 캐시)에는 `AvdVcnAv1DecDataProcessor`(필름 그레인 포함)와 `AvdJpegDecDataProcessor`가 있다. VP9 심볼과 문자열은 전혀 없다.
   - `VAFactory::createDecodeDataProcessor`는 종류 0부터 6까지: AVC, JPEG, MPEG2, MPEG2 VLD, VC1, HEVC, AV1.
   - `AvdVAContext::GetVAFunctionType`: funcId 1(디코드)의 subId 8=AVC(0), 0x10(1), 2(2), 0x40(3), 0x400=HEVC(4), 0x800(5), 0x1000000=AV1(6).
   - `AvdCapability::initDriverCapability`: 함수 표 41개, 색인 6(AV1, subId 0x1000000)은 능력 비트 7. `VAAcceleratorInfo::getExtraIdInfo`에서 하드웨어 종류 8, 9, 10(VCN3, Navi 2x)의 능력 마스크는 0x3de61ef로 비트 7이 켜져 있고, 종류 5, 6, 7은 0x3de616f로 꺼져 있다. M8은 device-id 0x73ff(Navi 23)로 위장하므로 AV1 기능이 광고되는 쪽이다(실행으로는 미확인).
4. 커널 X6000 가속기에는 VCN2 dec/enc, VCN3 unified 링과 채널만 있고 JPEG 링은 없다(HWLibs에는 jpeg SWIP이 있음). `AMDVCNHWEngine::requestCapability`는 코덱을 가리지 않고 대역폭만 계산한다.

## 판단
- AV1: 드라이버에 경로가 있음. AppleGVA와 VideoToolbox 쪽에 호출자가 없음. 직접 AVD 인터페이스(`AVD_CreateAVDAccelerator`)를 불러 디코딩하는 클라이언트와 VT 디코더 플러그인이 필요.
- VP9: 드라이버에 경로 없음. VCN 메시지와 확률 표를 직접 만들어야 함(Mesa radeon_vcn_dec.c 참고).
- JPEG: 유저랜드 처리기는 있으나 커널에 JPEG 링이 없어 가장 어려움.

## 16:20 갱신: 실행으로 확인한 것 (커밋 b49fa03)
- `tools/avdprobe`(M8의 `~/m8/vt/avdprobe`): AMD VA 드라이버를 직접 불러 기능 목록 출력. M8에서 엔진 2개(vcn0, graphics), 기능 23개, 그중 `func 5: id 0x1 sub 0x1000000 decode AV1` 확인. VP9에 해당하는 기능은 없음.
- `avdprobe ctx 0x1000000`: CreateDriver, CreateContexts(AV1, 1920x1080) 모두 0(성공) 반환. 실제 프레임 디코딩은 아직 안 함.
- `tools/avdtrace`: VideoToolbox HEVC 하드웨어 디코딩 중 AppleGVA가 부르는 AVD 함수 표 호출을 가로채 인자를 덤프. 프로세스 내 디코딩이 필요해서 `defaults write com.apple.coremedia VTDecodeServer -string never`를 켜고 실행 후 지움(VideoToolbox `VTDecompressionSessionCreateWithOptions`가 이 키를 읽음, 값 always 또는 never).
- 호출 순서(HEVC): AVD_CreateAVDAccelerator, CreateDriver, GetBitstreamBufferInfo, AllocateBitstreamBufferPool, GetDriverParameter, CreateContexts, 프레임마다 ScheduleAVDCommands와 QueryAVDCommandStatus(다른 스레드), 끝에 FreeBitstreamBufferPool, DeleteContexts, DeleteDriver.
- VideoToolbox는 `/System/Library/Video/Plug-Ins`와 `/Library/Video/Plug-Ins` 문자열을 갖고 있음(후자를 실제로 훑는지는 미확인). `/Library/Video/Plug-Ins`는 M8에 있지만 root 소유라 sudo 없이 못 씀.
- 시험 영상: `tools/vtprobe/test-av1.mp4`, `test-av1.obu`(SVT-AV1, 1920x1080 8비트, 필름 그레인 없음), `test-vp9.mp4`.

## 16:19경 M8 무응답은 유휴 잠자기였음(해결)
- pmset 기록에 16:18:25 Idle Sleep 진입(시스템 sleep 1분 설정, S3 깨우기는 알려진 대로 실패). 패닉 아님. 사용자가 재부팅. 원격 작업 중에는 M8에서 `caffeinate -is -t 14400 &`를 걸어 둘 것. avdprobe 시험과는 무관.

## 진행 중인 역분석(에이전트 결과 문서)
- `avd-decode-command-layout.md`: ScheduleAVDCommands 등 디코드 명령 구조.
- `avd-av1-picparam-layout.md`: AvdVcnAv1DecDataProcessor 입력 구조.

## 16:35 갱신: AV1 하드웨어 디코딩 성공 (커밋 "feat: AV1 하드웨어 디코딩 성공")
- `tools/av1dec/av1dec.c`(M8의 `~/m8/vt/av1dec`): `av1hdr.c`로 OBU를 해석하고 AMD VA 드라이버의 AV1 처리기를 직접 호출. `av1dec FILE.obu N out.nv12`.
- test-av1.obu(1080p 8비트, 화면 간 예측, show_existing_frame, CDEF, 루프 복원 포함) 120프레임이 ffmpeg 소프트웨어 디코딩과 화소 단위로 일치(`tools/av1dec/cmp.py`).
- 커널 확장, 펌웨어, OpenCore 설정은 바꾸지 않음(config-usbintx 그대로).
- 동기 호출(프레임마다 QueryAVDCommandStatus wait=1) 기준 프레임당 23에서 28ms. 1080p와 4K가 비슷해서 대기 방식이 병목으로 보임(미확인).
- 구조체 채우는 규칙: CDEF는 lo 칸에 `pri << 2 | sec(원시 2비트)`, hi 칸 0. qm은 `level | 0xf0`. 참조는 DPB 칸 번호(0..8), 빈 칸은 0x7f. 비트스트림은 첫 타일부터 마지막 타일 끝까지를 풀 버퍼 0에 복사하고 타일 오프셋은 그 시작 기준.
- 아직 안 한 것: 10비트(P010 표면), VideoToolbox 디코더 플러그인, Safari 연동.

## 16:40 갱신: 추가 검증과 남은 일
- 3840x2160 타일 4x2(8개) 60프레임, 1080p 필름 그레인(film-grain=10) 60프레임도 소프트웨어 디코딩과 화소 단위로 일치. 필름 그레인은 드라이버와 펌웨어가 적용함.
- 남은 단계(Safari 유튜브 4K까지):
  1. av01용 VideoToolbox 디코더 플러그인(VTVideoDecoder, 비공개 SPI. WebKit의 VP9 디코더 등록 코드가 참고 구현). 먼저 `VTRegisterVideoDecoder`로 프로세스 내 등록해 vtprobe로 검증.
  2. 시스템 전체 등록: `/Library/Video/Plug-Ins`에 번들 설치(root 필요). VTDecoderXPCService가 서명 안 된 번들을 읽게 하려면 라이브러리 검증 해제가 필요할 수 있음(미확인).
  3. `VTIsHardwareDecodeSupported('av01')`가 참이 되는지, Safari가 유튜브에서 AV1을 고르는지 확인.
  4. 10비트(P010), 비동기 제출로 속도 개선, 해상도 변경과 frame_refs_short_signaling 처리.
- VP9: 드라이버에 처리기가 없어 VCN 메시지와 확률 표를 직접 만들어 넣어야 함. AV1이 되면 유튜브 4K에는 VP9가 필수는 아님.
- JPEG: 커널 가속기에 JPEG 링이 없어 커널 확장 작업이 필요. 유튜브 목표와 무관.

## 16:50 갱신: VideoToolbox 디코더 (커밋 fb8904d)
- `tools/av1dec/av1vt.c`: av01용 VTVideoDecoder. SPI 구조체는 4바이트 정렬(Apple AV1DecoderSW 번들의 표에서 확인). 팩토리 `AV1AMD_CreateInstance`.
- 프로세스 내 등록은 `VTRegisterVideoDecoderWithInfo('av01', {"VTIsHardwareAccelerated": true}, factory)`여야 한다. `VTRegisterVideoDecoder`만 쓰면 목록에는 보이지만 av01 세션에서 선택되지 않는다(-12906). Apple 소프트웨어 AV1 디코더는 사양 키 `EnableAppleOnlySWAV1`이 있어야만 선택된다.
- `vtav1test`(INFO=1): `VTIsHardwareDecodeSupported('av01') = 1`, VideoToolbox 세션으로 120프레임 디코딩, 소프트웨어 결과와 화소 단위 일치. 프로세스 내 디코딩이라 `VTDecodeServer=never`가 필요했다.
- 번들 `AV1AMDDecoder.bundle`(Info.plist의 CMClassImplementations, VTIsHardwareAccelerated=true, IORegistryRequiredKey=IOGVACodec)을 만들었고 M8의 `~/m8/vt/plugins/`에 복사해 둠. `VTRegisterVideoDecoderBundleDirectory`는 앱 자신의 번들 안 디렉터리만 받아서 시험에 못 씀. 실제 시험은 `/Library/Video/Plug-Ins` 설치(root)가 필요.
- 속도 문제(기존 HEVC도 같음): 프레임당 정확히 33.3ms. 제출은 0.3ms이고 나머지는 `VAVendorTexture::waitForEvent`(IOAccel 이벤트) 대기. `vtprobe decode test-hevc.mp4 120`도 4.00초(30.0fps). 재부팅 직후(16:22)에는 같은 명령이 122fps였다. 화면 깨움, 다른 GPU 작업과 무관. VCN 완료 인터럽트가 전달되지 않고 주기적인 무언가에 얹혀 처리되는 것으로 추정(미확인). 60fps 영상에는 부족하므로 RembrandtGfx 쪽 조사 필요.

## 16:58 갱신: 시스템 전체 등록 성공 (임시 마운트)
- `/Library/Video/Plug-Ins`는 못 씀: CoreMedia `figRegistryAddResultItem`이 옵션이 0이 아닌 검색 경로의 번들에 Apple 서명(`kFigRegistryAppleCodeSignRequirement`)을 요구. 무시 설정 `figregistry_ignore_codesign`은 `os_variant_has_internal_content`일 때만. 검사가 없는 경로는 `/System/Library/Video/Plug-Ins`(옵션 0)뿐.
- 해결(봉인 유지): 원본 플러그인 19개 사본과 `AV1AMDDecoder.bundle`을 담은 HFS+ 이미지 `~/m8/vt/vtplugins.dmg`를 그 경로 위에 읽기 전용으로 마운트.
  `sudo hdiutil attach -nobrowse -readonly -owners on -mountpoint /System/Library/Video/Plug-Ins ~/m8/vt/vtplugins.dmg`
  되돌리기: `sudo hdiutil detach /System/Library/Video/Plug-Ins`. 재부팅하면 사라짐(LaunchDaemon으로 영구화는 아직 안 함).
- 결과: 수정 안 한 `vtprobe query`가 `av1 hardware decode: YES`. `vtav1test ... system`이 VTDecoderXPCService 안에서 내 디코더로 120프레임 디코딩, 화소 단위 일치. AMFI가 ad hoc 서명 경고를 남기지만 로드는 됨(현재 SIP 설정에서).
- 비밀번호 없는 sudo: 사용자가 `/etc/sudoers.d/m8-nopasswd` 추가(16:53).

## 17:03 갱신: Safari 유튜브 4K AV1 하드웨어 재생 확인
- 사용자가 Safari 유튜브 통계에서 코덱 av01 확인. 같은 시각 로그: `VTDecoderXPCService: (AV1AMDDecoder) AV1AMD: startSession 3840x2160: AMD VA driver open = 0`(2304x1440, 640x360 세션도). 디코드 실패 로그 없음. 재생이 매끄러운지는 미확인.
- 영구화: `scripts/m8-vtplugins`(M8의 `/usr/local/sbin/m8-vtplugins`), LaunchDaemon `/Library/LaunchDaemons/local.m8.vtplugins.plist`, 이미지 `/Library/Application Support/m8-vtplugins/vtplugins.dmg`. 재부팅 후 자동 마운트는 아직 검증 안 함.
- macOS를 업데이트하면 `sudo m8-vtplugins build ~/m8/vt/plugins/AV1AMDDecoder.bundle`로 이미지를 다시 만들어야 함(빌드 번호가 다르면 마운트하지 않게 해 둠).
- 남은 문제: 디코딩 완료 대기가 33.3ms 주기에 묶임(HEVC도 동일). 10비트 AV1 미지원(디코더가 거부해 소프트웨어로 넘어가는지 미확인).

## 17:13 갱신: 30fps 제한의 원인과 수정안 (미설치)
- Safari 유튜브 4K60 AV1 재생 중 WebKit 통계: `decode: 32.7 ms`, 172프레임 중 111프레임 표시. 끊김의 원인은 디코딩 완료 통지 지연.
- 측정: 폴링(`POLL=200 av1dec`)으로 보면 완료가 16.7ms의 배수에서만 보임(16.4ms, 33.5ms). Metal 명령 버퍼를 초당 1만 8천 개 돌려도 변화 없음. 즉 VCN 완료 인터럽트가 오지 않고, 화면 수직 동기 때 도는 `AMDSWScheduler::checkTimestamps`가 대신 알아챔(수직 동기가 호출자라는 것은 추정).
- 커널 경로: `AMDAccelEventMachine::waitForStamp`(X6000)가 `IOAccelEventMachine2::waitForStamp`(1초 시한 대기)를 부르고, 깨어나면 `checkTimestamps`를 부름. 깨우는 것은 `signalStamp`.
- 수정안: `patches/vcn.cpp`에 stampPoll 추가. `waitForStamp`를 감싸 대기자 수를 세고, 대기자가 1ms 넘게 있으면 스레드가 `checkTimestamps(scheduler)`를 1ms마다 호출. 끄는 부팅 인자 `-rmbnostamppoll`. 빌드는 `make LD=ld`(ref-winmax2/src/RembrandtGfx). 빌드한 바이너리는 M8의 `~/m8/RembrandtGfx.stamppoll`. 설치하려면 `~/m8/RembrandtGfx`로 이름을 바꾸고 `sudo /usr/local/sbin/m8-apply-oc reboot`.
- 왜 VCN 인터럽트가 안 오는지(근본 원인)는 미확인.

## 17:26 갱신: stampPoll 설치 후 결과
- 17:13 재부팅으로 설치(RembrandtGfx 652cd0c63e11, config-usbintx 그대로). 플러그인 이미지 자동 마운트 확인(`mounted: 21 plug-ins`).
- 12분간 1분 간격 측정: HEVC 122에서 151fps, AV1 1080p 프레임당 8에서 10ms(대기 방식과 폴링 방식 모두). Safari 유튜브 4K60 av01: `enqueued 359, displayed 360, dropped 0에서 1, >16ms late 0`, decode 8에서 9ms.
- 한계: 대조 실험(`-rmbnostamppoll`로 부팅해 느려지는지)은 안 했다. 측정 중 Safari가 재생 중이어서 커널 대기자가 늘 있었으므로 폴링 방식 측정도 stampPoll의 덕을 봤을 수 있다. dmesg에서 stamppoll 로그 줄은 찾지 못했다(버퍼가 돌아간 것으로 보임).

## 17:35 갱신: S3 잠자기 재검토 (실험 전)
- 조치: `sudo pmset -a sleep 0`으로 시스템 유휴 잠자기를 껐다(이전 값 1분). 화면 잠자기 10분은 그대로.
- 펌웨어: FADT flags 0x20c5bd(LOW_POWER_S0_IDLE_CAPABLE 켜짐), DSDT에 `_S3`이 `XS3`로 숨겨져 있고 `SS3 = 0`. GNVS(0x97D5AD98)에 `CNSB`(Modern Standby 표시), `DAS3` 필드. PEP 장치 AMDI0007 존재. 즉 Modern Standby 전용 출하이고 설정 메뉴에 S3 항목이 없다는 기억과 일치.
- 현재 설정(config-usbintx)에는 이미 `XS3 -> _S3`, `SS3 0 -> 1` ACPI 패치가 켜져 있어 macOS가 S3로 들어간다(오늘 16:18 Idle Sleep 진입 기록, 이후 Wake 기록 없음).
- 이전 시도 로그(logs/wake1.log): 잠자기 진입은 끝까지 감(USB4 호스트 라우터 SLPR 핸드셰이크 완료, 마지막 줄 70.43초). 그 뒤 깨우기 흔적 없이 약 94초 뒤(RTC 기준) 새 부팅이 시작됨. 커널이 재개했는지, 펌웨어가 리셋했는지, 사용자가 리셋했는지는 로그로 구분 안 됨.
- 참고: ref-winmax2(같은 Rembrandt SoC, GPD Win Max 2)는 README에서 실제 S3 잠자기와 깨우기가 된다고 함. 조건으로 PSP 정리, GART 저장과 복원, USB4 호스트 라우터 잠자기 핸드셰이크를 듦(건너뛰면 펌웨어의 S3 재개가 CF9 리셋으로 끝남). M8에도 USB4 NHI가 있고 usb4.cpp 로그가 찍힘.
- 지금은 `-rmbnodisklog`가 켜져 있어 디스크 로그(/private/var/log/rmb-msgbuf.log)가 안 쌓임. 깨우기 경로 증거를 남기려면 이 인자를 뺀 설정으로 부팅해야 함.

## 18:00 갱신: S3 깨우기 실험 (진행 중, 사용자 외출로 중단)
- 실험 1(17:45): `pmset sleepnow` 36초 뒤 잠듦. 전원 버튼으로 깨우면 팬은 돌지만 화면 불이 전혀 안 들어오고 ping이 2분 넘게 안 돌아옴. 강제 재부팅. (첫 시도는 caffeinate -s가 잠자기를 막아 실패했음. 실험 전 `pkill caffeinate` 필요.)
- 실험 2(17:50, 디스크 로그 켬): 같은 결과. 디스크 로그는 잠자기 진입 중 75.71초에서 끝나고 깨우기 흔적 없음(logs/wake2.log). 커널이 못 깨어난 것과 디스크 복구 전에 멈춘 것을 구분 못 함.
- wakecrumb 추가(커밋 참고): CMOS 0x58에 단계 기록. 17:56 부팅에서 `wakecrumb: previous boot ended at stage b0 seq 00 (acpi_sleep_kernel hook on)` 확인(값이 재부팅을 넘어 유지됨, 훅 설치됨).
- 실험 3: 17:57경 잠자기를 걸어 둔 상태에서 사용자가 외출. M8는 S3에 잠든 채로 있음. 돌아와서 전원 버튼을 눌러 깨우기를 시도하고, 실패하면 강제 재부팅한 뒤 디스크 로그의 `wakecrumb: previous boot ended at stage XX`를 읽으면 됨:
  `ssh m8lan 'sudo tail -c 2500000 /private/var/log/rmb-msgbuf.log | grep -a wakecrumb | tail -2'`
  해석: 52면 펌웨어가 커널에 제어를 돌려주지 않음(또는 커널의 저수준 재개에서 멈춤). 53이면 커널이 재개했고 이후 드라이버 깨우기에서 멈춤. 54나 55면 더 뒤.
- 현재 M8 상태: 설정은 config-usbintx-wakedebug(디스크 로그 켜짐, 자세한 로그), RembrandtGfx는 stamppoll과 wakecrumb 포함, 시스템 유휴 잠자기 0, caffeinate 없음, AV1 플러그인 자동 마운트 동작. 실험이 끝나면 config-usbintx로 되돌릴 것(디스크 로그가 계속 쌓임).
- stamppoll은 디스크 로그에서 `stamppoll: installed`, `stamppoll: scheduler`로 설치 확인됨.
- 무인 반복을 위한 하드웨어 논의: 깨우기는 RTC 예약이나 Wake-on-LAN으로 대체 가능(미검증), 강제 재부팅은 스마트 플러그와 BIOS의 전원 복구 시 자동 켜짐이 필요. 사용자는 헤이홈 플러그 보유(Tuya 기반일 가능성, 로컬 키 필요).

## 18:02 정정: 실험 3은 걸리지 않았음
- 17:57에 건 `pmset sleepnow`가 실행되지 않았다(전원 기록에 Sleep 없음, ping이 18:00까지 계속 응답). SSH 세션이 닫히면서 지연 실행하던 백그라운드 명령이 같이 죽은 것으로 보임. M8는 S3가 아니라 로그인 창에서 깨어 있는 상태다.
- 다음에는 `ssh m8lan 'nohup sh -c "sleep 3; pmset sleepnow" >/dev/null 2>&1 &'`처럼 nohup으로 걸고, 전원 기록에 Sleep 줄이 생겼는지 ping이 끊겼는지로 확인한 뒤 사용자에게 버튼을 부탁할 것.

## 19:40 갱신: VP9 하드웨어 디코딩 성공 (커밋 8d0e00b, 3152612)
- 방법: 드라이버에 VP9 처리기가 없어 AV1 디코더(subId 0x1000000)를 만들게 하고, `AvdVcnAv1DecDataProcessor`의 가상 함수 표(공유 캐시 __DATA_CONST, mach_vm_protect COPY로 쓰기)에서 세 슬롯을 바꿈.
  - 0x10 `ProcessCreateDecodeSession`: 원본 호출 뒤 생성 메시지(`ctx+0xe0`)의 `stream_type`(메시지 +0x28)을 0x13에서 0x11로.
  - 0x60 `initHWContext`: 문맥 버퍼(`ctx+0x1d0` 텍스처의 `+0x98`, 크기 `ctx+0x1c8` = 0x22b9a00)를 0으로 지우고 VP9 기본 확률(앞 2071바이트)을 씀.
  - 0x70 `SetDecodeMessageFields`: 원본 호출 뒤 메시지 버퍼(`ctx+0x98[ctx+0xd8]` 텍스처의 `+0x98`)에서 색인 항목 0x11(AV1, 0xdf8바이트)을 0xE(VP9, 120바이트)로 바꾸고 VP9 메시지를 씀. 공통 메시지의 `stream_type`, 폭과 높이, `db_pitch`(64 정렬), `db_pitch_uv` 0. 확률 버퍼(`ctx+0x1f0[ctx+0x270]` 텍스처의 `+0x98`)에 VP9 확률 표와 세그먼트 구역.
  - 경로: 처리기 `+0x10` = 디코더, 디코더 `+0x58` = ctx, 디코더 `+0x110` = 처리기. 훅은 드라이버 호출 스레드에서 동기 실행됨(생성은 CreateContexts 안, 나머지는 ScheduleAVDCommands 안).
- DPB는 드라이버의 AV1용 고정 DPB(`dpb_size` 0x2857800 at 1080p)를 그대로 씀. Mesa는 VCN 3에서 동적 DPB tier 2를 쓰지만 고정 DPB로도 동작했다. 참조는 DPB 칸 번호(0..8)로 넘김.
- 비트스트림은 프레임 전체(슈퍼프레임 색인 제외)를 128 배수로 0 채움. AV1용 더미 매개변수(타일 1개, 프레임 크기)를 넘겨 드라이버 코드가 돌게 함.
- 결과(`tools/vp9dec/vp9dec`): 1080p 120프레임, 1080p 타일 4열 60프레임, 4K 타일 20프레임 모두 ffmpeg 소프트웨어 디코딩과 화소 단위 일치. 프레임당 3ms(1080p), 8에서 9ms(4K).
- VideoToolbox: `tools/vp9dec/vp9vt.c`를 AV1AMDDecoder.bundle에 함께 빌드(`VP9AMD_CreateInstance`, Info.plist에 vp09 항목). vtable 교체가 프로세스 전체에 걸리므로 훅은 스레드 로컬(`tCur`)이 설정된 경우에만 동작, 아니면 원본으로 통과. AV1과 VP9 동시 실행 확인.
- VP9는 보조 디코더라 클라이언트가 `VTRegisterSupplementalVideoDecoderIfAvailable('vp09')`를 불러야 목록에 들어감. 그 뒤 `VTIsHardwareDecodeSupported('vp09') = 1`. `vtprobe query`(그 호출 없음)는 여전히 no로 나옴. VTDecoderXPCService에서 120프레임 디코딩, 화소 단위 일치.
- 미확인: Safari 유튜브가 VP9 하드웨어를 고르는지(로그인과 화면 확인 필요), 세그멘테이션이나 숨은 참조 프레임이 든 스트림(시험 영상에 없었음), 10비트(profile 2, 거부함), 스트림 중간 해상도 변경.
- 참고: 역분석을 맡긴 하위 작업은 문서 저장이 안전 분류기에 막혀 결과를 남기지 못했고, 필요한 오프셋은 직접 디스어셈블로 확인함.
- 속도 참고: 이 부팅에서 VideoToolbox HEVC 289fps, AV1 240fps, VP9 86fps.

## 20:05 갱신: 해상도 한계 시험
- 방법: 로컬에서 SVT-AV1과 libvpx로 8프레임짜리 영상을 해상도별로 만들고, M8의 `av1dec`, `vp9dec` 출력 4프레임의 md5를 ffmpeg 소프트웨어 디코딩과 대조.
- AV1과 VP9 모두 2560x1440, 3840x2160, 4096x2304, 5120x2880, 7680x4320에서 일치. 프레임당 시간(동기 대기): 1440p 11에서 13ms, 4K 15에서 18ms, 5K 21에서 23ms, 8K 36에서 39ms.
- 드라이버 한계(`avdprobe ctx`로 CreateContexts 결과 확인): AV1(0x1000000)은 8192x4320까지 되고 높이 4352는 오류 5. HEVC(0x800)는 4096x4096까지(8K 오류 5). H.264(0x8)는 4096x2304까지(4096x4096 오류 5). HEVC 0x400은 8192x4352도 생성됨(디코딩은 안 해 봄).
- 8192x4320: AV1 일치. VP9는 타일 열 2, 4, 8, 16개에서 일치하고 32개(libvpx 기본값)에서는 상태는 성공인데 출력이 전부 검은색. 32열은 폭 8192 이상에서만 가능. 원인(펌웨어 한계인지 이 경로의 문제인지)은 미확인. 디코더가 16열 초과를 거부하게 함.
- VideoToolbox 시스템 경로로 7680x4320 AV1과 VP9 8프레임 디코딩, md5 일치.
- 주의: `avdprobe ctx` 한계 탐색을 처음에 원격 zsh의 단어 분리 문제로 높이 1080으로 돌려 잘못된 표를 얻었고, 인자를 고쳐 다시 측정했다.

## 20:25 갱신: 10비트 AV1, VP9
- 출력 표면을 P010(`kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange`)으로 주면 된다. 컨텍스트 생성 인자는 8비트와 같다(`outFmt 'NV12'` 그대로).
- AV1: 시퀀스 구조체의 high_bitdepth(0x524)만 1로. 드라이버가 p010과 msb 모드를 스스로 채운다. VP9: 더미 AV1 매개변수의 비트 깊이를 10으로, `vp9_fill_msg`의 p010 인자를 1로, 문맥 버퍼 크기 검사는 profile 2 기준.
- 결과(1080p, SVT-AV1 10비트와 libvpx profile 2): 단독 디코더 20프레임, VideoToolbox 시스템 경로 30프레임 모두 ffmpeg `p010le` 출력과 md5 일치. 8비트도 그대로 동작.
- VideoToolbox 디코더는 비트 깊이를 av1C의 시퀀스 헤더(AV1), vpcC의 6번째 바이트 상위 4비트(VP9)에서 읽어 화소 형식을 정한다.
- 미확인: 10비트의 4K 이상, HDR 메타데이터 전달(색 공간 표시), Safari에서의 HDR 재생, 12비트(거부).

## 2026-11-11: VP9 제거

아래 VP9 하드웨어 디코딩은 시스템 경로(VTDecoderXPCService)와 단독 디코더에서는 동작했지만, 저장소에서 제거했다. Safari(WebKit GPU 프로세스)는 `com.apple.developer.videotoolbox.client-sandboxed-decoder` 권한이 있어 VideoToolbox가 VP9를 그 프로세스 안에서 디코딩하도록 하드코딩되어 있고(코덱 목록에 `vp09` 포함, AV1은 아님), 서명 없는 번들은 그 프로세스의 라이브러리 검증에 막혀 로드되지 못하고 소프트웨어(libvpx)로 떨어진다. 실제 쓰임인 Safari에서 실효가 없어 뺐다. 이 노트의 VP9 관련 서술은 당시 기록으로 그대로 남겨 둔다.

## 2026-11-11: S3 결론 (펌웨어 문제로 확정, 포기)

앞의 "18:00 갱신"에서 중단했던 S3 깨우기 실험을 이어서 끝냈다.

- 다중 칸 wakecrumb: 펌웨어가 CMOS 0x58/0x59를 덮어써서(강제 재부팅 뒤 `25 2a` 등 코드가 적지 않은 값으로 보임), 단계 값을 서로 떨어진 여섯 칸(0x4a/0x6a/0x7a/0xca/0xea 등)에 중복으로 적고 부팅 때 256바이트 전체를 덤프하도록 바꿨다.
- 결과: 잠자기 → 깨우기에서 네 칸 모두 `0x52`(커널의 `acpi_sleep_kernel` 진입)에서 멈춤. `0x53`(그 함수에서 복귀) 없음. 잠들기 직전 그래픽 드라이버의 잠자기 처리까지 디스크 로그가 남고, 그 뒤 로그는 없음. 즉 그래픽 드라이버 복구 문제가 아니라 그 전에 멈춘다.
- 깨우기 벡터 기록: `acpi_sleep_kernel` 직전에 OS의 실모드 wake vector(물리 0x2000)와 보호모드 진입점(`[0x2800]`)에 단계(`0x56`, `0x57`)를 심었다. 커널 파일에서 진입 코드 바이트를 확인하고, `acpi_sleep_kernel`이 그 코드를 다시 덮어쓰지 않는 것도 호출 목록으로 확인. 결과: `0x56`이 찍히지 않음 → 펌웨어가 OS의 깨우기 진입점까지 오지 않는다. (전원 버튼을 누르면 팬은 돌지만 OS로 제어가 넘어오지 않음.)
- BIOS 설정: 메뉴에 안 보이는 `S3/Modern Standby Support` 항목을 찾았다. 펌웨어가 설정 양식(HII)을 메모리에 내보내고 NVRAM 변수 `HiiDB`(크기+주소)로 가리키는 것을 이용해, 그 영역을 커널 확장의 물리 메모리 덤프 기능으로 떠서 IFR 파서로 분석. 항목은 `AMD_PBS_SETUP` 변수(GUID a339d746-…)의 오프셋 0x39: 0 = S3 Enable, 1 = Modern Standby(기본). macOS에서 그 바이트만 `nvram`으로 바꿔 쓸 수 있다(재부팅 뒤 유지됨).
- S3 Enable(0x39=0)로 바꾼 뒤 펌웨어가 만든 ACPI 표를 물리 메모리에서 떠서 대조: FADT flags가 `0020c5bd` → `0000c5ad`(Low Power S0 Idle 1→0), DSDT에서 `SS3 = 1`, `_S3` 숨김 없이 노출(이전 덤프 대비 19바이트 차이). 즉 펌웨어가 실제로 S3 모드로 전환됨. 그런데도 깨우기는 같은 지점에서 실패.
- 리눅스 대조: S3 Enable 상태로 Ubuntu 라이브(커널 7.0) 부팅 → `cat /sys/power/mem_sleep`에 `s2idle [deep]`, `ACPI: PM: (supports S0 S3 S4 S5)`. `rtcwake -m mem -s 45`로 재웠으나 복귀 없음(전원 버튼으로도 안 됨). OS를 바꿔도 안 되므로 펌웨어의 S3 복귀 경로가 고장난 것으로 확정.
- 정리: `AMD_PBS_SETUP`을 원래 값(Modern Standby)으로 되돌림(백업 원본과 바이트 일치 확인). 부팅 설정은 config-usbintx(디스크 로그 꺼짐), 커널 확장의 wakecrumb/깨우기 벡터 기록/물리 덤프는 `-rmbwakecrumb`가 있을 때만 동작하게 바꿔 둠(없으면 CMOS 미접근).
- 같은 Rembrandt SoC의 GPD Win Max 2에서는 S3가 되므로 SoC가 아니라 이 보드 BIOS(`M8 V1.04`)의 문제. 제조사 BIOS 업데이트가 나오면 같은 방법으로 재시험.
