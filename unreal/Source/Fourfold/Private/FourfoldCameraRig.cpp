// Fourfold - third-person camera rig (see FourfoldCameraRig.h).
#include "FourfoldCameraRig.h"

#include "FourfoldCoords.h"

#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"

AFourfoldCameraRig::AFourfoldCameraRig()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->bConstrainAspectRatio = false;
	Camera->SetFieldOfView(80.0f);
}

void AFourfoldCameraRig::ResetForScenario(const ff::ArenaView& InArena, const ff::Vec3& PlayerPos, const ff::Vec3& LookAt)
{
	Arena.Set(InArena);
	Logic.arena = &Arena;
	Logic.SnapTo(PlayerPos, LookAt);
	UpdateRig(0.0f, 0.0f, PlayerPos, nullptr, nullptr);
}

void AFourfoldCameraRig::UpdateRig(float GameDt, float RealDt, const ff::Vec3& PlayerPos, const ff::Vec3* Target, const ff::Vec3* Threat)
{
	// Aspect ratio of the game viewport (the logic frames the rival with the horizontal FOV).
	double Aspect = 16.0 / 9.0;
	if (UWorld* World = GetWorld())
	{
		if (UGameViewportClient* VC = World->GetGameViewport())
		{
			FVector2D Size;
			VC->GetViewportSize(Size);
			if (Size.Y > 1.0)
			{
				Aspect = Size.X / Size.Y;
			}
		}
	}
	Logic.aspect = float(Aspect);
	const ffg::CameraOutput Out = Logic.Update(GameDt, RealDt, PlayerPos, Target, Threat);
	const FVector Pos = FF::ToUE(Out.pos);
	const FVector Look = FF::ToUE(Out.look);
	FVector Dir = Look - Pos;
	if (Dir.SizeSquared() < 1e-6)
	{
		Dir = FVector(1.0, 0.0, 0.0);
	}
	SetActorLocationAndRotation(Pos, Dir.Rotation());
	// The logic's FOV is vertical (as the Godot camera); Unreal's camera FOV is horizontal.
	const double VFov = FMath::DegreesToRadians(double(Out.fov));
	const double HFov = 2.0 * FMath::Atan(FMath::Tan(VFov * 0.5) * Aspect);
	Camera->SetFieldOfView(float(FMath::Clamp(FMath::RadiansToDegrees(HFov), 30.0, 140.0)));
	SeeThrough.Reset();
	for (const std::string& Name : Out.see_through)
	{
		SeeThrough.Add(FString(UTF8_TO_TCHAR(Name.c_str())));
	}
}
