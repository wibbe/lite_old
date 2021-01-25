@echo off

rem download this:
rem https://nuwen.net/mingw.html

echo compiling (windows)...

windres res.rc -O coff -o res.res
gcc src/*.c src/api/*.c src/lib/lua52/*.c src/lib/stb/*.c^
    -O3 -s -std=gnu11 -fno-strict-aliasing -Isrc -DLUA_USE_POPEN^
    -lmingw32 -lm -luser32 -lgdi32 -lopengl32 -lole32^
    -o lite.exe

    rem -mwindows res.res^

echo done
