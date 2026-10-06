// FourfoldFX logic island - CPU particles. Owner: stream `fx`.
#include "FxParticles.h"

#include <cmath>

namespace ffx {

void ParticleSet::Reset(int capacity, uint32_t seed) {
	ps_.assign(static_cast<size_t>(MaxI(capacity, 0)), Particle());
	rng_.Seed(static_cast<uint64_t>(seed) * 0x2545F4914F6CDD1DULL + 1u);
}

Particle* ParticleSet::Spawn() {
	for (Particle& p : ps_)
		if (!p.alive) {
			p = Particle();
			p.alive = true;
			p.rnd = rng_.F01();
			return &p;
		}
	return nullptr;
}

void ParticleSet::Step(float dt, const ParticlePhysics& ph) {
	if (dt <= 0.0f) return;
	const float dragK = std::exp(-ph.drag * dt);
	for (Particle& p : ps_) {
		if (!p.alive) continue;
		p.age += dt;
		if (p.age >= p.life) {
			p.alive = false;
			continue;
		}
		p.v.y += (ph.buoyancy - ph.gravity) * dt;
		p.v += ph.wind * dt;
		p.v *= dragK;
		p.p += p.v * dt;
		p.rot += p.rotSpeed * dt;
		if (p.p.y < ph.groundY) {
			p.p.y = ph.groundY;
			if (p.v.y < 0.0f) {
				p.v.y = -p.v.y * ph.bounce;
				p.v.x *= 0.6f;
				p.v.z *= 0.6f;
				p.rotSpeed *= 0.5f;
			}
		}
	}
}

int ParticleSet::Alive() const {
	int n = 0;
	for (const Particle& p : ps_) n += p.alive ? 1 : 0;
	return n;
}

void ParticleSet::Build(MeshData& m, const Vec3& camPos, const Vec3& camUp, const ParticleLook& look) const {
	m.Clear();
	const Vec3 worldUp(0.0f, 1.0f, 0.0f);
	for (const Particle& p : ps_) {
		if (!p.alive) {
			// degenerate quad keeps the vertex / index counts fixed
			const int a = m.Add(Vec3(), Vec3(0.0f, 1.0f, 0.0f), Vec2(), Color(0.0f, 0.0f, 0.0f, 0.0f));
			m.Add(Vec3(), Vec3(0.0f, 1.0f, 0.0f), Vec2(), Color(0.0f, 0.0f, 0.0f, 0.0f));
			m.Add(Vec3(), Vec3(0.0f, 1.0f, 0.0f), Vec2(), Color(0.0f, 0.0f, 0.0f, 0.0f));
			m.Add(Vec3(), Vec3(0.0f, 1.0f, 0.0f), Vec2(), Color(0.0f, 0.0f, 0.0f, 0.0f));
			m.Tri(a, a, a);
			m.Tri(a, a, a);
			continue;
		}
		const float t = Sat(p.age / MaxF(p.life, 1e-4f));
		const float size = Lerp(p.size0, p.size1, 1.0f - (1.0f - t) * (1.0f - t));
		Color c = LerpC(p.c0, p.c1, t);
		c.a *= LifeFade(t, look.fadeIn, look.fadeOut);
		const Vec3 toCam = Norm(camPos - p.p, Vec3(0.0f, 0.0f, 1.0f));
		Vec3 right, up;
		if (look.streak && p.v.length_squared() > 1e-4f) {
			const Vec3 vd = Norm(p.v);
			right = Norm(vd.cross(toCam), Perp(toCam));
			const float len = MaxF(size, p.v.length() * look.streakScale);
			up = vd * (len * 0.5f);
			right = right * (size * 0.5f);
		} else if (look.alignUp) {
			right = Norm(worldUp.cross(toCam), Vec3(1.0f, 0.0f, 0.0f)) * (size * 0.5f);
			up = worldUp * (size * 0.5f);
		} else {
			Vec3 r0 = Norm(camUp.cross(toCam), Perp(toCam));
			Vec3 u0 = toCam.cross(r0);
			const float cr = std::cos(p.rot), sr = std::sin(p.rot);
			right = (r0 * cr + u0 * sr) * (size * 0.5f);
			up = (u0 * cr - r0 * sr) * (size * 0.5f);
		}
		float frame = 0.0f;
		if (look.frames > 1) {
			frame = look.frameByLife ? t * static_cast<float>(look.frames - 1)
			                         : std::fmod(p.frame0 + p.age * look.fps, static_cast<float>(look.frames));
			frame = Clamp(frame, 0.0f, static_cast<float>(look.frames - 1) - 0.001f + (look.frameByLife ? 0.0f : 0.999f));
		}
		const Vec2 f1(frame, p.extra), f2(t, p.rnd);
		const Vec3 n = toCam;
		const Vec3 tg = Norm(right);
		// corners: (u, v) with v = 0 at the top (texture convention); front = right x up = toward the camera
		const int a = m.Add(p.p - right - up, n, Vec2(0.0f, 1.0f), c, f1, f2, tg);
		const int b = m.Add(p.p + right - up, n, Vec2(1.0f, 1.0f), c, f1, f2, tg);
		const int d = m.Add(p.p + right + up, n, Vec2(1.0f, 0.0f), c, f1, f2, tg);
		const int e = m.Add(p.p - right + up, n, Vec2(0.0f, 0.0f), c, f1, f2, tg);
		m.Quad(a, b, d, e);
	}
}

}  // namespace ffx
