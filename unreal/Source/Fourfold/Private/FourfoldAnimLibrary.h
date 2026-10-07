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

/** One extra part of a MetaHuman (metahuman.json): a leader-posed mesh or a groom on the face. */
struct FFourfoldCharacterPart
{
	FString Name;        // Face | Torso | Legs | Feet | Hair | Eyebrows ...
	bool bGroom = false;
	FString Asset;       // skeletal mesh or groom asset
	FString Binding;     // groom binding asset (grooms)
	TArray<FString> Materials;   // per-slot override materials (empty entry = keep the mesh's)
	FName TintParam;             // vector parameter that tints this part per role (clothing)
};

/** character.json (stream `character`): mesh path, yaw offset and the role palettes. When metahuman.json is present
 *  (and its clips were built), the body becomes the MetaHuman body and Parts lists the rest of it. */
struct FFourfoldCharacterInfo
{
	FString MeshPath = TEXT("/Game/Fourfold/Characters/Fighter/SK_Fighter");
	double MeshYawOffsetDeg = -90.0;
	double HeightM = 1.79;
	TMap<FString, TMap<FName, FLinearColor>> Palettes;   // "player" | "rival" | "dummy" -> FF_Main / FF_Accent / FF_Trim
	FString Name = TEXT("Fighter");
	TArray<FFourfoldCharacterPart> Parts;
	TArray<FString> BodyMaterials;            // MetaHuman body slot overrides
	TMap<FString, FLinearColor> RoleTints;    // "player" | "rival" | "dummy" -> clothing tint
	bool bMetaHuman = false;
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
	void LoadMetaHumanJson();

	ffg::AnimLibrary Library;
	UPROPERTY() TArray<TObjectPtr<UAnimSequence>> Sequences;
	TSet<const USkeleton*> ValidatedSkeletons;
	FFourfoldCharacterInfo Character;
	int32 NumAvailable = 0;
};
