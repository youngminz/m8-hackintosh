# macOS Tahoe 26.7.3 올리기 (2026-11-11)

Sequoia 15.8.2가 돌아가는 M8에 Tahoe 26.7.3(25G342)을 **같은 APFS 컨테이너의 별도 볼륨**으로 설치해, 그래픽 가속이 붙은 상태로 쓸 수 있게 만든 기록이다. Sequoia는 그대로 두고 OpenCore 선택 화면에서 둘 중 하나를 고른다. ESP 설정은 하나(`configs/config.plist`)로 두 OS를 다 부팅한다. 두 버전의 기능 비교와 단계별 절차는 README에 있고, 이 문서는 Tahoe에서 겪은 문제와 패치의 세부를 적은 기록이다.

현재 상태:

| 항목 | Tahoe 26.7.3 |
|---|---|
| 부팅, Metal 가속 | 동작 (로그인 후 GPU 오류 없음) |
| 유선랜 | 동작 |
| USB | 동작 (Tahoe용 핀 인터럽트 패치) |
| H.264, HEVC, AV1 하드웨어 디코딩 | 동작 (Sequoia와 같은 `-rmbvcn`). AV1은 플러그인 이미지를 Tahoe에서 다시 만들어 마운트 |
| VP9 하드웨어 디코딩 | Chrome에서 동작. 드라이버 함수 주소를 25G342용으로 추가(`tools/vp9dec/vp9addr.h`) |
| H.264, HEVC 하드웨어 인코딩 | 동작 |
| USB 오디오 끊김 | 미확인 |
| 장시간 안정성 | 10분 부하 시험 통과 (AV1 하드웨어 디코딩 + Metal 계산 + 1GB 양방향 전송 동시) |

## 1. 설치

1. 같은 컨테이너에 볼륨을 만든다: `sudo diskutil apfs addVolume disk1 APFS Tahoe`.
2. `softwareupdate --fetch-full-installer --full-installer-version 26.7.3`으로 설치 앱을 받는다.
3. 설치 앱을 **창으로** 열고 대상 디스크로 `Tahoe`를 고른다. `startosinstall`에는 대상 볼륨을 고르는 옵션이 없다(지금 부팅한 볼륨을 업그레이드한다).
4. 설치 기간에는 그래픽 kext 없이 부팅했다: `RembrandtGfx` 끔, GPU `DeviceProperties` 비움, 부팅 인자 `-v keepsyms=1 debug=0x100 ipc_control_port_options=0 -lilubetaall -amd_no_dgpu_accel`, `PlatformInfo > Generic > MaxBIOSVersion = true`. 지금 설정(그래픽 kext 포함)으로는 복구 모드가 뜨지 않는다(6절). 설치 환경도 같은 시스템 이미지라 마찬가지로 본다.

## 2. 설치 단계에서 막힌 것

### 2.1 `Still waiting for root device`

설치 커널이 디스크를 찾지 못했다. 화면에는 NVMe가 레지스터를 전부 1로 읽은 흔적(`CAP != kNVMeInvalidRegisterValue64`)과 유선랜 드라이버의 `Unknown chipset`이 나왔다.

원인: AMD 칩셋에서 10비트 PCIe 태그를 끄는 IOPCIFamily 패치(`probeBusGated | Disable 10 bit tags`)가 Tahoe에서 맞지 않았다. Sequoia는 `bt r14d, 0x11; jb`(장치가 10비트 태그를 지원하면 켜는 쪽으로 점프)인데, Tahoe는 같은 코드가 `bt ebx, 0x11; jae +0x4a`(지원하지 않으면 건너뛰기)로 분기가 뒤집혀 있다. 원래 패치의 Find(`e0117200`, 마스크 `f0fffff0`)는 `72`(jb)를 찾으므로 Tahoe에서는 적용되지 않고 넘어간다.

Tahoe용 패치(Base `__ZN11IOPCIBridge13probeBusGatedEP14probeBusParams`, MinKernel 25.0.0):

| | 바이트 |
|---|---|
| Find | `0f ba e0 11 73 00` |
| Mask | `ff ff f0 ff ff 00` |
| Replace | `00 00 00 00 eb 00` |
| ReplaceMask | `00 00 00 00 ff 00` |

`jae`를 무조건 점프로 바꿔 10비트 태그를 켜는 코드를 항상 건너뛴다.

### 2.2 시간 역행 패치가 엉뚱한 곳을 고침

`Visual | thread_invoke, thread_dispatch | Remove non-monotonic time panic`은 스레드 구조체의 필드 `0x488`을 쓰는 비교를 찾는다. Tahoe에서는 이 필드가 `0x490`으로 옮겨져, 첫 두 일치가 `_kperf_lazy_wait_sample`과 `_vmx_hv_support`(rdmsr 0x48b 뒤)에 걸렸다. 두 번째는 점프 명령을 반쯤 덮어써 코드를 깨뜨린다.

Tahoe용: Find `48 2b 00 90 04 00 00 0f 00 00 00 00 00`, Mask `48 ef 00 f0 ff ff ff ff 00 00 00 00 00`(두 번째 바이트를 `2b`/`3b`로 좁혀 다른 함수를 제외), Replace는 원래 패치와 같다. Tahoe 커널에서 `_thread_block_reason`과 `_thread_dispatch`의 두 곳에만 처음 두 번 일치한다.

원래 패치 두 개는 MaxKernel을 24.99.99로 내렸다.

### 2.3 MSU 1013

디스크를 찾은 뒤 설치 단계가 `ota-failure-reason = "MSU 1013 (Unable to load context options into context.)"`(`patchd_macos_load_update_plist`)로 포기하고 원래 OS로 돌아갔다. 같은 볼륨에 설치 앱을 다시 돌리자 통과했다. 첫 시도가 디스크를 못 찾고 강제 종료된 흔적으로 추정한다(미확인).

## 3. 첫 설정 화면 건너뛰기

그래픽 가속 없이 뜬 설정 화면은 화면 하나 그리는 데 수 초씩 걸려 쓸 수 없었다. Sequoia로 부팅해 Tahoe의 Data 볼륨(`/Volumes/Tahoe - Data`)을 직접 설정했다.

```bash
T="/Volumes/Tahoe - Data"; N="$T/private/var/db/dslocal/nodes/Default"
sudo cp -p /private/var/db/dslocal/nodes/Default/users/<USER>.plist "$N/users/"   # 같은 UID, 같은 비밀번호
sudo dscl -f "$N" localonly -append /Local/Target/Groups/admin GroupMembership <USER>
sudo dscl -f "$N" localonly -append /Local/Target/Groups/admin GroupMembers <GeneratedUID>
sudo ditto "/Volumes/Tahoe/System/Library/User Template/Non_localized" "$T/Users/<USER>"
# ~/.ssh/authorized_keys 복사, chown -R <UID>:20
sudo cp -p /etc/sudoers.d/* "$T/private/etc/sudoers.d/"
sudo /usr/libexec/PlistBuddy -c "Add :com.openssh.sshd bool false" "$T/private/var/db/com.apple.xpc.launchd/disabled.plist"
sudo cp -p /etc/ssh/ssh_host_* "$T/private/etc/ssh/"   # 호스트 키를 같게 해 known_hosts 충돌을 피한다
sudo touch "$T/private/var/db/.AppleSetupDone"
```

시간대가 기본값으로 남으므로 Tahoe에서 `sudo systemsetup -settimezone Asia/Seoul`로 맞춘다.

## 4. 그래픽 가속

RembrandtGfx가 찾는 드라이버와 커널 심볼 194개가 모두 Tahoe에 같은 이름으로 있었다. `-lilubetaall`로 그대로 올리자 Metal 가속이 붙었다. 다만 처음에는 쓸 수 없을 만큼 느렸고 몇 분 뒤 멈췄다. 원인이 두 개였다.

### 4.1 USB 인터럽트

Tahoe 안에서 네트워크 왕복이 2에서 8초 걸렸다. 이 기기는 유선 연결과 키보드, 마우스가 모두 USB다. Sequoia용 xHCI 핀 인터럽트 패치(단계 3)를 Tahoe로 옮기자 4에서 5ms로 돌아왔다. `AppleUSBXHCIPCI::start`의 구조는 같고 점프 거리만 다르다.

| | 바이트 |
|---|---|
| Find | `31 db 31 f6 ff 90 08 07 00 00 85 c0 0f 85 68 02 00 00` |
| Replace | `31 db 31 f6 ff 90 08 07 00 00 85 c0 90 e9 68 02 00 00` |

Tahoe 부트 커널 컬렉션에서 정확히 한 번 일치한다.

### 4.2 동영상 엔진 시간 초과로 인한 GPU 재시작

로그인 2분 뒤 WindowServer 워치독 패닉(`userspace watchdog timeout: no successful checkins from WindowServer`)이 났다. 스택 기록에서 WindowServer는 `AMDAccelEventMachine` 스레드를 기다렸고, 그 스레드는 `IOAccelEventMachine2::hardwareErrorEvent` → `restart_channel` → `AMDAccelChannel::resetHardwareAndReplay` → `waitForTimestamp` → `pollWait`에서 멈춰 있었다. 커널 로그에는 2초마다 이것이 찍혔다.

```
Timestamp 0x2 on channel 15 VCN0Dec is not expired; stamp: 0x0 -> 0x0; ... wait time 2102ms
IOAccelEventMachine2::waitForStamp(...): a graphics error occurred, fRestartType = <2>
```

Tahoe에서는 VCN 훅(`kext/vcn.cpp`, macOS 15 오프셋)을 끄게 해 두었는데, 하드웨어 코덱 광고는 남아 있었다. 움직이는 배경화면 같은 앱이 응답하지 않는 VCN에 작업을 보내고, 2초마다 시간 초과와 GPU 재시작이 반복됐다. 화면이 몇 초씩 걸려 그려지던 것도 이것이다.

수정: VCN이 꺼져 있으면(`vcnEnabled()`가 거짓) 코덱 광고를 숨기는 `noVideoEngine`을 돌린다. 전에는 부팅 인자 `-rmbvcn`만 보고 판단했다. 그 뒤 로그인 후에도 GPU 오류 0건, 정상 속도.

### 4.3 부하 중 네트워크 패닉 (`fq_codel`)

하드웨어 디코딩과 scp 전송을 함께 돌리다 기기가 멈췄다(자동 재부팅도 되지 않음). 다음 부팅에 남은 패닉 보고서:

```
panic: assertion failed: now >= *pkt_enq_time, file: .../bsd/net/classq/classq_fq_codel.c, line: 1127
  fq_codel_dq <- ifclassq_dequeue <- IONetworkInterface::dequeueOutputPacketsWithMaxSize <- AppleUSBNCMPacketQueue::fill
```

패킷을 큐에 넣은 시각보다 꺼낸 시각이 이르다는 검사다. 이 CPU는 코어마다 TSC가 조금씩 어긋나(AmdTscSync를 써도), 넣은 코어와 꺼낸 코어가 다르면 시간이 거꾸로 보일 수 있다. Sequoia 커널에는 이 검사가 없고 Tahoe에서 새로 생겼다(두 커널의 시간 순서 assert 문자열을 비교하면 이것 하나만 늘었다).

Tahoe용 커널 패치(Identifier `kernel`, MinKernel 25.0.0, Count 1): `now - pkt_enq_time`이 음수일 때 assert로 가던 `jb`를 "대기 시간이 목표 이하" 경로로 돌린다.

| | 바이트 |
|---|---|
| Find | `4d 89 c1 49 29 c9 72 74 48 8b 46 20` |
| Replace | `4d 89 c1 49 29 c9 72 52 48 8b 46 20` |

### 4.4 패닉 뒤 재부팅되지 않고 멈춤

4.3의 패닉은 패닉 보고서를 저장한 뒤 재부팅하지 않고 멈췄다. 전원 버튼으로 끌 때까지 그대로였다.

- 이 기기의 ACPI FADT 재시작 레지스터는 I/O 포트 `0xB2`에 `0xBE`를 쓰는 SMI다. 재시작을 펌웨어(SMM) 코드가 처리한다.
- 평소 재시작에서는 RembrandtGfx가 그 전에 GPU 엔진을 멈추고 PSP와 SMU를 정리한다(`psp-exit`). 패닉 재시작은 이것을 건너뛰고 바로 펌웨어 재시작으로 간다.
- 부팅 초반의 패닉은 잘 재부팅됐고, GPU가 돌고 있을 때의 패닉만 멈췄다. 그래서 GPU와 PSP가 살아 있는 상태의 펌웨어 재시작에서 멈춘다고 추정한다(원래 방식으로 같은 조건을 재현하는 대조 시험은 하지 않았다).

수정(`kext/panicreset.cpp`): 커널의 `PEHaltRestartInternal`을 가로채, 패닉 재시작(type 4, 9)에서는 플랫폼 패닉 동작까지 원래대로 돌린 뒤 FCH 리셋 레지스터 `0xCF9`에 `0x02`, `0x0E`를 써서 콜드 리셋한다. 그래도 돌아오면 키보드 컨트롤러 리셋(`0x64`에 `0xFE`), 그다음 원래 경로로 간다. Sequoia와 Tahoe에 같은 함수가 있다.

시험: AV1 하드웨어 디코딩과 Metal 부하 중에 `sudo sysctl -w debug.rmb_panic=0x7061`로 패닉을 내자 50초 만에 스스로 다시 부팅했고 패닉 보고서도 남았다. `sysctl -w debug.rmb_panicreset=0`이나 부팅 인자 `-rmbnopanicreset`로 원래 방식으로 돌린다.

## 5. 커널 확장의 Tahoe 처리 (`kext/`)

- VCN 훅은 Sequoia와 같이 `-rmbvcn`으로 켠다. 구조체 오프셋과 `waitForStamp`가 Tahoe 드라이버에서도 같았다. Tahoe에서만 끄려면 `-rmbnovcntahoe`.
- VCN이 꺼지면 하드웨어 코덱을 숨긴다(VideoToolbox는 소프트웨어 코덱을 쓴다). 꺼진 VCN에 작업이 가면 4.2의 GPU 재시작이 된다.
- Darwin 25에서는 `-rmbnodisklog`를 무시하고 디스크 로그(`/private/var/log/rmb-msgbuf.log`)를 항상 남긴다. Tahoe가 멈추면 Sequoia로 부팅해 `/Volumes/Tahoe - Data/private/var/log/rmb-msgbuf.log`를 읽는다.
- AV1 VideoToolbox 플러그인 이미지는 Tahoe에서 `m8-vtplugins build`로 다시 만든다(Sequoia 이미지의 Apple 플러그인 복사본은 버전이 다르다).

## 6. 복구 모드와 설치 환경 (실기기에서 안 뜸)

지금 설정으로 OpenCore에서 Tahoe 복구(26.7.3)를 고르면, 커널과 초기 데몬까지 뜨고 복구 화면이 나오기 전에 멈춘다. 결론부터: **설치는 가상 머신에서 하거나(`docs/install.md` 6절) 이미 도는 macOS에서 설치 앱으로 한다.** 설치 단계에는 원래 가속이 없고, 설치된 시스템의 첫 부팅부터는 지금 EFI로 가속이 붙는다.

### 복구 이미지에는 가속 드라이버가 없다

복구 볼륨의 `BaseSystem.dmg`에 든 `BaseSystemKernelExtensions.kc`에는 `AMDRadeonX6000Framebuffer`(화면 출력)만 있고, `AMDRadeonX6000HWLibs`, `AMDRadeonX6000HWServices`, `AMDRadeonX6000`(가속기)은 없다. 그래서 복구와 설치 환경에서는 Metal 가속이 불가능하다.

### 왜 멈추나

평소에는 HWLibs가 PSP로 화면 펌웨어(DMUB)를 다시 올리고, RembrandtGfx가 그 직전에 DMUB를 초기화한다(`dmub-reset`). 복구에는 HWLibs가 없어 이 단계가 없다. 화면 드라이버(DAL)는 부팅 화면(GOP)이 쓰던 DMUB를 그대로 넘겨받는다.

복구 부팅의 커널 로그(아래 진단으로 얻음):

```
[ 2.80] found com.apple.kext.AMDRadeonX6000Framebuffer        <- HWLibs, AMDRadeonX6000은 나오지 않음
[ 3.34] dmub-cmd: held NULL-only batch, DAL wptr 0, hw wptr 5c0 <- DAL이 처음 쓰기 전에 이미 명령 23개가 대기
[10.20] stall: DMUB stuck at [0] (wptr 5c0) type 80 sub 3 payload 00000700
        IH 5s: clients none; DCE src none                       <- 이후 GPU 인터럽트 없음
```

멈춘 명령은 `ENABLE_DISP_POWER_GATING`으로, 평소 부팅에서는 RembrandtGfx가 걸러 내는 명령이다(PHY가 죽는다). 복구에서는 거르기 전에 이미 하드웨어 큐에 들어가 있었다.

복구와 설치 환경에서만 AMD 화면 컨트롤러(`AmdRadeonController::start`)를 거절해 기본 프레임버퍼로 떨어지게도 해 봤지만 여전히 복구 화면이 뜨지 않았다(GUI 프로세스가 계속 다시 시작하는 것으로 보였다). 실익이 없어 여기서 멈췄다. 이어서 볼 사람은 GPU 장치 속성(`DeviceProperties`의 Navi 23 위장)이 복구에도 적용된다는 점부터 보면 된다. 설치 기간에 위장과 kext를 둘 다 뺀 설정(1절 4번)에서는 설치 환경이 떴다.

### 진단 방법: 패닉에 커널 로그 싣기

복구는 읽기 전용 이미지에서 돌아 디스크 로그가 남지 않는다. 대신 kext가 부팅 150초에 일부러 패닉을 내면서 커널 로그 일부를 패닉 메시지에 싣게 했다. NVRAM의 패닉 기록은 2.3KB 정도만 남으므로, 30초에 kext의 설정 줄을 먼저 떠 두고 150초에 오류성 줄을 덧붙였다. 패닉 뒤에는 `panicreset`이 재시작하고, 다음 정상 부팅이 패닉 보고서를 `/Library/Logs/DiagnosticReports`에 남긴다. 이 코드는 공개 kext에는 넣지 않았다(부팅 인자 `-rmbrecdiag`로 켜던 실험 코드).

## 7. 남은 것

- USB 오디오 끊김과 수 시간 단위 안정성 확인.
