# M8 USB 오디오 팝핑 조사 노트 (진행 중, 2026-11-10)

## 확인된 것
- 시계 속도 정상: M8 monotonic 대 로컬 60초 비교 오차 1.6ppm. TSC 주파수 문제 아님.
- USB 프레임 번호 정상: IOUSBLib GetBusFrameNumber로 XHC3에서 3초간 1ms마다 +1, 건너뜀 없음, 호출 약 2us.
- 커널 로그: `AppleUSBAudio ... (Output Fell Behind) AppleUSBAudioDevice.cpp line 6837`이 257ms 간격(3번 나고 1번 쉬는 약 1028ms 주기)으로 반복. 모니터 허브(XHC3 0x6013), 본체 직결(XHC0 0x801, XHC1 0x811) 전부 동일. 컨트롤러와 포트 무관.
- coreaudiod: `HALS_IOA1Engine::EndWriting ... 0xE00002EE(kIOReturnIsoTooOld)`, `Out of bounds host time ... ZTS Host Time: 18446744073708884950`.
- IOAudioEngine 상태 버퍼를 유저랜드에서 직접 읽으면 fCurrentLoopCount가 0에 고정, fLastLoopTime이 0, -333us, -667us만 반복. 즉 스트림이 256ms마다 재시작되고 첫 타임스탬프가 0으로 나감(앵커 2개 미만이면 getTimeForFrameNumber가 0 반환).
- Fell Behind 판정 위치: `AppleUSBAudioStream::queueOutputFrames` (다음 예약 프레임 [this+0x168] - 현재 프레임 < 임계값이면 device+0x201 플래그).
- 모니터 허브 쪽 Gaming Keyboard(258a:010c, 0x6011)가 수 초마다 transaction error 뒤 재열거됨(별개 증상일 수 있음).
- USB 맵 없음, XhciPortLimit 꺼짐. 로드된 타사 kext: Lilu, RestrictEvents, RembrandtGfx, AmdTscSync, VirtualSMC, LucyRTL8125Ethernet.

## 도구 (세션 스크래치패드, M8의 /tmp에도 복사)
- engstat: AppleUSBAudioEngine 상태 버퍼 폴링. frm: 지정 벤더 USB 장치의 버스 프레임 번호 폴링.
- SystemKernelExtensions.kc에서 AppleUSBAudio를 capstone으로 디스어셈블(심볼 있음).

## 아직 모르는 것
- 왜 예약 프레임이 현재 프레임을 못 앞서는지(아이소크로너스 완료 지연인지, 시작 프레임 계산인지). 스피커가 뽑혀 있어 재현 대기.

## 15:50 갱신: 원인 실험으로 확인
- AppleUSBXHCI는 인터럽터 3개(primary 0, io 1, isochronous 2)를 반드시 쓰고(start에서 3개 미만이면 실패), AppleUSBXHCIPCI::start는 MSI 벡터 1개에만 이벤트 소스를 건다. 하나의 인터럽트 필터가 세 인터럽터를 모두 훑는다.
- M8의 AMD xHCI(1022:161d/161e/161f/15d6/15d7)는 MSI 벡터 1개 상태에서 인터럽터 2의 이벤트로는 인터럽트를 올리지 않는 것으로 보임. 아이소크로너스 완료가 다른 인터럽트(같은 컨트롤러의 제어/인터럽트 전송)에 얹혀서만 처리됨.
- 증거: 재생 중 같은 컨트롤러(XHC3)의 허브에 GET_STATUS를 초당 1.5만 번 보내면(/tmp/poke 0424 N) Fell Behind가 8초 21회에서 12초 2회로 줄고 IOAudioEngine 루프 간격이 341.3ms로 정상화. 본체 직결 포트가 무음인 것도 그 컨트롤러에 다른 장치가 없어서로 설명됨.
- 대응안: configs/config-usbintx.plist (config-vcn + Kernel Patch 1개). AppleUSBXHCIPCI::start의 첫 getInterruptType(0) 뒤 `0f 85 8b 02 00 00`(jne)를 `90 e9 8b 02 00 00`(jmp)로 바꿔 인덱스 0(레거시 핀 인터럽트)으로 이벤트 소스를 만들게 함. Darwin 24.x 한정(MinKernel 24.0.0, MaxKernel 24.99.99), KC 전체에서 Find 1회 일치 확인. 아직 미설치, 미검증.
- 위험: 핀 인터럽트가 안 되면 USB 전체가 죽음. SSH는 내장 RTL8125(en0)라 유지되므로 config-vcn으로 되돌릴 수 있음.
- 참고: 스피커가 한동안 열거 안 된 것은 스피커 USB 칩이 멈춘 것이었고 전원 재투입으로 복구됨.

## 15:56 갱신: 해결 확인
- config-usbintx.plist 설치 후 재부팅. USB 전부 정상 열거, XHC 장치들에서 IOPCIMSIMode 속성 사라짐(핀 인터럽트 사용).
- 모니터 허브 연결 상태로 10초 재생: Fell Behind 0회, coreaudiod 오류 0건, 엔진 루프 간격 341.33ms. 사용자도 "많이 괜찮아진 듯" 확인.
- 키보드 재열거도 부팅 후 5분간 부팅 시 1회뿐(이전에는 수 초마다).
- 미검증: 본체 직결 포트 재생, 잠자기 깨어난 뒤 동작. 정식 설정을 config-usbintx로 바꿀지는 미정. M8 ESP에는 현재 config-usbintx가 설치돼 있음.
- m8-hackintosh 커밋: config-usbintx.plist 추가.
