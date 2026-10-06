// FourfoldFX - sim-space logic output -> Unreal (centimetres, left-handed, Z up). The only place the FX module
// converts; it builds on Fourfold/Public/FourfoldCoords.h (UE(X, Y, Z) = 100 * (sim.x, sim.z, sim.y)).
//
// Meshes: positions x 100 and Y/Z swapped; normals / tangents swapped; every triangle's winding is reversed (the
// swap is a reflection, so without it the front faces would turn inward). UV0..UV2 are passed through unchanged
// (the vector data some materials keep in UVs is documented in UE local axis order: see FxMesh.h); UV3 = (vertex
// alpha, 0) for the materials (spec.py source "vca").
// Transforms: the logic's basis columns (images of the local x / y / z axes in sim space) become UE axes
// X = swz(B.x), Y = swz(B.z), Z = swz(B.y) - the same reflection on both sides keeps the determinant positive.
// Owner: stream `fx`.
#pragma once

#include "CoreMinimal.h"
#include "FourfoldCoords.h"
#include "Logic/FxBase.h"
#include "Logic/FxMesh.h"
#include "ProceduralMeshComponent.h"

namespace FFFx
{
	FORCEINLINE FVector Swz(const ffx::Vec3& V) { return FVector(double(V.x), double(V.z), double(V.y)); }

	FORCEINLINE FLinearColor ToLinear(const ffx::Color& C) { return FLinearColor(C.r, C.g, C.b, C.a); }

	/** Component transform for a logic Xform (rotation + per-axis scale; the bases the logic emits are orthogonal). */
	inline FTransform ToTransform(const ffx::Xform& X)
	{
		const FVector Ax = Swz(X.basis.x);
		const FVector Ay = Swz(X.basis.z);
		const FVector Az = Swz(X.basis.y);
		const FVector Scale(FMath::Max(Ax.Size(), 1e-4), FMath::Max(Ay.Size(), 1e-4), FMath::Max(Az.Size(), 1e-4));
		const FMatrix M(Ax / Scale.X, Ay / Scale.Y, Az / Scale.Z, FVector::ZeroVector);   // rows = axes
		FQuat Q(M);
		Q.Normalize();
		return FTransform(Q, FF::ToUE(X.pos), Scale);
	}

	/** Rotation / scale only (attached items: location comes from the bone). */
	inline void ToRotationScale(const ffx::Basis& B, FQuat& OutRot, FVector& OutScale)
	{
		ffx::Xform X;
		X.basis = B;
		const FTransform T = ToTransform(X);
		OutRot = T.GetRotation();
		OutScale = T.GetScale3D();
	}

	/** Reusable conversion buffers for one procedural mesh upload. */
	struct FMeshBuffers
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UV0, UV1, UV2, UV3;
		TArray<FLinearColor> Colors;
		TArray<FProcMeshTangent> Tangents;

		void Convert(const ffx::MeshData& M, bool bWithIndices)
		{
			const int32 N = M.NumVerts();
			Vertices.SetNumUninitialized(N, EAllowShrinking::No);
			Normals.SetNumUninitialized(N, EAllowShrinking::No);
			UV0.SetNumUninitialized(N, EAllowShrinking::No);
			UV1.SetNumUninitialized(N, EAllowShrinking::No);
			UV2.SetNumUninitialized(N, EAllowShrinking::No);
			Colors.SetNumUninitialized(N, EAllowShrinking::No);
			Tangents.SetNum(N, EAllowShrinking::No);
			// UV3.x = vertex alpha (the materials read alpha here: 16-bit and independent of the vertex colour pins)
			UV3.SetNumUninitialized(N, EAllowShrinking::No);
			for (int32 i = 0; i < N; ++i)
			{
				const size_t s = size_t(i);
				Vertices[i] = Swz(M.pos[s]) * FF::SimToUE;
				Normals[i] = Swz(M.nrm[s]);
				UV0[i] = FVector2D(M.uv0[s].x, M.uv0[s].y);
				UV1[i] = FVector2D(M.uv1[s].x, M.uv1[s].y);
				UV2[i] = FVector2D(M.uv2[s].x, M.uv2[s].y);
				Colors[i] = ToLinear(M.col[s]);
				UV3[i] = FVector2D(M.col[s].a, 0.0);
				Tangents[i] = FProcMeshTangent(Swz(M.tan[s]), false);
			}
			if (bWithIndices)
			{
				const int32 NI = int32(M.idx.size());
				Triangles.SetNumUninitialized(NI, EAllowShrinking::No);
				for (int32 i = 0; i + 2 < NI; i += 3)
				{
					// reflection: reverse each triangle so the front faces stay outward in Unreal
					Triangles[i] = M.idx[size_t(i)];
					Triangles[i + 1] = M.idx[size_t(i + 2)];
					Triangles[i + 2] = M.idx[size_t(i + 1)];
				}
			}
		}
	};
}
