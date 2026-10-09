// FourfoldFX - physics debris (see FourfoldFxDebris.h). Owner: stream `fx`.
#include "FourfoldFxDebris.h"

#include "Components/BoxComponent.h"
#include "FourfoldCoords.h"
#include "FourfoldFxActor.h"
#include "FxUeConvert.h"
#include "GameFramework/Actor.h"
#include "Logic/FxConfig.h"
#include "Logic/FxContext.h"
#include "Logic/FxDrawList.h"
#include "Logic/FxFracture.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "ProceduralMeshComponent.h"
#include "ff/Snapshot.h"

#include <string>

namespace
{
	constexpr int32 kHardCap = 64;        // components ever created (each keeps one cooked piece shape)
	constexpr double kKillZ = -1500.0;    // cm: fell off the arena
	constexpr float kHitCooldown = 0.3f;  // s between two impacts of one piece
	constexpr float kHitMinSpeed = 1.2f;  // m/s: slower contacts (rolling, resting) make no dust
	constexpr int32 kMaxImpacts = 8;      // per frame

	struct FParamNames
	{
		FName S[ffx::kNumParams];
		FName V[ffx::kNumVParams];
		FParamNames()
		{
			for (int32 i = 0; i < ffx::kNumParams; ++i)
			{
				S[i] = FName(UTF8_TO_TCHAR(std::string(ffx::kParamNames[size_t(i)]).c_str()));
			}
			for (int32 i = 0; i < ffx::kNumVParams; ++i)
			{
				V[i] = FName(UTF8_TO_TCHAR(std::string(ffx::kVParamNames[size_t(i)]).c_str()));
			}
		}
	};

	void PushParams(UMaterialInstanceDynamic& MID, const ffx::ParamBlock& PB)
	{
		static const FParamNames Names;
		MID.ClearParameterValues();   // a reused piece must not keep the last body's look
		for (int32 p = 0; p < ffx::kNumParams; ++p)
		{
			if ((PB.sMask >> p) & 1ULL)
			{
				MID.SetScalarParameterValue(Names.S[p], PB.s[size_t(p)]);
			}
		}
		for (int32 p = 0; p < ffx::kNumVParams; ++p)
		{
			if ((PB.vMask >> p) & 1u)
			{
				MID.SetVectorParameterValue(Names.V[p], FFFx::ToLinear(PB.v[size_t(p)]));
			}
		}
	}

	// Debris and the arena copy only see each other: Destructible blocks Destructible, everything else is ignored.
	void DebrisCollision(UPrimitiveComponent& C)
	{
		C.SetCollisionObjectType(ECC_Destructible);
		C.SetCollisionResponseToAllChannels(ECR_Ignore);
		C.SetCollisionResponseToChannel(ECC_Destructible, ECR_Block);
		C.SetGenerateOverlapEvents(false);
		C.SetCanEverAffectNavigation(false);
		C.CanCharacterStepUpOn = ECB_No;
	}
}

void FFourfoldFxDebris::Setup(const ffx::FxConfig& Config, int32 Quality)
{
	const ffx::FractureSettings& F = Config.fracture;
	MaxPieces = FMath::Clamp(Config.Q(Quality).piecesMax, 0, kHardCap);
	SinkTime = FMath::Max(0.05f, F.sinkTime);
	Spin = F.spin;
	LinearDamping = F.linearDamping;
	AngularDamping = F.angularDamping;
	MaxDepenetration = F.maxDepenetration * float(FF::SimToUE);
	Friction = F.friction;
	Restitution = F.restitution;
	Density = F.density;
}

void FFourfoldFxDebris::BuildArena(AActor* Owner, USceneComponent* Root, const ff::ArenaView& A, TArray<TObjectPtr<UObject>>& OutRefs)
{
	for (UBoxComponent* B : ArenaBoxes)
	{
		if (B)
		{
			B->DestroyComponent();
		}
	}
	ArenaBoxes.Reset();
	OutRefs.Reset();
	bArena = false;
	if (!Owner)
	{
		return;
	}
	// a new material per build picks up fx_config.json changes (pieces take it on their next spawn)
	PhysMat = NewObject<UPhysicalMaterial>(Owner, NAME_None, RF_Transient);
	PhysMat->Friction = Friction;
	PhysMat->StaticFriction = Friction;
	PhysMat->Restitution = Restitution;
	PhysMat->Density = Density;
	OutRefs.Add(PhysMat);
	auto AddBox = [&](const ffx::Vec3& Lo, const ffx::Vec3& Hi)   // sim-space AABB
	{
		if (Hi.x - Lo.x < 0.01f || Hi.y - Lo.y < 0.01f || Hi.z - Lo.z < 0.01f)
		{
			return;
		}
		UBoxComponent* B = NewObject<UBoxComponent>(Owner, NAME_None, RF_Transient);
		B->SetMobility(EComponentMobility::Movable);
		DebrisCollision(*B);
		B->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
		B->SetPhysMaterialOverride(PhysMat);
		B->SetHiddenInGame(true);
		B->SetBoxExtent(FF::HalfExtentsToUE((Hi - Lo) * 0.5f), false);
		B->SetupAttachment(Root);
		B->RegisterComponent();
		B->SetWorldLocation(FF::ToUE((Lo + Hi) * 0.5f));
		ArenaBoxes.Add(B);
		OutRefs.Add(B);
	};
	const float H = A.half_size + 2.0f;
	const float Depth = 2.0f;
	const bool bPool = A.pool_max.x > A.pool_min.x && A.pool_max.y > A.pool_min.y;   // Vec2 (x, z)
	if (A.world)
	{
		// open world: the terrain chunks near the camera carry collision (AFourfoldOpenWorld)
	}
	else if (!bPool)
	{
		AddBox(ffx::Vec3(-H, -Depth, -H), ffx::Vec3(H, 0.0f, H));
	}
	else
	{
		// ground around the pool, and its basin
		AddBox(ffx::Vec3(-H, -Depth, -H), ffx::Vec3(A.pool_min.x, 0.0f, H));
		AddBox(ffx::Vec3(A.pool_max.x, -Depth, -H), ffx::Vec3(H, 0.0f, H));
		AddBox(ffx::Vec3(A.pool_min.x, -Depth, -H), ffx::Vec3(A.pool_max.x, 0.0f, A.pool_min.y));
		AddBox(ffx::Vec3(A.pool_min.x, -Depth, A.pool_max.y), ffx::Vec3(A.pool_max.x, 0.0f, H));
		AddBox(ffx::Vec3(A.pool_min.x, A.pool_floor - Depth, A.pool_min.y), ffx::Vec3(A.pool_max.x, A.pool_floor, A.pool_max.y));
	}
	if (A.metal_max.x > A.metal_min.x && A.metal_max.y > A.metal_min.y)
	{
		AddBox(ffx::Vec3(A.metal_min.x, -0.5f, A.metal_min.y), ffx::Vec3(A.metal_max.x, A.metal_top, A.metal_max.y));
	}
	for (const ff::ArenaBox& S : A.solids)
	{
		AddBox(S.min, S.max);
	}
	bArena = true;
}

int32 FFourfoldFxDebris::Acquire(AFourfoldFxActor* Owner, const ffx::FracturePiece* Shape, TArray<TObjectPtr<UObject>>& OutRefs)
{
	int32 AnyIdle = INDEX_NONE;
	for (int32 i = 0; i < Pieces.Num(); ++i)
	{
		if (Pieces[i].bActive)
		{
			continue;
		}
		if (Pieces[i].Shape == Shape)
		{
			return i;   // already cooked for this shape
		}
		AnyIdle = AnyIdle == INDEX_NONE ? i : AnyIdle;
	}
	if (Pieces.Num() >= kHardCap)
	{
		return AnyIdle;   // rebuilds its geometry + collision
	}
	UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(Owner, NAME_None, RF_Transient);
	C->bUseAsyncCooking = false;
	C->bUseComplexAsSimpleCollision = false;   // the convex hull below is the simple collision that simulates
	C->SetMobility(EComponentMobility::Movable);
	DebrisCollision(*C);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(true);
	C->bAffectDistanceFieldLighting = false;
	C->bReceivesDecals = false;
	C->SetLinearDamping(LinearDamping);
	C->SetAngularDamping(AngularDamping);
	C->BodyInstance.SetMaxDepenetrationVelocity(MaxDepenetration);   // neighbours that start overlapping drift apart
	C->SetNotifyRigidBodyCollision(true);   // hard landings kick up dust (OnHit)
	C->OnComponentHit.AddDynamic(Owner, &AFourfoldFxActor::OnDebrisHit);
	C->RegisterComponent();   // not attached: a simulating body owns its transform
	C->SetVisibility(false);
	OutRefs.Add(C);
	FPiece& P = Pieces.AddDefaulted_GetRef();
	P.Comp = C;
	return Pieces.Num() - 1;
}

void FFourfoldFxDebris::Spawn(FPiece& P, const ffx::FractureReq& R, const ffx::FracturePiece& Shape, UMaterialInterface* Mat,
	AActor* Owner, TArray<TObjectPtr<UObject>>& OutRefs, uint32 Seed)
{
	UProceduralMeshComponent* C = P.Comp;
	if (P.Shape != &Shape)
	{
		FFFx::FMeshBuffers B;
		B.Convert(Shape.mesh, true);
		C->CreateMeshSection_LinearColor(0, B.Vertices, B.Triangles, B.Normals, B.UV0, B.UV1, B.UV2, B.UV3, B.Colors, B.Tangents,
			false, false);
		TArray<FVector> Hull;
		Hull.Reserve(int32(Shape.hull.size()));
		for (const ffx::Vec3& V : Shape.hull)
		{
			Hull.Add(FFFx::Swz(V) * FF::SimToUE);
		}
		C->SetCollisionConvexMeshes({Hull});   // cooks the body setup (once per shape on this component)
		P.Shape = &Shape;
		P.MIDParent = nullptr;   // the section was recreated: bind the material again
	}
	if (P.MIDParent != Mat || !P.MID)
	{
		P.MID = UMaterialInstanceDynamic::Create(Mat, Owner);
		P.MIDParent = Mat;
		OutRefs.Add(P.MID);
	}
	C->SetMaterial(0, P.MID);
	PushParams(*P.MID, R.params);
	if (P.PhysMatSet != PhysMat)
	{
		C->SetPhysMaterialOverride(PhysMat);
		P.PhysMatSet = PhysMat;
	}
	// placement: the intact body's transform with the piece shrunk about its own centre
	ffx::Xform X;
	const ffx::Vec3 Centre = R.xform.pos + R.xform.basis.Apply(Shape.centre);
	X.pos = R.xform.pos + R.xform.basis.Apply(Shape.centre) * (1.0f - R.scale);
	X.basis = R.xform.basis.Scaled(R.scale);
	C->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
	C->SetWorldTransform(FFFx::ToTransform(X), false, nullptr, ETeleportType::TeleportPhysics);
	C->SetVisibility(true);
	C->SetSimulatePhysics(true);
	// motion: inherited + outward burst + a lift, random spin
	ffx::Rng Rand(Seed);
	const ffx::Vec3 Out = ffx::Norm(Centre - R.origin, ffx::Vec3(0.0f, 1.0f, 0.0f));
	const ffx::Vec3 V = R.vel + Out * (R.burst * Rand.Range(0.6f, 1.2f)) + ffx::Vec3(0.0f, R.burst * Rand.Range(0.15f, 0.45f), 0.0f);
	C->SetPhysicsLinearVelocity(FF::DirToUE(V) * FF::SimToUE);
	C->SetPhysicsAngularVelocityInRadians(FF::DirToUE(Rand.OnSphere()) * double(Spin * Rand.Range(0.3f, 1.0f)));
	P.Age = 0.0f;
	P.Life = R.life * Rand.Range(0.85f, 1.15f);
	P.SinkDepth = float(C->Bounds.SphereRadius * 2.0 + 10.0);
	P.HitCooldown = 0.1f;   // not on the first contact frames (pieces start touching)
	P.bActive = true;
	P.bSinking = false;
	++Active;
	++Spawned;
}

void FFourfoldFxDebris::Idle(FPiece& P)
{
	if (P.Comp)
	{
		P.Comp->SetSimulatePhysics(false);
		P.Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		P.Comp->SetVisibility(false);
	}
	if (P.bActive)
	{
		--Active;
	}
	P.bActive = false;
	P.bSinking = false;
}

void FFourfoldFxDebris::UpdateColliders(AActor* Owner, USceneComponent* Root, const ffx::DrawList& List,
	TArray<TObjectPtr<UObject>>& OutRefs)
{
	TSet<uint32> Seen;
	for (const ffx::ColliderReq& R : List.colliders)
	{
		Seen.Add(R.key);
		UBoxComponent* B = nullptr;
		if (UBoxComponent** Found = Colliders.Find(R.key))
		{
			B = *Found;
		}
		else
		{
			B = IdleColliders.Num() > 0 ? IdleColliders.Pop(EAllowShrinking::No) : nullptr;
			if (!B)
			{
				B = NewObject<UBoxComponent>(Owner, NAME_None, RF_Transient);
				B->SetMobility(EComponentMobility::Movable);
				DebrisCollision(*B);
				B->SetHiddenInGame(true);
				B->SetBoxExtent(FVector(FF::SimToUE), false);   // unit box: the transform's scale carries the half extents (m)
				B->SetupAttachment(Root);
				B->RegisterComponent();
				OutRefs.Add(B);
			}
			B->SetPhysMaterialOverride(PhysMat);
			B->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
			Colliders.Add(R.key, B);
		}
		const FTransform T = FFFx::ToTransform(R.xform);
		if (!T.Equals(B->GetComponentTransform(), 0.1))
		{
			B->SetWorldTransform(T, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
	for (auto It = Colliders.CreateIterator(); It; ++It)
	{
		if (!Seen.Contains(It.Key()))
		{
			It.Value()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			IdleColliders.Add(It.Value());
			It.RemoveCurrent();
		}
	}
}

void FFourfoldFxDebris::OnHit(const UPrimitiveComponent* Comp, const FVector& Location, const FVector& NormalImpulse)
{
	if (int32(ImpactList.size()) >= kMaxImpacts)
	{
		return;
	}
	for (FPiece& P : Pieces)
	{
		if (P.Comp != Comp || !P.bActive || P.bSinking || P.HitCooldown > 0.0f)
		{
			continue;
		}
		const float Mass = FMath::Max(P.Comp->GetMass(), 0.1f);
		const float Speed = float(NormalImpulse.Size() / double(Mass) / FF::SimToUE);   // m/s
		if (Speed < kHitMinSpeed)
		{
			return;
		}
		P.HitCooldown = kHitCooldown;
		ffx::DebrisImpact H;
		H.pos = FF::ToSim(Location);
		H.speed = Speed;
		H.size = float(P.Comp->Bounds.SphereRadius / FF::SimToUE);
		ImpactList.push_back(H);
		return;
	}
}

void FFourfoldFxDebris::Update(AFourfoldFxActor* Owner, const ffx::DrawList& List,
	const TArray<TObjectPtr<UMaterialInterface>>& SlotMaterials, float Dt, TArray<TObjectPtr<UObject>>& OutRefs)
{
	// age: lie still, then stop simulating and sink into the ground
	for (FPiece& P : Pieces)
	{
		if (!P.bActive || !P.Comp)
		{
			continue;
		}
		P.Age += Dt;
		P.HitCooldown -= Dt;
		if (!P.bSinking && (P.Age >= P.Life || P.Comp->GetComponentLocation().Z < kKillZ))
		{
			P.Comp->SetSimulatePhysics(false);
			P.Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			P.SinkFrom = P.Comp->GetComponentLocation();
			P.bSinking = true;
			P.Age = 0.0f;
		}
		if (P.bSinking)
		{
			const float T = P.Age / SinkTime;
			if (T >= 1.0f || P.SinkFrom.Z < kKillZ)
			{
				Idle(P);
				continue;
			}
			P.Comp->SetWorldLocation(P.SinkFrom - FVector(0.0, 0.0, double(P.SinkDepth * T * T)));
		}
	}
	if (!IsReady())
	{
		return;
	}
	for (const ffx::FractureReq& R : List.fractures)
	{
		UMaterialInterface* Mat = SlotMaterials.IsValidIndex(int32(R.mat)) ? SlotMaterials[int32(R.mat)].Get() : nullptr;
		if (!R.pieces || R.pieces->empty() || !Mat)
		{
			continue;
		}
		// biggest pieces first when the budget runs short
		TArray<int32, TInlineAllocator<32>> Order;
		for (int32 i = 0; i < int32(R.pieces->size()); ++i)
		{
			Order.Add(i);
		}
		Order.Sort([&R](int32 A, int32 B) { return (*R.pieces)[size_t(A)].volume > (*R.pieces)[size_t(B)].volume; });
		for (int32 k = 0; k < Order.Num(); ++k)
		{
			if (Active >= MaxPieces)
			{
				Skipped += Order.Num() - k;
				break;
			}
			const ffx::FracturePiece& Shape = (*R.pieces)[size_t(Order[k])];
			const int32 Idx = Acquire(Owner, &Shape, OutRefs);
			if (Idx == INDEX_NONE)
			{
				++Skipped;
				continue;
			}
			Spawn(Pieces[Idx], R, Shape, Mat, Owner, OutRefs, ffx::HashCombine(R.seed, uint32(k)));
		}
	}
}

void FFourfoldFxDebris::ReleaseAll()
{
	for (const TPair<uint32, UBoxComponent*>& C : Colliders)
	{
		C.Value->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		IdleColliders.Add(C.Value);
	}
	Colliders.Reset();
	for (FPiece& P : Pieces)
	{
		if (P.bActive)
		{
			Idle(P);
		}
	}
	Active = 0;
}

FString FFourfoldFxDebris::GetDebugLine() const
{
	return FString::Printf(TEXT("debris: %d / %d active, %d components, %d spawned, %d over budget%s"), Active, MaxPieces,
		Pieces.Num(), Spawned, Skipped, bArena ? TEXT("") : TEXT(", no arena"));
}
