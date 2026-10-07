// FourfoldFX - Niagara cue layer: spawns the Niagara system configured for each ffx::SystemReq (fx_config.json
// "niagara"; Epic's free Niagara Examples Pack by default) and binds the request's colours / direction / strength to
// the system's user parameters, converted to each parameter's own type. A slot whose asset is missing (a clone
// without the pack) is simply not loaded: its bit stays 0 in LoadedMask() and the logic keeps the procedural
// one-shot. Components come from Niagara's world pool: AutoRelease (fire and forget), or ManualRelease for systems
// this layer stops itself (slot `life`, pre-warm), so a component is never stopped after the pool handed it to another
// cue. Owner: stream `fx`.
#pragma once

#include "CoreMinimal.h"
#include "Logic/FxTypes.h"
#include "UObject/ObjectKey.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AActor;
class UNiagaraComponent;
class UNiagaraSystem;

namespace ffx
{
	struct DrawList;
	struct FxConfig;
	struct SystemReq;
}

struct FFourfoldFxNiagara
{
	/** Resolves every slot's system and parameter bindings; loaded systems are appended to OutRefs (kept for the GC). */
	void Setup(const ffx::FxConfig& Config, int32 Quality, TArray<TObjectPtr<UObject>>& OutRefs);
	/** Stops the timed systems whose life ended, then spawns this frame's requests (unless bSpawnNew is false). */
	void Spawn(AActor* Owner, const ffx::DrawList& List, bool bSpawnNew = true);
	/** Stops every system this layer started (scenario change / effects off). */
	void StopAll();
	/** Pre-warm: spawns each loaded slot system once at Location (the caller picks a point hidden behind the arena
	 *  floor), small and exempt from culling, so its first-use costs (render / compute pipeline creation, shader
	 *  loading, in editor builds the system compile) are paid at scenario load instead of on the first real cue.
	 *  Once per system and world; the components go back to Niagara's world pool after kPrewarmLife. Returns the
	 *  number of systems spawned. */
	int32 Prewarm(AActor* Owner, const FVector& Location);
	/** True while a loaded slot system usable at the current quality has not been pre-warmed. */
	bool NeedsPrewarm() const;

	uint64 LoadedMask() const { return Mask; }
	FString GetDebugLine() const;

private:
	enum class EType : uint8 { Float, Int, Bool, Color, Vec3, Vec4, Vec2, Position, Other };
	enum class ESource : uint8 { Color, Color2, Dir, NegDir, Scale, Intensity, Literal };
	struct FBinding
	{
		FName Name;
		EType Type = EType::Other;
		ESource Source = ESource::Literal;
		float K = 1.0f;   // factor (scale / intensity) or the literal value
	};
	struct FSlot
	{
		UNiagaraSystem* System = nullptr;   // referenced by the owner's UPROPERTY array
		TArray<FBinding> Bindings;
		float Scale = 1.0f;
		float Life = 0.0f;
		float MinIntensity = 0.0f;
		int32 MinQuality = 1;
	};
	struct FTimed
	{
		TWeakObjectPtr<UNiagaraComponent> Comp;
		double StopAt = 0.0;
		bool bKill = false;   // pre-warm: particles go too (DeactivateImmediate)
	};

	void Bind(UNiagaraComponent& Comp, const FSlot& Slot, const ffx::SystemReq& Req, const FVector& Location, const FVector& Dir) const;

	bool Usable(const FSlot& S) const { return S.System && Quality >= S.MinQuality; }

	FSlot Slots[ffx::kNumNCues];
	TArray<FTimed> Timed;
	TArray<TWeakObjectPtr<UNiagaraComponent>> Live;
	TSet<TObjectKey<UNiagaraSystem>> Prewarmed;
	uint64 Mask = 0;
	int32 Quality = 2;
	int32 LastSpawns = 0;
	int32 TotalSpawns = 0;
};
