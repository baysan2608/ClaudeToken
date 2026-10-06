// Fourfold game logic island - flick recognition for the ATTACK and GUARD buttons (port of game/ui/flick_recognizer.gd).
// A flick is a finger travel of at least kFlickMm from where it landed on the button:
//   ATTACK: inside kFlickWindowS of the press, or at release after a hold (any later travel);
//   GUARD : any time while held; the origin re-bases after every flick (push then sink without lifting).
// Direction = dominant axis of the travel (screen space, y DOWN): Up / Down / Side. The guard only knows Up / Down.
#pragma once

#include "FFGMath.h"
#include "ff/Types.h"

namespace ffg {

class FlickRecognizer {
public:
	static constexpr float kFlickMm = 6.0f;
	static constexpr float kFlickWindowS = 0.25f;
	static constexpr float kPreviewFraction = 0.55f;   // travel past this share of the threshold lights the petal

	Vec2 origin;
	bool fired = false;
	ff::Gesture hot = ff::Gesture::None;    // gesture pointed at now (petal highlight)
	ff::Gesture last = ff::Gesture::None;   // last gesture reported

	void Begin(Vec2 pos);
	static ff::Gesture Classify(Vec2 d);
	static float ThresholdPx(float ppm) { return kFlickMm * ppm; }
	// Drag update; held_s = how long the button has been held. Returns the gesture recognised NOW (None if none).
	ff::Gesture Update(Vec2 pos, float held_s, float ppm, bool guard = false);
	// Lift: the gesture recognised at release (ATTACK after a hold), else None.
	ff::Gesture Release(Vec2 pos, float ppm);
};

}  // namespace ffg
