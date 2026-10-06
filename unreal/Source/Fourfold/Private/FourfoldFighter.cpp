// Fourfold - AFourfoldFighter: presents one sim actor. The sim is the only movement authority: position and facing
// are interpolated between the last two snapshots; the native anim runtime (UFourfoldAnimInstance) gets a recipe from
// the logic-island AnimDirector every frame (contact-aligned clips, stride-matched locomotion, reactions, hands,
// IK / springs / look-at parameters). Materials follow the actor's state (wet, frost, burn, charge glow).
// Without SK_Fighter (assets not imported yet) an engine-shape stand-in keeps the game playable.
#include "FourfoldFighter.h"

#include "FourfoldAnimInstance.h"
#include "FourfoldAnimLibrary.h"
#include "FourfoldCoords.h"
#include "FourfoldLog.h"
#include "FourfoldSettings.h"
#include "FourfoldSimSubsystem.h"
#include "Logic/FFGAnimDirector.h"
#include "Logic/FFGArena.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "UObject/ConstructorHelpers.h"

struct FFourfoldFighterImpl
{
	ffg::AnimDirector Director;
	bool bHaveMeasure = false;
	ff::Vec3 VelMeasured;      // sim m/s, horizontal
	ff::Vec3 VelSmooth;
	ff::Vec3 AccSmooth;
	float YawRate = 0.0f;
	float VisLift = 0.0f;      // metres (visual height smoothing)
	float VisY = 0.0f;
	bool bVisInit = false;
	bool bResetAnim = true;
	int64 LastTick = -1;
	float PrevHealth = -1.0f;
	float PrevBalance = 0.0f;
	bool bPrevGrounded = true;
	float PrevVy = 0.0f;
	// material smoothing
	float Wet = 0.0f, Frost = 0.0f, Burn = 0.0f, Glow = 0.0f;
	float SentWet = -1.0f, SentFrost = -1.0f, SentBurn = -1.0f, SentGlow = -1.0f;
	int32 SentElement = -1;
	// fallback pose
	float Lean = 0.0f, Tilt = 0.0f, Clock = 0.0f;
	TSharedPtr<const ffg::ArenaGround, ESPMode::ThreadSafe> Arena;
	const ff::ArenaView* ArenaSource = nullptr;
	FString AnimDebug;
};

namespace FourfoldFighterUtil
{
	// Element colours (linear) for FF_ElementColor and the fallback sash: Earth, Water, Fire, Air.
	static const FLinearColor ElementColors[4] = {FLinearColor(0.66f, 0.40f, 0.12f), FLinearColor(0.13f, 0.45f, 0.93f),
	                                              FLinearColor(0.96f, 0.19f, 0.07f), FLinearColor(0.48f, 0.79f, 0.71f)};

	static ff::Vec3 Forward(float Facing) { return ff::Vec3(FMath::Sin(Facing), 0.0f, FMath::Cos(Facing)); }

	static bool HasStatus(const ff::ActorView& A, const char* Name)
	{
		for (const ff::StatusView& S : A.statuses)
		{
			if (S.name == Name)
			{
				return true;
			}
		}
		return false;
	}
}

AFourfoldFighter::AFourfoldFighter()
{
	PrimaryActorTick.bCanEverTick = false;   // everything happens in the sim subsystem's OnFrame

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	BodyMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(Root);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetGenerateOverlapEvents(false);
	BodyMesh->CastShadow = true;
	// Animate after the sim subsystem ticked this frame (tickables run between TG_PostPhysics and TG_PostUpdateWork),
	// so the pose always matches the snapshot it was built from.
	BodyMesh->SetTickGroup(TG_PostUpdateWork);
	BodyMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	BodyMesh->bEnableUpdateRateOptimizations = false;

	// Engine shapes for the stand-in body (hard references so they are cooked).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	ShapeCylinder = CylinderFinder.Object;
	ShapeSphere = SphereFinder.Object;
	ShapeCube = CubeFinder.Object;
	ShapeMaterial = MaterialFinder.Object;
}

void AFourfoldFighter::InitFromSim(const ff::ActorView& Actor, const ff::Snapshot& Snapshot)
{
	SimActorId = Actor.id;
	if (Actor.is_player || Actor.id == Snapshot.player_id)
	{
		Role = TEXT("player");
	}
	else if (Actor.is_dummy)
	{
		Role = TEXT("dummy");
	}
	else
	{
		Role = TEXT("rival");
	}
	Impl = MakeShared<FFourfoldFighterImpl>();
	SetupBody();
	if (UFourfoldSimSubsystem* Sim = UFourfoldSimSubsystem::Get(this))
	{
		FrameHandle = Sim->OnFrame.AddUObject(this, &AFourfoldFighter::OnSimFrame);
	}
	SetActorLocationAndRotation(FF::ToUE(Actor.pos), FF::FacingToRotator(Actor.facing));
}

void AFourfoldFighter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UFourfoldSimSubsystem* Sim = UFourfoldSimSubsystem::Get(this))
	{
		Sim->OnFrame.Remove(FrameHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void AFourfoldFighter::SetupBody()
{
	UFourfoldAnimLibrarySubsystem* Lib = UFourfoldAnimLibrarySubsystem::Get(this);
	FString MeshPath = TEXT("/Game/Fourfold/Characters/Fighter/SK_Fighter");
	if (Lib)
	{
		MeshPath = Lib->GetCharacterInfo().MeshPath;
		MeshYawOffset = Lib->GetCharacterInfo().MeshYawOffsetDeg;
	}
	if (!MeshPath.Contains(TEXT(".")))
	{
		MeshPath = MeshPath + TEXT(".") + FPaths::GetBaseFilename(MeshPath);
	}
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Mesh)
	{
		static bool bWarned = false;
		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(LogFourfold, Warning, TEXT("Fighter mesh %s not found: using the engine-shape stand-in (run fourfold_setup.py)"), *MeshPath);
		}
		BuildFallbackBody();
		return;
	}
	bFallbackBody = false;
	BodyMesh->SetRelativeRotation(FRotator(0.0, MeshYawOffset, 0.0));
	BodyMesh->SetSkeletalMeshAsset(Mesh);
	if (Lib)
	{
		Lib->ValidateAgainstSkeleton(Mesh->GetSkeleton());
	}
	BodyMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	BodyMesh->SetAnimInstanceClass(UFourfoldAnimInstance::StaticClass());

	// One dynamic material instance per slot, tinted with the role palette.
	BodyMaterials.Reset();
	const TMap<FName, FLinearColor>* Palette = Lib ? Lib->GetCharacterInfo().Palettes.Find(Role) : nullptr;
	for (int32 i = 0; i < BodyMesh->GetNumMaterials(); ++i)
	{
		UMaterialInstanceDynamic* MID = BodyMesh->CreateDynamicMaterialInstance(i, BodyMesh->GetMaterial(i));
		if (!MID)
		{
			continue;
		}
		if (Palette)
		{
			for (const TPair<FName, FLinearColor>& P : *Palette)
			{
				MID->SetVectorParameterValue(P.Key, P.Value);
			}
		}
		BodyMaterials.Add(MID);
	}
	if (Lib)
	{
		Impl->Director.lib = &Lib->GetLibrary();
	}
	// Model axes of the mesh asset: forward = actor +X seen from the mesh (yaw offset), up = +Z, left = (fy, -fx).
	const double Yaw = FMath::DegreesToRadians(-MeshYawOffset);
	const float Fx = float(FMath::Cos(Yaw));
	const float Fy = float(FMath::Sin(Yaw));
	Impl->Director.axes.up = ff::Vec3(0.0f, 0.0f, 1.0f);
	Impl->Director.axes.fwd = ff::Vec3(Fx, Fy, 0.0f);
	Impl->Director.axes.left = ff::Vec3(Fy, -Fx, 0.0f);
	Impl->Director.Reset();
	Impl->bResetAnim = true;
}

void AFourfoldFighter::BuildFallbackBody()
{
	bFallbackBody = true;
	BodyMesh->SetVisibility(false);
	struct FPart
	{
		const TCHAR* Name;
		UStaticMesh* Mesh;
		FVector Location;   // cm, actor space (X forward, Y right, Z up)
		FVector Scale;      // engine shapes are 100 cm
		int32 ColorSlot;    // 0 main, 1 accent, 2 trim, 3 skin
	};
	const FPart Parts[] = {
		{TEXT("Torso"), ShapeCylinder, FVector(0, 0, 115), FVector(0.44, 0.36, 0.62), 0},
		{TEXT("Head"), ShapeSphere, FVector(0, 0, 166), FVector(0.24, 0.24, 0.27), 3},
		{TEXT("LegL"), ShapeCylinder, FVector(0, -12, 44), FVector(0.17, 0.17, 0.86), 0},
		{TEXT("LegR"), ShapeCylinder, FVector(0, 12, 44), FVector(0.17, 0.17, 0.86), 0},
		{TEXT("ArmL"), ShapeCylinder, FVector(0, -31, 118), FVector(0.13, 0.13, 0.66), 2},
		{TEXT("ArmR"), ShapeCylinder, FVector(0, 31, 118), FVector(0.13, 0.13, 0.66), 2},
		{TEXT("Sash"), ShapeCube, FVector(0, 0, 95), FVector(0.32, 0.48, 0.09), 1},
	};
	const UFourfoldAnimLibrarySubsystem* Lib = UFourfoldAnimLibrarySubsystem::Get(this);
	const TMap<FName, FLinearColor>* Palette = Lib ? Lib->GetCharacterInfo().Palettes.Find(Role) : nullptr;
	auto ColorOf = [Palette](int32 Slot) {
		static const FName Names[3] = {TEXT("FF_Main"), TEXT("FF_Accent"), TEXT("FF_Trim")};
		if (Slot == 3)
		{
			return FLinearColor(0.62f, 0.42f, 0.30f);
		}
		const FLinearColor* C = Palette ? Palette->Find(Names[Slot]) : nullptr;
		return C ? *C : FLinearColor(0.5f, 0.5f, 0.5f);
	};
	for (const FPart& P : Parts)
	{
		if (!P.Mesh)
		{
			continue;
		}
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, FName(P.Name));
		C->SetStaticMesh(P.Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetupAttachment(Root);
		C->SetRelativeLocation(P.Location);
		C->SetRelativeScale3D(P.Scale);
		C->RegisterComponent();
		if (ShapeMaterial)
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(ShapeMaterial, this);
			const FLinearColor Col = ColorOf(P.ColorSlot);
			MID->SetVectorParameterValue(TEXT("Color"), Col);
			MID->SetVectorParameterValue(TEXT("BaseColor"), Col);
			C->SetMaterial(0, MID);
			FallbackMaterials.Add(MID);
		}
		FallbackParts.Add(C);
	}
}

FVector AFourfoldFighter::GetBoneLocation(FName Bone) const
{
	if (BodyMesh && !bFallbackBody && BodyMesh->GetSkinnedAsset() && BodyMesh->GetBoneIndex(Bone) != INDEX_NONE)
	{
		return BodyMesh->GetBoneLocation(Bone, EBoneSpaces::WorldSpace);
	}
	// Approximate positions on the stand-in (actor space cm: X forward, Y right, Z up).
	const FString N = Bone.ToString();
	FVector Local(0.0, 0.0, 95.0);
	if (N.StartsWith(TEXT("head")))
	{
		Local = FVector(0.0, 0.0, 166.0);
	}
	else if (N.StartsWith(TEXT("spine")) || N.StartsWith(TEXT("neck")))
	{
		Local = FVector(0.0, 0.0, 140.0);
	}
	else if (N.StartsWith(TEXT("hand_l")))
	{
		Local = FVector(45.0, -25.0, 120.0);
	}
	else if (N.StartsWith(TEXT("hand_r")))
	{
		Local = FVector(45.0, 25.0, 120.0);
	}
	else if (N.StartsWith(TEXT("foot_l")) || N.StartsWith(TEXT("ball_l")))
	{
		Local = FVector(5.0, -10.0, 8.0);
	}
	else if (N.StartsWith(TEXT("foot_r")) || N.StartsWith(TEXT("ball_r")))
	{
		Local = FVector(5.0, 10.0, 8.0);
	}
	return GetActorTransform().TransformPosition(Local);
}

FString AFourfoldFighter::GetAnimDebug() const
{
	return Impl.IsValid() ? Impl->AnimDebug : FString();
}

void AFourfoldFighter::OnSimFrame(const FFourfoldFrame& Frame)
{
	if (!Impl.IsValid() || !Frame.Curr)
	{
		return;
	}
	const ff::ActorView* Cur = Frame.Curr->FindActor(SimActorId);
	if (!Cur)
	{
		return;
	}
	const ff::ActorView* Prev = Frame.Prev ? Frame.Prev->FindActor(SimActorId) : nullptr;
	if (!Prev)
	{
		Prev = Cur;
	}
	UFourfoldSimSubsystem* Sim = UFourfoldSimSubsystem::Get(this);
	const ff::Session* Session = (Sim && Sim->HasScenario()) ? &Sim->GetSession() : nullptr;

	// Measurements once per sim tick: ground velocity as the eye sees it, yaw rate, teleports.
	if (Frame.TicksStepped > 0 && Frame.Curr->tick != Impl->LastTick)
	{
		const ff::Vec3 D = Cur->pos - Prev->pos;
		ff::Vec3 V(D.x / float(ff::kSimDt), 0.0f, D.z / float(ff::kSimDt));
		if (V.length() > 12.0f)
		{
			V = ff::Vec3(Cur->vel.x, 0.0f, Cur->vel.z);   // a teleport / reset: use the sim velocity
		}
		if (D.length() > 3.0f)
		{
			Impl->bResetAnim = true;
			Impl->bVisInit = false;
		}
		Impl->VelMeasured = V;
		Impl->bHaveMeasure = true;
		Impl->YawRate = ffg::WrapAngle(Cur->facing - Prev->facing) / float(ff::kSimDt);
		Impl->LastTick = Frame.Curr->tick;
	}

	// Interpolated transform.
	const float Alpha = FMath::Clamp(Frame.Alpha, 0.0f, 1.0f);
	const ff::Vec3 Pos = Prev->pos.lerp(Cur->pos, Alpha);
	const float Facing = ffg::LerpAngle(Prev->facing, Cur->facing, Alpha);
	SetActorLocationAndRotation(FF::ToUE(Pos), FF::FacingToRotator(Facing));

	// Animation time: paused -> 0; Lab freeze -> one tick per manual step; Lab slow motion scales it.
	float AnimDt = 0.0f;
	if (!Frame.bPaused && Session)
	{
		AnimDt = Session->Frozen() ? float(Frame.TicksStepped) * float(ff::kSimDt) : Frame.GameDeltaSeconds * Session->TimeScale();
	}
	AnimDt = FMath::Clamp(AnimDt, 0.0f, 0.1f);

	// Visual height smoothing: the sim snaps the feet onto a step in one tick; the body follows over ~0.1 s.
	{
		const float Y = Pos.y;
		if (!Impl->bVisInit || FMath::Abs(Y - Impl->VisY) > 0.7f)
		{
			Impl->VisY = Y;
			Impl->bVisInit = true;
		}
		Impl->VisY = FMath::Lerp(Impl->VisY, Y, ffg::ExpK(Cur->grounded ? 14.0f : 40.0f, AnimDt));
		Impl->VisY = FMath::Clamp(Impl->VisY, Y - 0.45f, Y + 0.45f);
		Impl->VisLift = Impl->VisY - Y;
		if (!bFallbackBody)
		{
			BodyMesh->SetRelativeLocation(FVector(0.0, 0.0, double(Impl->VisLift) * FF::SimToUE));
		}
	}

	if (bFallbackBody)
	{
		UpdateFallbackPose(*Cur, AnimDt);
	}
	else
	{
		DriveAnimation(Frame, *Cur, *Prev, AnimDt);
		UpdateMaterials(*Cur, FMath::Max(Frame.RealDeltaSeconds, 0.0f));
	}
}

void AFourfoldFighter::DriveAnimation(const FFourfoldFrame& Frame, const ff::ActorView& Cur, const ff::ActorView& Prev, float AnimDt)
{
	UFourfoldAnimInstance* Anim = Cast<UFourfoldAnimInstance>(BodyMesh->GetAnimInstance());
	UFourfoldAnimLibrarySubsystem* Lib = UFourfoldAnimLibrarySubsystem::Get(this);
	if (!Anim || !Lib || !Impl->Director.lib)
	{
		return;
	}
	UFourfoldSimSubsystem* Sim = UFourfoldSimSubsystem::Get(this);
	FFourfoldFighterImpl& S = *Impl;

	// Smoothed ground velocity / acceleration in the fighter's own frame (sim ticks are 60 Hz, frames vary).
	const ff::Vec3 Raw = S.bHaveMeasure ? S.VelMeasured : ff::Vec3(Cur.vel.x, 0.0f, Cur.vel.z);
	const ff::Vec3 PrevVel = S.VelSmooth;
	S.VelSmooth = S.VelSmooth.lerp(Raw, ffg::ExpK(14.0f, AnimDt));
	if (AnimDt > 1e-5f)
	{
		S.AccSmooth = S.AccSmooth.lerp((S.VelSmooth - PrevVel) / AnimDt, ffg::ExpK(8.0f, AnimDt));
	}
	const ff::Vec3 Fwd = FourfoldFighterUtil::Forward(Cur.facing);
	const ff::Vec3 Right = Fwd.cross(ff::Vec3(0.0f, 1.0f, 0.0f));

	ffg::DirectorInput In;
	In.cur = &Cur;
	In.prev = &Prev;
	In.alpha = Frame.Alpha;
	In.dt = AnimDt;
	In.local_vel = ffg::Vec2(S.VelSmooth.dot(Right), S.VelSmooth.dot(Fwd));
	In.local_acc = ffg::Vec2(S.AccSmooth.dot(Right), S.AccSmooth.dot(Fwd));
	In.yaw_rate = FMath::Clamp(S.YawRate, -20.0f, 20.0f);
	In.dummy = Cur.is_dummy;

	// LOD: distance to the camera, dummies reduced, low quality reduced.
	int32 Lod = 0;
	if (const APlayerCameraManager* PCM = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		const double Dist = FVector::Dist(PCM->GetCameraLocation(), GetActorLocation()) / FF::SimToUE;
		Lod = Dist > 40.0 ? 2 : (Dist > 22.0 ? 1 : 0);
	}
	if (Cur.is_dummy)
	{
		Lod = FMath::Max(Lod, 1);
	}
	if (const UFourfoldSettingsSubsystem* Settings = UFourfoldSettingsSubsystem::Get(this))
	{
		if (Settings->GetEffectiveQuality() == 0)
		{
			Lod = FMath::Max(Lod, 1);
		}
	}
	In.lod = Lod;

	// Sim -> model space conversions (model = mesh component space, metres).
	const FTransform MeshXf = BodyMesh->GetComponentTransform();
	auto PointToModel = [&MeshXf](const ff::Vec3& P) {
		const FVector L = MeshXf.InverseTransformPosition(FF::ToUE(P)) / FF::SimToUE;
		return ff::Vec3(float(L.X), float(L.Y), float(L.Z));
	};
	auto DirToModel = [&MeshXf](const ff::Vec3& D) {
		const FVector L = MeshXf.InverseTransformVectorNoScale(FF::DirToUE(D));
		return ff::Vec3(float(L.X), float(L.Y), float(L.Z));
	};

	// Look at the nearest incoming attack body (by time to impact), else the locked target's head.
	if (Frame.Curr)
	{
		const ff::Vec3 Me = Cur.pos + ff::Vec3(0.0f, 1.5f, 0.0f);
		bool bFound = false;
		float BestT = 1.1f;
		ff::Vec3 Best;
		for (const ff::BodyView& B : Frame.Curr->bodies)
		{
			if (B.attack_id <= 0 || B.attack_owner == Cur.id || B.controller == Cur.id)
			{
				continue;
			}
			const ff::Vec3 Rel = Me - B.pos;
			const float Dist = Rel.length();
			if (Dist > 14.0f || Dist < 0.4f)
			{
				continue;
			}
			const float Closing = B.vel.dot(Rel / Dist);
			if (Closing < 2.0f)
			{
				continue;
			}
			const float Tti = Dist / Closing;
			if (Tti < BestT)
			{
				BestT = Tti;
				Best = B.pos;
				bFound = true;
			}
		}
		if (!bFound)
		{
			if (const ff::ActorView* T = Frame.Curr->FindActor(Cur.lock_target))
			{
				if (T->pos.distance_to(Cur.pos) < 24.0f)
				{
					Best = T->pos + ff::Vec3(0.0f, 1.5f, 0.0f);
					bFound = true;
				}
			}
		}
		if (bFound)
		{
			In.has_look_target = true;
			In.look_target = PointToModel(Best);
		}
	}

	// This actor's reactions this frame (springs, block impact, deflect, landing).
	if (Frame.Events)
	{
		bool bHitEvent = false;
		for (const ff::Event& E : *Frame.Events)
		{
			const int32 A = int32(E.data["actor"].as_int(-1));
			if (A != Cur.id && !(E.type == "interaction" && int32(E.data["counter_actor"].as_int(-1)) == Cur.id))
			{
				continue;
			}
			ffg::ReactionEvent R;
			if (E.type == "hit")
			{
				const float Dmg = E.data["damage"].as_f32(FMath::Max(S.PrevHealth - Cur.health, 0.0f));
				const float Bal = FMath::Max(S.PrevBalance - Cur.balance, 0.0f);
				R.kind = ffg::ReactionEvent::Hit;
				const ff::Vec3 Dir = E.data["dir"].is_vec3() ? E.data["dir"].as_vec3() : Cur.last_hit_dir;
				R.dir = DirToModel(Dir);
				R.strength = FMath::Clamp(0.35f + Dmg / 22.0f + Bal / 70.0f, 0.3f, 1.25f);
				R.knockdown = E.data["result"].as_string() == "knockdown";
				R.heavy = Cur.stun_kind == "heavy";
				In.events.push_back(R);
				bHitEvent = true;
			}
			else if (E.type == "block" || E.type == "guard_break")
			{
				R.kind = ffg::ReactionEvent::Block;
				R.strength = E.type == "guard_break" ? 1.0f : 0.55f;
				In.events.push_back(R);
			}
			else if (E.type == "perfect_deflect" || (E.type == "interaction" && E.data["perfect"].as_bool(false)))
			{
				R.kind = ffg::ReactionEvent::Perfect;
				In.events.push_back(R);
			}
			else if (E.type == "land")
			{
				R.kind = ffg::ReactionEvent::Land;
				R.down_speed = FMath::Max(E.data["speed"].as_f32(FMath::Abs(FMath::Min(S.PrevVy, 0.0f))), 0.0f);
				In.events.push_back(R);
			}
		}
		// Contact burns and other damage without a hit event: a small jolt.
		if (!bHitEvent && S.PrevHealth >= 0.0f && S.PrevHealth - Cur.health > 0.5f)
		{
			ffg::ReactionEvent R;
			R.kind = ffg::ReactionEvent::Burn;
			In.events.push_back(R);
		}
		// Landing without a land event.
		if (!S.bPrevGrounded && Cur.grounded)
		{
			bool bHad = false;
			for (const ffg::ReactionEvent& R : In.events)
			{
				bHad = bHad || R.kind == ffg::ReactionEvent::Land;
			}
			if (!bHad)
			{
				ffg::ReactionEvent R;
				R.kind = ffg::ReactionEvent::Land;
				R.down_speed = FMath::Abs(FMath::Min(S.PrevVy, 0.0f));
				In.events.push_back(R);
			}
		}
	}
	if (Frame.TicksStepped > 0)
	{
		S.PrevHealth = Cur.health;
		S.PrevBalance = Cur.balance;
		S.bPrevGrounded = Cur.grounded;
		S.PrevVy = Cur.vel.y;
	}

	if (S.bResetAnim)
	{
		S.Director.Reset();
	}
	const ffg::AnimRecipe& R = S.Director.Update(In);
	S.AnimDebug = FString(UTF8_TO_TCHAR(R.debug.c_str()));

	// The arena copy for the IK ground (rebuilt when the scenario changes).
	if (Sim && Sim->HasScenario())
	{
		const ff::ArenaView& AV = Sim->GetArena();
		if (S.ArenaSource != &AV || !S.Arena.IsValid())
		{
			TSharedPtr<ffg::ArenaGround, ESPMode::ThreadSafe> G = MakeShared<ffg::ArenaGround, ESPMode::ThreadSafe>();
			G->Set(AV);
			S.Arena = G;
			S.ArenaSource = &AV;
		}
	}

	FFourfoldAnimFrame F;
	auto Ref = [Lib](const ffg::ClipSample& C) {
		FFourfoldClipRef Out;
		Out.Seq = Lib->GetSequence(C.clip);
		Out.Time = C.time;
		Out.Weight = C.weight;
		Out.bLoop = C.clip && C.clip->loop;
		return Out;
	};
	for (const ffg::ClipSample& C : R.base)
	{
		F.Base.Add(Ref(C));
	}
	for (const ffg::ClipSample& C : R.legs)
	{
		F.Legs.Add(Ref(C));
	}
	F.LegsWeight = R.legs_weight;
	if (R.additive.clip)
	{
		F.Additive = Ref(R.additive);
		F.Additive.Weight = R.additive_weight;
	}
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (const ffg::ClipDef* H = R.hand[size_t(Side)])
		{
			F.Hand[Side].Seq = Lib->GetSequence(H);
			F.Hand[Side].Weight = R.hand_weight[size_t(Side)];
		}
		F.PlantHint[Side] = R.plant_hint[size_t(Side)];
	}
	F.TransitionSerial = R.transition_serial;
	F.TransitionTime = R.transition_time;
	F.LeanPitch = R.lean_pitch;
	F.LeanRoll = R.lean_roll;
	F.LandY = R.land_y;
	F.SpringTorso = R.spring_torso;
	F.SpringHead = R.spring_head;
	F.SpringArmL = R.spring_arm_l;
	F.SpringArmR = R.spring_arm_r;
	F.AimYaw = R.aim_yaw;
	F.AimWeight = R.aim_weight;
	F.bHasLook = R.has_look;
	F.LookTarget = R.look_target;
	F.LookWeight = R.look_weight;
	F.IkWeight = R.ik_weight;
	F.LockWeight = R.lock_weight;
	F.Breathe = R.breathe;
	F.BreathePhase = R.breathe_phase;
	F.Lod = R.lod;
	F.bChains = R.chains;
	F.Dt = R.dt;
	F.GroundSpeed = S.VelSmooth.length();
	F.LocalSpeed = In.local_vel.length();
	F.ModelLift = S.VisLift;
	F.bReset = S.bResetAnim;
	F.Axes = S.Director.axes;
	F.Arena = S.Arena;
	Anim->SetFrame(F);
	S.bResetAnim = false;
}

void AFourfoldFighter::UpdateMaterials(const ff::ActorView& Actor, float Dt)
{
	if (BodyMaterials.Num() == 0)
	{
		return;
	}
	FFourfoldFighterImpl& S = *Impl;
	const float K = ffg::ExpK(6.0f, Dt);
	const float FrostT = FourfoldFighterUtil::HasStatus(Actor, "frozen") ? 1.0f : (FourfoldFighterUtil::HasStatus(Actor, "chilled") ? 0.5f : 0.0f);
	const float BurnT = FourfoldFighterUtil::HasStatus(Actor, "burning") ? 1.0f : 0.0f;
	float GlowT = 0.0f;
	if (Actor.charge.active && Actor.charge.max_tier > 0)
	{
		GlowT = FMath::Clamp((float(Actor.charge.tier) + Actor.charge.frac) / float(Actor.charge.max_tier), 0.0f, 1.0f);
	}
	S.Wet += (FMath::Clamp(Actor.wetness, 0.0f, 1.0f) - S.Wet) * K;
	S.Frost += (FrostT - S.Frost) * K;
	S.Burn += (BurnT - S.Burn) * K;
	S.Glow += (GlowT - S.Glow) * ffg::ExpK(12.0f, Dt);
	auto Changed = [](float& Sent, float Now) {
		if (FMath::Abs(Sent - Now) < 0.004f)
		{
			return false;
		}
		Sent = Now;
		return true;
	};
	const bool bWet = Changed(S.SentWet, S.Wet);
	const bool bFrost = Changed(S.SentFrost, S.Frost);
	const bool bBurn = Changed(S.SentBurn, S.Burn);
	const bool bGlow = Changed(S.SentGlow, S.Glow);
	const int32 El = FMath::Clamp(Actor.element, 0, 3);
	const bool bEl = El != S.SentElement;
	S.SentElement = El;
	for (UMaterialInstanceDynamic* MID : BodyMaterials)
	{
		if (!MID)
		{
			continue;
		}
		if (bWet)
		{
			MID->SetScalarParameterValue(TEXT("FF_Wet"), S.Wet);
		}
		if (bFrost)
		{
			MID->SetScalarParameterValue(TEXT("FF_Frost"), S.Frost);
		}
		if (bBurn)
		{
			MID->SetScalarParameterValue(TEXT("FF_Burn"), S.Burn);
		}
		if (bGlow)
		{
			MID->SetScalarParameterValue(TEXT("FF_ElementGlow"), S.Glow);
		}
		if (bEl)
		{
			MID->SetVectorParameterValue(TEXT("FF_ElementColor"), FourfoldFighterUtil::ElementColors[El]);
		}
	}
}

void AFourfoldFighter::UpdateFallbackPose(const ff::ActorView& Actor, float Dt)
{
	if (FallbackParts.Num() < 6)
	{
		return;
	}
	FFourfoldFighterImpl& S = *Impl;
	const float Spd = ff::Vec2(Actor.vel.x, Actor.vel.z).length();
	S.Clock += Dt;
	S.Lean = FMath::Lerp(S.Lean, FMath::Clamp(Spd * 0.03f, 0.0f, 0.2f), ffg::ExpK(8.0f, Dt));
	const float Swing = FMath::Sin(S.Clock * (2.0f + Spd * 1.6f)) * FMath::Clamp(Spd * 0.12f, 0.0f, 0.6f);
	float ArmUp = 0.0f;
	if (Actor.action.active)
	{
		ArmUp = -1.2f;
	}
	if (Actor.guarding)
	{
		ArmUp = -1.6f;
	}
	const float TiltTarget = (Actor.stun > 0.0f && Actor.stun_kind == "knockdown") ? -1.3f : 0.0f;
	S.Tilt = FMath::Lerp(S.Tilt, TiltTarget, ffg::ExpK(10.0f, Dt));
	auto Pitch = [](UStaticMeshComponent* C, float Rad) {
		if (C)
		{
			C->SetRelativeRotation(FRotator(FMath::RadiansToDegrees(double(Rad)), 0.0, 0.0));
		}
	};
	Pitch(FallbackParts[2], Swing);
	Pitch(FallbackParts[3], -Swing);
	Pitch(FallbackParts[4], -Swing * 0.6f - ArmUp);
	Pitch(FallbackParts[5], Swing * 0.6f - ArmUp);
	Pitch(FallbackParts[0], -S.Lean);
	// Knockdown: the whole stand-in tips over backwards (the yaw stays the sim facing set this frame).
	SetActorRotation(FRotator(FMath::RadiansToDegrees(double(S.Tilt)), GetActorRotation().Yaw, 0.0));
}
