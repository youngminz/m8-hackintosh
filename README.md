# GMKtec M8 해킨토시 (Ryzen 5 PRO 6650H, Radeon 660M)

GMKtec NucBox M8 미니 PC에 macOS 15.8.2(Sequoia, 24H232)를 올리고, 내장 그래픽(Rembrandt)의 가속과 동영상 하드웨어 디코딩까지 살린 작업 기록이다. OpenCore 설정, 커널 확장 패치, 역분석 도구, 그리고 macOS에 없던 **AV1 하드웨어 디코딩**을 Safari 유튜브에서 쓰게 만든 VideoToolbox 디코더가 들어 있다.

이 저장소는 완성된 EFI 배포판이 아니다. 같은 기기나 같은 칩(Rembrandt, Radeon 660M과 680M)으로 작업하는 사람이 참고할 자료다. 대부분의 패치는 macOS 15.8.2의 드라이버 바이너리에 맞춰져 있어 다른 버전에서는 그대로 동작하지 않을 수 있다.

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
| VP9, JPEG 하드웨어 디코딩 | 안 됨 (macOS의 AMD 드라이버에 VP9 경로가 없고, JPEG은 커널에 링이 없음) |
| 10비트 AV1 | 미지원 |

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
| `tools/re` | dyld 공유 캐시와 커널 컬렉션을 직접 파싱해 디스어셈블하는 스크립트 |
| `docs/` | 작업 노트와 역분석 문서. VCN 레지스터 지도와 헛다리는 `docs/vcn-h264-hevc-bringup.md` |
| `hardware/` | `lspci`, `/proc/cpuinfo` 출력 |

들어 있지 않은 것: OpenCore와 서드파티 커널 확장 바이너리, AMD 펌웨어, VBIOS, Apple 바이너리, 원본 로그, 시험 영상. 버전은 아래 "설정" 절에 적었다.

## 작업 경과

하루 동안 진행한 순서대로다.

### 1. 설치와 첫 부팅

- OpenCore 1.0.5에 AMD 커널 패치(AMD Vanilla), Lilu 1.7.2, VirtualSMC 1.3.8, AmdTscSync 2.0.0, RestrictEvents 1.1.6, LucyRTL8125Ethernet 1.2.4, AppleMCEReporterDisabler를 쓴다.
- 실기기에서 부팅이 멈추던 원인은 세 가지였다: MMIO 화이트리스트, NVMeFix(끔), MCE 리포터(차단).
- 그래픽은 RembrandtGfx(Lilu 플러그인)가 맡는다. macOS의 Navi 계열 드라이버(AMDRadeonX6000)를 Rembrandt에서 돌게 하는 커널 확장이고, 이 기기에서는 660M의 PCI 리비전을 Navi 23으로 보고하게 하는 패치를 더했다(`kext/rembrandtgfx-pci-revision.patch`).

### 2. H.264, HEVC 하드웨어 디코딩 (VCN)

`-rmbvcn` 부팅 인자로 켠다. 그냥 켜면 VCN이 GPU 전체를 멈추게 했고, 원인과 수정은 다음과 같다. 레지스터 지도, 헛다리, 디버깅 기법은 `docs/vcn-h264-hevc-bringup.md`에 있다.

1. VCN이 전원 게이트돼 있어 SMU로 켠다.
2. HWLibs가 Navi의 MMHUB 2.0 레지스터 주소 표를 쓰는데 Rembrandt는 MMHUB 2.3이라 무효화 대기가 끝나지 않았다. 표를 이름 기준으로 2.3 주소로 바꾼다.
3. HWLibs에는 VCN 3.0 펌웨어만 있어, Rembrandt용 VCN 3.1.2 펌웨어(linux-firmware)를 그 자리에 넣는다.
4. 가속기가 채우는 MM hub VM 레지스터 주소도 2.3으로 바꾼다.
5. `MMMC_VM_MX_L1_TLB_CNTL`의 `ENABLE_ADVANCED_DRIVER_MODEL`이 꺼져 있어 출력이 검은 화면이었다. 이 비트를 켠다.
6. 디코딩 완료 인터럽트가 가속기에 도착하지 않아 프레임당 16.7 또는 33.3ms가 걸렸다(초당 30프레임). 완료를 기다리는 것이 있으면 1ms마다 타임스탬프를 확인하는 스레드를 넣었다(`stampPoll`). 근본 원인은 아직 모른다.

### 3. USB 오디오 팝핑

AMD xHCI가 MSI 벡터 1개 상태에서 아이소크로너스 인터럽터의 이벤트로 인터럽트를 올리지 않아, USB 오디오가 256ms마다 끊겼다. `AppleUSBXHCIPCI::start`가 레거시 핀 인터럽트를 쓰게 하는 커널 패치로 해결했다(`configs/config.plist`의 Kernel Patch, Darwin 24.x 한정). 조사 과정은 `docs/usb-audio-popping.md`에 있다.

### 4. AV1 하드웨어 디코딩

macOS는 인텔 맥에서 AV1 하드웨어 디코딩을 제공하지 않는다. 그런데 AMD의 유저랜드 VA 드라이버(`AMDRadeonVADriver2`) 안에는 AV1 디코드 처리기(`AvdVcnAv1DecDataProcessor`)가 들어 있고, Navi 23으로 보이는 기기에서 기능으로 광고된다. 호출하는 쪽(AppleGVA, VideoToolbox)이 없을 뿐이다. 그래서 다음을 만들었다.

1. **드라이버 직접 호출**: `AVD_CreateAVDAccelerator`가 돌려주는 함수 표를 직접 부른다. 기존 HEVC 디코딩의 호출 순서를 `avdtrace`로 추적해 구조체 배치를 얻었다.
2. **AV1 입력 구조체 역분석**: 처리기가 기대하는 시퀀스 헤더, 프레임 헤더, 타일 구조체의 배치를 디스어셈블로 풀었다. 드라이버가 만드는 메시지는 Mesa의 `rvcn_dec_message_av1_t`와 오프셋이 일치한다(`docs/avd-av1-picparam-layout.md`, `docs/avd-decode-command-layout.md`).
3. **AV1 헤더 파서와 단독 디코더**: `tools/av1dec/av1hdr.c`, `av1dec.c`. 1080p, 4K 타일 8개, 필름 그레인 영상이 ffmpeg 소프트웨어 디코딩과 화소 단위로 일치했다.
4. **VideoToolbox 디코더**: `tools/av1dec/av1vt.c`. 비공개 VTVideoDecoder 인터페이스를 구현한 번들이다. 하드웨어 가속으로 표시해 등록해야 av01 세션에서 선택된다.
5. **시스템 등록**: `/Library/Video/Plug-Ins`의 번들은 Apple 서명이 있어야 로드된다. 서명 검사가 없는 곳은 봉인된 `/System/Library/Video/Plug-Ins`뿐이라, 원본 플러그인 사본과 이 번들을 담은 읽기 전용 디스크 이미지를 그 경로 위에 마운트한다(`scripts/m8-vtplugins`). 시스템 볼륨의 봉인은 건드리지 않고, 언마운트하면 원래대로 돌아간다. SIP의 파일 시스템 보호가 꺼져 있어야 한다.

결과: `VTIsHardwareDecodeSupported('av01')`가 참이 되고, Safari 18.6의 유튜브가 4K 60fps를 av01로 재생한다. 프레임당 디코딩 8에서 9ms, 360프레임 구간당 누락 0에서 1개였다.

자세한 경과와 수치는 `docs/worklog-av1-s3.md`에 시간순으로 있다.

### 5. S3 잠자기 (미해결)

펌웨어는 Modern Standby 전용으로 출하돼 `_S3`이 `XS3`로 숨겨져 있다. ACPI 패치로 되살리면 잠들기는 된다. 깨울 때 전원은 들어오지만 화면과 네트워크가 돌아오지 않는다. 펌웨어가 커널에 제어를 돌려주는지 가르기 위해, 잠자기와 깨우기 단계를 RTC CMOS에 적는 기능(`wakecrumb`)을 커널 확장에 넣어 둔 상태다. 같은 칩의 원 프로젝트 기기에서는 S3가 동작한다고 한다.

## 설정

`configs/config.plist`를 쓰려면 다음을 직접 채워야 한다.

- `PlatformInfo > Generic`의 `SystemSerialNumber`, `MLB`, `SystemUUID`, `ROM` (GenSMBIOS 등으로 생성)
- `DeviceProperties`의 `ATY,bin_image` (자기 기기에서 뽑은 VBIOS. 원 프로젝트의 스크립트 참고)
- `EFI/OC`의 OpenCore 1.0.5 바이너리와 위에 적은 커널 확장들, `acpi/`의 SSDT

주요 부팅 인자: `-rmbvcn`(동영상 디코딩), `-rmbnostamppoll`(완료 폴링 끄기), `-rmbnodisklog`(디스크 로그 끄기).

## 빌드

### RembrandtGfx

원 프로젝트를 받아 `kext/rembrandtgfx-m8.patch`를 적용하고 `kext/vcn.cpp`, `mmhubremap.hpp`, `mmhub2gc.hpp`를 `src/RembrandtGfx`에 넣는다. 펌웨어는 linux-firmware의 `yellow_carp_*.bin`에서 원 프로젝트의 스크립트로 뽑는다. Xcode의 링커로 링크했다.

```bash
make LD=ld
```

### AV1 도구와 번들

다른 맥에서 x86_64로 교차 컴파일한다.

```bash
cd tools/av1dec && make
```

기기에 올린 뒤 번들을 설치한다(root 필요).

```bash
sudo ./scripts/m8-vtplugins build tools/av1dec/AV1AMDDecoder.bundle
sudo ./scripts/m8-vtplugins install
```

확인:

```bash
./vtprobe query
```

## 주의

- 실험 기록이다. 커널 확장의 훅과 구조체 오프셋, AV1 디코더의 구조체 배치는 macOS 15.8.2의 바이너리를 역분석해 얻은 것이라, macOS를 업데이트하면 깨질 수 있다. 업데이트 후에는 플러그인 이미지도 다시 만들어야 한다(스크립트가 빌드 번호가 다르면 마운트하지 않는다).
- `/System/Library/Video/Plug-Ins` 위에 이미지를 마운트하는 방식과 서명 없는 번들 로드는 SIP 일부를 끈 상태를 전제로 한다. 보안이 약해진다는 뜻이다.
- AV1 디코더는 8비트 4:2:0만 받는다. 참조 프레임 축약 신호(`frame_refs_short_signaling`)는 구현하지 않았다.
- 확인하지 않은 것은 문서에 "미확인"이나 "추정"으로 적어 두었다.

## 참고와 라이선스

- RembrandtGfx 원 프로젝트: [winmax2-2022-6800u-hackintosh](https://github.com/caoshiyuan0211-blip/winmax2-2022-6800u-hackintosh) (GPL-2.0). `kext/`의 내용은 그 파생물이다.
- [OpenCore](https://github.com/acidanthera/OpenCorePkg), [Lilu](https://github.com/acidanthera/Lilu), [AMD Vanilla 패치](https://github.com/AMD-OSX/AMD_Vanilla)
- VCN 메시지 형식 대조: Mesa의 `ac_vcn_dec.h`, `radeon_vcn_dec.c`
- 이 저장소는 GPL-2.0으로 배포한다(`LICENSE`).
