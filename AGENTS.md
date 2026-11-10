# AGENTS.md

AI 에이전트가 이 저장소를 읽고 사용자의 PC에서 같은 작업을 수행하기 위한 지침서다. 사람이 읽을 개요는 `README.md`에 있다.

## 이 문서를 쓰는 방법

- 대상: GMKtec NucBox M8, 또는 같은 칩(AMD Rembrandt, Radeon 660M과 680M)을 쓰는 x86 PC. 다른 기기라면 절차의 순서와 진단 방법만 참고하고 값은 다시 구해야 한다.
- 기준 환경: macOS 15.8.2 (24H232, Darwin 24.6.0), OpenCore 1.0.5. 여기 적힌 오프셋, 심볼, 구조체 배치는 이 빌드의 바이너리에서 얻었다. 다른 빌드에서는 "버전에 묶인 것" 절의 항목을 먼저 다시 확인한다.
- 각 단계는 목표, 절차, 검증, 실패 시 대응으로 되어 있다. 검증을 통과하기 전에는 다음 단계로 넘어가지 않는다.
- 확인하지 않은 것은 확인하지 않았다고 사용자에게 말한다. 이 저장소의 문서도 미확인과 추정을 구분해 적었다.

## 작업 구성

| 역할 | 내용 |
|---|---|
| 작업용 컴퓨터 | 에이전트가 도는 곳. 여기서 교차 컴파일하고 SSH로 대상 PC를 다룬다. 이 기록에서는 Apple 실리콘 맥이었다 |
| 대상 PC | 유선랜으로 같은 네트워크에 연결. 유선랜 드라이버가 그래픽이나 USB와 무관해서, 화면이 죽어도 SSH는 유지된다 |
| 사용자 | 대상 PC 앞에서 전원, BIOS, 화면 확인, 비밀번호 입력을 맡는다 |

대상 PC에는 개발 도구가 없다고 가정한다. macOS에서 `python3`를 실행하면 설치 창이 뜨므로 쓰지 않는다. 도구는 작업용 컴퓨터에서 `clang -target x86_64-apple-macos13`으로 빌드해 `scp`로 보낸다. zsh에서 `log`는 내장 명령이라 `/usr/bin/log`로 부른다.

## 지켜야 할 규칙

1. **재부팅과 전원 조작은 하기 전에 사용자에게 알린다.** 사용자가 작업 중일 수 있다.
2. **화면, 소리, LED는 직접 볼 수 없다.** 사용자에게 묻거나, 로그와 수치로 대신 확인한다.
3. **비밀번호를 입력하지 않는다.** `sudo`가 필요하면 사용자가 직접 실행할 명령을 준다. 반복 작업은 특정 스크립트만 비밀번호 없이 실행되게 sudoers에 등록하는 방식을 쓴다(`scripts/m8-apply-oc`의 머리말 참고).
4. **기기 식별 값(일련번호, MLB, UUID, MAC)과 VBIOS는 저장소에 올리지 않는다.** `configs/`의 값은 자리표시 값이다.
5. **되돌릴 방법을 먼저 만든다.** ESP의 이전 설정과 이전 커널 확장을 백업하고(`m8-apply-oc`가 `config-prev.plist`, `RembrandtGfx.prev`로 남긴다), 부팅이 안 될 때 돌아갈 Ubuntu 라이브 USB를 꽂아 둔다.
6. **설정은 하나씩 바꾼다.** 부팅 인자 하나, 패치 하나를 바꾸고 재부팅해 검증한다. 여러 개를 한꺼번에 바꾸면 원인을 가를 수 없다.
7. **원격 작업 중 대상 PC가 잠들지 않게 한다.** 이 기기는 S3에서 깨어나지 못한다. `sudo pmset -a sleep 0`으로 시스템 잠자기를 끈다.
8. **GPU가 멈춘 상태에서 웜 리부트하지 않는다.** 다음 부팅이 꼬인다. WindowServer 워치독 패닉(약 2분)을 기다리면 깨끗이 재부팅된다.
9. **커널 확장을 빌드할 때마다 미정의 심볼을 확인한다.** 익명 네임스페이스 안의 심볼을 다른 파일에서 참조하면 로드되지 않고 사과 로고에서 멈춘다. `nm -u RembrandtGfx | grep "^__Z" | c++filt`의 출력이 비어 있어야 한다. Lilu로 같은 함수를 두 번 라우팅해도 패닉이 난다.
10. **실험이 끝나면 권한을 되돌린다.** 비밀번호 없이 허용한 sudoers 항목과 설치 스크립트를 지우도록 사용자에게 안내한다.

## 단계 0: 준비와 설치

`docs/install.md`에 따로 정리했다. Ubuntu 라이브 USB로 부팅해 SSH로 접속하고, 하드웨어를 덤프하고, macOS를 설치하고, OpenCore EFI를 구성해 실기기로 부팅하기까지다.

검증: 대상 PC가 macOS로 부팅되고 작업용 컴퓨터에서 SSH로 접속된다.

```bash
ssh <M8> 'sw_vers; uname -a; nvram boot-args'
```

## 단계 1: 그래픽 가속 (RembrandtGfx)

목표: Metal 가속과 외부 모니터 출력.

절차:

1. [원 프로젝트](https://github.com/caoshiyuan0211-blip/winmax2-2022-6800u-hackintosh)를 받는다. `src/RembrandtGfx`가 macOS의 Navi 드라이버(AMDRadeonX6000)를 Rembrandt에서 돌게 하는 Lilu 플러그인이다.
2. `kext/rembrandtgfx-m8.patch`를 적용하고 `kext/vcn.cpp`, `kext/mmhubremap.hpp`, `kext/mmhub2gc.hpp`를 `src/RembrandtGfx`에 넣는다.
3. 펌웨어를 linux-firmware의 `yellow_carp_*.bin`에서 원 프로젝트의 `scripts/fw-extract.py`로 뽑는다(Makefile의 `fw/toc.bin` 규칙).
4. 빌드한다. 원 프로젝트의 Makefile은 cctools 링커를 기대하지만 Xcode의 링커로도 된다.

```bash
make LD=ld
```

5. 대상 PC의 VBIOS를 뽑아 OpenCore 설정의 `DeviceProperties`에 `ATY,bin_image`로 넣는다(원 프로젝트의 `scripts/vfct-rom.py`, `augment-rom.py`). 자세한 것은 `docs/install.md`.
6. 660M은 PCI 리비전을 Navi 23으로 보고하게 해야 한다(`kext/rembrandtgfx-pci-revision.patch`, 이미 통합 패치에 포함).
7. `scripts/m8-apply-oc`로 ESP에 설치하고 재부팅한다.

검증:

```bash
ssh <M8> 'ioreg -l -w0 | grep -E "MetalPluginName|IOClass.*GraphicsAccelerator"'
```

`AMDRadeonX6000MTLDriver`와 `AMDRadeonX6000_AMDNavi23GraphicsAccelerator`가 나와야 한다.

실패 시: 화면이 안 나와도 SSH는 된다. 커널 로그를 본다. 원 프로젝트는 커널 메시지를 디스크(`/private/var/log/rmb-msgbuf.log`)에 남기는 기능이 있다. `-rmbnodisklog`가 부팅 인자에 있으면 꺼져 있는 것이다.

## 단계 2: H.264, HEVC 하드웨어 디코딩

목표: VideoToolbox가 H.264와 HEVC를 하드웨어로 디코딩한다.

절차: 부팅 인자에 `-rmbvcn`을 넣는다. 필요한 수정은 `kext/vcn.cpp`에 들어 있고, 머리말에 원인 여섯 가지가 적혀 있다. 레지스터 지도, 실패했던 접근, 디버깅 기법(레지스터 추적, 한 번만 켜는 모드, 멈춤 덤프)은 `docs/vcn-h264-hevc-bringup.md`에 있다. 다른 기기나 다른 빌드에서 VCN이 멈추면 그 문서의 기법부터 쓴다.

검증: `tools/vtprobe`를 빌드해 올린다. H.264나 HEVC로 인코딩한 짧은 mp4가 필요하다(ffmpeg로 아무 영상이나 만들면 된다).

```bash
clang -target x86_64-apple-macos13 -fobjc-arc -O1 -o vtprobe tools/vtprobe/vtprobe.m -framework Foundation -framework AVFoundation -framework VideoToolbox -framework CoreMedia -framework CoreVideo -framework CoreImage -framework ImageIO
```

```bash
ssh <M8> './vtprobe query; ./vtprobe check test-hevc.mp4 60; ./vtprobe decode test-hevc.mp4 240'
```

- `query`가 `h264 hardware decode: YES`, `hevc hardware decode: YES`를 낸다.
- `check`가 하드웨어와 소프트웨어 출력의 밝기 차이를 낸다. 0에 가까워야 한다. 하드웨어 출력의 평균 밝기가 16 근처면 검은 화면이 나오는 것이다(`ENABLE_ADVANCED_DRIVER_MODEL` 문제를 의심한다).
- `decode`가 초당 프레임 수를 낸다. 정확히 영상의 프레임률이나 30으로 나오면 완료 통지가 늦는 것이다. `stampPoll`이 설치됐는지 디스크 로그에서 `stamppoll: installed`를 찾는다. 이 기록에서는 수정 후 초당 120에서 150프레임이었다.

실패 시: VCN이 멈추면 GPU 전체가 멈출 수 있다. 이때 바로 재부팅하지 말고 워치독 패닉을 기다린다(규칙 8). `-rmbvcn`을 빼고 부팅하면 디코딩 없이 돌아간다. 부팅 자체가 안 되면 Ubuntu 라이브 USB로 들어가 ESP의 `RembrandtGfx.prev`와 `config-prev.plist`로 되돌린다.

## 단계 3: USB 오디오 팝핑

증상: USB 스피커 소리가 약 256ms마다 끊긴다. 커널 로그에 `AppleUSBAudio ... Output Fell Behind`가 반복된다.

원인: AMD xHCI가 MSI 벡터 1개 상태에서 아이소크로너스 인터럽터의 이벤트로 인터럽트를 올리지 않는다.

절차: `configs/config.plist`의 Kernel Patch 중 `M8: AppleUSBXHCIPCI::start | use legacy pin interrupt instead of MSI`를 쓴다. Darwin 24.x 전용이다. 다른 빌드에서는 `AppleUSBXHCIPCI::start`를 디스어셈블해 첫 `getInterruptType(0)` 뒤의 조건 분기를 다시 찾아야 한다(`docs/usb-audio-popping.md`).

검증:

```bash
ssh <M8> '/usr/bin/log show --last 1m --predicate "eventMessage CONTAINS \"Fell Behind\"" | wc -l; ioreg -l -w0 | grep -c IOPCIMSIMode'
```

재생 중 `Fell Behind`가 없어야 한다. 소리가 깨끗한지는 사용자에게 묻는다.

위험: 핀 인터럽트가 동작하지 않으면 USB 전체가 죽는다. SSH가 유선랜이므로 이전 설정으로 되돌릴 수 있다.

## 단계 4: AV1 하드웨어 디코딩

목표: `VTIsHardwareDecodeSupported('av01')`가 참이 되고 Safari 유튜브가 av01로 재생된다. 전제는 단계 2가 끝난 것이다.

원리: macOS의 AMD VA 드라이버(`AMDRadeonVADriver2`)에 AV1 처리기가 들어 있지만 부르는 쪽이 없다. 이 저장소의 디코더가 그 처리기를 직접 부른다. 자세한 것은 `README.md`와 `docs/worklog-av1-s3.md`.

절차:

1. 드라이버가 AV1을 광고하는지 확인한다.

```bash
clang -target x86_64-apple-macos13 -O1 -o avdprobe tools/avdprobe/avdprobe.c -framework IOKit -framework CoreFoundation
```

```bash
ssh <M8> './avdprobe'
```

`decode AV1` 줄(funcId 1, subId 0x1000000)이 있어야 한다. 없으면 드라이버가 이 기기를 AV1 지원 기종으로 보지 않는 것이다(Navi 2x로 위장하지 않았거나 드라이버가 다른 버전이다).

2. 단독 디코더로 화소 단위 검증을 한다. 시험 영상은 SVT-AV1 등으로 만든다(8비트 4:2:0, `-f obu`).

```bash
cd tools/av1dec && make
```

```bash
ssh <M8> './av1dec test-av1.obu 120 out.nv12'
```

출력을 가져와 ffmpeg의 소프트웨어 디코딩과 비교한다(`tools/av1dec/cmp.py`). 모든 프레임에서 차이가 0이어야 한다.

3. VideoToolbox 디코더 번들을 시스템에 등록한다. root가 필요하다.

```bash
ssh <M8> 'sudo ./m8-vtplugins build AV1AMDDecoder.bundle && sudo ./m8-vtplugins install'
```

검증:

```bash
ssh <M8> './vtprobe query; ./vtav1test test-av1.mp4 120 - system'
```

`av1 hardware decode: YES`가 나오고 `vtav1test`가 프레임을 디코딩한다. Safari는 완전히 종료했다 다시 열어야 새 플러그인 목록을 읽는다. 유튜브의 통계에서 코덱이 `av01`인지 사용자에게 확인을 부탁하고, 로그로도 본다.

```bash
ssh <M8> '/usr/bin/log show --last 5m --predicate "eventMessage CONTAINS \"AV1AMD\"" | tail -3'
```

알아둘 것:

- `/Library/Video/Plug-Ins`에 넣는 방식은 통하지 않는다. CoreMedia가 그 경로의 번들에 Apple 서명을 요구한다.
- 디코더를 직접 시험할 때 VideoToolbox가 디코딩을 별도 프로세스(VTDecoderXPCService)로 보내면 프로세스 내 훅이나 등록이 먹지 않는다. `defaults write com.apple.coremedia VTDecodeServer -string never`로 프로세스 내 디코딩을 강제하고, 끝나면 지운다.
- 원격에서 `pmset sleepnow` 같은 지연 명령을 걸 때는 `nohup`으로 걸고, 실제로 실행됐는지 결과로 확인한다. SSH가 닫히면 백그라운드 명령이 같이 죽을 수 있다.

## 단계 5: S3 잠자기 (미해결)

현재 상태와 실험 방법은 `docs/worklog-av1-s3.md`의 S3 부분에 있다. 이어서 할 사람을 위한 요점:

- 펌웨어가 `_S3`을 `XS3`로 숨겨 둔다. `configs/config.plist`의 ACPI 패치 두 개(`XS3 to _S3`, `SS3 Zero to One`)가 되살린다.
- 잠들기는 된다. 깨우면 전원은 들어오지만 화면과 네트워크가 돌아오지 않는다.
- `wakecrumb`가 단계 번호를 RTC CMOS 0x58에 적는다. 강제 재부팅 뒤 디스크 로그에서 읽는다. 0x52면 펌웨어가 커널에 제어를 돌려주지 않은 것이고, 0x53 이상이면 커널은 깨어났고 드라이버 복구에서 멈춘 것이다.

```bash
ssh <M8> 'sudo tail -c 2500000 /private/var/log/rmb-msgbuf.log | grep -a wakecrumb | tail -2'
```

- 이 실험은 실패할 때마다 강제 전원 차단이 필요하다. 사람 없이 반복하려면 원격 전원 제어(스마트 플러그와 BIOS의 전원 복구 시 자동 켜짐)가 있어야 한다.

## 역분석 도구

대상 PC의 바이너리를 작업용 컴퓨터로 가져와 분석한다.

- 커널 확장: `/System/Library/KernelCollections/SystemKernelExtensions.kc`. 파일셋 형식이고 심볼이 남아 있다. `tools/re/kcdis.py`.
- 유저랜드: `/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_x86_64h*` (약 5GB). `tools/re/dsc.py`가 캐시를 직접 파싱한다. 최신 macOS의 `dsc_extractor`는 이 캐시를 서명 검사로 거부했다.

```bash
DSC_DIR=dl/dsc python3 tools/re/dsc.py syms 'AMDRadeonVADriver2$' 'Av1'
```

```bash
DSC_DIR=dl/dsc python3 tools/re/dsc.py dis 'AMDRadeonVADriver2$' _AVD_CreateAVDAccelerator
```

capstone이 필요하다. 동적 추적은 `tools/avdtrace`가 예시다. 자기 프로세스 안에서 드라이버 함수의 앞부분을 덮어써 가로채고 인자를 덤프한다.

## 버전에 묶인 것

다른 macOS 빌드에서는 아래를 다시 확인한다. 하나라도 어긋나면 멈추거나 잘못된 출력이 나온다.

| 항목 | 위치 | 확인 방법 |
|---|---|---|
| USB 핀 인터럽트 커널 패치 | `configs/config.plist` | 패치의 Find 바이트가 커널 컬렉션에 정확히 한 번 나오는지 |
| `stampPoll`의 vtable 오프셋 0x3b0, 필드 0xd38 | `kext/vcn.cpp` | `AMDAccelEventMachine::waitForStamp`를 디스어셈블해 같은 접근이 있는지 |
| VCN 관련 구조체 오프셋 | `kext/vcn.cpp` | 각 주석에 적힌 함수의 디스어셈블과 대조 |
| AVD 인터페이스 구조체 | `tools/avdprobe`, `tools/av1dec` | `avdprobe`가 기능 목록을 정상 출력하는지 |
| AV1 입력 구조체 배치 | `tools/av1dec/avdfill.h` | `av1dec` 출력이 소프트웨어 디코딩과 일치하는지 |
| VTVideoDecoder 구조체 | `tools/av1dec/av1vt.c` | Apple의 `AV1DecoderSW.bundle`의 표와 대조(`docs/worklog-av1-s3.md`) |
| 플러그인 이미지 | `scripts/m8-vtplugins` | macOS 업데이트 후 `build`로 다시 만든다 |

## 문서 목록

| 파일 | 내용 |
|---|---|
| `docs/install.md` | 설치와 첫 부팅 절차 |
| `docs/vcn-h264-hevc-bringup.md` | VCN 레지스터 지도, 헛다리, 디버깅 기법, 복구와 함정 |
| `docs/worklog-av1-s3.md` | AV1 작업과 S3 실험의 시간순 기록, 수치, 막혔던 지점 |
| `docs/avd-decode-command-layout.md` | AMD VA 드라이버의 디코드 명령 구조 |
| `docs/avd-av1-picparam-layout.md` | AV1 처리기의 입력 구조체 |
| `docs/usb-audio-popping.md` | USB 오디오 팝핑 조사 |
