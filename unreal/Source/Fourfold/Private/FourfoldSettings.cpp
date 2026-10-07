// Fourfold - UFourfoldSettingsSubsystem: player settings (port of game/ui/game_settings.gd + UE additions).
// Clamped on every set, saved as JSON under Saved/Fourfold/settings.json (ff::ParseJson / ff::ToJson through the logic
// island; FJsonObject's API changed in 5.8), applied to the engine (frame-rate cap, scalability) and broadcast.
#include "FourfoldSettings.h"

#include "FourfoldLog.h"
#include "Logic/FFGSettings.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Scalability.h"

#include <string>

namespace FourfoldSettingsConv
{
	static ffg::SettingsData ToData(const FFourfoldSettings& S)
	{
		ffg::SettingsData D;
		D.control_scale = S.ControlScale;
		D.control_opacity = S.ControlOpacity;
		D.layout_preset = std::string(TCHAR_TO_UTF8(*S.LayoutPreset));
		D.left_handed = S.bLeftHanded;
		D.strong_labels = S.bStrongLabels;
		D.touch_ui_mode = S.TouchUiMode;
		D.camera_sensitivity = S.CameraSensitivity;
		D.invert_y = S.bInvertY;
		D.screen_shake = S.ScreenShake;
		D.flashes = S.Flashes;
		D.haptics = S.bHaptics;
		D.reduced_motion = S.bReducedMotion;
		D.slowmo_assist = S.bSlowmoAssist;
		D.master_volume = S.MasterVolume;
		D.sfx_volume = S.SfxVolume;
		D.ambience_volume = S.AmbienceVolume;
		D.ui_volume = S.UiVolume;
		D.quality = S.Quality;
		D.frame_rate_cap = S.FrameRateCap;
		D.show_debug = S.bShowDebug;
		return D;
	}

	static FFourfoldSettings FromData(const ffg::SettingsData& D)
	{
		FFourfoldSettings S;
		S.ControlScale = D.control_scale;
		S.ControlOpacity = D.control_opacity;
		S.LayoutPreset = FString(UTF8_TO_TCHAR(D.layout_preset.c_str()));
		S.bLeftHanded = D.left_handed;
		S.bStrongLabels = D.strong_labels;
		S.TouchUiMode = D.touch_ui_mode;
		S.CameraSensitivity = D.camera_sensitivity;
		S.bInvertY = D.invert_y;
		S.ScreenShake = D.screen_shake;
		S.Flashes = D.flashes;
		S.bHaptics = D.haptics;
		S.bReducedMotion = D.reduced_motion;
		S.bSlowmoAssist = D.slowmo_assist;
		S.MasterVolume = D.master_volume;
		S.SfxVolume = D.sfx_volume;
		S.AmbienceVolume = D.ambience_volume;
		S.UiVolume = D.ui_volume;
		S.Quality = D.quality;
		S.FrameRateCap = D.frame_rate_cap;
		S.bShowDebug = D.show_debug;
		return S;
	}

	static FString SettingsPath() { return FPaths::ProjectSavedDir() / TEXT("Fourfold") / TEXT("settings.json"); }
}

UFourfoldSettingsSubsystem* UFourfoldSettingsSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UFourfoldSettingsSubsystem>() : nullptr;
}

void UFourfoldSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if PLATFORM_IOS || PLATFORM_ANDROID
	const bool bMobile = true;
#else
	const bool bMobile = false;
#endif
	const double MemoryGB = double(FPlatformMemory::GetConstants().TotalPhysical) / (1024.0 * 1024.0 * 1024.0);
	AutoQuality = ffg::AutoQualityFor(bMobile, FPlatformMisc::NumberOfCores(), MemoryGB);

	ffg::SettingsData Data;
	FString Text;
	if (FFileHelper::LoadFileToString(Text, *FourfoldSettingsConv::SettingsPath()))
	{
		if (!ffg::SettingsFromJson(std::string(TCHAR_TO_UTF8(*Text)), Data))
		{
			UE_LOG(LogFourfold, Warning, TEXT("settings.json is damaged; using defaults"));
		}
	}
	Settings = FourfoldSettingsConv::FromData(ffg::ClampSettings(Data));
	UE_LOG(LogFourfold, Log, TEXT("Settings loaded (auto quality %d, %.1f GB, %d cores)"), AutoQuality, MemoryGB, FPlatformMisc::NumberOfCores());
	// The engine-facing part is applied once the engine is fully up (first world tick reads it again anyway).
	SetSettings(Settings, false);
}

void UFourfoldSettingsSubsystem::SetSettings(const FFourfoldSettings& NewSettings, bool bSave)
{
	Settings = FourfoldSettingsConv::FromData(ffg::ClampSettings(FourfoldSettingsConv::ToData(NewSettings)));

	ApplyEngineSettings();

	if (bSave)
	{
		const FString Path = FourfoldSettingsConv::SettingsPath();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		const std::string Json = ffg::SettingsToJson(FourfoldSettingsConv::ToData(Settings));
		if (!FFileHelper::SaveStringToFile(FString(UTF8_TO_TCHAR(Json.c_str())), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogFourfold, Warning, TEXT("Could not write %s"), *Path);
		}
	}
	OnChanged.Broadcast(Settings);
}

void UFourfoldSettingsSubsystem::ApplyEngineSettings()
{
	// Frame-rate cap (30 / 60 / 120; 120 needs a ProMotion device and the iOS frame-rate lock off, see REQUESTS.md).
	if (Settings.FrameRateCap != AppliedFrameRate)
	{
		if (IConsoleVariable* MaxFps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
		{
			MaxFps->Set(float(Settings.FrameRateCap), ECVF_SetByGameSetting);
			AppliedFrameRate = Settings.FrameRateCap;
		}
	}
	// Scalability: 0 low .. 2 high. Desktop maps to Medium / High / Epic, mobile to Low / Medium / High.
	const int32 Q = GetEffectiveQuality();
#if PLATFORM_IOS || PLATFORM_ANDROID
	const int32 Level = Q;
#else
	const int32 Level = Q + 1;
#endif
	if (Level != AppliedQualityLevel)
	{
		Scalability::FQualityLevels Levels = Scalability::GetQualityLevels();
		Levels.SetFromSingleQualityLevel(Level);
#if !(PLATFORM_IOS || PLATFORM_ANDROID)
		if (Level >= 3)
		{
			// Epic everywhere measured ~23 ms GPU on an M4 Pro (1600x900): Lumen gather and the 200 % TSR history are
			// most of it. High GI / reflections / AA look the same in this outdoor, sun-lit arena.
			Levels.GlobalIlluminationQuality = 2;
			Levels.ReflectionQuality = 2;
			Levels.AntiAliasingQuality = 2;
		}
#endif
		Scalability::SetQualityLevels(Levels);
		AppliedQualityLevel = Level;
		UE_LOG(LogFourfold, Log, TEXT("Scalability level %d (quality tier %d)"), Level, Q);
	}
}

bool UFourfoldSettingsSubsystem::StepDownAutoQuality()
{
	if (Settings.Quality >= 0 || AutoQuality <= 0)
	{
		return false;
	}
	--AutoQuality;
	ApplyEngineSettings();
	OnChanged.Broadcast(Settings);
	return true;
}

int32 UFourfoldSettingsSubsystem::GetEffectiveQuality() const
{
	return Settings.Quality >= 0 ? FMath::Clamp(Settings.Quality, 0, 2) : FMath::Clamp(AutoQuality, 0, 2);
}
