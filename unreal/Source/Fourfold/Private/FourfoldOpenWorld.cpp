// Fourfold - open-world valley at runtime (see Public/FourfoldOpenWorld.h).
#include "FourfoldOpenWorld.h"

#include "FourfoldCoords.h"
#include "FourfoldLog.h"
#include "FourfoldSimSubsystem.h"
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
	static FString DataDir(const FString& Name)
	{
		return FPaths::ProjectContentDir() / TEXT("Fourfold") / TEXT("Data") / Name;
	}

	struct FCache
	{
		std::shared_ptr<const ff::WorldDef> World;
		TArray<uint8> Splat;
		ff::Value Json;
		FString Error;
		bool bTried = false;
	};

	// One cache per data set, so L_World and L_Ruins can both be played in one process.
	static FCache& Cache(const FString& Name)
	{
		static TMap<FString, TUniquePtr<FCache>> Caches;
		TUniquePtr<FCache>& C = Caches.FindOrAdd(Name);
		if (!C.IsValid())
		{
			C = MakeUnique<FCache>();
		}
		return *C;
	}

	static void LoadOnce(const FString& Name)
	{
		FCache& C = Cache(Name);
		if (C.bTried)
		{
			return;
		}
		C.bTried = true;
		const FString Dir = DataDir(Name);
		FString Text;
		TArray<uint8> Heights;
		if (!FFileHelper::LoadFileToString(Text, *(Dir / TEXT("world.json"))) ||
		    !FFileHelper::LoadFileToArray(Heights, *(Dir / TEXT("heights.f32"))))
		{
			C.Error = FString::Printf(TEXT("world data missing in %s (openworld: unreal/Tools/openworld/gen_world.py, ruins: unreal/Tools/ruins/bake_ruins.py)"), *Dir);
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
		if (!W->water_layer.empty())
		{
			// per-sample water surface (sloped rivers): world.json water.layer names the float32 file next to it
			const FString Layer = UTF8_TO_TCHAR(W->water_layer.c_str());
			TArray<uint8> Water;
			if (!FFileHelper::LoadFileToArray(Water, *(Dir / Layer)) || !W->LoadWaterLayer(Water.GetData(), size_t(Water.Num()), &Err))
			{
				C.Error = FString::Printf(TEXT("%s/%s: %s"), *Dir, *Layer, Err.empty() ? TEXT("missing") : UTF8_TO_TCHAR(Err.c_str()));
				return;
			}
		}
		ff::ParseJson(Utf8, C.Json);
		FFileHelper::LoadFileToArray(C.Splat, *(Dir / TEXT("splat.rgba")));
		if (C.Splat.Num() != W->nx * W->nz * 4)
		{
			C.Splat.Reset();
		}
		C.World = W;
	}

#if PLATFORM_IOS || PLATFORM_ANDROID
	constexpr float kPropCullScale = 0.7f;   // world.json prop_assets cull distances: shorter on phones / tablets
#else
	constexpr float kPropCullScale = 1.0f;
#endif

	inline float SampleH(const ff::WorldDef& W, int32 I, int32 K)
	{
		I = FMath::Clamp(I, 0, W.nx - 1);
		K = FMath::Clamp(K, 0, W.nz - 1);
		const size_t Ix = size_t(K) * size_t(W.nx) + size_t(I);
		return W.heights[Ix] + ((W.edits && !W.edits->delta.empty()) ? W.edits->delta[Ix] : 0.0f);
	}
}

AFourfoldOpenWorld::AFourfoldOpenWorld()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.0f;   // terrain edits show the frame they happen; LODs update every 0.1 s
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
	// CC0 scans (Tools/openworld/prep_vegetation.py): boulders placed by the generator, ground cover streamed at runtime
	for (const TCHAR* B : {TEXT("a"), TEXT("b"), TEXT("c"), TEXT("d"), TEXT("e"), TEXT("f")})
	{
		const FString Name = FString::Printf(TEXT("SM_OW_Boulder_%s"), B);
		PropMeshes.Add(FName(*FString::Printf(TEXT("boulder_%s"), B)),
		               TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(FString::Printf(TEXT("/Game/Fourfold/OpenWorld/Vegetation/%s.%s"), *Name, *Name))));
	}
}

std::shared_ptr<const ff::WorldDef> AFourfoldOpenWorld::LoadWorldDef(FString* OutError)
{
	return LoadWorldDefFrom(TEXT("openworld"), OutError);
}

std::shared_ptr<const ff::WorldDef> AFourfoldOpenWorld::LoadWorldDefFrom(const FString& DataDir, FString* OutError)
{
	FourfoldOW::LoadOnce(DataDir);
	if (OutError)
	{
		*OutError = FourfoldOW::Cache(DataDir).Error;
	}
	return FourfoldOW::Cache(DataDir).World;
}

float AFourfoldOpenWorld::TerrainHeightUE(const FVector& WorldUE) const
{
	return World ? float(World->HeightAt(WorldUE.X / FF::SimToUE, WorldUE.Y / FF::SimToUE)) : 0.0f;
}

void AFourfoldOpenWorld::BeginPlay()
{
	Super::BeginPlay();
	FString Err;
	World = LoadWorldDefFrom(DataDirName, &Err);
	if (!World)
	{
		UE_LOG(LogFourfold, Error, TEXT("AFourfoldOpenWorld: %s"), *Err);
		return;
	}
	Splat = FourfoldOW::Cache(DataDirName).Splat;
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
	const int32 NumC = bBuildTerrain ? FMath::Max(1, Cells / ChunkCells) : 0;   // no procedural terrain: the level has its own
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
	if (bBuildWater)
	{
		BuildWater();
	}
	if (bBuildSolids)
	{
		BuildSolids();
	}
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
		FLinearColor C(1.0f, 0.0f, 0.0f, 0.0f);
		const int32 Ic = FMath::Clamp(I, 0, W.nx - 1);
		const int32 Kc = FMath::Clamp(K, 0, W.nz - 1);
		if (Splat.Num() > 0)
		{
			const uint8* S = &Splat[(size_t(Kc) * size_t(W.nx) + size_t(Ic)) * 4];
			C = FLinearColor(S[0] / 255.0f, S[1] / 255.0f, S[2] / 255.0f, S[3] / 255.0f);
		}
		if (W.edits && !W.edits->delta.empty())
		{
			// torn ground: raised earth shows raw rock, dug ground bare soil (sand layer)
			const float D = W.edits->At(Ic, Kc);
			const float T = FMath::Clamp(FMath::Abs(D) / 0.8f, 0.0f, 1.0f);
			const FLinearColor Torn = D > 0.0f ? FLinearColor(0.0f, 0.85f, 0.15f, 0.0f) : FLinearColor(0.1f, 0.25f, 0.65f, 0.0f);
			C = FMath::Lerp(C, Torn, T);
		}
		Col.Add(C);
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

	if (!bBuildProps)
	{
		return;
	}
	// Trees and rocks: one hierarchical instanced component per mesh, distance culled. prop_assets (optional) maps a mesh
	// key to {asset: object path, cull: metres, shadow: bool} for data sets that bring their own meshes (L_Ruins).
	const ff::Value& Json = FourfoldOW::Cache(DataDirName).Json;
	const ff::Array Props = Json.as_dict().get("props").as_array();
	const ff::Dict Assets = Json.as_dict().get("prop_assets").as_dict();
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
			const ff::Dict Def = Assets.get(D.get("mesh").as_string()).as_dict();
			const TSoftObjectPtr<UStaticMesh>* Soft = PropMeshes.Find(MeshKey);
			UStaticMesh* Mesh = Soft ? Soft->LoadSynchronous() : nullptr;
			if (!Mesh && Def.has("asset"))
			{
				Mesh = LoadObject<UStaticMesh>(nullptr, UTF8_TO_TCHAR(Def.get("asset").as_string().c_str()));
			}
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
			float Cull = bCliff ? CliffCullDistance : (bRock ? RockCullDistance : TreeCullDistance);
			if (Def.has("cull"))
			{
				Cull = float(Def.get("cull").as_float(0.0) * FF::SimToUE) * FourfoldOW::kPropCullScale;
			}
			H->SetCullDistances(int32(Cull * 0.8f), int32(Cull));
			H->SetCastShadow(Def.get("shadow").as_bool(true));
			// wind (world position offset) only near the camera: far trees render without WPO / velocity
			H->WorldPositionOffsetDisableDistance = int32(bCliff || bRock ? 1 : TreeWindDistance);
			H->bCastFarShadow = false;
			H->bAffectDistanceFieldLighting = false;
			H->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			H->RegisterComponent();
			PropComps.Add(H);
			ByKey.Add(Key, H);
			Found = ByKey.Find(Key);
			// Normalise scanned meshes to game sizes: rocks ~3 m across, trees at most 18 m tall.
			const FVector Ext = Mesh->GetBoundingBox().GetSize();
			double Norm = 1.0;
			if (!bNormalizePropScale)
			{
				// sizes come from the data (L_Ruins: the scene's own instance scales)
			}
			else if (bRock)
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
		FPropRef Ref;
		Ref.H = *Found;
		Ref.Index = (*Found)->AddInstance(Xf, false);
		Ref.Base = Xf;
		Ref.Wx = Pos.get(0).as_float();
		Ref.Wz = Pos.get(2).as_float();
		Ref.bTree = MeshKey.ToString().StartsWith(TEXT("tree"));
		const int32 Ci = ChunkIndexAt(Ref.Wx, Ref.Wz);
		if (Chunks.IsValidIndex(Ci) && !bCliff)
		{
			Chunks[Ci].Props.Add(PropRefs.Num());
		}
		PropRefs.Add(Ref);
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
	ApplyTerrainEdits();
	LodTimer -= DeltaSeconds;
	if (LodTimer > 0.0f)
	{
		return;
	}
	LodTimer = 0.1f;
	FVector View = FVector::ZeroVector;
	if (APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		View = Cam->GetCameraLocation();
	}
	UpdateLods(View);
	const uint64 Rev = World->edits ? World->edits->revision : 0;
	if (bBuildCover)
	{
		UpdateCover(View, Rev != CoverEditRevision);
	}
	CoverEditRevision = Rev;
}

namespace FourfoldOW
{
	struct FCoverKind
	{
		const TCHAR* Mesh;
		float PerM2;        // instances per square metre at full grass weight
		float SMin, SMax;
		int32 Kind;         // 0 grass, 1 fern, 2 flower, 3 shrub
		float CullM;
	};
	static const FCoverKind kCover[] = {
		{TEXT("SM_OW_GrassLawn_a"), 0.55f, 1.5f, 2.3f, 0, 45.0f}, {TEXT("SM_OW_GrassLawn_b"), 0.40f, 1.5f, 2.3f, 0, 45.0f},
		{TEXT("SM_OW_GrassTuft_a"), 0.28f, 1.4f, 2.2f, 0, 45.0f}, {TEXT("SM_OW_GrassTuft_b"), 0.22f, 1.4f, 2.2f, 0, 45.0f},
		{TEXT("SM_OW_Fern_a"), 0.010f, 0.8f, 1.3f, 1, 55.0f}, {TEXT("SM_OW_Fern_b"), 0.008f, 0.8f, 1.3f, 1, 55.0f},
		{TEXT("SM_OW_Fern_c"), 0.010f, 0.9f, 1.4f, 1, 55.0f}, {TEXT("SM_OW_Flower_a"), 0.018f, 1.0f, 1.7f, 2, 40.0f},
		{TEXT("SM_OW_Flower_b"), 0.018f, 1.0f, 1.7f, 2, 40.0f}, {TEXT("SM_OW_Dandelion_a"), 0.012f, 1.0f, 1.5f, 2, 40.0f},
		{TEXT("SM_OW_Dandelion_b"), 0.012f, 1.0f, 1.5f, 2, 40.0f}, {TEXT("SM_OW_Shrub_a"), 0.004f, 2.4f, 3.6f, 3, 64.0f},
		{TEXT("SM_OW_Shrub_b"), 0.004f, 2.4f, 3.6f, 3, 64.0f}, {TEXT("SM_OW_Shrub_c"), 0.004f, 2.4f, 3.6f, 3, 64.0f},
		{TEXT("SM_OW_Shrub_d"), 0.004f, 2.4f, 3.6f, 3, 64.0f},
	};
	constexpr double kCoverCell = 16.0;   // m
}

void AFourfoldOpenWorld::UpdateCover(const FVector& ViewUE, bool bForce)
{
	using namespace FourfoldOW;
	const double MoveM = FVector::Dist2D(ViewUE, CoverCenter) / FF::SimToUE;
	if (!bForce && MoveM < 14.0)
	{
		return;
	}
	if (CoverComps.Num() == 0)
	{
		for (const FCoverKind& K : kCover)
		{
			const FString Path = FString::Printf(TEXT("/Game/Fourfold/OpenWorld/Vegetation/%s.%s"), K.Mesh, K.Mesh);
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
			UHierarchicalInstancedStaticMeshComponent* H = nullptr;
			if (Mesh)
			{
				H = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
				H->SetupAttachment(RootComponent);
				H->SetMobility(EComponentMobility::Static);
				H->SetStaticMesh(Mesh);
				H->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				H->SetCastShadow(K.Kind == 3);
				H->bAffectDistanceFieldLighting = false;
				H->WorldPositionOffsetDisableDistance = int32(K.CullM * 60.0f);
				H->SetCullDistances(int32(K.CullM * 75.0f), int32(K.CullM * 100.0f));
				H->RegisterComponent();
			}
			CoverComps.Add(H);
		}
	}
	CoverCenter = ViewUE;
	const double Cx = ViewUE.X / FF::SimToUE;   // world x
	const double Cz = ViewUE.Y / FF::SimToUE;   // world z
#if PLATFORM_IOS || PLATFORM_ANDROID
	const double Radius = FMath::Min(double(CoverRadius) / FF::SimToUE, 44.0);
	const double Density = CoverDensity * 0.45;
#else
	const double Radius = double(CoverRadius) / FF::SimToUE;
	const double Density = CoverDensity;
#endif
	TArray<TArray<FTransform>> Out;
	Out.SetNum(UE_ARRAY_COUNT(kCover));
	const ff::WorldDef& W = *World;
	const int32 C0x = FMath::FloorToInt((Cx - Radius) / kCoverCell), C1x = FMath::FloorToInt((Cx + Radius) / kCoverCell);
	const int32 C0z = FMath::FloorToInt((Cz - Radius) / kCoverCell), C1z = FMath::FloorToInt((Cz + Radius) / kCoverCell);
	for (int32 CzI = C0z; CzI <= C1z; ++CzI)
	{
		for (int32 CxI = C0x; CxI <= C1x; ++CxI)
		{
			const double Ox = CxI * kCoverCell, Oz = CzI * kCoverCell;
			if (FMath::Square(Ox + kCoverCell * 0.5 - Cx) + FMath::Square(Oz + kCoverCell * 0.5 - Cz) > FMath::Square(Radius + kCoverCell * 0.7))
			{
				continue;
			}
			FRandomStream R(int32(uint32(CxI) * 73856093u ^ uint32(CzI) * 19349663u));   // same cell, same plants
			for (int32 Ki = 0; Ki < int32(UE_ARRAY_COUNT(kCover)); ++Ki)
			{
				const FCoverKind& K = kCover[Ki];
				const int32 N = FMath::RoundToInt(K.PerM2 * kCoverCell * kCoverCell * Density);
				for (int32 n = 0; n < N; ++n)
				{
					const double X = Ox + R.FRand() * kCoverCell, Z = Oz + R.FRand() * kCoverCell;
					const float Roll = R.FRand(), Yaw = R.FRand() * 360.0f, Sc = FMath::Lerp(K.SMin, K.SMax, R.FRand());
					const int32 I = FMath::Clamp(int32((X - W.x0) / W.cell + 0.5), 0, W.nx - 1);
					const int32 Kk = FMath::Clamp(int32((Z - W.z0) / W.cell + 0.5), 0, W.nz - 1);
					float Grass = 1.0f, Sand = 0.0f;
					if (Splat.Num() > 0)
					{
						const uint8* S = &Splat[(size_t(Kk) * size_t(W.nx) + size_t(I)) * 4];
						Grass = S[0] / 255.0f;
						Sand = S[2] / 255.0f;
					}
					const float Torn = W.edits ? FMath::Abs(W.edits->At(I, Kk)) : 0.0f;
					if (Roll > Grass * Grass || Sand > 0.45f || Torn > 0.25f || W.IsWater(X, Z))
					{
						continue;
					}
					const double H = W.HeightAt(X, Z);
					const double Slope = FMath::Abs(W.HeightAt(X + 1.0, Z) - H) + FMath::Abs(W.HeightAt(X, Z + 1.0) - H);
					if (Slope > (K.Kind == 0 ? 0.9 : 0.6))
					{
						continue;
					}
					Out[Ki].Add(FTransform(FRotator(0.0, Yaw, 0.0), FF::WorldToUE(X, H - 0.03, Z), FVector(Sc)));
				}
			}
		}
	}
	int32 Total = 0;
	for (int32 Ki = 0; Ki < CoverComps.Num(); ++Ki)
	{
		if (UHierarchicalInstancedStaticMeshComponent* H = CoverComps[Ki])
		{
			H->ClearInstances();
			H->AddInstances(Out[Ki], false, true, false);
			Total += Out[Ki].Num();
		}
	}
	UE_LOG(LogFourfold, Verbose, TEXT("Ground cover: %d instances around (%.0f, %.0f)"), Total, Cx, Cz);
}

int32 AFourfoldOpenWorld::ChunkIndexAt(double Wx, double Wz) const
{
	if (!World)
	{
		return INDEX_NONE;
	}
	const int32 NumC = FMath::Max(1, (World->nx - 1) / ChunkCells);
	const int32 Cx = FMath::Clamp(int32((Wx - World->x0) / (World->cell * ChunkCells)), 0, NumC - 1);
	const int32 Cz = FMath::Clamp(int32((Wz - World->z0) / (World->cell * ChunkCells)), 0, NumC - 1);
	return Cz * NumC + Cx;
}

void AFourfoldOpenWorld::ApplyTerrainEdits()
{
	// Render the session's live world (its own copy carries the edits) once roaming starts.
	if (UFourfoldSimSubsystem* Sim = UFourfoldSimSubsystem::Get(this); Sim && Sim->IsRoaming())
	{
		std::shared_ptr<const ff::WorldDef> Live = Sim->GetSession().RoamWorld();
		if (Live && Live != World && Live->nx == World->nx && Live->nz == World->nz)
		{
			World = Live;
			SeenEditRevision = 0;
			for (FChunk& C : Chunks)
			{
				C.EditRev = 0;
			}
		}
	}
	if (!World->edits || World->edits->revision == SeenEditRevision)
	{
		return;
	}
	SeenEditRevision = World->edits->revision;
	const ff::TerrainEdits& E = *World->edits;
	for (FChunk& C : Chunks)
	{
		const int32 Idx = C.Cz * E.chunks_x + C.Cx;
		if (C.Cx >= E.chunks_x || C.Cz >= E.chunks_z || Idx < 0 || size_t(Idx) >= E.chunk_rev.size())
		{
			continue;
		}
		const uint32 Rev = E.chunk_rev[size_t(Idx)];
		if (Rev == C.EditRev)
		{
			continue;
		}
		C.EditRev = Rev;
		BuildChunkSection(C, 0, 4, false);
		if (C.bNearBuilt)
		{
			BuildChunkSection(C, 1, 1, true);
		}
		C.Mesh->SetMeshSectionVisible(0, !C.bNear || !C.bNearBuilt);
		if (C.bNearBuilt)
		{
			C.Mesh->SetMeshSectionVisible(1, C.bNear);
		}
		C.Bounds = C.Bounds.ExpandBy(FVector(0.0, 0.0, 1600.0));
		for (int32 Pi : C.Props)
		{
			UpdateProp(PropRefs[Pi]);
		}
	}
}

void AFourfoldOpenWorld::UpdateProp(FPropRef& P)
{
	if (P.bDown || !P.H)
	{
		return;
	}
	const double D = World->HeightAt(P.Wx, P.Wz) - World->BaseHeightAt(P.Wx, P.Wz);
	if (FMath::Abs(D) < 0.03)
	{
		return;
	}
	// Tilt of the moved ground under the prop (edit gradient over 1.5 m).
	const auto Delta = [this](double X, double Z) { return World->HeightAt(X, Z) - World->BaseHeightAt(X, Z); };
	const double Gx = (Delta(P.Wx + 1.5, P.Wz) - Delta(P.Wx - 1.5, P.Wz)) / 3.0;
	const double Gz = (Delta(P.Wx, P.Wz + 1.5) - Delta(P.Wx, P.Wz - 1.5)) / 3.0;
	FTransform Xf = P.Base;
	Xf.AddToTranslation(FVector(0.0, 0.0, D * FF::SimToUE));
	if (P.bTree && (FMath::Abs(D) > 1.4 || FMath::Sqrt(Gx * Gx + Gz * Gz) > 0.55))
	{
		// Uprooted: falls away from the rising side, roots sunk.
		const FVector Fall = FVector(-Gx, -Gz, 0.0).GetSafeNormal(1e-4, FVector(1.0, 0.0, 0.0));
		const FVector Axis = FVector::CrossProduct(FVector::UpVector, Fall).GetSafeNormal();
		const FQuat Tip(Axis, FMath::DegreesToRadians(82.0));
		Xf.SetRotation(Tip * Xf.GetRotation());
		Xf.AddToTranslation(FVector(0.0, 0.0, -60.0));
		P.bDown = true;
	}
	P.H->UpdateInstanceTransform(P.Index, Xf, true, true, true);
}
