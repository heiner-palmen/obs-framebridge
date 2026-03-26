/*===========================================================================
  lua-api.cpp — register framebuffer_* procedures on the global proc_handler
  ---------------------------------------------------------------------------
  Lua usage (from CloneHeroDrummingAutomationV2.lua or any other script):

    local function fb_get_pixel(x, y)
        local cd = obs.calldata_create()
        obs.calldata_set_int(cd, "x", x)
        obs.calldata_set_int(cd, "y", y)
        obs.proc_handler_call(obs.obs_get_proc_handler(), "framebuffer_get_pixel", cd)
        local r = obs.calldata_int(cd, "r")
        local g = obs.calldata_int(cd, "g")
        local b = obs.calldata_int(cd, "b")
        local a = obs.calldata_int(cd, "a")
        obs.calldata_destroy(cd)
        return r, g, b, a
    end
===========================================================================*/

#include "lua-api.hpp"
#include "screenshot.hpp"

#include <obs-module.h>
#include <callback/proc.h>
#include <callback/calldata.h>

#include <string>
#include <cstring>
#include <vector>

// Pointers set during registration, used by all callbacks
static FrameCapture  *g_fc = nullptr;
static ReferenceStore *g_rs = nullptr;

static bool capture_available()
{
    return g_fc != nullptr;
}

static bool refs_available()
{
    return g_rs != nullptr;
}

// ────────────────────────────────────────────────────────────────────────────
// Individual procedure implementations
// ────────────────────────────────────────────────────────────────────────────

// framebuffer_get_pixel(in int x, in int y)
//   → out int r, g, b, a   (-1 on error)
static void proc_get_pixel(void * /*data*/, calldata_t *cd)
{
    if (!capture_available()) {
        calldata_set_int(cd, "r", -1LL);
        calldata_set_int(cd, "g", -1LL);
        calldata_set_int(cd, "b", -1LL);
        calldata_set_int(cd, "a", -1LL);
        return;
    }

    int x = (int)calldata_int(cd, "x");
    int y = (int)calldata_int(cd, "y");

    uint8_t r = 0, g = 0, b = 0, a = 0;
    bool ok = g_fc->get_pixel(x, y, r, g, b, a);

    calldata_set_int(cd, "r", ok ? (long long)r : -1LL);
    calldata_set_int(cd, "g", ok ? (long long)g : -1LL);
    calldata_set_int(cd, "b", ok ? (long long)b : -1LL);
    calldata_set_int(cd, "a", ok ? (long long)a : -1LL);
}

// framebuffer_get_size()
//   → out int width, height
static void proc_get_size(void * /*data*/, calldata_t *cd)
{
    if (!capture_available()) {
        calldata_set_int(cd, "width", 0LL);
        calldata_set_int(cd, "height", 0LL);
        return;
    }

    uint32_t w = 0, h = 0;
    g_fc->get_size(w, h);
    calldata_set_int(cd, "width",  (long long)w);
    calldata_set_int(cd, "height", (long long)h);
}

// framebuffer_avg_color(in int x, in int y, in int w, in int h)
//   → out int r, g, b    (-1 on error)
static void proc_avg_color(void * /*data*/, calldata_t *cd)
{
    if (!capture_available()) {
        calldata_set_int(cd, "r", -1LL);
        calldata_set_int(cd, "g", -1LL);
        calldata_set_int(cd, "b", -1LL);
        return;
    }

    int x = (int)calldata_int(cd, "x");
    int y = (int)calldata_int(cd, "y");
    int w = (int)calldata_int(cd, "w");
    int h = (int)calldata_int(cd, "h");

    uint8_t r = 0, g = 0, b = 0;
    bool ok = g_fc->avg_color(x, y, w, h, r, g, b);

    calldata_set_int(cd, "r", ok ? (long long)r : -1LL);
    calldata_set_int(cd, "g", ok ? (long long)g : -1LL);
    calldata_set_int(cd, "b", ok ? (long long)b : -1LL);
}

// framebuffer_region_matches_color(in int x, in int y, in int w, in int h,
//                                  in int tr, in int tg, in int tb,
//                                  in int tolerance)
//   → out int matches   (1=yes, 0=no)
static void proc_region_matches_color(void * /*data*/, calldata_t *cd)
{
    if (!capture_available()) {
        calldata_set_int(cd, "matches", 0LL);
        return;
    }

    int x   = (int)calldata_int(cd, "x");
    int y   = (int)calldata_int(cd, "y");
    int w   = (int)calldata_int(cd, "w");
    int h   = (int)calldata_int(cd, "h");
    int tr  = (int)calldata_int(cd, "tr");
    int tg  = (int)calldata_int(cd, "tg");
    int tb  = (int)calldata_int(cd, "tb");
    int tol = (int)calldata_int(cd, "tolerance");

    bool ok = g_fc->region_matches_color(
        x, y, w, h,
        (uint8_t)std::min(255, std::max(0, tr)),
        (uint8_t)std::min(255, std::max(0, tg)),
        (uint8_t)std::min(255, std::max(0, tb)),
        tol);

    calldata_set_int(cd, "matches", ok ? 1LL : 0LL);
}

// framebuffer_compare_region(in int x, in int y, in int w, in int h,
//                             in string ref_id)
//   → out float similarity   (0.0 … 1.0;  -1.0 on error)
static void proc_compare_region(void * /*data*/, calldata_t *cd)
{
    if (!capture_available() || !refs_available()) {
        calldata_set_float(cd, "similarity", -1.0);
        return;
    }

    int x = (int)calldata_int(cd, "x");
    int y = (int)calldata_int(cd, "y");
    int w = (int)calldata_int(cd, "w");
    int h = (int)calldata_int(cd, "h");

    const char *ref_id = calldata_string(cd, "ref_id");
    if (!ref_id || w <= 0 || h <= 0) {
        calldata_set_float(cd, "similarity", -1.0);
        return;
    }

    const RefImage *ref = g_rs->get(ref_id);
    if (!ref || !ref->valid()) {
        calldata_set_float(cd, "similarity", -1.0);
        return;
    }

    // Scale the reference to the target region size if needed
    RefImage scaled;
    const uint8_t *ref_data = nullptr;
    if (ref->width == w && ref->height == h) {
        ref_data = ref->rgba.data();
    } else {
        scaled   = ReferenceStore::scale(*ref, w, h);
        ref_data = scaled.rgba.data();
    }

    float sim = g_fc->compare_region_mae(x, y, w, h, ref_data, w, h);
    calldata_set_float(cd, "similarity", (double)sim);
}

// framebuffer_load_reference(in string id, in string path)
//   → out int success   (1=ok, 0=fail)
static void proc_load_reference(void * /*data*/, calldata_t *cd)
{
    if (!refs_available()) {
        calldata_set_int(cd, "success", 0LL);
        return;
    }

    const char *id   = calldata_string(cd, "id");
    const char *path = calldata_string(cd, "path");

    bool ok = (id && path) && g_rs->load(id, path);
    calldata_set_int(cd, "success", ok ? 1LL : 0LL);
}

// framebuffer_unload_reference(in string id)
static void proc_unload_reference(void * /*data*/, calldata_t *cd)
{
    if (!refs_available()) return;

    const char *id = calldata_string(cd, "id");
    if (id) g_rs->unload(id);
}

// framebuffer_set_source(in string source_name)
static void proc_set_source(void * /*data*/, calldata_t *cd)
{
    if (!capture_available()) return;

    const char *name = calldata_string(cd, "source_name");
    g_fc->set_source_name(name ? name : "");
    blog(LOG_INFO, "[obs-framebridge] Capture source set to: '%s'",
         name ? name : "(none)");
}

// framebuffer_set_size(in int width, in int height)
static void proc_set_size(void * /*data*/, calldata_t *cd)
{
    if (!capture_available()) return;

    int w = (int)calldata_int(cd, "width");
    int h = (int)calldata_int(cd, "height");
    if (w > 0 && h > 0) {
        g_fc->set_capture_size((uint32_t)w, (uint32_t)h);
        blog(LOG_INFO, "[obs-framebridge] Capture size set to %d×%d", w, h);
    }
}

// framebuffer_set_enabled(in int enabled)   (1=on, 0=off)
static void proc_set_enabled(void * /*data*/, calldata_t *cd)
{
    if (!capture_available()) return;

    bool en = calldata_int(cd, "enabled") != 0;
    g_fc->set_enabled(en);
    blog(LOG_INFO, "[obs-framebridge] Capture %s",
         en ? "enabled" : "disabled");
}

// framebuffer_is_ready()
//   → out int ready   (1=yes, 0=no)
static void proc_is_ready(void * /*data*/, calldata_t *cd)
{
    if (!capture_available()) {
        calldata_set_int(cd, "ready", 0LL);
        return;
    }

    uint32_t w = 0, h = 0;
    bool rdy = g_fc->get_size(w, h);
    calldata_set_int(cd, "ready", rdy ? 1LL : 0LL);
}

// framebuffer_render_scene(in string scene_name, in string path,
//                           in int width, in int height)
//   → out int success   (1=ok, 0=fail)
//
// Renders a named OBS scene/source on-demand (even if not displayed) and
// writes the result to `path` as a PNG.  Pass width=0 / height=0 to use
// the scene's own dimensions.
static void proc_render_scene(void * /*data*/, calldata_t *cd)
{
    if (!capture_available()) {
        calldata_set_int(cd, "success", 0LL);
        return;
    }

    const char *scene_name = calldata_string(cd, "scene_name");
    const char *path       = calldata_string(cd, "path");
    int w = (int)calldata_int(cd, "width");
    int h = (int)calldata_int(cd, "height");

    if (!scene_name || !path || scene_name[0] == '\0' || path[0] == '\0') {
        calldata_set_int(cd, "success", 0LL);
        return;
    }

    bool ok = fc_render_scene_png(
        std::string(scene_name),
        std::string(path),
        (uint32_t)std::max(0, w),
        (uint32_t)std::max(0, h));

    calldata_set_int(cd, "success", ok ? 1LL : 0LL);
}

// framebuffer_screenshot(in string path)
//   → out int success   (1=ok, 0=fail)
//
// Captures the current framebuffer and writes it to `path` as a PNG.
// The caller is responsible for ensuring the directory exists.
static void proc_screenshot(void * /*data*/, calldata_t *cd)
{
    if (!capture_available()) {
        calldata_set_int(cd, "success", 0LL);
        return;
    }

    const char *path = calldata_string(cd, "path");
    if (!path || path[0] == '\0') {
        calldata_set_int(cd, "success", 0LL);
        return;
    }

    std::vector<uint8_t> rgba;
    uint32_t w = 0, h = 0;
    bool ok = g_fc->get_buffer(rgba, w, h);
    if (ok) {
        ok = fc_write_png(std::string(path), rgba, w, h);
    } else {
        blog(LOG_WARNING,
             "[obs-framebridge] screenshot: no frame captured yet");
    }

    calldata_set_int(cd, "success", ok ? 1LL : 0LL);
}

// ────────────────────────────────────────────────────────────────────────────
// Register / Unregister
// ────────────────────────────────────────────────────────────────────────────

void lua_api_register(FrameCapture *fc, ReferenceStore *rs)
{
    g_fc = fc;
    g_rs = rs;

    proc_handler_t *ph = obs_get_proc_handler();

    proc_handler_add(ph,
        "void framebuffer_get_pixel(int x; int y; out int r; out int g; out int b; out int a)",
        proc_get_pixel, nullptr);

    proc_handler_add(ph,
        "void framebuffer_get_size(out int width; out int height)",
        proc_get_size, nullptr);

    proc_handler_add(ph,
        "void framebuffer_avg_color(int x; int y; int w; int h; out int r; out int g; out int b)",
        proc_avg_color, nullptr);

    proc_handler_add(ph,
        "void framebuffer_region_matches_color(int x; int y; int w; int h; int tr; int tg; int tb; int tolerance; out int matches)",
        proc_region_matches_color, nullptr);

    proc_handler_add(ph,
        "void framebuffer_compare_region(int x; int y; int w; int h; string ref_id; out float similarity)",
        proc_compare_region, nullptr);

    proc_handler_add(ph,
        "void framebuffer_load_reference(string id; string path; out int success)",
        proc_load_reference, nullptr);

    proc_handler_add(ph,
        "void framebuffer_unload_reference(string id)",
        proc_unload_reference, nullptr);

    proc_handler_add(ph,
        "void framebuffer_set_source(string source_name)",
        proc_set_source, nullptr);

    proc_handler_add(ph,
        "void framebuffer_set_size(int width; int height)",
        proc_set_size, nullptr);

    proc_handler_add(ph,
        "void framebuffer_set_enabled(int enabled)",
        proc_set_enabled, nullptr);

    proc_handler_add(ph,
        "void framebuffer_is_ready(out int ready)",
        proc_is_ready, nullptr);

    proc_handler_add(ph,
        "void framebuffer_screenshot(string path; out int success)",
        proc_screenshot, nullptr);

    proc_handler_add(ph,
        "void framebuffer_render_scene(string scene_name; string path; int width; int height; out int success)",
        proc_render_scene, nullptr);

    blog(LOG_INFO,
         "[obs-framebridge] Lua API registered (13 procedures)");
}

void lua_api_unregister()
{
    // OBS does not provide individual proc_handler_remove; removing the
    // module clears its contribution.  Just null out our pointers so any
    // concurrent in-flight call safely fails (OBS won't call into freed
    // memory as the module is still resident until obs_module_unload returns).
    g_fc = nullptr;
    g_rs = nullptr;
}
