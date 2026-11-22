// LLM maintained.
// Headless SVG exporter: no sokol_app, no X11, no GPU.
#include "dh36_path.h"

int main(int argc, char* argv[]) {
    const char* out = (argc > 1) ? argv[1] : "dh36-oneline.svg";
    dh36_path_t path;
    dh36_build(&path);
    dh36_write_svg(&path, out);
    return 0;
}
