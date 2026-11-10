# Cross-build for the M8 (x86_64) on this Mac; copy the results to m8lan:m8/vt/.
CC = clang -target x86_64-apple-macos13 -O1 -Wall -Wno-unused-parameter -Wno-four-char-constants
FW = -framework IOKit -framework CoreFoundation -framework CoreVideo -framework IOSurface -framework CoreMedia -framework VideoToolbox
all: av1dec vtav1test AV1AMDDecoder.bundle
av1dec: av1dec.c av1hdr.c av1hdr.h avdfill.h
	$(CC) -o $@ av1dec.c av1hdr.c $(FW)
vtav1test: vtav1test.m av1vt.c av1hdr.c av1hdr.h avdfill.h
	$(CC) -fobjc-arc -o $@ vtav1test.m av1vt.c av1hdr.c $(FW) -framework Foundation -framework AVFoundation
AV1AMDDecoder.bundle: av1vt.c av1hdr.c av1hdr.h avdfill.h Info.plist
	mkdir -p $@/Contents/MacOS
	$(CC) -bundle -fvisibility=hidden -o $@/Contents/MacOS/AV1AMDDecoder av1vt.c av1hdr.c $(FW)
	cp Info.plist $@/Contents/Info.plist
	codesign -f -s - $@
clean:
	rm -rf av1dec vtav1test AV1AMDDecoder.bundle
