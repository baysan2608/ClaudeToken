// Fourfold - sim <-> Unreal coordinate conversion (header-only).
// FROZEN CONTRACT (architect). Every module (Fourfold, FourfoldFX, FourfoldAudio) converts through these functions.
//
// Sim space (Godot): metres, right-handed, +Y up; an actor's forward is (sin f, 0, cos f) for facing f (radians).
// Unreal: centimetres, left-handed, +Z up, yaw in degrees (forward = (cos yaw, sin yaw, 0)).
// Mapping: UE(X, Y, Z) = 100 * (sim.x, sim.z, sim.y). Swapping Y/Z is the reflection that turns the right-handed sim
// into Unreal's left-handed frame, so the arena is NOT mirrored: looking along sim -Z, sim +X is to the right; in UE
// looking along -Y, +X is to the right as well. Yaw: UE yaw = 90 - degrees(f).
#pragma once

#include "CoreMinimal.h"
#include "ff/Math.h"

namespace FF
{
	inline constexpr double SimToUE = 100.0;   // metres -> centimetres

	// (additive, open world) Unreal position of sim (0,0,0): zero in the arenas; in the open world the sim runs in a
	// bubble that moves through the valley and UFourfoldSimSubsystem keeps this at the bubble origin. ToUE / ToSim
	// are POSITION conversions and include it; offsets and directions go through DirToUE / DirToSim.
	extern FOURFOLD_API FVector GSimOriginUE;

	FORCEINLINE FVector ToUE(const ff::Vec3& P)
	{
		return GSimOriginUE + FVector(double(P.x) * SimToUE, double(P.z) * SimToUE, double(P.y) * SimToUE);
	}

	FORCEINLINE FVector DirToUE(const ff::Vec3& D)   // directions / velocities in m/s -> unit-less swap (scale yourself)
	{
		return FVector(double(D.x), double(D.z), double(D.y));
	}

	FORCEINLINE ff::Vec3 ToSim(const FVector& P)
	{
		const FVector L = P - GSimOriginUE;
		return ff::Vec3(float(L.X / SimToUE), float(L.Z / SimToUE), float(L.Y / SimToUE));
	}

	// World (open-world) metres -> Unreal, without the bubble origin (terrain, props, encounter sites).
	FORCEINLINE FVector WorldToUE(double X, double Y, double Z)
	{
		return FVector(X * SimToUE, Z * SimToUE, Y * SimToUE);
	}

	FORCEINLINE ff::Vec3 DirToSim(const FVector& D)
	{
		return ff::Vec3(float(D.X), float(D.Z), float(D.Y));
	}

	// Sim facing / camera yaw (radians) -> Unreal yaw (degrees) and back.
	FORCEINLINE double SimYawToUEYawDeg(float SimYaw)
	{
		return 90.0 - FMath::RadiansToDegrees(double(SimYaw));
	}

	FORCEINLINE float UEYawDegToSimYaw(double UEYawDeg)
	{
		return float(FMath::DegreesToRadians(90.0 - UEYawDeg));
	}

	FORCEINLINE FRotator FacingToRotator(float SimFacing)
	{
		return FRotator(0.0, SimYawToUEYawDeg(SimFacing), 0.0);
	}

	// Sim wall yaw (MatBody.wall_yaw, rotation about +Y) -> Unreal rotator. Same convention as facing.
	FORCEINLINE FRotator WallYawToRotator(float SimWallYaw)
	{
		return FRotator(0.0, SimYawToUEYawDeg(SimWallYaw), 0.0);
	}

	// Sim box half extents (x, y = up, z) -> Unreal half extents in cm (X, Y, Z).
	FORCEINLINE FVector HalfExtentsToUE(const ff::Vec3& H)
	{
		return FVector(double(H.x) * SimToUE, double(H.z) * SimToUE, double(H.y) * SimToUE);
	}
}
