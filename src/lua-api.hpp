#pragma once
/*===========================================================================
  lua-api.hpp — Lua/proc_handler bridge
  ---------------------------------------------------------------------------
  Registers calldata procedures on the global OBS proc_handler so that Lua
  scripts can call them via obs.proc_handler_call().

  Lua usage pattern:
  ──────────────────
    local cd = obs.calldata_create()
    obs.calldata_set_int(cd, "x", 100)
    obs.calldata_set_int(cd, "y", 200)
    obs.proc_handler_call(obs.obs_get_proc_handler(), "framebuffer_get_pixel", cd)
    local r = obs.calldata_int(cd, "r")
    obs.calldata_destroy(cd)
===========================================================================*/

#include "frame-capture.hpp"
#include "reference-image.hpp"

// Register all framebuffer_* procedures on the global proc_handler.
void lua_api_register(FrameCapture *fc, ReferenceStore *rs);

// Unregister all framebuffer_* procedures.
void lua_api_unregister();
