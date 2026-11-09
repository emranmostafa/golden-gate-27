#!/usr/bin/env bash
# Cross-compile GoldenGate.exe for Windows x64 with mingw-w64.
set -e
CXX=x86_64-w64-mingw32-g++-posix
WINDRES=x86_64-w64-mingw32-windres
OUT=$(readlink -f "${1:-build}")
mkdir -p "$OUT"
FLAGS="-std=c++17 -municode -DUNICODE -D_UNICODE -I src -O2 \
  -Wall -Wno-unknown-pragmas -Wno-unused-parameter -Wno-unused-variable \
  -Wno-unused-but-set-variable -Wno-sign-compare -ffunction-sections -fdata-sections"
SRC="util gfx theme apps menu menubar dock taskbar fan controlcenter calendar overlays launchpad extras settings main"
OBJ=""
for s in $SRC; do
  $CXX $FLAGS -c src/$s.cpp -o "$OUT/$s.o"
  OBJ="$OBJ $OUT/$s.o"
done
( cd src && $WINDRES app.rc -O coff -o "$OUT/app.res" )
LIBS="-ld2d1 -ldwrite -lwindowscodecs -ldwmapi -lole32 -loleaut32 -lshell32 \
  -lshlwapi -luuid -lgdi32 -luser32 -ladvapi32 -lpropsys -lversion -lpowrprof \
  -lcomctl32 -lwtsapi32 -lruntimeobject -lwbemuuid"
$CXX $FLAGS -mwindows $OBJ "$OUT/app.res" -o "$OUT/GoldenGate.exe" \
  -Wl,--gc-sections -static -static-libgcc -static-libstdc++ $LIBS
echo "built $OUT/GoldenGate.exe"
ls -la "$OUT/GoldenGate.exe"
