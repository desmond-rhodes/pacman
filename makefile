pacman.exe: main.cc | .setup
	x86_64-w64-mingw32-g++-posix -std=c++17 -Iglfw/include -Igl3w/include -Ilibpng -Izlib $^ gl3w/src/gl3w.c -Lglfw/build/src -Llibpng -Lzlib -lglfw3 -lpng -lz -lgdi32 -static-libgcc -static-libstdc++ -static -lpthread -o $@

.PHONY: run
run: pacman.exe
	./$<

.PHONY: force
force: clean pacman.exe

CC := x86_64-w64-mingw32-gcc-posix
AR := x86_64-w64-mingw32-ar
RANLIB := x86_64-w64-mingw32-ranlib

.setup: glfw gl3w zlib libpng
	cd gl3w && python3 gl3w_gen.py
	cmake -S glfw -B glfw/build -D CMAKE_TOOLCHAIN_FILE=CMake/x86_64-w64-mingw32.cmake
	make -C glfw/build glfw --no-print-directory
	cd zlib && ./configure --static
	sed -i 's`CC=.*`CC=$(CC)`; s`AR=.*`AR=$(AR)`; s`RANLIB=.*`RANLIB=$(RANLIB)`' zlib/Makefile
	make -C zlib libz.a --no-print-directory
	cp libpng/scripts/makefile.std libpng/makefile
	sed -i 's`CC =.*`CC=$(CC)`; s`AR_RC =.*`AR_RC=$(AR) rc`; s`RANLIB =.*`RANLIB=$(RANLIB)`' libpng/makefile
	touch libpng/pnglibconf.dfn
	cp libpng/scripts/pnglibconf.h.prebuilt libpng/pnglibconf.h
	make -C libpng libpng.a --no-print-directory
	touch .setup

.PHONY: clean
clean:
	rm -f pacman.exe

.PHONY: reset-setup
reset-setup:
	git submodule foreach --recursive git clean -ffdx
	rm -f .setup
