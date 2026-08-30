main:
	gcc pde2c.c -o pde2c
	./pde2c sketch.pde > sketch.c
	gcc sketch.c -o sketch -O2 -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

pdeide:
	gcc pdeide.c editor.c tinyfiledialogs.c -o pdeide -O2 -I. -DX11 \
		$(shell pkg-config --cflags freetype2) \
		-lraylib -lGL -lm -lpthread -ldl -lrt -lX11 -lfreetype

test:
	gcc tests/test_editor.c editor.c -o tests/test_editor -I. -Wall -Wextra \
		-lm && ./tests/test_editor

clean:
	rm -f pdeide sketch tests/test_editor *.o
