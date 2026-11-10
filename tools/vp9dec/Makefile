# vp9dump은 이 맥에서 네이티브로 빌드한다(파서 시험용). 헤더들은 M8(x86_64)용 디코더에 그대로 포함해 쓴다.
CC = clang -std=c11 -O1 -Wall -Wextra -Wno-unused-parameter
FFMPEG = /opt/homebrew/bin/ffmpeg
all: vp9dump
vp9dump: vp9dump.c vp9hdr.c vp9hdr.h vp9msg.h vp9_probs.h vcn_vp9.h
	$(CC) -o $@ vp9dump.c vp9hdr.c
# x86_64로도 컴파일되는지(정적 단언 포함) 확인
check-x86: vp9dump.c vp9hdr.c
	clang -target x86_64-apple-macos13 -std=c99 -pedantic -Wall -Wextra -Wno-unused-parameter -Wno-c11-extensions -fsyntax-only vp9dump.c vp9hdr.c
test-vp9.ivf: ../vtprobe/test-vp9.mp4
	$(FFMPEG) -v error -y -i $< -c copy -f ivf $@
# ffmpeg trace_headers와 전 프레임, 전 구문 요소 대조. 결과물 test-trace.txt, test-dump.txt는 폴더에 남는다
test: vp9dump test-vp9.ivf
	$(FFMPEG) -v verbose -i test-vp9.ivf -c copy -bsf:v trace_headers -f null - 2> test-trace.txt
	./vp9dump test-vp9.ivf -t > test-dump.txt
	python3 compare_trace.py test-trace.txt test-dump.txt
clean:
	rm -f vp9dump
