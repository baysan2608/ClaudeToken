// Fourfold - feel director (ARCHITECTURE §7.5, MOVESET §10.2; port of the feel half of fx_director.gd). Each frame it
// maps the frame's sim / session events through the logic island (ffg::HandleFeelEvents) and applies the result:
// hit-stop, the slow-motion assist and the cinematic slow motion of big counters / KOs through the sim subsystem
// (global time dilation 0.05 for N/60 s of real time, <= 20/60 s frozen per second, reduced motion caps one request at
// 3/60 s; cinematic 0.25 with a 3 s cooldown), camera trauma shake / kick / FOV punch / cinematic dolly / 3 % zoom on the rig,
// iOS haptics (FPlatformMisc mobile haptics, >= 60 ms apart, off in settings = never), HUD flashes (scaled by the
// Flashes setting) and toasts.
#pragma once

#include "CoreMinimal.h"
#include "Logic/FFGFeel.h"

class AFourfoldCameraRig;
class SFourfoldHud;
class UFourfoldSimSubsystem;
struct FFourfoldFrame;
struct FFourfoldSettings;

class FFourfoldFeel
{
public:
	/** Inside OnFrame. Hud may be null (no UI yet). Returns the number of toasts shown (UI sound cue). */
	int32 Apply(const FFourfoldFrame& Frame, const FFourfoldSettings& Settings, UFourfoldSimSubsystem& Sim, AFourfoldCameraRig* Rig, SFourfoldHud* Hud);
	/** Settings changed: reduced-motion cap and shake scale. */
	void OnSettings(const FFourfoldSettings& Settings, UFourfoldSimSubsystem& Sim, AFourfoldCameraRig* Rig);
	/** App going to the background: let the haptic engine go. */
	void ReleaseHaptics();
	/** One semantic haptic ("light", "hit", ...) through the 60 ms gate (also used by the touch controls). */
	void Haptic(const std::string& Kind, bool bEnabled);

private:
	ffg::FeelOutput Out;
	ffg::HapticGate Gate;
	ffg::CinematicGate CineGate;
	bool bHapticsPrepared = false;
};
