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

	FORCEINLINE FVector ToUE(const ff::Vec3& P)
	{
		return FVector(double(P.x) * SimToUE, double(P.z) * SimToUE, double(P.y) * SimToUE);
	}

	FORCEINLINE FVector DirToUE(const ff::Vec3& D)   // directions / velocities in m/s -> unit-less swap (scale yourself)
	{
		return FVector(double(D.x), double(D.z), double(D.y));
	}

	FORCEINLINE ff::Vec3 ToSim(const FVector& P)
	{
		return ff::Vec3(float(P.X / SimToUE), float(P.Z / SimToUE), float(P.Y / SimToUE));
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
