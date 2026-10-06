// Fourfold - placeholder arena + debug overlay (see FourfoldPlaceholderArena.h).
#include "FourfoldPlaceholderArena.h"

#include "FourfoldCoords.h"

#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AFourfoldPlaceholderArena::AFourfoldPlaceholderArena()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	Cube = CubeFinder.Object;
	BaseMaterial = MaterialFinder.Object;
	Tags.Add(TEXT("FourfoldPlaceholderArena"));
}

UStaticMeshComponent* AFourfoldPlaceholderArena::AddBox(const FString& Name, const FVector& MinUE, const FVector& MaxUE, const FLinearColor& Color)
{
	if (!Cube)
	{
		return nullptr;
	}
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, FName(*Name));
	C->SetStaticMesh(Cube);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);   // the sim does all collision analytically
	C->SetupAttachment(Root);
	const FVector Center = (MinUE + MaxUE) * 0.5;
	const FVector Size = (MaxUE - MinUE).GetAbs();
	C->SetRelativeLocation(Center);
	C->SetRelativeScale3D(FVector(FMath::Max(Size.X, 1.0), FMath::Max(Size.Y, 1.0), FMath::Max(Size.Z, 1.0)) / 100.0);   // cube = 100 cm
	C->RegisterComponent();
	if (BaseMaterial)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		MID->SetVectorParameterValue(TEXT("BaseColor"), Color);
		C->SetMaterial(0, MID);
	}
	Pieces.Add(C);
	return C;
}

void AFourfoldPlaceholderArena::Build(const ff::ArenaView& Arena)
{
	for (UStaticMeshComponent* C : Pieces)
	{
		if (C)
		{
			C->DestroyComponent();
		}
	}
	Pieces.Reset();
	Solids.Reset();
	SeeThroughNow.Reset();
	SetActorLocation(FVector::ZeroVector);

	const float H = Arena.half_size + 1.0f;
	const FLinearColor Stone(0.32f, 0.30f, 0.27f);
	const FLinearColor Floor(0.42f, 0.40f, 0.36f);
	auto Box = [this](const FString& Name, ff::Vec3 Mn, ff::Vec3 Mx, const FLinearColor& Col) {
		// sim (x, y up, z) -> UE (X, Y, Z up): min / max corners map component-wise.
		const FVector A = FF::ToUE(Mn), B = FF::ToUE(Mx);
		return AddBox(Name, FVector(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y), FMath::Min(A.Z, B.Z)),
		              FVector(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y), FMath::Max(A.Z, B.Z)), Col);
	};
	// Floor slab (top at y = 0) with a hole for the pool: four strips around it.
	const ff::Vec2 Pmin = Arena.pool_min, Pmax = Arena.pool_max;
	const bool bPool = Pmax.x > Pmin.x && Pmax.y > Pmin.y;
	const float T = 0.5f;   // slab thickness (m)
	if (bPool)
	{
		Box(TEXT("floor_n"), ff::Vec3(-H, -T, -H), ff::Vec3(H, 0.0f, Pmin.y), Floor);
		Box(TEXT("floor_s"), ff::Vec3(-H, -T, Pmax.y), ff::Vec3(H, 0.0f, H), Floor);
		Box(TEXT("floor_w"), ff::Vec3(-H, -T, Pmin.y), ff::Vec3(Pmin.x, 0.0f, Pmax.y), Floor);
		Box(TEXT("floor_e"), ff::Vec3(Pmax.x, -T, Pmin.y), ff::Vec3(H, 0.0f, Pmax.y), Floor);
		Box(TEXT("pool_basin"), ff::Vec3(Pmin.x, Arena.pool_floor - T, Pmin.y), ff::Vec3(Pmax.x, Arena.pool_floor, Pmax.y),
		    FLinearColor(0.22f, 0.24f, 0.24f));
		Box(TEXT("pool_water"), ff::Vec3(Pmin.x, Arena.pool_level - 0.01f, Pmin.y), ff::Vec3(Pmax.x, Arena.pool_level, Pmax.y),
		    FLinearColor(0.05f, 0.18f, 0.30f));
	}
	else
	{
		Box(TEXT("floor"), ff::Vec3(-H, -T, -H), ff::Vec3(H, 0.0f, H), Floor);
	}
	const ff::Vec2 Mmin = Arena.metal_min, Mmax = Arena.metal_max;
	if (Mmax.x > Mmin.x && Mmax.y > Mmin.y)
	{
		Box(TEXT("metal_plate"), ff::Vec3(Mmin.x, 0.0f, Mmin.y), ff::Vec3(Mmax.x, Arena.metal_top, Mmax.y), FLinearColor(0.45f, 0.47f, 0.50f));
	}
	for (const ff::ArenaBox& S : Arena.solids)
	{
		const FString Name = S.name.empty() ? FString::Printf(TEXT("solid_%d"), Solids.Num()) : FString(UTF8_TO_TCHAR(S.name.c_str()));
		FLinearColor Col = Stone;
		if (S.kind == "ledge")
		{
			Col = FLinearColor(0.38f, 0.35f, 0.30f);
		}
		else if (S.kind == "pillar")
		{
			Col = FLinearColor(0.28f, 0.26f, 0.24f);
		}
		if (UStaticMeshComponent* C = Box(Name, S.min, S.max, Col))
		{
			Solids.Add(Name, C);
		}
	}
}

void AFourfoldPlaceholderArena::SetSeeThrough(const TArray<FString>& Names)
{
	for (const FString& N : SeeThroughNow)
	{
		if (!Names.Contains(N))
		{
			if (TObjectPtr<UStaticMeshComponent>* C = Solids.Find(N))
			{
				(*C)->SetRenderInMainPass(true);
			}
		}
	}
	for (const FString& N : Names)
	{
		if (!SeeThroughNow.Contains(N))
		{
			if (TObjectPtr<UStaticMeshComponent>* C = Solids.Find(N))
			{
				(*C)->SetRenderInMainPass(false);   // still casts its shadow, never blocks the view
			}
		}
	}
	SeeThroughNow = Names;
}

void AFourfoldPlaceholderArena::SetSeeThroughOnLevel(UWorld* World, const TArray<FString>& Names, TArray<FString>& InOutApplied)
{
	if (!World || Names == InOutApplied)
	{
		return;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		for (const FName& Tag : A->Tags)
		{
			FString S = Tag.ToString();
			if (!S.RemoveFromStart(TEXT("FFSolid_")))
			{
				continue;
			}
			const bool bHide = Names.Contains(S);
			if (bHide != InOutApplied.Contains(S))
			{
				TArray<UPrimitiveComponent*> Prims;
				A->GetComponents<UPrimitiveComponent>(Prims);
				for (UPrimitiveComponent* P : Prims)
				{
					P->SetRenderInMainPass(!bHide);
				}
			}
		}
	}
	InOutApplied = Names;
}

void AFourfoldPlaceholderArena::DrawDebug(UWorld* World, const ff::ArenaView& Arena, const ff::Snapshot& Snap)
{
	if (!World)
	{
		return;
	}
	for (const ff::ArenaBox& S : Arena.solids)
	{
		const FVector A = FF::ToUE(S.min), B = FF::ToUE(S.max);
		DrawDebugBox(World, (A + B) * 0.5, (B - A).GetAbs() * 0.5, FColor(255, 220, 60), false, -1.0f, 0, 1.5f);
	}
	for (const ff::BodyView& Bd : Snap.bodies)
	{
		const FVector P = FF::ToUE(Bd.pos);
		const bool bHot = Bd.temp > 300.0f;
		const FColor Col = Bd.attack_id > 0 ? FColor(255, 90, 70) : (bHot ? FColor(255, 160, 60) : FColor(120, 255, 160));
		DrawDebugSphere(World, P, float(FMath::Max(double(Bd.radius) * FF::SimToUE, 5.0)), 10, Col, false, -1.0f, 0, 1.0f);
		if (Bd.zone_radius > 0.0f)
		{
			DrawDebugCircle(World, FVector(P.X, P.Y, P.Z + 3.0), float(double(Bd.zone_radius) * FF::SimToUE), 40, FColor(140, 170, 255), false, -1.0f, 0, 2.0f,
			                FVector(1, 0, 0), FVector(0, 1, 0), false);
		}
	}
	for (const ff::ActorView& A : Snap.actors)
	{
		const FVector P = FF::ToUE(A.pos);
		DrawDebugCapsule(World, P + FVector(0, 0, 90), 90.0f, 35.0f, FQuat::Identity, A.is_player ? FColor(80, 200, 255) : FColor(255, 120, 120),
		                 false, -1.0f, 0, 1.0f);
	}
}
