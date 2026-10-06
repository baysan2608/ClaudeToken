// Fourfold game logic island - flick recognition (see FFGFlick.h).
#include "FFGFlick.h"

namespace ffg {

void FlickRecognizer::Begin(Vec2 pos) {
	origin = pos;
	fired = false;
	hot = ff::Gesture::None;
	last = ff::Gesture::None;
}

ff::Gesture FlickRecognizer::Classify(Vec2 d) {
	if (d.length_squared() < 1e-6f) return ff::Gesture::None;
	if (std::fabs(d.y) > std::fabs(d.x)) return d.y < 0.0f ? ff::Gesture::Up : ff::Gesture::Down;
	return ff::Gesture::Side;
}

ff::Gesture FlickRecognizer::Update(Vec2 pos, float held_s, float ppm, bool guard) {
	const Vec2 d = pos - origin;
	const float thr = ThresholdPx(ppm);
	ff::Gesture g = Classify(d);
	if (guard && g == ff::Gesture::Side) g = ff::Gesture::None;
	const float len = d.length();
	hot = len >= thr * kPreviewFraction ? g : ff::Gesture::None;
	if (len < thr || g == ff::Gesture::None) return ff::Gesture::None;
	if (guard) {
		last = g;
		origin = pos;
		hot = ff::Gesture::None;
		return g;
	}
	if (fired || held_s > kFlickWindowS) return ff::Gesture::None;
	fired = true;
	last = g;
	return g;
}

ff::Gesture FlickRecognizer::Release(Vec2 pos, float ppm) {
	if (fired) return ff::Gesture::None;
	const Vec2 d = pos - origin;
	if (d.length() < ThresholdPx(ppm)) return ff::Gesture::None;
	last = Classify(d);
	fired = true;
	return last;
}

}  // namespace ffg
