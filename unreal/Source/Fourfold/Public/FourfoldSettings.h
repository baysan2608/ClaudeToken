// Fourfold - player settings shared by every module (persisted by the game module).
// FROZEN CONTRACT (architect): fields / functions below are fixed (additive changes allowed). Owner: stream `game`.
// Port of game/ui/game_settings.gd (+ audio volumes, quality, frame rate, touch UI mode).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "FourfoldSettings.generated.h"

USTRUCT(BlueprintType)
struct FOURFOLD_API FFourfoldSettings
{
	GENERATED_BODY()

	// Touch layout
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch") float ControlScale = 1.0f;      // 0.8 .. 1.4
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch") float ControlOpacity = 0.8f;    // 0.3 .. 1.0
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch") FString LayoutPreset = TEXT("default");  // default | compact | wide
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch") bool bLeftHanded = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch") bool bStrongLabels = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Touch") int32 TouchUiMode = 0;          // 0 auto, 1 always, 2 never
	// Camera
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") float CameraSensitivity = 1.0f; // 0.3 .. 2.5
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") bool bInvertY = false;
	// Comfort / accessibility (read by camera shake, hit-stop, flashes, VFX)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfort") float ScreenShake = 1.0f;      // 0 .. 1
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfort") float Flashes = 1.0f;          // 0 .. 1
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfort") bool bHaptics = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfort") bool bReducedMotion = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comfort") bool bSlowmoAssist = false;
	// Audio (0 .. 1)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio") float MasterVolume = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio") float SfxVolume = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio") float AmbienceVolume = 0.8f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio") float UiVolume = 0.8f;
	// Graphics
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Graphics") int32 Quality = -1;           // -1 auto, 0 low, 1 medium, 2 high
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Graphics") int32 FrameRateCap = 60;      // 30 | 60 | 120 (ProMotion)
	// Dev
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dev") bool bShowDebug = false;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnFourfoldSettingsChanged, const FFourfoldSettings&);

UCLASS()
class FOURFOLD_API UFourfoldSettingsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UFourfoldSettingsSubsystem* Get(const UObject* WorldContextObject);

	const FFourfoldSettings& GetSettings() const { return Settings; }
	/** Clamps every field, stores, broadcasts OnChanged and (optionally) saves to Saved/Fourfold/settings.json. */
	void SetSettings(const FFourfoldSettings& NewSettings, bool bSave = true);
	/** Effective quality 0..2 (resolves -1 = auto from device performance). */
	int32 GetEffectiveQuality() const;

	FOnFourfoldSettingsChanged OnChanged;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

protected:
	UPROPERTY() FFourfoldSettings Settings;
	int32 AutoQuality = 2;
};
