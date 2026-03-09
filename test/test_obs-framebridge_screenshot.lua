obs = obslua

-- ---------------------------------------------------------------------------
-- test_obs-framebridge_screenshot.lua
-- Renders a named OBS scene on-demand and saves it as a PNG.
-- Works even when the scene is not currently displayed/active.
--
-- Setup:
--   1.  Add this script in OBS → Tools → Scripts.
--   2.  Pick the Scene to capture from the drop-down.
--   3.  Set the Output path (e.g. /tmp/my-scene.png).
--   4.  Optionally override Width / Height (leave 0 to use the scene's
--       own resolution).
--   5.  Click "Take Screenshot".
-- ---------------------------------------------------------------------------

local KEY_SCENE  = "scene_name"
local KEY_PATH   = "output_path"
local KEY_WIDTH  = "render_width"
local KEY_HEIGHT = "render_height"

local g_settings = nil

-- ── Script metadata ──────────────────────────────────────────────────────────

function script_description()
  return [[<h3>obs-framebridge Scene Screenshot</h3>
Renders any named OBS scene on-demand (even if not currently visible) and
saves the result as a PNG via the <em>obs-framebridge</em> plug-in.]]
end

function script_defaults(settings)
  obs.obs_data_set_default_string(settings, KEY_PATH,   "/tmp/framebridge-scene.png")
  obs.obs_data_set_default_int(   settings, KEY_WIDTH,  0)
  obs.obs_data_set_default_int(   settings, KEY_HEIGHT, 0)
end

function script_properties()
  local props = obs.obs_properties_create()

  -- Scene selector: lists all scenes in the current OBS session.
  local scene_list = obs.obs_properties_add_list(
    props, KEY_SCENE, "Scene to capture",
    obs.OBS_COMBO_TYPE_LIST, obs.OBS_COMBO_FORMAT_STRING)

  local scenes = obs.obs_frontend_get_scenes()
  if scenes then
    for _, s in ipairs(scenes) do
      local name = obs.obs_source_get_name(s)
      obs.obs_property_list_add_string(scene_list, name, name)
    end
    obs.source_list_release(scenes)
  end

  obs.obs_properties_add_path(
    props, KEY_PATH, "Output path (.png)",
    obs.OBS_PATH_FILE_SAVE,
    "PNG files (*.png);;All files (*)",
    "/tmp/framebridge-scene.png")

  obs.obs_properties_add_int(props, KEY_WIDTH,  "Width  (0 = scene native)", 0, 7680, 1)
  obs.obs_properties_add_int(props, KEY_HEIGHT, "Height (0 = scene native)", 0, 4320, 1)

  obs.obs_properties_add_button(props, "btn_shot", "Take Screenshot",
    function()
      local scene  = obs.obs_data_get_string(g_settings, KEY_SCENE)
      local path   = obs.obs_data_get_string(g_settings, KEY_PATH)
      local width  = obs.obs_data_get_int(g_settings,    KEY_WIDTH)
      local height = obs.obs_data_get_int(g_settings,    KEY_HEIGHT)
      render_scene_screenshot(scene, path, width, height)
    end)

  return props
end

function script_update(settings)
  g_settings = settings
end

function script_load(settings)
  g_settings = settings
end

-- ── Core ─────────────────────────────────────────────────────────────────────

-- render_scene_screenshot(scene_name, path [, width [, height]])
-- Renders `scene_name` (even if not active) and writes a PNG to `path`.
-- width / height default to 0 = use the scene's own resolution.
-- Returns true on success.
function render_scene_screenshot(scene_name, path, width, height)
  width  = width  or 0
  height = height or 0

  if not scene_name or scene_name == "" then
    obs.script_log(obs.LOG_WARNING, "[screenshot] No scene name specified.")
    return false
  end
  if not path or path == "" then
    obs.script_log(obs.LOG_WARNING, "[screenshot] No output path specified.")
    return false
  end

  local ph = obs.obs_get_proc_handler()

  -- Verify the plugin is loaded.
  local cd_check = obs.calldata_create()
  local ok = obs.proc_handler_call(ph, "framebuffer_is_ready", cd_check)
  obs.calldata_destroy(cd_check)
  if not ok then
    obs.script_log(obs.LOG_WARNING,
      "[screenshot] obs-framebridge not found – is the plugin loaded?")
    return false
  end

  -- Request an on-demand render of the named scene.
  local cd = obs.calldata_create()
  obs.calldata_set_string(cd, "scene_name", scene_name)
  obs.calldata_set_string(cd, "path",       path)
  obs.calldata_set_int(   cd, "width",      width)
  obs.calldata_set_int(   cd, "height",     height)
  obs.proc_handler_call(ph, "framebuffer_render_scene", cd)
  local success = obs.calldata_int(cd, "success") == 1
  obs.calldata_destroy(cd)

  if success then
    obs.script_log(obs.LOG_INFO,
      string.format("[screenshot] Saved '%s' → %s", scene_name, path))
  else
    obs.script_log(obs.LOG_WARNING,
      string.format("[screenshot] Failed: scene='%s' path='%s'", scene_name, path))
  end

  return success
end
