# LLM maintained.
# Standalone one-line dh36 demo; sokol headers come from ../../../sokol.
CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra
SOKOL := ../../../sokol
INCLUDES := -I$(SOKOL) -I$(SOKOL)/util
LIBS := -lGL -lX11 -lXi -lXcursor -ldl -lm -lpthread

all: dh36_sokol_demo dh36_svg_export

dh36_sokol_demo: dh36_sokol_demo.c dh36_path.h
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@ $(LIBS)

dh36_svg_export: dh36_svg_export.c dh36_path.h
	$(CC) $(CFLAGS) $< -o $@ -lm

svg: dh36_svg_export
	./dh36_svg_export dh36-oneline.svg

run: dh36_sokol_demo
	DISPLAY=:0 ./dh36_sokol_demo --half

clean:
	rm -f dh36_sokol_demo dh36_svg_export

.PHONY: all svg run clean
