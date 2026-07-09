#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <GL/gl.h>
#include <wincodec.h>

#include "XPLMDisplay.h"
#include "XPLMDataAccess.h"
#include "XPLMGraphics.h"
#include "XPLMMenus.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMSound.h"
#include "XPLMUtilities.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace
{
constexpr const char* kPluginName = "Garmin GI260";
constexpr const char* kPluginSignature = "com.x-plane.garmin.gi260";
constexpr const char* kPluginDescription = "Garmin GI260 style AOA popout indicator.";
constexpr const char* kMenuTitle = "Garmin GI260";
constexpr const char* kToggleMenuTitle = "Toggle";
constexpr const char* kBorderMenuTitle = "Border";
constexpr const char* kLockPositionMenuTitle = "Lock Position";
constexpr const char* kDebugOverlayMenuTitle = "Debug Overlay";
constexpr int kDesignWidth = 264;
constexpr int kDesignHeight = 549;
constexpr float kDesignAspect = static_cast<float>(kDesignWidth) / static_cast<float>(kDesignHeight);
constexpr float kAudioArmDelaySeconds = 15.0f;
constexpr float kAudioStartupDelaySeconds = 15.0f;
constexpr float kMuteMinimumSeconds = 15.0f;
constexpr float kSlowBeepRepeatGapSeconds = 0.60f;
constexpr float kFastBeepRepeatGapSeconds = 0.0f;
constexpr float kFailBeepRepeatGapSeconds = 0.55f;
constexpr float kEventBeepGapSeconds = 0.18f;
constexpr float kPowerUpStepSeconds = 0.08f;
constexpr float kPowerUpRiseSeconds = 1.0f;
constexpr float kPowerUpFallSeconds = 0.5f;
constexpr float kSelfTestSeconds = 3.0f;
constexpr float kUnpoweredTestLongPressSeconds = 0.5f;
constexpr float kUnpoweredTestAudioOverrideSeconds = 5.0f;
constexpr float kCriticalAoaUnits = 10.0f;
constexpr float kDefaultApproachSpeedVsoMultiplier = 1.3f;
constexpr float kFailFlashSeconds = 0.5f;
constexpr float kCalibrationGraceSeconds = 5.0f;
constexpr int kValidationFailuresBeforeFail = 5;
constexpr const char* kPreferredAoaSourceDataRef = "sim/flightmodel2/misc/AoA_angle_degrees";
constexpr const char* kAoaPilotDataRef = "sim/cockpit2/gauges/indicators/AoA_pilot";
constexpr const char* kLegacyAoaSourceDataRef = "sim/flightmodel/position/alpha";
constexpr const char* kIasSourceDataRef = "sim/cockpit2/gauges/indicators/airspeed_kts_pilot";
constexpr const char* kPowerSourceDataRef = "sim/cockpit2/switches/avionics_power_on";

// Set true only when diagnosing texture loading or drawing problems. Keep false for normal use.
constexpr bool kDebugLogging = false;

enum class MenuItem : std::intptr_t
{
    Toggle = 1,
    Border = 2,
    LockPosition = 3,
    DebugOverlay = 4,
};

enum class ModuleId : size_t
{
    // Texture order follows the GI260 face from top warning chevrons down to
    // the lowest green bar. Drawing logic uses thresholds, not this enum order.
    RED_UPPER_CHEVRON = 0,
    RED_LOWER_CHEVRON,
    YELLOW_UPPER_CHEVRON,
    YELLOW_LOWER_CHEVRON,
    YELLOW_SPLIT_BAR,
    YELLOW_SOLID_BAR,
    GREEN_SPLIT_BAR,
    GREEN_CIRCLE,
    GREEN_SOLID_UPPER,
    GREEN_SOLID_MIDDLE,
    GREEN_SOLID_LOWER,
    Count,
};

enum class AudioAlertMode
{
    None,
    SlowBeep,
    FastBeep,
    FailBeep,
};

enum class DisplayScenario
{
    PowerOff,
    SelfTest,
    PowerUpSweep,
    Initializing,
    Fail,
    Disarmed,
    UnpoweredTest,
    Cruise,
    MinimumVisibleAoa,
    LowAoaReference,
    ApproachAoa,
    HighAoa,
    WarningAoa,
    ExtremeWarningAoa,
};

struct Rect
{
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct TextureAsset
{
    const char* fileName = nullptr;
    float centerX = 0.0f;
    float centerY = 0.0f;
    int textureId = 0;
    int width = 0;
    int height = 0;
    bool loadAttempted = false;
};

struct IndicatorModule
{
    ModuleId id = ModuleId::RED_UPPER_CHEVRON;
    TextureAsset texture = {};
    bool visible = false;
};

struct PcmSample
{
    std::vector<uint8_t> pcmData = {};
    int sampleRateHz = 0;
    int channelCount = 0;
    bool loaded = false;
    std::string error = {};
};

struct Gi260Config
{
    bool forceWarningOnStallWarning = true;
    bool initialStatus = true;
    float armIasKts = 50.0f;
    float audioStartupDelaySeconds = kAudioStartupDelaySeconds;
    float audioArmDelaySeconds = kAudioArmDelaySeconds;
    float audioGain = 1.49f;
    float slowBeepRepeatGapSeconds = kSlowBeepRepeatGapSeconds;
    float fastBeepRepeatGapSeconds = kFastBeepRepeatGapSeconds;
    float failBeepRepeatGapSeconds = kFailBeepRepeatGapSeconds;
    float eventBeepGapSeconds = kEventBeepGapSeconds;
    float muteMinimumSeconds = kMuteMinimumSeconds;
    float aoaSmoothingSeconds = 0.15f;
    float powerUpStepSeconds = kPowerUpStepSeconds;
    float powerUpRiseSeconds = kPowerUpRiseSeconds;
    float powerUpFallSeconds = kPowerUpFallSeconds;
    float selfTestSeconds = kSelfTestSeconds;
    float unpoweredTestLongPressSeconds = kUnpoweredTestLongPressSeconds;
    std::string powerDataRefName = kPowerSourceDataRef;
    std::string nightLightingDataRefName = {};
    float nightBackgroundBlendMax = 1.0f;
    float nightLedBlackMaskMax = 0.30f;
    float nightButtonBlackMaskMax = 0.15f;
    float aircraftVsoKts = 0.0f;
    float aircraftWarningAoaDeg = 10.0f;
    float normalizedZeroAoaDeg = 0.0f;
    float approachSpeedVsoMultiplier = kDefaultApproachSpeedVsoMultiplier;
    float moduleHysteresisDeg = 0.2f;
    float redUpperDeg = 10.0f;
    float redLowerDeg = 9.0f;
    float yellowUpperChevronDeg = 8.0f;
    float yellowLowerChevronDeg = 7.0f;
    float yellowSplitBarDeg = 6.0f;
    float yellowSolidBarDeg = 5.0f;
    float greenSplitBarDeg = 4.0f;
    float greenCircleDeg = 0.0f;
    float greenSolidUpperDeg = 3.0f;
    float greenSolidMiddleDeg = 2.0f;
    float greenSolidLowerDeg = 1.0f;
};

XPLMMenuID gMenu = nullptr;
int gPluginMenuItem = -1;
int gToggleMenuItem = -1;
int gBorderMenuItem = -1;
int gLockPositionMenuItem = -1;
int gDebugOverlayMenuItem = -1;
XPLMWindowID gWindow = nullptr;
bool gToggleEnabled = true;
bool gBorderEnabled = true;
bool gLockPositionEnabled = false;
bool gDebugOverlayEnabled = false;
bool gHasLastFloatingGeometry = false;
bool gHasLastOsGeometry = false;
bool gHasLockedOffset = false;
Rect gLastFloatingGeometry = {};
Rect gLastOsGeometry = {};
int gLockedOffsetFromLeft = 0;
int gLockedOffsetFromTop = 0;

std::filesystem::path gPluginRoot;
Gi260Config gConfig;
TextureAsset gBackgroundTexture = {"Background.png", 0.0f, 0.0f};
TextureAsset gNightBackgroundTexture = {"Background_LIT.png", kDesignWidth * 0.5f, kDesignHeight * 0.5f};
// LED texture centers are in the original 264x549 design coordinate system.
// The renderer scales them with the fitted popout face so asset placement stays
// stable at any window size.
std::array<IndicatorModule, static_cast<size_t>(ModuleId::Count)> gModules = {{
    {ModuleId::RED_UPPER_CHEVRON, {"1A.png", 132.0f, 92.0f}},
    {ModuleId::RED_LOWER_CHEVRON, {"2A.png", 132.0f, 144.0f}},
    {ModuleId::YELLOW_UPPER_CHEVRON, {"3A.png", 132.0f, 196.0f}},
    {ModuleId::YELLOW_LOWER_CHEVRON, {"4A.png", 132.0f, 248.0f}},
    {ModuleId::YELLOW_SPLIT_BAR, {"5DL.png", 132.0f, 284.0f}},
    {ModuleId::YELLOW_SOLID_BAR, {"6SL.png", 132.0f, 316.0f}},
    {ModuleId::GREEN_SPLIT_BAR, {"7DL.png", 132.0f, 348.0f}},
    {ModuleId::GREEN_CIRCLE, {"7C.png", 132.0f, 348.0f}},
    {ModuleId::GREEN_SOLID_UPPER, {"8SL.png", 132.0f, 380.0f}},
    {ModuleId::GREEN_SOLID_MIDDLE, {"9SL.png", 132.0f, 412.0f}},
    {ModuleId::GREEN_SOLID_LOWER, {"10SL.png", 132.0f, 440.0f}},
}};
TextureAsset gTestPressedTexture = {"test_pressed.png", 83.5f, 498.5f};
TextureAsset gMutePressedTexture = {"mute_pressed.png", 180.5f, 498.5f};
XPLMDataRef gAoaDataRef = nullptr;
XPLMDataRef gAoaPilotDataRef = nullptr;
XPLMDataRef gAoaFlightModelDataRef = nullptr;
XPLMDataRef gPositionAlphaDataRef = nullptr;
XPLMDataRef gIasDataRef = nullptr;
XPLMDataRef gPowerDataRef = nullptr;
XPLMDataRef gNightLightingDataRef = nullptr;
XPLMDataRef gPitchDataRef = nullptr;
XPLMDataRef gFlightPathDataRef = nullptr;
XPLMDataRef gAircraftVsoDataRef = nullptr;
XPLMDataRef gAircraftStallWarnAlphaDataRef = nullptr;
XPLMDataRef gAircraftEmptyMassKgDataRef = nullptr;
XPLMDataRef gAircraftMaxMassKgDataRef = nullptr;
XPLMDataRef gAircraftFuelMassKgDataRef = nullptr;
XPLMDataRef gPayloadMassKgDataRef = nullptr;
XPLMDataRef gTotalMassKgDataRef = nullptr;
XPLMDataRef gFlapRatioDataRef = nullptr;
XPLMDataRef gFlapRequestRatioDataRef = nullptr;
XPLMDataRef gStallWarningDataRef = nullptr;
XPLMDataRef gStallWarningRatioDataRef = nullptr;
XPLMDataRef gOnGroundAnyDataRef = nullptr;
XPLMDataRef gGearOnGroundDataRef = nullptr;
XPLMDataRef gAeroNormalForceDataRef = nullptr;
XPLMDataRef gGearNormalForceDataRef = nullptr;
XPLMDataRef gRadioVolumeRatioDataRef = nullptr;
bool gWasPowered = false;
bool gIsArmed = false;
bool gIsMuted = false;
bool gTestButtonPressed = false;
bool gMuteButtonPressed = false;
bool gStartupAudioDelayInitialized = false;
bool gPowerUpRedOnBeepPlayed = false;
bool gPowerUpRedOffBeepPlayed = false;
bool gUnpoweredTestBeepQueued = false;
float gSelfTestRemainingSeconds = 0.0f;
float gPowerUpElapsedSeconds = 0.0f;
float gUnpoweredTestHeldSeconds = 0.0f;
float gUnpoweredTestAudioOverrideSeconds = 0.0f;
float gStartupAudioDelayRemainingSeconds = 0.0f;
float gAudioDelayRemainingSeconds = 0.0f;
float gMuteMinimumRemainingSeconds = 0.0f;
float gBeepRepeatDelayRemainingSeconds = 0.0f;
float gAoaRateDegreesPerSecond = 0.0f;
float gLastDrivingAoaDegrees = 0.0f;
float gLastDisplayAoaUnits = 0.0f;
float gRawAoaDegrees = 0.0f;
float gFilteredAoaDegrees = 0.0f;
float gRawAoaUnits = 0.0f;
float gFilteredAoaUnits = 0.0f;
float gStallWarningRatio = 0.0f;
float gSpeedLiftReserveRatio = 0.0f;
float gSpeedProxyAoaUnits = 0.0f;
float gRawSpeedProxyAoaUnits = 0.0f;
float gAeroLiftSupportRatio = 1.0f;
float gGearLoadRatio = 0.0f;
float gAircraftEmptyMassKg = 0.0f;
float gAircraftFuelMassKg = 0.0f;
float gAircraftPayloadMassKg = 0.0f;
float gAircraftCurrentMassKg = 0.0f;
float gAircraftMaxMassKg = 0.0f;
float gWeightAdjustedVsoKts = 0.0f;
float gFlapRatio = 0.0f;
float gFlapRequestRatio = 0.0f;
float gAoaDeltaPerFrame = 0.0f;
float gAoaUnitsDeltaPerFrame = 0.0f;
float gAoaUnitsRatePerSecond = 0.0f;
uint32_t gNewlyCrossedThresholdMask = 0U;
bool gHasLastDrivingAoa = false;
bool gHasLastDisplayAoaUnits = false;
bool gHasFilteredAoa = false;
bool gOnGround = false;
AudioAlertMode gAudioAlertMode = AudioAlertMode::None;
PcmSample gBeepSample = {};
FMOD_CHANNEL* gActiveBeepChannel = nullptr;
bool gBeepChannelActive = false;
bool gBeepCompletionPending = false;
int gPendingEventBeeps = 0;
DisplayScenario gCurrentScenario = DisplayScenario::PowerOff;
DisplayScenario gLastLoggedScenario = DisplayScenario::PowerOff;
bool gHasLoggedScenario = false;
const char* gDrivingAoaSourceName = "none";
bool gPermanentFailMode = false;
float gFailAnimationElapsedSeconds = 0.0f;
float gCalibrationGraceRemainingSeconds = kCalibrationGraceSeconds;
int gValidationFailureCount = 0;
std::string gFailReason;
std::string gPendingValidationFailureReason;

void CopyPluginString(char* destination, const char* source)
{
    if (destination == nullptr)
    {
        return;
    }

    strcpy_s(destination, 256, source);
}

void DebugLog(const char* message)
{
    if (!kDebugLogging)
    {
        return;
    }

    XPLMDebugString("Garmin GI260: ");
    XPLMDebugString(message);
    XPLMDebugString("\n");
}

void DebugLogFormatted(const char* format, ...)
{
    if (!kDebugLogging)
    {
        return;
    }

    char buffer[1024] = {};
    va_list args;
    va_start(args, format);
    vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
    va_end(args);
    DebugLog(buffer);
}

std::string PathToUtf8(const std::filesystem::path& path)
{
    return path.u8string();
}

std::string Trim(const std::string& value)
{
    const char* whitespace = " \t\r\n";
    const size_t first = value.find_first_not_of(whitespace);
    if (first == std::string::npos)
    {
        return {};
    }

    const size_t last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1);
}

float ParseFloatOrDefault(const std::string& value, float fallbackValue)
{
    try
    {
        size_t parsed = 0;
        const float result = std::stof(value, &parsed);
        return parsed > 0 ? result : fallbackValue;
    }
    catch (...)
    {
        return fallbackValue;
    }
}

bool ParseBoolOrDefault(const std::string& value, bool fallbackValue)
{
    std::string normalized = value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    normalized = Trim(normalized);
    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on")
    {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off")
    {
        return false;
    }
    return fallbackValue;
}

std::string ParseStringOrDefault(const std::string& value, const std::string& fallbackValue)
{
    std::string stripped = value;
    const size_t commentPosition = stripped.find(';');
    if (commentPosition != std::string::npos)
    {
        stripped = stripped.substr(0, commentPosition);
    }

    stripped = Trim(stripped);
    return stripped.empty() ? fallbackValue : stripped;
}

void ApplyConfigValue(const std::string& section, const std::string& key, const std::string& value)
{
    if (section == "DataRefs")
    {
        if (key == "power_dataref")
        {
            gConfig.powerDataRefName = ParseStringOrDefault(value, gConfig.powerDataRefName);
        }
        else if (key == "night_lighting_dataref")
        {
            gConfig.nightLightingDataRefName = ParseStringOrDefault(value, gConfig.nightLightingDataRefName);
        }
        else if (key == "night_background_lit_blend_max")
        {
            gConfig.nightBackgroundBlendMax = std::clamp(ParseFloatOrDefault(value, gConfig.nightBackgroundBlendMax), 0.0f, 1.0f);
        }
        else if (key == "night_led_black_mask_max")
        {
            gConfig.nightLedBlackMaskMax = std::clamp(ParseFloatOrDefault(value, gConfig.nightLedBlackMaskMax), 0.0f, 1.0f);
        }
        else if (key == "night_button_black_mask_max")
        {
            gConfig.nightButtonBlackMaskMax = std::clamp(ParseFloatOrDefault(value, gConfig.nightButtonBlackMaskMax), 0.0f, 1.0f);
        }
    }
    else if (section == "System")
    {
        if (key == "initial_status")
        {
            gConfig.initialStatus = ParseBoolOrDefault(value, gConfig.initialStatus);
        }
        else if (key == "arm_ias_kts")
        {
            gConfig.armIasKts = std::max(0.0f, ParseFloatOrDefault(value, gConfig.armIasKts));
        }
        else if (key == "force_warning_on_stall_warning")
        {
            gConfig.forceWarningOnStallWarning = ParseBoolOrDefault(value, gConfig.forceWarningOnStallWarning);
        }
        else if (key == "audio_arm_delay_seconds")
        {
            gConfig.audioArmDelaySeconds = std::max(0.0f, ParseFloatOrDefault(value, gConfig.audioArmDelaySeconds));
        }
        else if (key == "audio_startup_delay_seconds")
        {
            gConfig.audioStartupDelaySeconds = std::max(0.0f, ParseFloatOrDefault(value, gConfig.audioStartupDelaySeconds));
        }
        else if (key == "audio_gain")
        {
            gConfig.audioGain = std::clamp(ParseFloatOrDefault(value, gConfig.audioGain), 0.0f, 2.0f);
        }
        else if (key == "slow_beep_repeat_gap_seconds")
        {
            gConfig.slowBeepRepeatGapSeconds = std::max(0.0f, ParseFloatOrDefault(value, gConfig.slowBeepRepeatGapSeconds));
        }
        else if (key == "fast_beep_repeat_gap_seconds")
        {
            gConfig.fastBeepRepeatGapSeconds = std::max(0.0f, ParseFloatOrDefault(value, gConfig.fastBeepRepeatGapSeconds));
        }
        else if (key == "fail_beep_repeat_gap_seconds")
        {
            gConfig.failBeepRepeatGapSeconds = std::max(0.0f, ParseFloatOrDefault(value, gConfig.failBeepRepeatGapSeconds));
        }
        else if (key == "event_beep_gap_seconds")
        {
            gConfig.eventBeepGapSeconds = std::max(0.0f, ParseFloatOrDefault(value, gConfig.eventBeepGapSeconds));
        }
        else if (key == "mute_minimum_seconds")
        {
            gConfig.muteMinimumSeconds = std::max(0.0f, ParseFloatOrDefault(value, gConfig.muteMinimumSeconds));
        }
        else if (key == "aoa_smoothing_seconds")
        {
            gConfig.aoaSmoothingSeconds = std::max(0.0f, ParseFloatOrDefault(value, gConfig.aoaSmoothingSeconds));
        }
        else if (key == "power_up_step_seconds")
        {
            gConfig.powerUpStepSeconds = std::max(0.01f, ParseFloatOrDefault(value, gConfig.powerUpStepSeconds));
        }
        else if (key == "power_up_rise_seconds")
        {
            gConfig.powerUpRiseSeconds = std::max(0.1f, ParseFloatOrDefault(value, gConfig.powerUpRiseSeconds));
        }
        else if (key == "power_up_fall_seconds")
        {
            gConfig.powerUpFallSeconds = std::max(0.1f, ParseFloatOrDefault(value, gConfig.powerUpFallSeconds));
        }
        else if (key == "self_test_seconds")
        {
            gConfig.selfTestSeconds = std::max(0.1f, ParseFloatOrDefault(value, gConfig.selfTestSeconds));
        }
        else if (key == "unpowered_test_long_press_seconds")
        {
            gConfig.unpoweredTestLongPressSeconds = std::max(0.1f, ParseFloatOrDefault(value, gConfig.unpoweredTestLongPressSeconds));
        }
    }
    else if (section == "AircraftReference")
    {
        if (key == "aircraft_vso_kts")
        {
            gConfig.aircraftVsoKts = ParseFloatOrDefault(value, gConfig.aircraftVsoKts);
        }
        else if (key == "aircraft_warning_aoa_deg")
        {
            gConfig.aircraftWarningAoaDeg = ParseFloatOrDefault(value, gConfig.aircraftWarningAoaDeg);
        }
        else if (key == "normalized_zero_aoa_deg")
        {
            gConfig.normalizedZeroAoaDeg = ParseFloatOrDefault(value, gConfig.normalizedZeroAoaDeg);
        }
        else if (key == "approach_speed_vso_multiplier")
        {
            gConfig.approachSpeedVsoMultiplier = std::max(1.01f, ParseFloatOrDefault(value, gConfig.approachSpeedVsoMultiplier));
        }
    }
    else if (section == "Calibration")
    {
        if (key == "module_hysteresis_deg") gConfig.moduleHysteresisDeg = std::max(0.0f, ParseFloatOrDefault(value, gConfig.moduleHysteresisDeg));
        else if (key == "red_upper_aoa_deg") gConfig.redUpperDeg = ParseFloatOrDefault(value, gConfig.redUpperDeg);
        else if (key == "red_lower_aoa_deg") gConfig.redLowerDeg = ParseFloatOrDefault(value, gConfig.redLowerDeg);
        else if (key == "yellow_upper_chevron_aoa_deg") gConfig.yellowUpperChevronDeg = ParseFloatOrDefault(value, gConfig.yellowUpperChevronDeg);
        else if (key == "yellow_lower_chevron_aoa_deg") gConfig.yellowLowerChevronDeg = ParseFloatOrDefault(value, gConfig.yellowLowerChevronDeg);
        else if (key == "yellow_split_bar_aoa_deg") gConfig.yellowSplitBarDeg = ParseFloatOrDefault(value, gConfig.yellowSplitBarDeg);
        else if (key == "yellow_solid_bar_aoa_deg") gConfig.yellowSolidBarDeg = ParseFloatOrDefault(value, gConfig.yellowSolidBarDeg);
        else if (key == "green_split_bar_aoa_deg") gConfig.greenSplitBarDeg = ParseFloatOrDefault(value, gConfig.greenSplitBarDeg);
        else if (key == "green_circle_aoa_deg") gConfig.greenCircleDeg = ParseFloatOrDefault(value, gConfig.greenCircleDeg);
        else if (key == "green_solid_upper_aoa_deg") gConfig.greenSolidUpperDeg = ParseFloatOrDefault(value, gConfig.greenSolidUpperDeg);
        else if (key == "green_solid_middle_aoa_deg") gConfig.greenSolidMiddleDeg = ParseFloatOrDefault(value, gConfig.greenSolidMiddleDeg);
        else if (key == "green_solid_lower_aoa_deg") gConfig.greenSolidLowerDeg = ParseFloatOrDefault(value, gConfig.greenSolidLowerDeg);
    }
}

void LoadConfig()
{
    const std::filesystem::path configPath = gPluginRoot / "config.ini";
    std::ifstream configFile(configPath);
    if (!configFile)
    {
        DebugLogFormatted("config.ini not found, using defaults: %s", PathToUtf8(configPath).c_str());
        return;
    }

    std::string section;
    std::string line;
    while (std::getline(configFile, line))
    {
        const size_t commentPosition = line.find_first_of(";#");
        if (commentPosition != std::string::npos)
        {
            line = line.substr(0, commentPosition);
        }

        line = Trim(line);
        if (line.empty())
        {
            continue;
        }

        if (line.front() == '[' && line.back() == ']')
        {
            section = Trim(line.substr(1, line.size() - 2));
            continue;
        }

        const size_t equalsPosition = line.find('=');
        if (equalsPosition == std::string::npos)
        {
            continue;
        }

        const std::string key = Trim(line.substr(0, equalsPosition));
        const std::string value = Trim(line.substr(equalsPosition + 1));
        ApplyConfigValue(section, key, value);
    }
}

float ReadDataRefFloat(XPLMDataRef dataRef, float fallbackValue);
bool ReadOnGroundState();
void UpdateGroundLiftSupportState();
void StopActiveBeep();
void QueueEventBeeps(int count, bool stopActiveBeep);
void QueueBeepBeepEvent();
void QueueSelfTestBeepEvent();
bool ReadPowerState();
void ClearModules();
float CriticalAoaDegrees();
float ApproachAoaDegrees();

void LogAlwaysFormatted(const char* format, ...)
{
    char buffer[1024] = {};
    va_list args;
    va_start(args, format);
    vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
    va_end(args);
    XPLMDebugString("Garmin GI260: ");
    XPLMDebugString(buffer);
    XPLMDebugString("\n");
}

float& ModuleThreshold(ModuleId id)
{
    switch (id)
    {
    case ModuleId::RED_UPPER_CHEVRON: return gConfig.redUpperDeg;
    case ModuleId::RED_LOWER_CHEVRON: return gConfig.redLowerDeg;
    case ModuleId::YELLOW_UPPER_CHEVRON: return gConfig.yellowUpperChevronDeg;
    case ModuleId::YELLOW_LOWER_CHEVRON: return gConfig.yellowLowerChevronDeg;
    case ModuleId::YELLOW_SPLIT_BAR: return gConfig.yellowSplitBarDeg;
    case ModuleId::YELLOW_SOLID_BAR: return gConfig.yellowSolidBarDeg;
    case ModuleId::GREEN_SPLIT_BAR: return gConfig.greenSplitBarDeg;
    case ModuleId::GREEN_CIRCLE: return gConfig.greenCircleDeg;
    case ModuleId::GREEN_SOLID_UPPER: return gConfig.greenSolidUpperDeg;
    case ModuleId::GREEN_SOLID_MIDDLE: return gConfig.greenSolidMiddleDeg;
    case ModuleId::GREEN_SOLID_LOWER: return gConfig.greenSolidLowerDeg;
    case ModuleId::Count: break;
    }
    return gConfig.greenSolidLowerDeg;
}

float ModuleThresholdValue(ModuleId id)
{
    return ModuleThreshold(id);
}

const char* ModuleShortName(ModuleId id)
{
    switch (id)
    {
    case ModuleId::RED_UPPER_CHEVRON: return "RU";
    case ModuleId::RED_LOWER_CHEVRON: return "RL";
    case ModuleId::YELLOW_UPPER_CHEVRON: return "YU";
    case ModuleId::YELLOW_LOWER_CHEVRON: return "YL";
    case ModuleId::YELLOW_SPLIT_BAR: return "YS";
    case ModuleId::YELLOW_SOLID_BAR: return "YB";
    case ModuleId::GREEN_SPLIT_BAR: return "GS";
    case ModuleId::GREEN_CIRCLE: return "GC";
    case ModuleId::GREEN_SOLID_UPPER: return "GU";
    case ModuleId::GREEN_SOLID_MIDDLE: return "GM";
    case ModuleId::GREEN_SOLID_LOWER: return "GL";
    case ModuleId::Count: break;
    }
    return "--";
}

const char* ScenarioName(DisplayScenario scenario)
{
    switch (scenario)
    {
    case DisplayScenario::PowerOff: return "POWER OFF";
    case DisplayScenario::SelfTest: return "SELF TEST";
    case DisplayScenario::PowerUpSweep: return "POWER-UP SWEEP";
    case DisplayScenario::Initializing: return "INITIALIZING - CALIBRATION DATA";
    case DisplayScenario::Fail: return "SELF-TEST FAIL - DATAREF/CALIBRATION";
    case DisplayScenario::Disarmed: return "UNARMED - BELOW 50 KIAS";
    case DisplayScenario::UnpoweredTest: return "POWER-OFF TEST";
    case DisplayScenario::Cruise: return "CRUISE - NO APPROACH REFERENCE";
    case DisplayScenario::MinimumVisibleAoa: return "MINIMUM VISIBLE AOA";
    case DisplayScenario::LowAoaReference: return "LOW AOA + APPROACH REFERENCE";
    case DisplayScenario::ApproachAoa: return "APPROACH AOA";
    case DisplayScenario::HighAoa: return "HIGH AOA - YELLOW CAUTION";
    case DisplayScenario::WarningAoa: return "WARNING AOA - RED";
    case DisplayScenario::ExtremeWarningAoa: return "EXTREME WARNING AOA - UPPER RED";
    }
    return "UNKNOWN";
}

const char* AudioAlertModeName(AudioAlertMode mode)
{
    switch (mode)
    {
    case AudioAlertMode::None: return "none";
    case AudioAlertMode::SlowBeep: return "slow beep requested";
    case AudioAlertMode::FastBeep: return "fast beep requested";
    case AudioAlertMode::FailBeep: return "fail beep requested";
    }
    return "unknown";
}

void SetScenario(DisplayScenario scenario)
{
    gCurrentScenario = scenario;
    if (!gHasLoggedScenario || gLastLoggedScenario != scenario)
    {
        LogAlwaysFormatted("scenario=%s raw_aoa=%.2f filtered_aoa=%.2f norm_units=%.2f source=%s",
            ScenarioName(scenario),
            gRawAoaDegrees,
            gFilteredAoaDegrees,
            gFilteredAoaUnits,
            gDrivingAoaSourceName);
        gLastLoggedScenario = scenario;
        gHasLoggedScenario = true;
    }
}

bool IsFinitePositive(float value)
{
    return std::isfinite(value) && value > 0.0f;
}

float ClampIasForLogic(float iasKnots)
{
    return std::isfinite(iasKnots) ? std::max(0.0f, iasKnots) : 0.0f;
}

void ResetCalibrationState()
{
    StopActiveBeep();
    gWasPowered = false;
    gIsArmed = false;
    gAudioDelayRemainingSeconds = 0.0f;
    gStartupAudioDelayRemainingSeconds = 0.0f;
    gStartupAudioDelayInitialized = false;
    gMuteMinimumRemainingSeconds = 0.0f;
    gIsMuted = false;
    gBeepRepeatDelayRemainingSeconds = 0.0f;
    gSelfTestRemainingSeconds = 0.0f;
    gPowerUpElapsedSeconds = 0.0f;
    gPowerUpRedOnBeepPlayed = false;
    gPowerUpRedOffBeepPlayed = false;
    gUnpoweredTestHeldSeconds = 0.0f;
    gUnpoweredTestAudioOverrideSeconds = 0.0f;
    gUnpoweredTestBeepQueued = false;
    gAoaRateDegreesPerSecond = 0.0f;
    gAoaDeltaPerFrame = 0.0f;
    gAoaUnitsDeltaPerFrame = 0.0f;
    gAoaUnitsRatePerSecond = 0.0f;
    gLastDrivingAoaDegrees = 0.0f;
    gLastDisplayAoaUnits = 0.0f;
    gRawAoaDegrees = 0.0f;
    gFilteredAoaDegrees = 0.0f;
    gRawAoaUnits = 0.0f;
    gFilteredAoaUnits = 0.0f;
    gStallWarningRatio = 0.0f;
    gSpeedLiftReserveRatio = 0.0f;
    gSpeedProxyAoaUnits = 0.0f;
    gRawSpeedProxyAoaUnits = 0.0f;
    gAeroLiftSupportRatio = 1.0f;
    gGearLoadRatio = 0.0f;
    gAircraftEmptyMassKg = 0.0f;
    gAircraftFuelMassKg = 0.0f;
    gAircraftPayloadMassKg = 0.0f;
    gAircraftCurrentMassKg = 0.0f;
    gAircraftMaxMassKg = 0.0f;
    gWeightAdjustedVsoKts = 0.0f;
    gFlapRatio = 0.0f;
    gFlapRequestRatio = 0.0f;
    gNewlyCrossedThresholdMask = 0U;
    gHasLastDrivingAoa = false;
    gHasLastDisplayAoaUnits = false;
    gHasFilteredAoa = false;
    gOnGround = false;
    gPermanentFailMode = false;
    gPendingEventBeeps = 0;
    gBeepCompletionPending = false;
    gFailAnimationElapsedSeconds = 0.0f;
    gCalibrationGraceRemainingSeconds = kCalibrationGraceSeconds;
    gValidationFailureCount = 0;
    gFailReason.clear();
    gPendingValidationFailureReason.clear();
    ClearModules();
    SetScenario(ReadPowerState() ? DisplayScenario::PowerUpSweep : DisplayScenario::PowerOff);
}

void LatchPermanentFail(const char* format, ...)
{
    if (gPermanentFailMode)
    {
        return;
    }

    char buffer[256] = {};
    va_list args;
    va_start(args, format);
    vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
    va_end(args);

    gPermanentFailMode = true;
    gFailReason = buffer;
    LogAlwaysFormatted("PERMANENT FAIL: %s", gFailReason.c_str());
}

const std::array<ModuleId, static_cast<size_t>(ModuleId::Count)>& BottomToTopModules()
{
    static const std::array<ModuleId, static_cast<size_t>(ModuleId::Count)> order = {{
        ModuleId::GREEN_SOLID_LOWER,
        ModuleId::GREEN_SOLID_MIDDLE,
        ModuleId::GREEN_SOLID_UPPER,
        ModuleId::GREEN_CIRCLE,
        ModuleId::GREEN_SPLIT_BAR,
        ModuleId::YELLOW_SOLID_BAR,
        ModuleId::YELLOW_SPLIT_BAR,
        ModuleId::YELLOW_LOWER_CHEVRON,
        ModuleId::YELLOW_UPPER_CHEVRON,
        ModuleId::RED_LOWER_CHEVRON,
        ModuleId::RED_UPPER_CHEVRON,
    }};
    return order;
}

const std::array<ModuleId, 10>& BottomToTopLadderModules()
{
    static const std::array<ModuleId, 10> order = {{
        ModuleId::GREEN_SOLID_LOWER,
        ModuleId::GREEN_SOLID_MIDDLE,
        ModuleId::GREEN_SOLID_UPPER,
        ModuleId::GREEN_SPLIT_BAR,
        ModuleId::YELLOW_SOLID_BAR,
        ModuleId::YELLOW_SPLIT_BAR,
        ModuleId::YELLOW_LOWER_CHEVRON,
        ModuleId::YELLOW_UPPER_CHEVRON,
        ModuleId::RED_LOWER_CHEVRON,
        ModuleId::RED_UPPER_CHEVRON,
    }};
    return order;
}

const std::array<ModuleId, 4>& GreenBarModules()
{
    static const std::array<ModuleId, 4> modules = {{
        ModuleId::GREEN_SOLID_LOWER,
        ModuleId::GREEN_SOLID_MIDDLE,
        ModuleId::GREEN_SOLID_UPPER,
        ModuleId::GREEN_SPLIT_BAR,
    }};
    return modules;
}

void ValidateModuleThresholds()
{
    // Keep the configured 10-LED ladder monotonic even if config.ini is edited.
    const auto& order = BottomToTopLadderModules();
    float previous = ModuleThresholdValue(order.front());
    for (size_t index = 1; index < order.size(); ++index)
    {
        float& current = ModuleThreshold(order[index]);
        if (current <= previous)
        {
            const float repaired = previous + 0.01f;
            LogAlwaysFormatted(
                "threshold order repaired for %s: %.2f -> %.2f",
                ModuleShortName(order[index]),
                current,
                repaired);
            current = repaired;
        }
        previous = current;
    }
}

void ReadAircraftReferenceValues()
{
    // Aircraft-author values are the calibration anchors. Vso is adjusted by
    // current mass; stall warning alpha defines the upper calibration end.
    if (gAircraftVsoDataRef != nullptr)
    {
        gConfig.aircraftVsoKts = ReadDataRefFloat(gAircraftVsoDataRef, 0.0f);
    }

    if (gAircraftStallWarnAlphaDataRef != nullptr)
    {
        gConfig.aircraftWarningAoaDeg = ReadDataRefFloat(gAircraftStallWarnAlphaDataRef, 0.0f);
    }

    gAircraftEmptyMassKg = ReadDataRefFloat(gAircraftEmptyMassKgDataRef, gAircraftEmptyMassKg);
    gAircraftFuelMassKg = ReadDataRefFloat(gAircraftFuelMassKgDataRef, gAircraftFuelMassKg);
    gAircraftPayloadMassKg = ReadDataRefFloat(gPayloadMassKgDataRef, gAircraftPayloadMassKg);
    gAircraftMaxMassKg = ReadDataRefFloat(gAircraftMaxMassKgDataRef, gAircraftMaxMassKg);
    gFlapRatio = ReadDataRefFloat(gFlapRatioDataRef, gFlapRatio);
    gFlapRequestRatio = ReadDataRefFloat(gFlapRequestRatioDataRef, gFlapRequestRatio);
    gOnGround = ReadOnGroundState();

    const float totalMassKg = ReadDataRefFloat(gTotalMassKgDataRef, 0.0f);
    if (totalMassKg > 0.0f)
    {
        gAircraftCurrentMassKg = totalMassKg;
    }
    else
    {
        const float summedMassKg = gAircraftEmptyMassKg + gAircraftFuelMassKg + gAircraftPayloadMassKg;
        if (summedMassKg > 0.0f)
        {
            gAircraftCurrentMassKg = summedMassKg;
        }
    }

    if (gConfig.aircraftVsoKts > 0.0f && gAircraftCurrentMassKg > 0.0f && gAircraftMaxMassKg > 0.0f)
    {
        const float weightRatio = std::clamp(gAircraftCurrentMassKg / gAircraftMaxMassKg, 0.25f, 2.0f);
        gWeightAdjustedVsoKts = gConfig.aircraftVsoKts * std::sqrt(weightRatio);
    }
    else
    {
        gWeightAdjustedVsoKts = gConfig.aircraftVsoKts;
    }

    UpdateGroundLiftSupportState();
}

bool ValidateCalibrationSources(float iasKnots)
{
    std::string failureReason;
    if (gIasDataRef == nullptr)
    {
        failureReason = "missing IAS dataref";
    }
    else if (!std::isfinite(iasKnots) || iasKnots > 1000.0f)
    {
        char buffer[128] = {};
        sprintf_s(buffer, "bad IAS value %.1f kt", iasKnots);
        failureReason = buffer;
    }
    else if (gAircraftVsoDataRef == nullptr ||
             !std::isfinite(gConfig.aircraftVsoKts) ||
             gConfig.aircraftVsoKts < 5.0f ||
             gConfig.aircraftVsoKts > 800.0f)
    {
        char buffer[128] = {};
        sprintf_s(buffer, "bad acf_Vso %.1f kt", gConfig.aircraftVsoKts);
        failureReason = buffer;
    }
    else if (!IsFinitePositive(gAircraftCurrentMassKg) || !IsFinitePositive(gAircraftMaxMassKg))
    {
        char buffer[128] = {};
        sprintf_s(buffer, "bad mass current/max %.1f/%.1f kg", gAircraftCurrentMassKg, gAircraftMaxMassKg);
        failureReason = buffer;
    }
    else
    {
        const float weightRatio = gAircraftCurrentMassKg / gAircraftMaxMassKg;
        if (!std::isfinite(weightRatio) || weightRatio <= 0.05f || weightRatio > 2.0f)
        {
            char buffer[128] = {};
            sprintf_s(buffer, "bad weight ratio %.3f", weightRatio);
            failureReason = buffer;
        }
        else if (!std::isfinite(gWeightAdjustedVsoKts) ||
                 gWeightAdjustedVsoKts < 5.0f ||
                 gWeightAdjustedVsoKts > 800.0f)
        {
            char buffer[128] = {};
            sprintf_s(buffer, "bad weight-adjusted Vso %.1f kt", gWeightAdjustedVsoKts);
            failureReason = buffer;
        }
        else if (gStallWarningRatioDataRef == nullptr)
        {
            failureReason = "missing stall_warning_ratio dataref";
        }
        else if (!std::isfinite(gStallWarningRatio))
        {
            char buffer[128] = {};
            sprintf_s(buffer, "bad stall_warning_ratio %.2f", gStallWarningRatio);
            failureReason = buffer;
        }
        else if (gAircraftStallWarnAlphaDataRef == nullptr ||
                 !std::isfinite(gConfig.aircraftWarningAoaDeg) ||
                 gConfig.aircraftWarningAoaDeg <= gConfig.normalizedZeroAoaDeg + 0.1f ||
                 gConfig.aircraftWarningAoaDeg > 90.0f)
        {
            char buffer[128] = {};
            sprintf_s(buffer, "bad stall warning alpha %.1f deg", gConfig.aircraftWarningAoaDeg);
            failureReason = buffer;
        }
        else if (!std::isfinite(ApproachAoaDegrees()) ||
                 ApproachAoaDegrees() <= gConfig.normalizedZeroAoaDeg ||
                 ApproachAoaDegrees() >= CriticalAoaDegrees())
        {
            char buffer[128] = {};
            sprintf_s(buffer, "bad approach alpha %.1f deg", ApproachAoaDegrees());
            failureReason = buffer;
        }
        else if (gFlapRatioDataRef == nullptr)
        {
            failureReason = "missing flap ratio dataref";
        }
        else if (!std::isfinite(gFlapRatio) || gFlapRatio < -0.05f || gFlapRatio > 1.2f)
        {
            char buffer[128] = {};
            sprintf_s(buffer, "bad flap ratio %.2f", gFlapRatio);
            failureReason = buffer;
        }
        else if (gFlapRequestRatioDataRef != nullptr && (!std::isfinite(gFlapRequestRatio) || gFlapRequestRatio < -0.05f || gFlapRequestRatio > 1.2f))
        {
            char buffer[128] = {};
            sprintf_s(buffer, "bad requested flap ratio %.2f", gFlapRequestRatio);
            failureReason = buffer;
        }
        else if (gOnGroundAnyDataRef == nullptr && gGearOnGroundDataRef == nullptr)
        {
            failureReason = "missing weight-on-wheels dataref";
        }
        else if (gAeroNormalForceDataRef == nullptr)
        {
            failureReason = "missing aero normal force dataref";
        }
        else if (gGearNormalForceDataRef == nullptr)
        {
            failureReason = "missing gear normal force dataref";
        }
        else if (!std::isfinite(gAeroLiftSupportRatio) || gAeroLiftSupportRatio < 0.0f || gAeroLiftSupportRatio > 1.2f)
        {
            char buffer[128] = {};
            sprintf_s(buffer, "bad aero lift support ratio %.2f", gAeroLiftSupportRatio);
            failureReason = buffer;
        }
    }

    if (failureReason.empty())
    {
        gValidationFailureCount = 0;
        gPendingValidationFailureReason.clear();
        return true;
    }

    if (gPendingValidationFailureReason != failureReason)
    {
        gValidationFailureCount = 0;
    }
    gPendingValidationFailureReason = failureReason;
    if (gCalibrationGraceRemainingSeconds > 0.0f)
    {
        return false;
    }

    ++gValidationFailureCount;
    if (gValidationFailureCount >= kValidationFailuresBeforeFail)
    {
        LatchPermanentFail("%s", failureReason.c_str());
    }

    return !gPermanentFailMode;
}

void DebugLogPath(const char* label, const std::filesystem::path& path)
{
    DebugLogFormatted("%s%s", label, PathToUtf8(path).c_str());
}

uint16_t ReadLe16(const uint8_t* data)
{
    return static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(data[1] << 8U);
}

uint32_t ReadLe32(const uint8_t* data)
{
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8U) |
           (static_cast<uint32_t>(data[2]) << 16U) |
           (static_cast<uint32_t>(data[3]) << 24U);
}

bool LoadPcm16Wav(const std::filesystem::path& wavPath, PcmSample& sample)
{
    sample = {};
    std::ifstream file(wavPath, std::ios::binary);
    if (!file)
    {
        sample.error = "missing Beep.wav";
        return false;
    }

    std::vector<uint8_t> bytes(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    if (bytes.size() < 44 ||
        std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
    {
        sample.error = "Beep.wav is not RIFF/WAVE";
        return false;
    }

    bool foundFormat = false;
    bool foundData = false;
    uint16_t audioFormat = 0;
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    size_t dataOffset = 0;
    uint32_t dataSize = 0;

    size_t offset = 12;
    while (offset + 8 <= bytes.size())
    {
        const uint8_t* chunk = bytes.data() + offset;
        const uint32_t chunkSize = ReadLe32(chunk + 4);
        const size_t payloadOffset = offset + 8;
        if (payloadOffset + chunkSize > bytes.size())
        {
            sample.error = "Beep.wav has a truncated chunk";
            return false;
        }

        if (std::memcmp(chunk, "fmt ", 4) == 0)
        {
            if (chunkSize < 16)
            {
                sample.error = "Beep.wav fmt chunk too small";
                return false;
            }
            const uint8_t* fmt = bytes.data() + payloadOffset;
            audioFormat = ReadLe16(fmt);
            channels = ReadLe16(fmt + 2);
            sampleRate = ReadLe32(fmt + 4);
            bitsPerSample = ReadLe16(fmt + 14);
            foundFormat = true;
        }
        else if (std::memcmp(chunk, "data", 4) == 0)
        {
            dataOffset = payloadOffset;
            dataSize = chunkSize;
            foundData = true;
        }

        offset = payloadOffset + chunkSize + (chunkSize & 1U);
    }

    if (!foundFormat || !foundData)
    {
        sample.error = "Beep.wav missing fmt/data chunk";
        return false;
    }
    if (audioFormat != 1 || bitsPerSample != 16 || channels < 1 || channels > 2 || sampleRate == 0)
    {
        sample.error = "Beep.wav must be PCM16 mono/stereo";
        return false;
    }
    if (dataSize == 0 || dataSize > std::numeric_limits<uint32_t>::max())
    {
        sample.error = "Beep.wav has no PCM data";
        return false;
    }

    sample.pcmData.assign(bytes.begin() + static_cast<std::ptrdiff_t>(dataOffset),
                          bytes.begin() + static_cast<std::ptrdiff_t>(dataOffset + dataSize));
    sample.sampleRateHz = static_cast<int>(sampleRate);
    sample.channelCount = static_cast<int>(channels);
    sample.loaded = true;
    return true;
}

void DebugLogGlError(const char* label)
{
    if (!kDebugLogging)
    {
        return;
    }

    GLenum error = glGetError();
    if (error == GL_NO_ERROR)
    {
        DebugLogFormatted("%s GL_NO_ERROR", label);
        return;
    }

    while (error != GL_NO_ERROR)
    {
        DebugLogFormatted("%s GL error 0x%04X", label, static_cast<unsigned int>(error));
        error = glGetError();
    }
}

void UpdateMenuChecks()
{
    if (gMenu == nullptr)
    {
        return;
    }

    if (gToggleMenuItem >= 0)
    {
        XPLMCheckMenuItem(gMenu, gToggleMenuItem, gToggleEnabled ? xplm_Menu_Checked : xplm_Menu_Unchecked);
    }
    if (gBorderMenuItem >= 0)
    {
        XPLMCheckMenuItem(gMenu, gBorderMenuItem, gBorderEnabled ? xplm_Menu_Checked : xplm_Menu_Unchecked);
    }
    if (gLockPositionMenuItem >= 0)
    {
        XPLMCheckMenuItem(gMenu, gLockPositionMenuItem, gLockPositionEnabled ? xplm_Menu_Checked : xplm_Menu_Unchecked);
    }
    if (gDebugOverlayMenuItem >= 0)
    {
        XPLMCheckMenuItem(gMenu, gDebugOverlayMenuItem, gDebugOverlayEnabled ? xplm_Menu_Checked : xplm_Menu_Unchecked);
    }
}

std::wstring ToWidePath(const std::filesystem::path& path)
{
    const std::string utf8Path = path.u8string();
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, nullptr, 0);
    if (size <= 0)
    {
        return {};
    }

    std::wstring widePath(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, widePath.data(), size);
    return widePath;
}

template <typename T>
void ReleaseCom(T*& pointer)
{
    if (pointer != nullptr)
    {
        pointer->Release();
        pointer = nullptr;
    }
}

bool LoadImageRgba(const std::filesystem::path& path, std::vector<unsigned char>& pixels, int& width, int& height)
{
    const std::wstring widePath = ToWidePath(path);
    if (widePath.empty())
    {
        DebugLogPath("WIC path conversion failed: ", path);
        return false;
    }

    const HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(initResult);

    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(result))
    {
        result = factory->CreateDecoderFromFilename(
            widePath.c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnLoad,
            &decoder);
    }
    if (SUCCEEDED(result))
    {
        result = decoder->GetFrame(0, &frame);
    }
    if (SUCCEEDED(result))
    {
        result = factory->CreateFormatConverter(&converter);
    }
    if (SUCCEEDED(result))
    {
        result = converter->Initialize(
            frame,
            GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
    }

    UINT imageWidth = 0;
    UINT imageHeight = 0;
    if (SUCCEEDED(result))
    {
        result = converter->GetSize(&imageWidth, &imageHeight);
    }

    if (SUCCEEDED(result) && imageWidth > 0 && imageHeight > 0)
    {
        const UINT stride = imageWidth * 4;
        const UINT byteCount = stride * imageHeight;
        pixels.resize(byteCount);
        result = converter->CopyPixels(nullptr, stride, byteCount, pixels.data());
        width = static_cast<int>(imageWidth);
        height = static_cast<int>(imageHeight);
    }

    ReleaseCom(converter);
    ReleaseCom(frame);
    ReleaseCom(decoder);
    ReleaseCom(factory);

    if (shouldUninitialize)
    {
        CoUninitialize();
    }

    if (FAILED(result) || pixels.empty())
    {
        DebugLogFormatted("WIC load failed path='%s' result=0x%08X", PathToUtf8(path).c_str(), static_cast<unsigned int>(result));
        return false;
    }

    return true;
}

std::filesystem::path FindTexturePath(const char* fileName)
{
    return gPluginRoot / "Textures" / fileName;
}

bool EnsureTextureLoaded(TextureAsset& texture)
{
    if (texture.textureId != 0)
    {
        return true;
    }
    if (texture.loadAttempted)
    {
        return false;
    }

    texture.loadAttempted = true;

    const std::filesystem::path texturePath = FindTexturePath(texture.fileName);
    std::error_code error;
    if (!std::filesystem::is_regular_file(texturePath, error))
    {
        DebugLogPath("texture file missing: ", texturePath);
        return false;
    }

    std::vector<unsigned char> pixels;
    if (!LoadImageRgba(texturePath, pixels, texture.width, texture.height))
    {
        DebugLogPath("texture load failed: ", texturePath);
        return false;
    }

    XPLMGenerateTextureNumbers(&texture.textureId, 1);
    XPLMBindTexture2d(texture.textureId, 0);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        texture.width,
        texture.height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels.data());
    DebugLogGlError("after texture upload");

    return true;
}

void DeleteTexture(TextureAsset& texture)
{
    if (texture.textureId != 0)
    {
        const GLuint textureId = static_cast<GLuint>(texture.textureId);
        glDeleteTextures(1, &textureId);
        texture.textureId = 0;
    }

    texture.width = 0;
    texture.height = 0;
    texture.loadAttempted = false;
}

void DeleteAllTextures()
{
    DeleteTexture(gBackgroundTexture);
    DeleteTexture(gNightBackgroundTexture);
    for (auto& module : gModules)
    {
        DeleteTexture(module.texture);
    }
    DeleteTexture(gTestPressedTexture);
    DeleteTexture(gMutePressedTexture);
}

size_t ModuleIndex(ModuleId id)
{
    return static_cast<size_t>(id);
}

void SetModule(ModuleId id, bool visible)
{
    gModules[ModuleIndex(id)].visible = visible;
}

bool IsModuleVisible(ModuleId id)
{
    return gModules[ModuleIndex(id)].visible;
}

uint32_t ModuleMask(ModuleId id)
{
    return 1U << ModuleIndex(id);
}

void SetModuleByThreshold(ModuleId id, float aoaDegrees, float thresholdDegrees)
{
    const bool wasVisible = IsModuleVisible(id);
    const float turnOffThreshold = thresholdDegrees - gConfig.moduleHysteresisDeg;
    SetModule(id, wasVisible ? aoaDegrees >= turnOffThreshold : aoaDegrees >= thresholdDegrees);
}

void ClearModules()
{
    for (auto& module : gModules)
    {
        module.visible = false;
    }
}

void SetAllModules(bool visible)
{
    for (auto& module : gModules)
    {
        module.visible = visible;
    }
}

float ReadDataRefFloat(XPLMDataRef dataRef, float fallbackValue)
{
    return dataRef != nullptr ? XPLMGetDataf(dataRef) : fallbackValue;
}

float CriticalAoaDegrees()
{
    return std::max(gConfig.normalizedZeroAoaDeg + 0.1f, gConfig.aircraftWarningAoaDeg);
}

float ApproachAoaUnits()
{
    return gConfig.greenSplitBarDeg;
}

float ApproachReferenceSpeedKts()
{
    const float vsoKts = gWeightAdjustedVsoKts > 0.0f ? gWeightAdjustedVsoKts : gConfig.aircraftVsoKts;
    return vsoKts > 0.0f ? vsoKts * gConfig.approachSpeedVsoMultiplier : 0.0f;
}

float ApproachReferenceAoaUnits(float iasKnots)
{
    (void)iasKnots;

    if (gConfig.greenCircleDeg > 0.0f)
    {
        return gConfig.greenCircleDeg;
    }

    return gConfig.greenSolidLowerDeg;
}

bool IsLandingFlapCalibrated()
{
    return gFlapRatio >= 0.95f;
}

float ApproachAoaDegrees()
{
    const float zeroAoaDegrees = gConfig.normalizedZeroAoaDeg;
    const float warningAoaDegrees = CriticalAoaDegrees();
    const float multiplier = std::max(1.01f, gConfig.approachSpeedVsoMultiplier);
    return zeroAoaDegrees + (warningAoaDegrees - zeroAoaDegrees) / (multiplier * multiplier);
}

float NormalizeAoaDegreesToUnits(float aoaDegrees)
{
    // Global-plugin approximation: X-Plane does not expose a universal transform
    // between every aircraft's measured alpha dataref and Plane Maker's stall
    // warning alpha. We assume both are compatible degrees for calibration.
    const float zeroAoaDegrees = gConfig.normalizedZeroAoaDeg;
    const float approachAoaDegrees = ApproachAoaDegrees();
    const float warningAoaDegrees = CriticalAoaDegrees();
    const float approachUnits = ApproachAoaUnits();
    const float warningUnits = gConfig.redLowerDeg;

    if (aoaDegrees <= approachAoaDegrees)
    {
        const float span = std::max(0.1f, approachAoaDegrees - zeroAoaDegrees);
        return std::max(0.0f, (aoaDegrees - zeroAoaDegrees) * approachUnits / span);
    }

    const float span = std::max(0.1f, warningAoaDegrees - approachAoaDegrees);
    return approachUnits + (aoaDegrees - approachAoaDegrees) * (warningUnits - approachUnits) / span;
}

float NormalizeStallWarningRatioToUnits(float ratio)
{
    if (ratio <= 0.01f)
    {
        return 0.0f;
    }

    const float approachRatio = 1.0f / std::pow(std::max(1.01f, gConfig.approachSpeedVsoMultiplier), 2.0f);
    const float approachUnits = ApproachAoaUnits();
    const float warningUnits = gConfig.redLowerDeg;

    if (ratio <= approachRatio)
    {
        return std::max(0.0f, ratio * approachUnits / approachRatio);
    }

    const float span = std::max(0.01f, 1.0f - approachRatio);
    return approachUnits + (ratio - approachRatio) * (warningUnits - approachUnits) / span;
}

float ComputeSpeedLiftReserveRatio(float iasKnots)
{
    const float vsoKts = gWeightAdjustedVsoKts > 0.0f ? gWeightAdjustedVsoKts : gConfig.aircraftVsoKts;
    if (!std::isfinite(iasKnots) || !std::isfinite(vsoKts) || iasKnots <= 1.0f || vsoKts <= 1.0f)
    {
        return 0.0f;
    }

    const float ratio = vsoKts / iasKnots;
    return std::max(0.0f, ratio * ratio);
}

float ComputeSpeedProxyAoaUnits(float iasKnots)
{
    // Main normalized ladder source: Vso/IAS maps approach speed to the top
    // green bar. On the ground, actual lift/gear forces attenuate the proxy so
    // rollout and takeoff roll are not driven by IAS alone.
    gSpeedLiftReserveRatio = ComputeSpeedLiftReserveRatio(iasKnots);
    gRawSpeedProxyAoaUnits = NormalizeStallWarningRatioToUnits(gSpeedLiftReserveRatio);
    return gOnGround ? gRawSpeedProxyAoaUnits * gAeroLiftSupportRatio : gRawSpeedProxyAoaUnits;
}

bool ReadOnGroundState()
{
    if (gOnGroundAnyDataRef != nullptr && XPLMGetDatai(gOnGroundAnyDataRef) != 0)
    {
        return true;
    }

    if (gGearOnGroundDataRef != nullptr)
    {
        int wheelOnGround[10] = {};
        const int count = XPLMGetDatavi(gGearOnGroundDataRef, wheelOnGround, 0, 10);
        for (int index = 0; index < count; ++index)
        {
            if (wheelOnGround[index] != 0)
            {
                return true;
            }
        }
    }

    return false;
}

void UpdateGroundLiftSupportState()
{
    gAeroLiftSupportRatio = gOnGround ? 0.0f : 1.0f;
    gGearLoadRatio = 0.0f;

    if (!std::isfinite(gAircraftCurrentMassKg) || gAircraftCurrentMassKg <= 1.0f)
    {
        return;
    }

    const float aircraftWeightNewtons = gAircraftCurrentMassKg * 9.80665f;
    if (aircraftWeightNewtons <= 1.0f)
    {
        return;
    }

    const float aeroNormalForceNewtons = ReadDataRefFloat(gAeroNormalForceDataRef, 0.0f);
    const float gearNormalForceNewtons = ReadDataRefFloat(gGearNormalForceDataRef, 0.0f);
    const float aeroSupport = std::clamp(std::max(0.0f, aeroNormalForceNewtons) / aircraftWeightNewtons, 0.0f, 1.2f);
    gGearLoadRatio = std::clamp(std::fabs(gearNormalForceNewtons) / aircraftWeightNewtons, 0.0f, 1.2f);

    if (!gOnGround)
    {
        gAeroLiftSupportRatio = 1.0f;
        return;
    }

    // On weight-on-wheels, preserve the calibrated speed/Vso ladder shape but
    // scale it by actual simulated lift support. During takeoff this grows as
    // the wing unloads the gear; after touchdown it decays as lift dumps away.
    const float gearUnloadedSupport = std::clamp(1.0f - gGearLoadRatio, 0.0f, 1.0f);
    gAeroLiftSupportRatio = std::clamp(std::max(aeroSupport, gearUnloadedSupport), 0.0f, 1.0f);
}

bool ReadPowerState()
{
    return gPowerDataRef == nullptr || XPLMGetDatai(gPowerDataRef) != 0;
}

bool IsStallWarningActive()
{
    const bool annunciator = gStallWarningDataRef != nullptr && XPLMGetDatai(gStallWarningDataRef) != 0;
    const bool ratio = gStallWarningRatioDataRef != nullptr && gStallWarningRatio > 0.99f;
    return annunciator || ratio;
}

float MasterRadioVolumeRatio()
{
    const float ratio = ReadDataRefFloat(gRadioVolumeRatioDataRef, 1.0f);
    return std::clamp(std::isfinite(ratio) ? ratio : 1.0f, 0.0f, 1.0f);
}

float EffectiveAudioVolume()
{
    return std::clamp(gConfig.audioGain, 0.0f, 2.0f) * MasterRadioVolumeRatio();
}

void OnBeepComplete(void* refcon, FMOD_RESULT status)
{
    (void)refcon;
    (void)status;
    gActiveBeepChannel = nullptr;
    gBeepChannelActive = false;
    gBeepCompletionPending = true;
}

void StopActiveBeep()
{
    if (gActiveBeepChannel != nullptr)
    {
        FMOD_CHANNEL* channel = gActiveBeepChannel;
        gActiveBeepChannel = nullptr;
        gBeepChannelActive = false;
        XPLMStopAudio(channel);
        gBeepCompletionPending = false;
    }
}

bool StartBeep()
{
    if (!gBeepSample.loaded || gBeepSample.pcmData.empty() || gBeepChannelActive)
    {
        return false;
    }

    gBeepCompletionPending = false;
    gActiveBeepChannel = XPLMPlayPCMOnBus(
        gBeepSample.pcmData.data(),
        static_cast<uint32_t>(gBeepSample.pcmData.size()),
        FMOD_SOUND_FORMAT_PCM16,
        gBeepSample.sampleRateHz,
        gBeepSample.channelCount,
        0,
        xplm_AudioInterior,
        OnBeepComplete,
        nullptr);
    if (gActiveBeepChannel == nullptr)
    {
        gBeepChannelActive = false;
        return false;
    }

    gBeepChannelActive = true;
    XPLMSetAudioVolume(gActiveBeepChannel, EffectiveAudioVolume());
    return true;
}

float RepeatGapForMode(AudioAlertMode mode)
{
    switch (mode)
    {
    case AudioAlertMode::SlowBeep: return gConfig.slowBeepRepeatGapSeconds;
    case AudioAlertMode::FastBeep: return gConfig.fastBeepRepeatGapSeconds;
    case AudioAlertMode::FailBeep: return gConfig.failBeepRepeatGapSeconds;
    case AudioAlertMode::None: break;
    }
    return 0.0f;
}

bool AoaAudioAllowed(bool powered)
{
    return powered &&
           gIsArmed &&
           !gIsMuted &&
           gStartupAudioDelayRemainingSeconds <= 0.0f &&
           gAudioDelayRemainingSeconds <= 0.0f;
}

void QueueEventBeeps(int count, bool stopActiveBeep)
{
    if (stopActiveBeep)
    {
        StopActiveBeep();
    }

    gPendingEventBeeps = std::max(gPendingEventBeeps, std::max(0, count));
    gBeepRepeatDelayRemainingSeconds = 0.0f;
    gBeepCompletionPending = false;
}

void QueueBeepBeepEvent()
{
    QueueEventBeeps(2, false);
}

void QueueSelfTestBeepEvent()
{
    // The TEST button owns the audio lane: play one confirmation beep, then
    // suppress continuous alert beeps until the visual self-test completes.
    QueueEventBeeps(1, true);
}

void UpdateManualMuteState(float elapsedSeconds)
{
    gMuteMinimumRemainingSeconds = std::max(
        0.0f,
        gMuteMinimumRemainingSeconds - std::max(0.0f, elapsedSeconds));
    gIsMuted = gMuteMinimumRemainingSeconds > 0.0f;
}

AudioAlertMode ContinuousAudioMode(bool powered)
{
    if (gSelfTestRemainingSeconds > 0.0f)
    {
        return AudioAlertMode::None;
    }

    if (powered && gPermanentFailMode && !gIsMuted && gStartupAudioDelayRemainingSeconds <= 0.0f)
    {
        return AudioAlertMode::FailBeep;
    }

    if (!AoaAudioAllowed(powered))
    {
        return AudioAlertMode::None;
    }

    return gAudioAlertMode;
}

void UpdateAudioPlayback(float elapsedSeconds, bool powered)
{
    gUnpoweredTestAudioOverrideSeconds = std::max(
        0.0f,
        gUnpoweredTestAudioOverrideSeconds - std::max(0.0f, elapsedSeconds));

    if (gActiveBeepChannel != nullptr)
    {
        XPLMSetAudioVolume(gActiveBeepChannel, EffectiveAudioVolume());
    }

    const AudioAlertMode continuousMode = ContinuousAudioMode(powered);
    const bool eventAllowed = (powered || gUnpoweredTestAudioOverrideSeconds > 0.0f) && !gIsMuted;
    if (!eventAllowed && continuousMode == AudioAlertMode::None)
    {
        StopActiveBeep();
        gPendingEventBeeps = 0;
        gBeepRepeatDelayRemainingSeconds = 0.0f;
        gBeepCompletionPending = false;
        return;
    }

    if (gBeepChannelActive)
    {
        return;
    }

    if (gBeepCompletionPending)
    {
        gBeepCompletionPending = false;
        gBeepRepeatDelayRemainingSeconds =
            (gPendingEventBeeps > 0 && eventAllowed)
                ? gConfig.eventBeepGapSeconds
                : RepeatGapForMode(continuousMode);
    }

    gBeepRepeatDelayRemainingSeconds = std::max(0.0f, gBeepRepeatDelayRemainingSeconds - elapsedSeconds);
    if (gBeepRepeatDelayRemainingSeconds > 0.0f)
    {
        return;
    }

    if (gPendingEventBeeps > 0 && eventAllowed)
    {
        if (StartBeep())
        {
            --gPendingEventBeeps;
        }
        return;
    }

    if (continuousMode != AudioAlertMode::None)
    {
        StartBeep();
    }
}

float ReadDrivingAoaDegrees()
{
    return ReadDataRefFloat(gAoaDataRef, 0.0f);
}

const char* GetDrivingAoaSourceName()
{
    return gDrivingAoaSourceName;
}

void UpdateAoaValues(float elapsedSeconds)
{
    // Blend the calibrated speed/Vso ladder with X-Plane's stall-warning ratio.
    // Raw AoA degrees remain visible in debug but are not the primary ladder
    // driver because some aircraft report wrapped/static values on the ground.
    gRawAoaDegrees = ReadDrivingAoaDegrees();
    gStallWarningRatio = ReadDataRefFloat(gStallWarningRatioDataRef, -1.0f);
    const float iasKnots = ClampIasForLogic(ReadDataRefFloat(gIasDataRef, 0.0f));
    gSpeedProxyAoaUnits = ComputeSpeedProxyAoaUnits(iasKnots);

    const float stallRatioForDisplay = std::isfinite(gStallWarningRatio) ? std::max(0.0f, gStallWarningRatio) : 0.0f;
    float targetAoaUnits = std::max(NormalizeStallWarningRatioToUnits(stallRatioForDisplay), gSpeedProxyAoaUnits);
    if (gConfig.forceWarningOnStallWarning && IsStallWarningActive())
    {
        targetAoaUnits = std::max(targetAoaUnits, gConfig.redLowerDeg);
    }
    targetAoaUnits = std::clamp(targetAoaUnits, 0.0f, gConfig.redUpperDeg);

    gRawAoaUnits = targetAoaUnits;
    if (!gHasFilteredAoa || gConfig.aoaSmoothingSeconds <= 0.0f)
    {
        gFilteredAoaDegrees = gRawAoaDegrees;
        gFilteredAoaUnits = targetAoaUnits;
        gHasFilteredAoa = true;
        return;
    }

    const float alpha = std::clamp(elapsedSeconds / gConfig.aoaSmoothingSeconds, 0.0f, 1.0f);
    if (std::isfinite(gRawAoaDegrees))
    {
        gFilteredAoaDegrees += (gRawAoaDegrees - gFilteredAoaDegrees) * alpha;
    }
    gFilteredAoaUnits += (targetAoaUnits - gFilteredAoaUnits) * alpha;
}

const char* GetDisplayStateName()
{
    return ScenarioName(gCurrentScenario);
}

void UpdateAoaRate(float elapsedSeconds)
{
    if (elapsedSeconds > 0.001f && gHasLastDrivingAoa)
    {
        gAoaDeltaPerFrame = gFilteredAoaDegrees - gLastDrivingAoaDegrees;
        gAoaRateDegreesPerSecond = gAoaDeltaPerFrame / elapsedSeconds;
    }
    else
    {
        gAoaDeltaPerFrame = 0.0f;
        gAoaRateDegreesPerSecond = 0.0f;
    }

    if (elapsedSeconds > 0.001f && gHasLastDisplayAoaUnits)
    {
        gAoaUnitsDeltaPerFrame = gFilteredAoaUnits - gLastDisplayAoaUnits;
        gAoaUnitsRatePerSecond = gAoaUnitsDeltaPerFrame / elapsedSeconds;
    }
    else
    {
        gAoaUnitsDeltaPerFrame = 0.0f;
        gAoaUnitsRatePerSecond = 0.0f;
    }

    gLastDrivingAoaDegrees = gFilteredAoaDegrees;
    gLastDisplayAoaUnits = gFilteredAoaUnits;
    gHasLastDrivingAoa = true;
    gHasLastDisplayAoaUnits = true;
}

size_t CountVisibleModules()
{
    size_t count = 0;
    for (const auto& module : gModules)
    {
        if (module.visible)
        {
            ++count;
        }
    }
    return count;
}

bool IsGreenBarVisible(ModuleId id)
{
    return IsModuleVisible(id) && id != ModuleId::GREEN_CIRCLE;
}

size_t CountVisibleGreenBars()
{
    size_t count = 0;
    for (const ModuleId id : GreenBarModules())
    {
        if (IsGreenBarVisible(id))
        {
            ++count;
        }
    }
    return count;
}

DisplayScenario DetermineNormalScenario()
{
    if (IsModuleVisible(ModuleId::RED_UPPER_CHEVRON))
    {
        return DisplayScenario::ExtremeWarningAoa;
    }
    if (IsModuleVisible(ModuleId::RED_LOWER_CHEVRON))
    {
        return DisplayScenario::WarningAoa;
    }
    if (IsModuleVisible(ModuleId::YELLOW_UPPER_CHEVRON) ||
        IsModuleVisible(ModuleId::YELLOW_LOWER_CHEVRON) ||
        IsModuleVisible(ModuleId::YELLOW_SPLIT_BAR) ||
        IsModuleVisible(ModuleId::YELLOW_SOLID_BAR))
    {
        return DisplayScenario::HighAoa;
    }

    const size_t greenBars = CountVisibleGreenBars();
    const bool approachReferenceVisible = IsModuleVisible(ModuleId::GREEN_CIRCLE);
    if (approachReferenceVisible && greenBars == GreenBarModules().size())
    {
        return DisplayScenario::ApproachAoa;
    }
    if (approachReferenceVisible)
    {
        return DisplayScenario::LowAoaReference;
    }
    if (greenBars > 0)
    {
        return DisplayScenario::MinimumVisibleAoa;
    }
    return DisplayScenario::Cruise;
}

ModuleId HighestActiveModule()
{
    for (const auto& module : gModules)
    {
        if (module.visible)
        {
            return module.id;
        }
    }
    return ModuleId::Count;
}

std::string ModuleMaskToNames(uint32_t mask)
{
    std::string result;
    for (const ModuleId id : BottomToTopModules())
    {
        if ((mask & ModuleMask(id)) == 0U)
        {
            continue;
        }
        if (!result.empty())
        {
            result += ",";
        }
        result += ModuleShortName(id);
    }
    return result.empty() ? std::string("none") : result;
}

bool UpdateArming(float iasKnots, bool powered, float elapsedSeconds)
{
    // Manual behavior: once the unit arms above 50 KIAS, visual annunciation
    // remains available until power/reset. There is no taxi-speed auto-disarm.
    if (!powered)
    {
        gIsArmed = false;
        gAudioDelayRemainingSeconds = 0.0f;
        return false;
    }

    const bool shouldArm = gConfig.armIasKts <= 0.0f || iasKnots > gConfig.armIasKts;
    // The GI 260 arms after exceeding 50 KIAS. Treat that as a session latch,
    // otherwise a light aircraft can lose all warning annunciation while slowing
    // through 50 KIAS toward an actual stall.
    if (shouldArm && !gIsArmed)
    {
        gIsArmed = true;
        gAudioDelayRemainingSeconds = gConfig.audioArmDelaySeconds;
    }

    if (gIsArmed && gAudioDelayRemainingSeconds > 0.0f)
    {
        gAudioDelayRemainingSeconds = std::max(0.0f, gAudioDelayRemainingSeconds - elapsedSeconds);
    }

    return gIsArmed;
}

bool ApplyPowerUpSweep(float elapsedSeconds)
{
    const auto& order = BottomToTopModules();
    const size_t moduleCount = order.size();
    const float riseSeconds = std::max(0.1f, gConfig.powerUpRiseSeconds);
    const float fallSeconds = std::max(0.1f, gConfig.powerUpFallSeconds);
    const float totalSweepSeconds = riseSeconds + fallSeconds;
    if (gPowerUpElapsedSeconds >= totalSweepSeconds)
    {
        return false;
    }

    gPowerUpElapsedSeconds = std::min(totalSweepSeconds, gPowerUpElapsedSeconds + std::max(0.0f, elapsedSeconds));

    size_t visibleCount = 0;
    if (gPowerUpElapsedSeconds <= riseSeconds)
    {
        const float progress = std::clamp(gPowerUpElapsedSeconds / riseSeconds, 0.0f, 1.0f);
        visibleCount = std::min(moduleCount, static_cast<size_t>(std::ceil(progress * static_cast<float>(moduleCount))));
    }
    else
    {
        const float progress = std::clamp((gPowerUpElapsedSeconds - riseSeconds) / fallSeconds, 0.0f, 1.0f);
        const size_t hiddenCount = std::min(moduleCount, static_cast<size_t>(std::floor(progress * static_cast<float>(moduleCount))));
        visibleCount = moduleCount - hiddenCount;
    }

    ClearModules();
    for (size_t index = 0; index < visibleCount; ++index)
    {
        SetModule(order[index], true);
    }

    const bool lowerRedVisible = IsModuleVisible(ModuleId::RED_LOWER_CHEVRON);
    const bool anyRedVisible = lowerRedVisible || IsModuleVisible(ModuleId::RED_UPPER_CHEVRON);
    if (!gPowerUpRedOnBeepPlayed && lowerRedVisible)
    {
        QueueEventBeeps(1, false);
        gPowerUpRedOnBeepPlayed = true;
    }
    if (gPowerUpRedOnBeepPlayed &&
        !gPowerUpRedOffBeepPlayed &&
        gPowerUpElapsedSeconds > riseSeconds &&
        !anyRedVisible)
    {
        QueueEventBeeps(1, false);
        gPowerUpRedOffBeepPlayed = true;
    }

    return true;
}

void ApplyUnpoweredTestAnimation(float elapsedSeconds)
{
    gUnpoweredTestHeldSeconds += std::max(0.0f, elapsedSeconds);
    gFailAnimationElapsedSeconds += std::max(0.0f, elapsedSeconds);
    const int phase = static_cast<int>(std::floor(gFailAnimationElapsedSeconds / kFailFlashSeconds));

    ClearModules();
    SetModule((phase % 2) == 0 ? ModuleId::RED_UPPER_CHEVRON : ModuleId::RED_LOWER_CHEVRON, true);
    gNewlyCrossedThresholdMask = 0U;
    gAudioAlertMode = AudioAlertMode::None;
    SetScenario(DisplayScenario::UnpoweredTest);

    if (!gUnpoweredTestBeepQueued &&
        gUnpoweredTestHeldSeconds >= gConfig.unpoweredTestLongPressSeconds)
    {
        QueueEventBeeps(3, true);
        gUnpoweredTestAudioOverrideSeconds = kUnpoweredTestAudioOverrideSeconds;
        gUnpoweredTestBeepQueued = true;
    }
}

void ApplyFailAnimation(float elapsedSeconds)
{
    gFailAnimationElapsedSeconds += std::max(0.0f, elapsedSeconds);
    const int phase = static_cast<int>(std::floor(gFailAnimationElapsedSeconds / kFailFlashSeconds));

    ClearModules();
    SetModule((phase % 2) == 0 ? ModuleId::RED_UPPER_CHEVRON : ModuleId::RED_LOWER_CHEVRON, true);
    gNewlyCrossedThresholdMask = 0U;
    gAudioAlertMode = AudioAlertMode::FailBeep;
    SetScenario(DisplayScenario::Fail);
}

void ApplyNormalAoaDisplay(float aoaUnits, float iasKnots)
{
    // Annunciators are cumulative. The green circle is intentionally handled as
    // a separate Approach AOA Reference, not as one of the 10 ladder segments.
    const float previousAoaUnits = aoaUnits - gAoaUnitsDeltaPerFrame;
    gNewlyCrossedThresholdMask = 0U;
    for (const ModuleId id : BottomToTopLadderModules())
    {
        const float threshold = ModuleThresholdValue(id);
        if (previousAoaUnits < threshold && aoaUnits >= threshold)
        {
            gNewlyCrossedThresholdMask |= ModuleMask(id);
        }
    }

    SetModuleByThreshold(ModuleId::GREEN_SOLID_LOWER, aoaUnits, gConfig.greenSolidLowerDeg);
    SetModuleByThreshold(ModuleId::GREEN_SOLID_MIDDLE, aoaUnits, gConfig.greenSolidMiddleDeg);
    SetModuleByThreshold(ModuleId::GREEN_SOLID_UPPER, aoaUnits, gConfig.greenSolidUpperDeg);
    // The Garmin manual treats the green circle as the Approach AOA Reference,
    // not as one of the four green bar annunciators.
    SetModuleByThreshold(ModuleId::GREEN_CIRCLE, aoaUnits, ApproachReferenceAoaUnits(iasKnots));
    SetModuleByThreshold(ModuleId::GREEN_SPLIT_BAR, aoaUnits, gConfig.greenSplitBarDeg);
    SetModuleByThreshold(ModuleId::YELLOW_SOLID_BAR, aoaUnits, gConfig.yellowSolidBarDeg);
    SetModuleByThreshold(ModuleId::YELLOW_SPLIT_BAR, aoaUnits, gConfig.yellowSplitBarDeg);
    SetModuleByThreshold(ModuleId::YELLOW_LOWER_CHEVRON, aoaUnits, gConfig.yellowLowerChevronDeg);
    SetModuleByThreshold(ModuleId::YELLOW_UPPER_CHEVRON, aoaUnits, gConfig.yellowUpperChevronDeg);
    SetModuleByThreshold(ModuleId::RED_LOWER_CHEVRON, aoaUnits, gConfig.redLowerDeg);
    SetModuleByThreshold(ModuleId::RED_UPPER_CHEVRON, aoaUnits, gConfig.redUpperDeg);
}

void UpdateAudioAlertState()
{
    // AOA alert audio is muted after startup/arming and begins only at the
    // manual-defined alert thresholds: upper yellow for slow, red for fast.
    if (!gIsArmed ||
        gStartupAudioDelayRemainingSeconds > 0.0f ||
        gAudioDelayRemainingSeconds > 0.0f ||
        gIsMuted)
    {
        gAudioAlertMode = AudioAlertMode::None;
        return;
    }

    if (IsModuleVisible(ModuleId::RED_LOWER_CHEVRON) || IsModuleVisible(ModuleId::RED_UPPER_CHEVRON))
    {
        gAudioAlertMode = AudioAlertMode::FastBeep;
    }
    else if (IsModuleVisible(ModuleId::YELLOW_UPPER_CHEVRON))
    {
        gAudioAlertMode = AudioAlertMode::SlowBeep;
    }
    else
    {
        gAudioAlertMode = AudioAlertMode::None;
    }
}

void UpdateIndicatorState(float elapsedSeconds)
{
    const bool powered = ReadPowerState();
    if (!powered)
    {
        if (gTestButtonPressed)
        {
            ApplyUnpoweredTestAnimation(elapsedSeconds);
        }
        else
        {
            ClearModules();
            SetScenario(DisplayScenario::PowerOff);
            gUnpoweredTestHeldSeconds = 0.0f;
            gUnpoweredTestBeepQueued = false;
        }
        gWasPowered = false;
        gStartupAudioDelayInitialized = false;
        gStartupAudioDelayRemainingSeconds = 0.0f;
        gPowerUpElapsedSeconds = 0.0f;
        gPowerUpRedOnBeepPlayed = false;
        gPowerUpRedOffBeepPlayed = false;
        gSelfTestRemainingSeconds = 0.0f;
        gAudioAlertMode = AudioAlertMode::None;
        gAoaRateDegreesPerSecond = 0.0f;
        gAoaDeltaPerFrame = 0.0f;
        gAoaUnitsRatePerSecond = 0.0f;
        gAoaUnitsDeltaPerFrame = 0.0f;
        gNewlyCrossedThresholdMask = 0U;
        gHasLastDrivingAoa = false;
        gHasLastDisplayAoaUnits = false;
        gHasFilteredAoa = false;
        UpdateArming(0.0f, false, elapsedSeconds);
        return;
    }

    if (!gStartupAudioDelayInitialized)
    {
        gStartupAudioDelayRemainingSeconds = gConfig.audioStartupDelaySeconds;
        gStartupAudioDelayInitialized = true;
    }
    gStartupAudioDelayRemainingSeconds = std::max(0.0f, gStartupAudioDelayRemainingSeconds - elapsedSeconds);

    ReadAircraftReferenceValues();
    UpdateAoaValues(elapsedSeconds);
    UpdateAoaRate(elapsedSeconds);
    const float rawIasKnots = ReadDataRefFloat(gIasDataRef, 0.0f);
    const float iasKnots = ClampIasForLogic(rawIasKnots);
    gCalibrationGraceRemainingSeconds = std::max(0.0f, gCalibrationGraceRemainingSeconds - elapsedSeconds);

    if (gPermanentFailMode)
    {
        ApplyFailAnimation(elapsedSeconds);
        return;
    }
    if (!ValidateCalibrationSources(rawIasKnots))
    {
        ClearModules();
        SetScenario(DisplayScenario::Initializing);
        gAudioAlertMode = AudioAlertMode::None;
        return;
    }

    if (!gWasPowered)
    {
        gWasPowered = true;
        gPowerUpElapsedSeconds = 0.0f;
        gPowerUpRedOnBeepPlayed = false;
        gPowerUpRedOffBeepPlayed = false;
        gFailAnimationElapsedSeconds = 0.0f;
    }

    UpdateArming(iasKnots, true, elapsedSeconds);

    if (gSelfTestRemainingSeconds > 0.0f)
    {
        gSelfTestRemainingSeconds = std::max(0.0f, gSelfTestRemainingSeconds - elapsedSeconds);
        SetAllModules(true);
        SetScenario(DisplayScenario::SelfTest);
        gNewlyCrossedThresholdMask = 0U;
        gAudioAlertMode = AudioAlertMode::None;
        return;
    }

    if (ApplyPowerUpSweep(elapsedSeconds))
    {
        SetScenario(DisplayScenario::PowerUpSweep);
        gNewlyCrossedThresholdMask = 0U;
        gAudioAlertMode = AudioAlertMode::None;
        return;
    }

    if (!gIsArmed)
    {
        ClearModules();
        SetScenario(DisplayScenario::Disarmed);
        gNewlyCrossedThresholdMask = 0U;
        gAudioAlertMode = AudioAlertMode::None;
        return;
    }

    ApplyNormalAoaDisplay(gFilteredAoaUnits, iasKnots);
    SetScenario(DetermineNormalScenario());
    UpdateAudioAlertState();
}

void GetAspectFitRect(int left, int top, int right, int bottom, int& drawLeft, int& drawTop, int& drawRight, int& drawBottom)
{
    const int windowWidth = std::max(1, right - left);
    const int windowHeight = std::max(1, top - bottom);
    int drawWidth = windowWidth;
    int drawHeight = static_cast<int>(static_cast<float>(drawWidth) / kDesignAspect);

    if (drawHeight > windowHeight)
    {
        drawHeight = windowHeight;
        drawWidth = static_cast<int>(static_cast<float>(drawHeight) * kDesignAspect);
    }

    drawLeft = left + (windowWidth - drawWidth) / 2;
    drawRight = drawLeft + drawWidth;
    drawBottom = bottom + (windowHeight - drawHeight) / 2;
    drawTop = drawBottom + drawHeight;
}

int RectWidth(const Rect& rect)
{
    return std::abs(rect.right - rect.left);
}

int RectHeight(const Rect& rect)
{
    return std::abs(rect.top - rect.bottom);
}

Rect GetFloatingGeometry()
{
    Rect rect = {};
    if (gWindow != nullptr)
    {
        XPLMGetWindowGeometry(gWindow, &rect.left, &rect.top, &rect.right, &rect.bottom);
    }
    return rect;
}

Rect GetOsGeometry()
{
    Rect rect = {};
    if (gWindow != nullptr)
    {
        XPLMGetWindowGeometryOS(gWindow, &rect.left, &rect.top, &rect.right, &rect.bottom);
    }
    return rect;
}

void SetFloatingGeometry(const Rect& rect)
{
    XPLMSetWindowGeometry(gWindow, rect.left, rect.top, rect.right, rect.bottom);
    gLastFloatingGeometry = rect;
    gHasLastFloatingGeometry = true;
}

void SetOsGeometry(const Rect& rect)
{
    XPLMSetWindowGeometryOS(gWindow, rect.left, rect.top, rect.right, rect.bottom);
    gLastOsGeometry = rect;
    gHasLastOsGeometry = true;
}

Rect CorrectAspectRatio(const Rect& rect, const Rect& lastRect, bool hasLastRect)
{
    const int width = std::max(1, RectWidth(rect));
    const int height = std::max(1, RectHeight(rect));
    const int desiredHeightFromWidth = std::max(1, static_cast<int>(std::lround(static_cast<double>(width) * kDesignHeight / kDesignWidth)));
    const int desiredWidthFromHeight = std::max(1, static_cast<int>(std::lround(static_cast<double>(height) * kDesignWidth / kDesignHeight)));

    if (std::abs(height - desiredHeightFromWidth) <= 1)
    {
        return rect;
    }

    int correctedWidth = width;
    int correctedHeight = desiredHeightFromWidth;

    if (hasLastRect)
    {
        const int lastWidth = std::max(1, RectWidth(lastRect));
        const int lastHeight = std::max(1, RectHeight(lastRect));
        const double widthChange = std::abs(static_cast<double>(width - lastWidth)) / static_cast<double>(lastWidth);
        const double heightChange = std::abs(static_cast<double>(height - lastHeight)) / static_cast<double>(lastHeight);

        if (heightChange > widthChange)
        {
            correctedWidth = desiredWidthFromHeight;
            correctedHeight = height;
        }
    }

    Rect corrected = rect;
    corrected.right = rect.left + correctedWidth;
    corrected.bottom = rect.top + ((rect.bottom >= rect.top) ? correctedHeight : -correctedHeight);
    return corrected;
}

void CaptureLockPositionOffset()
{
    if (gWindow == nullptr || XPLMWindowIsPoppedOut(gWindow))
    {
        gHasLockedOffset = false;
        return;
    }

    int screenLeft = 0;
    int screenTop = 0;
    int screenRight = 0;
    int screenBottom = 0;
    XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);
    (void)screenRight;
    (void)screenBottom;
    (void)screenRight;
    (void)screenBottom;

    const Rect rect = GetFloatingGeometry();
    gLockedOffsetFromLeft = rect.left - screenLeft;
    gLockedOffsetFromTop = screenTop - rect.top;
    gHasLockedOffset = true;
}

void ApplyLockedPosition(Rect& rect)
{
    if (!gLockPositionEnabled || !gHasLockedOffset || gWindow == nullptr || XPLMWindowIsPoppedOut(gWindow))
    {
        return;
    }

    int screenLeft = 0;
    int screenTop = 0;
    int screenRight = 0;
    int screenBottom = 0;
    XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);
    (void)screenRight;
    (void)screenBottom;

    const int width = std::max(1, RectWidth(rect));
    const int height = std::max(1, RectHeight(rect));
    rect.left = screenLeft + gLockedOffsetFromLeft;
    rect.top = screenTop - gLockedOffsetFromTop;
    rect.right = rect.left + width;
    rect.bottom = rect.top - height;
}

void EnforceWindowGeometry()
{
    if (gWindow == nullptr)
    {
        return;
    }

    if (XPLMWindowIsPoppedOut(gWindow))
    {
        const Rect rect = GetOsGeometry();
        const Rect corrected = CorrectAspectRatio(rect, gLastOsGeometry, gHasLastOsGeometry);
        if (corrected.left != rect.left || corrected.top != rect.top || corrected.right != rect.right || corrected.bottom != rect.bottom)
        {
            SetOsGeometry(corrected);
        }
        else
        {
            gLastOsGeometry = rect;
            gHasLastOsGeometry = true;
        }
        return;
    }

    Rect rect = GetFloatingGeometry();
    Rect corrected = CorrectAspectRatio(rect, gLastFloatingGeometry, gHasLastFloatingGeometry);
    ApplyLockedPosition(corrected);

    if (corrected.left != rect.left || corrected.top != rect.top || corrected.right != rect.right || corrected.bottom != rect.bottom)
    {
        SetFloatingGeometry(corrected);
    }
    else
    {
        gLastFloatingGeometry = rect;
        gHasLastFloatingGeometry = true;
    }
}

void DrawPlaceholder(int left, int top, int right, int bottom);

float ReadNightLightingRatio()
{
    if (gNightLightingDataRef == nullptr)
    {
        return 0.0f;
    }

    const float ratio = ReadDataRefFloat(gNightLightingDataRef, 0.0f);
    return std::clamp(std::isfinite(ratio) ? ratio : 0.0f, 0.0f, 1.0f);
}

void DrawTexturedQuad(
    int textureId,
    float left,
    float top,
    float right,
    float bottom,
    float alpha = 1.0f,
    float red = 1.0f,
    float green = 1.0f,
    float blue = 1.0f)
{
    XPLMSetGraphicsState(0, 1, 0, 0, 1, 0, 0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    XPLMBindTexture2d(textureId, 0);

    glColor4f(red, green, blue, std::clamp(alpha, 0.0f, 1.0f));
    const GLfloat vertices[] = {
        static_cast<GLfloat>(left), static_cast<GLfloat>(bottom),
        static_cast<GLfloat>(right), static_cast<GLfloat>(bottom),
        static_cast<GLfloat>(right), static_cast<GLfloat>(top),
        static_cast<GLfloat>(left), static_cast<GLfloat>(top),
    };
    const GLfloat texCoords[] = {
        0.0f, 1.0f,
        1.0f, 1.0f,
        1.0f, 0.0f,
        0.0f, 0.0f,
    };

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, vertices);
    glTexCoordPointer(2, GL_FLOAT, 0, texCoords);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);

    DebugLogGlError("after textured vertex array draw");
}

void DrawSolidQuad(float left, float top, float right, float bottom, float red, float green, float blue, float alpha)
{
    if (alpha <= 0.0f)
    {
        return;
    }

    XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glColor4f(red, green, blue, std::clamp(alpha, 0.0f, 1.0f));

    const GLfloat vertices[] = {
        static_cast<GLfloat>(left), static_cast<GLfloat>(bottom),
        static_cast<GLfloat>(right), static_cast<GLfloat>(bottom),
        static_cast<GLfloat>(right), static_cast<GLfloat>(top),
        static_cast<GLfloat>(left), static_cast<GLfloat>(top),
    };

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, vertices);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableClientState(GL_VERTEX_ARRAY);

    DebugLogGlError("after solid vertex array draw");
}

void OverlayTextureBounds(const TextureAsset& texture, float drawLeft, float drawTop, float scale, float& left, float& top, float& right, float& bottom)
{
    const float centerX = drawLeft + texture.centerX * scale;
    const float centerY = drawTop - texture.centerY * scale;
    const float halfWidth = static_cast<float>(texture.width) * scale * 0.5f;
    const float halfHeight = static_cast<float>(texture.height) * scale * 0.5f;
    left = centerX - halfWidth;
    top = centerY + halfHeight;
    right = centerX + halfWidth;
    bottom = centerY - halfHeight;
}

void DrawOverlayTexture(TextureAsset& texture, float drawLeft, float drawTop, float scale, float alpha = 1.0f)
{
    if (!EnsureTextureLoaded(texture))
    {
        return;
    }

    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    OverlayTextureBounds(texture, drawLeft, drawTop, scale, left, top, right, bottom);
    DrawTexturedQuad(texture.textureId, left, top, right, bottom, alpha);
}

void DrawOverlayBlackMask(TextureAsset& texture, float drawLeft, float drawTop, float scale, float alpha)
{
    if (alpha <= 0.0f || !EnsureTextureLoaded(texture))
    {
        return;
    }

    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    OverlayTextureBounds(texture, drawLeft, drawTop, scale, left, top, right, bottom);
    DrawTexturedQuad(texture.textureId, left, top, right, bottom, alpha, 0.0f, 0.0f, 0.0f);
}

void DrawDebugLine(int x, int y, const char* text)
{
    float color[] = {1.0f, 1.0f, 1.0f};
    XPLMDrawString(color, x, y, const_cast<char*>(text), nullptr, xplmFont_Proportional);
}

void DrawDebugOverlay(int left, int top, int right, int bottom)
{
    if (!gDebugOverlayEnabled)
    {
        return;
    }

    XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
    const int boxLeft = left + 8;
    const int boxTop = top - 8;
    const int boxRight = std::min(right - 8, boxLeft + 390);
    const int boxBottom = std::max(bottom + 8, boxTop - 540);
    XPLMDrawTranslucentDarkBox(boxLeft, boxTop, boxRight, boxBottom);

    const float aoaPilotDegrees = ReadDataRefFloat(gAoaPilotDataRef, 0.0f);
    const float aoaFlightModelDegrees = ReadDataRefFloat(gAoaFlightModelDataRef, 0.0f);
    const float positionAlphaDegrees = ReadDataRefFloat(gPositionAlphaDataRef, 0.0f);
    const float rawIasKnots = ReadDataRefFloat(gIasDataRef, 0.0f);
    const float iasKnots = ClampIasForLogic(rawIasKnots);
    const float pitchDegrees = ReadDataRefFloat(gPitchDataRef, 0.0f);
    const float flightPathDegrees = ReadDataRefFloat(gFlightPathDataRef, 0.0f);
    const bool powered = ReadPowerState();

    char line[256] = {};
    int y = boxTop - 18;

    DrawDebugLine(boxLeft + 8, y, "GI260 DEBUG - speed/Vso proxy + stall_warning_ratio drives ladder");
    y -= 16;
    sprintf_s(line, "state: %s", GetDisplayStateName());
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    if (gPermanentFailMode)
    {
        sprintf_s(line, "FAIL: %s", gFailReason.c_str());
        DrawDebugLine(boxLeft + 8, y, line);
        y -= 16;
    }
    else if (!gPendingValidationFailureReason.empty())
    {
        sprintf_s(line, "INIT CHECK: %s  grace %.1f  count %d/%d",
            gPendingValidationFailureReason.c_str(),
            gCalibrationGraceRemainingSeconds,
            gValidationFailureCount,
            kValidationFailuresBeforeFail);
        DrawDebugLine(boxLeft + 8, y, line);
        y -= 16;
    }
    sprintf_s(line, "IAS raw/logic: %.1f/%.1f kt  visual_arm: %d  audio_arm: %d  power: %d",
        rawIasKnots,
        iasKnots,
        gIsArmed ? 1 : 0,
        (gIsArmed && gAudioDelayRemainingSeconds <= 0.0f && !gIsMuted) ? 1 : 0,
        powered ? 1 : 0);
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "night dim: %.2f  bg/led/button max: %.2f/%.2f/%.2f",
        ReadNightLightingRatio(),
        gConfig.nightBackgroundBlendMax,
        gConfig.nightLedBlackMaskMax,
        gConfig.nightButtonBlackMaskMax);
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    if (gConfig.armIasKts <= 0.0f)
    {
        sprintf_s(line, "arm IAS: disabled  Vso_adj/Vref: %.1f/%.1f",
            gWeightAdjustedVsoKts,
            ApproachReferenceSpeedKts());
    }
    else
    {
        sprintf_s(line, "arm IAS: %.1f kt  Vso_adj/Vref: %.1f/%.1f",
            gConfig.armIasKts,
            gWeightAdjustedVsoKts,
            ApproachReferenceSpeedKts());
    }
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "mass kg: cur %.0f max %.0f ratio %.2f",
        gAircraftCurrentMassKg,
        gAircraftMaxMassKg,
        gAircraftMaxMassKg > 0.0f ? gAircraftCurrentMassKg / gAircraftMaxMassKg : 0.0f);
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "mass parts kg: empty %.0f fuel %.0f payload %.0f",
        gAircraftEmptyMassKg,
        gAircraftFuelMassKg,
        gAircraftPayloadMassKg);
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "flaprat/flaprqst: %.2f/%.2f  cal_ref: %s",
        gFlapRatio,
        gFlapRequestRatio,
        IsLandingFlapCalibrated() ? "FULL FLAPS" : "NOT LANDING-FLAP");
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "ground: %d  aero lift support: %.2f  gear load: %.2f",
        gOnGround ? 1 : 0,
        gAeroLiftSupportRatio,
        gGearLoadRatio);
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    if (!IsLandingFlapCalibrated())
    {
        DrawDebugLine(boxLeft + 8, y, "ADVISORY: AOA REF NOT LANDING-FLAP CALIBRATED");
        y -= 16;
    }
    sprintf_s(line, "raw_aoa_deg: %.2f", gRawAoaDegrees);
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "filtered_aoa_deg: %.2f", gFilteredAoaDegrees);
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "norm_aoa_units: raw %.2f  filt %.2f", gRawAoaUnits, gFilteredAoaUnits);
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "speed lift ratio: %.2f  speed units raw/adj: %.2f/%.2f  stall units: %.2f",
        gSpeedLiftReserveRatio,
        gRawSpeedProxyAoaUnits,
        gSpeedProxyAoaUnits,
        NormalizeStallWarningRatioToUnits(gStallWarningRatio));
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "unit_delta/frame: %.2f  unit_rate: %.2f/s", gAoaUnitsDeltaPerFrame, gAoaUnitsRatePerSecond);
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "deg_delta/frame: %.2f  deg_rate: %.2f/s", gAoaDeltaPerFrame, gAoaRateDegreesPerSecond);
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "AOA source: %s", GetDrivingAoaSourceName());
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "AoA_pilot: %.2f  FM AoA: %.2f", aoaPilotDegrees, aoaFlightModelDegrees);
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    DrawDebugLine(boxLeft + 8, y, "AOA angle datarefs are debug-only; not used for LEDs");
    y -= 16;
    sprintf_s(line, "position alpha: %.2f deg", positionAlphaDegrees);
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "Pitch theta: %.2f deg", pitchDegrees);
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "Flight path: %.2f deg", flightPathDegrees);
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "theta - vpath: %.2f deg", pitchDegrees - flightPathDegrees);
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "modules: %llu/11  mute: %d", static_cast<unsigned long long>(CountVisibleModules()), gIsMuted ? 1 : 0);
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "green bars: %llu/4  approach ref dot: %d",
        static_cast<unsigned long long>(CountVisibleGreenBars()),
        IsModuleVisible(ModuleId::GREEN_CIRCLE) ? 1 : 0);
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "scenario flags: APP=%d HIGH=%d WARN=%d EXTREME=%d",
        gCurrentScenario == DisplayScenario::ApproachAoa ? 1 : 0,
        gCurrentScenario == DisplayScenario::HighAoa ? 1 : 0,
        gCurrentScenario == DisplayScenario::WarningAoa ? 1 : 0,
        gCurrentScenario == DisplayScenario::ExtremeWarningAoa ? 1 : 0);
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "highest_active_module: %s", ModuleShortName(HighestActiveModule()));
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    const std::string newlyCrossed = ModuleMaskToNames(gNewlyCrossedThresholdMask);
    sprintf_s(line, "newly_crossed: %s", newlyCrossed.c_str());
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "audio delay start/arm: %.1f/%.1f  audio: %s",
        gStartupAudioDelayRemainingSeconds,
        gAudioDelayRemainingSeconds,
        AudioAlertModeName(gAudioAlertMode));
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "mute timer: %.1f  vol gain/master/final: %.2f/%.2f/%.2f",
        gMuteMinimumRemainingSeconds,
        gConfig.audioGain,
        MasterRadioVolumeRatio(),
        EffectiveAudioVolume());
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "beep loaded/active/events: %d/%d/%d  repeat delay: %.2f",
        gBeepSample.loaded ? 1 : 0,
        gBeepChannelActive ? 1 : 0,
        gPendingEventBeeps,
        gBeepRepeatDelayRemainingSeconds);
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "stall warn: %d  ratio: %.2f  acf warn: %.1f",
        IsStallWarningActive() ? 1 : 0,
        gStallWarningRatio,
        gConfig.aircraftWarningAoaDeg);
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "alpha zero/app/warn: %.1f/%.1f/%.1f deg",
        gConfig.normalizedZeroAoaDeg,
        ApproachAoaDegrees(),
        CriticalAoaDegrees());
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "top-green/dot units: %.1f/%.1f  hys %.1f",
        ApproachAoaUnits(),
        ApproachReferenceAoaUnits(iasKnots),
        gConfig.moduleHysteresisDeg);
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "norm zero/crit: %.1f/%.1f deg  dot units: %.2f",
        gConfig.normalizedZeroAoaDeg,
        CriticalAoaDegrees(),
        ApproachReferenceAoaUnits(iasKnots));
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "thr units G: %.1f %.1f %.1f GS%.1f C%.1f",
        gConfig.greenSolidLowerDeg,
        gConfig.greenSolidMiddleDeg,
        gConfig.greenSolidUpperDeg,
        gConfig.greenSplitBarDeg,
        ApproachReferenceAoaUnits(iasKnots));
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "thr units Y: %.1f %.1f %.1f %.1f",
        gConfig.yellowSolidBarDeg,
        gConfig.yellowSplitBarDeg,
        gConfig.yellowLowerChevronDeg,
        gConfig.yellowUpperChevronDeg);
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "thr units R: lower %.1f upper %.1f", gConfig.redLowerDeg, gConfig.redUpperDeg);
    DrawDebugLine(boxLeft + 8, y, line);
    y -= 16;
    sprintf_s(line, "mods R%d%d Y%d%d%d%d G%d%d%d%d%d",
        IsModuleVisible(ModuleId::RED_UPPER_CHEVRON) ? 1 : 0,
        IsModuleVisible(ModuleId::RED_LOWER_CHEVRON) ? 1 : 0,
        IsModuleVisible(ModuleId::YELLOW_UPPER_CHEVRON) ? 1 : 0,
        IsModuleVisible(ModuleId::YELLOW_LOWER_CHEVRON) ? 1 : 0,
        IsModuleVisible(ModuleId::YELLOW_SPLIT_BAR) ? 1 : 0,
        IsModuleVisible(ModuleId::YELLOW_SOLID_BAR) ? 1 : 0,
        IsModuleVisible(ModuleId::GREEN_SPLIT_BAR) ? 1 : 0,
        IsModuleVisible(ModuleId::GREEN_CIRCLE) ? 1 : 0,
        IsModuleVisible(ModuleId::GREEN_SOLID_UPPER) ? 1 : 0,
        IsModuleVisible(ModuleId::GREEN_SOLID_MIDDLE) ? 1 : 0,
        IsModuleVisible(ModuleId::GREEN_SOLID_LOWER) ? 1 : 0);
    DrawDebugLine(boxLeft + 8, y, line);
}

bool GetDesignPoint(int x, int y, float& designX, float& designY)
{
    if (gWindow == nullptr)
    {
        return false;
    }

    const Rect windowRect = GetFloatingGeometry();
    int drawLeft = 0;
    int drawTop = 0;
    int drawRight = 0;
    int drawBottom = 0;
    GetAspectFitRect(windowRect.left, windowRect.top, windowRect.right, windowRect.bottom, drawLeft, drawTop, drawRight, drawBottom);

    const float scale = static_cast<float>(drawRight - drawLeft) / static_cast<float>(kDesignWidth);
    if (scale <= 0.0f)
    {
        return false;
    }

    designX = (static_cast<float>(x) - static_cast<float>(drawLeft)) / scale;
    designY = (static_cast<float>(drawTop) - static_cast<float>(y)) / scale;
    return designX >= 0.0f && designX <= static_cast<float>(kDesignWidth) &&
           designY >= 0.0f && designY <= static_cast<float>(kDesignHeight);
}

bool IsInsideDesignTexture(const TextureAsset& texture, float designX, float designY, float fallbackWidth, float fallbackHeight)
{
    const float width = texture.width > 0 ? static_cast<float>(texture.width) : fallbackWidth;
    const float height = texture.height > 0 ? static_cast<float>(texture.height) : fallbackHeight;
    return designX >= texture.centerX - width * 0.5f &&
           designX <= texture.centerX + width * 0.5f &&
           designY >= texture.centerY - height * 0.5f &&
           designY <= texture.centerY + height * 0.5f;
}

void DrawGI260Face(int left, int top, int right, int bottom)
{
    int drawLeft = 0;
    int drawTop = 0;
    int drawRight = 0;
    int drawBottom = 0;
    GetAspectFitRect(left, top, right, bottom, drawLeft, drawTop, drawRight, drawBottom);

    if (!EnsureTextureLoaded(gBackgroundTexture))
    {
        DrawPlaceholder(left, top, right, bottom);
        return;
    }

    DrawTexturedQuad(
        gBackgroundTexture.textureId,
        static_cast<float>(drawLeft),
        static_cast<float>(drawTop),
        static_cast<float>(drawRight),
        static_cast<float>(drawBottom));

    const float scale = static_cast<float>(drawRight - drawLeft) / static_cast<float>(kDesignWidth);
    const float nightRatio = ReadNightLightingRatio();
    const float litBackgroundAlpha = std::clamp(nightRatio * gConfig.nightBackgroundBlendMax, 0.0f, 1.0f);
    const float ledBlackMaskAlpha = std::clamp(nightRatio * gConfig.nightLedBlackMaskMax, 0.0f, 1.0f);
    const float buttonBlackMaskAlpha = std::clamp(nightRatio * gConfig.nightButtonBlackMaskMax, 0.0f, 1.0f);

    if (litBackgroundAlpha > 0.0f && EnsureTextureLoaded(gNightBackgroundTexture))
    {
        DrawOverlayTexture(
            gNightBackgroundTexture,
            static_cast<float>(drawLeft),
            static_cast<float>(drawTop),
            scale,
            litBackgroundAlpha);
    }

    for (auto& module : gModules)
    {
        if (module.visible)
        {
            DrawOverlayTexture(module.texture, static_cast<float>(drawLeft), static_cast<float>(drawTop), scale);
            DrawOverlayBlackMask(module.texture, static_cast<float>(drawLeft), static_cast<float>(drawTop), scale, ledBlackMaskAlpha);
        }
    }

    if (gTestButtonPressed)
    {
        DrawOverlayTexture(gTestPressedTexture, static_cast<float>(drawLeft), static_cast<float>(drawTop), scale);
        DrawOverlayBlackMask(gTestPressedTexture, static_cast<float>(drawLeft), static_cast<float>(drawTop), scale, buttonBlackMaskAlpha);
    }
    if (gMuteButtonPressed)
    {
        DrawOverlayTexture(gMutePressedTexture, static_cast<float>(drawLeft), static_cast<float>(drawTop), scale);
        DrawOverlayBlackMask(gMutePressedTexture, static_cast<float>(drawLeft), static_cast<float>(drawTop), scale, buttonBlackMaskAlpha);
    }
}

void DrawPlaceholder(int left, int top, int right, int bottom)
{
    XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
    XPLMDrawTranslucentDarkBox(left, top, right, bottom);

    float color[] = {1.0f, 1.0f, 1.0f};
    XPLMDrawString(color, left + 12, top - 28, const_cast<char*>("Garmin GI260"), nullptr, xplmFont_Proportional);
    XPLMDrawString(color, left + 12, top - 48, const_cast<char*>("Waiting for Textures/Background.png"), nullptr, xplmFont_Proportional);
}

void DrawWindow(XPLMWindowID windowId, void* refcon)
{
    (void)refcon;

    EnforceWindowGeometry();

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    XPLMGetWindowGeometry(windowId, &left, &top, &right, &bottom);

    DrawGI260Face(left, top, right, bottom);
    DrawDebugOverlay(left, top, right, bottom);
}

int HandleMouseClick(XPLMWindowID windowId, int x, int y, XPLMMouseStatus mouse, void* refcon)
{
    (void)windowId;
    (void)refcon;

    float designX = 0.0f;
    float designY = 0.0f;
    const bool insideFace = GetDesignPoint(x, y, designX, designY);

    if (mouse == xplm_MouseDown)
    {
        gTestButtonPressed = insideFace && IsInsideDesignTexture(gTestPressedTexture, designX, designY, 83.0f, 29.0f);
        gMuteButtonPressed = insideFace && IsInsideDesignTexture(gMutePressedTexture, designX, designY, 83.0f, 29.0f);
        if (gTestButtonPressed)
        {
            gUnpoweredTestHeldSeconds = 0.0f;
            gUnpoweredTestBeepQueued = false;
            gFailAnimationElapsedSeconds = 0.0f;
        }
    }
    else if (mouse == xplm_MouseDrag)
    {
        if (gTestButtonPressed)
        {
            gTestButtonPressed = insideFace && IsInsideDesignTexture(gTestPressedTexture, designX, designY, 83.0f, 29.0f);
        }
        if (gMuteButtonPressed)
        {
            gMuteButtonPressed = insideFace && IsInsideDesignTexture(gMutePressedTexture, designX, designY, 83.0f, 29.0f);
        }
    }
    else if (mouse == xplm_MouseUp)
    {
        if (gTestButtonPressed && insideFace && IsInsideDesignTexture(gTestPressedTexture, designX, designY, 83.0f, 29.0f))
        {
            if (ReadPowerState())
            {
                gSelfTestRemainingSeconds = gConfig.selfTestSeconds;
                QueueSelfTestBeepEvent();
            }
        }
        if (gMuteButtonPressed && insideFace && IsInsideDesignTexture(gMutePressedTexture, designX, designY, 83.0f, 29.0f))
        {
            gMuteMinimumRemainingSeconds = gConfig.muteMinimumSeconds;
            gIsMuted = gMuteMinimumRemainingSeconds > 0.0f;
            UpdateAudioAlertState();
        }

        gTestButtonPressed = false;
        gMuteButtonPressed = false;
        gUnpoweredTestHeldSeconds = 0.0f;
        gUnpoweredTestBeepQueued = false;
    }

    return 1;
}

void HandleKey(XPLMWindowID windowId, char key, XPLMKeyFlags flags, char virtualKey, void* refcon, int losingFocus)
{
    (void)windowId;
    (void)key;
    (void)flags;
    (void)virtualKey;
    (void)refcon;
    (void)losingFocus;
}

XPLMCursorStatus HandleCursor(XPLMWindowID windowId, int x, int y, void* refcon)
{
    (void)windowId;
    (void)x;
    (void)y;
    (void)refcon;
    return xplm_CursorDefault;
}

int HandleMouseWheel(XPLMWindowID windowId, int x, int y, int wheel, int clicks, void* refcon)
{
    (void)windowId;
    (void)x;
    (void)y;
    (void)wheel;
    (void)clicks;
    (void)refcon;
    return 1;
}

void CreateGI260Window()
{
    if (gWindow != nullptr)
    {
        return;
    }

    int screenLeft = 0;
    int screenTop = 0;
    int screenRight = 0;
    int screenBottom = 0;
    XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);

    const int width = kDesignWidth;
    const int height = kDesignHeight;
    const int left = screenLeft + 80;
    const int top = screenTop - 80;

    XPLMCreateWindow_t params = {};
    params.structSize = sizeof(params);
    params.left = left;
    params.top = top;
    params.right = left + width;
    params.bottom = top - height;
    params.visible = 1;
    params.drawWindowFunc = DrawWindow;
    params.handleMouseClickFunc = HandleMouseClick;
    params.handleKeyFunc = HandleKey;
    params.handleCursorFunc = HandleCursor;
    params.handleMouseWheelFunc = HandleMouseWheel;
    params.refcon = nullptr;
    params.decorateAsFloatingWindow = gBorderEnabled ? xplm_WindowDecorationRoundRectangle : xplm_WindowDecorationSelfDecoratedResizable;
    params.layer = xplm_WindowLayerFloatingWindows;
    params.handleRightClickFunc = HandleMouseClick;

    gWindow = XPLMCreateWindowEx(&params);
    if (gWindow == nullptr)
    {
        DebugLog("Failed to create window.");
        return;
    }

    XPLMSetWindowTitle(gWindow, "Garmin GI260");
    XPLMSetWindowResizingLimits(gWindow, 132, 275, 1056, 2196);
    XPLMSetWindowGravity(gWindow, 0.0f, 1.0f, 0.0f, 1.0f);

    gLastFloatingGeometry = {left, top, left + width, top - height};
    gHasLastFloatingGeometry = true;
    gHasLastOsGeometry = false;
    if (gLockPositionEnabled)
    {
        CaptureLockPositionOffset();
    }
}

void DestroyGI260Window()
{
    if (gWindow != nullptr)
    {
        XPLMDestroyWindow(gWindow);
        gWindow = nullptr;
    }

    gHasLastFloatingGeometry = false;
    gHasLastOsGeometry = false;
}

void SetToggleEnabled(bool enabled)
{
    gToggleEnabled = enabled;
    if (gToggleEnabled)
    {
        CreateGI260Window();
    }
    else
    {
        DestroyGI260Window();
    }

    UpdateMenuChecks();
}

void RecreateWindowPreservingGeometry()
{
    if (gWindow == nullptr)
    {
        return;
    }

    const bool wasPoppedOut = XPLMWindowIsPoppedOut(gWindow) != 0;
    const Rect floatingGeometry = GetFloatingGeometry();
    const Rect osGeometry = wasPoppedOut ? GetOsGeometry() : Rect{};

    XPLMDestroyWindow(gWindow);
    gWindow = nullptr;

    CreateGI260Window();
    if (gWindow == nullptr)
    {
        return;
    }

    SetFloatingGeometry(floatingGeometry);
    if (wasPoppedOut)
    {
        XPLMSetWindowPositioningMode(gWindow, xplm_WindowPopOut, -1);
        SetOsGeometry(osGeometry);
    }

    if (gLockPositionEnabled)
    {
        CaptureLockPositionOffset();
    }
}

void SetBorderEnabled(bool enabled)
{
    if (gBorderEnabled == enabled)
    {
        return;
    }

    gBorderEnabled = enabled;
    if (gWindow != nullptr)
    {
        RecreateWindowPreservingGeometry();
    }

    UpdateMenuChecks();
}

void SetLockPositionEnabled(bool enabled)
{
    gLockPositionEnabled = enabled;
    if (gLockPositionEnabled)
    {
        CaptureLockPositionOffset();
    }
    else
    {
        gHasLockedOffset = false;
    }

    UpdateMenuChecks();
}

void SetDebugOverlayEnabled(bool enabled)
{
    gDebugOverlayEnabled = enabled;
    UpdateMenuChecks();
}

void MenuHandler(void* menuRef, void* itemRef)
{
    (void)menuRef;

    const auto item = static_cast<MenuItem>(reinterpret_cast<std::intptr_t>(itemRef));
    switch (item)
    {
    case MenuItem::Toggle:
        SetToggleEnabled(!gToggleEnabled);
        break;
    case MenuItem::Border:
        SetBorderEnabled(!gBorderEnabled);
        break;
    case MenuItem::LockPosition:
        SetLockPositionEnabled(!gLockPositionEnabled);
        break;
    case MenuItem::DebugOverlay:
        SetDebugOverlayEnabled(!gDebugOverlayEnabled);
        break;
    default:
        break;
    }
}

void CreateGI260Menu()
{
    XPLMMenuID pluginsMenu = XPLMFindPluginsMenu();
    gPluginMenuItem = XPLMAppendMenuItem(pluginsMenu, kMenuTitle, nullptr, 0);
    gMenu = XPLMCreateMenu(kMenuTitle, pluginsMenu, gPluginMenuItem, MenuHandler, nullptr);
    gToggleMenuItem = XPLMAppendMenuItem(gMenu, kToggleMenuTitle, reinterpret_cast<void*>(static_cast<std::intptr_t>(MenuItem::Toggle)), 0);
    gBorderMenuItem = XPLMAppendMenuItem(gMenu, kBorderMenuTitle, reinterpret_cast<void*>(static_cast<std::intptr_t>(MenuItem::Border)), 0);
    gLockPositionMenuItem = XPLMAppendMenuItem(gMenu, kLockPositionMenuTitle, reinterpret_cast<void*>(static_cast<std::intptr_t>(MenuItem::LockPosition)), 0);
    gDebugOverlayMenuItem = XPLMAppendMenuItem(gMenu, kDebugOverlayMenuTitle, reinterpret_cast<void*>(static_cast<std::intptr_t>(MenuItem::DebugOverlay)), 0);
    UpdateMenuChecks();
}

void DestroyGI260Menu()
{
    if (gMenu != nullptr)
    {
        XPLMDestroyMenu(gMenu);
        gMenu = nullptr;
    }

    gPluginMenuItem = -1;
    gToggleMenuItem = -1;
    gBorderMenuItem = -1;
    gLockPositionMenuItem = -1;
    gDebugOverlayMenuItem = -1;
}

void ResolvePluginRoot()
{
    char pluginPath[512] = {};
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, pluginPath, nullptr, nullptr);
    const std::filesystem::path xplPath = std::filesystem::u8path(pluginPath);
    gPluginRoot = xplPath.parent_path().parent_path();

    if (gPluginRoot.empty())
    {
        DebugLog("plugin root resolve failed: XPLMGetPluginInfo returned an empty path.");
    }

    DebugLogFormatted("plugin signature=%s", kPluginSignature);
    DebugLogPath("plugin xpl path=", xplPath);
    DebugLogPath("resolved plugin root=", gPluginRoot);
}

void InitializeDataRefs()
{
    // Simulator input boundary. Keep XPLMFindDataRef calls centralized so
    // aircraft reload/reset can rebuild all handles consistently.
    gAoaFlightModelDataRef = XPLMFindDataRef(kPreferredAoaSourceDataRef);
    gAoaPilotDataRef = XPLMFindDataRef(kAoaPilotDataRef);
    gPositionAlphaDataRef = XPLMFindDataRef(kLegacyAoaSourceDataRef);
    if (gPositionAlphaDataRef == nullptr)
    {
        gPositionAlphaDataRef = XPLMFindDataRef("sim/flightmodel2/position/alpha");
    }

    if (gAoaFlightModelDataRef != nullptr)
    {
        gAoaDataRef = gAoaFlightModelDataRef;
        gDrivingAoaSourceName = kPreferredAoaSourceDataRef;
    }
    else if (gAoaPilotDataRef != nullptr)
    {
        gAoaDataRef = gAoaPilotDataRef;
        gDrivingAoaSourceName = kAoaPilotDataRef;
    }
    else if (gPositionAlphaDataRef != nullptr)
    {
        gAoaDataRef = gPositionAlphaDataRef;
        gDrivingAoaSourceName = kLegacyAoaSourceDataRef;
    }
    else
    {
        gAoaDataRef = nullptr;
        gDrivingAoaSourceName = "missing AOA dataref";
    }

    gIasDataRef = XPLMFindDataRef(kIasSourceDataRef);
    if (gIasDataRef == nullptr)
    {
        gIasDataRef = XPLMFindDataRef("sim/cockpit2/gauges/indicators/airspeed_kts_pilot");
    }

    gPowerDataRef = XPLMFindDataRef(gConfig.powerDataRefName.c_str());
    if (gPowerDataRef == nullptr)
    {
        gPowerDataRef = XPLMFindDataRef("sim/cockpit/electrical/avionics_on");
    }
    gNightLightingDataRef = gConfig.nightLightingDataRefName.empty()
        ? nullptr
        : XPLMFindDataRef(gConfig.nightLightingDataRefName.c_str());
    gAircraftVsoDataRef = XPLMFindDataRef("sim/aircraft/view/acf_Vso");
    gAircraftStallWarnAlphaDataRef = XPLMFindDataRef("sim/aircraft/overflow/acf_stall_warn_alpha");
    gAircraftEmptyMassKgDataRef = XPLMFindDataRef("sim/aircraft/weight/acf_m_empty");
    gAircraftMaxMassKgDataRef = XPLMFindDataRef("sim/aircraft/weight/acf_m_max");
    gAircraftFuelMassKgDataRef = XPLMFindDataRef("sim/flightmodel/weight/m_fuel_total");
    if (gAircraftFuelMassKgDataRef == nullptr)
    {
        gAircraftFuelMassKgDataRef = XPLMFindDataRef("sim/aircraft/weight/acf_m_fuel_tot");
    }
    gPayloadMassKgDataRef = XPLMFindDataRef("sim/flightmodel/weight/m_fixed");
    gTotalMassKgDataRef = XPLMFindDataRef("sim/flightmodel/weight/m_total");
    gFlapRatioDataRef = XPLMFindDataRef("sim/flightmodel/controls/flaprat");
    gFlapRequestRatioDataRef = XPLMFindDataRef("sim/flightmodel/controls/flaprqst");
    gStallWarningDataRef = XPLMFindDataRef("sim/cockpit2/annunciators/stall_warning");
    if (gStallWarningDataRef == nullptr)
    {
        gStallWarningDataRef = XPLMFindDataRef("sim/flightmodel/failures/stallwarning");
    }
    gStallWarningRatioDataRef = XPLMFindDataRef("sim/cockpit2/annunciators/stall_warning_ratio");
    gOnGroundAnyDataRef = XPLMFindDataRef("sim/flightmodel/failures/onground_any");
    gGearOnGroundDataRef = XPLMFindDataRef("sim/flightmodel2/gear/on_ground");
    gAeroNormalForceDataRef = XPLMFindDataRef("sim/flightmodel/forces/fnrml_aero");
    gGearNormalForceDataRef = XPLMFindDataRef("sim/flightmodel/forces/fnrml_gear");
    gRadioVolumeRatioDataRef = XPLMFindDataRef("sim/operation/sound/radio_volume_ratio");
    gPitchDataRef = XPLMFindDataRef("sim/flightmodel/position/theta");
    if (gPitchDataRef == nullptr)
    {
        gPitchDataRef = XPLMFindDataRef("sim/flightmodel2/position/true_theta");
    }
    gFlightPathDataRef = XPLMFindDataRef("sim/flightmodel/position/vpath");
    if (gFlightPathDataRef == nullptr)
    {
        gFlightPathDataRef = XPLMFindDataRef("sim/flightmodel2/position/vpath");
    }
}

float FlightLoopCallback(float elapsedSinceLastCall, float elapsedTimeSinceLastFlightLoop, int counter, void* refcon)
{
    (void)elapsedSinceLastCall;
    (void)elapsedTimeSinceLastFlightLoop;
    (void)counter;
    (void)refcon;

    if (gWindow == nullptr)
    {
        return 0.1f;
    }

    if (gToggleEnabled && !XPLMGetWindowIsVisible(gWindow))
    {
        gToggleEnabled = false;
        DestroyGI260Window();
        UpdateMenuChecks();
        return 0.1f;
    }

    if (gToggleEnabled)
    {
        const float elapsedSeconds = std::max(0.0f, elapsedSinceLastCall);
        UpdateIndicatorState(elapsedSeconds);
        UpdateManualMuteState(elapsedSeconds);
        UpdateAudioPlayback(elapsedSeconds, ReadPowerState());
        EnforceWindowGeometry();
    }

    return 0.1f;
}
} // namespace

PLUGIN_API int XPluginStart(char* outName, char* outSig, char* outDesc)
{
    CopyPluginString(outName, kPluginName);
    CopyPluginString(outSig, kPluginSignature);
    CopyPluginString(outDesc, kPluginDescription);

    ResolvePluginRoot();
    LoadConfig();
    const std::filesystem::path beepPath = gPluginRoot / "Resources" / "Beep.wav";
    if (!LoadPcm16Wav(beepPath, gBeepSample))
    {
        LogAlwaysFormatted("audio disabled: %s (%s)", gBeepSample.error.c_str(), PathToUtf8(beepPath).c_str());
    }
    else
    {
        LogAlwaysFormatted("loaded audio sample: %s (%d Hz, %d channel)",
            PathToUtf8(beepPath).c_str(),
            gBeepSample.sampleRateHz,
            gBeepSample.channelCount);
    }
    ValidateModuleThresholds();
    InitializeDataRefs();
    ReadAircraftReferenceValues();
    CreateGI260Menu();
    SetToggleEnabled(gConfig.initialStatus);
    XPLMRegisterFlightLoopCallback(FlightLoopCallback, 0.1f, nullptr);

    DebugLog("plugin loaded.");
    return 1;
}

PLUGIN_API void XPluginStop()
{
    StopActiveBeep();
    XPLMUnregisterFlightLoopCallback(FlightLoopCallback, nullptr);
    DestroyGI260Window();
    DeleteAllTextures();
    DestroyGI260Menu();
}

PLUGIN_API int XPluginEnable()
{
    if (gToggleEnabled)
    {
        CreateGI260Window();
    }

    UpdateMenuChecks();
    return 1;
}

PLUGIN_API void XPluginDisable()
{
    StopActiveBeep();
    DestroyGI260Window();
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID inFromWho, int inMessage, void* inParam)
{
    (void)inFromWho;
    (void)inParam;

    if (inMessage == XPLM_MSG_PLANE_LOADED ||
        inMessage == XPLM_MSG_PLANE_UNLOADED ||
        inMessage == XPLM_MSG_PLANE_CRASHED ||
        inMessage == XPLM_MSG_AIRPLANE_COUNT_CHANGED)
    {
        InitializeDataRefs();
        ResetCalibrationState();
        ReadAircraftReferenceValues();
    }
}

