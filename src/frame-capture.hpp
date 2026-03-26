#pragma once
/*===========================================================================
  frame-capture.hpp — FrameCapture
  ---------------------------------------------------------------------------
  Manages per-frame GPU→CPU pixel capture of a named OBS source or scene.

  Thread model
  ────────────
  • write side  : OBS video/render thread (tick callback)
  • read side   : any thread (Lua scripting, UI)
  A mutex guards access to the pixel buffer and configuration.
===========================================================================*/

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

#include <obs-module.h>
#include <graphics/graphics.h>

enum class CapturePhase { IDLE, STAGED };

class FrameCapture {
public:
    FrameCapture();
    ~FrameCapture();

    // ── Configuration (thread-safe) ──────────────────────────────────────
    void set_source_name(const char *name);
    void set_capture_size(uint32_t width, uint32_t height);
    void set_enabled(bool enabled);

    std::string source_name() const;
    uint32_t    capture_width()  const;
    uint32_t    capture_height() const;
    bool        is_enabled()     const;

    // ── Pixel queries (thread-safe, operate on last complete frame) ───────
    bool get_size(uint32_t &out_w, uint32_t &out_h) const;

    // Returns false if (x,y) is out of bounds or no frame is available yet.
    bool get_pixel(int x, int y,
                   uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &a) const;

    // Average colour of a rectangular region.
    bool avg_color(int x, int y, int w, int h,
                   uint8_t &r, uint8_t &g, uint8_t &b) const;

    // True if every pixel in region is within `tolerance` of (tr,tg,tb).
    bool region_matches_color(int x, int y, int w, int h,
                              uint8_t tr, uint8_t tg, uint8_t tb,
                              int tolerance) const;

    // Mean-Absolute-Error similarity [0=no match … 1=identical] against
    // a reference RGBA block (already scaled to w×h by the caller).
    float compare_region_mae(int x, int y, int w, int h,
                             const uint8_t *ref_rgba, int ref_w, int ref_h) const;

    // Copy the whole RGBA pixel buffer to out_rgba.
    // Returns false if no frame is available yet.
    bool get_buffer(std::vector<uint8_t> &out_rgba,
                    uint32_t &out_w, uint32_t &out_h) const;

    // ── Called from the video/render tick callback ────────────────────────
    void tick();

    // ── Called from obs_module_unload (graphics thread) ──────────────────
    void destroy_graphics();

private:
    // Configuration — protected by cfg_mutex_
    mutable std::mutex cfg_mutex_;
    std::string  source_name_;
    uint32_t     cfg_width_  = 1920;
    uint32_t     cfg_height_ = 1080;
    bool         enabled_    = true;

    // Pixel buffer — protected by buf_mutex_
    mutable std::mutex   buf_mutex_;
    std::vector<uint8_t> pixel_buf_;   // RGBA, row-major
    uint32_t             buf_width_  = 0;
    uint32_t             buf_height_ = 0;
    bool                 buf_ready_  = false;

    // Graphics resources — only accessed from the graphics thread
    gs_texrender_t  *texrender_    = nullptr;
    gs_stagesurf_t  *stagesurface_ = nullptr;
    uint32_t          res_width_     = 0;
    uint32_t          res_height_    = 0;
    CapturePhase      phase_         = CapturePhase::IDLE;

    void ensure_resources(uint32_t w, uint32_t h);
    void release_resources();
    void map_staged_frame(uint32_t w, uint32_t h);
    void clear_ready_flag();
};
