obs = obslua

function script_description()
  return "Test loader for obs-framebridge"
end

function script_load(settings)
  local ph = obs.obs_get_proc_handler()
  local cd = obs.calldata_create()
  local ok = obs.proc_handler_call(ph, "framebuffer_is_ready", cd)
  if not ok then
    obs.script_log(obs.LOG_WARNING, "[obs-framebridge-test] plugin not available")
  else
    local ready = obs.calldata_int(cd, "ready")
    obs.script_log(obs.LOG_INFO, string.format("[obs-framebridge-test] framebuffer_is_ready -> %d", ready))
  end
  obs.calldata_destroy(cd)
end
