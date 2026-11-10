# VCN 3.1.2 켜기: H.264, HEVC 하드웨어 디코딩

Rembrandt(Radeon 660M)의 VCN을 macOS의 Navi 드라이버(AMDRadeonX6000, HWLibs)로 돌리기까지 알아낸 것이다. 구현은 `kext/vcn.cpp`에 있고, 이 문서는 그 배경과 레지스터 지도, 헛다리, 디버깅 기법을 모았다.

출처 표시: "대조함"은 이 문서를 쓰면서 `kext/vcn.cpp`나 패치, 설정 파일과 맞춰 본 것이다. "원 세션 보고"는 작업한 세션이 기억에서 옮겨 준 값으로, 이 문서를 쓸 때 로그와 대조하지 못했다.

## 1. 레지스터 지도 (MMHUB 2.3 Rembrandt 대 2.0 Navi 23)

모든 주소는 dword 단위 MMIO 오프셋이다.

| 항목 | 값 | 출처 |
|---|---|---|
| MMHUB 2.3 기준 | 0x1A000 | 대조함 |
| IP discovery가 알려 주는 MMHUB 기준 | 0x13200 | 대조함 (`vcn.cpp` 주석) |
| 컨텍스트 n의 페이지 테이블 base, start, end | 0x940 + 8n (+0, +2, +4, 각각 lo와 hi) | 대조함 |
| 컨텍스트 n의 CNTL | 0x740 + n | 대조함 |
| 무효화 엔진 e의 SEM, REQ, ACK | 0xa00 + 8e, +1, +2 | 대조함 |
| L2 보호 폴트 STATUS, ADDR_LO, ADDR_HI | 0x70c, 0x70d, 0x70e | 대조함 |
| 폴트 해제 CNTL | 0x708 (0x709가 아님) | 대조함 |
| MMMC_VM_MX_L1_TLB_CNTL | 0x8f3 (절대 0x1a8f3) | 대조함 |
| GC hub 기준 (GC seg0) | 0x1260 | 대조함 |
| GC 컨텍스트 v의 CNTL, base, start, end | 0x15fc + v, 0x1667 + 2v, 0x1687 + 2v, 0x16a7 + 2v | 대조함 |

참고: 무효화 엔진의 SEM 레지스터는 **읽기만 해도 세마포어를 잡는다.** 그래서 상태 덤프는 REQ와 ACK만 읽는다.

### 가속기의 fillVMRegisters

IP discovery가 알려 주는 0x13200에 가속기의 `AMDMMHub_2_x::fillVMRegisters`가 MMHUB 2.0 오프셋을 더해 쓴다. Rembrandt는 MMHUB 2.3이라 그 주소가 다른 레지스터이고, SDMA의 VMPT 작업이 무효화 응답을 기다리다 끝나지 않고 멈췄다.

- 이 함수가 채우는 `AMD_GFX10_VM_REGS_PER_HUB` 구조체는 0x358바이트다: 전역 0x00부터 0x2c, 컨텍스트 16개 x 0x1c(0x30부터), 무효화 엔진 18개 x 0x14(0x1f0부터). (대조함)
- 가속기 클래스 네 개(`AMDMMHub_2_0_0`, `2_0_2`, `2_1_0`, `2_1_1`)의 이 함수를 훅해, 채워진 모든 항목을 이름 기준으로 2.0에서 2.3 주소로 바꾼다. 기준값은 `p[0x30/4] - 0x72b`다. (대조함)
- 변환된 항목은 213개이고 0x873 하나가 남았다. (원 세션 보고)

### 검은 화면의 원인

`MMMC_VM_MX_L1_TLB_CNTL`의 bit 6(`ENABLE_ADVANCED_DRIVER_MODEL`)이 꺼져 있었다. HWLibs의 MMHUB 2.3 초기화가 0x501로 두고, 가속기가 나중에 0x1d19로 다시 쓴다. 0x1d59여야 한다. 이 비트가 없으면 MM hub가 VCN의 메모리 접근을 전부 VMID 0으로 변환하고, VCN 펌웨어가 디코드 IB(사용자 VMID의 주소)를 읽다 폴트가 난다. 디코딩은 멈추지 않지만 출력이 검다.

- 로그에는 `mmfault ... vmid 0`과 주소 `0_400010000`이 찍혔다. (원 세션 보고)
- `mmhub_2_3_0_initialize_registers` 훅과 매 프레임 제출 때 비트를 켠다. `-rmbnommadm`으로 끈다. (대조함)
- Linux는 `mmhub_v2_3_init_tlb_regs`에서 이 비트를 켠다.

## 2. VCN 켜기 세부

| 항목 | 내용 | 출처 |
|---|---|---|
| 전원 | SMU 메시지 `PowerUpVcn` 0x07, `PowerUpJpeg` 0x22를 `smu_hw_init`에서 보낸다 | 대조함 |
| 펌웨어 로드 | PSP가 로드하고 DPG 모드로 돈다. SRAM은 PSP로 올린다(`psp_np_fw_update_vcn_sram`, type 0x1c) | 원 세션 보고 |
| 펌웨어 교체 | HWLibs에는 Navi의 VCN 3.0 이미지(`ativvaxy_vcn3.dat`)만 있다. Rembrandt용 VCN 3.1.2 이미지(linux-firmware의 `vcn_3_1_2.bin`에서 0x100바이트 헤더를 뗀 것)를 그 자리에 넣고 이름을 `ativvaxy_vcn3_1.dat`로 바꾼다(`wrapCreateFw`). 기종별 펌웨어 목록이 가득 차 있어 항목을 추가하는 방식은 실패했다 | 대조함 |

디코드 링의 PACKET0 내부 오프셋(대조함):

| 이름 | 오프셋 |
|---|---|
| data0, data1 | 0x10, 0x11 |
| cmd | 0xf |
| IB_VMID | 0x431 |
| IB_BAR_LOW, IB_BAR_HIGH | 0x3b4, 0x3b5 |
| IB_SIZE | 0x25c |
| NOP | 0x29 |
| CONTEXT_ID | 0x27 (원 세션 보고) |

## 3. 헛다리

다음 사람이 시간을 아끼도록 적는다.

- **HWLibs의 MMHUB 레지스터 표 교체**: 표를 2.3 주소로 바꿨지만 VMPT 경로에는 쓰이지 않았다. 실제 원인은 가속기의 `fillVMRegisters`였다.
- **CPU에서 GC 페이지 테이블을 MM hub로 복사**: MM hub의 VMID와 GC hub의 VMID는 따로 할당된다. GC의 VMID n을 MM의 VMID n에 복사하면 VCN 클라이언트의 페이지 테이블을 덮어쓴다. 기본으로 꺼 두었다(`-rmbmmmirror`로만 켠다).
- **Linux가 DPG 목록에 넣는 추가 레지스터**(`UVD_REG_XX_MASK`, `UVD_RBC_XX_IB_REG_CHECK`, `UVD_RB_ARB_CTRL`): 디코딩에는 필요 없었다. `-rmbxxmask`로만 켠다.
- **완료 인터럽트**: VCN의 디코딩 완료 인터럽트가 가속기에 도착하지 않는 것으로 보인다. 우회로 1ms 폴링(`stampPoll`)을 넣었고 근본 원인은 모른다.

배경으로, H.264와 HEVC가 된 뒤에도 Safari의 유튜브 4K는 VP9로 와서 소프트웨어로 디코딩됐다(WebKit GPU 프로세스 CPU 약 250%, 원 세션 보고). 이것이 AV1 작업을 시작한 이유다.

## 4. 디버깅 기법

- **레지스터 추적 (`-rmbvcntrace`)**: HWLibs의 `internal_cgs_write_register`, `internal_cgs_read_register`와 VCN, 큐, 엔진, MMHUB 초기화 함수 40여 개를 훅해 레지스터 접근을 기록한다. 절대 주소는 `off + *(*(ctx + 0x10) + 0x34 + seg * 4)`다.
- **한 번만 켜는 모드 (`-rmbvcnarm`)**: 부팅 때는 VCN을 숨기고, `~/m8/vcn-once` 파일에 내용이 있으면 그때 한 번 켠 뒤 파일을 비운다. 멈춰도 다음 부팅은 정상이다. 커널에서 파일을 비울 때 `O_TRUNC`가 통하지 않아 `vnode_setattr`로 `va_data_size = 0`을 썼다.
- **실험 스위치와 레지스터 찌르기**: arm 모드에서 `~/m8/vcn-mode`(16진 비트 스위치)와 `~/m8/vcn-poke`(`r ADDR [COUNT]`, `w ADDR VALUE`)를 감시 스레드가 1초마다 읽는다. 재부팅 없이 실험 조건을 바꾸고 레지스터를 읽고 쓸 수 있다.
- **멈춤 덤프 (`vcnHangDump`)**: 채널 재시작 훅에서 SDMA 페이지 큐, MMHUB 무효화 엔진(REQ와 ACK만), L2 폴트 상태를 덤프한다. SEM은 읽으면 세마포어가 잡혀서 뺐다.
- **GART 읽기 (`gartRead`)**: VMID 0은 깊이 0의 평평한 테이블이다. 여러 단계 페이지 워크로 읽으면 매핑되지 않은 것으로 나온다. 색인을 `page - start`로 잡아 직접 읽는다.
- **출력 검증 (`vtprobe check`)**: 같은 프레임을 하드웨어와 소프트웨어로 디코딩해 밝기 차이를 내고 `hw.png`, `sw.png`를 쓴다. 최종 결과는 두 코덱 모두 300프레임 평균 차이 0.000, 동시 디코드 4개 통과, QuickTime의 4K60 HEVC 재생 CPU 약 6%였다. (원 세션 보고)

## 5. 복구와 함정

- **GPU가 멈춘 상태에서 웜 리부트하면 다음 부팅이 꼬인다.** WindowServer 워치독 패닉(약 2분)을 기다리면 깨끗이 재부팅된다.
- **Lilu로 같은 함수를 두 번 라우팅하면 패닉이 나고 부팅이 반복된다.**
- **익명 네임스페이스 안의 심볼을 다른 번역 단위에서 참조하면** 커널 확장에 미정의 심볼로 남아 로드되지 않고, 사과 로고에서 멈춘다. 빌드할 때마다 확인한다.

```bash
nm -u RembrandtGfx | grep "^__Z" | c++filt
```

- **부팅 실패 시**: Ubuntu 라이브 USB로 부팅해 SSH로 접속하고, ESP(이 기기에서는 `nvme0n1p1`)를 마운트해 `EFI/OC/RembrandtGfx.prev`와 `config-prev.plist`로 되돌린다. 그 환경에서 `scp`가 되지 않아 `ssh ... 'cat > file' < local` 방식으로 파일을 보냈다.
- **OpenCore의 `UEFI > Output > Resolution`을 `Max`로** 해야 부팅 화면과 로그인 창이 4K로 나온다. `configs/config.plist`에 반영돼 있다. (대조함)
- **sudo**: 설치 스크립트 하나만 비밀번호 없이 허용해 썼다. 되돌리려면 `/etc/sudoers.d/m8-apply-oc`와 `/usr/local/sbin/m8-apply-oc`를 지우면 된다.
