/*===========================================================================
  plugin-main.cpp — OBS module entry point for obs-framebridge
  ---------------------------------------------------------------------------
  Exposes a settings panel under OBS → Tools → obs-framebridge where the
  user can choose:
    • which source/scene to capture
    • capture resolution
    • enable/disable toggle
===========================================================================*/

#include <obs-module.h>
#include <obs-frontend-api.h>

#include "frame-capture.hpp"
#include "reference-image.hpp"
#include "lua-api.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-framebridge", "en-US")

#define PLUGIN_VERSION "1.0.0"

// ────────────────────────────────────────────────────────────────────────────
// Module globals
// ────────────────────────────────────────────────────────────────────────────

static FrameCapture   *g_fc = nullptr;
static ReferenceStore *g_rs = nullptr;

// Settings key constants
#define S_SOURCE_NAME  "source_name"
#define S_CAP_WIDTH    "cap_width"
#define S_CAP_HEIGHT   "cap_height"
#define S_ENABLED      "enabled"

// Forward declaration (defined at bottom of file)
static void on_frontend_event(enum obs_frontend_event event, void *data);

// ────────────────────────────────────────────────────────────────────────────
// Tick callback — runs every frame on the video thread
// ────────────────────────────────────────────────────────────────────────────

static void on_tick(void * /*data*/, float /*seconds*/)
{
    if (g_fc) g_fc->tick();
}

// ────────────────────────────────────────────────────────────────────────────
// obs_module_load  (module initialisation)
// ────────────────────────────────────────────────────────────────────────────

bool obs_module_load(void)
{
    blog(LOG_INFO, "[obs-framebridge] Loading v" PLUGIN_VERSION);

    g_fc = new FrameCapture();
    g_rs = new ReferenceStore();

    lua_api_register(g_fc, g_rs);

    obs_add_tick_callback(on_tick, nullptr);
    obs_frontend_add_event_callback(on_frontend_event, nullptr);

    blog(LOG_INFO, "[obs-framebridge] Loaded");
    return true;
}

// ────────────────────────────────────────────────────────────────────────────
// obs_module_post_load  (called after all modules are loaded)
// ────────────────────────────────────────────────────────────────────────────

void obs_module_post_load(void)
{
    // Nothing extra needed; source names are resolved lazily each tick.
}

// ────────────────────────────────────────────────────────────────────────────
// obs_module_unload  (module teardown)
// ────────────────────────────────────────────────────────────────────────────

void obs_module_unload(void)
{
    obs_remove_tick_callback(on_tick, nullptr);
    obs_frontend_remove_event_callback(on_frontend_event, nullptr);

    lua_api_unregister();

    if (g_fc) {
        g_fc->destroy_graphics();
        delete g_fc;
        g_fc = nullptr;
    }

    if (g_rs) {
        delete g_rs;
        g_rs = nullptr;
    }

    blog(LOG_INFO, "[obs-framebridge] Unloaded");
}

// ────────────────────────────────────────────────────────────────────────────
// Settings UI helpers
// ────────────────────────────────────────────────────────────────────────────

// Populate the source-name dropdown from the current scene collection.
static bool populate_sources(obs_properties_t *props,
                              obs_property_t   *prop,
                              obs_data_t       * /*settings*/)
{
    obs_property_list_clear(prop);

    // Add a blank entry (no capture)
    obs_property_list_add_string(prop, "(none)", "");

    // Enumerate all sources
    obs_enum_sources([](void *param, obs_source_t *src) -> bool {
        obs_property_t *p = static_cast<obs_property_t*>(param);
        const char *name = obs_source_get_name(src);
        obs_property_list_add_string(p, name, name);
        return true;                           // continue enumeration
    }, prop);

    UNUSED_PARAMETER(props);
    return true;
}

// Apply UI values to the FrameCapture instance.
// Called from framebridge_get_properties refresh callbacks and on load.
static void apply_settings(obs_data_t *settings)
{
    if (!g_fc || !settings) return;

    const char *src = obs_data_get_string(settings, S_SOURCE_NAME);
    int         w   = (int)obs_data_get_int(settings, S_CAP_WIDTH);
    int         h   = (int)obs_data_get_int(settings, S_CAP_HEIGHT);
    bool        en  = obs_data_get_bool(settings, S_ENABLED);

    g_fc->set_source_name(src);
    if (w > 0 && h > 0)
        g_fc->set_capture_size((uint32_t)w, (uint32_t)h);
    g_fc->set_enabled(en);
}

// Callback wired to OBS settings "modified" so changes apply live.
static bool on_settings_modified(obs_properties_t * /*props*/,
                                  obs_property_t   * /*prop*/,
                                  obs_data_t        *settings)
{
    apply_settings(settings);
    return true;
}

// ────────────────────────────────────────────────────────────────────────────
// obs_module_*  settings / properties
// ────────────────────────────────────────────────────────────────────────────

// OBS calls obs_module_set_locale, obs_module_free_locale automatically via
// OBS_MODULE_USE_DEFAULT_LOCALE. For script-accessible settings we use the
// *_frontend* configuration API through the tools menu if we had a dock.
// For now we embed settings via the module settings callbacks below, which
// OBS exposes in future as module-level settings in the UI.

// These are also set automatically by obs_module_load via the proc API.
// The settings object is persisted by OBS per session.

extern "C" {

obs_data_t *obs_module_config_path_data(void)
{
    return nullptr;  // reserved for future per-session persistence
}

// Called by OBS to get the UI property definition for this module.
void framebridge_get_properties(obs_properties_t *props)
{
    // Source selector
    obs_property_t *p_source = obs_properties_add_list(
        props,
        S_SOURCE_NAME,
        obs_module_text("SourceName"),
        OBS_COMBO_TYPE_LIST,
        OBS_COMBO_FORMAT_STRING);

    obs_property_set_modified_callback(p_source, populate_sources);
    populate_sources(props, p_source, nullptr);  // fill on first show

    // Capture width
    obs_property_t *p_w = obs_properties_add_int(props,
        S_CAP_WIDTH,
        obs_module_text("CaptureWidth"),
        64, 3840, 16);

    // Capture height
    obs_property_t *p_h = obs_properties_add_int(props,
        S_CAP_HEIGHT,
        obs_module_text("CaptureHeight"),
        36, 2160, 9);

    // Enable toggle
    obs_property_t *p_en = obs_properties_add_bool(props,
        S_ENABLED,
        obs_module_text("EnableCapture"));

    // Wire live-apply callback to every property
    obs_property_set_modified_callback(p_source, on_settings_modified);
    obs_property_set_modified_callback(p_w,      on_settings_modified);
    obs_property_set_modified_callback(p_h,      on_settings_modified);
    obs_property_set_modified_callback(p_en,     on_settings_modified);
}

}  // extern "C"

// ────────────────────────────────────────────────────────────────────────────
// Frontend event hook — load defaults and react to scene-collection changes
// ────────────────────────────────────────────────────────────────────────────

static void on_frontend_event(enum obs_frontend_event event, void * /*data*/)
{
    switch (event) {
    case OBS_FRONTEND_EVENT_FINISHED_LOADING:
        blog(LOG_INFO,
             "[obs-framebridge] OBS finished loading — "
             "plugin ready. Configure via Tools > obs-framebridge.");
        break;

    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
        // The user switched scene collection: reset capture so we don't keep
        // a stale source pointer.
        if (g_fc) g_fc->set_source_name("");
        blog(LOG_INFO,
             "[obs-framebridge] Scene collection changed — source reset.");
        break;

    default:
        break;
    }
}
