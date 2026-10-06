// Fourfold - the animation clip library shared by every fighter: the logic-island catalogue (built-in defaults from
// MARTIAL_ARTS.md, overridden by Content/Fourfold/Data/clips.json + anim_map.json) and the UAnimSequence assets it
// resolves to ('<asset_root>/<asset>.<asset>'). Loaded once per game instance; every missing piece is reported once
// and falls back (clip fallbacks -> slot fallbacks -> element stance -> reference pose).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Logic/FFGAnimLibrary.h"
#include "FourfoldAnimLibrary.generated.h"

class UAnimSequence;
class USkeleton;

/** character.json (stream `character`): mesh path, yaw offset and the role palettes. */
struct FFourfoldCharacterInfo
{
	FString MeshPath = TEXT("/Game/Fourfold/Characters/Fighter/SK_Fighter");
	double MeshYawOffsetDeg = -90.0;
	double HeightM = 1.79;
	TMap<FString, TMap<FName, FLinearColor>> Palettes;   // "player" | "rival" | "dummy" -> FF_Main / FF_Accent / FF_Trim
};

UCLASS()
class UFourfoldAnimLibrarySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UFourfoldAnimLibrarySubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	const ffg::AnimLibrary& GetLibrary() const { return Library; }
	UAnimSequence* GetSequence(int32 Handle) const;
	UAnimSequence* GetSequence(const ffg::ClipDef* Clip) const { return Clip ? GetSequence(Clip->handle) : nullptr; }
	const FFourfoldCharacterInfo& GetCharacterInfo() const { return Character; }
	/** Disables every clip whose skeleton differs from the fighter mesh's (once per skeleton). */
	void ValidateAgainstSkeleton(const USkeleton* Skeleton);
	int32 NumAvailableClips() const { return NumAvailable; }

private:
	void LoadJson();
	void LoadAssets();
	void LoadCharacterJson();

	ffg::AnimLibrary Library;
	UPROPERTY() TArray<TObjectPtr<UAnimSequence>> Sequences;
	TSet<const USkeleton*> ValidatedSkeletons;
	FFourfoldCharacterInfo Character;
	int32 NumAvailable = 0;
};
