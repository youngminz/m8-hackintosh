# GMKtec M8 해킨토시 (Ryzen 5 PRO 6650H, Radeon 660M)

GMKtec NucBox M8 미니 PC에 macOS 15.8.2(Sequoia, 24H232)를 올리고, 내장 그래픽(Rembrandt)의 가속과 동영상 하드웨어 디코딩까지 살린 작업 기록이다. OpenCore 설정, 커널 확장 패치, 역분석 도구, 그리고 macOS에 없던 **AV1과 VP9 하드웨어 디코딩**을 VideoToolbox에 붙인 디코더가 들어 있다.

이 저장소는 완성된 EFI 배포판이 아니다. 같은 기기나 같은 칩(Rembrandt, Radeon 660M과 680M)으로 작업하는 사람이 참고할 자료다. 대부분의 패치는 macOS 15.8.2의 드라이버 바이너리에 맞춰져 있어 다른 버전에서는 그대로 동작하지 않을 수 있다.

사람이 읽어도 되고, AI 에이전트에게 이 문서를 주고 자기 PC에서 같은 작업을 시켜도 되게 썼다. 각 단계에 배경, 절차, 검증 명령, 실패 시 대응이 있다.

## 하드웨어

| 항목 | 내용 |
|---|---|
| 본체 | GMKtec NucBox M8 |
| CPU | AMD Ryzen 5 PRO 6650H |
| GPU | Radeon 660M (Rembrandt, PCI 1002:1681), VCN 3.1.2 |
| 유선랜 | Realtek RTL8125 2.5GbE 2개 |
| 무선 | MediaTek MT7922 (macOS에서 확인하지 않음) |
| USB | AMD xHCI 5개, USB4 |
| SMBIOS | MacPro7,1 |

## 현재 상태

| 기능 | 상태 |
|---|---|
| 부팅, Metal 가속, 4K 출력 | 동작 |
| H.264, HEVC 하드웨어 디코딩 (VideoToolbox) | 동작 |
| AV1 하드웨어 디코딩 (8비트, VideoToolbox와 Safari) | 동작. Safari 유튜브 4K 60fps가 av01로 재생됨 |
| USB 오디오 | 동작 (커널 패치 필요, 아래 참고) |
| 유선랜 | 동작 (LucyRTL8125Ethernet) |
| S3 잠자기 | 잠들기는 되고 깨어나지 못함. 조사 중 |
| VP9 하드웨어 디코딩 (8비트, VideoToolbox) | 동작. 시험 영상이 소프트웨어 디코딩과 화소 단위로 일치. Safari 유튜브에서는 아직 확인하지 않음 |
| JPEG 하드웨어 디코딩 | 안 됨 (커널 가속기에 JPEG 링이 없음) |
| 10비트 AV1, VP9 | 미지원 |

## 저장소 구성

| 경로 | 내용 |
|---|---|
| `configs/config.plist` | 현재 쓰는 OpenCore 1.0.5 설정. 일련번호와 VBIOS는 빼 두었다 |
| `configs/config-wakedebug.plist` | S3 디버깅용 (디스크 로그와 자세한 로그를 켠 것) |
| `acpi/` | SSDT-PLUG, SSDT-EC-USBX |
| `kext/` | RembrandtGfx 커널 확장에 대한 패치. [원 프로젝트](https://github.com/caoshiyuan0211-blip/winmax2-2022-6800u-hackintosh)의 `src/RembrandtGfx`에 적용한다 |
| `scripts/` | ESP에 설정과 커널 확장을 설치하는 스크립트, AV1 플러그인 이미지 관리 스크립트 |
| `tools/vtprobe` | VideoToolbox 하드웨어 디코딩 여부와 출력 프레임 검증 |
| `tools/avdprobe`, `tools/avdtrace` | AMD VA 드라이버 직접 호출과 호출 추적 |
| `tools/av1dec` | AV1 헤더 파서, 단독 디코더, VideoToolbox 디코더 번들 |
| `tools/vp9dec` | VP9 헤더 파서, VCN용 VP9 메시지(Mesa에서 옮김), 단독 디코더, VideoToolbox 디코더 |
| `tools/re` | dyld 공유 캐시와 커널 컬렉션을 직접 파싱해 디스어셈블하는 스크립트 |
| `docs/` | 작업 노트와 역분석 문서. VCN 레지스터 지도와 헛다리는 `docs/vcn-h264-hevc-bringup.md` |
| `hardware/` | `lspci`, `/proc/cpuinfo` 출력 |

들어 있지 않은 것: OpenCore와 서드파티 커널 확장 바이너리, AMD 펌웨어, VBIOS, Apple 바이너리, 원본 로그, 시험 영상. 버전은 아래 "설정" 절에 적었다.

## 전제와 작업 구성

- 대상: GMKtec NucBox M8, 또는 같은 칩(AMD Rembrandt, Radeon 660M과 680M)을 쓰는 x86 PC. 다른 기기라면 절차의 순서와 진단 방법만 참고하고 값은 다시 구해야 한다.
- 기준 환경: macOS 15.8.2 (24H232, Darwin 24.6.0), OpenCore 1.0.5. 여기 적힌 오프셋, 심볼, 구조체 배치는 이 빌드의 바이너리에서 얻었다. 다른 빌드에서는 "버전에 묶인 것" 절의 항목을 먼저 다시 확인한다.
- 각 단계는 목표, 절차, 검증, 실패 시 대응으로 되어 있다. 앞 단계의 검증을 통과한 상태가 다음 단계의 출발점이다.
- 이 저장소의 문서는 확인한 것과 미확인, 추정을 구분해 적었다.

| 역할 | 내용 |
|---|---|
| 작업용 컴퓨터 | 에이전트가 도는 곳. 여기서 교차 컴파일하고 SSH로 대상 PC를 다룬다. 이 기록에서는 Apple 실리콘 맥이었다 |
| 대상 PC | 유선랜으로 같은 네트워크에 연결. 유선랜 드라이버가 그래픽이나 USB와 무관해서, 화면이 죽어도 SSH는 유지된다 |
| 사용자 | 대상 PC 앞에서 전원, BIOS, 화면 확인, 비밀번호 입력을 맡는다 |

대상 PC에는 개발 도구가 없다. macOS에서 `python3`를 실행하면 개발 도구 설치 창이 뜬다. 그래서 도구는 작업용 컴퓨터에서 `clang -target x86_64-apple-macos13`으로 빌드해 `scp`로 보낸다. zsh에서 `log`는 내장 명령이라 `/usr/bin/log`로 부른다.

## 작업하면서 알게 된 것

- 화면, 소리, LED는 원격에서 볼 수 없다. 사용자에게 묻거나 로그와 수치로 대신 확인했다.
- ESP에는 이전 설정과 이전 커널 확장을 백업해 두었다(`m8-apply-oc`가 `config-prev.plist`, `RembrandtGfx.prev`로 남긴다). 부팅이 안 될 때는 Ubuntu 라이브 USB로 들어가 이 백업으로 되돌렸다.
- 설정은 하나씩 바꾸는 편이 빨랐다. 부팅 인자 하나, 패치 하나를 바꾸고 재부팅해 검증하면 원인을 가를 수 있다.
- 이 기기는 S3에서 깨어나지 못한다. 원격 작업 중 잠들면 사람이 가서 재부팅해야 한다. `sudo pmset -a sleep 0`으로 시스템 잠자기를 꺼 두면 된다.
- GPU가 멈춘 상태에서 웜 리부트하면 다음 부팅이 꼬인다. WindowServer 워치독 패닉(약 2분)을 기다리면 깨끗이 재부팅된다.
- 익명 네임스페이스 안의 심볼을 다른 파일에서 참조하면 커널 확장이 로드되지 않고 사과 로고에서 멈춘다. 빌드 후 `nm -u RembrandtGfx | grep "^__Z" | c++filt`의 출력이 비어 있으면 정상이다. Lilu로 같은 함수를 두 번 라우팅해도 패닉이 난다.
- `sudo`는 설치 스크립트 하나만 비밀번호 없이 실행되게 sudoers에 등록해 썼다(`scripts/m8-apply-oc`의 머리말 참고).
- `configs/`의 기기 식별 값(일련번호, MLB, UUID, MAC)과 VBIOS는 자리표시 값이다. 자기 기기의 값으로 채운다.

## 단계 0: 준비와 설치

- 설치는 Ventoy USB로 부팅한 Ubuntu 라이브에서 SSH로 진행했다. 실기기에서는 macOS 복구 이미지가 부팅되지 않아, Ubuntu에서 QEMU 가상 머신에 실기기 디스크 전체를 그대로 물려 설치한 뒤 같은 디스크를 실기기에서 부팅했다. 실제 명령과 스크립트는 `docs/install.md`에 있다.
- OpenCore 1.0.5에 AMD 커널 패치(AMD Vanilla), Lilu 1.7.2, VirtualSMC 1.3.8, AmdTscSync 2.0.0, RestrictEvents 1.1.6, LucyRTL8125Ethernet 1.2.4, AppleMCEReporterDisabler를 쓴다.

`docs/install.md`에 실제로 실행한 명령과 함께 정리했다. 흐름은 이렇다.

1. Ventoy USB의 Ubuntu 라이브로 대상 PC를 부팅하고 SSH를 연다(`scripts/ventoy/`의 자동 시작 스크립트). 이후 모든 작업을 SSH로 한다.
2. 하드웨어를 덤프한다(ACPI 표, `dmidecode`, `lspci`, `dmesg`). SSDT와 VBIOS, 설정 값의 근거가 된다.
3. 대상 디스크를 ESP와 APFS용 파티션으로 나누고, ESP에 OpenCore와 macOS 복구 이미지(`macrecovery.py`)를 넣는다.
4. 실기기에서 복구 이미지가 부팅되지 않았다. 그래서 Ubuntu에서 QEMU 가상 머신을 띄우고 **실기기 디스크 전체를 그대로 물려** 설치했다. 화면과 입력은 QMP 소켓으로 원격 조작한다.
5. 같은 디스크를 실기기에서 OpenCore로 부팅한다. 실패하면 가상 머신으로 다시 부팅해 macOS 안의 로그를 읽는다.
6. 실기기 첫 부팅 문제 네 가지를 순서대로 고친다: MMIO 화이트리스트, NVMeFix 끔, MCE 리포터 차단(`kext/AppleMCEReporterDisabler.kext`), PCI 리비전을 Navi 23 값으로 보고.

사용자가 직접 해야 하는 것: USB 부팅, 가상 머신 안에서의 계정 생성과 원격 로그인 켜기.

검증: 대상 PC가 macOS로 부팅되고 작업용 컴퓨터에서 SSH로 접속된다.

```bash
ssh <M8> 'sw_vers; uname -a; nvram boot-args'
```

## 단계 1: 그래픽 가속 (RembrandtGfx)

그래픽은 RembrandtGfx(Lilu 플러그인)가 맡는다. macOS의 Navi 계열 드라이버(AMDRadeonX6000)를 Rembrandt에서 돌게 하는 커널 확장이고, 이 기기에서는 660M의 PCI 리비전을 Navi 23으로 보고하게 하는 패치를 더했다(`kext/rembrandtgfx-pci-revision.patch`).

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

`-rmbvcn` 부팅 인자로 켠다. 그냥 켜면 VCN이 GPU 전체를 멈추게 했고, 원인과 수정은 다음과 같다. 레지스터 지도, 헛다리, 디버깅 기법은 `docs/vcn-h264-hevc-bringup.md`에 있다.

1. VCN이 전원 게이트돼 있어 SMU로 켠다.
2. HWLibs가 Navi의 MMHUB 2.0 레지스터 주소 표를 쓰는데 Rembrandt는 MMHUB 2.3이라 무효화 대기가 끝나지 않았다. 표를 이름 기준으로 2.3 주소로 바꾼다.
3. HWLibs에는 VCN 3.0 펌웨어만 있어, Rembrandt용 VCN 3.1.2 펌웨어(linux-firmware)를 그 자리에 넣는다.
4. 가속기가 채우는 MM hub VM 레지스터 주소도 2.3으로 바꾼다.
5. `MMMC_VM_MX_L1_TLB_CNTL`의 `ENABLE_ADVANCED_DRIVER_MODEL`이 꺼져 있어 출력이 검은 화면이었다. 이 비트를 켠다.
6. 디코딩 완료 인터럽트가 가속기에 도착하지 않아 프레임당 16.7 또는 33.3ms가 걸렸다(초당 30프레임). 완료를 기다리는 것이 있으면 1ms마다 타임스탬프를 확인하는 스레드를 넣었다(`stampPoll`). 근본 원인은 아직 모른다.

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

실패 시: VCN이 멈추면 GPU 전체가 멈출 수 있다. 이때는 워치독 패닉(약 2분)을 기다리면 깨끗이 재부팅된다. `-rmbvcn`을 빼고 부팅하면 디코딩 없이 돌아간다. 부팅 자체가 안 되면 Ubuntu 라이브 USB로 들어가 ESP의 `RembrandtGfx.prev`와 `config-prev.plist`로 되돌린다.

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

조사 과정은 `docs/usb-audio-popping.md`에 있다.

## 단계 4: AV1 하드웨어 디코딩

macOS는 인텔 맥에서 AV1 하드웨어 디코딩을 제공하지 않는다. 그런데 AMD의 유저랜드 VA 드라이버(`AMDRadeonVADriver2`) 안에는 AV1 디코드 처리기(`AvdVcnAv1DecDataProcessor`)가 들어 있고, Navi 23으로 보이는 기기에서 기능으로 광고된다. 호출하는 쪽(AppleGVA, VideoToolbox)이 없을 뿐이다. 그래서 다음을 만들었다.

1. **드라이버 직접 호출**: `AVD_CreateAVDAccelerator`가 돌려주는 함수 표를 직접 부른다. 기존 HEVC 디코딩의 호출 순서를 `avdtrace`로 추적해 구조체 배치를 얻었다.
2. **AV1 입력 구조체 역분석**: 처리기가 기대하는 시퀀스 헤더, 프레임 헤더, 타일 구조체의 배치를 디스어셈블로 풀었다. 드라이버가 만드는 메시지는 Mesa의 `rvcn_dec_message_av1_t`와 오프셋이 일치한다(`docs/avd-av1-picparam-layout.md`, `docs/avd-decode-command-layout.md`).
3. **AV1 헤더 파서와 단독 디코더**: `tools/av1dec/av1hdr.c`, `av1dec.c`. 1080p, 4K 타일 8개, 필름 그레인 영상이 ffmpeg 소프트웨어 디코딩과 화소 단위로 일치했다.
4. **VideoToolbox 디코더**: `tools/av1dec/av1vt.c`. 비공개 VTVideoDecoder 인터페이스를 구현한 번들이다. 하드웨어 가속으로 표시해 등록해야 av01 세션에서 선택된다.
5. **시스템 등록**: `/Library/Video/Plug-Ins`의 번들은 Apple 서명이 있어야 로드된다. 서명 검사가 없는 곳은 봉인된 `/System/Library/Video/Plug-Ins`뿐이라, 원본 플러그인 사본과 이 번들을 담은 읽기 전용 디스크 이미지를 그 경로 위에 마운트한다(`scripts/m8-vtplugins`). 시스템 볼륨의 봉인은 건드리지 않고, 언마운트하면 원래대로 돌아간다. SIP의 파일 시스템 보호가 꺼져 있어야 한다.

결과: `VTIsHardwareDecodeSupported('av01')`가 참이 되고, Safari 18.6의 유튜브가 4K 60fps를 av01로 재생한다. 프레임당 디코딩 8에서 9ms, 360프레임 구간당 누락 0에서 1개였다.

자세한 경과와 수치는 `docs/worklog-av1-s3.md`에 시간순으로 있다.

### 절차와 검증

목표: `VTIsHardwareDecodeSupported('av01')`가 참이 되고 Safari 유튜브가 av01로 재생된다. 전제는 단계 2가 끝난 것이다.

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

## 단계 4-2: VP9 하드웨어 디코딩

AMD VA 드라이버에는 VP9 처리기가 없다. 칩과 펌웨어는 VP9를 지원하므로, 드라이버의 AV1 경로를 빌려 VP9 메시지를 보낸다. 전제는 단계 4가 끝난 것이다.

원리:

1. 드라이버에게 AV1 디코더를 만들게 한다. 버퍼 할당, 명령 제출, 완료 처리는 드라이버가 그대로 한다.
2. 자기 프로세스 안에서 `AvdVcnAv1DecDataProcessor`의 가상 함수 표에서 세 개를 바꾼다.
   - `ProcessCreateDecodeSession`: 세션 생성 메시지의 스트림 종류를 AV1(0x13)에서 VP9(0x11)로 바꾼다.
   - `initHWContext`: 문맥 버퍼에 VP9의 기본 확률 표를 쓴다.
   - `SetDecodeMessageFields`: 드라이버가 만든 디코드 메시지에서 AV1 메시지 항목을 VP9 메시지로 바꾸고, 확률 버퍼에 VP9 표를 쓴다.
3. VP9 메시지 형식과 기본 확률 표는 Mesa(`ac_vcn_dec.h`, `radeon_vcn_dec.c`)에서 옮겼다(`tools/vp9dec/vcn_vp9.h`, `vp9_probs.h`, `vp9msg.h`).
4. 드라이버 내부의 오프셋과 함수 주소는 macOS 15.8.2(24H232)의 것이다. `vp9dec.c` 머리말에 있고, 실행 시 가상 함수 표의 값이 기대와 같은지 확인한 뒤에만 바꾼다.

절차와 검증:

```bash
cd tools/vp9dec && clang -target x86_64-apple-macos13 -O1 -Wno-four-char-constants -Wno-unused-function -o vp9dec vp9dec.c vp9hdr.c ../av1dec/av1hdr.c -framework IOKit -framework CoreFoundation -framework CoreVideo -framework IOSurface
```

```bash
ssh <M8> './vp9dec test-vp9.ivf 120 out.nv12'
```

시험 영상은 libvpx로 만든 8비트 4:2:0 ivf다. 출력을 ffmpeg의 소프트웨어 디코딩과 비교한다(`tools/av1dec/cmp.py`). 이 기록에서는 1080p 120프레임, 타일 4열 1080p, 4K 모두 차이가 0이었고 프레임당 3ms(1080p), 8에서 9ms(4K)였다.

VideoToolbox 쪽은 `tools/av1dec`의 번들에 같이 들어간다(`make`가 `vp9vt.c`를 함께 빌드한다). 설치는 단계 4와 같다. VP9는 보조 디코더라, 클라이언트가 먼저 요청해야 목록에 들어간다.

```bash
ssh <M8> 'CODEC=vp09 SUPP=1 ./vtav1test test-vp9.mp4 120 - system'
```

`VTIsHardwareDecodeSupported(vp09) = 1`이 나오고 프레임이 디코딩된다.

알아둘 것:

- 가상 함수 표를 바꾸면 그 프로세스의 모든 AV1 디코더에 걸린다. 그래서 훅은 VP9 세션이 드라이버를 부르는 동안의 스레드에서만 동작하고, 나머지는 원래 함수로 넘긴다.
- Mesa는 이 세대의 VCN에서 동적 DPB를 쓰지만, 드라이버가 AV1용으로 잡는 고정 DPB로도 동작했다.
- 세그멘테이션이나 숨은 참조 프레임이 든 스트림, 스트림 중간의 해상도 변경은 시험하지 않았다.

## 단계 5: S3 잠자기 (미해결)

펌웨어는 Modern Standby 전용으로 출하돼 `_S3`이 `XS3`로 숨겨져 있다. `configs/config.plist`의 ACPI 패치 두 개(`XS3 to _S3`, `SS3 Zero to One`)로 되살리면 잠들기는 된다. 깨울 때 전원은 들어오지만 화면과 네트워크가 돌아오지 않는다. 펌웨어가 커널에 제어를 돌려주는지 가르기 위해, 잠자기와 깨우기 단계를 RTC CMOS에 적는 기능(`wakecrumb`)을 커널 확장에 넣어 둔 상태다. 같은 칩의 원 프로젝트 기기에서는 S3가 동작한다고 한다.

실험 기록은 `docs/worklog-av1-s3.md`의 S3 부분에 있다. 이어서 할 사람을 위한 요점:

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
| VP9 가로채기의 함수 주소와 오프셋 | `tools/vp9dec/vp9dec.c`, `vp9vt.c` | 실행 시 표가 맞지 않으면 스스로 멈춘다. `tools/re/dsc.py`로 새 주소를 구한다 |
| VTVideoDecoder 구조체 | `tools/av1dec/av1vt.c` | Apple의 `AV1DecoderSW.bundle`의 표와 대조(`docs/worklog-av1-s3.md`) |
| 플러그인 이미지 | `scripts/m8-vtplugins` | macOS 업데이트 후 `build`로 다시 만든다 |

## 설정

`configs/config.plist`를 쓰려면 다음을 직접 채워야 한다.

- `PlatformInfo > Generic`의 `SystemSerialNumber`, `MLB`, `SystemUUID`, `ROM` (GenSMBIOS 등으로 생성)
- `DeviceProperties`의 `ATY,bin_image` (자기 기기에서 뽑은 VBIOS. 원 프로젝트의 스크립트 참고)
- `EFI/OC`의 OpenCore 1.0.5 바이너리와 위에 적은 커널 확장들, `acpi/`의 SSDT

주요 부팅 인자: `-rmbvcn`(동영상 디코딩), `-rmbnostamppoll`(완료 폴링 끄기), `-rmbnodisklog`(디스크 로그 끄기).

## 알아둘 것

- 실험 기록이다. 커널 확장의 훅과 구조체 오프셋, AV1 디코더의 구조체 배치는 macOS 15.8.2의 바이너리를 역분석해 얻은 것이라, macOS를 업데이트하면 깨질 수 있다. 업데이트 후에는 플러그인 이미지도 다시 만들어야 한다(스크립트가 빌드 번호가 다르면 마운트하지 않는다).
- `/System/Library/Video/Plug-Ins` 위에 이미지를 마운트하는 방식과 서명 없는 번들 로드는 SIP 일부를 끈 상태를 전제로 한다. 보안이 약해진다는 뜻이다.
- AV1과 VP9 디코더는 8비트 4:2:0만 받는다. 참조 프레임 축약 신호(`frame_refs_short_signaling`)는 구현하지 않았다.
- 확인하지 않은 것은 문서에 "미확인"이나 "추정"으로 적어 두었다.

## 문서 목록

| 파일 | 내용 |
|---|---|
| `docs/install.md` | 설치와 첫 부팅 절차(실제 명령, 가상 머신 실행 스크립트, 문제와 해결, 복구 절차) |
| `docs/vcn-h264-hevc-bringup.md` | VCN 레지스터 지도, 헛다리, 디버깅 기법, 복구와 함정 |
| `docs/worklog-av1-s3.md` | AV1 작업과 S3 실험의 시간순 기록, 수치, 막혔던 지점 |
| `docs/avd-decode-command-layout.md` | AMD VA 드라이버의 디코드 명령 구조 |
| `docs/avd-av1-picparam-layout.md` | AV1 처리기의 입력 구조체 |
| `docs/usb-audio-popping.md` | USB 오디오 팝핑 조사 |

## 참고와 라이선스

- RembrandtGfx 원 프로젝트: [winmax2-2022-6800u-hackintosh](https://github.com/caoshiyuan0211-blip/winmax2-2022-6800u-hackintosh) (GPL-2.0). `kext/`의 내용은 그 파생물이다.
- [OpenCore](https://github.com/acidanthera/OpenCorePkg), [Lilu](https://github.com/acidanthera/Lilu), [AMD Vanilla 패치](https://github.com/AMD-OSX/AMD_Vanilla)
- VCN 메시지 형식: Mesa의 `ac_vcn_dec.h`, `radeon_vcn_dec.c` (MIT). `tools/vp9dec`의 구조체와 확률 표는 거기서 옮긴 것이고 저작권 고지를 유지했다
- 라이선스는 폴더에 따라 다르다.
  - `kext/`: GPL-2.0 (`kext/LICENSE`). GPL-2.0인 원 프로젝트의 파생물이다.
  - 그 밖의 전부(`tools/`, `scripts/`, `acpi/`, `configs/`, `docs/` 등): MIT (`LICENSE`).
  - `configs/config.plist`에 든 커널 패치 목록은 AMD Vanilla 등 다른 프로젝트에서 온 것이고, 그 부분의 권리는 각 원저자에게 있다.
