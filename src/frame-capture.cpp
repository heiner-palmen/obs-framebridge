/*===========================================================================
  frame-capture.cpp
===========================================================================*/
#include "frame-capture.hpp"

#include <cstring>
#include <cmath>
#include <algorithm>

#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <graphics/matrix4.h>

// ────────────────────────────────────────────────────────────────────────────
// Construction / destruction
// ────────────────────────────────────────────────────────────────────────────

FrameCapture::FrameCapture() = default;

FrameCapture::~FrameCapture()
{
    // destroy_graphics() must have been called from the graphics thread
    // before the destructor runs.
}

// ────────────────────────────────────────────────────────────────────────────
// Configuration setters/getters
// ────────────────────────────────────────────────────────────────────────────

void FrameCapture::set_source_name(const char *name)
{
    std::lock_guard<std::mutex> lk(cfg_mutex_);
    source_name_ = name ? name : "";
}

void FrameCapture::set_capture_size(uint32_t w, uint32_t h)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    std::lock_guard<std::mutex> lk(cfg_mutex_);
    cfg_width_  = w;
    cfg_height_ = h;
}

void FrameCapture::set_enabled(bool enabled)
{
    std::lock_guard<std::mutex> lk(cfg_mutex_);
    enabled_ = enabled;
}

void FrameCapture::clear_ready_flag()
{
    std::lock_guard<std::mutex> lk(buf_mutex_);
    buf_ready_ = false;
    buf_width_ = 0;
    buf_height_ = 0;
    pixel_buf_.clear();
}

std::string FrameCapture::source_name() const
{
    std::lock_guard<std::mutex> lk(cfg_mutex_);
    return source_name_;
}

uint32_t FrameCapture::capture_width() const
{
    std::lock_guard<std::mutex> lk(cfg_mutex_);
    return cfg_width_;
}

uint32_t FrameCapture::capture_height() const
{
    std::lock_guard<std::mutex> lk(cfg_mutex_);
    return cfg_height_;
}

bool FrameCapture::is_enabled() const
{
    std::lock_guard<std::mutex> lk(cfg_mutex_);
    return enabled_;
}

// ────────────────────────────────────────────────────────────────────────────
// Pixel queries (read side, any thread)
// ────────────────────────────────────────────────────────────────────────────

bool FrameCapture::get_size(uint32_t &out_w, uint32_t &out_h) const
{
    std::lock_guard<std::mutex> lk(buf_mutex_);
    if (!buf_ready_) return false;
    out_w = buf_width_;
    out_h = buf_height_;
    return true;
}

bool FrameCapture::get_buffer(std::vector<uint8_t> &out_rgba,
                              uint32_t &out_w, uint32_t &out_h) const
{
    std::lock_guard<std::mutex> lk(buf_mutex_);
    if (!buf_ready_) return false;
    out_rgba = pixel_buf_;
    out_w    = buf_width_;
    out_h    = buf_height_;
    return true;
}

bool FrameCapture::get_pixel(int x, int y,
                             uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &a) const
{
    std::lock_guard<std::mutex> lk(buf_mutex_);
    if (!buf_ready_ || x < 0 || y < 0
        || (uint32_t)x >= buf_width_ || (uint32_t)y >= buf_height_)
        return false;

    const size_t offset = ((size_t)y * buf_width_ + x) * 4;
    r = pixel_buf_[offset];
    g = pixel_buf_[offset + 1];
    b = pixel_buf_[offset + 2];
    a = pixel_buf_[offset + 3];
    return true;
}

bool FrameCapture::avg_color(int x, int y, int w, int h,
                             uint8_t &r, uint8_t &g, uint8_t &b) const
{
    std::lock_guard<std::mutex> lk(buf_mutex_);
    if (!buf_ready_ || w <= 0 || h <= 0
        || x < 0 || y < 0
        || (uint32_t)(x + w) > buf_width_
        || (uint32_t)(y + h) > buf_height_)
        return false;

    uint64_t sr = 0, sg = 0, sb = 0;
    for (int row = y; row < y + h; ++row) {
        const uint8_t *p = pixel_buf_.data() + ((size_t)row * buf_width_ + x) * 4;
        for (int col = 0; col < w; ++col, p += 4) {
            sr += p[0];
            sg += p[1];
            sb += p[2];
        }
    }
    uint64_t n = (uint64_t)w * h;
    r = (uint8_t)(sr / n);
    g = (uint8_t)(sg / n);
    b = (uint8_t)(sb / n);
    return true;
}

bool FrameCapture::region_matches_color(int x, int y, int w, int h,
                                        uint8_t tr, uint8_t tg, uint8_t tb,
                                        int tolerance) const
{
    std::lock_guard<std::mutex> lk(buf_mutex_);
    if (!buf_ready_ || w <= 0 || h <= 0
        || x < 0 || y < 0
        || (uint32_t)(x + w) > buf_width_
        || (uint32_t)(y + h) > buf_height_)
        return false;

    for (int row = y; row < y + h; ++row) {
        const uint8_t *p = pixel_buf_.data() + ((size_t)row * buf_width_ + x) * 4;
        for (int col = 0; col < w; ++col, p += 4) {
            if (std::abs((int)p[0] - (int)tr) > tolerance) return false;
            if (std::abs((int)p[1] - (int)tg) > tolerance) return false;
            if (std::abs((int)p[2] - (int)tb) > tolerance) return false;
        }
    }
    return true;
}

float FrameCapture::compare_region_mae(int x, int y, int w, int h,
                                       const uint8_t *ref_rgba,
                                       int /*ref_w*/, int /*ref_h*/) const
{
    // ref_rgba is expected to have been scaled to w×h by the caller.
    std::lock_guard<std::mutex> lk(buf_mutex_);
    if (!buf_ready_ || w <= 0 || h <= 0
        || x < 0 || y < 0
        || (uint32_t)(x + w) > buf_width_
        || (uint32_t)(y + h) > buf_height_
        || !ref_rgba)
        return 0.0f;

    uint64_t total_err = 0;
    for (int row = 0; row < h; ++row) {
        const uint8_t *src = pixel_buf_.data()
                             + ((size_t)(y + row) * buf_width_ + x) * 4;
        const uint8_t *ref = ref_rgba + (size_t)row * w * 4;
        for (int col = 0; col < w; ++col, src += 4, ref += 4) {
            total_err += (uint64_t)std::abs((int)src[0] - (int)ref[0]);
            total_err += (uint64_t)std::abs((int)src[1] - (int)ref[1]);
            total_err += (uint64_t)std::abs((int)src[2] - (int)ref[2]);
            // Ignore alpha channel for the similarity score
        }
    }

    // MAE per channel, normalise to [0..1], invert so 1=identical
    double mae  = (double)total_err / ((double)w * h * 3);
    double sim  = 1.0 - (mae / 255.0);
    return (float)std::max(0.0, std::min(1.0, sim));
}

// ────────────────────────────────────────────────────────────────────────────
// Graphics-thread helpers
// ────────────────────────────────────────────────────────────────────────────

void FrameCapture::ensure_resources(uint32_t w, uint32_t h)
{
    // Must be called from the graphics thread (inside obs_enter_graphics).
    if (texrender_ && res_width_ == w && res_height_ == h)
        return;  // already have the right size

    release_resources();
    texrender_    = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    stagesurface_ = gs_stagesurface_create(w, h, GS_RGBA);
    if (!texrender_ || !stagesurface_) {
        blog(LOG_WARNING,
             "[obs-framebridge] Failed to allocate capture resources for %ux%u",
             w, h);
        release_resources();
        return;
    }
    res_width_    = w;
    res_height_   = h;
}

void FrameCapture::release_resources()
{
    // Must be called from the graphics thread.
    if (stagesurface_) {
        gs_stagesurface_destroy(stagesurface_);
        stagesurface_ = nullptr;
    }
    if (texrender_) {
        gs_texrender_destroy(texrender_);
        texrender_ = nullptr;
    }
    res_width_  = 0;
    res_height_ = 0;
    phase_      = CapturePhase::IDLE;
}

void FrameCapture::map_staged_frame(uint32_t w, uint32_t h)
{
    // Must be called from the graphics thread.
    if (!stagesurface_) return;

    uint8_t  *gpu_data  = nullptr;
    uint32_t  linesize  = 0;

    if (!gs_stagesurface_map(stagesurface_, &gpu_data, &linesize))
        return;

    {
        std::lock_guard<std::mutex> lk(buf_mutex_);
        pixel_buf_.resize((size_t)w * h * 4);
        for (uint32_t row = 0; row < h; ++row) {
            memcpy(pixel_buf_.data() + (size_t)row * w * 4,
                   gpu_data            + (size_t)row * linesize,
                   (size_t)w * 4);
        }
        buf_width_  = w;
        buf_height_ = h;
        buf_ready_  = true;
    }

    gs_stagesurface_unmap(stagesurface_);
}

// ────────────────────────────────────────────────────────────────────────────
// Main tick — called from the OBS video/render thread each frame
// ────────────────────────────────────────────────────────────────────────────

void FrameCapture::tick()
{
    // Snapshot config quickly so we don't hold cfg_mutex_ across graphics ops
    std::string sname;
    uint32_t    cap_w, cap_h;
    bool        en;
    {
        std::lock_guard<std::mutex> lk(cfg_mutex_);
        sname = source_name_;
        cap_w = cfg_width_;
        cap_h = cfg_height_;
        en    = enabled_;
    }

    if (!en || sname.empty()) {
        if (texrender_) {
            obs_enter_graphics();
            release_resources();
            obs_leave_graphics();
        }
        clear_ready_flag();
        return;
    }

    obs_source_t *source = obs_get_source_by_name(sname.c_str());
    if (!source) return;

    obs_enter_graphics();

    ensure_resources(cap_w, cap_h);
    if (!texrender_ || !stagesurface_) {
        obs_leave_graphics();
        obs_source_release(source);
        clear_ready_flag();
        return;
    }

    // ── Phase MAP: consume the staging surface staged in the previous tick ──
    if (phase_ == CapturePhase::STAGED) {
        map_staged_frame(cap_w, cap_h);
    }

    // ── Phase RENDER: render source → texrender → stage for next tick ─────
    gs_texrender_reset(texrender_);
    bool rendered = false;

    if (gs_texrender_begin(texrender_, cap_w, cap_h)) {
        struct vec4 bg;
        vec4_zero(&bg);
        gs_clear(GS_CLEAR_COLOR, &bg, 0.0f, 0);

        // Orthographic projection that maps source pixels into cap_w × cap_h
        uint32_t src_w = obs_source_get_width(source);
        uint32_t src_h = obs_source_get_height(source);
        if (src_w == 0) src_w = cap_w;
        if (src_h == 0) src_h = cap_h;

        gs_ortho(0.0f, (float)src_w, 0.0f, (float)src_h, -100.0f, 100.0f);
        gs_set_viewport(0, 0, (int)cap_w, (int)cap_h);

        gs_blend_state_push();
        gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
        obs_source_video_render(source);
        gs_blend_state_pop();

        gs_texrender_end(texrender_);
        rendered = true;
    }

    // Stage the rendered texture so it can be mapped to CPU on the next tick
    if (rendered) {
        gs_texture_t *texture = gs_texrender_get_texture(texrender_);
        if (texture) {
            gs_stage_texture(stagesurface_, texture);
            phase_ = CapturePhase::STAGED;
        } else {
            phase_ = CapturePhase::IDLE;
            clear_ready_flag();
        }
    } else {
        phase_ = CapturePhase::IDLE;
    }

    obs_leave_graphics();
    obs_source_release(source);
}

// ────────────────────────────────────────────────────────────────────────────
// Cleanup — must be called from the graphics thread before destruction
// ────────────────────────────────────────────────────────────────────────────

void FrameCapture::destroy_graphics()
{
    obs_enter_graphics();
    release_resources();
    obs_leave_graphics();
}
