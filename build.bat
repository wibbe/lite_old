@echo off

rem download this:
rem https://nuwen.net/mingw.html

echo compiling (windows)...

windres res.rc -O coff -o res.res
gcc src/*.c src/api/*.c src/lib/lua54/*.c src/lib/stb/*.c^
    -O3 -s -std=gnu11 -fno-strict-aliasing -Isrc -DLUA_USE_POPEN^
    -Iwinlib/SDL2-2.0.10/x86_64-w64-mingw32/include^
    -lmingw32 -lm -lSDL2main -luuid -lole32 -lcomdlg32 -lSDL2 -lSDL2_ttf -Lwinlib/SDL2-2.0.10/x86_64-w64-mingw32/lib^
    -o lite.exe
    rem -mwindows res.res^

echo done
