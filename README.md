# obs-framebridge

> **A clean, first-class pixel bridge between OBS Studio scenes and Lua automation scripts.**

---

## The Problem — and Why This Plugin Exists

OBS Studio is a powerful recording and streaming tool, but it has always had a blind spot: **you cannot easily inspect what is happening *inside* a scene that is not currently on-air**.

Developers who needed this — for example, to automate scene switching based on in-game state — were forced into creative workarounds: spawning hidden projector windows, abusing studio-mode transitions, or relying on fragile screen-grab APIs outside of OBS entirely. These tricks worked — barely — but they were brittle, resolution-dependent, tightly coupled to the desktop layout, and completely undocumented.

**obs-framebridge was built to replace all of that.**

It hooks directly into OBS's render pipeline and exposes a clean, thread-safe, Lua-callable API that lets any script **read pixel data from any source or scene** — whether it is currently visible on the stream or not. No window hacks, no external screen capture, no undocumented internals.

---

## What It Does

```
 OBS Render Thread          obs-framebridge               Your Lua Script
 ─────────────────          ───────────────               ───────────────
 [Scene / Source]  ──GPU──▶  FrameCapture  ──CPU buffer──▶  get_pixel()
                            (every frame)                   avg_color()
                                                            compare_region()
                                                            → switch scene ✓
```

Every frame, the plugin captures the GPU output of a configured OBS source into a CPU-side RGBA pixel buffer. Your Lua scripts can then interrogate this buffer at any time through OBS's built-in `proc_handler` interface — no custom Lua bindings, no native module loading.

---

## Features

| Capability | Description |
|---|---|
| **Per-pixel queries** | Read the exact RGBA value at any coordinate |
| **Region average colour** | Average colour of an arbitrary rectangle |
| **Colour matching** | Check whether a region matches a target colour within a tolerance |
| **Template matching** | Compare a region against a reference image (PNG/JPEG) using MAE similarity |
| **Reference image store** | Load images from disk by ID; reuse across multiple comparisons |
| **On-demand screenshots** | Render *any* scene to a PNG — even if it is not currently active |
| **Thread-safe** | Pixel buffer is protected by a mutex; safe to query from any thread |
| **Pure Lua integration** | Uses OBS's standard `proc_handler` — works in any OBS Lua script |

---

## Lua API Reference

All procedures are called via `obs.proc_handler_call(obs.obs_get_proc_handler(), "<name>", cd)`.

### Quick helper pattern

```lua
local function fb_call(name, inputs, outputs)
    local cd = obs.calldata_create()
    for k, v in pairs(inputs) do
        if type(v) == "number" then obs.calldata_set_int(cd, k, v)
        elseif type(v) == "string" then obs.calldata_set_string(cd, k, v) end
    end
    obs.proc_handler_call(obs.obs_get_proc_handler(), name, cd)
    local result = {}
    for _, k in ipairs(outputs) do result[k] = obs.calldata_int(cd, k) end
    obs.calldata_destroy(cd)
    return result
end
```

### Procedures

#### `framebuffer_is_ready`
Returns `ready = 1` when the plugin is loaded and the buffer is active.

#### `framebuffer_get_pixel`
| Input | Type | Description |
|---|---|---|
| `x`, `y` | int | Pixel coordinate |

| Output | Type | Description |
|---|---|---|
| `r`, `g`, `b`, `a` | int | Channel values 0–255, or -1 on error |

#### `framebuffer_get_size`
| Output | Type | Description |
|---|---|---|
| `width`, `height` | int | Current capture dimensions |

#### `framebuffer_avg_color`
| Input | Type | Description |
|---|---|---|
| `x`, `y`, `w`, `h` | int | Region rectangle |

| Output | Type | Description |
|---|---|---|
| `r`, `g`, `b` | int | Average channel values, or -1 on error |

#### `framebuffer_region_matches_color`
| Input | Type | Description |
|---|---|---|
| `x`, `y`, `w`, `h` | int | Region rectangle |
| `tr`, `tg`, `tb` | int | Target RGB colour |
| `tolerance` | int | Per-channel tolerance (0–255) |

| Output | Type |
|---|---|
| `matches` | int — `1` yes / `0` no |

#### `framebuffer_compare_region`
Compares a live region against a pre-loaded reference image using Mean Absolute Error.

| Input | Type | Description |
|---|---|---|
| `x`, `y`, `w`, `h` | int | Region in the live framebuffer |
| `ref_id` | string | ID of a previously loaded reference image |

| Output | Type | Description |
|---|---|---|
| `similarity` | float | `1.0` = identical, `0.0` = completely different, `-1.0` = error |

#### `framebuffer_load_reference`
| Input | Type | Description |
|---|---|---|
| `id` | string | Arbitrary key for this image |
| `path` | string | Absolute path to a PNG or JPEG file |

| Output | Type |
|---|---|
| `success` | int — `1` ok / `0` fail |

#### `framebuffer_take_screenshot`
Renders a named OBS scene on demand and saves the result as a PNG — even if the scene is not currently live.

| Input | Type | Description |
|---|---|---|
| `scene_name` | string | Scene to render |
| `path` | string | Output file path (`.png`) |
| `width`, `height` | int | Render resolution; `0` = use scene's native size |

---

## Example: Automated Scene Switching

The following snippet reads the average luminance of a region in a game capture scene and automatically switches OBS scenes based on thresholds — detecting, for example, a pause menu versus active gameplay versus a loading screen.

```lua
obs = obslua

local PROBE = { x=1280, y=810, w=320, h=270 }

local function avg_luminance()
    local cd = obs.calldata_create()
    obs.calldata_set_int(cd, "x", PROBE.x)
    obs.calldata_set_int(cd, "y", PROBE.y)
    obs.calldata_set_int(cd, "w", PROBE.w)
    obs.calldata_set_int(cd, "h", PROBE.h)
    obs.proc_handler_call(obs.obs_get_proc_handler(), "framebuffer_avg_color", cd)
    local r = obs.calldata_int(cd, "r")
    local g = obs.calldata_int(cd, "g")
    local b = obs.calldata_int(cd, "b")
    obs.calldata_destroy(cd)
    if r < 0 then return nil end
    return 0.299 * r + 0.587 * g + 0.114 * b   -- perceived luminance
end

local function switch_to(scene_name)
    local src = obs.obs_get_source_by_name(scene_name)
    if src then
        obs.obs_frontend_set_current_scene(src)
        obs.obs_source_release(src)
    end
end

local g_timer = nil

function script_load(settings)
    g_timer = obs.timer_add(function()
        local lum = avg_luminance()
        if not lum then return end
        if     lum > 42.5 then switch_to("MenuScene")
        elseif lum > 34.0 then switch_to("PlayingScene")
        else                    switch_to("PauseScene")
        end
    end, 250)   -- poll every 250 ms
end

function script_unload()
    if g_timer then obs.timer_remove(g_timer) end
end
```

---

## Building

### Prerequisites

- OBS Studio **32.0+** (headers included in `build/_obs_src/`)
- CMake **3.16+**
- MSVC 2022 (Windows) or GCC/Clang (Linux/macOS)

### Windows (Visual Studio)

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The built plugin DLL will be in `build/Release/`.  Copy it (along with `data/`) into your OBS plugins directory:

```
C:\Program Files\obs-studio\obs-plugins\64bit\obs-framebridge.dll
C:\Program Files\obs-studio\data\obs-plugins\obs-framebridge\
```

### Linux

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

---

## Installation

1. Copy `obs-framebridge.dll` (Windows) or `obs-framebridge.so` (Linux) into your OBS `obs-plugins/64bit/` folder.
2. Copy the `data/locale/` directory into OBS's `data/obs-plugins/obs-framebridge/` folder.
3. Restart OBS.
4. Add or load any Lua script that calls the `framebuffer_*` procedures.

---

## Project Structure

```
src/
  plugin-main.cpp      — OBS module entry, tick callback, settings panel
  frame-capture.cpp/h  — GPU→CPU per-frame pixel capture
  lua-api.cpp/h        — proc_handler registration and procedure implementations
  reference-image.cpp/h— stb_image-powered reference image store
  screenshot.cpp/h     — On-demand scene → PNG rendering
test/
  test_obs-framebridge_loads.lua        — Verify the plugin is visible to Lua
  test_obs-framebridge_scene_switch.lua — Scene switching demo with probe UI
  test_obs-framebridge_screenshot.lua   — On-demand scene screenshot tool
third_party/
  stb/                 — stb_image / stb_image_write (single-header libraries)
data/locale/
  en-US.ini            — Localisation strings
```

---

## License

[MIT](LICENSE)
