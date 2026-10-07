// FourfoldFX - pooled component renderer for the logic's DrawList. Owner: stream `fx`.
#include "FourfoldFxActor.h"

#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "FourfoldFighter.h"
#include "FourfoldFxDebris.h"
#include "FourfoldFxNiagara.h"
#include "FourfoldSimSubsystem.h"
#include "FxUeConvert.h"
#include "Logic/FxConfig.h"
#include "Logic/FxContext.h"
#include "Logic/FxDrawList.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

#include <string>
#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogFourfoldFxRender, Log, All);

namespace
{
	constexpr int32 kNumSlots = ffx::kNumMatSlots;
	constexpr int32 kMaxLights = 4;

	FName ToFName(std::string_view S)
	{
		const std::string Str(S);
		return FName(UTF8_TO_TCHAR(Str.c_str()));
	}

	// Package path "/Game/A/B" -> object path "/Game/A/B.B" (an object path is kept as is).
	FString ObjectPath(const std::string& PackagePath)
	{
		FString P = UTF8_TO_TCHAR(PackagePath.c_str());
		if (P.IsEmpty() || P.Contains(TEXT(".")))
		{
			return P;
		}
		int32 Slash = INDEX_NONE;
		P.FindLastChar(TEXT('/'), Slash);
		return P + TEXT(".") + P.Mid(Slash + 1);
	}

	FName BoneName(ffx::Bone B)
	{
		return ToFName(ffx::kBoneNames[size_t(B)]);
	}

	struct FCompState
	{
		UPrimitiveComponent* Comp = nullptr;
		UProceduralMeshComponent* Proc = nullptr;
		UStaticMeshComponent* Static = nullptr;
		UMaterialInstanceDynamic* MID = nullptr;
		int32 Slot = 0;
		bool bStatic = false;
		bool bInUse = false;
		bool bUsed = false;
		uint32 Key = 0;
		// geometry bound to the procedural section
		const ffx::MeshData* MeshPtr = nullptr;
		uint32 MeshUid = 0, MeshVersion = 0, MeshTopo = 0;
		int32 SectionVerts = -1, SectionIdx = -1;
		bool bHasSection = false;
		UStaticMesh* BoundMesh = nullptr;
		// static meshes: one dynamic instance per parent (the slot master, or the mesh's own instance of it that
		// carries baked textures such as a rock's normal map); MID is the one in use
		UMaterialInstanceDynamic* MasterMID = nullptr;
		TArray<TPair<UMaterialInterface*, UMaterialInstanceDynamic*>> ParentMids;
		// parameter cache
		float S[ffx::kNumParams] = {};
		uint64 SMask = 0;
		FLinearColor V[ffx::kNumVParams];
		uint32 VMask = 0;
		uint8 Flipbook = 0;
		// placement
		int32 AttachActor = -1;
		uint8 AttachBone = 0;
		bool bAttached = false;
		FTransform LastTransform = FTransform::Identity;
		bool bHasTransform = false;
		bool bCastShadow = false;
		int32 Sort = 0;
		bool bVisible = false;
	};
}

namespace
{
	// A static mesh's own material when it is an instance of the slot's master (it carries baked maps), else null.
	UMaterialInterface* OwnSlotMaterial(UStaticMesh* Mesh, UMaterialInterface* SlotMat)
	{
		UMaterialInterface* Own = Mesh ? Mesh->GetMaterial(0) : nullptr;
		return (Own && SlotMat && Own != SlotMat && Own->GetBaseMaterial() == SlotMat->GetBaseMaterial()) ? Own : nullptr;
	}
}

struct FFourfoldFxRendererImpl
{
	TArray<FCompState> States;
	TMap<uint32, int32> KeyToState;
	TArray<int32> Free[2][kNumSlots];   // [static][slot]
	FName ParamNames[ffx::kNumParams];
	FName VParamNames[ffx::kNumVParams];
	FName FlipbookParam;
	FFFx::FMeshBuffers Buffers;
	uint32 LightKeys[kMaxLights] = {0, 0, 0, 0};
	float IntensityScale = 900.0f;
	float RadiusScale = 1.0f;
	float MinIntensity = 0.03f;
	int32 MissingWarned = 0;
	bool bWarnedSlot[kNumSlots] = {};
	// counters
	int32 Uploads = 0, Creates = 0, ParamSets = 0, ActiveItems = 0;

	FFourfoldFxRendererImpl()
	{
		for (int32 i = 0; i < ffx::kNumParams; ++i)
		{
			ParamNames[i] = ToFName(ffx::kParamNames[size_t(i)]);
		}
		for (int32 i = 0; i < ffx::kNumVParams; ++i)
		{
			VParamNames[i] = ToFName(ffx::kVParamNames[size_t(i)]);
		}
		FlipbookParam = ToFName(ffx::kFlipbookParamName);
	}
};

AFourfoldFxActor::AFourfoldFxActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;
	SetCanBeDamaged(false);
	Impl = MakeShared<FFourfoldFxRendererImpl>();
	Niagara = MakeShared<FFourfoldFxNiagara>();
	Debris = MakeShared<FFourfoldFxDebris>();
}

void AFourfoldFxActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseAll();
	Super::EndPlay(EndPlayReason);
}

namespace
{
	// Every effect primitive (pooled or pre-warm) is created the same way: the flags take part in its render state, so
	// a pre-warm draw only builds the pipelines the real items use when they match.
	template <typename T>
	T* NewFxPrimitive(AFourfoldFxActor* Owner, USceneComponent* Root)
	{
		T* P = NewObject<T>(Owner, NAME_None, RF_Transient);
		if constexpr (std::is_same_v<T, UProceduralMeshComponent>)
		{
			P->bUseAsyncCooking = false;
		}
		P->SetMobility(EComponentMobility::Movable);
		P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		P->SetGenerateOverlapEvents(false);
		P->SetCanEverAffectNavigation(false);
		P->CanCharacterStepUpOn = ECB_No;
		P->SetCastShadow(false);
		P->bAffectDistanceFieldLighting = false;
		P->bReceivesDecals = false;
		P->SetBoundsScale(2.0f);   // vertex-shader motion (flames, funnels) leaves the CPU bounds
		P->SetupAttachment(Root);
		P->RegisterComponent();
		P->SetVisibility(false);
		return P;
	}

	int32 CreateComponent(AFourfoldFxActor* Owner, USceneComponent* Root, FFourfoldFxRendererImpl& I, bool bStatic, int32 Slot,
		UMaterialInterface* Material, TArray<TObjectPtr<UPrimitiveComponent>>& Pooled,
		TArray<TObjectPtr<UMaterialInstanceDynamic>>& PooledMids)
	{
		FCompState St;
		St.bStatic = bStatic;
		St.Slot = Slot;
		if (bStatic)
		{
			St.Static = NewFxPrimitive<UStaticMeshComponent>(Owner, Root);
			St.Comp = St.Static;
		}
		else
		{
			St.Proc = NewFxPrimitive<UProceduralMeshComponent>(Owner, Root);
			St.Comp = St.Proc;
		}
		UPrimitiveComponent* P = St.Comp;
		if (Material)
		{
			St.MID = UMaterialInstanceDynamic::Create(Material, Owner);
			St.MasterMID = St.MID;
			PooledMids.Add(St.MID);
		}
		Pooled.Add(P);
		return I.States.Add(St);
	}
}

void AFourfoldFxActor::Setup(const ffx::FxConfig& Config, int32 Quality)
{
	FFourfoldFxRendererImpl& I = *Impl;
	I.IntensityScale = Config.lights.intensityScale;
	I.RadiusScale = Config.lights.radiusScale;
	I.MinIntensity = Config.lights.minIntensity;
	SlotMaterials.SetNum(kNumSlots);
	for (int32 i = 0; i < kNumSlots; ++i)
	{
		const FString Path = ObjectPath(Config.materials[size_t(i)]);
		SlotMaterials[i] = Path.IsEmpty() ? nullptr : LoadObject<UMaterialInterface>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!SlotMaterials[i])
		{
			UE_LOG(LogFourfoldFxRender, Warning, TEXT("FX material '%s' (slot %s) not found: run the Fourfold setup (fx). Items using it are hidden."),
				*Path, UTF8_TO_TCHAR(std::string(ffx::kMatSlotNames[size_t(i)]).c_str()));
		}
	}
	AssetMeshes.SetNum(ffx::kNumMeshAssets);
	for (int32 i = 1; i < ffx::kNumMeshAssets; ++i)
	{
		const FString Path = ObjectPath(Config.meshes[size_t(i)]);
		AssetMeshes[i] = Path.IsEmpty() ? nullptr : LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	FlipbookTextures.SetNum(ffx::kNumFlipbooks);
	for (int32 i = 1; i < ffx::kNumFlipbooks; ++i)
	{
		const FString Path = ObjectPath(Config.flipbooks[size_t(i)]);
		FlipbookTextures[i] = Path.IsEmpty() ? nullptr : LoadObject<UTexture>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	// Pre-warm: a few hidden components per material (their PSOs are precached at creation, so the first effect of a
	// kind does not hitch mid-fight), plus the four shadowless lights.
	const int32 PerSlot = Quality >= 2 ? 4 : 3;
	for (int32 Slot = 0; Slot < kNumSlots; ++Slot)
	{
		if (!SlotMaterials[Slot] || I.Free[0][Slot].Num() >= PerSlot)
		{
			continue;
		}
		for (int32 k = I.Free[0][Slot].Num(); k < PerSlot; ++k)
		{
			I.Free[0][Slot].Add(CreateComponent(this, Root, I, false, Slot, SlotMaterials[Slot], PooledComponents, PooledMaterials));
		}
	}
	for (int32 Slot : {int32(ffx::MatSlot::Rock), int32(ffx::MatSlot::Metal), int32(ffx::MatSlot::Crystal)})
	{
		if (SlotMaterials[Slot] && I.Free[1][Slot].Num() == 0)
		{
			for (int32 k = 0; k < 4; ++k)
			{
				I.Free[1][Slot].Add(CreateComponent(this, Root, I, true, Slot, SlotMaterials[Slot], PooledComponents, PooledMaterials));
			}
		}
	}
	if (Lights.Num() == 0)
	{
		for (int32 i = 0; i < kMaxLights; ++i)
		{
			UPointLightComponent* L = NewObject<UPointLightComponent>(this, NAME_None, RF_Transient);
			L->SetMobility(EComponentMobility::Movable);
			L->SetCastShadows(false);
			L->SetIntensityUnits(ELightUnits::Candelas);
			L->SetSourceRadius(5.0f);
			L->SetAffectTranslucentLighting(false);
			L->SetVolumetricScatteringIntensity(0.0f);
			L->SetupAttachment(Root);
			L->RegisterComponent();
			L->SetVisibility(false);
			Lights.Add(L);
		}
	}
	NiagaraSystems.Reset();
	Niagara->Setup(Config, Quality, NiagaraSystems);
	Debris->Setup(Config, Quality);
}

void AFourfoldFxActor::ReleaseAll()
{
	if (!Impl)
	{
		return;
	}
	FFourfoldFxRendererImpl& I = *Impl;
	for (int32 i = 0; i < I.States.Num(); ++i)
	{
		FCompState& St = I.States[i];
		if (!St.bInUse)
		{
			continue;
		}
		St.bInUse = false;
		if (St.Comp)
		{
			St.Comp->SetVisibility(false);
			if (St.bAttached)
			{
				St.Comp->AttachToComponent(Root, FAttachmentTransformRules::KeepWorldTransform);
				St.Comp->SetUsingAbsoluteRotation(false);
				St.Comp->SetUsingAbsoluteScale(false);
				St.bAttached = false;
			}
		}
		St.bVisible = false;
		I.Free[St.bStatic ? 1 : 0][St.Slot].Add(i);
	}
	I.KeyToState.Reset();
	for (UPointLightComponent* L : Lights)
	{
		if (L)
		{
			L->SetVisibility(false);
		}
	}
	for (uint32& K : I.LightKeys)
	{
		K = 0;
	}
	if (Niagara)
	{
		Niagara->StopAll();
	}
	if (Debris)
	{
		Debris->ReleaseAll();
	}
	EndMaterialPrewarm();
}

void AFourfoldFxActor::BuildDebrisArena(const ff::ArenaView& Arena)
{
	Debris->BuildArena(this, Root, Arena, DebrisArena);
}

bool AFourfoldFxActor::IsDebrisReady() const
{
	return Debris && Debris->IsReady();
}

const std::vector<ffx::DebrisImpact>* AFourfoldFxActor::GetDebrisImpacts() const
{
	return Debris && !Debris->Impacts().empty() ? &Debris->Impacts() : nullptr;
}

void AFourfoldFxActor::ClearDebrisImpacts()
{
	if (Debris)
	{
		Debris->ClearImpacts();
	}
}

void AFourfoldFxActor::OnDebrisHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	if (Debris)
	{
		Debris->OnHit(HitComponent, FVector(Hit.ImpactPoint), NormalImpulse);
	}
}

void AFourfoldFxActor::PrewarmMaterials(const FVector& Location)
{
	bMaterialsPrewarmed = true;
	EndMaterialPrewarm();
	// one small triangle with the vertex streams of every procedural section (the vertex layout is part of the PSO)
	ffx::MeshData Tri;
	const ffx::Vec3 N(0.0f, 0.0f, 1.0f);
	Tri.Add(ffx::Vec3(-0.15f, 0.0f, 0.0f), N, ffx::Vec2(0.0f, 0.0f), ffx::Color(1.0f, 1.0f, 1.0f, 1.0f));
	Tri.Add(ffx::Vec3(0.15f, 0.0f, 0.0f), N, ffx::Vec2(1.0f, 0.0f), ffx::Color(1.0f, 1.0f, 1.0f, 1.0f));
	Tri.Add(ffx::Vec3(0.0f, 0.3f, 0.0f), N, ffx::Vec2(0.5f, 1.0f), ffx::Color(1.0f, 1.0f, 1.0f, 1.0f));
	Tri.Tri(0, 1, 2);
	FFFx::FMeshBuffers B;
	B.Convert(Tri, true);
	// opaque items that cast shadows: rocks and walls (Rock slot); their shadow-depth pipelines get built too
	auto CastsShadow = [](int32 Slot) { return Slot == int32(ffx::MatSlot::Rock); };
	int32 k = 0;
	auto Place = [&](UPrimitiveComponent* C, bool bShadow)
	{
		C->SetWorldLocation(Location + FVector(double(k % 6) * 40.0, double(k / 6) * 40.0, 0.0));   // spread: no overdraw
		C->SetCastShadow(bShadow);
		C->SetVisibility(true);
		PrewarmComponents.Add(C);
		++k;
	};
	for (int32 Slot = 0; Slot < kNumSlots; ++Slot)
	{
		if (UMaterialInterface* Mat = SlotMaterials[Slot].Get())
		{
			UProceduralMeshComponent* C = NewFxPrimitive<UProceduralMeshComponent>(this, Root);
			C->CreateMeshSection_LinearColor(0, B.Vertices, B.Triangles, B.Normals, B.UV0, B.UV1, B.UV2, B.UV3, B.Colors,
				B.Tangents, false, false);
			C->SetMaterial(0, Mat);   // the pooled dynamic instances share their parent's shaders
			Place(C, CastsShadow(Slot));
		}
	}
	// the static meshes the views draw, with the material Apply() binds to them
	struct FStaticUse
	{
		ffx::MeshAsset Asset;
		ffx::MatSlot Slot;
	};
	using ffx::MeshAsset;
	using ffx::MatSlot;
	const FStaticUse Statics[] = {{MeshAsset::Rock0, MatSlot::Rock}, {MeshAsset::Rock1, MatSlot::Rock},
		{MeshAsset::Rock2, MatSlot::Rock}, {MeshAsset::Rock3, MatSlot::Rock}, {MeshAsset::Rock4, MatSlot::Rock},
		{MeshAsset::Rock5, MatSlot::Rock}, {MeshAsset::Rock6, MatSlot::Rock}, {MeshAsset::Rock7, MatSlot::Rock},
		{MeshAsset::Disc, MatSlot::Metal}, {MeshAsset::Lance, MatSlot::Metal}, {MeshAsset::Plate, MatSlot::Metal},
		{MeshAsset::IceShard, MatSlot::Crystal}};
	for (const FStaticUse& S : Statics)
	{
		UStaticMesh* Mesh = AssetMeshes.IsValidIndex(int32(S.Asset)) ? AssetMeshes[int32(S.Asset)].Get() : nullptr;
		UMaterialInterface* Mat = SlotMaterials.IsValidIndex(int32(S.Slot)) ? SlotMaterials[int32(S.Slot)].Get() : nullptr;
		if (!Mesh || !Mat)
		{
			continue;
		}
		UStaticMeshComponent* C = NewFxPrimitive<UStaticMeshComponent>(this, Root);
		C->SetStaticMesh(Mesh);
		UMaterialInterface* Own = OwnSlotMaterial(Mesh, Mat);
		for (int32 m = 0; m < C->GetNumMaterials(); ++m)
		{
			C->SetMaterial(m, Own ? Own : Mat);
		}
		C->SetWorldScale3D(FVector(0.3));
		Place(C, CastsShadow(int32(S.Slot)));
	}
	PrewarmFrames = 4;   // a few frames: the base pass draws on the first (occlusion has no history yet), shadows on each
	UE_LOG(LogFourfoldFxRender, Log, TEXT("Material pre-warm: %d components at %s"), PrewarmComponents.Num(), *Location.ToCompactString());
}

void AFourfoldFxActor::EndMaterialPrewarm()
{
	for (UPrimitiveComponent* C : PrewarmComponents)
	{
		if (C)
		{
			C->DestroyComponent();
		}
	}
	PrewarmComponents.Reset();
	PrewarmFrames = 0;
}

void AFourfoldFxActor::Apply(const ffx::DrawList& List, UFourfoldSimSubsystem* Sim, bool bNiagara)
{
	FFourfoldFxRendererImpl& I = *Impl;
	I.Uploads = 0;
	I.ParamSets = 0;
	I.ActiveItems = 0;
	for (FCompState& St : I.States)
	{
		St.bUsed = false;
	}
	for (const ffx::DrawItem& Item : List.items)
	{
		const int32 Slot = int32(Item.mat);
		UMaterialInterface* Mat = SlotMaterials.IsValidIndex(Slot) ? SlotMaterials[Slot].Get() : nullptr;
		if (!Mat)
		{
			continue;   // material not built yet: hidden (warned once in Setup)
		}
		UStaticMesh* Asset = (Item.kind == ffx::DrawKind::StaticMesh && AssetMeshes.IsValidIndex(int32(Item.asset)))
			? AssetMeshes[int32(Item.asset)].Get() : nullptr;
		const bool bStatic = Asset != nullptr;
		if (!bStatic && !Item.mesh)
		{
			continue;
		}
		// ---- bind a component to the key
		int32* Found = I.KeyToState.Find(Item.key);
		int32 Idx = Found ? *Found : INDEX_NONE;
		if (Idx != INDEX_NONE && (I.States[Idx].bStatic != bStatic || I.States[Idx].Slot != Slot))
		{
			Idx = INDEX_NONE;   // the item changed representation: rebind
		}
		if (Idx == INDEX_NONE)
		{
			TArray<int32>& FreeList = I.Free[bStatic ? 1 : 0][Slot];
			if (FreeList.Num() > 0)
			{
				Idx = FreeList.Pop(EAllowShrinking::No);
			}
			else
			{
				Idx = CreateComponent(this, Root, I, bStatic, Slot, Mat, PooledComponents, PooledMaterials);
				++I.Creates;   // a pool grew past its pre-warmed size
			}
			if (Found)
			{
				I.KeyToState.Remove(Item.key);
			}
			FCompState& New = I.States[Idx];
			New.bInUse = true;
			New.Key = Item.key;
			New.bHasTransform = false;
			if (New.MID && (New.SMask != 0 || New.VMask != 0 || New.Flipbook != 0))
			{
				// a recycled component: parameters the new item does not set must fall back to the material defaults
				New.MID->ClearParameterValues();
			}
			New.SMask = 0;
			New.VMask = 0;
			New.Flipbook = 0;
			if (New.Proc && New.MID)
			{
				New.Proc->SetMaterial(0, New.MID);
			}
			I.KeyToState.Add(Item.key, Idx);
		}
		FCompState& St = I.States[Idx];
		St.bUsed = true;
		++I.ActiveItems;
		// ---- geometry
		if (bStatic)
		{
			if (St.BoundMesh != Asset)
			{
				St.Static->SetStaticMesh(Asset);
				St.BoundMesh = Asset;
				// the mesh's own slot material wins when it is an instance of this slot's master (baked maps)
				UMaterialInterface* Parent = OwnSlotMaterial(Asset, Mat);
				UMaterialInstanceDynamic* Want = St.MasterMID;
				if (Parent)
				{
					Want = nullptr;
					for (const TPair<UMaterialInterface*, UMaterialInstanceDynamic*>& P : St.ParentMids)
					{
						if (P.Key == Parent)
						{
							Want = P.Value;
						}
					}
					if (!Want)
					{
						Want = UMaterialInstanceDynamic::Create(Parent, this);
						PooledMaterials.Add(Want);
						St.ParentMids.Add(TPair<UMaterialInterface*, UMaterialInstanceDynamic*>(Parent, Want));
					}
				}
				if (Want && Want != St.MID)
				{
					// switching instance: reset it to its parent's defaults and forget the parameter cache (the item's
					// values are pushed below)
					Want->ClearParameterValues();
					St.MID = Want;
					St.SMask = 0;
					St.VMask = 0;
					St.Flipbook = 0;
				}
				if (St.MID)
				{
					for (int32 m = 0; m < St.Static->GetNumMaterials(); ++m)
					{
						St.Static->SetMaterial(m, St.MID);
					}
				}
			}
		}
		else if (St.Proc)
		{
			const ffx::MeshData& M = *Item.mesh;
			const bool bChanged = St.MeshPtr != &M || St.MeshUid != M.uid || St.MeshVersion != M.version;
			if (bChanged)
			{
				const bool bRecreate = !St.bHasSection || St.MeshPtr != &M || St.MeshUid != M.uid || St.MeshTopo != M.topo ||
					St.SectionVerts != M.NumVerts() || St.SectionIdx != int32(M.idx.size());
				if (M.Empty())
				{
					if (St.bHasSection)
					{
						St.Proc->ClearMeshSection(0);
						St.bHasSection = false;
					}
				}
				else
				{
					I.Buffers.Convert(M, bRecreate);
					if (bRecreate)
					{
						St.Proc->CreateMeshSection_LinearColor(0, I.Buffers.Vertices, I.Buffers.Triangles, I.Buffers.Normals, I.Buffers.UV0,
							I.Buffers.UV1, I.Buffers.UV2, I.Buffers.UV3, I.Buffers.Colors, I.Buffers.Tangents, false, false);
						if (St.MID)
						{
							St.Proc->SetMaterial(0, St.MID);
						}
					}
					else
					{
						St.Proc->UpdateMeshSection_LinearColor(0, I.Buffers.Vertices, I.Buffers.Normals, I.Buffers.UV0, I.Buffers.UV1,
							I.Buffers.UV2, I.Buffers.UV3, I.Buffers.Colors, I.Buffers.Tangents, false);
					}
					St.bHasSection = true;
					++I.Uploads;
				}
				St.MeshPtr = &M;
				St.MeshUid = M.uid;
				St.MeshVersion = M.version;
				St.MeshTopo = M.topo;
				St.SectionVerts = M.NumVerts();
				St.SectionIdx = int32(M.idx.size());
			}
		}
		// ---- placement: attached to a fighter bone, or a world transform
		bool bAttachNow = false;
		if (Item.attachActor >= 0 && Sim)
		{
			if (AFourfoldFighter* F = Sim->FindFighter(Item.attachActor))
			{
				USkeletalMeshComponent* Body = F->GetBodyMesh();
				const FName Bone = BoneName(Item.attachBone);
				if (Body && !F->UsesFallbackBody() && Body->DoesSocketExist(Bone))
				{
					bAttachNow = true;
					if (!St.bAttached || St.AttachActor != Item.attachActor || St.AttachBone != uint8(Item.attachBone))
					{
						St.Comp->AttachToComponent(Body, FAttachmentTransformRules(EAttachmentRule::SnapToTarget,
							EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, false), Bone);
						St.Comp->SetUsingAbsoluteRotation(true);
						St.Comp->SetUsingAbsoluteScale(true);
						St.Comp->SetRelativeLocation(FVector::ZeroVector);
						St.bAttached = true;
						St.AttachActor = Item.attachActor;
						St.AttachBone = uint8(Item.attachBone);
					}
					FQuat Q;
					FVector Scale;
					FFFx::ToRotationScale(Item.xform.basis, Q, Scale);
					St.Comp->SetWorldRotation(Q);
					St.Comp->SetWorldScale3D(Scale);
				}
			}
		}
		if (!bAttachNow)
		{
			if (St.bAttached)
			{
				St.Comp->AttachToComponent(Root, FAttachmentTransformRules::KeepWorldTransform);
				St.Comp->SetUsingAbsoluteRotation(false);
				St.Comp->SetUsingAbsoluteScale(false);
				St.bAttached = false;
				St.bHasTransform = false;
			}
			const FTransform T = FFFx::ToTransform(Item.xform);
			if (!St.bHasTransform || !T.Equals(St.LastTransform, 0.01))
			{
				St.Comp->SetWorldTransform(T);
				St.LastTransform = T;
				St.bHasTransform = true;
			}
		}
		// ---- render flags
		if (St.bCastShadow != Item.castShadow)
		{
			St.Comp->SetCastShadow(Item.castShadow);
			St.bCastShadow = Item.castShadow;
		}
		if (St.Sort != Item.sortPriority)
		{
			St.Comp->SetTranslucentSortPriority(Item.sortPriority);
			St.Sort = Item.sortPriority;
		}
		// ---- parameters (only the changed ones)
		if (St.MID)
		{
			const ffx::ParamBlock& PB = Item.params;
			for (int32 p = 0; p < ffx::kNumParams; ++p)
			{
				if (!((PB.sMask >> p) & 1ULL))
				{
					continue;
				}
				const float Val = PB.s[size_t(p)];
				if (!((St.SMask >> p) & 1ULL) || St.S[p] != Val)
				{
					St.MID->SetScalarParameterValue(I.ParamNames[p], Val);
					St.S[p] = Val;
					St.SMask |= (1ULL << p);
					++I.ParamSets;
				}
			}
			for (int32 p = 0; p < ffx::kNumVParams; ++p)
			{
				if (!((PB.vMask >> p) & 1u))
				{
					continue;
				}
				const FLinearColor Val = FFFx::ToLinear(PB.v[size_t(p)]);
				if (!((St.VMask >> p) & 1u) || !St.V[p].Equals(Val, 1e-4f))
				{
					St.MID->SetVectorParameterValue(I.VParamNames[p], Val);
					St.V[p] = Val;
					St.VMask |= (1u << p);
					++I.ParamSets;
				}
			}
			if (PB.flipbook != ffx::Flipbook::None && St.Flipbook != uint8(PB.flipbook))
			{
				UTexture* Tex = FlipbookTextures.IsValidIndex(int32(PB.flipbook)) ? FlipbookTextures[int32(PB.flipbook)].Get() : nullptr;
				if (Tex)
				{
					St.MID->SetTextureParameterValue(I.FlipbookParam, Tex);
				}
				St.Flipbook = uint8(PB.flipbook);
			}
		}
		if (!St.bVisible)
		{
			St.Comp->SetVisibility(true);
			St.bVisible = true;
		}
	}
	// ---- release components whose key did not appear this frame
	for (int32 i = 0; i < I.States.Num(); ++i)
	{
		FCompState& St = I.States[i];
		if (!St.bInUse || St.bUsed)
		{
			continue;
		}
		St.bInUse = false;
		I.KeyToState.Remove(St.Key);
		if (St.bVisible)
		{
			St.Comp->SetVisibility(false);
			St.bVisible = false;
		}
		if (St.bAttached)
		{
			St.Comp->AttachToComponent(Root, FAttachmentTransformRules::KeepWorldTransform);
			St.Comp->SetUsingAbsoluteRotation(false);
			St.Comp->SetUsingAbsoluteScale(false);
			St.bAttached = false;
		}
		I.Free[St.bStatic ? 1 : 0][St.Slot].Add(i);
	}
	// ---- lights: keep each request on the component it had last frame (no popping), <= 4, shadowless
	bool bTaken[kMaxLights] = {false, false, false, false};
	int32 Assign[kMaxLights] = {INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE};
	const int32 NumReq = FMath::Min(int32(List.lights.size()), kMaxLights);
	for (int32 r = 0; r < NumReq; ++r)
	{
		for (int32 l = 0; l < kMaxLights; ++l)
		{
			if (!bTaken[l] && I.LightKeys[l] == List.lights[size_t(r)].key)
			{
				bTaken[l] = true;
				Assign[r] = l;
				break;
			}
		}
	}
	for (int32 r = 0; r < NumReq; ++r)
	{
		if (Assign[r] != INDEX_NONE)
		{
			continue;
		}
		for (int32 l = 0; l < kMaxLights; ++l)
		{
			if (!bTaken[l])
			{
				bTaken[l] = true;
				Assign[r] = l;
				break;
			}
		}
	}
	for (int32 l = 0; l < kMaxLights && l < Lights.Num(); ++l)
	{
		UPointLightComponent* L = Lights[l];
		if (!L)
		{
			continue;
		}
		int32 Req = INDEX_NONE;
		for (int32 r = 0; r < NumReq; ++r)
		{
			if (Assign[r] == l)
			{
				Req = r;
			}
		}
		if (Req == INDEX_NONE || List.lights[size_t(Req)].intensity < I.MinIntensity)
		{
			if (L->IsVisible())
			{
				L->SetVisibility(false);
			}
			I.LightKeys[l] = 0;
			continue;
		}
		const ffx::LightReq& LR = List.lights[size_t(Req)];
		I.LightKeys[l] = LR.key;
		L->SetWorldLocation(FF::ToUE(LR.pos));
		L->SetLightColor(FFFx::ToLinear(LR.color), false);   // logic light colours are display (sRGB) values
		L->SetIntensity(LR.intensity * I.IntensityScale);
		L->SetAttenuationRadius(float(LR.radius * FF::SimToUE) * I.RadiusScale);
		if (!L->IsVisible())
		{
			L->SetVisibility(true);
		}
	}
	// Niagara cue systems (fire and forget; timed stops run even while new spawns are off) and persistent ones
	Niagara->Spawn(this, List, bNiagara);
	Niagara->UpdateLoops(this, List, bNiagara);
	// physics debris of broken stones / walls, and the raised walls they bounce off
	Debris->UpdateColliders(this, Root, List, DebrisObjects);
	Debris->Update(this, List, SlotMaterials, GetWorld() ? GetWorld()->GetDeltaSeconds() : 1.0f / 60.0f, DebrisObjects);
	if (PrewarmFrames > 0 && --PrewarmFrames == 0)
	{
		EndMaterialPrewarm();
	}
}

FString AFourfoldFxActor::GetDebugLine() const
{
	if (!Impl)
	{
		return FString();
	}
	const FFourfoldFxRendererImpl& I = *Impl;
	int32 Lit = 0;
	for (const TObjectPtr<UPointLightComponent>& L : Lights)
	{
		Lit += (L && L->IsVisible()) ? 1 : 0;
	}
	return FString::Printf(TEXT("fx: %d items, %d components (%d grown), %d uploads, %d params, %d lights | %s | %s"), I.ActiveItems,
		I.States.Num(), I.Creates, I.Uploads, I.ParamSets, Lit, *Niagara->GetDebugLine(), *Debris->GetDebugLine());
}

uint64 AFourfoldFxActor::GetNiagaraLoadedMask() const
{
	return Niagara ? Niagara->LoadedMask() : 0;
}

uint64 AFourfoldFxActor::GetNiagaraLoopsLoadedMask() const
{
	return Niagara ? Niagara->LoopsLoadedMask() : 0;
}

bool AFourfoldFxActor::NeedsNiagaraPrewarm() const
{
	return Niagara && Niagara->NeedsPrewarm();
}

int32 AFourfoldFxActor::PrewarmNiagara(const FVector& Location)
{
	return Niagara ? Niagara->Prewarm(this, Location) : 0;
}
