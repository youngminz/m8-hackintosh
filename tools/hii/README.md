# AMI 설정 양식(HII) 덤프와 파서

이 기기의 BIOS(AMI Aptio 5.24, `M8 V1.04`)에서 메뉴에 보이지 않는 설정 항목을 찾을 때 쓴 도구다. 숨겨진 `S3/Modern Standby Support` 항목을 이렇게 찾았다(단계 5 참고).

## 덤프

AMI 펌웨어는 설정 양식(HII)을 메모리에 내보내고 NVRAM 변수 `1B838190-4625-4EAD-ABC9-CD5E6AF18FE0:HiiDB`에 크기(4바이트)와 주소(4바이트)를 둔다. macOS에서 `sudo nvram -x <변수>`로 읽어 주소를 얻는다.

RembrandtGfx(`kext/`)에 물리 메모리 덤프 기능이 있다. `~/m8/physdump-req`에 "주소 길이"를 16진으로 쓰고 재부팅하면, 가속기 로드 20초 뒤 그 영역을 `~/m8/physdump.bin`에 쓴다.

## 파서

```
python3 ifr.py hiidb.bin > ifr.txt
```

양식, 변수 저장소, 항목(이름, 변수, 오프셋, 선택지, 기본값), 숨김 조건을 들여쓰기로 출력한다. 이 기기에서 뽑은 결과 예시가 `ifr.txt`다(양식 6개: AMD PBS, AMD CBS, Setup 등).

## 찾은 것

`AMD PBS` → `Power Saving Configurations` → `S3/Modern Standby Support`. 변수 `A339D746-F678-49B3-9FC7-54CE0F9DF226:AMD_PBS_SETUP`(0x500바이트)의 오프셋 0x39: 0은 S3 Enable, 1은 Modern Standby Enable(기본값).

macOS에서 변수 전체를 `%xx` 형식으로 되써서 한 바이트만 바꿀 수 있다. 0으로 바꾸면 펌웨어가 `_S3`를 노출하고 FADT의 Low Power S0 Idle을 끈다(바뀐 뒤 물리 메모리의 ACPI 표를 떠서 대조했다). 그래도 S3 복귀는 macOS와 Ubuntu 모두에서 실패했다 — 펌웨어 문제다(단계 5).
