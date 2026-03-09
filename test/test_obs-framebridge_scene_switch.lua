obs = obslua

-- ---------------------------------------------------------------------------
-- test_obs-framebridge_scene_switch.lua
-- Simple test script: sample a region via obs-framebridge, compute
-- average luminance, and switch to a configured scene based on the
-- measured value. Useful to verify probe locations and automatic
-- scene switching without running the full detector.
-- ---------------------------------------------------------------------------

local KEY_SOURCE    = "source_name"
local KEY_X         = "probe_x"
local KEY_Y         = "probe_y"
local KEY_W         = "probe_w"
local KEY_H         = "probe_h"
local KEY_THRESH_A  = "thresh_a"
local KEY_THRESH_B  = "thresh_b"
local KEY_SCENE_MENU    = "scene_menu"
local KEY_SCENE_PLAYING = "scene_playing"
local KEY_SCENE_PAUSE   = "scene_pause"

local g_settings = nil

local function log_info(msg)  obs.script_log(obs.LOG_INFO,    "[scene_switch_test] " .. tostring(msg)) end
local function log_warn(msg)  obs.script_log(obs.LOG_WARNING, "[scene_switch_test] " .. tostring(msg)) end

local function script_description()
  return [[Simple test: sample a pixel region via obs-framebridge, compute
average luminance, then switch to one of three configured scenes.]]
end

function script_defaults(settings)
  obs.obs_data_set_default_string(settings, KEY_SOURCE, "")
  obs.obs_data_set_default_int(settings, KEY_X, 1280)
  obs.obs_data_set_default_int(settings, KEY_Y, 810)
  obs.obs_data_set_default_int(settings, KEY_W, 320)
  obs.obs_data_set_default_int(settings, KEY_H, 270)
  obs.obs_data_set_default_double(settings, KEY_THRESH_A, 42.5)
  obs.obs_data_set_default_double(settings, KEY_THRESH_B, 34.0)
  obs.obs_data_set_default_string(settings, KEY_SCENE_MENU,    "MenuScene")
  obs.obs_data_set_default_string(settings, KEY_SCENE_PLAYING, "PlayingScene")
  obs.obs_data_set_default_string(settings, KEY_SCENE_PAUSE,   "PauseScene")
end

function script_properties()
  local props = obs.obs_properties_create()

  local scene_list = obs.obs_properties_add_list(
    props, KEY_SOURCE, "Scene to watch",
    obs.OBS_COMBO_TYPE_LIST, obs.OBS_COMBO_FORMAT_STRING)
  obs.obs_property_list_add_string(scene_list, "(none - select a scene)", "")
  local scenes = obs.obs_frontend_get_scenes()
  if scenes then
    for _, s in ipairs(scenes) do
      local name = obs.obs_source_get_name(s)
      obs.obs_property_list_add_string(scene_list, name, name)
    end
    obs.source_list_release(scenes)
  end

  obs.obs_properties_add_int(props, KEY_X, "Probe X", 0, 10000, 1)
  obs.obs_properties_add_int(props, KEY_Y, "Probe Y", 0, 10000, 1)
  obs.obs_properties_add_int(props, KEY_W, "Probe Width", 1, 10000, 1)
  obs.obs_properties_add_int(props, KEY_H, "Probe Height", 1, 10000, 1)

  obs.obs_properties_add_float(props, KEY_THRESH_A, "Threshold A (menu)", 0, 255, 0.1)
  obs.obs_properties_add_float(props, KEY_THRESH_B, "Threshold B (playing)", 0, 255, 0.1)

  obs.obs_properties_add_text(props, KEY_SCENE_MENU,    "Scene for MENU", obs.OBS_TEXT_DEFAULT)
  obs.obs_properties_add_text(props, KEY_SCENE_PLAYING, "Scene for PLAYING", obs.OBS_TEXT_DEFAULT)
  obs.obs_properties_add_text(props, KEY_SCENE_PAUSE,   "Scene for PAUSE", obs.OBS_TEXT_DEFAULT)

  obs.obs_properties_add_button(props, "btn_test_switch", "Test & Switch",
    function()
      local src = obs.obs_data_get_string(g_settings, KEY_SOURCE)
      if not src or src == "" then log_warn("No scene selected"); return end
      local x = obs.obs_data_get_int(g_settings, KEY_X)
      local y = obs.obs_data_get_int(g_settings, KEY_Y)
      local w = obs.obs_data_get_int(g_settings, KEY_W)
      local h = obs.obs_data_get_int(g_settings, KEY_H)
      local ta = obs.obs_data_get_double(g_settings, KEY_THRESH_A)
      local tb = obs.obs_data_get_double(g_settings, KEY_THRESH_B)
      local state, lum = sample_and_decide(x,y,w,h,ta,tb)
      if state then
        log_info(string.format("Decision: %s  (luminance=%.2f)", state, lum))
        perform_switch(state)
      else
        log_warn("Sampling failed: " .. tostring(lum))
      end
    end)

  return props
end

function script_update(settings)
  g_settings = settings
end

-- ------------------------------------------------------------------
-- Plugin probe helpers (borrowed from scripts in repo)
-- ------------------------------------------------------------------
local function fb_is_ready()
  local ph = obs.obs_get_proc_handler()
  if not ph then return false end
  local cd = obs.calldata_create()
  local ok = obs.proc_handler_call(ph, "framebuffer_is_ready", cd)
  if not ok then obs.calldata_destroy(cd); return false end
  local ready = obs.calldata_int(cd, "ready") == 1
  obs.calldata_destroy(cd)
  return ready
end

local function avg_gray(x, y, w, h)
  local ph = obs.obs_get_proc_handler()
  if not ph then return nil, "no proc_handler" end
  local cd = obs.calldata_create()
  obs.calldata_set_int(cd, "x", x)
  obs.calldata_set_int(cd, "y", y)
  obs.calldata_set_int(cd, "w", w)
  obs.calldata_set_int(cd, "h", h)
  local ok = obs.proc_handler_call(ph, "framebuffer_avg_color", cd)
  if not ok then obs.calldata_destroy(cd); return nil, "proc call failed" end
  local r = obs.calldata_int(cd, "r")
  local g = obs.calldata_int(cd, "g")
  local b = obs.calldata_int(cd, "b")
  obs.calldata_destroy(cd)
  if r < 0 then return nil, string.format("r=%d (framebuffer error)", r) end
  local lum = r * 0.299 + g * 0.587 + b * 0.114
  return lum, { r = r, g = g, b = b }
end

local function perform_switch(state)
  if not g_settings then log_warn("perform_switch: no settings"); return end
  local target = nil
  if state == "menu" then
    target = obs.obs_data_get_string(g_settings, KEY_SCENE_MENU)
  elseif state == "playing" then
    target = obs.obs_data_get_string(g_settings, KEY_SCENE_PLAYING)
  else
    target = obs.obs_data_get_string(g_settings, KEY_SCENE_PAUSE)
  end
  if not target or target == "" then log_warn("No target scene configured for state: " .. state); return end
  local s = obs.obs_get_source_by_name(target)
  if s then
    obs.obs_frontend_set_current_scene(s)
    obs.obs_source_release(s)
    log_info("Switched to: " .. target)
  else
    log_warn("Target scene not found: " .. tostring(target))
  end
end

-- Returns state, luminance_on_success / error_message_on_fail
function sample_and_decide(x,y,w,h,th_a,th_b)
  if not fb_is_ready() then return nil, "plugin not ready" end
  local lum, rgb_or_err = avg_gray(x,y,w,h)
  if not lum then return nil, rgb_or_err end
  local state
  if lum > th_a then
    state = "menu"
  elseif lum > th_b then
    state = "playing"
  else
    state = "pause"
  end
  return state, lum
end

function script_load(settings)
  g_settings = settings
  log_info("script_loaded. Plugin ready=" .. tostring(fb_is_ready()))
end
