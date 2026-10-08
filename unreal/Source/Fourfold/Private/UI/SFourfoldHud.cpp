// Fourfold - in-play HUD (see SFourfoldHud.h). Sizes follow hud.gd: Godot layout units (a 720-unit-tall viewport)
// scaled to Slate local units, with millimetre floors on phones / tablets so text never drops below ~2 mm.
#include "UI/SFourfoldHud.h"

#include "UI/FourfoldUi.h"

#include "HAL/PlatformTime.h"
#include "Rendering/DrawElements.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float kTextMm = 2.0f;    // stat text, rival name
	constexpr float kLineMm = 2.2f;    // objective / challenge lines
	constexpr float kToastMm = 3.0f;
	constexpr float kBarMm = 0.6f;
	constexpr float kHotRockC = 300.0f;
	constexpr float kCalloutLife = 1.25f;   // s an outcome callout stays up

	FString HS(const std::string& Str) { return FString(UTF8_TO_TCHAR(Str.c_str())); }
	FString HS(std::string_view Sv) { return FString(UTF8_TO_TCHAR(std::string(Sv).c_str())); }
	FLinearColor C4(float R, float G, float B, float A) { return FFUi::SRGB(R, G, B, FMath::Clamp(A, 0.0f, 1.0f)); }
	FLinearColor WithA(const FLinearColor& C, float A) { return FLinearColor(C.R, C.G, C.B, FMath::Clamp(A, 0.0f, 1.0f)); }

	/** The input that triggers a slot on the device in use (desktop chords: K + J push, K + N sink). */
	FString SlotKey(const FString& Device, const std::string& Slot)
	{
		static const char* const kSlots[8] = {"guard", "push", "sink", "tech", "thrust", "ground", "sweep", "strike"};
		static const TCHAR* const kTouchKeys[8] = {TEXT("GUARD"), TEXT("GUARD ↑"), TEXT("GUARD ↓"), TEXT("TECH"),
		                                           TEXT("ATTACK ↑"), TEXT("ATTACK ↓"), TEXT("ATTACK ↔"), TEXT("ATTACK")};
		static const TCHAR* const kDeskKeys[8] = {TEXT("K"), TEXT("K + J"), TEXT("K + N"), TEXT("L"), TEXT("U"), TEXT("N"), TEXT("H"), TEXT("J")};
		static const TCHAR* const kPadKeys[8] = {TEXT("RB"), TEXT("RB + X"), TEXT("RB + LT"), TEXT("RT"), TEXT("Y"), TEXT("LT"), TEXT("B"), TEXT("X")};
		int32 I = 0;
		while (I < 7 && Slot != kSlots[I])
		{
			++I;
		}
		const TCHAR* const* Keys = Device == TEXT("gamepad") ? kPadKeys : Device == TEXT("keyboard") ? kDeskKeys : kTouchKeys;
		return Keys[I];
	}

	struct FStatusStyle
	{
		const TCHAR* Code;
		float R, G, B;
	};
	const FStatusStyle* FindStatusStyle(const std::string& Name)
	{
		static const TMap<FString, FStatusStyle> Styles = {
			{TEXT("wet"), {TEXT("WT"), 0.45f, 0.75f, 1.0f}},        {TEXT("burning"), {TEXT("BN"), 1.0f, 0.5f, 0.25f}},
			{TEXT("chilled"), {TEXT("CH"), 0.7f, 0.9f, 1.0f}},      {TEXT("frozen"), {TEXT("FZ"), 0.6f, 0.85f, 1.0f}},
			{TEXT("rooted"), {TEXT("RT"), 0.5f, 0.8f, 0.4f}},       {TEXT("slowed"), {TEXT("SL"), 0.8f, 0.8f, 0.6f}},
			{TEXT("muddy"), {TEXT("MD"), 0.65f, 0.5f, 0.35f}},      {TEXT("slick"), {TEXT("SK"), 0.6f, 0.9f, 0.95f}},
			{TEXT("blinded"), {TEXT("BL"), 0.85f, 0.85f, 0.6f}},    {TEXT("concealed"), {TEXT("CN"), 0.7f, 0.75f, 0.85f}},
			{TEXT("deafened"), {TEXT("DF"), 0.8f, 0.7f, 0.9f}},     {TEXT("shocked"), {TEXT("SH"), 0.95f, 0.9f, 0.4f}},
			{TEXT("anchored"), {TEXT("AN"), 0.8f, 0.7f, 0.5f}},     {TEXT("armored"), {TEXT("AR"), 0.75f, 0.78f, 0.85f}},
			{TEXT("levitating"), {TEXT("LV"), 0.7f, 0.95f, 0.9f}},  {TEXT("charged"), {TEXT("CG"), 1.0f, 0.9f, 0.5f}},
		};
		return Styles.Find(HS(Name));
	}
}

/** Per-paint sizing (hud.gd _draw locals). */
struct SFourfoldHud::FPaintCtx
{
	FFUi::FPainter P;
	FVector2D Size;          // local viewport size
	FVector2D SrPos, SrSize; // safe rect
	float U = 1.0f;          // local units per scaled Godot unit
	float FloorPpm = 0.0f;   // mm floors (phones / tablets only, as the Godot build)
	float BarH = 4.0f;
	float FsStat = 11.0f, FsName = 12.0f, FsLine = 13.0f;
	float GrowBar = 0.0f, GrowStat = 0.0f, GrowLine = 0.0f;
	float PixelToLocal = 1.0f;

	float Fs(float Base, float Mm) const { return FMath::Max(Base * U, Mm * FloorPpm); }
	FVector2D SrEnd() const { return SrPos + SrSize; }
	FVector2D SrCenter() const { return SrPos + SrSize * 0.5; }

	/** Godot draw_string: text on a baseline; Align -1 left at X, 0 centred in [X, X + W]. */
	void TextBase(const FString& S, double X, double BaseY, float Px, const FLinearColor& Col, int32 Align = -1, double W = 0.0, float OutlineA = 0.0f) const
	{
		const FSlateFontInfo F = FFUi::Font(Px, false, OutlineA > 0.0f ? FMath::Max(1, FMath::RoundToInt(Px / 6.0f)) : 0, FLinearColor(0, 0, 0, OutlineA));
		double Tx = X;
		if (Align == 0)
		{
			Tx = X + (W - FFUi::Measure(S, F).X) * 0.5;
		}
		P.Text(S, FVector2D(Tx, BaseY - Px * 0.93), F, Col);
	}
	void Bar(const FVector2D& Pos, float W, float Frac, const FLinearColor& Col, float A) const
	{
		P.Rect(Pos, FVector2D(W, BarH), FLinearColor(0, 0, 0, 0.35f * A), BarH * 0.5f);
		P.Rect(Pos, FVector2D(W * FMath::Clamp(Frac, 0.0f, 1.0f), BarH), WithA(Col, A), BarH * 0.5f);
	}
};

void SFourfoldHud::Construct(const FArguments& InArgs)
{
	ForceVolatile(true);
	SetVisibility(EVisibility::HitTestInvisible);
}

void SFourfoldHud::Toast(const FString& Text, const FString& Kind)
{
	ToastText = Text;
	ToastT = Kind == TEXT("mastery") ? 2.6f : 1.8f;
}

void SFourfoldHud::Flash(const FString& Kind)
{
	FlashKind = Kind;
	FlashT = 0.18f;
}

void SFourfoldHud::UpdateFrame(const ff::HudModel& InHud, const ff::Snapshot& Snap, const FFrameInput& In, FProjectFn Project)
{
	Hud = InHud;
	Input = In;
	const float Dt = FMath::Clamp(In.RealDt, 0.0f, 0.1f);
	ToastT = FMath::Max(0.0f, ToastT - Dt);
	FlashT = FMath::Max(0.0f, FlashT - Dt);

	const ff::ActorView* Player = Snap.FindActor(Hud.player_id);
	bHasPlayer = Hud.valid && Player != nullptr;
	if (Player)
	{
		const bool bBusy = Player->action.active || Player->stun > 0.0f || Player->health < LastHealth - 0.01f || Player->focus < 99.0f ||
		                   Player->heat_reserve > 0.0f;
		LastHealth = Player->health;
		Calm = bBusy ? 0.0f : Calm + Dt;
		ChargeElement = Player->action.active ? Player->action.element : Hud.element;
	}
	Alpha = FMath::Lerp(Alpha, Calm > 2.5f ? 0.35f : 1.0f, 1.0f - FMath::Exp(-6.0f * Dt));

	// Status fill references (longest remaining time seen per actor / status).
	auto TrackStatuses = [this](int32 ActorId, const std::vector<ff::StatusView>& List) {
		for (const ff::StatusView& St : List)
		{
			if (St.t >= 0.0f)
			{
				float& T0 = StatusT0.FindOrAdd(FString::Printf(TEXT("%d/%s"), ActorId, *HS(St.name)));
				T0 = FMath::Max(T0, St.t);
			}
		}
	};
	TrackStatuses(Hud.player_id, Hud.statuses);
	TrackStatuses(Hud.target_id, Hud.rival_statuses);
	if (StatusT0.Num() > 256)
	{
		StatusT0.Reset();
	}

	// Lock-on marker: eases toward the projected position, fades in / out.
	FVector2D Px;
	const bool bWant = Hud.has_marker && Project(Hud.marker_world, Px);
	if (bWant)
	{
		if (!bMarker && MarkerAlpha <= 0.01f)
		{
			MarkerShown = Px;
		}
		MarkerPx = Px;
		MarkerLabel = HS(Hud.marker_label);
	}
	bMarker = bWant;
	const float Want = bMarker ? 1.0f : 0.0f;
	MarkerAlpha = In.bReducedMotion ? Want : FMath::FInterpConstantTo(MarkerAlpha, Want, Dt, 8.0f);
	MarkerShown = FMath::Lerp(MarkerShown, MarkerPx, In.bReducedMotion ? 1.0f : FMath::Min(1.0f, Dt * 22.0f));

	// World HUD (CONTROLS_HUD_PLAN part D): vitals arcs at the feet of the player and of the fighter it fights; the rival's
	// charge as a rim in its element colour.
	Rings.Reset();
	bPlayerRing = bRivalRing = false;
	const ff::ActorView* Opp = Player ? Snap.FindActor(Player->lock_target) : nullptr;
	{
		const ff::Vec3 CamF = ff::Vec3(In.CamForwardSim.x, 0.0f, In.CamForwardSim.z).normalized();
		const float A0 = std::atan2(-CamF.z, -CamF.x);   // the ring point nearest the camera
		constexpr int32 N = 48;
		const float Radii[4] = {0.66f, 0.57f, 0.48f, 0.78f};
		for (const ff::ActorView& A : Snap.actors)
		{
			const bool bP = A.id == Hud.player_id;
			if ((!bP && !(Opp && A.id == Opp->id) && !A.is_rival) || A.health <= 0.0f)
			{
				continue;
			}
			FFootRing Rg;
			Rg.bPlayer = bP;
			Rg.Element = A.element;
			Rg.bCharge = A.charge.active && A.charge.max_tier > 0;
			const ff::Vec3 C = A.pos + ff::Vec3(0.0f, 0.04f, 0.0f);
			bool bOk = true;
			for (int32 k = 0; k < 4 && bOk; ++k)
			{
				if ((k == 2 && !bP) || (k == 3 && !Rg.bCharge))
				{
					continue;
				}
				Rg.Ring[k].Reserve(N + 1);
				for (int32 i = 0; i <= N; ++i)
				{
					const float Ang = A0 + (float(i) / float(N) - 0.5f) * 2.0f * PI;   // i = N / 2 faces the camera
					FVector2D RingPx;
					if (!Project(C + ff::Vec3(std::cos(Ang) * Radii[k], 0.0f, std::sin(Ang) * Radii[k]), RingPx))
					{
						bOk = false;
						break;
					}
					Rg.Ring[k].Add(RingPx);
				}
			}
			if (!bOk)
			{
				continue;
			}
			Rg.Frac[0] = FMath::Clamp(A.health / 100.0f, 0.0f, 1.0f);
			Rg.Frac[1] = FMath::Clamp(A.balance / 100.0f, 0.0f, 1.0f);
			Rg.Frac[2] = FMath::Clamp(A.focus / 100.0f, 0.0f, 1.0f);
			Rg.ChargeTier = A.charge.tier;
			Rg.ChargeMax = A.charge.max_tier;
			Rg.Frac[3] = FMath::Clamp(A.charge.frac, 0.0f, 1.0f);
			(bP ? bPlayerRing : bRivalRing) = true;
			Rings.Add(MoveTemp(Rg));
		}
	}

	// Outcome callouts: every counter that resolved this frame, named by the rule's outcome, at the impact.
	for (FCallout& C : Callouts)
	{
		C.T += Dt;
	}
	Callouts.RemoveAll([](const FCallout& C) { return C.T > kCalloutLife; });
	if (In.Events)
	{
		for (const ff::Event& E : *In.Events)
		{
			if (E.type != "interaction" || !E.data["pos"].is_vec3())
			{
				continue;
			}
			const int32 CA = int32(E.data["counter_actor"].as_int(-1));
			const int32 TA = int32(E.data["threat_actor"].as_int(-1));
			const std::string To = E.data["to"].is_string() ? E.data["to"].as_string() : std::string();
			const FString Txt = HS(ff::CounterOutcomeLabel(E.data["outcome"].is_string() ? E.data["outcome"].as_string() : std::string(), To));
			if (Txt.IsEmpty() || (CA < 0 && TA < 0))
			{
				continue;
			}
			const std::string Band = E.data["band"].is_string() ? E.data["band"].as_string() : std::string();
			FCallout C;
			C.World = E.data["pos"].as_vec3();
			C.Text = Txt;
			C.bPerfect = E.data["perfect"].as_bool(false);
			if (CA == Hud.player_id)        // your answer: how well it worked
			{
				C.Col = Band == "partial" ? C4(1.0f, 0.78f, 0.3f, 1) : Band == "fail" ? C4(1.0f, 0.38f, 0.3f, 1) : C4(0.5f, 1.0f, 0.62f, 1);
			}
			else if (TA == Hud.player_id)   // your attack met their answer
			{
				C.Col = Band == "fail" ? C4(0.5f, 1.0f, 0.62f, 1) : C4(1.0f, 0.55f, 0.42f, 1);
			}
			else
			{
				C.Col = C4(0.85f, 0.9f, 1.0f, 1);
			}
			// the same outcome again close by (a volley, a stream) counts up on the live callout instead of stacking a copy
			FCallout* Same = Callouts.FindByPredicate([&](const FCallout& O) {
				return O.T < 0.9f && O.Text == C.Text && (O.World - C.World).length() < 2.5f;
			});
			if (Same)
			{
				++Same->Count;
				Same->T = FMath::Min(Same->T, 0.05f);   // pops again
				Same->World = C.World;
				Same->Col = C.Col;
				Same->bPerfect |= C.bPerfect;
			}
			else
			{
				Callouts.Add(C);
			}
		}
		while (Callouts.Num() > 4)
		{
			Callouts.RemoveAt(0);
		}
	}
	for (FCallout& C : Callouts)
	{
		C.bVisible = Project(C.World + ff::Vec3(0.0f, 0.7f + 0.55f * C.T, 0.0f), C.Px);
	}

	// Off-screen threats (bodies attacking the player).
	Threats.Reset();
	Labels.Reset();
	Bars.Reset();
	if (!Player)
	{
		return;
	}
	const ff::Vec3 Chest = Player->pos + ff::Vec3(0.0f, 1.25f, 0.0f);
	const ff::Vec3 F = ff::Vec3(In.CamForwardSim.x, 0.0f, In.CamForwardSim.z).normalized();
	const ff::Vec3 R(-F.z, 0.0f, F.x);   // forward x up (sim space)
	for (const ff::BodyView& B : Snap.bodies)
	{
		if (B.attack_id == 0 || B.attack_owner == Hud.player_id)
		{
			continue;
		}
		const ff::Vec3 To = Chest - B.pos;
		if (B.form != ff::Form::Wave && B.vel.dot(To) <= 0.0f)
		{
			continue;
		}
		if (To.length() > 16.0f)
		{
			continue;
		}
		FVector2D Sp;
		const bool bProj = Project(B.pos, Sp);
		if (bProj && Sp.X >= 0.0 && Sp.Y >= 0.0 && Sp.X <= In.ViewportPixels.X && Sp.Y <= In.ViewportPixels.Y)
		{
			// On screen (the paint re-checks against the safe rect in local units).
			const double Mx = In.ViewportPixels.X * 0.04, My = In.ViewportPixels.Y * 0.06;
			if (Sp.X > Mx && Sp.Y > My && Sp.X < In.ViewportPixels.X - Mx && Sp.Y < In.ViewportPixels.Y - My)
			{
				continue;
			}
		}
		const ff::Vec3 D = To * -1.0f;
		FThreat T;
		T.Dir = FVector2D(D.dot(R), -D.dot(F)).GetSafeNormal();
		T.bHot = B.mat == ff::Mat::Stone && (B.temp >= kHotRockC || B.liquid > 0.0f);
		if (!T.Dir.IsNearlyZero())
		{
			Threats.Add(T);
		}
	}

	// Lab overlay: every live body (id, material, tag, mass, temperature, power, tier), zone radii, actor actions.
	if (In.bLabOverlay)
	{
		for (const ff::BodyView& B : Snap.bodies)
		{
			if (B.form == ff::Form::Pool)
			{
				continue;
			}
			FVector2D Sp;
			if (!Project(B.pos + ff::Vec3(0.0f, 0.3f, 0.0f), Sp))
			{
				continue;
			}
			FScreenLabel L;
			L.Px = Sp;
			L.Color = C4(0.6f, 1.0f, 0.7f, 0.85f);
			if (B.form == ff::Form::Zone || B.zone_radius > 0.0f)
			{
				L.Color = C4(0.7f, 0.8f, 1.0f, 0.85f);
				FVector2D Edge, Centre;
				if (Project(B.pos + R * FMath::Max(B.zone_radius, 0.2f), Edge) && Project(B.pos + ff::Vec3(0.0f, 0.3f, 0.0f), Centre))
				{
					L.ZoneRadiusPx = float(FVector2D::Distance(Edge, Centre));
				}
			}
			else if (B.mat == ff::Mat::Stone && (B.temp >= kHotRockC || B.liquid > 0.0f))
			{
				L.Color = C4(1.0f, 0.65f, 0.4f, 0.9f);
			}
			const int32 MatIndex = FMath::Clamp(int32(B.mat), 0, 8);
			L.Text = FString::Printf(TEXT("#%d %s%s %.0fkg %.0fC"), B.id, *HS(ff::kMatNames[MatIndex]),
			                         B.tag.empty() ? TEXT("") : *FString::Printf(TEXT("[%s]"), *HS(B.tag)), B.mass, B.temp);
			if (B.power > 0.0f)
			{
				L.Text += FString::Printf(TEXT(" P%.0f"), B.power);
			}
			if (B.tier > 0)
			{
				L.Text += FString::Printf(TEXT(" T%d"), B.tier);
			}
			Labels.Add(L);
		}
		for (const ff::ActorView& A : Snap.actors)
		{
			FVector2D Sp;
			if (!Project(A.pos + ff::Vec3(0.0f, 2.4f, 0.0f), Sp))
			{
				continue;
			}
			FString Act = TEXT("-");
			if (A.action.active)
			{
				const int32 Ph = FMath::Clamp(int32(A.action.phase), 0, 5);
				Act = FString::Printf(TEXT("%s/%s"), *HS(A.action.id), *HS(ff::kActionPhaseNames[Ph]));
			}
			FString St;
			for (const ff::StatusView& S : A.statuses)
			{
				St += (St.IsEmpty() ? TEXT("") : TEXT(" ")) + HS(S.name);
			}
			FScreenLabel L;
			L.Px = Sp;
			L.bDot = false;
			L.Color = C4(1.0f, 1.0f, 0.8f, 0.9f);
			L.Text = FString::Printf(TEXT("%s %s%s"), *HS(A.name), *Act, St.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" [%s]"), *St));
			Labels.Add(L);
		}
	}

	// Practice / debug overlay: each fighter's current move as startup / active / recovery segments.
	if (In.bShowDebug)
	{
		for (const ff::ActorView& A : Snap.actors)
		{
			if (!A.action.active || A.action.id == "guard")
			{
				continue;
			}
			FVector2D Sp;
			if (!Project(A.pos + ff::Vec3(0.0f, 2.1f, 0.0f), Sp))
			{
				continue;
			}
			FTimingBar Tb;
			Tb.Px = Sp;
			Tb.Startup = A.action.startup;
			Tb.Active = A.action.active_time;
			Tb.Recovery = A.action.recovery;
			Tb.Elapsed = (A.action.phase == ff::ActionPhase::Charge || A.action.phase == ff::ActionPhase::Channel) ? A.action.startup : A.action.total;
			const int32 Ph = FMath::Clamp(int32(A.action.phase), 0, 5);
			Tb.Text = FString::Printf(TEXT("%s %s"), *HS(A.action.id), *HS(ff::kActionPhaseNames[Ph]));
			Bars.Add(Tb);
		}
	}
}

int32 SFourfoldHud::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
                            int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	FPaintCtx H{FFUi::FPainter(AllottedGeometry, OutDrawElements, LayerId)};
	H.Size = FVector2D(AllottedGeometry.GetLocalSize());
	if (H.Size.X < 2.0 || H.Size.Y < 2.0)
	{
		return LayerId;
	}
	H.PixelToLocal = AllottedGeometry.Scale > 1e-4f ? 1.0f / AllottedGeometry.Scale : 1.0f;
	const FMargin& Safe = FFUi::SafeLocal;
	const float G = float(FMath::Min(H.Size.X, H.Size.Y)) / 720.0f;   // one Godot layout unit (canvas_items, 720 tall)
	const double Inset = 12.0 * G;
	H.SrPos = FVector2D(Safe.Left + Inset, Safe.Top + Inset);
	H.SrSize = FVector2D(FMath::Max(1.0, H.Size.X - Safe.Left - Safe.Right - 2.0 * Inset), FMath::Max(1.0, H.Size.Y - Safe.Top - Safe.Bottom - 2.0 * Inset));
	const float S = FMath::Clamp(float(H.SrSize.Y) / (720.0f * G), 0.8f, 2.0f);
	H.U = S * G;
	H.FloorPpm = (PLATFORM_IOS || PLATFORM_ANDROID) ? FFUi::Ppm : 0.0f;
	H.BarH = FMath::Max(4.0f * G, kBarMm * H.FloorPpm);
	H.FsStat = H.Fs(11.0f, kTextMm);
	H.FsName = H.Fs(12.0f, kTextMm);
	H.FsLine = H.Fs(13.0f, kLineMm);
	H.GrowBar = H.BarH - 4.0f * G;
	H.GrowStat = H.FsStat - 11.0f * H.U;
	H.GrowLine = H.FsLine - 13.0f * H.U;
	const float U = H.U;

	// Flash first (under everything).
	if (FlashT > 0.0f && Input.Flashes > 0.0f)
	{
		FLinearColor Col = FlashKind == TEXT("perfect") ? C4(1.0f, 0.95f, 0.8f, 0.10f) : C4(0.8f, 0.85f, 1.0f, 0.08f);
		Col.A *= Input.Flashes;
		H.P.Rect(FVector2D(0, 0), H.Size, Col);
	}

	if (bGameplay && bHasPlayer)
	{
		DrawRings(H);
		DrawCallouts(H);
		const float Row = 9.0f * U + H.GrowBar;
		const double X = H.SrPos.X + 8.0 * U;
		const double Y = H.SrPos.Y + 8.0 * U;
		const float Bw = 190.0f * U;
		if (!bPlayerRing)   // the arcs at the feet carry the vitals while the fighter is on screen
		{
			H.Bar(FVector2D(X, Y), Bw, Hud.health / 100.0f, C4(0.92f, 0.9f, 0.86f, 1), Alpha);
			H.Bar(FVector2D(X, Y + Row), Bw * 0.8f, Hud.balance / 100.0f, C4(0.75f, 0.82f, 0.92f, 1), Alpha * 0.9f);
			H.Bar(FVector2D(X, Y + 2 * Row), Bw * 0.8f, Hud.focus / 100.0f, C4(0.96f, 0.82f, 0.45f, 1), Alpha * 0.9f);
		}
		// Sub-element line, then the resource bars that matter right now.
		const FLinearColor Ec = FFUi::ElementColor(Hud.element);
		const double SubBase = Y + 3 * Row + H.FsStat + H.GrowStat * 0.2;
		H.TextBase(FString::Printf(TEXT("%s / %s"), *HS(Hud.element_name), *HS(Hud.sub_name)), X, SubBase, H.FsStat, WithA(Ec, 0.95f * Alpha), -1, 0.0,
		           0.45f * Alpha);
		double Ry = SubBase + 4.0 * U + H.GrowBar * 0.5;
		const double ResStep = FMath::Max(9.0 * U + H.GrowBar + H.GrowStat * 0.2, H.FsStat + 2.0 * U);
		double HeatBase = Ry + 6.0 * U + H.GrowBar * 0.5 + H.GrowStat * 0.35;
		auto Resource = [&](float Frac, const FLinearColor& BarCol, float BarA, const FString& Text, const FLinearColor& TextCol) {
			H.Bar(FVector2D(X, Ry), Bw * 0.6f, Frac, BarCol, BarA);
			H.TextBase(Text, X + Bw * 0.62, HeatBase, H.FsStat, TextCol);
			Ry += ResStep;
			HeatBase += ResStep;
		};
		if (Hud.show_heat)
		{
			Resource(Hud.heat_reserve / 500.0f, C4(1.0f, 0.45f, 0.2f, 1), 1.0f, FString::Printf(TEXT("HEAT %d"), int32(Hud.heat_reserve)), C4(1, 0.6f, 0.4f, 0.9f));
		}
		if (Hud.show_water)
		{
			Resource(Hud.water_carried / 6.0f, C4(0.45f, 0.78f, 1.0f, 1), 0.9f * Alpha, FString::Printf(TEXT("water %.1f kg"), Hud.water_carried),
			         C4(0.6f, 0.85f, 1.0f, 0.85f * Alpha));
		}
		if (Hud.show_metal)
		{
			Resource(Hud.metal_carried / 12.0f, C4(0.72f, 0.78f, 0.86f, 1), 0.9f * Alpha, FString::Printf(TEXT("metal %.1f kg"), Hud.metal_carried),
			         C4(0.8f, 0.85f, 0.92f, 0.85f * Alpha));
		}
		if (Hud.show_static)
		{
			Resource(Hud.static_charge / 60.0f, C4(0.7f, 0.55f, 1.0f, 1), 0.9f * Alpha, FString::Printf(TEXT("static %d"), int32(Hud.static_charge)),
			         C4(0.8f, 0.7f, 1.0f, 0.85f * Alpha));
		}
		double WaterBase = FMath::Max(HeatBase, Ry);
		WaterBase += DrawStatuses(H, Hud.statuses, Hud.player_id, FVector2D(X, Ry + 1.5 * U), false);

		// Rival vitals (locked target), top centre, compact.
		if (Hud.has_rival)
		{
			const double Cx = H.SrCenter().X;
			if (!bRivalRing)
			{
				H.Bar(FVector2D(Cx - 80.0 * U, Y), 160.0f * U, Hud.rival_health / 100.0f, C4(0.95f, 0.55f, 0.45f, 1), 0.85f);
				H.Bar(FVector2D(Cx - 64.0 * U, Y + Row), 128.0f * U, Hud.rival_balance / 100.0f, C4(0.75f, 0.82f, 0.92f, 1), 0.7f);
			}
			const double NameBase = Y + 28.0 * U + H.GrowBar + (H.FsName - 12.0f * U) * 0.75;
			H.TextBase(HS(Hud.rival_name), Cx - 80.0 * U, NameBase, H.FsName, FLinearColor(1, 1, 1, 0.7f), 0, 160.0 * U);
			if (!Hud.rival_statuses.empty())
			{
				DrawStatuses(H, Hud.rival_statuses, Hud.target_id, FVector2D(Cx - 80.0 * U, NameBase + 5.0 * U), true);
			}
		}
		if (!Input.LabStatus.IsEmpty())
		{
			const double Lx = H.SrCenter().X;
			H.TextBase(Input.LabStatus, Lx - 90.0 * U, Y + 52.0 * U + H.GrowBar + H.GrowStat, H.FsStat, C4(1.0f, 0.85f, 0.45f, 0.95f), 0, 180.0 * U);
		}
		DrawChargeBar(H);
		DrawCounters(H);
		DrawChain(H);

		// Objective + challenge.
		const double ObjBase = H.SrEnd().Y - 10.0 * U;
		if (!Hud.objective.empty())
		{
			H.TextBase(HS(Hud.objective), H.SrPos.X, ObjBase, H.FsLine, FLinearColor(1, 1, 1, 0.55f), 0, H.SrSize.X, 0.35f);
		}
		if (!Hud.challenge_text.empty())
		{
			H.TextBase(HS(Hud.challenge_text), H.SrPos.X, ObjBase - 18.0 * U - H.GrowLine, H.FsLine, C4(1.0f, 0.86f, 0.55f, 0.85f), 0, H.SrSize.X, 0.35f);
		}

		// Off-screen threat arrows.
		for (const FThreat& T : Threats)
		{
			const FVector2D C = H.SrCenter();
			const FVector2D Edge = C + T.Dir * FMath::Min(H.SrSize.X, H.SrSize.Y) * 0.42;
			const FLinearColor Col = T.bHot ? C4(1.0f, 0.55f, 0.3f, 0.9f) : FLinearColor(1, 1, 1, 0.85f);
			const FVector2D Tip = Edge + T.Dir * 14.0 * U;
			const FVector2D Side = FVector2D(-T.Dir.Y, T.Dir.X) * 9.0 * U;
			H.P.Triangle(Tip, Edge + Side, Edge - Side, Col);
		}

		// Lab overlay labels.
		const float LabFs = FMath::Max(10.0f, 10.0f * U);
		for (const FScreenLabel& L : Labels)
		{
			const FVector2D P = L.Px * H.PixelToLocal;
			if (L.ZoneRadiusPx > 0.0f)
			{
				H.P.Ring(P, L.ZoneRadiusPx * H.PixelToLocal, FMath::Max(1.5f, S), WithA(L.Color, 0.5f));
			}
			if (L.bDot)
			{
				H.P.Circle(P, 3.0f * U, L.Color);
				H.TextBase(L.Text, P.X + 6.0 * U, P.Y - 4.0 * U, LabFs, L.Color, -1, 0.0, 0.7f);
			}
			else
			{
				H.TextBase(L.Text, P.X - 60.0 * U, P.Y, LabFs, L.Color, -1, 0.0, 0.7f);
			}
		}

		// Debug: timing bars, debug lines, perf.
		if (Input.bShowDebug)
		{
			for (const FTimingBar& Tb : Bars)
			{
				const float Tot = FMath::Max(Tb.Startup + Tb.Active + Tb.Recovery, 0.01f);
				const float W = 90.0f * U;
				const FVector2D O = Tb.Px * H.PixelToLocal - FVector2D(W * 0.5f, 0.0);
				H.P.Rect(O, FVector2D(W * Tb.Startup / Tot, 5.0 * U), C4(1.0f, 0.85f, 0.3f, 0.85f));
				H.P.Rect(O + FVector2D(W * Tb.Startup / Tot, 0.0), FVector2D(W * Tb.Active / Tot, 5.0 * U), C4(1.0f, 0.35f, 0.3f, 0.9f));
				H.P.Rect(O + FVector2D(W * (Tb.Startup + Tb.Active) / Tot, 0.0), FVector2D(W * Tb.Recovery / Tot, 5.0 * U), C4(0.6f, 0.6f, 0.65f, 0.8f));
				H.P.Rect(O + FVector2D(W * FMath::Clamp(Tb.Elapsed / Tot, 0.0f, 1.0f) - 1.0f, -3.0 * U), FVector2D(2.0, 11.0 * U), FLinearColor::White);
				H.TextBase(Tb.Text, O.X, O.Y - 5.0 * U, FMath::Max(9.0f, 10.0f * U), FLinearColor(1, 1, 1, 0.8f));
			}
			double Dy = WaterBase + 16.0 * U;
			for (const FString& Line : Input.DebugLines)
			{
				H.TextBase(Line, X, Dy, FMath::Max(9.0f, 11.0f * U), C4(0.85f, 1.0f, 0.85f, 0.9f), -1, 0.0, 0.5f);
				Dy += 14.0 * U;
			}
			H.TextBase(Input.PerfText, H.SrEnd().X - 260.0 * U, H.SrEnd().Y - 50.0 * U - H.GrowLine, FMath::Max(9.0f, 11.0f * U), C4(0.85f, 1.0f, 0.85f, 0.9f), -1,
			           0.0, 0.5f);
		}
		DrawMarker(H);
	}

	// Toasts show in every mode (scenario loaded, mastery, Lab messages).
	if (ToastT > 0.0f && !ToastText.IsEmpty())
	{
		const float A = FMath::Clamp(ToastT / 0.4f, 0.0f, 1.0f);
		H.TextBase(ToastText, H.SrPos.X, H.SrCenter().Y - 90.0 * U, H.Fs(18.0f, kToastMm), FLinearColor(1, 1, 1, 0.9f * A), 0, H.SrSize.X, 0.5f * A);
	}
	return LayerId + 1;
}

float SFourfoldHud::DrawStatuses(const FPaintCtx& H, const std::vector<ff::StatusView>& Statuses, int32 ActorId, FVector2D Origin, bool bCentred) const
{
	if (Statuses.empty())
	{
		return 0.0f;
	}
	const float U = H.U;
	const float Sz = FMath::Max(16.0f * U, 3.8f * H.FloorPpm);
	const float Gap = 3.0f * U;
	std::vector<const ff::StatusView*> Sorted;
	for (const ff::StatusView& S : Statuses)
	{
		Sorted.push_back(&S);
	}
	std::sort(Sorted.begin(), Sorted.end(), [](const ff::StatusView* A, const ff::StatusView* B) { return A->name < B->name; });
	const float Total = float(Sorted.size()) * (Sz + Gap) - Gap;
	double X = Origin.X;
	if (bCentred)
	{
		X = Origin.X + (160.0 * U - Total) * 0.5;
	}
	const FSlateFontInfo F = FFUi::Font(FMath::Max(8.0f, Sz * 0.42f), true);
	for (const ff::StatusView* St : Sorted)
	{
		const FStatusStyle* Style = FindStatusStyle(St->name);
		const FString Code = Style ? FString(Style->Code) : HS(St->name).Left(2).ToUpper();
		const FLinearColor Col = Style ? C4(Style->R, Style->G, Style->B, 1.0f) : C4(0.8f, 0.8f, 0.8f, 1.0f);
		const FVector2D Pos(X, Origin.Y);
		const float Radius = Sz * 0.18f;
		H.P.Rect(Pos, FVector2D(Sz, Sz), C4(0.04f, 0.05f, 0.07f, 0.55f * Alpha), Radius);
		if (St->t >= 0.0f)
		{
			const float* T0 = StatusT0.Find(FString::Printf(TEXT("%d/%s"), ActorId, *HS(St->name)));
			const float Ref = T0 ? FMath::Max(*T0, 0.1f) : FMath::Max(St->t, 0.1f);
			const float Fr = FMath::Clamp(St->t / Ref, 0.0f, 1.0f);
			H.P.Rect(FVector2D(X, Origin.Y + Sz * (1.0f - Fr)), FVector2D(Sz, Sz * Fr), WithA(Col, 0.25f * Alpha), Radius * Fr);
		}
		H.P.RoundRect(Pos, FVector2D(Sz, Sz), FLinearColor::Transparent, WithA(Col, 0.9f * Alpha), FMath::Max(1.5f, Sz * 0.07f), Radius);
		H.P.TextCentered(Code, Pos + FVector2D(Sz * 0.5, Sz * 0.5), F, FLinearColor(1, 1, 1, 0.95f * Alpha));
		X += Sz + Gap;
	}
	return Sz + 3.0f * U;
}

void SFourfoldHud::DrawChargeBar(const FPaintCtx& H) const
{
	const ff::ChargeView& Cv = Hud.charge;
	const int32 Mx = FMath::Clamp(Cv.max_tier, 0, 3);
	if (!Cv.active || Mx <= 0)
	{
		return;
	}
	const float U = H.U;
	const int32 Tier = FMath::Clamp(Cv.tier, 0, 3);
	const FString Name = Hud.charge_move_name.empty() ? FString() : HS(Hud.charge_move_name);
	// World HUD: with the player's ground rim on screen the tiers live there; only the move name + tier sit under it.
	const FFootRing* Own = Rings.FindByPredicate([](const FFootRing& Rg) { return Rg.bPlayer && Rg.bCharge && Rg.Ring[3].Num() > 2; });
	if (Own)
	{
		const TArray<FVector2D>& R = Own->Ring[3];
		const FVector2D Front = R[(R.Num() - 1) / 2] * H.PixelToLocal;
		const float Fs = H.Fs(13.0f, kTextMm * 1.1f);
		const FLinearColor Col = Tier > 0 ? FMath::Lerp(FFUi::ElementColor(ChargeElement), FLinearColor::White, 0.35f) : FLinearColor(1, 1, 1, 0.9f);
		const FString Txt = Tier > 0 ? FString::Printf(TEXT("%s  T%d"), *Name.ToUpper(), Tier) : Name.ToUpper();
		H.TextBase(Txt, Front.X - 200.0 * U, Front.Y + Fs * 1.45f, Fs, Col, 0, 400.0 * U, 0.7f);
		return;
	}
	const float W = FMath::Max(150.0f * U, 28.0f * H.FloorPpm);
	const float Hh = FMath::Max(6.0f * U, 1.2f * H.FloorPpm);
	const double X = H.SrCenter().X - W * 0.5;
	const double Y = H.SrEnd().Y - 62.0 * U;
	const FLinearColor Col = FFUi::ElementColor(ChargeElement);
	const float Gap = 4.0f * U;
	const float Seg = (W - Gap * float(Mx - 1)) / float(Mx);
	for (int32 K = 0; K < Mx; ++K)
	{
		const FVector2D Pos(X + K * (Seg + Gap), Y);
		H.P.Rect(Pos, FVector2D(Seg, Hh), FLinearColor(0, 0, 0, 0.45f), Hh * 0.3f);
		const float Lit = K < Tier ? 1.0f : (K == Tier ? FMath::Clamp(Cv.frac, 0.0f, 1.0f) : 0.0f);
		if (Lit > 0.0f)
		{
			H.P.Rect(Pos, FVector2D(Seg * Lit, Hh), WithA(Col, 0.95f), Hh * 0.3f);
		}
		H.P.RoundRect(Pos, FVector2D(Seg, Hh), FLinearColor::Transparent, FLinearColor(1, 1, 1, 0.35f), 1.0f, Hh * 0.3f);
	}
	H.TextBase(FString::Printf(TEXT("%s  T%d"), *Name, Tier), X, Y - 4.0 * U, H.Fs(12.0f, kTextMm), FLinearColor(1, 1, 1, 0.95f), 0, W, 0.6f);
}

void SFourfoldHud::DrawCounters(const FPaintCtx& H) const
{
	// Context counters (docs/game/CONTROLS_HUD_PLAN.md part A): what each defensive input does to the threat that is coming,
	// predicted by the sim's own counter rule; the colour is the outcome band.
	if (!Hud.has_threat || Hud.counters.empty())
	{
		return;
	}
	const float U = H.U;
	const float FsKey = H.Fs(10.0f, kTextMm * 0.8f);
	const float FsTxt = H.Fs(14.0f, kTextMm * 1.1f);
	const FSlateFontInfo FKey = FFUi::Font(FsKey, false);
	const FSlateFontInfo FTxt = FFUi::Font(FsTxt, true, FMath::Max(1, FMath::RoundToInt(FsTxt / 7.0f)), FLinearColor(0, 0, 0, 0.6f));
	const float Pad = 8.0f * U, Gap = 6.0f * U;
	// near impact the strip pulses (the perfect window is close)
	const float Urgent = Hud.threat_charging ? 0.35f : FMath::Clamp(1.0f - (Hud.threat_tti - 0.15f) / 0.6f, 0.0f, 1.0f);
	const float Pulse = 0.75f + 0.25f * FMath::Sin(float(FPlatformTime::Seconds()) * 18.0f) * Urgent;
	struct FPill
	{
		FString Key, Txt;
		FLinearColor Col;
		float W;
		bool bNow = false;
	};
	TArray<FPill> Pills;
	float Total = 0.0f;
	for (const ff::CounterHintView& C : Hud.counters)
	{
		FPill Pl;
		// press now for a perfect guard (only when the impact time is known: a held charge lands whenever released)
		const bool bNow = C.slot == "guard" && !Hud.threat_charging && Hud.threat_tti <= Hud.perfect_window;
		Pl.Key = SlotKey(Input.Device, C.slot);   // the input that gives this answer on the device in use
		if (bNow)
		{
			Pl.Key += TEXT("  NOW");
			Pl.bNow = true;
		}
		Pl.Txt = HS(C.label);
		if (C.tier > 0)
		{
			Pl.Txt += FString::Printf(TEXT(" T%d"), C.tier);
		}
		if (C.perfect)
		{
			Pl.Txt += TEXT(" ★");   // needs a perfect guard
		}
		Pl.Col = C.band == "full" ? C4(0.45f, 1.0f, 0.6f, 1) : C.band == "partial" ? C4(1.0f, 0.78f, 0.3f, 1)
		       : C.band == "fail" ? C4(1.0f, 0.38f, 0.3f, 1) : C4(0.7f, 0.7f, 0.7f, 1);
		Pl.W = FMath::Max(FFUi::Measure(Pl.Txt, FTxt).X, FFUi::Measure(Pl.Key, FKey).X) + 2.0f * Pad;
		Total += Pl.W;
		Pills.Add(Pl);
	}
	Total += Gap * float(Pills.Num() - 1);
	const float Hh = FsKey + FsTxt + 1.6f * Pad;
	double X = H.SrCenter().X - Total * 0.5;
	const double Y = H.SrEnd().Y - 112.0 * U - Hh;
	const FString When = Hud.threat_charging ? FString(TEXT("CHARGING")) : FString::Printf(TEXT("%.1f s"), Hud.threat_tti);
	H.TextBase(FString::Printf(TEXT("%s  %s"), *HS(ff::ThreatLabel(Hud.threat_cls)).ToUpper(), *When), H.SrCenter().X - Total * 0.5, Y - 4.0 * U,
	           FsKey, FLinearColor(1, 1, 1, 0.7f), 0, Total, 0.5f);
	for (const FPill& Pl : Pills)
	{
		// inside the perfect window the guard pill lights up (the honest telegraph: press now)
		const FLinearColor Fill = Pl.bNow ? WithA(FMath::Lerp(Pl.Col, FLinearColor::Black, 0.45f), 0.92f) : FLinearColor(0.02f, 0.02f, 0.03f, 0.6f);
		H.P.RoundRect(FVector2D(X, Y), FVector2D(Pl.W, Hh), Fill, WithA(Pl.bNow ? FLinearColor::White : Pl.Col, 0.9f * Pulse), (Pl.bNow ? 3.0f : 2.0f) * U, 6.0f * U);
		H.P.Text(Pl.Key, FVector2D(X + Pad, Y + 0.5f * Pad), FKey, FLinearColor(1, 1, 1, Pl.bNow ? 1.0f : 0.6f));
		H.P.Text(Pl.Txt, FVector2D(X + Pad, Y + 0.7f * Pad + FsKey), FTxt, Pl.bNow ? FLinearColor::White : Pl.Col);
		X += Pl.W + Gap;
	}
}

void SFourfoldHud::DrawChain(const FPaintCtx& H) const
{
	// Chain prompt (CONTROLS_HUD_PLAN part C): inside the chain window the attacks that would chain now, with their keys,
	// under the player's ground arcs; a bar drains as the window closes.
	const ff::ChainView& Ch = Hud.chain;
	if (!Ch.open || Ch.slots.empty())
	{
		return;
	}
	const float U = H.U;
	const FLinearColor Ec = FFUi::ElementColor(Hud.element);
	const float FsKey = H.Fs(10.0f, kTextMm * 0.8f);
	const float FsTxt = H.Fs(13.0f, kTextMm);
	const FSlateFontInfo FKey = FFUi::Font(FsKey, false);
	const FSlateFontInfo FTxt = FFUi::Font(FsTxt, true, FMath::Max(1, FMath::RoundToInt(FsTxt / 7.0f)), FLinearColor(0, 0, 0, 0.6f));
	const float Pad = 6.0f * U, Gap = 5.0f * U;
	TArray<FString> Keys, Names;
	TArray<float> Ws;
	const FString Head = Ch.weave ? FString::Printf(TEXT("WEAVE %d"), Ch.n + 1) : FString::Printf(TEXT("CHAIN %d"), Ch.n + 1);
	const float HeadW = FFUi::Measure(Head, FTxt).X + Pad;
	float Total = HeadW;
	for (size_t i = 0; i < Ch.slots.size(); ++i)
	{
		Keys.Add(SlotKey(Input.Device, Ch.slots[i]));
		Names.Add(HS(i < Ch.moves.size() ? Ch.moves[i] : std::string()));
		Ws.Add(FMath::Max(FFUi::Measure(Names.Last(), FTxt).X, FFUi::Measure(Keys.Last(), FKey).X) + 2.0f * Pad);
		Total += Ws.Last() + Gap;
	}
	const float Hh = FsKey + FsTxt + 1.4f * Pad;
	// under the player's arcs when they are on screen, else just above the counter strip
	FVector2D Anchor(H.SrCenter().X, H.SrEnd().Y - 170.0 * U);
	if (const FFootRing* Own = Rings.FindByPredicate([](const FFootRing& Rg) { return Rg.bPlayer && Rg.Ring[0].Num() > 2; }))
	{
		const TArray<FVector2D>& R = Own->Ring[0];
		Anchor = R[(R.Num() - 1) / 2] * H.PixelToLocal + FVector2D(0.0, 14.0 * U);
	}
	double X = Anchor.X - Total * 0.5;
	const double Y = Anchor.Y;
	H.P.Text(Head, FVector2D(X, Y + 0.5f * Pad + FsKey * 0.5f), FTxt, FMath::Lerp(Ec, FLinearColor::White, 0.4f));
	X += HeadW;
	for (int32 i = 0; i < Keys.Num(); ++i)
	{
		H.P.RoundRect(FVector2D(X, Y), FVector2D(Ws[i], Hh), FLinearColor(0.02f, 0.02f, 0.03f, 0.62f), WithA(Ec, 0.95f), 2.0f * U, 6.0f * U);
		H.P.Text(Keys[i], FVector2D(X + Pad, Y + 0.4f * Pad), FKey, FLinearColor(1, 1, 1, 0.7f));
		H.P.Text(Names[i], FVector2D(X + Pad, Y + 0.6f * Pad + FsKey), FTxt, FMath::Lerp(Ec, FLinearColor::White, 0.25f));
		X += Ws[i] + Gap;
	}
	// the window drains
	const float Left = Ch.window > 1e-4f ? FMath::Clamp(Ch.left / Ch.window, 0.0f, 1.0f) : 1.0f;
	const float BarW = Total - HeadW - Gap;
	const FVector2D BarPos(Anchor.X - Total * 0.5 + HeadW, Y + Hh + 3.0f * U);
	H.P.Rect(BarPos, FVector2D(BarW, 3.0f * U), FLinearColor(0, 0, 0, 0.45f), 1.5f * U);
	H.P.Rect(BarPos, FVector2D(BarW * Left, 3.0f * U), WithA(Ec, 0.95f), 1.5f * U);
}

void SFourfoldHud::DrawRings(const FPaintCtx& H) const
{
	const float U = H.U;
	auto Arc = [&](const TArray<FVector2D>& Ring, float Frac, float Width, const FLinearColor& Col) {
		// the gauge is centred on the camera side (index N / 2) and shrinks toward it
		const int32 N = Ring.Num() - 1;
		if (N < 2 || Frac <= 0.0f)
		{
			return;
		}
		const int32 Half = FMath::Max(1, FMath::RoundToInt(Frac * float(N) * 0.5f));
		TArray<FVector2f> Pts;
		for (int32 i = N / 2 - Half; i <= N / 2 + Half; ++i)
		{
			Pts.Add(FVector2f(Ring[FMath::Clamp(i, 0, N)] * H.PixelToLocal));
		}
		H.P.Polyline(Pts, Width, Col);
	};
	for (const FFootRing& Rg : Rings)
	{
		const float W = FMath::Max(2.0f * U, 0.35f * H.FloorPpm);
		const FLinearColor HealthCol = Rg.bPlayer ? C4(0.95f, 0.93f, 0.88f, 1) : C4(0.98f, 0.55f, 0.45f, 1);
		// dim full circles under the gauges, then the gauges
		Arc(Rg.Ring[0], 1.0f, W * 2.2f, FLinearColor(0, 0, 0, 0.4f));
		Arc(Rg.Ring[0], Rg.Frac[0], W * 1.4f, WithA(HealthCol, 0.9f));
		Arc(Rg.Ring[1], 1.0f, W * 1.6f, FLinearColor(0, 0, 0, 0.32f));
		Arc(Rg.Ring[1], Rg.Frac[1], W, C4(0.72f, 0.82f, 0.95f, 0.85f));
		if (Rg.bPlayer)
		{
			Arc(Rg.Ring[2], 1.0f, W * 1.4f, FLinearColor(0, 0, 0, 0.3f));
			Arc(Rg.Ring[2], Rg.Frac[2], W * 0.8f, C4(0.96f, 0.82f, 0.45f, 0.85f));
		}
		if (Rg.bCharge)
		{
			// charge (the rival's intent, the player's own wind-up): the rim fills from the camera side, one step per tier,
			// with dark notches at the tier steps, pulsing faster per tier reached
			const FLinearColor Ec = FFUi::ElementColor(Rg.Element);
			const int32 Mx = FMath::Clamp(Rg.ChargeMax, 1, 3);
			const float Pulse = 0.7f + 0.3f * FMath::Sin(float(FPlatformTime::Seconds()) * (6.0f + 4.0f * float(Rg.ChargeTier)));
			const float Lit = FMath::Clamp((float(Rg.ChargeTier) + Rg.Frac[3]) / float(Mx), 0.0f, 1.0f);
			Arc(Rg.Ring[3], 1.0f, W * 2.6f, FLinearColor(0, 0, 0, 0.3f));
			Arc(Rg.Ring[3], 1.0f, W * 1.8f, WithA(Ec, 0.18f));
			Arc(Rg.Ring[3], Lit, W * 1.8f, WithA(Ec, 0.95f * Pulse));
			const int32 N = Rg.Ring[3].Num() - 1;
			for (int32 K = 1; K < Mx && N > 2; ++K)
			{
				const int32 Half = FMath::RoundToInt(float(K) / float(Mx) * float(N) * 0.5f);
				for (const int32 I : {N / 2 - Half, N / 2 + Half})
				{
					H.P.Circle(Rg.Ring[3][FMath::Clamp(I, 0, N)] * H.PixelToLocal, W * 1.3f, FLinearColor(0, 0, 0, 0.75f));
				}
			}
		}
	}
}

void SFourfoldHud::DrawCallouts(const FPaintCtx& H) const
{
	// Oldest first: each callout keeps clear of the ones already placed by lifting itself above them, so a burst of
	// impacts reads as a short stack instead of overprinted words.
	TArray<FBox2D> Placed;
	for (const FCallout& C : Callouts)
	{
		if (!C.bVisible)
		{
			continue;
		}
		const float Pop = 1.0f + 0.45f * (1.0f - FMath::Clamp(C.T / 0.12f, 0.0f, 1.0f));
		const float A = 1.0f - FMath::Clamp((C.T - (kCalloutLife - 0.4f)) / 0.4f, 0.0f, 1.0f);
		const float Base = H.Fs(C.bPerfect ? 30.0f : 24.0f, kLineMm * 1.6f);
		FString Txt = C.bPerfect ? TEXT("PERFECT  ") + C.Text.ToUpper() : C.Text.ToUpper();
		if (C.Count > 1)
		{
			Txt += FString::Printf(TEXT("  x%d"), C.Count);
		}
		// layout at the settled size so the pop does not shove its neighbours
		const FVector2D Sz = FFUi::Measure(Txt, FFUi::Font(Base, true));
		const FVector2D Anchor = C.Px * H.PixelToLocal;
		float Want = 0.0f;
		for (int32 Guard = 0; Guard < 8; ++Guard)
		{
			const FVector2D Ctr = Anchor - FVector2D(0.0, Want);
			const FBox2D Box(Ctr - Sz * 0.5, Ctr + Sz * 0.5);
			const FBox2D* Hit = Placed.FindByPredicate([&](const FBox2D& B) { return B.Intersect(Box); });
			if (!Hit)
			{
				break;
			}
			Want = float(Anchor.Y - Hit->Min.Y + Sz.Y * 0.5 + 2.0);
		}
		C.Lift = C.Lift < 0.0f || Input.bReducedMotion ? Want : FMath::FInterpTo(C.Lift, Want, Input.RealDt, 16.0f);
		const FVector2D Ctr = Anchor - FVector2D(0.0, C.Lift);
		Placed.Add(FBox2D(Anchor - FVector2D(0.0, Want) - Sz * 0.5, Anchor - FVector2D(0.0, Want) + Sz * 0.5));
		const float Px = Base * Pop;
		const FSlateFontInfo F = FFUi::Font(Px, true, FMath::Max(1, FMath::RoundToInt(Px / 8.0f)), FLinearColor(0, 0, 0, 0.75f * A));
		H.P.TextCentered(Txt, Ctr, F, WithA(C.Col, A));
	}
}

void SFourfoldHud::DrawMarker(const FPaintCtx& H) const
{
	if (MarkerAlpha <= 0.01f)
	{
		return;
	}
	const float Ppm = FFUi::Ppm;
	float R = 4.6f * Ppm;
	FVector2D C = MarkerShown * H.PixelToLocal;
	C.X = FMath::Clamp(C.X, double(R), FMath::Max(double(R), H.Size.X - R));
	C.Y = FMath::Clamp(C.Y, double(R), FMath::Max(double(R), H.Size.Y - R));
	if (!Input.bReducedMotion)
	{
		R *= 1.0f + 0.04f * FMath::Sin(float(FPlatformTime::Seconds() * 5.0));
	}
	const float W = FMath::Max(1.6f, 0.2f * Ppm);
	const FLinearColor Col = C4(1.0f, 0.93f, 0.78f, 0.85f * MarkerAlpha);
	const FLinearColor Shadow(0, 0, 0, 0.35f * MarkerAlpha);
	const float Arm = R * 0.55f;
	for (const float Sx : {-1.0f, 1.0f})
	{
		for (const float Sy : {-1.0f, 1.0f})
		{
			const FVector2D Corner = C + FVector2D(Sx * R, Sy * R);
			const FVector2D A = Corner - FVector2D(Sx * Arm, 0.0);
			const FVector2D B = Corner - FVector2D(0.0, Sy * Arm);
			const FVector2D Off(1.0, 1.5);
			H.P.Polyline({FVector2f(A + Off), FVector2f(Corner + Off), FVector2f(B + Off)}, W * 1.6f, Shadow);
			H.P.Polyline({FVector2f(A), FVector2f(Corner), FVector2f(B)}, W, Col);
		}
	}
	H.P.Circle(C, W * 1.1f, Col);
	if (!MarkerLabel.IsEmpty())
	{
		const float Fs = FMath::RoundToFloat(1.9f * Ppm);
		H.TextBase(MarkerLabel, C.X - R * 3.0f, C.Y - R - Fs * 0.5f, Fs, FLinearColor(1, 1, 1, 0.8f * MarkerAlpha), 0, R * 6.0f, 0.55f * MarkerAlpha);
	}
}
