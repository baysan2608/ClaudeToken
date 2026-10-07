// FourfoldFX - Niagara cue layer (see FourfoldFxNiagara.h). Owner: stream `fx`.
#include "FourfoldFxNiagara.h"

#include "Engine/World.h"
#include "FourfoldCoords.h"
#include "GameFramework/Actor.h"
#include "Logic/FxConfig.h"
#include "Logic/FxDrawList.h"
#include "Misc/PackageName.h"
#include "NiagaraComponent.h"
#include "NiagaraEffectType.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "Particles/ParticleSystemComponent.h"

#include <string>

DEFINE_LOG_CATEGORY_STATIC(LogFourfoldFxNiagara, Log, All);

namespace
{
	constexpr int32 kMaxSpawnsPerFrame = 10;
	constexpr float kPrewarmScale = 0.1f;   // pre-warm instances: small, so nothing reaches above the floor that hides them
	constexpr double kPrewarmLife = 1.5;    // seconds (editor builds first compile the system; late emitters)

	FString ToFString(const std::string& S) { return UTF8_TO_TCHAR(S.c_str()); }
	FString CueName(int32 Cue) { return ToFString(std::string(ffx::NCueName(ffx::NCue(Cue)))); }

	// "User.Smoke Color" -> "Smoke Color"
	FString UserName(const FName& Name)
	{
		FString S = Name.ToString();
		S.RemoveFromStart(TEXT("User."));
		return S;
	}
}

bool FFourfoldFxNiagara::LoadSlot(const ffx::NiagaraSlot& Cfg, FSlot& S, const FString& Where, TMap<FString, UNiagaraSystem*>& Loaded,
	TArray<TObjectPtr<UObject>>& OutRefs)
{
	static TSet<FString> Described;   // the parameter list of each system is logged once per session
	S = FSlot();
	S.Scale = Cfg.scale;
	S.Life = Cfg.life;
	S.MinIntensity = Cfg.minIntensity;
	S.MinQuality = Cfg.minQuality;
	if (Cfg.path.empty())
	{
		return false;
	}
	FString Package = ToFString(Cfg.path);
	FString Object = Package;
	if (Package.Contains(TEXT(".")))
	{
		Package = Package.Left(Package.Find(TEXT(".")));
	}
	else
	{
		Object = Package + TEXT(".") + FPackageName::GetShortName(Package);
	}
	UNiagaraSystem** Found = Loaded.Find(Object);
	UNiagaraSystem* Sys = Found ? *Found : nullptr;
	if (!Found)
	{
		Sys = FPackageName::DoesPackageExist(Package)
			? LoadObject<UNiagaraSystem>(nullptr, *Object, nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
		Loaded.Add(Object, Sys);
		if (Sys)
		{
			OutRefs.AddUnique(Sys);
		}
	}
	if (!Sys)
	{
		return false;
	}
	S.System = Sys;

	TArray<FNiagaraVariable> Params;
	Sys->GetExposedParameters().GetUserParameters(Params);
	if (!Described.Contains(Object))
	{
		Described.Add(Object);
		FString List;
		for (const FNiagaraVariable& P : Params)
		{
			List += FString::Printf(TEXT("%s'%s' [%s]"), List.IsEmpty() ? TEXT("") : TEXT(", "), *UserName(P.GetName()),
				*P.GetType().GetName());
		}
		UE_LOG(LogFourfoldFxNiagara, Display, TEXT("Niagara %s: user parameters %s"), *FPackageName::GetShortName(Package),
			List.IsEmpty() ? TEXT("(none)") : *List);
	}
	for (const std::pair<std::string, std::string>& KV : Cfg.params)
	{
		const FString Want = ToFString(KV.first);
		const FNiagaraVariable* Var = Params.FindByPredicate([&Want](const FNiagaraVariable& P) { return UserName(P.GetName()) == Want; });
		if (!Var)
		{
			UE_LOG(LogFourfoldFxNiagara, Warning, TEXT("%s: %s has no user parameter '%s' (binding skipped)"), *Where,
				*FPackageName::GetShortName(Package), *Want);
			continue;
		}
		FBinding B;
		B.Name = Var->GetName();
		const FNiagaraTypeDefinition& T = Var->GetType();
		B.Type = T == FNiagaraTypeDefinition::GetFloatDef() ? EType::Float
			: T == FNiagaraTypeDefinition::GetIntDef() ? EType::Int
			: T == FNiagaraTypeDefinition::GetBoolDef() ? EType::Bool
			: T == FNiagaraTypeDefinition::GetColorDef() ? EType::Color
			: T == FNiagaraTypeDefinition::GetVec3Def() ? EType::Vec3
			: T == FNiagaraTypeDefinition::GetVec4Def() ? EType::Vec4
			: T == FNiagaraTypeDefinition::GetVec2Def() ? EType::Vec2
			: T == FNiagaraTypeDefinition::GetPositionDef() ? EType::Position : EType::Other;
		FString Src = ToFString(KV.second).TrimStartAndEnd();
		FString Factor;
		if (Src.Split(TEXT("*"), &Src, &Factor))
		{
			B.K = FCString::Atof(*Factor);
		}
		if (Src == TEXT("color")) B.Source = ESource::Color;
		else if (Src == TEXT("color2")) B.Source = ESource::Color2;
		else if (Src == TEXT("dir")) B.Source = ESource::Dir;
		else if (Src == TEXT("-dir")) B.Source = ESource::NegDir;
		else if (Src == TEXT("scale")) B.Source = ESource::Scale;
		else if (Src == TEXT("intensity")) B.Source = ESource::Intensity;
		else if (FCString::IsNumeric(*Src))
		{
			B.Source = ESource::Literal;
			B.K = FCString::Atof(*Src);
		}
		else
		{
			UE_LOG(LogFourfoldFxNiagara, Warning, TEXT("%s.params.%s: unknown source '%s'"), *Where, *Want, *Src);
			continue;
		}
		if (B.Type == EType::Other)
		{
			UE_LOG(LogFourfoldFxNiagara, Warning, TEXT("%s.params.%s: type %s is not supported"), *Where, *Want, *T.GetName());
			continue;
		}
		S.Bindings.Add(B);
	}
	return true;
}

void FFourfoldFxNiagara::Setup(const ffx::FxConfig& Config, int32 InQuality, TArray<TObjectPtr<UObject>>& OutRefs)
{
	Quality = InQuality;
	Mask = 0;
	LoopMask = 0;
	TMap<FString, UNiagaraSystem*> Loaded;   // one load per asset (several slots share a system)
	int32 NumLoaded = 0, NumMissing = 0, NumLoops = 0;
	for (int32 Cue = 0; Cue < ffx::kNumNCues; ++Cue)
	{
		const ffx::NiagaraSlot& Cfg = Config.niagara[size_t(Cue)];
		if (LoadSlot(Cfg, Slots[Cue], TEXT("niagara.") + CueName(Cue), Loaded, OutRefs))
		{
			Mask |= (uint64(1) << Cue);
			++NumLoaded;
		}
		else if (!Cfg.path.empty())
		{
			++NumMissing;
		}
	}
	for (int32 Cue = 0; Cue < ffx::kNumLCues; ++Cue)
	{
		const ffx::NiagaraSlot& Cfg = Config.niagaraLoops[size_t(Cue)];
		const FString Name = TEXT("niagara_loops.") + ToFString(std::string(ffx::LCueName(ffx::LCue(Cue))));
		if (LoadSlot(Cfg, LoopSlots[Cue], Name, Loaded, OutRefs))
		{
			LoopMask |= (uint64(1) << Cue);
			++NumLoops;
		}
		else if (!Cfg.path.empty())
		{
			++NumMissing;
		}
	}
	UE_LOG(LogFourfoldFxNiagara, Log, TEXT("Niagara: %d cue slots + %d persistent slots loaded, %d missing (procedural fallback)."),
		NumLoaded, NumLoops, NumMissing);
}

void FFourfoldFxNiagara::Bind(UNiagaraComponent& Comp, const FSlot& Slot, const FBindIn& Req) const
{
	const FVector& Location = Req.Location;
	const FVector& Dir = Req.Dir;
	for (const FBinding& B : Slot.Bindings)
	{
		switch (B.Source)
		{
			case ESource::Color:
			case ESource::Color2:
			{
				const ffx::Color& C = B.Source == ESource::Color ? Req.Color : Req.Color2;
				if (C.a <= 0.0f)
				{
					break;   // keep the system's own colour
				}
				const ffx::Color L = ffx::Linear(C);
				const FLinearColor Lin(L.r * B.K, L.g * B.K, L.b * B.K, 1.0f);
				switch (B.Type)
				{
					case EType::Color: Comp.SetVariableLinearColor(B.Name, Lin); break;
					case EType::Vec3: Comp.SetVariableVec3(B.Name, FVector(Lin.R, Lin.G, Lin.B)); break;
					case EType::Vec4: Comp.SetVariableVec4(B.Name, FVector4(Lin.R, Lin.G, Lin.B, 1.0)); break;
					case EType::Float: Comp.SetVariableFloat(B.Name, Lin.GetLuminance()); break;
					default: break;
				}
				break;
			}
			case ESource::Dir:
			case ESource::NegDir:
			{
				const FVector D = (B.Source == ESource::Dir ? Dir : -Dir) * double(B.K);
				switch (B.Type)
				{
					case EType::Vec3: Comp.SetVariableVec3(B.Name, D); break;
					case EType::Vec4: Comp.SetVariableVec4(B.Name, FVector4(D.X, D.Y, D.Z, 0.0)); break;
					case EType::Position: Comp.SetVariablePosition(B.Name, Location + D * 100.0); break;
					default: break;
				}
				break;
			}
			default:
			{
				const float V = B.Source == ESource::Scale ? Req.Scale * B.K
					: B.Source == ESource::Intensity ? Req.Intensity * B.K : B.K;
				switch (B.Type)
				{
					case EType::Float: Comp.SetVariableFloat(B.Name, V); break;
					case EType::Int: Comp.SetVariableInt(B.Name, FMath::RoundToInt(V)); break;
					case EType::Bool: Comp.SetVariableBool(B.Name, V != 0.0f); break;
					case EType::Vec3: Comp.SetVariableVec3(B.Name, FVector(V)); break;
					case EType::Vec2: Comp.SetVariableVec2(B.Name, FVector2D(V, V)); break;
					case EType::Color: Comp.SetVariableLinearColor(B.Name, FLinearColor(V, V, V, 1.0f)); break;
					default: break;
				}
				break;
			}
		}
	}
}

void FFourfoldFxNiagara::Spawn(AActor* Owner, const ffx::DrawList& List, bool bSpawnNew)
{
	UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	// timed systems are ManualRelease: no other spawn can be handed the component before this releases it
	for (int32 i = Timed.Num() - 1; i >= 0; --i)
	{
		if (Now >= Timed[i].StopAt)
		{
			if (UNiagaraComponent* C = Timed[i].Comp.Get())
			{
				if (Timed[i].bKill)
				{
					C->DeactivateImmediate();   // pre-warm instance: particles go too
					C->SetOcclusionQueryMode(ENiagaraOcclusionQueryMode::Default);   // the pool hands it out as is
				}
				C->ReleaseToPool();   // stops emitting; back to the pool once the live particles finished
			}
			Timed.RemoveAtSwap(i);
		}
	}
	Live.RemoveAllSwap([](const TWeakObjectPtr<UNiagaraComponent>& C) { return !C.IsValid() || !C->IsActive(); });
	LastSpawns = 0;
	if (!World || Mask == 0 || !bSpawnNew)
	{
		return;
	}
	for (const ffx::SystemReq& R : List.systems)
	{
		const int32 Cue = int32(R.cue);
		if (Cue < 0 || Cue >= ffx::kNumNCues || LastSpawns >= kMaxSpawnsPerFrame)
		{
			continue;
		}
		const FSlot& S = Slots[Cue];
		if (!Usable(S) || R.intensity < S.MinIntensity)
		{
			continue;
		}
		const FVector Location = FF::ToUE(R.pos);
		FVector Dir = FF::DirToUE(R.dir).GetSafeNormal();
		if (Dir.IsNearlyZero())
		{
			Dir = FVector::UpVector;
		}
		const FRotator Rotation = FRotationMatrix::MakeFromZ(Dir).Rotator();
		const float Scale = FMath::Max(0.01f, S.Scale * R.scale);
		const bool bTimed = S.Life > 0.0f;
		UNiagaraComponent* Comp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(Owner, S.System, Location, Rotation,
			FVector(Scale), true, false, bTimed ? ENCPoolMethod::ManualRelease : ENCPoolMethod::AutoRelease, true);
		if (!Comp)
		{
			continue;   // culled (effect type budget / not visible)
		}
		Bind(*Comp, S, {Location, Dir, R.scale, R.intensity, R.color, R.color2});
		Comp->Activate(true);
		++LastSpawns;
		++TotalSpawns;
		if (bTimed)
		{
			Timed.Add({Comp, Now + double(S.Life)});
		}
		else
		{
			Live.Add(Comp);
		}
	}
}

int32 FFourfoldFxNiagara::Prewarm(AActor* Owner, const FVector& Location)
{
	UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	if (!World || Mask == 0)
	{
		return 0;
	}
	const double Now = World->GetTimeSeconds();
	int32 Num = 0;
	TArray<const FSlot*, TInlineAllocator<48>> All;
	for (const FSlot& S : Slots) All.Add(&S);
	for (const FSlot& S : LoopSlots) All.Add(&S);
	for (const FSlot* SP : All)
	{
		const FSlot& S = *SP;
		if (!Usable(S) || Prewarmed.Contains(S.System))
		{
			continue;
		}
		Prewarmed.Add(S.System);
		FFXSystemSpawnParameters P;
		P.WorldContextObject = Owner;
		P.SystemTemplate = S.System;
		P.Location = Location;
		P.Scale = FVector(kPrewarmScale);
		P.bAutoDestroy = true;
		P.bAutoActivate = true;
		P.PoolingMethod = EPSCPoolMethod::ManualRelease;
		P.bPreCullCheck = false;
		P.bIsPlayerEffect = true;   // exempt from distance / visibility culling: it has to render once
		UNiagaraComponent* Comp = UNiagaraFunctionLibrary::SpawnSystemAtLocationWithParams(P);
		if (!Comp)
		{
			continue;
		}
		// never occlusion-culled: it must draw (the floor's depth hides it) whenever it has particles - in editor builds
		// those only appear after the system compile, when an occludable proxy would long be culled
		Comp->SetOcclusionQueryMode(ENiagaraOcclusionQueryMode::AlwaysDisabled);
		Timed.Add({Comp, Now + kPrewarmLife, true});
		++Num;
		const FNiagaraSystemScalabilitySettings& Sc = S.System->GetScalabilitySettings();
		UE_LOG(LogFourfoldFxNiagara, Log, TEXT("Pre-warm %s (culling: distance %s, not rendered %s, outside frustum %s)"),
			*S.System->GetName(), Sc.bCullByDistance ? *FString::Printf(TEXT("%.0f cm"), Sc.MaxDistance) : TEXT("off"),
			Sc.VisibilityCulling.bCullWhenNotRendered ? *FString::Printf(TEXT("%.1f s"), Sc.VisibilityCulling.MaxTimeWithoutRender) : TEXT("off"),
			Sc.VisibilityCulling.bCullByViewFrustum ? *FString::Printf(TEXT("%.1f s"), Sc.VisibilityCulling.MaxTimeOutsideViewFrustum) : TEXT("off"));
	}
	return Num;
}

bool FFourfoldFxNiagara::NeedsPrewarm() const
{
	for (const FSlot& S : Slots)
	{
		if (Usable(S) && !Prewarmed.Contains(S.System))
		{
			return true;
		}
	}
	for (const FSlot& S : LoopSlots)
	{
		if (Usable(S) && !Prewarmed.Contains(S.System))
		{
			return true;
		}
	}
	return false;
}

void FFourfoldFxNiagara::UpdateLoops(AActor* Owner, const ffx::DrawList& List, bool bEnabled)
{
	UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	TSet<uint32> Seen;
	if (World && bEnabled && LoopMask != 0)
	{
		for (const ffx::LoopReq& R : List.loops)
		{
			const int32 Cue = int32(R.cue);
			if (Cue < 0 || Cue >= ffx::kNumLCues || !Usable(LoopSlots[Cue]))
			{
				continue;
			}
			const FSlot& S = LoopSlots[Cue];
			const FVector Location = FF::ToUE(R.pos);
			FVector Dir = FF::DirToUE(R.dir).GetSafeNormal();
			if (Dir.IsNearlyZero())
			{
				Dir = FVector::UpVector;
			}
			const FRotator Rotation = FRotationMatrix::MakeFromZ(Dir).Rotator();
			const float Scale = FMath::Max(0.01f, S.Scale * R.scale);
			FLoop* L = Loops.Find(R.key);
			UNiagaraComponent* Comp = (L && L->Cue == Cue) ? L->Comp.Get() : nullptr;
			if (L && !Comp)
			{
				if (UNiagaraComponent* Old = L->Comp.Get())
				{
					Old->ReleaseToPool();
				}
				Loops.Remove(R.key);
			}
			if (!Comp)
			{
				// ManualRelease: it is ours until the key goes away; no pre-cull (a persistent look must not miss its start)
				Comp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(Owner, S.System, Location, Rotation, FVector(Scale), true, false,
					ENCPoolMethod::ManualRelease, false);
				if (!Comp)
				{
					continue;
				}
				Bind(*Comp, S, {Location, Dir, R.scale, R.intensity, R.color, R.color2});
				Comp->Activate(true);
				Loops.Add(R.key, {Comp, Cue});
			}
			else
			{
				Comp->SetWorldLocationAndRotation(Location, Rotation);
				Comp->SetWorldScale3D(FVector(Scale));
				Bind(*Comp, S, {Location, Dir, R.scale, R.intensity, R.color, R.color2});
			}
			Seen.Add(R.key);
		}
	}
	for (auto It = Loops.CreateIterator(); It; ++It)
	{
		if (!Seen.Contains(It.Key()))
		{
			if (UNiagaraComponent* Comp = It.Value().Comp.Get())
			{
				Comp->ReleaseToPool();   // stops emitting; back to the pool once its particles are gone
			}
			It.RemoveCurrent();
		}
	}
}

void FFourfoldFxNiagara::StopAll()
{
	for (const TWeakObjectPtr<UNiagaraComponent>& C : Live)
	{
		if (UNiagaraComponent* Comp = C.Get())
		{
			Comp->DeactivateImmediate();   // AutoRelease: returns to the pool by itself
		}
	}
	for (const FTimed& T : Timed)
	{
		if (UNiagaraComponent* Comp = T.Comp.Get())
		{
			Comp->DeactivateImmediate();
			if (T.bKill)
			{
				Comp->SetOcclusionQueryMode(ENiagaraOcclusionQueryMode::Default);
			}
			Comp->ReleaseToPool();
		}
	}
	for (const TPair<uint32, FLoop>& L : Loops)
	{
		if (UNiagaraComponent* Comp = L.Value.Comp.Get())
		{
			Comp->DeactivateImmediate();
			Comp->ReleaseToPool();
		}
	}
	Live.Reset();
	Timed.Reset();
	Loops.Reset();
}

FString FFourfoldFxNiagara::GetDebugLine() const
{
	return FString::Printf(TEXT("niagara: %d + %d slots, %d live, %d loops, %d this frame, %d total, %d pre-warmed"),
		FMath::CountBits64(Mask), FMath::CountBits64(LoopMask), Live.Num() + Timed.Num(), Loops.Num(), LastSpawns, TotalSpawns,
		Prewarmed.Num());
}
