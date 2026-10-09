// Fourfold - open-world valley at runtime (see Public/FourfoldOpenWorld.h).
#include "FourfoldOpenWorld.h"

#include "FourfoldCoords.h"
#include "FourfoldLog.h"
#include "ff/Json.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"

#include <string>

namespace FourfoldOW
{
	static FString DataDir()
	{
		return FPaths::ProjectContentDir() / TEXT("Fourfold") / TEXT("Data") / TEXT("openworld");
	}

	struct FCache
	{
		std::shared_ptr<const ff::WorldDef> World;
		TArray<uint8> Splat;
		ff::Value Json;
		FString Error;
		bool bTried = false;
	};

	static FCache& Cache()
	{
		static FCache C;
		return C;
	}

	static void LoadOnce()
	{
		FCache& C = Cache();
		if (C.bTried)
		{
			return;
		}
		C.bTried = true;
		FString Text;
		TArray<uint8> Heights;
		if (!FFileHelper::LoadFileToString(Text, *(DataDir() / TEXT("world.json"))) ||
		    !FFileHelper::LoadFileToArray(Heights, *(DataDir() / TEXT("heights.f32"))))
		{
			C.Error = FString::Printf(TEXT("open world data missing in %s (run unreal/Tools/openworld/gen_world.py)"), *DataDir());
			return;
		}
		const std::string Utf8 = TCHAR_TO_UTF8(*Text);
		auto W = std::make_shared<ff::WorldDef>();
		std::string Err;
		if (!ff::WorldDef::Parse(Utf8, Heights.GetData(), size_t(Heights.Num()), *W, &Err))
		{
			C.Error = UTF8_TO_TCHAR(Err.c_str());
			return;
		}
		ff::ParseJson(Utf8, C.Json);
		FFileHelper::LoadFileToArray(C.Splat, *(DataDir() / TEXT("splat.rgba")));
		if (C.Splat.Num() != W->nx * W->nz * 4)
		{
			C.Splat.Reset();
		}
		C.World = W;
	}

	inline float SampleH(const ff::WorldDef& W, int32 I, int32 K)
	{
		I = FMath::Clamp(I, 0, W.nx - 1);
		K = FMath::Clamp(K, 0, W.nz - 1);
		return W.heights[size_t(K) * size_t(W.nx) + size_t(I)];
	}
}

AFourfoldOpenWorld::AFourfoldOpenWorld()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	Tags.Add(FName(TEXT("FourfoldArena")));
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Static);

	TerrainMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Fourfold/OpenWorld/Materials/MI_OW_Terrain.MI_OW_Terrain")));
	WaterMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Fourfold/Env/Materials/MI_Env_Water.MI_Env_Water")));
	StoneMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Fourfold/Env/Materials/MI_Env_StoneWall.MI_Env_StoneWall")));
	MarkerMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Fourfold/Env/Materials/MI_Env_Glow.MI_Env_Glow")));
	const TCHAR* Trees[][2] = {
		{TEXT("tree_broadleaf_a"), TEXT("SM_Tree_Broadleaf_a")}, {TEXT("tree_broadleaf_b"), TEXT("SM_Tree_Broadleaf_b")},
		{TEXT("tree_fir_a"), TEXT("SM_Tree_Fir_a")}, {TEXT("tree_fir_b"), TEXT("SM_Tree_Fir_b")}, {TEXT("tree_fir_c"), TEXT("SM_Tree_Fir_c")},
		{TEXT("tree_firslim_a"), TEXT("SM_Tree_FirSlim_a")}, {TEXT("tree_firslim_b"), TEXT("SM_Tree_FirSlim_b")},
		{TEXT("tree_firslim_c"), TEXT("SM_Tree_FirSlim_c")}, {TEXT("rock_granite"), TEXT("SM_Rock_Granite")},
		{TEXT("rock_strata"), TEXT("SM_Rock_Strata")}, {TEXT("rock_cliff"), TEXT("SM_Rock_Cliff")}};
	for (const auto& T : Trees)
	{
		const FString Path = FString::Printf(TEXT("/Game/Fourfold/Env/Trees/%s.%s"), T[1], T[1]);
		PropMeshes.Add(FName(T[0]), TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(Path)));
	}
}

std::shared_ptr<const ff::WorldDef> AFourfoldOpenWorld::LoadWorldDef(FString* OutError)
{
	FourfoldOW::LoadOnce();
	if (OutError)
	{
		*OutError = FourfoldOW::Cache().Error;
	}
	return FourfoldOW::Cache().World;
}

float AFourfoldOpenWorld::TerrainHeightUE(const FVector& WorldUE) const
{
	return World ? float(World->HeightAt(WorldUE.X / FF::SimToUE, WorldUE.Y / FF::SimToUE)) : 0.0f;
}

void AFourfoldOpenWorld::BeginPlay()
{
	Super::BeginPlay();
	FString Err;
	World = LoadWorldDef(&Err);
	if (!World)
	{
		UE_LOG(LogFourfold, Error, TEXT("AFourfoldOpenWorld: %s"), *Err);
		return;
	}
	Splat = FourfoldOW::Cache().Splat;
	BuildAll();
}

void AFourfoldOpenWorld::BuildAll()
{
	const double T0 = FPlatformTime::Seconds();
	TerrainMat = TerrainMaterial.LoadSynchronous();
	if (!TerrainMat)
	{
		TerrainMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Fourfold/Env/Materials/MI_Env_Ground.MI_Env_Ground"));
	}
	const int32 Cells = World->nx - 1;
	const int32 NumC = FMath::Max(1, Cells / ChunkCells);
	for (int32 Cz = 0; Cz < NumC; ++Cz)
	{
		for (int32 Cx = 0; Cx < NumC; ++Cx)
		{
			FChunk C;
			C.Cx = Cx;
			C.Cz = Cz;
			C.Mesh = NewObject<UProceduralMeshComponent>(this, *FString::Printf(TEXT("Terrain_%02d_%02d"), Cx, Cz));
			C.Mesh->bUseAsyncCooking = true;
			C.Mesh->SetMobility(EComponentMobility::Static);
			C.Mesh->SetupAttachment(RootComponent);
			const double Ox = World->x0 + double(Cx * ChunkCells) * World->cell;
			const double Oz = World->z0 + double(Cz * ChunkCells) * World->cell;
			C.Mesh->SetRelativeLocation(FF::WorldToUE(Ox, 0.0, Oz));
			C.Mesh->SetCollisionProfileName(TEXT("BlockAll"));
			C.Mesh->RegisterComponent();
			BuildChunkSection(C, 0, 4, false);
			const double Size = double(ChunkCells) * World->cell;
			float MinH = 1e9f, MaxH = -1e9f;
			for (int32 K = Cz * ChunkCells; K <= (Cz + 1) * ChunkCells; K += 4)
			{
				for (int32 I = Cx * ChunkCells; I <= (Cx + 1) * ChunkCells; I += 4)
				{
					const float H = FourfoldOW::SampleH(*World, I, K);
					MinH = FMath::Min(MinH, H);
					MaxH = FMath::Max(MaxH, H);
				}
			}
			C.Bounds = FBox(FF::WorldToUE(Ox, MinH, Oz), FF::WorldToUE(Ox + Size, MaxH, Oz + Size));
			Chunks.Add(C);
		}
	}
	BuildWater();
	BuildSolids();
	BuildSitesAndProps();
	bBuilt = true;
	UE_LOG(LogFourfold, Log, TEXT("Open world built: %d terrain chunks, %d prop sets in %.0f ms"), Chunks.Num(), PropComps.Num(),
	       (FPlatformTime::Seconds() - T0) * 1000.0);
}

void AFourfoldOpenWorld::BuildChunkSection(FChunk& C, int32 Section, int32 Step, bool bCollision)
{
	const ff::WorldDef& W = *World;
	const int32 N = ChunkCells / Step + 1;
	const int32 I0 = C.Cx * ChunkCells;
	const int32 K0 = C.Cz * ChunkCells;
	const double Cell = W.cell;
	const float Skirt = Step == 1 ? 300.0f : 1200.0f;   // cm

	TArray<FVector> V;
	TArray<FVector> Nrm;
	TArray<FVector2D> UV;
	TArray<FLinearColor> Col;
	TArray<FProcMeshTangent> Tan;
	TArray<int32> Tri;
	V.Reserve(N * N + 4 * N);
	Nrm.Reserve(N * N + 4 * N);
	UV.Reserve(N * N + 4 * N);
	Col.Reserve(N * N + 4 * N);
	Tan.Reserve(N * N + 4 * N);

	auto AddVert = [&](int32 A, int32 B, float Drop) {
		const int32 I = I0 + A * Step;
		const int32 K = K0 + B * Step;
		const float H = FourfoldOW::SampleH(W, I, K);
		const float Dx = (FourfoldOW::SampleH(W, I + Step, K) - FourfoldOW::SampleH(W, I - Step, K)) / float(2.0 * Step * Cell);
		const float Dz = (FourfoldOW::SampleH(W, I, K + Step) - FourfoldOW::SampleH(W, I, K - Step)) / float(2.0 * Step * Cell);
		V.Add(FVector(double(A * Step) * Cell * FF::SimToUE, double(B * Step) * Cell * FF::SimToUE, double(H) * FF::SimToUE - Drop));
		Nrm.Add(FVector(-Dx, -Dz, 1.0).GetSafeNormal());   // UE (x, z, y) of the sim normal (-dh/dx, 1, -dh/dz)
		Tan.Add(FProcMeshTangent(FVector(1.0, 0.0, Dx).GetSafeNormal(), false));
		const double Wx = W.x0 + double(I) * Cell;
		const double Wz = W.z0 + double(K) * Cell;
		UV.Add(FVector2D(Wx / 4.0, Wz / 4.0));
		if (Splat.Num() > 0)
		{
			const int32 Ic = FMath::Clamp(I, 0, W.nx - 1);
			const int32 Kc = FMath::Clamp(K, 0, W.nz - 1);
			const uint8* S = &Splat[(size_t(Kc) * size_t(W.nx) + size_t(Ic)) * 4];
			Col.Add(FLinearColor(S[0] / 255.0f, S[1] / 255.0f, S[2] / 255.0f, S[3] / 255.0f));
		}
		else
		{
			Col.Add(FLinearColor(1.0f, 0.0f, 0.0f, 0.0f));
		}
	};
	// Measured in game (2026-10-10): with the grid below, Unreal's front face is (A, C, B).
	auto AddTri = [&](int32 A, int32 B, int32 Cc) {
		if (bFlipWinding)
		{
			Tri.Append({A, B, Cc});
		}
		else
		{
			Tri.Append({A, Cc, B});
		}
	};

	for (int32 B = 0; B < N; ++B)
	{
		for (int32 A = 0; A < N; ++A)
		{
			AddVert(A, B, 0.0f);
		}
	}
	for (int32 B = 0; B + 1 < N; ++B)
	{
		for (int32 A = 0; A + 1 < N; ++A)
		{
			const int32 I00 = B * N + A;
			const int32 I10 = I00 + 1;
			const int32 I01 = I00 + N;
			const int32 I11 = I01 + 1;
			AddTri(I00, I10, I01);
			AddTri(I10, I11, I01);
		}
	}
	// Skirts: each border vertex dropped by Skirt, both windings (cheap, never visible from inside the terrain).
	auto Edge = [&](TFunctionRef<int32(int32)> Idx) {
		const int32 Base = V.Num();
		for (int32 T = 0; T < N; ++T)
		{
			const int32 Src = Idx(T);
			const int32 A = Src % N;
			const int32 B = Src / N;
			AddVert(A, B, Skirt);
		}
		for (int32 T = 0; T + 1 < N; ++T)
		{
			const int32 U0 = Idx(T), U1 = Idx(T + 1), D0 = Base + T, D1 = Base + T + 1;
			AddTri(U0, U1, D0);
			AddTri(U1, D1, D0);
			AddTri(U0, D0, U1);
			AddTri(U1, D0, D1);
		}
	};
	Edge([N](int32 T) { return T; });
	Edge([N](int32 T) { return (N - 1) * N + T; });
	Edge([N](int32 T) { return T * N; });
	Edge([N](int32 T) { return T * N + N - 1; });

	const TArray<FVector2D> Empty;
	C.Mesh->CreateMeshSection_LinearColor(Section, V, Tri, Nrm, UV, Empty, Empty, Empty, Col, Tan, bCollision, false);
	if (TerrainMat)
	{
		C.Mesh->SetMaterial(Section, TerrainMat);
	}
}

void AFourfoldOpenWorld::BuildWater()
{
	const ff::WorldDef& W = *World;
	int32 IMin = W.nx, IMax = -1, KMin = W.nz, KMax = -1;
	for (int32 K = 0; K < W.nz; K += 2)
	{
		for (int32 I = 0; I < W.nx; I += 2)
		{
			if (W.heights[size_t(K) * size_t(W.nx) + size_t(I)] < W.water_level)
			{
				IMin = FMath::Min(IMin, I);
				IMax = FMath::Max(IMax, I);
				KMin = FMath::Min(KMin, K);
				KMax = FMath::Max(KMax, K);
			}
		}
	}
	if (IMax < 0)
	{
		return;
	}
	const double X0 = W.x0 + (IMin - 8) * W.cell, X1 = W.x0 + (IMax + 8) * W.cell;
	const double Z0 = W.z0 + (KMin - 8) * W.cell, Z1 = W.z0 + (KMax + 8) * W.cell;
	UProceduralMeshComponent* Water = NewObject<UProceduralMeshComponent>(this, TEXT("Lake"));
	Water->SetupAttachment(RootComponent);
	Water->SetMobility(EComponentMobility::Static);
	Water->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Water->RegisterComponent();
	constexpr int32 G = 33;
	TArray<FVector> V;
	TArray<FVector> Nrm;
	TArray<FVector2D> UV;
	TArray<FLinearColor> Col;
	TArray<FProcMeshTangent> Tan;
	TArray<int32> Tri;
	for (int32 B = 0; B < G; ++B)
	{
		for (int32 A = 0; A < G; ++A)
		{
			const double X = X0 + (X1 - X0) * A / (G - 1);
			const double Z = Z0 + (Z1 - Z0) * B / (G - 1);
			V.Add(FF::WorldToUE(X, W.water_level, Z));
			Nrm.Add(FVector::UpVector);
			Tan.Add(FProcMeshTangent(FVector::ForwardVector, false));
			UV.Add(FVector2D(X / 8.0, Z / 8.0));
			// depth in alpha (shore fade for materials that read vertex alpha)
			const float Depth = float(W.water_level - W.HeightAt(X, Z));
			Col.Add(FLinearColor(1.0f, 1.0f, 1.0f, FMath::Clamp(Depth / 3.0f, 0.0f, 1.0f)));
		}
	}
	for (int32 B = 0; B + 1 < G; ++B)
	{
		for (int32 A = 0; A + 1 < G; ++A)
		{
			const int32 I00 = B * G + A, I10 = I00 + 1, I01 = I00 + G, I11 = I01 + 1;
			if (bFlipWinding)
			{
				Tri.Append({I00, I10, I01, I10, I11, I01});
			}
			else
			{
				Tri.Append({I00, I01, I10, I10, I01, I11});
			}
		}
	}
	const TArray<FVector2D> Empty;
	Water->CreateMeshSection_LinearColor(0, V, Tri, Nrm, UV, Empty, Empty, Empty, Col, Tan, false, false);
	if (UMaterialInterface* M = WaterMaterial.LoadSynchronous())
	{
		Water->SetMaterial(0, M);
	}
	Water->SetCastShadow(false);
}

void AFourfoldOpenWorld::BuildSolids()
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Stone = StoneMaterial.LoadSynchronous();
	if (!Cube)
	{
		return;
	}
	int32 Index = 0;
	for (const ff::WorldBox& B : World->solids)
	{
		UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("Solid_%s"), UTF8_TO_TCHAR(B.name.c_str())));
		M->SetupAttachment(RootComponent);
		M->SetMobility(EComponentMobility::Static);
		M->SetStaticMesh(Cube);
		const FVector Lo = FF::WorldToUE(B.min.x, B.min.y, B.min.z);
		const FVector Hi = FF::WorldToUE(B.max.x, B.max.y, B.max.z);
		M->SetRelativeLocation((Lo + Hi) * 0.5);
		M->SetRelativeScale3D((Hi - Lo) / 100.0);
		if (Stone)
		{
			M->SetMaterial(0, Stone);
		}
		M->RegisterComponent();
		++Index;
	}
}

void AFourfoldOpenWorld::BuildSitesAndProps()
{
	// Encounter-site waystones: a stone post with a glowing cap (the fighter waits beside it).
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Stone = StoneMaterial.LoadSynchronous();
	UMaterialInterface* Glow = MarkerMaterial.LoadSynchronous();
	for (const ff::EncounterSite& S : World->sites)
	{
		if (!Cube)
		{
			break;
		}
		const FVector Base = FF::WorldToUE(S.pos.x + 2.5 * std::sin(S.facing + 1.6), S.pos.y, S.pos.z + 2.5 * std::cos(S.facing + 1.6));
		UStaticMeshComponent* Post = NewObject<UStaticMeshComponent>(this);
		Post->SetupAttachment(RootComponent);
		Post->SetMobility(EComponentMobility::Static);
		Post->SetStaticMesh(Cube);
		Post->SetRelativeLocation(Base + FVector(0, 0, 130));
		Post->SetRelativeScale3D(FVector(0.45, 0.45, 3.0));
		Post->SetMaterial(0, Stone);
		Post->RegisterComponent();
		UStaticMeshComponent* Cap = NewObject<UStaticMeshComponent>(this);
		Cap->SetupAttachment(RootComponent);
		Cap->SetMobility(EComponentMobility::Static);
		Cap->SetStaticMesh(Cube);
		Cap->SetRelativeLocation(Base + FVector(0, 0, 290));
		Cap->SetRelativeScale3D(FVector(0.3, 0.3, 0.3));
		Cap->SetRelativeRotation(FRotator(45.0, 45.0, 0.0));
		Cap->SetMaterial(0, Glow);
		Cap->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Cap->RegisterComponent();
	}

	// Trees and rocks: one hierarchical instanced component per mesh, distance culled.
	const ff::Value& Json = FourfoldOW::Cache().Json;
	const ff::Array Props = Json.as_dict().get("props").as_array();
	TMap<FName, UHierarchicalInstancedStaticMeshComponent*> ByKey;
	TMap<FName, double> NormScale;
	int32 Placed = 0;
	for (const ff::Value& P : Props)
	{
		const ff::Dict D = P.as_dict();
		const FName MeshKey(UTF8_TO_TCHAR(D.get("mesh").as_string().c_str()));
		const bool bCliff = D.has("size");   // big rock faces: own component, seen across the valley
		const FName Key = bCliff ? FName(*(MeshKey.ToString() + TEXT("_cliff"))) : MeshKey;
		UHierarchicalInstancedStaticMeshComponent** Found = ByKey.Find(Key);
		if (!Found)
		{
			const TSoftObjectPtr<UStaticMesh>* Soft = PropMeshes.Find(MeshKey);
			UStaticMesh* Mesh = Soft ? Soft->LoadSynchronous() : nullptr;
			if (!Mesh)
			{
				ByKey.Add(Key, nullptr);
				UE_LOG(LogFourfold, Warning, TEXT("Open world: no mesh for prop '%s'"), *Key.ToString());
				continue;
			}
			UHierarchicalInstancedStaticMeshComponent* H = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
			H->SetupAttachment(RootComponent);
			H->SetMobility(EComponentMobility::Static);
			H->SetStaticMesh(Mesh);
			const bool bRock = MeshKey.ToString().StartsWith(TEXT("rock"));
			const float Cull = bCliff ? CliffCullDistance : (bRock ? RockCullDistance : TreeCullDistance);
			H->SetCullDistances(int32(Cull * 0.8f), int32(Cull));
			H->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			H->RegisterComponent();
			PropComps.Add(H);
			ByKey.Add(Key, H);
			Found = ByKey.Find(Key);
			// Normalise scanned meshes to game sizes: rocks ~3 m across, trees at most 18 m tall.
			const FVector Ext = Mesh->GetBoundingBox().GetSize();
			double Norm = 1.0;
			if (bRock)
			{
				Norm = 300.0 / FMath::Max(1.0, FMath::Max(Ext.X, Ext.Y));
			}
			else if (Ext.Z > 1800.0)
			{
				Norm = 1800.0 / Ext.Z;
			}
			NormScale.Add(Key, Norm);
		}
		if (!*Found)
		{
			continue;
		}
		const ff::Array Pos = D.get("pos").as_array();
		double Sc = D.get("scale").as_float(1.0) * NormScale[Key];
		if (D.has("size"))
		{
			// explicit size (metres across): cliffs and boulders placed by the generator
			const FVector Ext = (*Found)->GetStaticMesh()->GetBoundingBox().GetSize();
			Sc = D.get("size").as_float(3.0) * 100.0 / FMath::Max(1.0, FMath::Max(Ext.X, Ext.Y));
		}
		const FTransform Xf(FRotator(0.0, D.get("yaw").as_float(0.0), 0.0),
		                    FF::WorldToUE(Pos.get(0).as_float(), Pos.get(1).as_float(), Pos.get(2).as_float()), FVector(Sc));
		(*Found)->AddInstance(Xf, false);
		++Placed;
	}
	UE_LOG(LogFourfold, Log, TEXT("Open world: %d props placed"), Placed);
}

void AFourfoldOpenWorld::UpdateLods(const FVector& ViewUE)
{
	// Nearest chunks first, at most two new 2 m sections per update.
	int32 Built = 0;
	TArray<TPair<double, int32>> Want;
	for (int32 i = 0; i < Chunks.Num(); ++i)
	{
		FChunk& C = Chunks[i];
		const double D = FMath::Sqrt(C.Bounds.ComputeSquaredDistanceToPoint(ViewUE));
		const bool bNear = D < (C.bNear ? NearRadius * 1.15 : NearRadius);
		if (bNear && !C.bNearBuilt)
		{
			Want.Add(TPair<double, int32>(D, i));
		}
		else if (!bNear && C.bNearBuilt && D > NearRadius * 1.8)
		{
			C.Mesh->ClearMeshSection(1);
			C.bNearBuilt = false;
		}
		if (bNear != C.bNear && (C.bNearBuilt || !bNear))
		{
			C.bNear = bNear;
			C.Mesh->SetMeshSectionVisible(0, !bNear);
			if (C.bNearBuilt)
			{
				C.Mesh->SetMeshSectionVisible(1, bNear);
			}
		}
	}
	Want.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
	for (const TPair<double, int32>& W : Want)
	{
		if (Built >= 2)
		{
			break;
		}
		FChunk& C = Chunks[W.Value];
		BuildChunkSection(C, 1, 1, true);
		C.bNearBuilt = true;
		C.bNear = true;
		C.Mesh->SetMeshSectionVisible(0, false);
		C.Mesh->SetMeshSectionVisible(1, true);
		++Built;
	}
}

void AFourfoldOpenWorld::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bBuilt)
	{
		return;
	}
	FVector View = FVector::ZeroVector;
	if (APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		View = Cam->GetCameraLocation();
	}
	UpdateLods(View);
}
