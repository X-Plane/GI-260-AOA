--[[ XLua 2.0 ]]
-- Garmin GI260 XLua 2.0 aircraft-local port.
-- First-pass port of the native plugin mechanism. Audio is exposed as datarefs
-- for aircraft FMOD integration instead of direct WAV playback.

require("XPLMDataAccess")
require("XPLMDisplay")
require("XPLMPanelGraphics")
require("XPLMPlanes")
require("XPLMProcessing")
require("XPLMUtilities")

local DESIGN_W = 264.0
local DESIGN_H = 549.0
local DEVICE_ID = "gi260_aoa"
local DREF_PREFIX = "laminar/gi260/"
local COMMAND_PREFIX = "laminar/gi260/"
local XPLM = {
    WindowDecorationRoundRectangle = 1
}

local MATERIAL_ATLAS_SIZE = 2048.0
local SOURCE_LED_LEFT = 42.0
local SOURCE_LED_RIGHT = 222.0
local SOURCE_LED_TOP = 503.0
local SOURCE_LED_BOTTOM = 100.0
local FAIL_FLASH_SECONDS = 0.5
local CALIBRATION_GRACE_SECONDS = 4.0
local VALIDATION_FAILURES_BEFORE_FAIL = 6
local UNPOWERED_TEST_AUDIO_OVERRIDE_SECONDS = 1.2

local MODULE = {
    RU = 1,
    RL = 2,
    YU = 3,
    YL = 4,
    YS = 5,
    YB = 6,
    GS = 7,
    GC = 8,
    GU = 9,
    GM = 10,
    GL = 11
}

local module_order_bottom_to_top = {
    MODULE.GL,
    MODULE.GM,
    MODULE.GU,
    MODULE.GC,
    MODULE.GS,
    MODULE.YB,
    MODULE.YS,
    MODULE.YL,
    MODULE.YU,
    MODULE.RL,
    MODULE.RU
}

local ladder_order_bottom_to_top = {
    MODULE.GL,
    MODULE.GM,
    MODULE.GU,
    MODULE.GS,
    MODULE.YB,
    MODULE.YS,
    MODULE.YL,
    MODULE.YU,
    MODULE.RL,
    MODULE.RU
}

local green_bar_modules = {
    MODULE.GL,
    MODULE.GM,
    MODULE.GU,
    MODULE.GS
}

local modules = {
    [MODULE.RU] = { name = "RU", file = "1A.png", center_x = 132.0, center_y = 92.0, visible = false, image = nil },
    [MODULE.RL] = { name = "RL", file = "2A.png", center_x = 132.0, center_y = 144.0, visible = false, image = nil },
    [MODULE.YU] = { name = "YU", file = "3A.png", center_x = 132.0, center_y = 196.0, visible = false, image = nil },
    [MODULE.YL] = { name = "YL", file = "4A.png", center_x = 132.0, center_y = 248.0, visible = false, image = nil },
    [MODULE.YS] = { name = "YS", file = "5DL.png", center_x = 132.0, center_y = 284.0, visible = false, image = nil },
    [MODULE.YB] = { name = "YB", file = "6SL.png", center_x = 132.0, center_y = 316.0, visible = false, image = nil },
    [MODULE.GS] = { name = "GS", file = "7DL.png", center_x = 132.0, center_y = 348.0, visible = false, image = nil },
    [MODULE.GC] = { name = "GC", file = "7C.png", center_x = 132.0, center_y = 348.0, visible = false, image = nil },
    [MODULE.GU] = { name = "GU", file = "8SL.png", center_x = 132.0, center_y = 380.0, visible = false, image = nil },
    [MODULE.GM] = { name = "GM", file = "9SL.png", center_x = 132.0, center_y = 412.0, visible = false, image = nil },
    [MODULE.GL] = { name = "GL", file = "10SL.png", center_x = 132.0, center_y = 440.0, visible = false, image = nil }
}

local test_pressed = { file = "test_pressed.png", center_x = 83.5, center_y = 498.5, image = nil }
local mute_pressed = { file = "mute_pressed.png", center_x = 180.5, center_y = 498.5, image = nil }
local background_asset = { file = "Background.png", center_x = DESIGN_W * 0.5, center_y = DESIGN_H * 0.5, image = nil }
local night_background_asset = { file = "Background_LIT.png", center_x = DESIGN_W * 0.5, center_y = DESIGN_H * 0.5, image = nil }

local config = {
    power_dataref = "sim/cockpit2/switches/avionics_power_on",
    night_lighting_dataref = "sim/graphics/scenery/percent_lights_on",
    night_dimming_source = "sun_pitch",
    night_sun_pitch_day_deg = 6.0,
    night_sun_pitch_night_deg = -6.0,
    night_background_lit_blend_max = 0.0,
    night_led_black_mask_max = 0.60,
    night_led_black_mask_start_at = 0.0,
    night_led_black_mask_full_at = 1.0,
    night_led_black_mask_power = 1.0,
    night_button_black_mask_max = 0.0,
    instrument_brightness_dataref = "sim/cockpit2/electrical/instrument_brightness_ratio",
    instrument_brightness_index = 0,
    initial_status = 0,
    enable_popup = 1,
    arm_ias_kts = 50.0,
    force_warning_on_stall_warning = true,
    audio_arm_delay_seconds = 15.0,
    audio_startup_delay_seconds = 15.0,
    audio_gain = 1.49,
    slow_beep_repeat_gap_seconds = 0.60,
    fast_beep_repeat_gap_seconds = 0.0,
    fail_beep_repeat_gap_seconds = 0.55,
    event_beep_gap_seconds = 0.18,
    mute_minimum_seconds = 15.0,
    aoa_smoothing_seconds = 0.15,
    power_up_rise_seconds = 1.0,
    power_up_fall_seconds = 0.5,
    self_test_seconds = 3.0,
    unpowered_test_long_press_seconds = 0.5,
    panel_brightness = 1.0,
    panel_brightness_day = 1.0,
    panel_brightness_night = 0.55,
    panel_brightness_ambient_power = 0.75,
    display_luminance = 1.0,
    element_emissive_gain = 1.0,
    draw_background_in_3d = 0,
    popup_lock_native_aspect = 1,
    element_align_scale_x = 0.95,
    element_align_scale_y = 0.96,
    element_align_offset_x = 18.0,
    element_align_offset_y = -12.0,
    element_bbox_left_px = 1379.0,
    element_bbox_right_px = 1957.0,
    element_bbox_top_px = 451.0,
    element_bbox_bottom_px = 1755.0,
    aircraft_vso_kts = 0.0,
    aircraft_warning_aoa_deg = 10.0,
    normalized_zero_aoa_deg = 0.0,
    approach_speed_vso_multiplier = 1.3,
    module_hysteresis_deg = 0.2,
    green_solid_lower_aoa_deg = 1.0,
    green_solid_middle_aoa_deg = 2.0,
    green_solid_upper_aoa_deg = 3.0,
    green_circle_aoa_deg = 0.0,
    green_split_bar_aoa_deg = 4.0,
    yellow_solid_bar_aoa_deg = 5.0,
    yellow_split_bar_aoa_deg = 6.0,
    yellow_lower_chevron_aoa_deg = 7.0,
    yellow_upper_chevron_aoa_deg = 8.0,
    red_lower_aoa_deg = 9.0,
    red_upper_aoa_deg = 10.0
}

local dr = {}
local custom_dr = {}
local commands = {}
local command_handlers_registered = false

local atlas = nil
local avionic = nil
local popup_avionic = nil
local flight_loop = nil
local popup_visible = false
local draw_context = "3d"

local aircraft_dir = ""
local aircraft_acf_path = ""
local asset_dir = ""
local texture_asset_dir = ""
local config_path = ""

local state = {
    was_powered = false,
    is_armed = false,
    is_muted = false,
    test_button_pressed = false,
    mute_button_pressed = false,
    startup_audio_delay_initialized = false,
    power_up_red_on_beep_played = false,
    power_up_red_off_beep_played = false,
    unpowered_test_beep_queued = false,
    self_test_remaining = 0.0,
    power_up_elapsed = 0.0,
    unpowered_test_held = 0.0,
    unpowered_test_audio_override = 0.0,
    startup_audio_delay_remaining = 0.0,
    audio_delay_remaining = 0.0,
    mute_remaining = 0.0,
    beep_repeat_delay_remaining = 0.0,
    pending_event_beeps = 0,
    beep_mode = 0,
    beep_gain = 0.0,
    xlua_loaded = 0,
    xlua_heartbeat = 0,
    raw_aoa_deg = 0.0,
    filtered_aoa_deg = 0.0,
    raw_aoa_units = 0.0,
    filtered_aoa_units = 0.0,
    stall_warning_ratio = 0.0,
    speed_lift_ratio = 0.0,
    raw_speed_proxy_units = 0.0,
    speed_proxy_units = 0.0,
    aero_lift_support_ratio = 1.0,
    gear_load_ratio = 0.0,
    empty_mass_kg = 0.0,
    fuel_mass_kg = 0.0,
    payload_mass_kg = 0.0,
    current_mass_kg = 0.0,
    max_mass_kg = 0.0,
    weight_adjusted_vso_kts = 0.0,
    flap_ratio = 0.0,
    flap_request_ratio = 0.0,
    on_ground = false,
    has_filtered_aoa = false,
    last_units = 0.0,
    units_delta = 0.0,
    permanent_fail = false,
    fail_animation_elapsed = 0.0,
    calibration_grace_remaining = CALIBRATION_GRACE_SECONDS,
    validation_failure_count = 0,
    fail_reason = "",
    pending_validation_failure_reason = "",
    audio_alert_mode = 0,
    scenario = "OFF",
    unit_pitch_in_acf = 0.0,
    unit_pitch_object_index = -1
}

local function log(msg)
    print("[GI260 XLua2] " .. tostring(msg))
end

local function clamp(v, lo, hi)
    if v ~= v then return lo end
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

local function trim(s)
    local trimmed = (s or ""):gsub("^%s+", ""):gsub("%s+$", "")
    return trimmed
end

local function strip_inline_comment(s)
    local p = string.find(s, ";", 1, true)
    if p then
        return trim(string.sub(s, 1, p - 1))
    end
    return trim(s)
end

local function parse_bool(value, fallback)
    local v = string.lower(trim(value))
    if v == "1" or v == "true" or v == "yes" or v == "on" then return true end
    if v == "0" or v == "false" or v == "no" or v == "off" then return false end
    return fallback
end

local function parse_number(value, fallback)
    local n = tonumber(trim(value))
    if n == nil then return fallback end
    return n
end

local function unwrap_string(v)
    if type(v) == "table" then
        return v[1] or v.outPath or v.outFileName or ""
    end
    return v or ""
end

local function unwrap_number(v, fallback)
    if type(v) == "number" then return v end
    local n = tonumber(tostring(v))
    if n ~= nil then return n end
    return fallback
end

local function normalize_path(path)
    local normalized = (path or ""):gsub("\\", "/")
    return normalized
end

local function dirname(path)
    path = normalize_path(path)
    return path:match("^(.*)/[^/]*$") or ""
end

local function join_path(a, b)
    if a == "" then return b end
    if string.sub(a, -1) == "/" then return a .. b end
    return a .. "/" .. b
end

local function make_xplane_relative_path(path)
    path = normalize_path(path)
    local system = XPLMGetSystemPath()
    local system_path = ""
    if type(system) == "table" then
        system_path = normalize_path(unwrap_string(system.outSystemPath))
    else
        system_path = normalize_path(unwrap_string(system))
    end

    if system_path ~= "" then
        if string.sub(system_path, -1) ~= "/" then
            system_path = system_path .. "/"
        end
        if string.sub(path, 1, string.len(system_path)) == system_path then
            return string.sub(path, string.len(system_path) + 1)
        end
    end

    return path
end

local function file_exists(path)
    local f = io.open(path, "r")
    if f then
        f:close()
        return true
    end
    return false
end

local function read_gi260_pitch_from_acf()
    state.unit_pitch_in_acf = 0.0
    state.unit_pitch_object_index = -1

    if aircraft_acf_path == "" then
        log("GI260 object pitch not scanned: aircraft .acf path is unknown")
        return
    end

    local f = io.open(aircraft_acf_path, "r")
    if not f then
        log("GI260 object pitch not scanned: cannot open " .. aircraft_acf_path)
        return
    end

    local gi260_object_index = nil
    local pitch_by_index = {}
    for line in f:lines() do
        local normalized = normalize_path(line)
        local object_index, object_path = normalized:match("_obja/(%d+)/_v10_att_file_stl%s+(.+)")
        if object_index and object_path and object_path:find("GI%-260%.obj") then
            gi260_object_index = object_index
        end

        local pitch_index, pitch_value = normalized:match("_obja/(%d+)/_v10_att_the_ref%s+([%-%+]?%d+%.?%d*)")
        if pitch_index and pitch_value then
            pitch_by_index[pitch_index] = tonumber(pitch_value)
        end
    end
    f:close()

    if gi260_object_index and pitch_by_index[gi260_object_index] then
        state.unit_pitch_in_acf = pitch_by_index[gi260_object_index]
        state.unit_pitch_object_index = tonumber(gi260_object_index) or -1
        log(string.format(
            "GI260 object pitch from .acf: obja/%s _v10_att_the_ref %.3f deg",
            gi260_object_index,
            state.unit_pitch_in_acf))
    else
        log("GI260 object pitch not found in .acf by GI-260.obj reference")
    end
end

local function discover_aircraft_paths()
    local model = XPLMGetNthAircraftModel(0)
    local acf_path = ""
    if type(model) == "table" then
        acf_path = normalize_path(unwrap_string(model.outPath))
    end

    aircraft_acf_path = acf_path
    aircraft_dir = dirname(acf_path)
    if aircraft_dir == "" then
        aircraft_dir = "."
        log("Could not discover aircraft path; using relative aircraft-root fallback")
    end

    local candidates = {
        join_path(aircraft_dir, "plugins/xlua/scripts/GI-260"),
        join_path(aircraft_dir, "plugins/xlua2/scripts/GI-260")
    }
    asset_dir = candidates[1]
    for _, candidate in ipairs(candidates) do
        if file_exists(join_path(candidate, "config.ini")) then
            asset_dir = candidate
            break
        end
    end

    texture_asset_dir = make_xplane_relative_path(asset_dir)
    config_path = join_path(asset_dir, "config.ini")
    log("Aircraft dir: " .. aircraft_dir)
    log("GI260 asset dir: " .. asset_dir)
    log("GI260 texture asset dir: " .. texture_asset_dir)
    read_gi260_pitch_from_acf()
end

local function read_config()
    local f = io.open(config_path, "r")
    if not f then
        log("config.ini not found; using defaults: " .. config_path)
        return
    end

    for line in f:lines() do
        local cleaned = strip_inline_comment(line)
        if cleaned ~= "" and string.sub(cleaned, 1, 1) ~= "[" then
            local key, value = cleaned:match("^([^=]+)=(.*)$")
            if key and value then
                key = string.lower(trim(key))
                value = trim(value)
                if config[key] ~= nil then
                    if type(config[key]) == "boolean" then
                        config[key] = parse_bool(value, config[key])
                    elseif type(config[key]) == "number" then
                        config[key] = parse_number(value, config[key])
                    else
                        config[key] = value
                    end
                end
            end
        end
    end

    f:close()
end

local function atlas_x_to_panel(x)
    return (x / MATERIAL_ATLAS_SIZE) * DESIGN_W
end

local function atlas_y_to_panel(y)
    return (1.0 - (y / MATERIAL_ATLAS_SIZE)) * DESIGN_H
end

local function update_alignment_from_bbox()
    local target_left = atlas_x_to_panel(config.element_bbox_left_px)
    local target_right = atlas_x_to_panel(config.element_bbox_right_px)
    local target_top = atlas_y_to_panel(config.element_bbox_top_px)
    local target_bottom = atlas_y_to_panel(config.element_bbox_bottom_px)
    local source_width = SOURCE_LED_RIGHT - SOURCE_LED_LEFT
    local source_height = SOURCE_LED_TOP - SOURCE_LED_BOTTOM
    local sx = (target_right - target_left) / source_width
    local sy = (target_top - target_bottom) / source_height
    local cx = DESIGN_W * 0.5
    local cy = DESIGN_H * 0.5

    config.element_align_scale_x = sx
    config.element_align_scale_y = sy
    config.element_align_offset_x = target_left - (cx + (SOURCE_LED_LEFT - cx) * sx)
    config.element_align_offset_y = target_bottom - (cy + (SOURCE_LED_BOTTOM - cy) * sy)
end

local function find_dr(name)
    if not name or name == "" then return nil end
    return XPLMFindDataRef(name)
end

local function getf(handle, fallback)
    if handle == nil then return fallback end
    local ok, value = pcall(XPLMGetDataf, handle)
    if ok and value ~= nil then return value end
    return fallback
end

local function geti(handle, fallback)
    if handle == nil then return fallback end
    local ok, value = pcall(XPLMGetDatai, handle)
    if ok and value ~= nil then return value end
    return fallback
end

local function get_int_array_first_any(handle, max_count)
    if handle == nil then return false end
    local values = {}
    local ok, count = pcall(XPLMGetDatavi, handle, values, 0, max_count)
    if not ok or not count then return false end
    for i = 1, count do
        if values[i] ~= nil and values[i] ~= 0 then
            return true
        end
    end
    return false
end

local function get_float_array_value(handle, index, fallback)
    if handle == nil then return fallback end
    local values = {}
    local zero_based_index = math.max(0, math.floor(index or 0))
    local ok, count = pcall(XPLMGetDatavf, handle, values, zero_based_index, 1)
    if not ok or not count or count <= 0 then return fallback end
    if values[1] == nil then return fallback end
    return values[1]
end

local function register_int_dataref(name, getter)
    return XPLMRegisterDataAccessor(
        name,
        XPLMDataTypeID.xplmType_Int,
        false,
        getter,
        nil,
        nil,
        nil,
        nil,
        nil,
        nil,
        nil,
        nil,
        nil,
        nil,
        nil,
        nil,
        nil
    )
end

local function register_float_dataref(name, getter)
    return XPLMRegisterDataAccessor(
        name,
        XPLMDataTypeID.xplmType_Float,
        false,
        nil,
        nil,
        getter,
        nil,
        nil,
        nil,
        nil,
        nil,
        nil,
        nil,
        nil,
        nil,
        nil,
        nil
    )
end

local function read_beep_mode()
    return state.beep_mode
end

local function read_beep_gain()
    return state.beep_gain
end

local function read_popup_visible()
    return popup_visible and 1 or 0
end

local function read_unit_pitch_in_acf()
    return state.unit_pitch_in_acf
end

local function read_xlua_loaded()
    return state.xlua_loaded
end

local function read_xlua_heartbeat()
    return state.xlua_heartbeat
end

local function register_custom_datarefs()
    custom_dr.xlua_loaded = register_int_dataref(DREF_PREFIX .. "xlua_loaded", read_xlua_loaded)
    custom_dr.xlua_heartbeat = register_int_dataref(DREF_PREFIX .. "xlua_heartbeat", read_xlua_heartbeat)
    custom_dr.beep_mode = register_int_dataref(DREF_PREFIX .. "audio/beep_mode", read_beep_mode)
    custom_dr.beep_gain = register_float_dataref(DREF_PREFIX .. "audio/beep_gain", read_beep_gain)
    custom_dr.popup_visible = register_int_dataref(DREF_PREFIX .. "popup_visible", read_popup_visible)
    custom_dr.unit_pitch_in_acf = register_float_dataref(DREF_PREFIX .. "unit_pitch_in_acf", read_unit_pitch_in_acf)
end

local function unregister_custom_datarefs()
    for _, handle in pairs(custom_dr) do
        if handle then
            XPLMUnregisterDataAccessor(handle)
        end
    end
    custom_dr = {}
end

local function initialize_datarefs()
    dr.aoa_fm = find_dr("sim/flightmodel2/misc/AoA_angle_degrees")
    dr.aoa_pilot = find_dr("sim/cockpit2/gauges/indicators/AoA_pilot")
    dr.position_alpha = find_dr("sim/flightmodel/position/alpha") or find_dr("sim/flightmodel2/position/alpha")
    dr.ias = find_dr("sim/cockpit2/gauges/indicators/airspeed_kts_pilot")
    dr.power = find_dr(config.power_dataref) or find_dr("sim/cockpit/electrical/avionics_on")
    dr.night = find_dr(config.night_lighting_dataref)
    dr.sun_pitch = find_dr("sim/graphics/scenery/sun_pitch_degrees")
    dr.instrument_brightness = find_dr(config.instrument_brightness_dataref)
    dr.vso = find_dr("sim/aircraft/view/acf_Vso")
    dr.stall_warn_alpha = find_dr("sim/aircraft/overflow/acf_stall_warn_alpha")
    dr.empty_mass = find_dr("sim/aircraft/weight/acf_m_empty")
    dr.max_mass = find_dr("sim/aircraft/weight/acf_m_max")
    dr.fuel_mass = find_dr("sim/flightmodel/weight/m_fuel_total") or find_dr("sim/aircraft/weight/acf_m_fuel_tot")
    dr.payload_mass = find_dr("sim/flightmodel/weight/m_fixed")
    dr.total_mass = find_dr("sim/flightmodel/weight/m_total")
    dr.flap_ratio = find_dr("sim/flightmodel/controls/flaprat")
    dr.flap_request_ratio = find_dr("sim/flightmodel/controls/flaprqst")
    dr.stall_warning = find_dr("sim/cockpit2/annunciators/stall_warning") or find_dr("sim/flightmodel/failures/stallwarning")
    dr.stall_warning_ratio = find_dr("sim/cockpit2/annunciators/stall_warning_ratio")
    dr.on_ground_any = find_dr("sim/flightmodel/failures/onground_any")
    dr.gear_on_ground = find_dr("sim/flightmodel2/gear/on_ground")
    dr.aero_normal_force = find_dr("sim/flightmodel/forces/fnrml_aero")
    dr.gear_normal_force = find_dr("sim/flightmodel/forces/fnrml_gear")
    dr.radio_volume = find_dr("sim/operation/sound/radio_volume_ratio")
end

local function threshold_for_module(id)
    if id == MODULE.RU then return config.red_upper_aoa_deg end
    if id == MODULE.RL then return config.red_lower_aoa_deg end
    if id == MODULE.YU then return config.yellow_upper_chevron_aoa_deg end
    if id == MODULE.YL then return config.yellow_lower_chevron_aoa_deg end
    if id == MODULE.YS then return config.yellow_split_bar_aoa_deg end
    if id == MODULE.YB then return config.yellow_solid_bar_aoa_deg end
    if id == MODULE.GS then return config.green_split_bar_aoa_deg end
    if id == MODULE.GC then return config.green_circle_aoa_deg end
    if id == MODULE.GU then return config.green_solid_upper_aoa_deg end
    if id == MODULE.GM then return config.green_solid_middle_aoa_deg end
    return config.green_solid_lower_aoa_deg
end

local function set_threshold_for_module(id, value)
    if id == MODULE.RU then config.red_upper_aoa_deg = value
    elseif id == MODULE.RL then config.red_lower_aoa_deg = value
    elseif id == MODULE.YU then config.yellow_upper_chevron_aoa_deg = value
    elseif id == MODULE.YL then config.yellow_lower_chevron_aoa_deg = value
    elseif id == MODULE.YS then config.yellow_split_bar_aoa_deg = value
    elseif id == MODULE.YB then config.yellow_solid_bar_aoa_deg = value
    elseif id == MODULE.GS then config.green_split_bar_aoa_deg = value
    elseif id == MODULE.GC then config.green_circle_aoa_deg = value
    elseif id == MODULE.GU then config.green_solid_upper_aoa_deg = value
    elseif id == MODULE.GM then config.green_solid_middle_aoa_deg = value
    elseif id == MODULE.GL then config.green_solid_lower_aoa_deg = value end
end

local function validate_module_thresholds()
    local previous = threshold_for_module(ladder_order_bottom_to_top[1])
    for i = 2, #ladder_order_bottom_to_top do
        local id = ladder_order_bottom_to_top[i]
        local current = threshold_for_module(id)
        if current <= previous then
            current = previous + 0.01
            set_threshold_for_module(id, current)
        end
        previous = current
    end
end

local function clear_modules()
    for _, m in pairs(modules) do
        m.visible = false
    end
end

local function set_all_modules(visible)
    for _, m in pairs(modules) do
        m.visible = visible
    end
end

local function set_module(id, visible)
    modules[id].visible = visible
end

local function is_visible(id)
    return modules[id].visible == true
end

local function set_module_by_threshold(id, units, threshold)
    local was_visible = is_visible(id)
    local off_threshold = threshold - config.module_hysteresis_deg
    if was_visible then
        set_module(id, units >= off_threshold)
    else
        set_module(id, units >= threshold)
    end
end

local function critical_aoa_deg()
    return math.max(config.normalized_zero_aoa_deg + 0.1, config.aircraft_warning_aoa_deg)
end

local function approach_aoa_units()
    return config.green_split_bar_aoa_deg
end

local function approach_ref_units()
    if config.green_circle_aoa_deg > 0.0 then
        return config.green_circle_aoa_deg
    end
    return config.green_solid_lower_aoa_deg
end

local function approach_aoa_deg()
    local zero = config.normalized_zero_aoa_deg
    local warn = critical_aoa_deg()
    local mult = math.max(1.01, config.approach_speed_vso_multiplier)
    return zero + (warn - zero) / (mult * mult)
end

local function normalize_stall_ratio_to_units(ratio)
    if ratio <= 0.01 then
        return 0.0
    end
    local approach_ratio = 1.0 / math.pow(math.max(1.01, config.approach_speed_vso_multiplier), 2.0)
    local app_units = approach_aoa_units()
    local warn_units = config.red_lower_aoa_deg
    if ratio <= approach_ratio then
        return math.max(0.0, ratio * app_units / approach_ratio)
    end
    local span = math.max(0.01, 1.0 - approach_ratio)
    return app_units + (ratio - approach_ratio) * (warn_units - app_units) / span
end

local function clamp_ias(raw_ias)
    if raw_ias ~= raw_ias then return 0.0 end
    if raw_ias < 0.0 and raw_ias > -5.0 then return 0.0 end
    return math.max(0.0, raw_ias)
end

local function read_power()
    return dr.power == nil or geti(dr.power, 0) ~= 0
end

local function read_on_ground()
    if dr.on_ground_any and geti(dr.on_ground_any, 0) ~= 0 then
        return true
    end
    return get_int_array_first_any(dr.gear_on_ground, 10)
end

local function update_ground_lift_support()
    state.aero_lift_support_ratio = state.on_ground and 0.0 or 1.0
    state.gear_load_ratio = 0.0

    if state.current_mass_kg <= 1.0 then return end
    local weight_n = state.current_mass_kg * 9.80665
    if weight_n <= 1.0 then return end

    local aero_n = getf(dr.aero_normal_force, 0.0)
    local gear_n = getf(dr.gear_normal_force, 0.0)
    local aero_support = clamp(math.max(0.0, aero_n) / weight_n, 0.0, 1.2)
    state.gear_load_ratio = clamp(math.abs(gear_n) / weight_n, 0.0, 1.2)

    if not state.on_ground then
        state.aero_lift_support_ratio = 1.0
        return
    end

    local gear_unloaded_support = clamp(1.0 - state.gear_load_ratio, 0.0, 1.0)
    state.aero_lift_support_ratio = clamp(math.max(aero_support, gear_unloaded_support), 0.0, 1.0)
end

local function read_aircraft_reference_values()
    state.on_ground = read_on_ground()
    config.aircraft_vso_kts = getf(dr.vso, config.aircraft_vso_kts)
    config.aircraft_warning_aoa_deg = getf(dr.stall_warn_alpha, config.aircraft_warning_aoa_deg)
    state.empty_mass_kg = getf(dr.empty_mass, state.empty_mass_kg)
    state.fuel_mass_kg = getf(dr.fuel_mass, state.fuel_mass_kg)
    state.payload_mass_kg = getf(dr.payload_mass, state.payload_mass_kg)
    state.max_mass_kg = getf(dr.max_mass, state.max_mass_kg)
    state.flap_ratio = getf(dr.flap_ratio, state.flap_ratio)
    state.flap_request_ratio = getf(dr.flap_request_ratio, state.flap_request_ratio)

    local total_mass = getf(dr.total_mass, 0.0)
    if total_mass > 0.0 then
        state.current_mass_kg = total_mass
    else
        local summed = state.empty_mass_kg + state.fuel_mass_kg + state.payload_mass_kg
        if summed > 0.0 then
            state.current_mass_kg = summed
        end
    end

    if config.aircraft_vso_kts > 0.0 and state.current_mass_kg > 0.0 and state.max_mass_kg > 0.0 then
        local weight_ratio = clamp(state.current_mass_kg / state.max_mass_kg, 0.25, 2.0)
        state.weight_adjusted_vso_kts = config.aircraft_vso_kts * math.sqrt(weight_ratio)
    else
        state.weight_adjusted_vso_kts = config.aircraft_vso_kts
    end

    update_ground_lift_support()
end

local function compute_speed_lift_ratio(ias)
    local vso = state.weight_adjusted_vso_kts > 0.0 and state.weight_adjusted_vso_kts or config.aircraft_vso_kts
    if ias <= 1.0 or vso <= 1.0 then return 0.0 end
    local r = vso / ias
    return math.max(0.0, r * r)
end

local function compute_speed_proxy_units(ias)
    state.speed_lift_ratio = compute_speed_lift_ratio(ias)
    state.raw_speed_proxy_units = normalize_stall_ratio_to_units(state.speed_lift_ratio)
    if state.on_ground then
        return state.raw_speed_proxy_units * state.aero_lift_support_ratio
    end
    return state.raw_speed_proxy_units
end

local function stall_warning_active()
    return dr.stall_warning and geti(dr.stall_warning, 0) ~= 0
end

local function update_aoa_values(elapsed)
    local raw_ias = getf(dr.ias, 0.0)
    local ias = clamp_ias(raw_ias)
    state.raw_aoa_deg = getf(dr.aoa_fm, getf(dr.aoa_pilot, getf(dr.position_alpha, 0.0)))
    state.stall_warning_ratio = getf(dr.stall_warning_ratio, -1.0)
    state.speed_proxy_units = compute_speed_proxy_units(ias)

    local stall_ratio = state.stall_warning_ratio == state.stall_warning_ratio and math.max(0.0, state.stall_warning_ratio) or 0.0
    local target_units = math.max(normalize_stall_ratio_to_units(stall_ratio), state.speed_proxy_units)
    if config.force_warning_on_stall_warning and stall_warning_active() then
        target_units = math.max(target_units, config.red_lower_aoa_deg)
    end
    target_units = clamp(target_units, 0.0, config.red_upper_aoa_deg)
    state.raw_aoa_units = target_units

    if not state.has_filtered_aoa or config.aoa_smoothing_seconds <= 0.0 then
        state.filtered_aoa_deg = state.raw_aoa_deg
        state.filtered_aoa_units = target_units
        state.has_filtered_aoa = true
    else
        local alpha = clamp(elapsed / config.aoa_smoothing_seconds, 0.0, 1.0)
        state.filtered_aoa_deg = state.filtered_aoa_deg + (state.raw_aoa_deg - state.filtered_aoa_deg) * alpha
        state.filtered_aoa_units = state.filtered_aoa_units + (target_units - state.filtered_aoa_units) * alpha
    end

    state.units_delta = state.filtered_aoa_units - state.last_units
    state.last_units = state.filtered_aoa_units
end

local function latch_fail(reason)
    if not state.permanent_fail then
        state.permanent_fail = true
        state.fail_reason = reason or "unknown"
        log("FAIL: " .. state.fail_reason)
    end
end

local function validate_sources(raw_ias)
    local reason = ""
    if dr.ias == nil then
        reason = "missing IAS dataref"
    elseif raw_ias ~= raw_ias or raw_ias > 1000.0 then
        reason = string.format("bad IAS value %.1f kt", raw_ias)
    elseif dr.vso == nil or config.aircraft_vso_kts ~= config.aircraft_vso_kts or config.aircraft_vso_kts < 5.0 or config.aircraft_vso_kts > 800.0 then
        reason = string.format("bad acf_Vso %.1f kt", config.aircraft_vso_kts)
    elseif state.current_mass_kg <= 0.0 or state.max_mass_kg <= 0.0 then
        reason = string.format("bad mass current/max %.1f/%.1f kg", state.current_mass_kg, state.max_mass_kg)
    else
        local weight_ratio = state.current_mass_kg / state.max_mass_kg
        if weight_ratio <= 0.05 or weight_ratio > 2.0 then
            reason = string.format("bad weight ratio %.3f", weight_ratio)
        elseif state.weight_adjusted_vso_kts < 5.0 or state.weight_adjusted_vso_kts > 800.0 then
            reason = string.format("bad weight-adjusted Vso %.1f kt", state.weight_adjusted_vso_kts)
        elseif dr.stall_warning_ratio == nil then
            reason = "missing stall_warning_ratio dataref"
        elseif state.stall_warning_ratio ~= state.stall_warning_ratio then
            reason = "bad stall_warning_ratio"
        elseif dr.stall_warn_alpha == nil or config.aircraft_warning_aoa_deg <= config.normalized_zero_aoa_deg + 0.1 or config.aircraft_warning_aoa_deg > 90.0 then
            reason = string.format("bad stall warning alpha %.1f deg", config.aircraft_warning_aoa_deg)
        elseif approach_aoa_deg() <= config.normalized_zero_aoa_deg or approach_aoa_deg() >= critical_aoa_deg() then
            reason = string.format("bad approach alpha %.1f deg", approach_aoa_deg())
        elseif dr.flap_ratio == nil then
            reason = "missing flap ratio dataref"
        elseif state.flap_ratio < -0.05 or state.flap_ratio > 1.2 then
            reason = string.format("bad flap ratio %.2f", state.flap_ratio)
        elseif dr.on_ground_any == nil and dr.gear_on_ground == nil then
            reason = "missing weight-on-wheels dataref"
        elseif dr.aero_normal_force == nil then
            reason = "missing aero normal force dataref"
        elseif dr.gear_normal_force == nil then
            reason = "missing gear normal force dataref"
        elseif state.aero_lift_support_ratio < 0.0 or state.aero_lift_support_ratio > 1.2 then
            reason = string.format("bad aero lift support ratio %.2f", state.aero_lift_support_ratio)
        end
    end

    if reason == "" then
        state.validation_failure_count = 0
        state.pending_validation_failure_reason = ""
        return true
    end

    if state.pending_validation_failure_reason ~= reason then
        state.validation_failure_count = 0
    end
    state.pending_validation_failure_reason = reason

    if state.calibration_grace_remaining > 0.0 then
        return false
    end

    state.validation_failure_count = state.validation_failure_count + 1
    if state.validation_failure_count >= VALIDATION_FAILURES_BEFORE_FAIL then
        latch_fail(reason)
    end
    return not state.permanent_fail
end

local function queue_beeps(count)
    state.pending_event_beeps = math.max(state.pending_event_beeps, count or 1)
end

local function fire_beep(mode)
    state.beep_mode = mode or 1
    local master = clamp(getf(dr.radio_volume, 1.0), 0.0, 1.0)
    state.beep_gain = config.audio_gain * master
end

local function update_beep_events(elapsed)
    if state.unpowered_test_audio_override > 0.0 then
        state.unpowered_test_audio_override = math.max(0.0, state.unpowered_test_audio_override - elapsed)
    end

    state.beep_repeat_delay_remaining = math.max(0.0, state.beep_repeat_delay_remaining - elapsed)
    if state.beep_repeat_delay_remaining > 0.0 then return end

    if state.pending_event_beeps > 0 then
        fire_beep(1)
        state.pending_event_beeps = state.pending_event_beeps - 1
        state.beep_repeat_delay_remaining = config.event_beep_gap_seconds
        return
    end

    if state.unpowered_test_audio_override > 0.0 then
        return
    end

    if state.audio_alert_mode == 3 then
        fire_beep(3)
        state.beep_repeat_delay_remaining = config.fast_beep_repeat_gap_seconds
    elseif state.audio_alert_mode == 2 then
        fire_beep(2)
        state.beep_repeat_delay_remaining = config.slow_beep_repeat_gap_seconds
    elseif state.audio_alert_mode == 4 then
        fire_beep(4)
        state.beep_repeat_delay_remaining = config.fail_beep_repeat_gap_seconds
    else
        state.beep_mode = 0
        state.beep_gain = 0.0
    end
end

local function update_arming(ias, powered, elapsed)
    if not powered then
        state.is_armed = false
        state.audio_delay_remaining = 0.0
        return false
    end

    local should_arm = config.arm_ias_kts <= 0.0 or ias > config.arm_ias_kts
    if should_arm and not state.is_armed then
        state.is_armed = true
        state.audio_delay_remaining = config.audio_arm_delay_seconds
    end

    if state.is_armed and state.audio_delay_remaining > 0.0 then
        state.audio_delay_remaining = math.max(0.0, state.audio_delay_remaining - elapsed)
    end

    return state.is_armed
end

local function apply_power_up_sweep(elapsed)
    local rise = math.max(0.1, config.power_up_rise_seconds)
    local fall = math.max(0.1, config.power_up_fall_seconds)
    local total = rise + fall
    if state.power_up_elapsed >= total then return false end

    state.power_up_elapsed = math.min(total, state.power_up_elapsed + math.max(0.0, elapsed))
    local visible_count = 0
    if state.power_up_elapsed <= rise then
        visible_count = math.min(#module_order_bottom_to_top, math.ceil((state.power_up_elapsed / rise) * #module_order_bottom_to_top))
    else
        local hidden_count = math.min(#module_order_bottom_to_top, math.floor(((state.power_up_elapsed - rise) / fall) * #module_order_bottom_to_top))
        visible_count = #module_order_bottom_to_top - hidden_count
    end

    clear_modules()
    for i = 1, visible_count do
        set_module(module_order_bottom_to_top[i], true)
    end

    local lower_red = is_visible(MODULE.RL)
    local any_red = lower_red or is_visible(MODULE.RU)
    if not state.power_up_red_on_beep_played and lower_red then
        queue_beeps(1)
        state.power_up_red_on_beep_played = true
    end
    if state.power_up_red_on_beep_played and not state.power_up_red_off_beep_played and state.power_up_elapsed > rise and not any_red then
        queue_beeps(1)
        state.power_up_red_off_beep_played = true
    end
    return true
end

local function apply_fail_animation(elapsed)
    state.fail_animation_elapsed = state.fail_animation_elapsed + math.max(0.0, elapsed)
    local phase = math.floor(state.fail_animation_elapsed / FAIL_FLASH_SECONDS)
    clear_modules()
    if phase % 2 == 0 then
        set_module(MODULE.RU, true)
    else
        set_module(MODULE.RL, true)
    end
    state.audio_alert_mode = 4
    state.scenario = "FAIL"
end

local function apply_unpowered_test_animation(elapsed)
    state.unpowered_test_held = state.unpowered_test_held + math.max(0.0, elapsed)
    state.fail_animation_elapsed = state.fail_animation_elapsed + math.max(0.0, elapsed)
    local phase = math.floor(state.fail_animation_elapsed / FAIL_FLASH_SECONDS)
    clear_modules()
    if phase % 2 == 0 then
        set_module(MODULE.RU, true)
    else
        set_module(MODULE.RL, true)
    end
    state.audio_alert_mode = 0
    state.scenario = "UNPOWERED TEST"

    if not state.unpowered_test_beep_queued and state.unpowered_test_held >= config.unpowered_test_long_press_seconds then
        queue_beeps(3)
        state.unpowered_test_audio_override = UNPOWERED_TEST_AUDIO_OVERRIDE_SECONDS
        state.unpowered_test_beep_queued = true
    end
end

local function apply_normal_display(units)
    set_module_by_threshold(MODULE.GL, units, config.green_solid_lower_aoa_deg)
    set_module_by_threshold(MODULE.GM, units, config.green_solid_middle_aoa_deg)
    set_module_by_threshold(MODULE.GU, units, config.green_solid_upper_aoa_deg)
    set_module_by_threshold(MODULE.GC, units, approach_ref_units())
    set_module_by_threshold(MODULE.GS, units, config.green_split_bar_aoa_deg)
    set_module_by_threshold(MODULE.YB, units, config.yellow_solid_bar_aoa_deg)
    set_module_by_threshold(MODULE.YS, units, config.yellow_split_bar_aoa_deg)
    set_module_by_threshold(MODULE.YL, units, config.yellow_lower_chevron_aoa_deg)
    set_module_by_threshold(MODULE.YU, units, config.yellow_upper_chevron_aoa_deg)
    set_module_by_threshold(MODULE.RL, units, config.red_lower_aoa_deg)
    set_module_by_threshold(MODULE.RU, units, config.red_upper_aoa_deg)
end

local function count_green_bars()
    local c = 0
    for _, id in ipairs(green_bar_modules) do
        if is_visible(id) then c = c + 1 end
    end
    return c
end

local function determine_scenario()
    if is_visible(MODULE.RU) then return "EXTREME WARNING AOA" end
    if is_visible(MODULE.RL) then return "WARNING AOA" end
    if is_visible(MODULE.YU) or is_visible(MODULE.YL) or is_visible(MODULE.YS) or is_visible(MODULE.YB) then return "HIGH AOA" end
    if is_visible(MODULE.GC) and count_green_bars() == 4 then return "APPROACH AOA" end
    if is_visible(MODULE.GC) then return "LOW AOA + REF" end
    if count_green_bars() > 0 then return "MINIMUM VISIBLE AOA" end
    return "CRUISE"
end

local function update_audio_alert_state()
    if not state.is_armed or state.startup_audio_delay_remaining > 0.0 or state.audio_delay_remaining > 0.0 or state.is_muted then
        state.audio_alert_mode = 0
        return
    end
    if is_visible(MODULE.RL) or is_visible(MODULE.RU) then
        state.audio_alert_mode = 3
    elseif is_visible(MODULE.YU) then
        state.audio_alert_mode = 2
    else
        state.audio_alert_mode = 0
    end
end

local function reset_runtime_state()
    clear_modules()
    state.was_powered = false
    state.is_armed = false
    state.is_muted = false
    state.test_button_pressed = false
    state.mute_button_pressed = false
    state.startup_audio_delay_initialized = false
    state.power_up_red_on_beep_played = false
    state.power_up_red_off_beep_played = false
    state.unpowered_test_beep_queued = false
    state.self_test_remaining = 0.0
    state.power_up_elapsed = 0.0
    state.unpowered_test_held = 0.0
    state.unpowered_test_audio_override = 0.0
    state.startup_audio_delay_remaining = 0.0
    state.audio_delay_remaining = 0.0
    state.mute_remaining = 0.0
    state.beep_repeat_delay_remaining = 0.0
    state.pending_event_beeps = 0
    state.raw_aoa_units = 0.0
    state.filtered_aoa_units = 0.0
    state.has_filtered_aoa = false
    state.permanent_fail = false
    state.fail_animation_elapsed = 0.0
    state.calibration_grace_remaining = CALIBRATION_GRACE_SECONDS
    state.validation_failure_count = 0
    state.fail_reason = ""
    state.pending_validation_failure_reason = ""
    state.audio_alert_mode = 0
    state.scenario = "RESET"
end

local function update_indicator_state(elapsed)
    local powered = read_power()
    if not powered then
        if state.test_button_pressed then
            apply_unpowered_test_animation(elapsed)
        else
            clear_modules()
            state.scenario = "OFF"
            state.unpowered_test_held = 0.0
            state.unpowered_test_beep_queued = false
        end
        state.was_powered = false
        state.startup_audio_delay_initialized = false
        state.startup_audio_delay_remaining = 0.0
        state.power_up_elapsed = 0.0
        state.power_up_red_on_beep_played = false
        state.power_up_red_off_beep_played = false
        state.self_test_remaining = 0.0
        state.audio_alert_mode = 0
        state.has_filtered_aoa = false
        update_arming(0.0, false, elapsed)
        return
    end

    if not state.startup_audio_delay_initialized then
        state.startup_audio_delay_remaining = config.audio_startup_delay_seconds
        state.startup_audio_delay_initialized = true
    end
    state.startup_audio_delay_remaining = math.max(0.0, state.startup_audio_delay_remaining - elapsed)
    state.mute_remaining = math.max(0.0, state.mute_remaining - elapsed)
    state.is_muted = state.mute_remaining > 0.0

    read_aircraft_reference_values()
    update_aoa_values(elapsed)
    local raw_ias = getf(dr.ias, 0.0)
    local ias = clamp_ias(raw_ias)
    state.calibration_grace_remaining = math.max(0.0, state.calibration_grace_remaining - elapsed)

    if state.permanent_fail then
        apply_fail_animation(elapsed)
        return
    end
    if not validate_sources(raw_ias) then
        clear_modules()
        state.audio_alert_mode = 0
        state.scenario = "INITIALIZING"
        return
    end

    if not state.was_powered then
        state.was_powered = true
        state.power_up_elapsed = 0.0
        state.power_up_red_on_beep_played = false
        state.power_up_red_off_beep_played = false
        state.fail_animation_elapsed = 0.0
    end

    update_arming(ias, true, elapsed)

    if state.self_test_remaining > 0.0 then
        state.self_test_remaining = math.max(0.0, state.self_test_remaining - elapsed)
        set_all_modules(true)
        state.audio_alert_mode = 0
        state.scenario = "SELF TEST"
        return
    end

    if apply_power_up_sweep(elapsed) then
        state.audio_alert_mode = 0
        state.scenario = "POWER UP"
        return
    end

    if not state.is_armed then
        clear_modules()
        state.audio_alert_mode = 0
        state.scenario = "DISARMED"
        return
    end

    apply_normal_display(state.filtered_aoa_units)
    state.scenario = determine_scenario()
    update_audio_alert_state()
end

local function tick_callback(elapsed_since_last_call, elapsed_since_last_loop, counter, refcon)
    local elapsed = elapsed_since_last_call or elapsed_since_last_loop or 0.016
    elapsed = clamp(elapsed, 0.001, 0.1)
    state.xlua_heartbeat = state.xlua_heartbeat + 1
    update_indicator_state(elapsed)
    update_beep_events(elapsed)
    return -1
end

local function image_path(file)
    return join_path(join_path(texture_asset_dir, "Textures"), file)
end

local function add_image(file)
    local path = image_path(file)
    local ok, idx = pcall(XPLMTextureAtlasAddImageFile, atlas, path)
    if ok then
        return idx
    end
    log("Failed to add texture " .. path .. ": " .. tostring(idx))
    return nil
end

local function load_atlas()
    atlas = XPLMCreateTextureAtlas()
    background_asset.image = add_image(background_asset.file)
    night_background_asset.image = add_image(night_background_asset.file)
    for _, m in pairs(modules) do
        m.image = add_image(m.file)
    end
    test_pressed.image = add_image(test_pressed.file)
    mute_pressed.image = add_image(mute_pressed.file)
    XPLMTextureAtlasBake(atlas)
end

local function night_ratio()
    return clamp(getf(dr.night, 0.0), 0.0, 1.0)
end

-- Reusable Panel Graphics dimming helpers.
-- `percent_lights_on` is useful as an on/off scenery-light cue, but it is not a
-- linear ambient-light source. For display dimming, sun pitch gives a smoother
-- twilight ramp and keeps the LED mask from bunching near 0.97..1.0.
local function remap01(v, in_min, in_max)
    if math.abs(in_max - in_min) < 0.001 then
        return v >= in_max and 1.0 or 0.0
    end
    return clamp((v - in_min) / (in_max - in_min), 0.0, 1.0)
end

local function apply_curve01(v, power)
    return clamp(v, 0.0, 1.0) ^ clamp(power or 1.0, 0.05, 4.0)
end

local function sun_pitch_night_ratio()
    local sun_pitch = getf(dr.sun_pitch, config.night_sun_pitch_day_deg)
    local day_pitch = config.night_sun_pitch_day_deg or 6.0
    local night_pitch = config.night_sun_pitch_night_deg or -6.0
    return remap01(sun_pitch, day_pitch, night_pitch)
end

local function display_night_ratio()
    local source = string.lower(config.night_dimming_source or "sun_pitch")
    if source == "percent_lights_on" then
        return night_ratio()
    end
    if source == "max" then
        return math.max(night_ratio(), sun_pitch_night_ratio())
    end
    if dr.sun_pitch ~= nil then
        return sun_pitch_night_ratio()
    end
    return night_ratio()
end

local function night_led_mask_ratio()
    local raw = display_night_ratio()
    local start_at = clamp(config.night_led_black_mask_start_at or 0.0, 0.0, 1.0)
    local full_at = clamp(config.night_led_black_mask_full_at or 1.0, 0.0, 1.0)
    if full_at <= start_at + 0.001 then
        return raw >= full_at and 1.0 or 0.0
    end
    local t = clamp((raw - start_at) / (full_at - start_at), 0.0, 1.0)
    return apply_curve01(t, config.night_led_black_mask_power)
end

local function draw_line(start_x, start_y, end_x, end_y, width, color)
    local verts = {
        { x = start_x, y = start_y },
        { x = end_x, y = end_y }
    }
    XPLMLinesWithWidth(color, width, verts, 2)
end

local function fill_rect_black(left, top, right, bottom)
    local height = math.max(1.0, top - bottom)
    local mid_y = (top + bottom) * 0.5
    draw_line(left, mid_y, right, mid_y, height, XPLMMakeColor(0, 0, 0, 1))
end

local function fill_panel_black()
    fill_rect_black(0.0, DESIGN_H, DESIGN_W, 0.0)
end

local draw_atlas_image_in

function draw_atlas_image_in(image, alpha, left, top, right, bottom)
    local draw_alpha = alpha or 1.0
    local gain = clamp((config.display_luminance or 1.0) * (config.element_emissive_gain or 1.0), 0.0, 1.0)
    XPLMTextureAtlasDrawIn(atlas, image, XPLMMakeColor(gain, gain, gain, draw_alpha), left, top, right, bottom)
end

local function apply_element_alignment(left, top, right, bottom)
    if draw_context == "popup" then
        return left, top, right, bottom
    end

    local cx = DESIGN_W * 0.5
    local cy = DESIGN_H * 0.5
    local sx = config.element_align_scale_x
    local sy = config.element_align_scale_y
    local ox = config.element_align_offset_x
    local oy = config.element_align_offset_y

    return
        cx + (left - cx) * sx + ox,
        cy + (top - cy) * sy + oy,
        cx + (right - cx) * sx + ox,
        cy + (bottom - cy) * sy + oy
end

local function draw_image_centered(asset, alpha, black_mask)
    if asset.image == nil then return end
    local w = XPLMTextureAtlasGetImageWidth(atlas, asset.image)
    local h = XPLMTextureAtlasGetImageHeight(atlas, asset.image)
    local panel_center_y = DESIGN_H - asset.center_y
    local left = asset.center_x - w * 0.5
    local top = panel_center_y + h * 0.5
    local right = asset.center_x + w * 0.5
    local bottom = panel_center_y - h * 0.5
    left, top, right, bottom = apply_element_alignment(left, top, right, bottom)
    draw_atlas_image_in(asset.image, alpha, left, top, right, bottom)
    if black_mask and black_mask > 0.0 then
        XPLMTextureAtlasDrawIn(atlas, asset.image, XPLMMakeColor(0.0, 0.0, 0.0, black_mask), left, top, right, bottom)
    end
end

local function accumulate_button_touch_zone(identifier, left, top, right, bottom)
    left, top, right, bottom = apply_element_alignment(left, top, right, bottom)
    XPLMAccumulateTouchZone({
        type = XPLMTouchZone.xplm_TouchZone_Identifier,
        identifier = identifier,
        left = left,
        top = top,
        right = right,
        bottom = bottom
    })
end

local function draw_background_asset(asset, alpha)
    if asset.image == nil then return end
    local w = XPLMTextureAtlasGetImageWidth(atlas, asset.image)
    local h = XPLMTextureAtlasGetImageHeight(atlas, asset.image)
    local left = asset.center_x - w * 0.5
    local top = DESIGN_H - asset.center_y + h * 0.5
    local right = asset.center_x + w * 0.5
    local bottom = DESIGN_H - asset.center_y - h * 0.5
    XPLMTextureAtlasDrawIn(atlas, asset.image, XPLMMakeColor(1, 1, 1, alpha or 1.0), left, top, right, bottom)
end

local function screen_draw_cb(ref)
    local previous_context = draw_context
    draw_context = ref or "3d"

    local draw_static_background = draw_context == "popup" or config.draw_background_in_3d ~= 0
    if draw_static_background then
        fill_panel_black()

        if background_asset.image ~= nil then
            draw_background_asset(background_asset, 1.0)
        end
    end

    local nr = display_night_ratio()
    local led_mask = clamp(night_led_mask_ratio() * config.night_led_black_mask_max, 0.0, 1.0)
    local button_mask = clamp(nr * config.night_button_black_mask_max, 0.0, 1.0)

    local lit_alpha = clamp(nr * config.night_background_lit_blend_max, 0.0, 1.0)
    if draw_static_background and night_background_asset.image ~= nil and lit_alpha > 0.0 then
        draw_background_asset(night_background_asset, lit_alpha)
    end

    for _, id in ipairs({ MODULE.RU, MODULE.RL, MODULE.YU, MODULE.YL, MODULE.YS, MODULE.YB, MODULE.GS, MODULE.GC, MODULE.GU, MODULE.GM, MODULE.GL }) do
        local m = modules[id]
        if m.visible then
            draw_image_centered(m, 1.0, led_mask)
        end
    end

    if state.test_button_pressed then
        draw_image_centered(test_pressed, 1.0, button_mask)
    end
    if state.mute_button_pressed then
        draw_image_centered(mute_pressed, 1.0, button_mask)
    end

    accumulate_button_touch_zone(1, 42, 65, 125, 36)
    accumulate_button_touch_zone(2, 139, 65, 222, 36)

    draw_context = previous_context
end

local function bezel_draw_cb(ambR, ambG, ambB, ref)
    local previous_context = draw_context
    draw_context = ref or "popup"
    fill_panel_black()
    draw_context = previous_context
end

local function aircraft_instrument_brightness()
    return clamp(
        get_float_array_value(dr.instrument_brightness, config.instrument_brightness_index or 0, 1.0),
        0.0,
        1.0
    )
end

local function auto_panel_brightness(ambient)
    local night = clamp(config.panel_brightness_night or 0.35, 0.0, 1.0)
    local day = clamp(config.panel_brightness_day or 1.0, 0.0, 1.0)
    local ambient_ratio = clamp(ambient or 1.0, 0.0, 1.0)
    if dr.sun_pitch ~= nil or dr.night ~= nil then
        ambient_ratio = 1.0 - display_night_ratio()
    end
    local power = clamp(config.panel_brightness_ambient_power or 0.75, 0.1, 4.0)
    local shaped_ambient = ambient_ratio ^ power
    return clamp(night + ((day - night) * shaped_ambient), 0.0, 1.0)
end

local function device_brightness_ratio(rheo, ambient)
    local plugin_master = clamp(rheo or config.panel_brightness, 0.0, 1.0)
    return clamp(plugin_master * aircraft_instrument_brightness() * auto_panel_brightness(ambient), 0.0, 1.0)
end

local function brightness_cb(rheo, ambient, bus, ref)
    -- Panel Graphics is always emissive; OBJ NITS defines the physical maximum.
    -- Final device brightness is multiplicative:
    -- plugin master * aircraft default instrument brightness * auto day/night.
    return device_brightness_ratio(rheo, ambient)
end

local function apply_popup_native_aspect()
    if not popup_avionic or config.popup_lock_native_aspect == 0 then return end
    local ok, geom = pcall(XPLMGetAvionicsGeometry, popup_avionic)
    if not ok or type(geom) ~= "table" then return end

    local left = unwrap_number(geom.outLeft, 100)
    local top = unwrap_number(geom.outTop, 700)
    local right = unwrap_number(geom.outRight, left + DESIGN_W)
    local bottom = unwrap_number(geom.outBottom, top - DESIGN_H)
    local height = math.abs(top - bottom)
    if height < 100.0 then height = DESIGN_H end
    local width = height * (DESIGN_W / DESIGN_H)
    XPLMSetAvionicsGeometry(popup_avionic, math.floor(left), math.floor(top), math.floor(left + width), math.floor(top - height))
end

local function begin_test_button()
    state.test_button_pressed = true
    state.unpowered_test_held = 0.0
    state.unpowered_test_beep_queued = false
    state.fail_animation_elapsed = 0.0
end

local function end_test_button()
    if state.test_button_pressed and read_power() then
        state.self_test_remaining = config.self_test_seconds
        queue_beeps(1)
    end
    state.test_button_pressed = false
    state.unpowered_test_held = 0.0
    state.unpowered_test_beep_queued = false
end

local function press_mute_button()
    state.mute_remaining = config.mute_minimum_seconds
    state.is_muted = state.mute_remaining > 0.0
end

local function touch_event_cb(identifier, status, x, y, dx, dy, button, ref)
    if identifier == 1 then
        if status == XPLMMouseStatus.xplm_MouseDown then
            begin_test_button()
        elseif status == XPLMMouseStatus.xplm_MouseUp then
            end_test_button()
        end
    elseif identifier == 2 then
        if status == XPLMMouseStatus.xplm_MouseDown then
            state.mute_button_pressed = true
        elseif status == XPLMMouseStatus.xplm_MouseUp then
            if state.mute_button_pressed then
                press_mute_button()
            end
            state.mute_button_pressed = false
        end
    end
end

local function set_popup_visible(visible)
    if config.enable_popup == 0 then
        popup_visible = false
        if popup_avionic then
            XPLMSetAvionicsPopupVisible(popup_avionic, false)
        end
        return
    end

    popup_visible = visible and true or false
    if popup_avionic then
        XPLMSetAvionicsPopupVisible(popup_avionic, popup_visible)
        if popup_visible then
            apply_popup_native_aspect()
        end
    end
end

local function command_popup_toggle(cmd, phase, ref)
    if phase == XPLMCommandPhase.xplm_CommandBegin and config.enable_popup ~= 0 then
        set_popup_visible(not popup_visible)
    end
    return true
end

local function command_test(cmd, phase, ref)
    if phase == XPLMCommandPhase.xplm_CommandBegin then
        begin_test_button()
    elseif phase == XPLMCommandPhase.xplm_CommandEnd then
        end_test_button()
    end
    return true
end

local function command_mute(cmd, phase, ref)
    if phase == XPLMCommandPhase.xplm_CommandBegin then
        state.mute_button_pressed = true
        press_mute_button()
    elseif phase == XPLMCommandPhase.xplm_CommandEnd then
        state.mute_button_pressed = false
    end
    return true
end

local function create_commands()
    commands.popup = XPLMCreateCommand(COMMAND_PREFIX .. "popup_toggle", "Toggle GI260 popup")
    commands.test = XPLMCreateCommand(COMMAND_PREFIX .. "test", "Press GI260 TEST")
    commands.mute = XPLMCreateCommand(COMMAND_PREFIX .. "mute", "Press GI260 MUTE")
    commands.button_left = XPLMCreateCommand(COMMAND_PREFIX .. "button_L_press_CMD", "Press GI260 left TEST button")
    commands.button_right = XPLMCreateCommand(COMMAND_PREFIX .. "button_R_press_CMD", "Press GI260 right MUTE button")
    XPLMRegisterCommandHandler(commands.popup, command_popup_toggle, true, nil)
    XPLMRegisterCommandHandler(commands.test, command_test, true, nil)
    XPLMRegisterCommandHandler(commands.mute, command_mute, true, nil)
    XPLMRegisterCommandHandler(commands.button_left, command_test, true, nil)
    XPLMRegisterCommandHandler(commands.button_right, command_mute, true, nil)
    command_handlers_registered = true
end

local function unregister_commands()
    if not command_handlers_registered then return end
    XPLMUnregisterCommandHandler(commands.popup, command_popup_toggle, true, nil)
    XPLMUnregisterCommandHandler(commands.test, command_test, true, nil)
    XPLMUnregisterCommandHandler(commands.mute, command_mute, true, nil)
    XPLMUnregisterCommandHandler(commands.button_left, command_test, true, nil)
    XPLMUnregisterCommandHandler(commands.button_right, command_mute, true, nil)
    command_handlers_registered = false
end

function XPluginStart()
    log("XPluginStart")
    state.xlua_loaded = 1
    discover_aircraft_paths()
    read_config()
    update_alignment_from_bbox()
    validate_module_thresholds()
    initialize_datarefs()
    register_custom_datarefs()
    create_commands()
    load_atlas()
    return true
end

function XPluginEnable()
    log("XPluginEnable")
    reset_runtime_state()
    local cavio3d = {
        screenWidth = DESIGN_W,
        screenHeight = DESIGN_H,
        bezelWidth = DESIGN_W,
        bezelHeight = DESIGN_H,
        screenOffsetX = 0,
        screenOffsetY = 0,
        bezelDrawCallback = bezel_draw_cb,
        drawCallback = screen_draw_cb,
        brightnessCallback = brightness_cb,
        deviceID = DEVICE_ID,
        deviceName = "Garmin GI260",
        refcon = "3d",
        contentType = XPLMWindowContentType.xplm_WindowContentTypePanelGraphics
    }
    avionic = XPLMCreateAvionicsEx(cavio3d)
    XPLMSetAvionicsBrightnessRheo(avionic, clamp(config.panel_brightness, 0.0, 1.0))
    XPLMAvionicsSetTouchEventHandler(avionic, touch_event_cb)
    XPLMSetAvionicsPopupVisible(avionic, false)

    if config.enable_popup ~= 0 then
        local cavio_popup = {
            screenWidth = DESIGN_W,
            screenHeight = DESIGN_H,
            bezelWidth = DESIGN_W,
            bezelHeight = DESIGN_H,
            screenOffsetX = 0,
            screenOffsetY = 0,
            bezelDrawCallback = bezel_draw_cb,
            drawCallback = screen_draw_cb,
            brightnessCallback = brightness_cb,
            deviceID = DEVICE_ID .. "_popup",
            deviceName = "Garmin GI260 Popup",
            refcon = "popup",
            contentType = XPLMWindowContentType.xplm_WindowContentTypePanelGraphics
        }
        popup_avionic = XPLMCreateAvionicsEx(cavio_popup)
        XPLMSetAvionicsBrightnessRheo(popup_avionic, clamp(config.panel_brightness, 0.0, 1.0))
        XPLMAvionicsSetTouchEventHandler(popup_avionic, touch_event_cb)
        set_popup_visible(config.initial_status ~= 0)
    end

    flight_loop = XPLMCreateFlightLoop({
        phase = XPLMFlightLoopPhaseType.xplm_FlightLoop_Phase_AfterFlightModel,
        callbackFunc = tick_callback,
        refcon = nil
    })
    XPLMScheduleFlightLoop(flight_loop, -1, true)
    return true
end

function XPluginDisable()
    log("XPluginDisable")
    if flight_loop then
        XPLMDestroyFlightLoop(flight_loop)
        flight_loop = nil
    end
    if avionic then
        XPLMDestroyAvionics(avionic)
        avionic = nil
    end
    if popup_avionic then
        XPLMDestroyAvionics(popup_avionic)
        popup_avionic = nil
    end
end

function XPluginStop()
    log("XPluginStop")
    if popup_avionic then
        XPLMDestroyAvionics(popup_avionic)
        popup_avionic = nil
    end
    if avionic then
        XPLMDestroyAvionics(avionic)
        avionic = nil
    end
    unregister_commands()
    unregister_custom_datarefs()
    if atlas then
        XPLMDestroyTextureAtlas(atlas)
        atlas = nil
    end
end

function XPluginReceiveMessage(from, msg, param)
    -- Aircraft reload/crash message IDs vary by exposed constants; keep this
    -- conservative and reset on all X-Plane-originated messages that reach us.
    if from == XPLM_PLUGIN_XPLANE then
        reset_runtime_state()
        discover_aircraft_paths()
        initialize_datarefs()
    end
end
