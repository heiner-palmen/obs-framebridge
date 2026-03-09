#pragma once
/*===========================================================================
  screenshot.hpp — write the current framebuffer to a PNG file
===========================================================================*/

#include <cstdint>
#include <vector>
#include <string>

// Write `width × height` RGBA pixel data to `path` as a PNG.
// Returns true on success, false on any I/O or encoding error.
bool fc_write_png(const std::string &path,
                  const std::vector<uint8_t> &rgba,
                  uint32_t width, uint32_t height);

// Render a named OBS scene/source on-demand (even if it is not currently
// displayed), capture the result and write it to `path` as a PNG.
//
// `width` and `height` set the render resolution; pass 0 for either to
// use the source's own dimensions.
//
// Must be called from a thread that is allowed to enter the OBS graphics
// context (i.e. any thread — obs_enter_graphics() handles the handoff).
// Returns true on success.
bool fc_render_scene_png(const std::string &scene_name,
                         const std::string &path,
                         uint32_t width, uint32_t height);

