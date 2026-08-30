main:
	gcc pde2c.c -o pde2c
	./pde2c sketch.pde > sketch.c
	gcc sketch.c -o sketch -O2 -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

# Static dependencies for the IDE's Export (self-contained binary) feature.
static: static-raylib static-freetype
static-raylib:
	cmake -S third_party/raylib -B build/static/raylib -G Ninja \
		-DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release \
		-DBUILD_EXAMPLES=OFF -DGLFW_BUILD_WAYLAND=OFF \
		-DCMAKE_C_FLAGS="-fPIC"
	cmake --build build/static/raylib
static-freetype:
	cmake -S third_party/freetype -B build/static/freetype -G Ninja \
		-DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_C_FLAGS="-fPIC" -DFT_DISABLE_HARFBUZZ=TRUE \
		-DFT_DISABLE_BROTLI=TRUE -DFT_DISABLE_BZIP2=TRUE \
		-DFT_DISABLE_PNG=TRUE -DFT_DISABLE_ZLIB=TRUE
	cmake --build build/static/freetype

pdeide:
	gcc pdeide.c editor.c tinyfiledialogs.c -o pdeide -O2 -I. -DX11 \
		$(shell pkg-config --cflags freetype2) \
		-lraylib -lGL -lm -lpthread -ldl -lrt -lX11 -lfreetype

test:
	gcc tests/test_editor.c editor.c -o tests/test_editor -I. -Wall -Wextra \
		-lm && ./tests/test_editor

clean:
	rm -f pdeide sketch tests/test_editor *.o
