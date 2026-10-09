// Fourfold - feel director (see FourfoldFeel.h).
#include "FourfoldFeel.h"

#include "FourfoldCameraRig.h"
#include "FourfoldSettings.h"
#include "FourfoldSimSubsystem.h"
#include "UI/SFourfoldHud.h"

#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"

namespace
{
	EMobileHapticsType HapticEnum(const char* Name)
	{
		const FString N(UTF8_TO_TCHAR(Name));
		if (N == TEXT("ImpactMedium")) return EMobileHapticsType::ImpactMedium;
		if (N == TEXT("ImpactHeavy")) return EMobileHapticsType::ImpactHeavy;
		if (N == TEXT("FeedbackSuccess")) return EMobileHapticsType::FeedbackSuccess;
		if (N == TEXT("FeedbackWarning")) return EMobileHapticsType::FeedbackWarning;
		if (N == TEXT("FeedbackError")) return EMobileHapticsType::FeedbackError;
		if (N == TEXT("SelectionChanged")) return EMobileHapticsType::SelectionChanged;
		return EMobileHapticsType::ImpactLight;
	}
}

void FFourfoldFeel::OnSettings(const FFourfoldSettings& Settings, UFourfoldSimSubsystem& Sim, AFourfoldCameraRig* Rig)
{
	Sim.SetReducedMotion(Settings.bReducedMotion);
	if (Rig)
	{
		Rig->Logic.shake_scale = Settings.ScreenShake;
		Rig->Logic.reduced_motion = Settings.bReducedMotion;
	}
	if (!Settings.bHaptics)
	{
		ReleaseHaptics();
	}
}

void FFourfoldFeel::ReleaseHaptics()
{
	if (bHapticsPrepared)
	{
		FPlatformMisc::ReleaseMobileHaptics();
		bHapticsPrepared = false;
	}
}

void FFourfoldFeel::Haptic(const std::string& Kind, bool bEnabled)
{
	if (!Gate.Allow(FPlatformTime::Seconds(), bEnabled))
	{
		return;
	}
	// Prepare (spins the Taptic engine up for that type) then trigger; released when the app backgrounds.
	FPlatformMisc::PrepareMobileHaptics(HapticEnum(ffg::HapticTypeFor(Kind)));
	FPlatformMisc::TriggerMobileHaptics();
	bHapticsPrepared = true;
}

int32 FFourfoldFeel::Apply(const FFourfoldFrame& Frame, const FFourfoldSettings& Settings, UFourfoldSimSubsystem& Sim, AFourfoldCameraRig* Rig,
                           SFourfoldHud* Hud)
{
	if (!Frame.Events || !Frame.Curr || Frame.Events->empty())
	{
		return 0;
	}
	ffg::FeelOptions Opt;
	Opt.player_id = Frame.Curr->player_id;
	Opt.flashes = Settings.Flashes;
	Opt.slowmo_assist = Settings.bSlowmoAssist;
	Out.Clear();
	ffg::HandleFeelEvents(*Frame.Events, *Frame.Curr, Opt, Out);

	if (Out.hitstop > 0)
	{
		Sim.RequestHitStop(Out.hitstop);
	}
	if (Out.slowmo > 0.0f)
	{
		Sim.RequestSlowmo(Out.slowmo);
	}
	// Cinematic slow motion on big counters / a KO: never with reduced motion, in a Lab freeze or at a Lab time scale
	// (the Lab is for reading frames), at most once every 3 s (a KO always).
	bool bCinematic = false;
	if (Out.cinematic > 0.0f && !Settings.bReducedMotion && Sim.HasScenario())
	{
		const ff::LabState& Lab = Sim.GetSession().Lab();
		if (!Lab.frozen && FMath::IsNearlyEqual(Lab.time_scale, 1.0f, 0.01f) && CineGate.Allow(FPlatformTime::Seconds(), Out.cinematic_ko))
		{
			Sim.RequestSlowmo(ffg::HitStop::EncodeCinematic(Out.cinematic));
			bCinematic = true;
		}
	}
	if (Rig)
	{
		for (const ffg::FeelShake& S : Out.shakes)
		{
			if (S.has_pos)
			{
				Rig->Logic.ShakeAt(S.amount, S.pos, S.decay_s, S.player, S.roll);
			}
			else
			{
				Rig->Logic.Shake(S.amount, S.decay_s);
			}
		}
		for (const ffg::FeelKick& K : Out.kicks)
		{
			Rig->Logic.Kick(K.dir, K.amount);
		}
		if (FMath::Abs(Out.fov_punch) > 0.01f)
		{
			Rig->Logic.FovPunch(Out.fov_punch);
		}
		if (Out.zoom)
		{
			Rig->Logic.ZoomTo(Out.zoom_at);
		}
		if (bCinematic)
		{
			Rig->Logic.Cinematic(Out.cinematic_at);
		}
	}
	for (const std::string& K : Out.haptics)
	{
		Haptic(K, Settings.bHaptics);
	}
	int32 Toasts = 0;
	if (Hud)
	{
		if (!Out.flash.empty() && Settings.Flashes > 0.0f)
		{
			Hud->Flash(FString(UTF8_TO_TCHAR(Out.flash.c_str())));
		}
		for (const std::string& T : Out.toasts)
		{
			Hud->Toast(FString(UTF8_TO_TCHAR(T.c_str())));
			++Toasts;
		}
	}
	return Toasts;
}
