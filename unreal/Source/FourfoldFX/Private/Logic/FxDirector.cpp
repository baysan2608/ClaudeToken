// FourfoldFX logic island - the FX director: frame loop, body view lifecycle, per-fighter effects.
// Event cues live in FxCues.cpp. Owner: stream `fx`.
#include "FxDirector.h"

#include "FxActorFx.h"
#include "FxMeshLib.h"

#include <algorithm>
#include <cmath>

namespace ffx {

namespace {

const ff::BodyView* FindPrevBody(const ff::Snapshot* s, int id) { return s ? s->FindBody(id) : nullptr; }

bool ActionCharging(const ff::ActorView& a) {
	return a.action.active && (a.action.phase == ff::ActionPhase::Charge || a.action.phase == ff::ActionPhase::Channel) &&
	       a.stun <= 0.0f;
}

}  // namespace

std::string_view StatusStyle(std::string_view s) {
	struct E {
		const char* k;
		const char* v;
	};
	static const E kMap[] = {{"burning", "flames"},  {"wet", "drip"},        {"chilled", "frost"},    {"frozen", "frost"},
	                         {"shocked", "crackle"}, {"charged", "crackle"}, {"blinded", "grit"},     {"rooted", "vines"},
	                         {"concealed", "veil"},  {"anchored", "dust"},   {"armored", "aura"},     {"muddy", "mud"},
	                         {"slowed", "mud"},      {"levitating", "lift"}, {"deafened", "ring"},    {"overcharged", "crackle"},
	                         {"icegrip", "frostfeet"}, {"windborne", "lift"}, {"flight", "lift"},     {"scalded", "steam"},
	                         {"fogbound", "fogpuff"}, {"lava_wade", "embers"}};
	for (const E& e : kMap)
		if (s == e.k) return e.v;
	return "";
}

FxDirector::FxDirector() : rng_(0x5eed1234u) {
	// shared mesh caches are process-wide: build them before the first gameplay frame
	static const bool kWarm = [] {
		meshlib::Prewarm();
		return true;
	}();
	(void)kWarm;
}
FxDirector::~FxDirector() = default;

void FxDirector::Reset() {
	views_.clear();
	dying_.clear();
	tierHint_.clear();
	charges_.clear();
	fireCharges_.clear();
	status_.clear();
	auras_.clear();
	transients_.clear();
	glides_.clear();
	fx_.Clear();
	world_.Reset();
	out_.Clear();
	lastTick_ = -1;
	stingT_ = 0.0f;
}

Ctx FxDirector::MakeCtx(const FxFrameIn& in, float dt) {
	return Ctx{cfg_, cfg_.Q(in.quality), in, out_, keys_, rng_, fx_, dt, time_};
}

const DrawList& FxDirector::Update(const FxFrameIn& in) {
	out_.Clear();
	stats_ = FxStats();
	const float dt = in.paused ? 0.0f : Clamp(in.dt, 0.0f, 0.1f);
	time_ += static_cast<double>(dt);
	Ctx c = MakeCtx(in, dt);
	stingT_ = MaxF(0.0f, stingT_ - dt);
	out_.env = EnvState();
	if (!in.curr) return out_;
	// events first: cues that spawn this frame are drawn this frame
	if (in.events) {
		for (const ff::Event& e : *in.events) {
			HandleEvent(c, e);
			++stats_.eventsHandled;
		}
	}
	// physics debris landing hard kicks up dust
	if (in.debrisImpacts) {
		for (const DebrisImpact& h : *in.debrisImpacts) {
			const float k = Sat((h.speed - 1.2f) / 5.0f) * Clamp(h.size / 0.3f, 0.4f, 1.6f);
			if (k > 0.05f) fx_.Dust(c, h.pos, Vec3(0.0f, 1.0f, 0.0f), 0.25f + 0.5f * k, cfg_.DustColor(Fam::Stone));
		}
	}
	SyncViews(c);
	UpdateActors(c);
	world_.Update(c);
	fx_.Step(c);
	SelectLights(out_.lights, MinI(c.q.maxLights, 4), in.cam.pos);
	stats_.items = static_cast<int>(out_.items.size());
	stats_.lights = static_cast<int>(out_.lights.size());
	stats_.fractures = static_cast<int>(out_.fractures.size());
	stats_.worldFx = world_.ActiveCount();
	stats_.oneShots = fx_.ActiveCount() + stats_.worldFx;
	stats_.views = static_cast<int>(views_.size());
	stats_.dyingViews = static_cast<int>(dying_.size());
	stats_.actorFx = static_cast<int>(charges_.size() + fireCharges_.size() + status_.size() + auras_.size() +
	                                  transients_.size() + glides_.size());
	for (const DrawItem& it : out_.items)
		if (it.mesh) stats_.triangles += it.mesh->NumTris();
	return out_;
}

// ============================================================================================== body views
void FxDirector::SyncViews(Ctx& c) {
	const ff::Snapshot& cur = *c.in.curr;
	const bool newTick = cur.tick != lastTick_;
	lastTick_ = cur.tick;
	std::map<int, bool> alive;
	for (const ff::BodyView& b : cur.bodies) {
		const ViewSel sel = SelectView(b);
		if (sel.kind == ViewKind::None) {
			// slick zones lie over their own puddle view; an untagged generic zone ("zone", the zone verb's default tag,
			// e.g. the extra one the slick move spawns) has nothing to show either
			const bool slick = b.form == ff::Form::Zone && (b.tag == "slick" || b.tag == "zone");
			if (b.form != ff::Form::Pool && !slick) {
				++stats_.unmappedBodies;
				stats_.unmapped.push_back(std::string(ff::kMatNames[static_cast<size_t>(b.mat)]) + "/" +
				                          std::string(ff::kFormNames[static_cast<size_t>(b.form)]) + "/" + b.tag);
			}
			continue;
		}
		alive[b.id] = true;
		const ff::BodyView* pb = FindPrevBody(c.in.prev, b.id);
		const Vec3 p = pb ? LerpV(pb->pos, b.pos, c.in.alpha) : b.pos;
		const BodyFrame f{b, pb, p, newTick};
		auto it = views_.find(b.id);
		if (it != views_.end() && it->second.view && it->second.view->sel != sel) {
			// representation changed (stone -> lava wave, sand wall -> glass ...): the old view fades out
			it->second.fade = 1.0f;
			dying_.push_back(std::move(it->second));
			views_.erase(it);
			it = views_.end();
		}
		if (it == views_.end()) {
			ViewSlot slot;
			slot.view = MakeView(sel);
			if (!slot.view) continue;
			slot.view->body = b.id;
			slot.view->lastPos = b.pos;
			if (auto th = tierHint_.find(b.id); th != tierHint_.end()) slot.view->tierHint = th->second;
			slot.view->Init(f, c);
			it = views_.emplace(b.id, std::move(slot)).first;
		}
		BodyView& v = *it->second.view;
		if (auto th = tierHint_.find(b.id); th != tierHint_.end()) v.tierHint = th->second;
		if (newTick && (sel.kind == ViewKind::Ribbon || sel.kind == ViewKind::Vine ||
		                (sel.kind == ViewKind::Cloud && sel.style == "slug"))) {
			v.trail.push_back(b.pos);
			if (v.trail.size() > 6) v.trail.erase(v.trail.begin());
		}
		v.Update(f, c);
		v.Draw(c, 1.0f);
		v.lastPos = p;
		// charged bodies crackle
		ViewSlot& slot = it->second;
		const bool wantAux = b.charge > kCrackleCharge && sel.kind != ViewKind::Crackle;
		if (wantAux && !slot.aux) {
			slot.aux = std::make_unique<Crackle>();
			const bool mast = b.form == ff::Form::Zone && b.tag == "rod";
			slot.aux->Setup(c, Crackle::Mode::Body, mast ? 0.3f : MaxF(b.radius, 0.2f), SeedOf(b.id));
		} else if (!wantAux && slot.aux) {
			slot.aux.reset();
		}
		if (slot.aux) {
			const bool mast = b.form == ff::Form::Zone && b.tag == "rod";
			slot.aux->SetTarget(mast ? Vec3(p.x, c.Ground(p) + 2.1f, p.z) : p);
			slot.aux->SetIntensity(Clamp(b.charge / 30.0f, 0.3f, 1.3f));
			slot.aux->Draw(c, 1.0f);
		}
	}
	// bodies that disappeared fade out
	for (auto it = views_.begin(); it != views_.end();) {
		if (!alive.count(it->first)) {
			it->second.fade = 1.0f;
			it->second.aux.reset();
			dying_.push_back(std::move(it->second));
			tierHint_.erase(it->first);
			it = views_.erase(it);
		} else {
			++it;
		}
	}
	for (size_t i = 0; i < dying_.size();) {
		ViewSlot& s = dying_[i];
		const float ft = s.view ? s.view->FadeTime() : 0.0f;
		s.fade = ft > 0.0f ? s.fade - c.dt / ft : 0.0f;
		if (s.fade <= 0.0f || !s.view) {
			dying_.erase(dying_.begin() + static_cast<std::ptrdiff_t>(i));
			continue;
		}
		s.view->Draw(c, s.fade);
		++i;
	}
}

Vec3 FxDirector::BodyPos(Ctx& c, int bodyId) const {
	const ff::BodyView* b = c.Body(bodyId);
	if (b) {
		const ff::BodyView* pb = FindPrevBody(c.in.prev, bodyId);
		return pb ? LerpV(pb->pos, b->pos, c.in.alpha) : b->pos;
	}
	// removed this tick (events run before the views sync) or recently (merged / exploded): its view's last position
	if (const auto it = views_.find(bodyId); it != views_.end() && it->second.view) return it->second.view->lastPos;
	for (const ViewSlot& s : dying_)
		if (s.view && s.view->body == bodyId) return s.view->lastPos;
	return Vec3();
}

bool FxDirector::BreakView(Ctx& c, int bodyId) {
	const auto it = views_.find(bodyId);
	return it != views_.end() && it->second.view && it->second.view->Break(c);
}

Fam FxDirector::BodyFam(Ctx& c, int bodyId, Fam fallback) const {
	const ff::BodyView* b = bodyId >= 0 ? c.Body(bodyId) : nullptr;
	return b ? FamOf(*b) : fallback;
}

Fam FxDirector::ActorFam(Ctx& c, int actorId) const {
	const ff::ActorView* a = actorId >= 0 ? c.Actor(actorId) : nullptr;
	return a ? FamOfSub(a->element, a->sub) : Fam::Wind;
}

void FxDirector::RingM(Ctx& c, const Vec3& pos, const Vec3& normal, float r0, float r1, float dur, Fam fam, const RingOpts& o) {
	fx_.Ring(c, pos, normal, r0, r1, dur, cfg_.MatColor(fam), o);
}

void FxDirector::BurstM(Ctx& c, const Vec3& pos, const Vec3& normal, float strength, ffx::Burst style) {
	fx_.Burst(c, pos, normal, strength, style);
}

// ============================================================================================== fighters
void FxDirector::ChargeFx::Draw(Ctx& c, const Vec3& hands, const Vec3& feet) {
	t += c.dt;
	const int tr = MaxI(tier, 0);
	const float ft = static_cast<float>(tr);
	const Color col = Linear(c.cfg.MatColor(fam));
	// T1+: hand ring (camera-facing), pulsing from T2
	const float grow = ft + frac * 0.6f;
	const float rr = 0.14f + 0.12f * grow;
	const float pul = 1.0f + (tr >= 2 ? 0.12f * std::sin(t * 14.0f) : 0.0f);
	Xform xr;
	xr.pos = hands;
	const float sq = rr * pul / 0.8f;
	xr.basis = c.Facing(hands).Scaled(sq, sq, 1.0f);
	DrawItem& ring = c.out.Add(keyRing, MatSlot::Ring, &meshlib::FaceQuad(), xr);
	ring.params.Set(PV::Color, col);
	ring.params.Set(P::Cover, 0.15f);
	ring.params.Set(P::Opacity, 0.5f + 0.12f * ft);
	ring.params.Set(P::Width, 0.08f + 0.02f * ft);
	ring.params.Set(P::Glow, 1.0f + 0.3f * ft + (glint > 0 ? 5.0f * c.cfg.flashScale * c.in.flashes : 0.0f));
	ring.params.Set(P::Radius, 0.8f);
	ring.params.Set(P::Phase, t);
	ring.sortPriority = 3;
	// motes: sparks drawn in toward the hands (energy gathering), more with the tier
	moteT -= c.dt;
	if (moteT <= 0.0f && tr >= 1) {
		moteT = 0.09f / (1.0f + 0.5f * ft);
		Particle* p = motes.Spawn();
		if (p) {
			Rng& r = motes.R();
			const Vec3 off = r.OnSphere() * r.Range(0.35f, 0.6f);
			p->p = hands + off;
			p->v = off * -2.2f;
			p->life = 0.28f;
			p->size0 = 0.035f;
			p->size1 = 0.01f;
			p->c0 = col.WithA(1.0f) * 1.0f;
			p->c1 = col.WithA(0.0f);
		}
	}
	ParticlePhysics ph;
	ph.drag = 1.5f;
	motes.Step(c.dt, ph);
	ParticleLook lk;
	lk.streak = true;
	lk.streakScale = 0.05f;
	lk.fadeIn = 0.2f;
	lk.fadeOut = 0.3f;
	motes.Build(motesMesh, c.in.cam.pos, c.in.cam.up, lk);
	motesMesh.Commit();
	DrawItem& m = c.out.Add(keyMotes, MatSlot::Spark, &motesMesh);
	m.params.Set(P::EmissiveScale, 2.0f + ft);
	m.sortPriority = 3;
	// T1+: ground ripple at the feet
	if (tr >= 1) {
		const float rp = std::fmod(t * (0.7f + 0.25f * ft), 1.0f);
		const float r2 = (0.3f + 0.9f * rp) * (0.6f + 0.4f * ft);
		Xform xg;
		xg.pos = feet + Vec3(0.0f, 0.03f, 0.0f);
		xg.basis = Basis::Identity().Scaled(r2 / 0.8f, 1.0f, r2 / 0.8f);
		DrawItem& g = c.out.Add(keyRipple, MatSlot::Ring, &meshlib::GroundQuad(), xg);
		g.params.Set(PV::Color, col);
		g.params.Set(P::Cover, 0.6f);
		g.params.Set(P::Opacity, (1.0f - rp) * (0.25f + 0.12f * ft));
		g.params.Set(P::Width, 0.03f + 0.012f * ft);
		g.params.Set(P::Radius, 0.8f);
		g.params.Set(P::Glow, 1.2f);
		g.params.Set(P::Phase, t);
	}
	// T2: a faint body rim; T3: the full aura shell
	if (tr >= 2) {
		const float big = tr >= 3 ? 1.0f : 0.82f;
		Xform xa;
		xa.pos = feet + Vec3(0.0f, 0.95f, 0.0f);
		const float wob = 1.0f + 0.04f * std::sin(t * 9.0f);
		xa.basis = Basis::Identity().Scaled(0.66f * big * wob, 1.08f * big * wob, 0.66f * big * wob);
		DrawItem& a = c.out.Add(keyAura, MatSlot::Shell, &meshlib::Sphere(2), xa);
		a.params.Set(PV::Color, col);
		a.params.Set(PV::Color2, col);
		a.params.Set(P::Rim, 2.2f);
		a.params.Set(P::Streak, -0.4f);
		a.params.Set(P::Pulse, 0.25f);
		a.params.Set(P::Phase, t);
		a.params.Set(P::Opacity, (tr >= 3 ? 0.34f : 0.14f) + 0.5f * pulse);
		a.params.Set(P::Glow, 1.1f);
		a.params.Set(P::Cover, 0.3f);
		a.sortPriority = 1;
	}
	pulse = MaxF(0.0f, pulse - c.dt * 2.5f);
	const float steady = tr >= 3 ? 0.55f + 0.2f * std::sin(t * 9.0f) : 0.0f;
	const float le = (1.6f * pulse + steady) * MaxF(c.in.flashes, 0.3f);
	if (le > 0.02f) c.out.Light(keyLight, hands, c.cfg.MatColor(fam), le, 4.0f, 2.2f);
	if (glint > 0) --glint;
}

void FxDirector::FireChargeFx::Draw(Ctx& c, const Vec3& hands, const Vec3& target, float charge01) {
	t = charge01;
	scroll += c.dt;
	const std::array<Color, 4>& cols = c.cfg.flame;
	if (!aim) {
		// steady teardrop flame cupped at the hands, growing with the charge
		const float size = 0.16f + 0.22f * charge01;
		Xform x;
		x.pos = hands - Vec3(0.0f, size * 0.25f, 0.0f);
		x.basis = Basis::Identity().Scaled(size * 0.45f, size * 1.6f, size * 0.45f);
		for (int layer = 0; layer < 2; ++layer) {
			const bool core = layer == 1;
			DrawItem& it = c.out.Add(core ? keyInner : keyOuter, MatSlot::Flame, &meshlib::Flame(), x);
			it.params.Set(P::Style, 1.0f);
			it.params.Set(P::Shape, 1.0f);
			it.params.Set(P::Core, core ? 1.0f : 0.0f);
			it.params.Set(P::Cover, core ? 0.25f : 0.8f);
			it.params.Set(P::Scroll, scroll);
			it.params.Set(P::Intensity, 0.8f + 0.6f * charge01);
			it.params.Set(P::Seed, core ? 0.53f : 0.17f);
			it.params.Set(PV::Color, Linear(cols[0]));
			it.params.Set(PV::Color2, Linear(cols[1]));
			it.params.Set(PV::Color3, Linear(cols[2]));
			it.params.Set(PV::Color4, Linear(cols[3]));
			it.sortPriority = core ? 3 : 2;
		}
		const float flick = 0.85f + 0.15f * std::sin(scroll * 21.0f) * std::sin(scroll * 7.7f);
		c.out.Light(keyLight, hands, Color(1.0f, 0.55f, 0.2f), (0.15f + 1.65f * charge01 * charge01) * flick, 4.0f, 1.8f);
		return;
	}
	// bolt ready: a thin crackling line that calms and brightens as the charge completes
	restrike -= c.dt;
	if (restrike <= 0.0f || line.Empty()) {
		restrike = Lerp(0.035f, 0.08f, charge01);
		BoltParams bp;
		bp.jitter = Lerp(0.09f, 0.03f, charge01);
		bp.maxLevels = MaxI(3, c.q.boltLevels - 2);
		bp.maxSegment = 0.35f;
		bp.width = 0.035f;
		bp.minBranches = 0;
		bp.maxBranches = 0;
		GenerateBolt({hands, target}, rng.Next(), bp, lines);
		line.Clear();
		BuildBoltMesh(line, lines, c.in.cam.pos);
		line.Commit();
	}
	DrawItem& it = c.out.Add(keyLine, MatSlot::Lightning, &line);
	it.params.Set(P::Age, 0.15f);
	it.params.Set(P::Intensity, 0.35f + 0.65f * charge01);
	it.params.Set(PV::Color, Linear(c.cfg.MatColor(Fam::Lightning)));
	it.sortPriority = 3;
	Xform xd;
	xd.pos = hands;
	const float s = 0.05f + 0.06f * charge01;
	xd.basis = c.Facing(hands).Scaled(s / 0.8f, s / 0.8f, 1.0f);
	DrawItem& dot = c.out.Add(keyDot, MatSlot::Ring, &meshlib::FaceQuad(), xd);
	dot.params.Set(PV::Color, Linear(Color(0.8f, 0.85f, 1.0f)));
	dot.params.Set(P::Style, 4.0f);   // filled glow dot
	dot.params.Set(P::Glow, 2.5f);
	dot.params.Set(P::Cover, 0.0f);
	dot.params.Set(P::Opacity, 0.9f);
	dot.params.Set(P::Radius, 0.8f);
	dot.params.Set(P::Width, 0.8f);
	dot.sortPriority = 3;
}

void FxDirector::Aura(Ctx& c, int actor, Fam fam, bool on) {
	auto it = auras_.find(actor);
	if (!on) {
		if (it != auras_.end()) it->second->on = false;   // fades out
		return;
	}
	if (it != auras_.end() && it->second->fam == fam && it->second->on) return;
	auto a = std::make_unique<AuraFx>();
	a->actor = actor;
	a->fam = fam;
	a->key = c.keys.New();
	a->fade = 0.0f;
	auras_[actor] = std::move(a);
}

void FxDirector::LashTransient(Ctx& c, int actor, const Vec3& dir, float range) {
	auto tr = std::make_unique<Transient>();
	tr->kind = Transient::Kind::Lash;
	tr->actor = actor;
	tr->dir = Norm(Flat(dir), Vec3(0.0f, 0.0f, 1.0f));
	tr->range = range;
	tr->dur = 0.28f;
	tr->key = c.keys.New();
	transients_.push_back(std::move(tr));
}

void FxDirector::DrawStream(Ctx& c, int actor, const Vec3& at, int body) {
	constexpr float kDrawHold = 0.2f;
	for (auto& tr : transients_)
		if (tr->kind == Transient::Kind::Draw && tr->actor == actor) {
			tr->dur = tr->t + kDrawHold;
			tr->at = at;
			tr->body = body;
			if (tr->t - tr->splash > 0.45f) {
				tr->splash = tr->t;
				fx_.Splash(c, at, Vec3(0.0f, 1.0f, 0.0f), 0.25f);
			}
			return;
		}
	auto tr = std::make_unique<Transient>();
	tr->kind = Transient::Kind::Draw;
	tr->actor = actor;
	tr->at = at;
	tr->body = body;
	tr->dur = kDrawHold;
	tr->key = c.keys.New();
	transients_.push_back(std::move(tr));
	fx_.Splash(c, at, Vec3(0.0f, 1.0f, 0.0f), 0.3f);
}

void FxDirector::DashTrail(Ctx& c, int actor, const Vec3& dir) {
	const ff::ActorView* a = c.Actor(actor);
	if (a && a->grounded) fx_.Dust(c, a->pos, Norm(Vec3(0.0f, 1.0f, 0.0f) - dir * 0.8f), 0.35f, cfg_.DustColor(Fam::Stone));
	auto tr = std::make_unique<Transient>();
	tr->kind = Transient::Kind::Dash;
	tr->actor = actor;
	tr->dur = 0.28f;
	tr->trail.Begin(c, 0.18f, Color(0.86f, 0.90f, 0.95f), 0.32f, 0.7f);
	transients_.push_back(std::move(tr));
}

void FxDirector::UpdateActors(Ctx& c) {
	const ff::Snapshot& cur = *c.in.curr;
	// ---- charge tiers: live while the action charges / channels
	for (auto it = charges_.begin(); it != charges_.end();) {
		const ff::ActorView* a = c.Actor(it->first);
		if (!a || !ActionCharging(*a)) {
			it = charges_.erase(it);
			continue;
		}
		ChargeFx& ch = *it->second;
		ch.Set(MaxI(ch.tier, a->charge.tier), a->charge.frac, ch.fam);
		ch.Draw(c, c.Hands(a->id), c.ActorPos(a->id));
		++it;
	}
	// ---- legacy fire charge flame / bolt aim line (from the action state, FxDirector._charge_visual)
	for (const ff::ActorView& a : cur.actors) {
		const bool charging = a.action.active && a.action.phase == ff::ActionPhase::Charge && a.action.id == "fire_attack";
		auto it = fireCharges_.find(a.id);
		if (!charging) {
			if (it != fireCharges_.end()) fireCharges_.erase(it);
			continue;
		}
		if (it == fireCharges_.end()) {
			auto fc = std::make_unique<FireChargeFx>();
			fc->Init(c, a.id);
			it = fireCharges_.emplace(a.id, std::move(fc)).first;
		}
		FireChargeFx& fc = *it->second;
		const bool bolt = a.action.data.get("bolt_ready", ff::Value(false)).truthy();
		if (bolt != fc.aim) {
			// switching flame <-> aim line: fresh keys so the renderer rebinds cleanly
			fc.Init(c, a.id);
			fc.aim = bolt;
		}
		const float t01 = Sat(a.action.total / 0.65f);
		Vec3 target;
		if (const ff::ActorView* tg = a.lock_target >= 0 ? c.Actor(a.lock_target) : nullptr) {
			target = c.Chest(tg->id);
		} else {
			target = c.Chest(a.id) + Vec3(std::sin(a.facing), 0.0f, std::cos(a.facing)) * 10.0f;
		}
		fc.Draw(c, c.Hands(a.id), target, t01);
	}
	// ---- statuses: event-started, kept in sync with the actor's status list (state is the truth)
	for (const ff::ActorView& a : cur.actors) {
		for (const ff::StatusView& s : a.statuses) {
			const std::string key = std::to_string(a.id) + ":" + s.name;
			if (!status_.count(key) && !StatusStyle(s.name).empty()) {
				ff::Dict d;
				d.set("actor", ff::Value(a.id));
				d.set("status", ff::Value(s.name));
				d.set("on", ff::Value(true));
				CueStatus(c, ff::Value(d), false);
			}
		}
	}
	for (auto it = status_.begin(); it != status_.end();) {
		StatusFx& s = *it->second;
		const ff::ActorView* a = c.Actor(s.actor);
		bool listed = false;
		if (a)
			for (const ff::StatusView& sv : a->statuses) listed = listed || sv.name == s.name;
		s.missing = listed ? 0.0f : s.missing + c.dt;
		if (!a || s.missing > 0.15f) {
			it = status_.erase(it);
			continue;
		}
		s.t += c.dt;
		s.phase += c.dt;
		const Vec3 feet = c.ActorPos(a->id);
		const Vec3 pelvis = c.Anchor(a->id, Bone::Pelvis);
		const Vec3 chest = c.Chest(a->id);
		if (s.flames) {
			Xform x;
			x.pos = pelvis;
			s.flames->Draw(c, x, 1.0f, false, a->id, Bone::Pelvis);
		}
		if (s.crackle) {
			s.crackle->SetTarget(pelvis);
			s.crackle->SetIntensity(0.8f);
			s.crackle->Draw(c, 1.0f, a->id);
		}
		if (s.vines) {
			Xform x;
			x.pos = feet;
			s.vines->Draw(c, x, 1.0f);
		}
		if (s.veil) {
			Xform x;
			x.pos = feet;
			s.veil->Draw(c, x, 1.0f);
		}
		if (s.keyShell != 0) {
			const bool frost = s.style == "frost";
			Xform x;
			x.pos = pelvis + Vec3(0.0f, frost ? -0.35f : 0.0f, 0.0f);
			const float hs = frost ? (s.name == "frozen" ? 1.0f : 0.6f) : 1.3f;
			const float r = frost ? 0.5f : 0.7f;
			x.basis = Basis::Identity().Scaled(r, r * hs, r);
			DrawItem& it2 = c.out.Add(s.keyShell, MatSlot::Shell, &meshlib::Sphere(2), x);
			const ShellLook& sl = cfg_.Shell(frost ? ShellStyle::Frost : ShellStyle::Aura);
			const Color col = frost ? (sl.color.a > 0.0f ? sl.color : cfg_.MatColor(sl.fam)) : cfg_.MatColor(Fam::Stone);
			it2.params.Set(PV::Color, Linear(col));
			it2.params.Set(PV::Color2, Linear(sl.coreColor.a > 0.0f ? sl.coreColor : col));
			it2.params.Set(P::Rim, sl.rim);
			it2.params.Set(P::Core, sl.core);
			it2.params.Set(P::Pulse, sl.pulse);
			it2.params.Set(P::Opacity, sl.opacity);
			it2.params.Set(P::Glow, sl.glow);
			it2.params.Set(P::Phase, s.phase);
			it2.params.Set(P::Cover, 0.5f);
			it2.params.Set(P::Height, hs);
			it2.sortPriority = 1;
			it2.attachActor = a->id;
			it2.attachBone = Bone::Pelvis;
		}
		// periodic puffs
		constexpr float kTick = 0.35f;
		if (s.t >= kTick) {
			s.t = 0.0f;
			const std::string& st = s.style;
			if (st == "drip") BurstM(c, feet + Vec3(0.0f, 0.9f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 0.15f, Burst::Water);
			else if (st == "grit") BurstM(c, chest + Vec3(0.0f, 0.45f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), 0.25f, Burst::Grit);
			else if (st == "mud") BurstM(c, feet, Vec3(0.0f, 1.0f, 0.0f), 0.2f, Burst::Dust);
			else if (st == "lift") {
				RingOpts o;
				o.cover = 0.4f;
				RingM(c, feet + Vec3(0.0f, 0.05f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), 0.2f, 0.8f, 0.35f, Fam::Wind, o);
			} else if (st == "frostfeet") BurstM(c, feet, Vec3(0.0f, 1.0f, 0.0f), 0.2f, Burst::Frost);
			else if (st == "steam") BurstM(c, chest, Vec3(0.0f, 1.0f, 0.0f), 0.22f, Burst::Steam);
			else if (st == "fogpuff") BurstM(c, chest, Vec3(0.0f, 1.0f, 0.0f), 0.25f, Burst::Mist);
			else if (st == "embers") BurstM(c, feet, Vec3(0.0f, 1.0f, 0.0f), 0.3f, Burst::Ember);
		}
		++it;
	}
	// ---- stance / guard auras follow their fighter
	for (auto it = auras_.begin(); it != auras_.end();) {
		AuraFx& au = *it->second;
		const ff::ActorView* a = c.Actor(au.actor);
		au.fade = au.on ? MinF(1.0f, au.fade + c.dt * 5.0f) : au.fade - c.dt * 4.0f;
		if (!a || au.fade <= 0.0f) {
			it = auras_.erase(it);
			continue;
		}
		au.phase += c.dt;
		Xform x;
		x.pos = c.Anchor(a->id, Bone::Pelvis);
		x.basis = Basis::Identity().Scaled(0.75f, 0.75f * 1.3f, 0.75f);
		const ShellLook& sl = cfg_.Shell(ShellStyle::Aura);
		DrawItem& d = c.out.Add(au.key, MatSlot::Shell, &meshlib::Sphere(2), x);
		d.params.Set(PV::Color, Linear(cfg_.MatColor(au.fam)));
		d.params.Set(PV::Color2, Linear(cfg_.MatColor(au.fam)));
		d.params.Set(P::Rim, sl.rim);
		d.params.Set(P::Pulse, sl.pulse);
		d.params.Set(P::Opacity, sl.opacity * au.fade);
		d.params.Set(P::Glow, sl.glow);
		d.params.Set(P::Phase, au.phase);
		d.params.Set(P::Cover, 0.3f);
		d.params.Set(P::Height, 1.3f);
		d.sortPriority = 1;
		d.attachActor = a->id;
		d.attachBone = Bone::Pelvis;
		++it;
	}
	// ---- transients: water lash arcs, water draw streams, dash trails
	for (size_t i = 0; i < transients_.size();) {
		Transient& tr = *transients_[i];
		tr.t += c.dt;
		const ff::ActorView* a = c.Actor(tr.actor);
		bool done = tr.t >= tr.dur || !a;
		if (tr.kind == Transient::Kind::Dash) {
			if (!done && a) tr.trail.Push(static_cast<float>(c.time), c.ActorPos(a->id) + Vec3(0.0f, 0.95f, 0.0f));
			if (done) tr.trail.End();
			if (!tr.trail.Draw(c)) {
				transients_.erase(transients_.begin() + static_cast<std::ptrdiff_t>(i));
				continue;
			}
			++i;
			continue;
		}
		if (done) {
			transients_.erase(transients_.begin() + static_cast<std::ptrdiff_t>(i));
			continue;
		}
		std::vector<Vec3> pts;
		std::vector<float> rad;
		const Vec3 o = c.Hands(tr.actor);
		if (tr.kind == Transient::Kind::Lash) {
			// an arc sweeping from the off side across the front
			const float x = tr.t / tr.dur;
			const Vec3 side = tr.dir.cross(Vec3(0.0f, 1.0f, 0.0f));
			const float reach = tr.range * (0.4f + 0.6f * std::sin(x * kFxPi));
			for (int k = 0; k < 8; ++k) {
				const float u = static_cast<float>(k) / 7.0f;
				const float ang = Lerp(-1.1f, 1.1f, Sat(x * 1.6f - (1.0f - u) * 0.6f));
				pts.push_back(o + (tr.dir * std::cos(ang) + side * std::sin(ang)) * (reach * u) + Vec3(0.0f, -0.4f * u, 0.0f));
				rad.push_back(Lerp(0.05f, 0.11f, u) * (1.0f - x * 0.5f));
			}
		} else {
			// water draw: a thin stream arcs from the source surface up into the held orb
			const ff::BodyView* b = c.Body(tr.body);
			const bool held = b && b->controller == tr.actor;
			if (!held) tr.dur = MinF(tr.dur, tr.t + 0.06f);
			const Vec3 end = held ? BodyPos(c, tr.body) : o;
			const Vec3 start = tr.at;
			const Vec3 d = end - start;
			Vec3 side = d.cross(Vec3(0.0f, 1.0f, 0.0f));
			side = side.length_squared() > 1e-6f ? Norm(side) : Vec3(1.0f, 0.0f, 0.0f);
			const float lift = 0.25f + 0.12f * d.length();
			const float rEnd = held ? Clamp(b->radius * 0.5f, 0.04f, 0.12f) : 0.04f;
			const float env = Sat(tr.t / 0.1f) * Sat((tr.dur - tr.t) / 0.06f);
			for (int k = 0; k < 10; ++k) {
				const float u = static_cast<float>(k) / 9.0f;
				const float arc = std::sin(u * kFxPi);
				pts.push_back(LerpV(start, end, u) + Vec3(0.0f, 1.0f, 0.0f) * (arc * lift) +
				              side * (std::sin(u * 6.0f - tr.t * 9.0f) * 0.05f * arc));
				const float r = 0.035f + 0.04f * (1.0f - Smooth(0.0f, 0.25f, u)) + (rEnd - 0.035f) * u * u;
				rad.push_back(MaxF(r * env * (1.0f + 0.3f * std::sin(u * 14.0f - tr.t * 22.0f)), 0.002f));
			}
		}
		tr.mesh.Clear();
		AppendTube(tr.mesh, pts, rad, 8, true, true, 2);
		tr.mesh.Commit();
		DrawItem& it = c.out.Add(tr.key, MatSlot::Water, &tr.mesh);
		it.params.Set(P::Shape, 0.0f);
		it.params.Set(P::Frozen, 0.0f);
		it.params.Set(P::Flow, tr.t * 2.0f);
		it.params.Set(P::Fade, 1.0f);
		++i;
	}
	// ---- glide / flight trails (soft wind ribbons behind gliding fighters)
	for (const ff::ActorView& a : cur.actors) {
		const bool want = a.gliding || a.flying;
		auto it = glides_.find(a.id);
		if (want && it == glides_.end()) {
			auto t = std::make_unique<Trail>();
			t->Begin(c, 0.5f, Color(0.86f, 0.90f, 0.95f), 0.32f, 0.7f);
			it = glides_.emplace(a.id, std::move(t)).first;
		}
		if (it == glides_.end()) continue;
		if (want) it->second->Push(static_cast<float>(c.time), c.ActorPos(a.id) + Vec3(0.0f, 0.95f, 0.0f));
		else it->second->End();
	}
	for (auto it = glides_.begin(); it != glides_.end();) {
		if (!c.Actor(it->first)) it->second->End();
		if (!it->second->Draw(c)) it = glides_.erase(it);
		else ++it;
	}
}

}  // namespace ffx
