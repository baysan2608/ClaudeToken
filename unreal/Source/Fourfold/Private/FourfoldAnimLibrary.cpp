// Fourfold - animation clip library subsystem (see FourfoldAnimLibrary.h).
#include "FourfoldAnimLibrary.h"

#include "FourfoldLog.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ff/Json.h"
#include "ff/Value.h"

#include <string>

namespace FourfoldAnimLib
{
	static FString ToF(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

	static bool ReadData(const TCHAR* Name, std::string& Out)
	{
		FString Text;
		const FString Path = FPaths::ProjectContentDir() / TEXT("Fourfold") / TEXT("Data") / Name;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			return false;
		}
		Out = std::string(TCHAR_TO_UTF8(*Text));
		return true;
	}

	static FString ObjectPath(const FString& Root, const FString& Asset)
	{
		FString R = Root;
		R.RemoveFromEnd(TEXT("/"));
		// the asset may sit in a sub-folder of the root ("Mocap/A_mm_walk"): the object name is the leaf
		const FString Leaf = FPaths::GetCleanFilename(Asset);
		return FString::Printf(TEXT("%s/%s.%s"), *R, *Asset, *Leaf);
	}
}

UFourfoldAnimLibrarySubsystem* UFourfoldAnimLibrarySubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UFourfoldAnimLibrarySubsystem>() : nullptr;
}

void UFourfoldAnimLibrarySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadJson();
	LoadCharacterJson();
	LoadMetaHumanJson();   // may switch the body mesh and the clip asset root before the clips load
	LoadAssets();
}

void UFourfoldAnimLibrarySubsystem::LoadJson()
{
	std::string Text;
	if (FourfoldAnimLib::ReadData(TEXT("clips.json"), Text))
	{
		Library.LoadClipsJson(Text);
	}
	// Overlay written by Content/Python/fourfold/animation/mocap.py (retargeted motion capture); merged key by key.
	if (FourfoldAnimLib::ReadData(TEXT("clips_mocap.json"), Text))
	{
		Library.LoadClipsJson(Text);
	}
	else
	{
		UE_LOG(LogFourfold, Warning, TEXT("Content/Fourfold/Data/clips.json not found: using the built-in clip catalogue"));
	}
	if (FourfoldAnimLib::ReadData(TEXT("anim_map.json"), Text))
	{
		Library.LoadAnimMapJson(Text);
	}
	if (FourfoldAnimLib::ReadData(TEXT("anim_map_mocap.json"), Text))
	{
		Library.LoadAnimMapJson(Text);
	}
	else
	{
		UE_LOG(LogFourfold, Warning, TEXT("Content/Fourfold/Data/anim_map.json not found: using the built-in move map"));
	}
	for (const std::string& W : Library.warnings)
	{
		UE_LOG(LogFourfold, Warning, TEXT("Anim library: %s"), *FourfoldAnimLib::ToF(W));
	}
}

void UFourfoldAnimLibrarySubsystem::LoadAssets()
{
	Sequences.Reset();
	NumAvailable = 0;
	TArray<FString> Missing;
	const FString Root = FourfoldAnimLib::ToF(Library.asset_root);
	for (auto& It : Library.clips)
	{
		ffg::ClipDef& C = It.second;
		C.available = false;
		C.handle = -1;
		const FString Path = FourfoldAnimLib::ObjectPath(Root, FourfoldAnimLib::ToF(C.asset));
		UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Seq)
		{
			Missing.Add(FourfoldAnimLib::ToF(It.first));
			continue;
		}
		C.handle = Sequences.Add(Seq);
		C.available = true;
		const float Len = float(Seq->GetPlayLength());
		if (Len > 0.0f)
		{
			C.duration = Len;   // the asset is the truth for the length; frames / contacts stay in 60 fps units
		}
		++NumAvailable;
	}
	UE_LOG(LogFourfold, Log, TEXT("Anim library: %d of %d clips loaded from %s"), NumAvailable, int32(Library.clips.size()), *Root);
	if (Missing.Num() > 0)
	{
		UE_LOG(LogFourfold, Warning, TEXT("Anim library: %d clips have no asset yet (fallbacks apply): %s"), Missing.Num(),
		       *FString::Join(Missing, TEXT(", ")));
	}
}

void UFourfoldAnimLibrarySubsystem::ValidateAgainstSkeleton(const USkeleton* Skeleton)
{
	if (!Skeleton || ValidatedSkeletons.Contains(Skeleton))
	{
		return;
	}
	ValidatedSkeletons.Add(Skeleton);
	TArray<FString> Bad;
	for (auto& It : Library.clips)
	{
		ffg::ClipDef& C = It.second;
		UAnimSequence* Seq = GetSequence(C.handle);
		if (C.available && Seq && Seq->GetSkeleton() != Skeleton)
		{
			C.available = false;
			Bad.Add(FourfoldAnimLib::ToF(It.first));
			--NumAvailable;
		}
	}
	if (Bad.Num() > 0)
	{
		UE_LOG(LogFourfold, Warning, TEXT("Anim library: %d clips use another skeleton than %s and are disabled: %s"), Bad.Num(),
		       *Skeleton->GetName(), *FString::Join(Bad, TEXT(", ")));
	}
}

UAnimSequence* UFourfoldAnimLibrarySubsystem::GetSequence(int32 Handle) const
{
	return Sequences.IsValidIndex(Handle) ? Sequences[Handle].Get() : nullptr;
}

void UFourfoldAnimLibrarySubsystem::LoadCharacterJson()
{
	// Defaults = ARCHITECTURE §8.2 (linear colours).
	auto SetPal = [this](const TCHAR* Role, FLinearColor Main, FLinearColor Accent, FLinearColor Trim) {
		TMap<FName, FLinearColor>& P = Character.Palettes.FindOrAdd(Role);
		P.Add(TEXT("FF_Main"), Main);
		P.Add(TEXT("FF_Accent"), Accent);
		P.Add(TEXT("FF_Trim"), Trim);
	};
	SetPal(TEXT("player"), FLinearColor(0.19f, 0.23f, 0.31f), FLinearColor(0.62f, 0.50f, 0.32f), FLinearColor(0.85f, 0.82f, 0.74f));
	SetPal(TEXT("rival"), FLinearColor(0.40f, 0.20f, 0.15f), FLinearColor(0.78f, 0.58f, 0.28f), FLinearColor(0.20f, 0.18f, 0.17f));
	SetPal(TEXT("dummy"), FLinearColor(0.56f, 0.50f, 0.40f), FLinearColor(0.40f, 0.36f, 0.30f), FLinearColor(0.70f, 0.66f, 0.58f));

	std::string Text;
	if (!FourfoldAnimLib::ReadData(TEXT("character.json"), Text))
	{
		UE_LOG(LogFourfold, Warning, TEXT("Content/Fourfold/Data/character.json not found: default mesh path, yaw offset and palettes"));
		return;
	}
	ff::Value Root;
	if (!ff::ParseJson(Text, Root) || !Root.is_dict())
	{
		UE_LOG(LogFourfold, Warning, TEXT("character.json does not parse: defaults used"));
		return;
	}
	if (Root["mesh"].is_string())
	{
		Character.MeshPath = FourfoldAnimLib::ToF(Root["mesh"].as_string());
	}
	if (Root["mesh_yaw_offset_deg"].is_number())
	{
		Character.MeshYawOffsetDeg = Root["mesh_yaw_offset_deg"].as_float();
	}
	if (Root["height_m"].is_number())
	{
		Character.HeightM = Root["height_m"].as_float();
	}
	const ff::Value& Pals = Root["palettes"];
	if (Pals.is_dict())
	{
		for (const auto& RoleIt : Pals.as_dict())
		{
			if (!RoleIt.second.is_dict())
			{
				continue;
			}
			TMap<FName, FLinearColor>& P = Character.Palettes.FindOrAdd(FourfoldAnimLib::ToF(RoleIt.first));
			for (const auto& ParamIt : RoleIt.second.as_dict())
			{
				const ff::Value& C = ParamIt.second;
				// [r, g, b, a] (the exporter may also tag colours as {"$color": [...]}, decoded to the same array)
				if (C.is_array() && C.as_array().size() >= 3)
				{
					const ff::Array A = C.as_array();
					const float Alpha = A.size() >= 4 ? A[3].as_f32(1.0f) : 1.0f;
					P.Add(FName(*FourfoldAnimLib::ToF(ParamIt.first)), FLinearColor(A[0].as_f32(), A[1].as_f32(), A[2].as_f32(), Alpha));
				}
			}
		}
	}
}

void UFourfoldAnimLibrarySubsystem::LoadMetaHumanJson()
{
	// -FFCharacter=fighter keeps the original fighter even when a MetaHuman is set up.
	FString Want;
	if (FParse::Value(FCommandLine::Get(), TEXT("-FFCharacter="), Want) && Want.Equals(TEXT("fighter"), ESearchCase::IgnoreCase))
	{
		return;
	}
	std::string Text;
	if (!FourfoldAnimLib::ReadData(TEXT("metahuman.json"), Text))
	{
		return;
	}
	ff::Value Root;
	if (!ff::ParseJson(Text, Root) || !Root.is_dict() || !Root["body"].is_string() || !Root["anim_root"].is_string())
	{
		UE_LOG(LogFourfold, Log, TEXT("metahuman.json present but not built yet (no anim_root): keeping the fighter"));
		return;
	}
	const FString Body = FourfoldAnimLib::ToF(Root["body"].as_string());
	const FString BodyObj = Body + TEXT(".") + FPaths::GetBaseFilename(Body);
	if (!LoadObject<UObject>(nullptr, *BodyObj, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		UE_LOG(LogFourfold, Warning, TEXT("MetaHuman body %s missing (content not copied in?): keeping the fighter"), *Body);
		return;
	}
	Character.bMetaHuman = true;
	Character.MeshPath = Body;
	// the rendered body: a complete body of the same type when the setup made one (Kellan's own body is partial)
	if (Root["body_mesh"].is_string())
	{
		const FString Full = FourfoldAnimLib::ToF(Root["body_mesh"].as_string());
		if (LoadObject<UObject>(nullptr, *(Full + TEXT(".") + FPaths::GetBaseFilename(Full)), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			Character.MeshPath = Full;
		}
	}
	Character.Name = Root["name"].is_string() ? FourfoldAnimLib::ToF(Root["name"].as_string()) : TEXT("MetaHuman");
	Library.asset_root = Root["anim_root"].as_string();
	const ff::Value& Parts = Root["parts"];
	if (Parts.is_array())
	{
		for (const ff::Value& P : Parts.as_array())
		{
			if (!P.is_dict() || !P["asset"].is_string() || !P["name"].is_string())
			{
				continue;
			}
			// outfit: the setup part disables MetaHuman clothing the fighters don't wear (e.g. hoodie, shoes)
			if (P["enabled"].is_bool() && !P["enabled"].as_bool())
			{
				continue;
			}
			FFourfoldCharacterPart Part;
			Part.Name = FourfoldAnimLib::ToF(P["name"].as_string());
			TArray<FString> Mats;
			if (P["materials"].is_array())
			{
				for (const ff::Value& M : P["materials"].as_array())
				{
					Mats.Add(M.is_string() ? FourfoldAnimLib::ToF(M.as_string()) : FString());
				}
			}
			if (Part.Name == TEXT("Body"))
			{
				Character.BodyMaterials = Mats;
				continue;
			}
			Part.Materials = Mats;
			if (P["tint_param"].is_string())
			{
				Part.TintParam = FName(*FourfoldAnimLib::ToF(P["tint_param"].as_string()));
			}
			Part.bGroom = P["kind"].is_string() && P["kind"].as_string() == "groom";
			Part.Asset = FourfoldAnimLib::ToF(P["asset"].as_string());
			if (P["binding"].is_string())
			{
				Part.Binding = FourfoldAnimLib::ToF(P["binding"].as_string());
			}
			Character.Parts.Add(Part);
		}
	}
	const ff::Value& Tints = Root["tints"];
	if (Tints.is_dict())
	{
		for (const auto& It : Tints.as_dict())
		{
			const ff::Value& C = It.second;
			if (C.is_array() && C.as_array().size() >= 3)
			{
				Character.RoleTints.Add(FourfoldAnimLib::ToF(It.first),
				                        FLinearColor(float(C.as_array()[0].as_float()), float(C.as_array()[1].as_float()),
				                                     float(C.as_array()[2].as_float()), 1.0f));
			}
		}
	}
	UE_LOG(LogFourfold, Log, TEXT("Character: MetaHuman %s (%d parts), clips from %s"), *Character.Name, Character.Parts.Num(),
	       *FourfoldAnimLib::ToF(Library.asset_root));
}
