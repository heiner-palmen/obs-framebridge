/*===========================================================================
  screenshot.cpp — PNG writing via stb_image_write
  ---------------------------------------------------------------------------
  stb_image_write.h must be present under third_party/stb/.
  If it is missing, run:
    curl -Lo third_party/stb/stb_image_write.h \
         https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h
  or retrieve it from https://github.com/nothings/stb
===========================================================================*/

// Compile the stb_image_write implementation exactly ONCE here.
// Suppress upstream warnings that are harmless but noisy.
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#  pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

#include "screenshot.hpp"

#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>

#include <vector>
#include <cstring>

bool fc_write_png(const std::string &path,
                  const std::vector<uint8_t> &rgba,
                  uint32_t width, uint32_t height)
{
    if (rgba.empty() || width == 0 || height == 0) {
        blog(LOG_WARNING, "[obs-framebridge] screenshot: empty frame, nothing written");
        return false;
    }

    // stbi_write_png expects: (path, w, h, channels, data, stride_in_bytes)
    int stride = (int)(width * 4);
    int result = stbi_write_png(path.c_str(),
                                (int)width, (int)height,
                                4,              // RGBA
                                rgba.data(),
                                stride);

    if (result == 0) {
        blog(LOG_WARNING,
             "[obs-framebridge] screenshot: stbi_write_png failed for '%s'",
             path.c_str());
        return false;
    }

    blog(LOG_INFO,
         "[obs-framebridge] screenshot saved: %s (%u×%u)",
         path.c_str(), width, height);
    return true;
}

// ── On-demand scene render ────────────────────────────────────────────────────

bool fc_render_scene_png(const std::string &scene_name,
                         const std::string &path,
                         uint32_t width, uint32_t height)
{
    if (scene_name.empty() || path.empty()) {
        blog(LOG_WARNING,
             "[obs-framebridge] render_scene_png: scene_name or path is empty");
        return false;
    }

    obs_source_t *source = obs_get_source_by_name(scene_name.c_str());
    if (!source) {
        blog(LOG_WARNING,
             "[obs-framebridge] render_scene_png: source not found: '%s'",
             scene_name.c_str());
        return false;
    }

    // Use source's own dimensions when caller passes 0.
    if (width  == 0) width  = obs_source_get_width(source);
    if (height == 0) height = obs_source_get_height(source);
    if (width  == 0) width  = 1920;   // absolute fallback
    if (height == 0) height = 1080;

    obs_enter_graphics();

    gs_texrender_t  *tr   = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    gs_stagesurf_t  *surf = gs_stagesurface_create(width, height, GS_RGBA);

    gs_texrender_reset(tr);
    bool rendered = false;

    if (gs_texrender_begin(tr, width, height)) {
        struct vec4 clear_color;
        vec4_zero(&clear_color);
        gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);

        uint32_t src_w = obs_source_get_width(source);
        uint32_t src_h = obs_source_get_height(source);
        if (src_w == 0) src_w = width;
        if (src_h == 0) src_h = height;

        gs_ortho(0.0f, (float)src_w, 0.0f, (float)src_h, -100.0f, 100.0f);
        gs_set_viewport(0, 0, (int)width, (int)height);

        gs_blend_state_push();
        gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

        obs_source_video_render(source);

        gs_blend_state_pop();
        gs_texrender_end(tr);
        rendered = true;
    }

    std::vector<uint8_t> pixels;
    bool mapped = false;

    if (rendered) {
        gs_texture_t *tex = gs_texrender_get_texture(tr);
        gs_stage_texture(surf, tex);

        // Flush so the GPU has finished writing before we map.
        gs_flush();

        uint8_t  *data     = nullptr;
        uint32_t  linesize = 0;
        if (gs_stagesurface_map(surf, &data, &linesize)) {
            pixels.resize((size_t)width * height * 4);
            for (uint32_t row = 0; row < height; ++row) {
                memcpy(pixels.data() + (size_t)row * width * 4,
                       data          + (size_t)row * linesize,
                       (size_t)width * 4);
            }
            gs_stagesurface_unmap(surf);
            mapped = true;
        } else {
            blog(LOG_WARNING,
                 "[obs-framebridge] render_scene_png: failed to map stage surface");
        }
    }

    gs_stagesurface_destroy(surf);
    gs_texrender_destroy(tr);

    obs_leave_graphics();
    obs_source_release(source);

    if (!mapped) return false;

    return fc_write_png(path, pixels, width, height);
}
