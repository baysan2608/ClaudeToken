// FourfoldFX - Niagara cue layer (see FourfoldFxNiagara.h). Owner: stream `fx`.
#include "FourfoldFxNiagara.h"

#include "Engine/World.h"
#include "FourfoldCoords.h"
#include "GameFramework/Actor.h"
#include "Logic/FxConfig.h"
#include "Logic/FxDrawList.h"
#include "Misc/PackageName.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"

#include <string>

DEFINE_LOG_CATEGORY_STATIC(LogFourfoldFxNiagara, Log, All);

namespace
{
	constexpr int32 kMaxSpawnsPerFrame = 10;

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

void FFourfoldFxNiagara::Setup(const ffx::FxConfig& Config, int32 InQuality, TArray<TObjectPtr<UObject>>& OutRefs)
{
	Quality = InQuality;
	Mask = 0;
	TMap<FString, UNiagaraSystem*> Loaded;   // one load per asset (several slots share a system)
	static TSet<FString> Described;          // the parameter list of each system is logged once per session
	int32 NumLoaded = 0, NumMissing = 0;
	for (int32 Cue = 0; Cue < ffx::kNumNCues; ++Cue)
	{
		const ffx::NiagaraSlot& Cfg = Config.niagara[size_t(Cue)];
		FSlot& S = Slots[Cue];
		S = FSlot();
		S.Scale = Cfg.scale;
		S.Life = Cfg.life;
		S.MinIntensity = Cfg.minIntensity;
		S.MinQuality = Cfg.minQuality;
		if (Cfg.path.empty())
		{
			continue;
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
			++NumMissing;
			continue;
		}
		S.System = Sys;
		Mask |= (uint64(1) << Cue);
		++NumLoaded;

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
				UE_LOG(LogFourfoldFxNiagara, Warning, TEXT("niagara.%s: %s has no user parameter '%s' (binding skipped)"),
					*CueName(Cue), *FPackageName::GetShortName(Package), *Want);
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
				UE_LOG(LogFourfoldFxNiagara, Warning, TEXT("niagara.%s.params.%s: unknown source '%s'"), *CueName(Cue), *Want, *Src);
				continue;
			}
			if (B.Type == EType::Other)
			{
				UE_LOG(LogFourfoldFxNiagara, Warning, TEXT("niagara.%s.params.%s: type %s is not supported"), *CueName(Cue), *Want,
					*T.GetName());
				continue;
			}
			S.Bindings.Add(B);
		}
	}
	UE_LOG(LogFourfoldFxNiagara, Log, TEXT("Niagara cue slots: %d loaded, %d missing (procedural fallback)."), NumLoaded, NumMissing);
}

void FFourfoldFxNiagara::Bind(UNiagaraComponent& Comp, const FSlot& Slot, const ffx::SystemReq& Req, const FVector& Location,
	const FVector& Dir) const
{
	for (const FBinding& B : Slot.Bindings)
	{
		switch (B.Source)
		{
			case ESource::Color:
			case ESource::Color2:
			{
				const ffx::Color& C = B.Source == ESource::Color ? Req.color : Req.color2;
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
				const float V = B.Source == ESource::Scale ? Req.scale * B.K
					: B.Source == ESource::Intensity ? Req.intensity * B.K : B.K;
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
	for (int32 i = Timed.Num() - 1; i >= 0; --i)
	{
		if (Now >= Timed[i].StopAt)
		{
			if (UNiagaraComponent* C = Timed[i].Comp.Get())
			{
				C->Deactivate();   // stop emitting; live particles finish
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
		if (!S.System || Quality < S.MinQuality || R.intensity < S.MinIntensity)
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
		UNiagaraComponent* Comp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(Owner, S.System, Location, Rotation,
			FVector(Scale), true, false, ENCPoolMethod::AutoRelease, true);
		if (!Comp)
		{
			continue;   // culled (effect type budget / not visible)
		}
		Bind(*Comp, S, R, Location, Dir);
		Comp->Activate(true);
		++LastSpawns;
		++TotalSpawns;
		Live.Add(Comp);
		if (S.Life > 0.0f)
		{
			Timed.Add({Comp, Now + double(S.Life)});
		}
	}
}

void FFourfoldFxNiagara::StopAll()
{
	for (const TWeakObjectPtr<UNiagaraComponent>& C : Live)
	{
		if (UNiagaraComponent* Comp = C.Get())
		{
			Comp->DeactivateImmediate();
		}
	}
	Live.Reset();
	Timed.Reset();
}

FString FFourfoldFxNiagara::GetDebugLine() const
{
	return FString::Printf(TEXT("niagara: %d slots, %d live, %d this frame, %d total"), FMath::CountBits64(Mask), Live.Num(),
		LastSpawns, TotalSpawns);
}
