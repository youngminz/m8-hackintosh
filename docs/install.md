# GMKtec NucBox M8에 macOS 15.8.2를 설치한 실제 절차

이 문서는 2026-11-10 새벽(KST 02:17부터 07:13까지)의 Claude Code 세션 기록과 작업 저장소 `m8-hackintosh`의 커밋 `eb80fdf`부터 `a2c43df`까지를 근거로, 실제로 실행한 것만 정리한 것이다. 추측이나 확인하지 못한 내용은 마지막 "불분명한 점"에 따로 모았다.

> 이 문서가 가리키는 경로 가운데 공개 저장소에 없는 것이 있다. `roms/`(VBIOS), `smbios.txt`, `dump/`의 대부분, `logs/`, `recovery/`, `efi/`의 바이너리는 기기 식별 값이나 타사 바이너리라 올리지 않았다. 여기에 있는 것은 `configs/`, `acpi/`(원래 `src/acpi/`), `kext/`(원래 `patches/`, MCE 차단용 `AppleMCEReporterDisabler.kext` 포함), `scripts/`(원래 `scripts/`와 `ventoy/`), `hardware/`(`lspci`, `cpuinfo`)다. 커밋 번호는 비공개 작업 저장소의 것이다.

표기 규칙

- `<M8_IP>`: M8이 DHCP로 받은 주소. Ubuntu 라이브와 macOS가 같은 주소를 받았다.
- `<DISK>`: 설치 대상 NVMe의 `/dev/disk/by-id/nvme-<모델>_<일련번호>` 경로.
- `<GITHUB_USER>`: 작업용 맥의 SSH 공개 키가 등록된 GitHub 계정.
- `<MAC_USER>`: macOS에서 만든 계정의 짧은 이름.
- `$M`: 작업용 맥의 저장소 경로(`~/workspaces/m8-hackintosh`).
- 근거 표기: `[세션 hh:mm]`은 주 세션 기록의 KST 시각, `[커밋 xxxxxxx]`는 저장소 커밋.

## 0. 대상 하드웨어와 결과

| 항목 | 값 | 근거 |
|---|---|---|
| 본체 | GMKtec NucBox M8, AMI BIOS `M8 V1.04` (2026-04-23) | `dump/dmidecode.txt`, [세션 02:51] |
| CPU | Ryzen 5 PRO 6650H, 6코어 12스레드 | `dump/cpu.txt` |
| GPU | Radeon 660M, PCI `1002:1681` 리비전 `0x13`, 경로 `PciRoot(0x0)/Pci(0x8,0x1)/Pci(0x0,0x0)` | `dump/lspci.txt`, [세션 07:05] |
| 유선 랜 | RTL8125 2.5G 두 개 | `dump/lspci.txt` |
| 무선 | MediaTek MT7922 (macOS 드라이버 없음, 동작 안 함) | [세션 07:08] |
| 오디오 | Realtek ALC269VC | [세션 02:52] |
| 설치 디스크 | Fanxiang S500Pro 512GB (Maxio MAP1202, DRAM 없음, HMB 사용) | [세션 06:41] |
| 모니터 | Dell U3223QE, USB-C 케이블 한 가닥(영상, USB 허브, USB 랜) | [세션 06:54] |
| 결과 | macOS Sequoia 15.8.2 (24H232), Metal 지원, 4K 출력, SSH 접속 | [세션 07:02, 07:05] |

참고한 선행 사례는 GPD Win Max 2(6800U, 680M)용 공개 저장소 `caoshiyuan0211-blip/winmax2-2022-6800u-hackintosh`이며 `$M/ref-winmax2`로 복제해 썼다. GPU 드라이버 `RembrandtGfx`(Lilu 플러그인)와 `config.plist.example`, 보조 스크립트가 여기서 왔다.

## 1. 준비물과 사전 조건

1. 작업용 맥: Xcode 명령줄 도구(`clang`, `ld`), `gh`, `python3`, Homebrew의 `acpica`(`iasl`). [세션 02:55, 02:57]
2. USB 저장 장치: Ventoy가 설치된 외장 SSD(Samsung T5). 루트에 `ubuntu-26.04.1-desktop-amd64.iso`를 넣어 두었다. Ventoy 자체를 어떻게 설치했는지는 기록에 없다. [세션 03:25]
3. 네트워크: M8과 작업용 맥이 같은 LAN. M8은 모니터의 USB 랜으로 연결되어 DHCP 주소를 받았다. 설치 프로그램이 macOS를 인터넷에서 내려받으므로 유선 인터넷이 필수다.
4. BIOS: 설치 과정에서 BIOS 설정을 바꾼 기록은 없다. Ubuntu에서 확인한 상태는 다음과 같았고, 이 상태가 전제 조건이다.
   - Secure Boot 꺼짐: `mokutil --sb-state` 결과 `SecureBoot disabled`. [세션 02:52]
   - 가상화(SVM) 켜짐: `/dev/kvm` 존재, `kvm_amd` 로드, Nested Paging 활성. [세션 03:35, 03:37]
   - 부팅 메뉴 키는 F7로 안내했다(사용자가 실제로 누른 키는 기록에 없다).
5. 디스크: M8에는 NVMe가 두 개 있었다(Windows가 든 Firebat, 거의 안 쓴 Fanxiang). 사용자가 Fanxiang을 지우라고 명시했고, 파티션을 나눈 뒤 실수 방지를 위해 Windows 디스크는 물리적으로 뽑았다. [세션 02:55, 03:01]

## 2. Ubuntu 라이브에서 SSH 열기

1. Ventoy로 Ubuntu 26.04.1 데스크톱 ISO를 부팅해 "Try Ubuntu" 세션으로 들어간다.
2. 첫 부팅에서는 M8 터미널에 직접 세 줄을 입력했다. 비밀번호 대신 GitHub에 등록된 공개 키를 받아 온다. [세션 02:49]

   ```bash
   sudo apt update && sudo apt install -y openssh-server
   mkdir -p ~/.ssh && wget -qO- https://github.com/<GITHUB_USER>.keys >> ~/.ssh/authorized_keys && chmod 700 ~/.ssh && chmod 600 ~/.ssh/authorized_keys
   sudo systemctl start ssh
   ```

3. 작업용 맥에서 접속을 확인한다. 라이브 세션은 부팅할 때마다 호스트 키가 바뀌므로 매번 이전 키를 지운다.

   ```bash
   ssh-keygen -R <M8_IP>
   ssh -o StrictHostKeyChecking=accept-new ubuntu@<M8_IP> 'uname -a; whoami; head -2 /etc/os-release'
   ```

4. 두 번째 부팅부터는 자동화했다(Ventoy persistence). Ventoy 데이터 파티션에 4GB짜리 `casper-rw` 이미지를 만들고, 그 안의 overlay에 SSH 설정 스크립트와 GNOME 자동 시작 항목을 넣었다. 이후에는 Ubuntu 데스크톱이 뜨면 아무 입력 없이 SSH가 열린다. [세션 03:26, 커밋 1257dd7, `ventoy/m8-ssh-setup.sh`, `ventoy/m8-ssh.desktop`]

   ```bash
   # Ubuntu 라이브 안에서. 스크립트 두 개는 먼저 /tmp 로 복사해 둔다.
   sudo mkdir -p /mnt/v && sudo mount /dev/mapper/VentoyPart /mnt/v
   V=/mnt/v; sudo mkdir -p $V/persistence $V/ventoy
   sudo truncate -s 4G $V/persistence/ubuntu-ssh.dat        # exFAT 에서는 fallocate 가 실패한다
   sudo mkfs.ext4 -q -F -L casper-rw $V/persistence/ubuntu-ssh.dat
   sudo mkdir -p /mnt/p && sudo mount -o loop $V/persistence/ubuntu-ssh.dat /mnt/p
   U=/mnt/p/upper
   sudo mkdir -p $U/usr/local/bin $U/home/ubuntu/.config/autostart $U/home/ubuntu/.ssh /mnt/p/work
   sudo install -m 755 /tmp/m8-ssh-setup.sh $U/usr/local/bin/m8-ssh-setup
   sudo install -m 644 /tmp/m8-ssh.desktop $U/home/ubuntu/.config/autostart/m8-ssh.desktop
   sudo cp ~/.ssh/authorized_keys $U/home/ubuntu/.ssh/authorized_keys
   sudo chown -R 1000:1000 $U/home/ubuntu; sudo chmod 700 $U/home/ubuntu/.ssh; sudo chmod 600 $U/home/ubuntu/.ssh/authorized_keys
   sudo umount /mnt/p
   printf '{\n  "persistence": [\n    { "image": "/ubuntu-26.04.1-desktop-amd64.iso", "backend": "/persistence/ubuntu-ssh.dat", "autosel": 1 }\n  ]\n}\n' | sudo tee $V/ventoy/ventoy.json
   sync
   ```

   - `m8-ssh-setup`: `openssh-server`가 없으면 설치하고, 공개 키를 `authorized_keys`에 넣고, `ssh` 서비스를 켠다.
   - `m8-ssh.desktop`: 로그인 시 `sudo /usr/local/bin/m8-ssh-setup`을 실행하고 로그를 `/tmp/m8-ssh-setup.log`에 남긴다.
   - 확인: 재부팅 후 `cat /tmp/m8-ssh-setup.log`, `findmnt -no SOURCE,OPTIONS /`. 첫 부팅은 SSH 설치에 1분쯤 걸렸다. [세션 03:35]

## 3. 하드웨어 덤프 (`dump/` 만들기)

작업용 맥에서 실행했다. [세션 02:51, 커밋 eb80fdf]

```bash
D=$M/dump; mkdir -p $D/acpi
ssh ubuntu@<M8_IP> 'sudo tar -C /sys/firmware/acpi/tables -cf - .' | tar -C $D/acpi -xf -
ssh ubuntu@<M8_IP> 'sudo dmidecode' > $D/dmidecode.txt
ssh ubuntu@<M8_IP> 'lspci -nnvvv 2>/dev/null; echo; lspci -tv' > $D/lspci.txt
ssh ubuntu@<M8_IP> 'sudo dmesg' > $D/dmesg.txt
ssh ubuntu@<M8_IP> 'lscpu; cat /proc/cpuinfo | head -30' > $D/cpu.txt
ssh ubuntu@<M8_IP> 'for c in /sys/class/drm/card1-*; do echo "$c $(cat $c/status 2>/dev/null)"; done; cat /proc/meminfo | head -3; sudo cat /sys/kernel/debug/dri/*/amdgpu_firmware_info 2>/dev/null | head -40; cat /sys/class/drm/card1/device/mem_info_vram_total' > $D/gpu.txt
cd $D/acpi && iasl -d -dl DSDT SSDT* && iasl -d APIC
```

덤프에서 확인해 둔 것

- GPU가 선행 사례와 같은 장치 ID `1002:1681`이고 IP 블록이 PSP 13, SMU 13, VCN 3, GFX 10(`dmesg`).
- `VFCT` 테이블이 있음(VBIOS 추출용).
- `lspci`를 root 없이 돌려서 링크 속도 정보가 빠졌다. 필요하면 `sudo lspci -nnvvv`로 받는 편이 낫다. [세션 03:06]
- 디스크 식별: `ls -l /dev/disk/by-id/ | grep nvme`, `sudo nvme smart-log`, `sudo efibootmgr -v`, `sudo blkid`로 어느 디스크가 Windows인지 구분했다. [세션 02:53]

## 4. 디스크 파티션

대상 디스크 전체를 지우고 GPT로 두 파티션을 만들었다. 커널 이름(`nvme1n1`)은 다른 디스크를 뽑으면 `nvme0n1`로 바뀌므로 모든 명령에서 `by-id` 경로를 썼다. [세션 02:55]

```bash
ssh ubuntu@<M8_IP> 'set -e; D=<DISK>
  [ "$(readlink -f $D)" = /dev/nvme1n1 ]        # 대상이 맞는지 한 번 더 확인
  sudo apt-get install -y -qq gdisk dosfstools
  sudo wipefs -a ${D}-part1 ${D}-part2 ${D}-part3 ${D}-part4
  sudo sgdisk --zap-all $D
  sudo sgdisk -n1:0:+4G -t1:ef00 -c1:EFI -n2:0:0 -t2:af0a -c2:macOS $D
  sudo partprobe $D; sleep 2
  sudo mkfs.fat -F32 -n OC ${D}-part1
  sudo sgdisk -p $D; lsblk -f $D'
```

- 파티션 1: 4GB, EFI 시스템 파티션, FAT32, 라벨 `OC`. OpenCore와 복구 이미지를 함께 넣으려고 크게 잡았다.
- 파티션 2: 나머지(약 473GiB), 타입 `af0a`(APFS). 포맷은 macOS 설치 프로그램의 디스크 유틸리티에서 했다.

## 5. OpenCore EFI 구성 (작업용 맥에서)

### 5.1 내려받은 것과 버전

`gh release download`로 받았다. [세션 02:57, `efi/EFI/OC/Kexts/*/Contents/Info.plist`]

| 구성 요소 | 버전 | 출처 | 최종 상태 |
|---|---|---|---|
| OpenCore | 1.0.5 DEBUG 빌드 | acidanthera/OpenCorePkg | 사용 |
| 드라이버 | `OpenRuntime.efi`, `OpenHfsPlus.efi`, `ResetNvramEntry.efi` | OpenCore 1.0.5 | 사용 |
| 도구 | `OpenShell.efi` | OpenCore 1.0.5 | 사용 |
| Lilu | 1.7.2 | acidanthera/Lilu | 켬 |
| VirtualSMC | 1.3.8 | acidanthera/VirtualSMC | 켬 |
| AmdTscSync | 2.0.0 | naveenkrdy/AmdTscSync | 켬 |
| RestrictEvents | 1.1.6 | acidanthera/RestrictEvents | 켬 |
| LucyRTL8125Ethernet | 1.2.4 | Mieze/LucyRTL8125Ethernet | 켬 |
| AppleMCEReporterDisabler | 직접 작성한 plist 전용 kext | 7.4절 | 켬 |
| RembrandtGfx | 0.1.0 + M8용 패치 | ref-winmax2 소스를 직접 빌드 | 켬 |

AppleALC 1.9.8과 RTCMemoryFixup 1.0.7도 내려받았지만 EFI에는 넣지 않았다(오디오는 설치 후 사용자가 "소리 된다"고 보고).

### 5.2 RembrandtGfx 빌드

```bash
cd $M/ref-winmax2/src
git clone -q --depth 1 https://github.com/acidanthera/MacKernelSDK
git clone -q --depth 1 --branch 1.7.2 https://github.com/acidanthera/Lilu Lilu-1.7.2
mkdir -p ../fwsrc
# GPU 펌웨어는 M8의 Ubuntu 에서 가져온다 (심볼릭 링크 대상 두 개도 따로 받아야 빌드가 된다)
ssh ubuntu@<M8_IP> 'cd /usr/lib/firmware/amdgpu && tar cf - yellow_carp_* psp_13_0_5_ta.bin.zst vcn_3_1_2.bin.zst' | tar -C ../fwsrc -xf -
cd RembrandtGfx && make -j8 LD=/usr/bin/ld FWSRC=../../fwsrc
file RembrandtGfx          # x86_64 Mach-O 인지 확인
```

[세션 02:56] 처음에는 `yellow_carp_*`만 받아서 링크 대상이 없어 실패했고, 두 파일을 추가로 받아 성공했다.

### 5.3 SSDT

- `SSDT-PLUG-M8`: M8의 DSDT는 CPU를 `ACPI0007` 장치(`\_SB.PLTF.P000` 등)로 정의한다. 선행 사례의 `SSDT-PLUG-ALT`와 같은 방식으로, MADT의 프로세서 ID 1부터 12에 맞춘 `Processor (CP00..CP0B)` 객체 12개를 Darwin에서만 보이게 추가하고 `CP00`에 `plugin-type = 1`을 준다. MADT ID는 `iasl -d APIC`로 확인했다.
- `SSDT-EC-USBX-M8`: 선행 사례의 `SSDT-EC-USBX.dsl`을 그대로 쓰되 OEM ID만 바꿨다. 실제 EC 경로는 `\_SB.PCI0.SBRG.EC0`으로 GPD와 같았다(처음에 `LPC0`으로 바꿨다가 DSDT를 보고 되돌렸다).
- 선행 사례의 나머지 SSDT 여섯 개(PNLF, I2C, XHC2, SWUS, USB NOWAKE, PS2K)와 ACPI 패치 11개는 GPD 전용이라 모두 뺐다.
- 컴파일: `iasl -va SSDT-PLUG-M8.dsl`, `iasl -va SSDT-EC-USBX-M8.dsl`. [세션 02:58, `src/acpi/`]

### 5.4 VBIOS (`ATY,bin_image`)

```bash
cd $M && mkdir -p roms
python3 ref-winmax2/scripts/vfct-rom.py dump/acpi/VFCT
#  -> roms/680m.rom (원본, 44544바이트), roms/680m-73ff.rom (PCIR 장치 ID를 0x73FF로 바꾸고 체크섬 수정)
python3 ref-winmax2/scripts/augment-rom.py roms/680m-73ff.rom roms/680m-73ff-aug.rom
#  -> 0xB600 바이트. Apple Navi 23 프레임버퍼가 요구하는 PSP 디렉터리와 vram_info 테이블을 덧붙인 것
```

`roms/680m-73ff-aug.rom` 전체를 GPU 장치 경로의 `ATY,bin_image` 데이터로 넣었다. 파일 이름은 선행 사례 스크립트가 정한 것이고 내용은 M8의 660M VBIOS다. [세션 02:58]

### 5.5 SMBIOS

```bash
S=$M/dl/x/OpenCore-1.0.5-DEBUG/Utilities/macserial/macserial; chmod +x $S
$S -m MacPro7,1 -n 1 | tail -1 > $M/smbios.txt     # "일련번호 | MLB"
uuidgen >> $M/smbios.txt
```

`PlatformInfo.Generic`에 `SystemProductName=MacPro7,1`, 생성한 일련번호, MLB, UUID를 넣고, `ROM`은 M8 유선 랜의 MAC 주소 6바이트로 채웠다. 이 값들은 기기마다 새로 만들어야 하며 공개 저장소에 올리면 안 된다(`smbios.txt`와 `config.plist`에 그대로 들어 있다). [세션 02:59]

### 5.6 config.plist 생성

`ref-winmax2/efi/config.plist.example`을 파이썬 `plistlib`로 읽어 아래만 바꾼 뒤 `efi/EFI/OC/config.plist`로 저장했다. [세션 02:59, 커밋 eb80fdf]

선행 사례에서 그대로 가져온 것

- AMD 커널 패치 15개(algrey 등, AMD Vanilla 계열). 26개 항목 중 15개가 켜져 있다.
- `Booter.Quirks`(`DevirtualiseMmio`, `RebuildAppleMemoryMap`, `SyncRuntimePermissions`, `SetupVirtualMap`, `ProvideCustomSlide` 등)와 `MmioWhitelist`의 `0xFD000000` 항목.
- `Kernel.Quirks`(`ProvideCurrentCpuInfo`, `PanicNoKextDump`, `PowerTimeoutKernelPanic`, `DisableLinkeditJettison`), `Kernel.Emulate.DummyPowerManagement = true`.
- GPU `DeviceProperties`의 `device-id = FF 73 00 00`, `AAPL,iokit-ignore-ndrv`, `CFG_NO_BOOT_PWDN`, `CFG_USE_AGDC`.
- NVRAM의 `csr-active-config = 03020000` 등.

M8에 맞게 바꾼 것

| 항목 | 값 |
|---|---|
| `ACPI.Add` | `SSDT-PLUG-M8.aml`, `SSDT-EC-USBX-M8.aml` 두 개만 |
| `ACPI.Patch` | 전부 끔 |
| 코어 수 패치 (`Force cpuid_cores_per_package`) | `Replace`를 `BA 08 00 00 00`에서 `BA 06 00 00 00`으로 (6코어) |
| `DeviceProperties` | GPU 경로 하나만 남기고 `ATY,bin_image`를 M8 VBIOS로, `model = AMD Radeon 660M` |
| `Kernel.Add` | 5.1절의 kext 7개, 순서는 Lilu, VirtualSMC, AmdTscSync, AppleMCEReporterDisabler, RestrictEvents, LucyRTL8125Ethernet, RembrandtGfx |
| `Misc.Debug` | `AppleDebug`, `ApplePanic`, `DisableWatchDog` 켬, `Target = 67`(파일 로그) |
| `Misc.Boot` | `PickerMode = Builtin`, `HideAuxiliary = false`, `Timeout = 10`, `PollAppleHotKeys = true` |
| `Misc.Security` | `SecureBootModel = Disabled`, `AllowSetDefault = true`, `ScanPolicy = 0` |
| `boot-args` (첫 부팅용) | `-v keepsyms=1 debug=0x100 ipc_control_port_options=0 agdpmod=pikera -rmbnomode2 -lilubetaall -rmbverbose msgbuf=4194304` |
| `UEFI.Drivers` | `OpenRuntime.efi`, `OpenHfsPlus.efi`, `ResetNvramEntry.efi` |

검증: `ocvalidate efi/EFI/OC/config.plist` 결과 `No issues found`.

비교용으로 GPU 드라이버를 끈 `configs/config-nogpu.plist`도 만들었다(RembrandtGfx 끔, `DeviceProperties` 비움, `boot-args`에 `-amd_no_dgpu_accel`). [커밋 096fdf0]

### 5.7 macOS 복구 이미지

```bash
R=$M/dl/x/OpenCore-1.0.5-DEBUG/Utilities/macrecovery
mkdir -p $M/recovery && cd $M/recovery
script -q /dev/null python3 $R/macrecovery.py -b Mac-937A206F2EE63C01 -m 00000000000000000 download > dl.log 2>&1
ls -la com.apple.recovery.boot      # BaseSystem.dmg (약 884MB), BaseSystem.chunklist
```

`macrecovery.py`는 터미널 크기를 읽으므로 tty 없이 실행하면 `OSError: Inappropriate ioctl for device`로 죽는다. `script -q /dev/null`로 가상 터미널을 붙여 해결했다. 로그 끝의 `Image verification complete!`로 확인한다. [세션 02:59]

### 5.8 ESP에 복사하고 부팅 항목 등록

```bash
ssh ubuntu@<M8_IP> 'sudo mkdir -p /mnt/oc && sudo mount <DISK>-part1 /mnt/oc'
cd $M && COPYFILE_DISABLE=1 tar -cf - -C efi EFI -C $M/recovery com.apple.recovery.boot \
  | ssh ubuntu@<M8_IP> 'sudo tar -C /mnt/oc -xf - --no-same-owner && sync && find /mnt/oc -maxdepth 3 | sort'
ssh ubuntu@<M8_IP> 'sudo umount /mnt/oc
  sudo efibootmgr -c -d /dev/nvme1n1 -p 1 -L "OpenCore" -l "\\EFI\\OC\\OpenCore.efi"
  N=$(sudo efibootmgr | awk "/OpenCore/{print substr(\$1,5,4)}"); sudo efibootmgr -n $N'
```

ESP 배치: `/EFI/BOOT/BOOTx64.efi`, `/EFI/OC/{OpenCore.efi,config.plist,ACPI,Drivers,Kexts,Tools}`, `/com.apple.recovery.boot/`. 펌웨어에는 `Boot0001 OpenCore`가 생겼다. 이후 재부팅할 때마다 `sudo efibootmgr -n 0001`로 다음 한 번만 OpenCore로 부팅하게 했고, 기본 순서는 Ventoy가 먼저였다. [세션 03:00, 03:27]

## 6. 설치: 실기기 복구 부팅은 실패, QEMU 가상 머신으로 설치

### 6.1 실기기에서 복구 이미지 부팅 (실패)

1. OpenCore 선택 화면에서 `OC (dmg)`(복구 이미지)를 골랐다. 커널과 launchd까지 올라오고 `com.apple.recoveryosd`가 실행됐지만 `SLSPackagesInitializeSessionPreferences` 부근에서 화면이 넘어가지 않았다. [세션 03:10부터 03:21]
2. GPU 드라이버를 끈 `config-nogpu.plist`로 바꿔 다시 시도했지만 같은 자리에서 멈췄다. [세션 03:27부터 03:32]
3. 복구 환경은 로그가 디스크에 남지 않아 원인을 볼 수 없었다. 그래서 설치만 가상 머신에서 하기로 했다. 선행 사례도 가상 머신에서 설치한 뒤 실기기로 옮긴 방식이었다.

### 6.2 QEMU 준비

Ubuntu 라이브(persistence 적용)에서 실행했다. 작업 파일은 Ventoy 데이터 파티션에 두어 재부팅 후에도 남게 했다. [세션 03:36]

```bash
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq qemu-system-x86 qemu-utils dmg2img git python3-pip socat
sudo mkdir -p /mnt/v && sudo mount -o uid=1000,gid=1000 /dev/mapper/VentoyPart /mnt/v
mkdir -p /mnt/v/vm && cd /mnt/v/vm && git clone -q --depth 1 https://github.com/kholia/OSX-KVM
cd OSX-KVM
sudo mount -o ro <DISK>-part1 /mnt/oc
dmg2img -s -i /mnt/oc/com.apple.recovery.boot/BaseSystem.dmg -o BaseSystem.img
sudo umount /mnt/oc
```

QEMU는 10.2.1이었다.

### 6.3 가상 머신 실행 스크립트 (`run-m8.sh`)

OSX-KVM의 `OpenCore-Boot.sh`를 바탕으로 만들었다. root로 실행한다. [세션 03:36]

```bash
#!/bin/bash
cd /mnt/v/vm/OSX-KVM
echo 1 > /sys/module/kvm/parameters/ignore_msrs
exec qemu-system-x86_64 -enable-kvm -m 6144 \
  -cpu Skylake-Client,-hle,-rtm,kvm=on,vendor=GenuineIntel,+invtsc,vmware-cpuid-freq=on,+ssse3,+sse4.2,+popcnt,+avx,+aes,+xsave,+xsaveopt,check \
  -machine q35 -smp 8,cores=4,sockets=1 \
  -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 -device usb-tablet,bus=xhci.0 \
  -device isa-applesmc,osk="<OSX-KVM의 OpenCore-Boot.sh에 있는 osk 값>" \
  -drive if=pflash,format=raw,readonly=on,file=OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=OVMF_VARS-1920x1080.fd \
  -smbios type=2 -device ich9-ahci,id=sata \
  -drive id=OpenCoreBoot,if=none,snapshot=on,format=qcow2,file=OpenCore/OpenCore.qcow2 -device ide-hd,bus=sata.2,drive=OpenCoreBoot \
  -drive id=InstallMedia,if=none,file=BaseSystem.img,format=raw -device ide-hd,bus=sata.3,drive=InstallMedia \
  -drive id=MacHDD,if=none,file=<DISK>,format=raw,cache=none,aio=native -device ide-hd,bus=sata.4,drive=MacHDD \
  -netdev user,id=net0 -device virtio-net-pci,netdev=net0,id=net0,mac=<VM_MAC> \
  -device vmware-svga -display none -vnc 127.0.0.1:0 \
  -qmp unix:/tmp/qmp.sock,server,nowait -monitor unix:/tmp/mon.sock,server,nowait
```

각 선택의 이유

- 실기기 디스크 전체(`<DISK>`)를 raw로 직접 물렸다. 가상 디스크 이미지를 만들어 `dd`로 옮기는 단계가 없다. 가상 머신 안에서는 가상 SATA 디스크로 보인다.
- 부팅은 OSX-KVM에 든 OpenCore(`OpenCore.qcow2`, `snapshot=on`)와 OVMF(`OVMF_CODE_4M.fd`)가 맡는다. 실기기 ESP의 OpenCore는 가상 머신에서 쓰지 않고 건드리지도 않는다.
- CPU는 `Skylake-Client`에 `vendor=GenuineIntel`로 Intel처럼 보이게 했다. OSX-KVM이 Sequoia용으로 지정한 줄이다.
- 그래픽은 `vmware-svga`, 화면은 `-display none`에 로컬 VNC(127.0.0.1:5900)와 QMP 소켓. GPU 패스스루는 하지 않았다.
- 네트워크는 사용자 모드 NAT(`-netdev user`). 설치 프로그램이 인터넷에서 macOS를 받는다.
- 메모리 6GB, 8스레드(호스트는 16GB 중 VRAM 3GB 제외 약 12GB).

### 6.4 원격 조작 도구 (`vmctl`)

`/usr/local/bin/vmctl`로 설치했다. QMP와 HMP 소켓을 `socat`으로 호출한다. [세션 03:37]

```bash
#!/bin/bash
# vmctl shot <file.png> | key <keys...> | click <x> <y> | hmp <cmd>
Q=/tmp/qmp.sock; M=/tmp/mon.sock
qmp(){ printf "%s\n%s\n" "{\"execute\":\"qmp_capabilities\"}" "$1" | sudo socat - UNIX-CONNECT:$Q | tail -1; }
case "$1" in
 shot) qmp "{\"execute\":\"screendump\",\"arguments\":{\"filename\":\"$2\",\"format\":\"png\"}}" >/dev/null; sudo chmod 644 "$2";;
 key) shift; for k in "$@"; do echo "sendkey $k" | sudo socat - UNIX-CONNECT:$M >/dev/null; sleep 0.15; done;;
 hmp) shift; echo "$*" | sudo socat - UNIX-CONNECT:$M | tail -n +2;;
 click) W=${W:-1920}; H=${H:-1080}; X=$(( $2 * 32767 / W )); Y=$(( $3 * 32767 / H ))
   qmp "{\"execute\":\"input-send-event\",\"arguments\":{\"events\":[{\"type\":\"abs\",\"data\":{\"axis\":\"x\",\"value\":$X}},{\"type\":\"abs\",\"data\":{\"axis\":\"y\",\"value\":$Y}}]}}" >/dev/null; sleep 0.2
   qmp "{\"execute\":\"input-send-event\",\"arguments\":{\"events\":[{\"type\":\"btn\",\"data\":{\"down\":true,\"button\":\"left\"}}]}}" >/dev/null; sleep 0.1
   qmp "{\"execute\":\"input-send-event\",\"arguments\":{\"events\":[{\"type\":\"btn\",\"data\":{\"down\":false,\"button\":\"left\"}}]}}" >/dev/null;;
esac
```

사용 패턴: 작업용 맥에서 `ssh ubuntu@<M8_IP> 'vmctl shot /tmp/s.png; cat /tmp/s.png' > s.png`로 화면을 받아 보고, 좌표를 읽어 `W=1280 H=800 vmctl click X Y`로 누른다. 실제 화면은 1280x800이었으므로 `W`, `H`를 그 값으로 줘야 좌표가 맞는다. USB 태블릿 장치를 붙였기 때문에 절대 좌표 클릭이 된다.

### 6.5 설치 진행

1. 실행: `sudo setsid nohup /mnt/v/vm/OSX-KVM/run-m8.sh > /tmp/qemu.log 2>&1 < /dev/null &`. 확인은 `pgrep -x qemu-system-x86`과 `lsmod | grep kvm`. [세션 03:37]
2. 가상 머신의 OpenCore 선택 화면에서 `vmctl key right right ret`로 "macOS Base System"을 골랐다. 약 90초 뒤 복구 화면이 떴다(실기기에서 멈췄던 단계).
3. 디스크 유틸리티에서 507.81GB로 보이는 파티션을 골라 이름 `M8`, 포맷 APFS로 지웠다. [세션 03:39, 03:40]
4. "Reinstall macOS Sequoia"를 시작했다. 사용권 계약 동의는 사용자에게 확인을 받은 뒤 눌렀다. 설치 대상은 `M8` 볼륨. 시작 화면은 `logs/vm-install-start.png`. [세션 03:44]
5. 내려받기와 설치에 약 25분 걸렸다(03:45 시작, 04:00에 첫 재부팅 후 2단계, 04:10에 설정 도우미). 재부팅 때 가상 머신의 OpenCore가 설치 항목을 자동으로 골라서 손댈 일이 없었다. 설정 도우미 화면은 `logs/vm-setup-assistant.png`.
6. 설정 도우미: 국가, 언어 기본값, "새 Mac으로 설정", 손쉬운 사용 건너뜀까지 원격으로 넘겼다. 계정 이름과 비밀번호는 사용자가 직접 입력했다. 이를 위해 M8의 Ubuntu 데스크톱에 VNC 창을 띄웠다. [세션 04:12]

   ```bash
   sudo apt-get install -y -qq tigervnc-viewer
   X=$(ls /run/user/1000/.mutter-Xwaylandauth.* | head -1)
   DISPLAY=:0 XAUTHORITY=$X setsid nohup vncviewer localhost:5900 >/tmp/vncviewer.log 2>&1 < /dev/null &
   ```

7. 나머지 설정 도우미: Apple 계정 건너뜀, 이용 약관 동의(사용자 확인 후), 시간대, 분석 데이터 공유 끔, 스크린 타임 나중에, 자동 업데이트는 "다운로드만". [세션 05:41부터 05:43]

### 6.6 가상 머신의 macOS에 SSH 열기

1. 원격 로그인 켜기: 시스템 설정의 "Remote Login" 화면을 열어 두고 스위치는 사용자가 VNC 창에서 직접 켰다. [세션 05:44, 05:45]
2. 포트 전달: `vmctl hmp "hostfwd_add net0 tcp:127.0.0.1:2222-:22"`. 가상 머신을 다시 켤 때마다 다시 실행해야 한다.
3. 공개 키 넣기(비밀번호를 모르는 상태): Ubuntu에서 `authorized_keys`를 HTTP로 잠깐 내주고, 가상 머신의 터미널에 키 입력으로 명령을 쳐 넣었다. [세션 05:45, 05:46]

   ```bash
   # Ubuntu
   mkdir -p /tmp/kw && cp ~/.ssh/authorized_keys /tmp/kw/k && cd /tmp/kw && (nohup python3 -m http.server 8000 --bind 0.0.0.0 >/tmp/kw.log 2>&1 &)
   # 가상 머신: Spotlight 로 Terminal 을 열고(vmctl key meta_l-spc ...) 아래를 sendkey 로 입력
   mkdir -p ~/.ssh;curl -s 10.0.2.2:8000/k>>~/.ssh/authorized_keys;tail -2 ~/.ssh/authorized_keys
   # 끝나면 Ubuntu 에서
   pkill -f "http.server 8000"
   ```

   문자열을 QEMU 키 이름으로 바꾸는 짧은 파이썬 변환기를 썼다(공백은 `spc`, `/`는 `slash`, `~`는 `shift-grave_accent` 등). `10.0.2.2`는 QEMU 사용자 모드 네트워크에서 호스트를 가리킨다.
4. 접속과 확인. 작업용 맥의 `~/.ssh/config`에 별칭을 추가했다. [세션 05:46, 05:56]

   ```
   Host m8vm
     HostName 127.0.0.1
     Port 2222
     User <MAC_USER>
     ProxyJump ubuntu@<M8_IP>
   ```

   ```bash
   ssh m8vm 'sw_vers; diskutil list | head -30'     # ProductVersion 15.8.2, BuildVersion 24H232
   ```

5. 진단 편의를 위해 임시로 비밀번호 없는 sudo를 열었다. 사용자가 VNC 창의 터미널에서 직접 실행했고, 실기기 부팅 성공 뒤 지웠다. [세션 06:00, 07:05]

   ```bash
   echo '<MAC_USER> ALL=(ALL) NOPASSWD: ALL' | sudo tee /etc/sudoers.d/claude    # 임시
   sudo rm -f /etc/sudoers.d/claude                                              # 끝난 뒤
   ```

### 6.7 가상 머신에서 실기기 부팅으로 넘어가기

1. 가상 머신 종료: `vmctl hmp system_powerdown`은 무시됐다. macOS 안에서 `sudo shutdown -h now`로 끄고, `pgrep -x qemu-system-x86`이 비는 것을 확인한다(`pgrep -f qemu-system`은 자기 명령줄에 걸려 항상 참이 된다). [세션 05:46부터 05:50]
2. 실기기 ESP의 `config.plist`를 원하는 설정으로 바꾼다(8절의 방법).
3. `sudo efibootmgr -n 0001 && sudo systemctl reboot --force -i`로 재부팅하고, OpenCore 선택 화면에서 볼륨 `M8`을 고른다.
4. 디스크 내용을 옮기는 작업은 없다. 같은 디스크를 가상 머신과 실기기가 번갈아 부팅한다. 실기기 부팅이 실패하면 Ubuntu로 돌아와 같은 가상 머신을 다시 켜서(선택 화면에서 `vmctl key right right right ret`로 `M8` 볼륨) 설치된 macOS 안의 로그를 읽었다. 이 왕복이 7절 진단의 기본 도구였다.

## 7. 실기기 첫 부팅에서 겪은 문제와 해결

05:50부터 07:02까지 실기기 부팅을 여덟 번쯤 반복했다. 순서대로 적는다.

### 7.1 증상의 출발점

- GPU 드라이버를 끈 설정으로 `M8` 볼륨을 부팅하면 verbose 화면의 `IOG flags 0x3`, `DK: IOUserDock...` 부근에서 멈췄다. 패닉 리포트도, 디스크의 통합 로그도 없었다(로그 파일이 0바이트로 생성만 됨). [세션 05:57, 06:08]
- 4K 모니터 탓일 가능성을 보고 `UEFI.Output.Resolution`을 `1920x1080@32`로 바꿨지만 같은 곳에서 멈췄다. [세션 06:06]
- macOS 안에 10초(나중에 3초)마다 `dmesg`, `ps`, `ifconfig`를 `/var/log/m8boot/`에 떨구는 LaunchDaemon을 넣었지만, 멈춘 부팅에서는 거의 아무것도 남지 않았다. 나중에 원인이 디스크 쓰기 정지였음이 드러났다. 이 데몬은 성공 후 지웠다. [세션 06:02부터 06:10, 07:02]

### 7.2 MMIO 화이트리스트 [커밋 9a66bb6]

- 단서: 선행 사례 README의 "`MmioWhitelist 0xFD000000`이 없으면 EFI 런타임 `SetVariable`이 실패한다"는 주의.
- 진단: Ubuntu에서 펌웨어의 런타임 MMIO 영역을 덤프했다.

  ```bash
  sudo bash -c 'cd /sys/firmware/efi/runtime-map; for d in $(ls | sort -n); do echo type=$(cat $d/type) phys=$(cat $d/phys_addr) pages=$(cat $d/num_pages) attr=$(cat $d/attribute); done'
  ```

  타입 `0xb`(런타임 MMIO) 영역이 `0x460000000`, `0xFEE01000`, `0xFEE00000`, `0xFD000000`, `0xE0000000` 다섯 개였다. 선행 사례 설정은 `0xFD000000` 하나만 화이트리스트에 있었다.
- 잘못된 시도: `Booter.Quirks.DevirtualiseMmio`를 통째로 껐더니 멈춤이 즉시 리셋으로 바뀌었다. ESP에 남은 OpenCore 로그를 보면 첫 부팅은 약 45초 뒤, 이후는 `EXITBS:START` 직후 몇 초 만에 리셋됐다. 패닉 정보도 남지 않았다. 되돌렸다. [세션 06:18부터 06:36]
- 최종 설정: `DevirtualiseMmio = true`를 유지하고 `Booter.MmioWhitelist`에 두 항목을 추가했다.

  ```bash
  P=/usr/libexec/PlistBuddy; f=efi/EFI/OC/config.plist
  $P -c 'Set :Booter:Quirks:DevirtualiseMmio true' $f
  for a in 3758096384 4276097024; do     # 0xE0000000 (PCIe ECAM), 0xFEE01000
    $P -c "Add :Booter:MmioWhitelist:0 dict" -c "Add :Booter:MmioWhitelist:0:Address integer $a" \
       -c "Add :Booter:MmioWhitelist:0:Comment string M8" -c "Add :Booter:MmioWhitelist:0:Enabled bool true" $f
  done
  ```

  결과적으로 켜진 항목은 `0xFD000000`(선행 사례), `0xE0000000`, `0xFEE01000` 세 개다.
- 효과: 빠른 리셋이 사라지고 다시 원래 지점까지 올라갔다. 두 항목 각각이 꼭 필요한지는 따로 분리해 시험하지 않았다.

### 7.3 디스크 쓰기 멈춤: NVMeFix 제거 [커밋 9a66bb6]

- 진단 도구: RembrandtGfx의 디스크 로그 기능. `-rmbverbose`가 있고 `-rmbnodisklog`가 없으면 커널 메시지 버퍼를 0.5초마다 `/private/var/log/rmb-msgbuf.log`에 fsync한다. GPU 드라이버를 켠 설정(`config-rmb.plist`)으로 부팅한 뒤 가상 머신에서 이 파일을 꺼냈다(`logs/rmb-msgbuf-bm1.log`).
- 증상: 디스크 로그는 부팅 9.4초에서 끊겼는데, 화면(사용자 사진)에는 그 뒤의 커널 메시지가 계속 찍혀 있었다. 즉 커널은 살아 있고 디스크 쓰기만 멈춘 상태였다. [세션 06:40부터 06:44]
- 판단: 대상 SSD는 DRAM 없는 Maxio MAP1202(`nvme id-ctrl`에서 `hmpre 4096`, `apsta 0x1`). 가상 머신에서는 가상 SATA로 보였기 때문에 문제가 없었고, NVMe로 직접 쓰는 것은 실기기 부팅이 처음이었다. 타사 SSD의 절전(APST)을 켜는 NVMeFix를 원인으로 보고 껐다.
- 설정: `NVMeFix.kext`를 EFI에서 뺐다. 처음에는 `Kernel.Add`에서 끄기만 했고, 이후 NVMeFix 없이 계속 정상 동작해서 항목과 파일을 모두 지웠다.
- 효과: 멈춤이 부팅 11.7초의 커널 패닉으로 바뀌었다(더 진행됨).

### 7.4 AppleIntelMCEReporter 패닉 차단 [커밋 9a66bb6]

- 증상: 화면에는 중첩 패닉만 보이고 첫 패닉 내용은 밀려 올라가 안 보였다.
- 진단: macOS는 패닉 내용을 NVRAM 변수 `AAPL,PanicInfo000N`에 남긴다. Ubuntu에서 선행 사례의 디코더로 읽었다.

  ```bash
  ssh ubuntu@<M8_IP> 'cat > /tmp/efipanic.py; ls /sys/firmware/efi/efivars | grep -i panic; sudo python3 /tmp/efipanic.py | head -80' < $M/ref-winmax2/scripts/efipanic.py
  ```

  `page fault`, `CR2 = 0x8`이었고, 세션은 역추적에서 `AppleIntelMCEReporter`를 원인으로 확인했다. SMBIOS가 MacPro7,1이면 이 Intel 전용 드라이버가 보드 ID로 매칭되어 시작하고, AMD에서는 널 포인터를 읽는다.
- 처음 넣었던 차단 kext가 빈 껍데기였다. dortania 가이드의 zip은 내려받지 못해 다른 공개 저장소에서 가져온 `Info.plist`를 썼는데, `IOKitPersonalities`가 비어 있어 아무 일도 하지 않았다. 다른 사본 두 개는 옛 `AppleMCEReporter`를 대상으로 한 것이라 맞지 않았다.
- 해결: 설치된 macOS에서 실제 매칭 조건을 읽고(`plutil -p /System/Library/Extensions/AppleIntelMCEReporter.kext/Contents/Info.plist`), 같은 매치 카테고리를 더 높은 점수로 먼저 차지하는 아무 일도 안 하는 퍼스널리티를 직접 썼다. `efi/EFI/OC/Kexts/AppleMCEReporterDisabler.kext/Contents/Info.plist`의 핵심은 다음과 같다.

  ```xml
  <key>IOKitPersonalities</key>
  <dict>
    <key>AppleIntelMCEInterruptControllerDisabler</key>
    <dict>
      <key>CFBundleIdentifier</key><string>com.apple.kpi.iokit</string>
      <key>IOClass</key><string>IOService</string>
      <key>IOMatchCategory</key><string>AppleIntelMCEInterruptController</string>
      <key>IOProviderClass</key><string>IOPlatformExpertDevice</string>
      <key>IOProbeScore</key><integer>100000</integer>
      <key>IOPropertyMatch</key>
      <array>
        <dict><key>board-id</key><string>Mac-F60DEB81FF30ACF6</string></dict>
        <dict><key>board-id</key><string>Mac-7BA5B2D9E42DDD94</string></dict>
        <dict><key>board-id</key><string>Mac-27AD2F918AE68F61</string></dict>
      </array>
    </dict>
  </dict>
  <key>OSBundleRequired</key><string>Root</string>
  ```

  `config.plist`의 `Kernel.Add` 항목은 `ExecutablePath`가 빈 plist 전용 kext다. `Kernel.Block`으로 막는 방법은 이 kext가 시스템 커널 컬렉션 안에 있어 통하지 않는다고 설정 주석에 적혀 있다.
- 효과: 패닉이 사라지고 AMD 그래픽 드라이버 초기화, 네트워크 인터페이스 생성, WindowServer 시작(`gIOScreenLockState 3`)까지 진행됐다. [세션 06:52]

### 7.5 PCI 리비전을 Navi 23 값으로 보고 [커밋 dbb67f2]

- 증상: 패닉 없이 verbose 글자 화면에서 그대로 멈춤. 그런데 `/var/log/m8boot`의 덤프(`logs/bm-065129/`)를 보니 `loginwindow`가 떠 있고 DHCP 주소도 받았다. 화면만 안 나온 것이다. [세션 06:56]
- 진단 1: 디스크 로그(`logs/bm2-boot.log`)에 `RembrandtGfx: << ttl_initialize = 1`, `ttl_initialize failed status = 0x00000001`. Apple의 HWLibs가 장치를 거부해 가속기가 붙지 않았고 프레임버퍼가 그것을 기다리고 있었다.
- 진단 2: 가상 머신에서 `/System/Library/KernelCollections/SystemKernelExtensions.kc`(373MB)를 받아 capstone으로 `_ttl_initialize`와 `_ttlIsSupportedDevice`를 역어셈블하고 지원 장치 표를 덤프했다. 표는 장치 ID와 PCI 리비전을 함께 본다. `0x73FF`(Navi 23)로 허용되는 리비전은 `0x40`부터 `0x43`, `0xC1`, `0xC3`, `0xC7`, `0xCB`뿐이었다. M8의 660M은 `0x13`이다. RembrandtGfx는 장치 ID만 `0x73FF`로 바꿔 보고하고 리비전은 실제 값을 넘기고 있었다. [세션 06:59, 07:00]
- 해결: `kern_start.cpp`의 PCI 설정 공간 읽기 후킹에 리비전 위장을 추가했다(`patches/rembrandtgfx-pci-revision.patch`).
  - `IOPCIDevice::extendedConfigRead8`도 후킹 목록에 추가.
  - 실제 장치가 `1002:1681`이고 읽는 오프셋이 리비전 ID이며 실제 리비전이 허용 목록에 없으면 `0xC1`(RX 6600 XT 값)로 돌려준다. 8, 16, 32비트 읽기 모두 처리.
  - 끄는 부팅 인자 `-rmbnopcirev`.
  - 다시 `make -j8 LD=/usr/bin/ld FWSRC=../../fwsrc`로 빌드해 ESP의 `EFI/OC/Kexts/RembrandtGfx.kext/Contents/MacOS/RembrandtGfx`를 교체.
- 효과: 다음 부팅에서 로그인 화면이 4K로 떴다. `system_profiler SPDisplaysDataType`에 `Device ID: 0x73ff`, `Metal: Supported`, VRAM 3GB, `ioreg -rc IOAccelerator`에 `AMDRadeonX6000_AMDNavi23GraphicsAccelerator`. [세션 07:02]

### 7.6 마무리 설정 [커밋 a2c43df]

- `boot-args`: `keepsyms=1 ipc_control_port_options=0 agdpmod=pikera -rmbnomode2 -lilubetaall -rmbnodisklog` (`-v`, `debug=0x100`, `-rmbverbose`, `msgbuf` 제거, 디스크 로그 끔).
- `Misc.Debug.Target = 3`(파일 로그 끔), `AppleDebug = false`. ESP에 쌓인 `opencore-*.txt` 삭제.
- `Misc.Boot.Timeout = 5`, `HideAuxiliary = true`.
- `Misc.Security.ScanPolicy = 0x80103`(524547): 내장 NVMe의 APFS만 스캔. 선택 화면에 `M8` 하나만 남아 5초 뒤 자동 부팅된다.
- macOS의 시동 디스크 지정(`sudo bless --mount / --setBoot`)은 `Could not set boot device property: 0xe00002e2`로 실패해서 위 방법으로 대신했다.
- 디버그용 이전 설정은 ESP에 `EFI/OC/config-debug.plist`로 남겼다.
- Ventoy USB가 꽂혀 있으면 펌웨어가 Ventoy부터 부팅한다. 평소에는 뽑아 둔다.

## 8. 복구 절차: macOS가 부팅되지 않을 때

1. Ventoy USB를 꽂고 전원을 길게 눌러 끈 뒤 켠다. Ventoy가 부팅 순서 1순위였고, 안 뜨면 부팅 메뉴(F7로 안내)에서 고른다. persistence가 선택된 Ubuntu가 뜨면 1분 안에 SSH가 열린다.
2. 작업용 맥에서 접속한다. 라이브 세션 호스트 키는 매번 다르다.

   ```bash
   until nc -z -G 2 <M8_IP> 22; do sleep 5; done
   ssh-keygen -R <M8_IP>; ssh -o StrictHostKeyChecking=accept-new ubuntu@<M8_IP> 'lsblk -o NAME,SIZE,FSTYPE,LABEL | grep -v loop'
   ```

3. ESP를 `/mnt/oc`에 마운트하고 파일을 되돌린다. `scp`는 라이브 세션에서 `Connection closed`로 실패했으므로 `ssh 'cat > 파일' < 로컬파일` 방식을 썼다.

   ```bash
   ssh ubuntu@<M8_IP> 'cat > /tmp/c.plist' < $M/configs/<되돌릴 설정>.plist
   ssh ubuntu@<M8_IP> 'set -e
     sudo mkdir -p /mnt/oc; sudo mount <DISK>-part1 /mnt/oc
     O=/mnt/oc/EFI/OC
     sudo cp $O/config.plist $O/config-bad.plist          # 문제 설정 보관
     sudo cp /tmp/c.plist $O/config.plist                 # 설정 되돌리기
     # 커널 확장도 되돌릴 때: sudo cp $O/RembrandtGfx.prev $O/Kexts/RembrandtGfx.kext/Contents/MacOS/RembrandtGfx
     sync; sudo umount /mnt/oc'
   ```

   되돌릴 수 있는 파일: `EFI/OC/config.plist`(저장소의 `configs/*.plist` 중 하나, 또는 ESP의 `config-prev.plist`, `config-debug.plist`), `EFI/OC/Kexts/RembrandtGfx.kext/Contents/MacOS/RembrandtGfx`(ESP의 `EFI/OC/RembrandtGfx.prev`), `EFI/OC/Kexts/AppleMCEReporterDisabler.kext/Contents/Info.plist`.
4. 진단 자료를 읽는다.
   - OpenCore 로그: `ls -t /mnt/oc/opencore-*.txt | head -1` (`Misc.Debug.Target = 67`일 때만 생긴다). 끝부분은 `tr -d "\000" < 파일 | tail`.
   - 패닉: `ls /sys/firmware/efi/efivars | grep -i panic` 후 `ref-winmax2/scripts/efipanic.py`.
   - macOS 안의 파일(`/private/var/log/rmb-msgbuf.log`, `/Library/Logs/DiagnosticReports`): 6.7절처럼 가상 머신으로 같은 디스크를 부팅해 `ssh m8vm`으로 읽는다. 가상 머신을 끈 뒤에만 ESP를 건드리고 재부팅한다.
5. 재부팅한다. USB를 꽂아 둔 채라면 `sudo efibootmgr -n 0001`로 다음 부팅만 OpenCore로 지정하고 `sudo systemctl reboot --force -i`. USB를 뽑고 재부팅해도 된다.

같은 날 12:20에 실험용 커널 확장으로 부팅 패닉이 났을 때 이 절차로 실제 복구했다(ESP는 이때 `/dev/nvme0n1p1`, 설정은 `config-safe.plist`, 커널 확장은 `RembrandtGfx.prev`). OpenCore 선택 화면만으로는 되돌릴 수 없다. 복구 모드도 같은 커널 확장과 부팅 인자로 뜨기 때문이다.

## 9. macOS 쪽 원격 작업 설정

| 설정 | 방법 | 시점과 근거 |
|---|---|---|
| 원격 로그인(SSH) | 시스템 설정의 Remote Login을 사용자가 직접 켬 | [세션 05:45] |
| 공개 키 | 6.6절의 방법으로 `~/.ssh/authorized_keys`에 추가 | [세션 05:46] |
| 맥 쪽 별칭 | `Host m8mac`(모니터 USB 랜 주소), 나중에 `Host m8lan`(본체 랜 주소). 둘 다 `HostKeyAlias m8-macos` | [세션 07:02, 07:21] |
| 호스트 키 | Ubuntu와 macOS가 같은 IP를 받아 `known_hosts`가 충돌한다. 가상 머신에서 확인해 둔 macOS 호스트 키를 `m8-macos` 별칭으로 등록해 해결 | [세션 07:02] |
| 임시 전체 sudo | `/etc/sudoers.d/claude`. 진단 중에만 쓰고 삭제 | [세션 06:00, 07:05] |
| 자동 업데이트 | 설정 도우미에서 "다운로드만" | [세션 05:43] |
| 최대 절전 끔 | `sudo pmset -a hibernatemode 0 standby 0 autopoweroff 0` (적용 스크립트에 포함) | [세션 07:13] |
| 설정 적용 스크립트 1 | 바탕화면의 `apply-oc-config.command`. `~/m8/config.plist`(와 있으면 `~/m8/RembrandtGfx`)를 ESP(`disk0s1`)에 넣고 이전 것을 `config-prev.plist`, `RembrandtGfx.prev`로 백업. 비밀번호를 묻는다 | [커밋 c7cbf04, `scripts/apply-oc-config.command`] |
| 설정 적용 스크립트 2 | `/usr/local/sbin/m8-apply-oc`. 같은 일을 root로 하고 `reboot` 인자를 주면 재부팅. 이 경로 하나만 비밀번호 없이 허용 | 설치 구간 밖(12:15부터 12:29), [커밋 3d5cb6e, `scripts/m8-apply-oc`] |

`m8-apply-oc` 등록 명령(사용자가 직접 실행, 비밀번호 입력 필요)

```bash
ssh -t m8lan "sudo mkdir -p /usr/local/sbin && sudo install -o root -g wheel -m 755 ~/m8/m8-apply-oc /usr/local/sbin/m8-apply-oc \
  && echo '<MAC_USER> ALL=(root) NOPASSWD: /usr/local/sbin/m8-apply-oc' | sudo tee /etc/sudoers.d/m8-apply-oc >/dev/null \
  && sudo chmod 440 /etc/sudoers.d/m8-apply-oc && sudo visudo -cf /etc/sudoers.d/m8-apply-oc"
# 이후 사용: scp 새설정 m8lan:m8/config.plist && ssh m8lan 'sudo /usr/local/sbin/m8-apply-oc reboot'
# 제거:     ssh -t m8lan "sudo rm /etc/sudoers.d/m8-apply-oc /usr/local/sbin/m8-apply-oc"
```

`/usr/local/sbin`이 없어서 첫 등록 시도는 실패했고 `mkdir -p`를 추가해 성공했다. 스크립트 안에 홈 경로가 고정되어 있으므로 계정 이름에 맞게 고쳐야 한다.

자동 로그인을 설정한 기록은 이 세션들에서 찾지 못했다.

## 10. 문제와 해결 요약

| 번호 | 증상 | 진단 방법 | 해결 |
|---|---|---|---|
| 1 | 실기기에서 복구 이미지가 `SLSPackagesInitializeSessionPreferences`에서 멈춤(GPU 드라이버 유무와 무관) | 화면 사진, GPU 끈 비교 설정 | 원인 미확정. QEMU 가상 머신에서 실기기 디스크에 직접 설치 |
| 2 | `macrecovery.py`가 `Inappropriate ioctl for device`로 종료 | 오류 메시지 | `script -q /dev/null python3 macrecovery.py ...` |
| 3 | exFAT인 Ventoy 파티션에서 `fallocate` 실패 | 오류 메시지 | `truncate -s 4G` |
| 4 | Ubuntu 라이브로 `scp`가 `Connection closed` | 재현 | `ssh host 'cat > 파일' < 로컬파일` |
| 5 | `systemctl poweroff`가 데스크톱 세션 잠금에 막힘, 라이브 세션이 종료 직전 Enter를 기다림 | `systemd-inhibit --list` | `systemctl poweroff -i`, `reboot --force -i` |
| 6 | 설치된 macOS가 실기기에서 `IOG flags 0x3` 부근 정지, 로그 없음 | 가상 머신으로 디스크 로그 확인(0바이트) | 7.2부터 7.5의 네 가지를 차례로 해결 |
| 7 | `DevirtualiseMmio`를 끄자 즉시 리셋 | ESP의 OpenCore 로그 타임스탬프 | 다시 켜고 `MmioWhitelist`에 `0xE0000000`, `0xFEE01000` 추가 |
| 8 | 부팅 9.4초에 디스크 쓰기만 멈춤 | RembrandtGfx 디스크 로그와 화면 사진 비교, `nvme id-ctrl` | `NVMeFix.kext` 제거 |
| 9 | 부팅 11.7초 커널 패닉(페이지 폴트) | NVRAM `AAPL,PanicInfo*`를 `efipanic.py`로 해독 | `AppleIntelMCEInterruptController` 매치를 가로채는 plist kext 작성 |
| 10 | 받아 온 `AppleMCEReporterDisabler`가 효과 없음 | `Info.plist`의 `IOKitPersonalities`가 빈 것을 확인 | 설치된 macOS의 실제 퍼스널리티를 보고 새로 작성 |
| 11 | 패닉 없이 화면만 안 나옴, 시스템은 살아 있음 | `ttl_initialize = 1` 로그, 커널 컬렉션 역어셈블로 지원 장치 표 확인 | RembrandtGfx가 PCI 리비전을 `0xC1`로 보고하게 패치 |
| 12 | `efibootmgr -n 0001`이 한 번 `Boot entry 1 does not exist` | 다음 부팅에서 `efibootmgr` 재확인 | 항목은 다시 보였다. 사라졌다면 5.8절 명령으로 재등록. `EFI/BOOT/BOOTx64.efi`가 대체 경로 |
| 13 | `bless --setBoot` 실패(`0xe00002e2`) | 오류 메시지 | `ScanPolicy = 0x80103`과 `Timeout = 5`로 자동 부팅 |
| 14 | QEMU `system_powerdown` 무시 | 화면 캡처 | 게스트 안에서 `sudo shutdown -h now` |
| 15 | Ubuntu와 macOS가 같은 IP라 호스트 키 충돌 | SSH 경고 | 별칭별 `HostKeyAlias` |
| 16 | 통합 로그를 debug로 덤프하니 한 번에 360MB | 파일 크기 | 덤프 방식을 줄였고, 결국 커널 내부 디스크 로그가 유일하게 쓸모 있었다 |

## 11. 겪어 보고 알게 된 것

1. 실기기의 복구 환경은 멈춰도 로그가 남지 않아 원인을 잡을 수 없었다. 설치는 가상 머신으로 하고, 문제는 설치된 시스템에서 디스크 로그로 잡는 쪽이 빨랐다.
2. 가상 머신에서 잘 된다고 실기기에서 되는 것이 아니다. 가상 머신은 디스크를 SATA로, CPU를 Intel로, GPU를 가상 장치로 보여 준다. NVMe, AMD 전용 문제, GPU는 실기기에서 처음 드러난다.
3. 부팅이 멈추면 먼저 "커널이 죽었는지, 디스크 쓰기만 죽었는지"를 구분한다. 화면에는 계속 찍히는데 디스크 로그가 끊기면 저장 장치 문제다. 사용자 영역 진단 데몬은 이 상황에서 아무것도 남기지 못한다.
4. `DevirtualiseMmio`는 켠 채로 두고, 펌웨어의 런타임 MMIO 영역을 `/sys/firmware/efi/runtime-map`에서 읽어 `MmioWhitelist`에 넣는다.
5. `AppleMCEReporterDisabler`는 인터넷에 빈 껍데기 사본이 돌아다닌다. `IOKitPersonalities`가 비어 있으면 아무것도 막지 못한다.
6. `device-id` 위장만으로는 부족하다. Apple의 HWLibs는 PCI 리비전도 표로 확인한다. 660M(리비전 `0x13`)은 거부된다. 다른 Rembrandt 기기도 `lspci`의 리비전을 먼저 확인한다.
7. 재부팅 전에는 항상 가상 머신을 끄고(`pgrep -x qemu-system-x86`로 확인) ESP를 `sync` 후 언마운트한다. 가상 머신과 호스트가 같은 디스크를 동시에 쓰면 파일 시스템이 깨질 수 있다.
8. 디스크 지정은 `by-id` 경로로 한다. 다른 NVMe를 뽑으면 `nvme1n1`이 `nvme0n1`이 된다.
9. 사용권 동의, 계정 비밀번호, 원격 로그인 켜기, sudoers 편집은 사람이 직접 하거나 승인한 뒤에 진행했다.
10. SMBIOS 일련번호, MLB, UUID, ROM은 기기마다 새로 만든다. 이 공개 저장소의 설정에는 자리표시 값이 들어 있다.

## 12. 확실한 것과 불분명한 것

확실한 것(명령과 출력이 기록에 있음)

- Ubuntu 26.04.1 라이브, Ventoy persistence와 자동 SSH 구성, 덤프 명령, 파티션 명령.
- OpenCore 1.0.5와 kext 버전, config 생성 스크립트의 변경 항목, `ocvalidate` 통과.
- macrecovery 보드 ID `Mac-937A206F2EE63C01`과 실행 방식.
- QEMU 명령줄 전체, 실기기 디스크를 raw로 직접 물린 것, `vmctl` 방식.
- 설치된 버전 15.8.2 (24H232), 실기기에서 Metal 지원과 Navi 23 가속기 연결.
- 네 가지 수정의 설정 내용과 적용 순서, 각 수정 뒤 증상이 어떻게 바뀌었는지.
- 리비전 허용 목록(커널 컬렉션에서 직접 덤프)과 패치 내용.

불분명한 것

1. BIOS 설정: 바꾼 기록이 없다. Secure Boot 꺼짐과 SVM 켜짐이 출고 상태인지, 사용자가 이전에 바꿔 둔 것인지 알 수 없다. 부팅 메뉴 키 F7도 안내만 있고 확인 기록은 없다.
2. Ventoy 설치 방법과 버전: 기록에 없다. 이미 만들어져 있던 USB를 썼다.
3. 실기기 복구 이미지 정지의 원인: 확인하지 않고 우회했다. 7절의 문제들(MCE 리포터, MMIO)과 같은 원인이었을 가능성이 있지만 수정 후 복구 이미지를 다시 부팅해 보지 않았다.
4. MMIO 화이트리스트: 추가한 두 항목(`0xE0000000`, `0xFEE01000`) 각각의 필요 여부를 분리 시험하지 않았다. 처음 설정(`0xFD000000`만 있는 상태)에서의 정지가 MMIO 때문이었는지도 직접 증명되지 않았다. 다른 수정들과 겹쳐 있다. 참고로 `0xFD000000` 항목의 주석은 선행 사례에서 온 "AMD FCH AcpiMmio 0xFED80000"인데 주소와 주석이 일치하지 않는다.
5. `Boot0001` 항목이 한 번 "없다"고 나온 이유: 다음 부팅에서 다시 보였다. 세션은 일시적인 NVRAM 읽기 문제로 추정했다. 그 사이 약 7분간 M8의 상태는 확인되지 않았다.
6. 복구 이미지 버전: 세션은 15.4.1이라고 적었지만 화면을 보고 말한 것이고 명령 출력으로 확인한 기록은 없다. 설치 프로그램이 내려받은 최종 버전(15.8.2)만 확실하다.
7. 사용자가 VNC로 계정을 만든 구간(04:12부터 05:41): 무엇을 입력했는지 기록이 없다. 05:08에 세션이 한 번 요약(컨텍스트 압축)되어 그 직전 일부는 요약으로만 남아 있다.
8. 선행 사례에 리비전 패치가 없었던 이유: 세션은 "GPD의 GPU 리비전이 목록에 있어서"라고 추정했지만 확인하지 않았다.
10. `csr-active-config = 03020000` 등 선행 사례에서 그대로 가져온 값들의 필요 여부는 검토하지 않았다.
11. SMBIOS 일련번호가 Apple에 등록되지 않은 값인지 확인한 기록이 없다. Apple 계정 로그인은 건너뛰었다.
12. 본체 RTL8125(LucyRTL8125Ethernet) 동작: 설치 구간에서는 모니터의 USB 랜으로 연결됐다. 07:21 이후 본체 랜 주소로 접속한 기록이 있어 동작하는 것으로 보이지만 범위 밖이다.
13. 자동 로그인: 설정 기록을 찾지 못했다. 다른 세션에서 했을 수 있다.
14. 범위 밖이지만 알려 둘 것: S3 잠자기는 이 BIOS에서 깨어나지 못해 포기했고(`_S3`가 `XS3`로 숨겨져 있음), Wi-Fi와 블루투스(MT7922)는 동작하지 않는다.
