// LLM maintained.
//
// One-line sokol port of sketch_261120a_PAL_formats.pde from the
// /home/kof/src/2026/11-listopad sketchbook. The whole drawing is a single
// continuous polyline: the pen never leaves the paper. No raylib, no
// processing.h, no text layer (text would require a pen lift).
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#define SOKOL_IMPL
#define SOKOL_GLCORE
#include "sokol_gfx.h"
#include "sokol_app.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#define SOKOL_GL_IMPL
#include "util/sokol_gl.h"

#include "dh36_path.h"
#include <string.h>

static dh36_path_t path;
static sg_pass_action pass_action;

static void init(void) {
    sg_setup(&(sg_desc){
        .environment = sglue_environment(),
        .logger.func = slog_func,
    });
    sgl_setup(&(sgl_desc_t){ .logger.func = slog_func });
    pass_action = (sg_pass_action){
        .colors[0] = {
            .load_action = SG_LOADACTION_CLEAR,
            .clear_value = { 0.0f, 0.0f, 0.0f, 1.0f },
        },
    };
}

static void frame(void) {
    const float w = (float)sapp_width();
    const float h = (float)sapp_height();

    sg_begin_pass(&(sg_pass){ .action = pass_action, .swapchain = sglue_swapchain() });
    sgl_defaults();
    sgl_ortho(0.0f, w, h, 0.0f, -1.0f, 1.0f);

    // A single line strip: one pen-down stroke for the entire drawing.
    sgl_begin_line_strip();
    sgl_c4f(1.0f, 1.0f, 1.0f, 1.0f);
    for (int i = 0; i < path.n; i++) {
        sgl_v2f_c4f(path.pts[i].x * w / (float)PATH_W,
                    path.pts[i].y * h / (float)PATH_H,
                    1.0f, 1.0f, 1.0f, 1.0f);
    }
    sgl_end();
    sgl_draw();

    sg_end_pass();
    sg_commit();
}

static void cleanup(void) {
    sgl_shutdown();
    sg_shutdown();
}

sapp_desc sokol_main(int argc, char* argv[]) {
    bool half_size = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--half") == 0) {
            half_size = true;
        }
    }
    dh36_build(&path);
    return (sapp_desc){
        .init_cb = init,
        .frame_cb = frame,
        .cleanup_cb = cleanup,
        .width = half_size ? PATH_W : PATH_W * 2,
        .height = half_size ? PATH_H : PATH_H * 2,
        .window_title = "dh36 oneline (sokol)",
        .icon.sokol_default = true,
        .logger.func = slog_func,
    };
}
