// FourfoldAudio logic island - rule engine implementation (engine-free).
#include "FFARules.h"

#include "ff/Types.h"

#include <algorithm>
#include <cmath>

namespace ffa {

struct RuleEngine::Ctx {
	const ff::Event* e = nullptr;
	const IWorld* w = nullptr;
	const ff::Dict* fields = nullptr;     // loop evaluation: the entity's field dict (e == nullptr)
	float speed = 0.0f;
};

namespace {

constexpr float kChestY = 1.25f;
constexpr float kHandY = 1.2f;
constexpr float kHandFwd = 0.55f;

const char* const kSubMat[4][4] = {{"stone", "metal", "sand", "magma"},
                                   {"water", "ice", "mist", "plant"},
                                   {"flame", "blue", "lightning", "blast"},
                                   {"wind", "vortex", "vacuum", "sound"}};

std::string NameOf(std::string_view sv) { return std::string(sv); }

std::string KeyString(const ff::Value& v) {
	switch (v.type()) {
		case ff::Value::Type::String: return v.as_string();
		case ff::Value::Type::Int: return std::to_string(v.as_int());
		case ff::Value::Type::Float: {
			const double f = v.as_float();
			if (f == std::floor(f) && std::fabs(f) < 1e9) return std::to_string(static_cast<long long>(f));
			return std::to_string(f);
		}
		case ff::Value::Type::Bool: return v.as_bool() ? "true" : "false";
		default: return std::string();
	}
}

int IntOf(const ff::Value& v, int def) { return v.is_number() ? static_cast<int>(v.as_int()) : def; }

ff::Vec3 ActorChest(const ff::ActorView& a) { return ff::Vec3(a.pos.x, a.pos.y + kChestY, a.pos.z); }

ff::Vec3 ActorHand(const ff::ActorView& a) {
	return ff::Vec3(a.pos.x + std::sin(a.facing) * kHandFwd, a.pos.y + kHandY, a.pos.z + std::cos(a.facing) * kHandFwd);
}

float HSpeed(const ff::Vec3& v) { return std::sqrt(v.x * v.x + v.z * v.z); }

ff::Dict ActionFields(const ff::ActionView& a) {
	ff::Dict d;
	d.set("active", a.active);
	d.set("id", a.id);
	d.set("phase", NameOf(ff::kActionPhaseNames[static_cast<int>(a.phase)]));
	d.set("slot", NameOf(ff::SlotName(a.slot)));
	d.set("tier", a.tier);
	d.set("heavy", a.heavy);
	d.set("element", a.element);
	d.set("sub", a.sub);
	d.set("data", a.data);
	return d;
}

ff::Dict BodyFields(const ff::BodyView& b) {
	ff::Dict d;
	d.set("id", b.id);
	d.set("mat", NameOf(ff::kMatNames[static_cast<int>(b.mat)]));
	d.set("form", NameOf(ff::kFormNames[static_cast<int>(b.form)]));
	d.set("phase", NameOf(ff::kPhaseNames[static_cast<int>(b.phase)]));
	d.set("tag", b.tag);
	d.set("fx_mat", b.fx_mat);
	d.set("liquid", b.liquid);
	d.set("temp", b.temp);
	d.set("charge", b.charge);
	d.set("power", b.power);
	d.set("spin", b.spin);
	d.set("mass", b.mass);
	d.set("radius", b.radius);
	d.set("zone_radius", b.zone_radius);
	d.set("age", b.age);
	d.set("tier", b.tier);
	d.set("sub", b.sub);
	d.set("owner", b.owner);
	d.set("controller", b.controller);
	d.set("on_ground", b.on_ground);
	d.set("static_body", b.static_body);
	d.set("$speed", b.vel.length());
	return d;
}

ff::Dict ActorFields(const ff::ActorView& a) {
	ff::Dict d;
	d.set("id", a.id);
	d.set("element", std::string(ElementName(a.element)));
	d.set("sub", a.sub);
	d.set("surface", a.surface);
	d.set("grounded", a.grounded);
	d.set("guarding", a.guarding);
	d.set("gliding", a.gliding);
	d.set("flying", a.flying);
	d.set("in_water", a.in_water);
	d.set("stun", a.stun);
	d.set("stance", a.stance);
	d.set("health", a.health);
	d.set("is_player", a.is_player);
	d.set("action", ff::Value(ActionFields(a.action)));
	d.set("$speed", HSpeed(a.vel));
	ff::Array st;
	for (const ff::StatusView& s : a.statuses) st.append(s.name);
	d.set("$status", ff::Value(st));
	return d;
}

std::string JoinKey(const std::vector<std::string>& parts) {
	std::string k;
	for (size_t i = 0; i < parts.size(); ++i) {
		if (i) k += '_';
		k += parts[i];
	}
	return k;
}

}  // namespace

RuleEngine::RuleEngine(const Manifest* manifest, uint32_t seed) : m_(manifest), rng_(seed) {}

ff::Value RuleEngine::DictPath(const ff::Value& root, const std::string& path) {
	ff::Value cur = root;
	size_t start = 0;
	while (start <= path.size()) {
		const size_t dot = path.find('.', start);
		const std::string part = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
		const ff::Dict* d = cur.dict_ptr();
		if (d == nullptr) return ff::Value();
		cur = d->get(part);
		if (dot == std::string::npos) break;
		start = dot + 1;
	}
	return cur;
}

bool RuleEngine::EvalCond(const Cond& c, const ff::Value& v) {
	switch (c.op) {
		case Op::Eq: return v == c.arg;
		case Op::Ne: return !(v == c.arg);
		case Op::Gt: return v.is_number() && c.arg.is_number() && v.as_float() > c.arg.as_float();
		case Op::Gte: return v.is_number() && c.arg.is_number() && v.as_float() >= c.arg.as_float();
		case Op::Lt: return v.is_number() && c.arg.is_number() && v.as_float() < c.arg.as_float();
		case Op::Lte: return v.is_number() && c.arg.is_number() && v.as_float() <= c.arg.as_float();
		case Op::In: {
			const ff::Array* a = c.arg.array_ptr();
			return a != nullptr && a->has(v);
		}
		case Op::Nin: {
			const ff::Array* a = c.arg.array_ptr();
			return a == nullptr || !a->has(v);
		}
		case Op::Has:
		case Op::Nhas: {
			bool has = false;
			if (const ff::Array* a = v.array_ptr()) has = a->has(c.arg);
			else if (const ff::Dict* d = v.dict_ptr()) has = c.arg.is_string() && d->has(c.arg.as_string());
			else if (v.is_string() && c.arg.is_string()) has = v.as_string().find(c.arg.as_string()) != std::string::npos;
			return c.op == Op::Has ? has : !has;
		}
		case Op::Truthy: return v.truthy();
		case Op::Falsy: return !v.truthy();
	}
	return false;
}

std::string RuleEngine::ClassMat(const std::string& cls) const {
	if (m_ == nullptr || cls.empty()) return std::string();
	const auto it = m_->tables.find("class_mat");
	if (it == m_->tables.end()) return std::string();
	const ff::Value& v = it->second.get(cls);
	return v.is_string() ? v.as_string() : std::string();
}

std::string RuleEngine::BodyMat(const IWorld& w, int id) const {
	const ff::BodyView* b = id >= 0 ? w.Body(id) : nullptr;
	return b != nullptr ? b->fx_mat : std::string();
}

std::string RuleEngine::ClassifySurface(const std::string& surface) const {
	if (m_ == nullptr) return "stone";
	const StepsDef& s = m_->steps;
	const auto it = s.exact.find(surface);
	if (it != s.exact.end()) return it->second;
	for (const auto& pr : s.contains)
		if (surface.find(pr.first) != std::string::npos) return pr.second;
	return s.def;
}

ff::Value RuleEngine::Derived(const std::string& name, const Ctx& ctx) const {
	if (ctx.e == nullptr) return ff::Value();
	const ff::Value& data = ctx.e->data;
	const IWorld& w = *ctx.w;
	const int actor_id = IntOf(data["actor"], -1);
	const ff::ActorView* actor = actor_id >= 0 ? w.Actor(actor_id) : nullptr;
	if (name == "$player") return ff::Value(actor_id >= 0 && actor_id == w.PlayerId());
	if (name == "$involves_player") {
		const int pid = w.PlayerId();
		return ff::Value(pid >= 0 && (IntOf(data["threat_actor"], -2) == pid || IntOf(data["counter_actor"], -2) == pid));
	}
	if (name == "$el") {
		int el = IntOf(data["element"], -1);
		if (el < 0 && actor != nullptr) el = actor->element;
		return ff::Value(std::string(ElementName(el)));
	}
	if (name == "$weight") {
		const bool heavy = data["heavy"].truthy() || (data["tier"].is_number() && data["tier"].as_int() >= 2) ||
		                   data["result"] == ff::Value("knockdown") || (data["damage"].is_number() && data["damage"].as_float() >= 15.0);
		return ff::Value(heavy ? "heavy" : "light");
	}
	if (name == "$body_mat") return ff::Value(BodyMat(w, IntOf(data["body"], -1)));
	if (name == "$threat_mat") {
		std::string mat = data["threat"].is_string() ? ClassMat(data["threat"].as_string()) : std::string();
		if (mat.empty()) mat = BodyMat(w, IntOf(data["threat_body"], -1));
		return ff::Value(mat.empty() ? std::string("stone") : mat);
	}
	if (name == "$counter_mat") {
		std::string mat = data["counter"].is_string() ? ClassMat(data["counter"].as_string()) : std::string();
		if (mat.empty()) mat = BodyMat(w, IntOf(data["counter_body"], -1));
		if (mat.empty()) {
			const int ca = IntOf(data["counter_actor"], -1);
			const ff::ActorView* a = ca >= 0 ? w.Actor(ca) : nullptr;
			if (a != nullptr && a->element >= 0 && a->element < 4) mat = kSubMat[a->element][std::clamp(a->sub, 0, 3)];
		}
		return ff::Value(mat.empty() ? std::string("wind") : mat);
	}
	if (name == "$surface") return ff::Value(actor != nullptr ? ClassifySurface(actor->surface) : std::string("stone"));
	if (name == "$speed") return ff::Value(data["speed"].is_number() ? data["speed"].as_float() : 0.0);
	return ff::Value();
}

ff::Value RuleEngine::Field(const std::string& name, const Ctx& ctx) const {
	if (name.empty()) return ff::Value();
	if (ctx.fields != nullptr) return DictPath(ff::Value(*ctx.fields), name);
	if (name[0] == '$') return Derived(name, ctx);
	return DictPath(ctx.e->data, name);
}

bool RuleEngine::AllConds(const std::vector<Cond>& conds, const Ctx& ctx) const {
	for (const Cond& c : conds)
		if (!EvalCond(c, Field(c.field, ctx))) return false;
	return true;
}

std::string RuleEngine::RoundRobin(const std::string& key, const std::vector<std::string>& list) {
	if (list.empty()) return std::string();
	if (list.size() == 1) return list[0];
	uint32_t& n = rr_[key];
	const std::string& s = list[n % list.size()];
	++n;
	return s;
}

std::string RuleEngine::Resolve(const SoundRef& ref, const Ctx& ctx) {
	switch (ref.kind) {
		case SoundRef::Kind::None: return std::string();
		case SoundRef::Kind::Literal: return ref.text;
		case SoundRef::Kind::List: return RoundRobin("list:" + JoinKey(ref.list), ref.list);
		case SoundRef::Kind::Template: {
			std::string out;
			const std::string& t = ref.text;
			size_t i = 0;
			while (i < t.size()) {
				if (t[i] == '{') {
					const size_t j = t.find('}', i);
					if (j == std::string::npos) return std::string();
					const std::string v = KeyString(Field(t.substr(i + 1, j - i - 1), ctx));
					if (v.empty()) return std::string();
					out += v;
					i = j + 1;
				} else {
					out += t[i++];
				}
			}
			return out;
		}
		case SoundRef::Kind::Table: {
			if (m_ == nullptr) return std::string();
			const auto it = m_->tables.find(ref.table);
			if (it == m_->tables.end()) return ref.def;
			std::vector<std::string> parts;
			for (const std::string& f : ref.key) parts.push_back(KeyString(Field(f, ctx)));
			const std::string key = JoinKey(parts);
			const ff::Value& v = it->second.get(key);
			if (v.is_string()) return v.as_string();
			if (const ff::Array* a = v.array_ptr()) {
				std::vector<std::string> list;
				for (const ff::Value& e : *a)
					if (e.is_string()) list.push_back(e.as_string());
				return RoundRobin("table:" + ref.table + ":" + key, list);
			}
			return ref.def;
		}
	}
	return std::string();
}

bool RuleEngine::ResolvePos(const std::string& src, const Ctx& ctx, ff::Vec3& out) const {
	const ff::Value& data = ctx.e->data;
	const IWorld& w = *ctx.w;
	if (src == "actor" || src == "hand" || src == "feet") {
		const int id = IntOf(data["actor"], -1);
		const ff::ActorView* a = id >= 0 ? w.Actor(id) : nullptr;
		if (a == nullptr) return false;
		out = src == "actor" ? ActorChest(*a) : (src == "hand" ? ActorHand(*a) : a->pos);
		return true;
	}
	if (src == "body") {
		const int id = IntOf(data["body"], -1);
		const ff::BodyView* b = id >= 0 ? w.Body(id) : nullptr;
		if (b == nullptr) return false;
		out = b->pos;
		return true;
	}
	if (src == "pos" || src == "at") {
		const ff::Value& v = data[src];
		if (!v.is_vec3()) return false;
		out = v.as_vec3();
		return true;
	}
	if (src == "path_end") {
		const ff::Array* a = data["path"].array_ptr();
		if (a == nullptr || a->empty()) return false;
		const ff::Value last = a->back();
		if (!last.is_vec3()) return false;
		out = last.as_vec3();
		return true;
	}
	return false;
}

void RuleEngine::ProcessEvent(const ff::Event& e, const IWorld& world, std::vector<PlayRequest>& plays, std::vector<HoldRequest>& holds) {
	if (m_ == nullptr) return;
	Ctx ctx;
	ctx.e = &e;
	ctx.w = &world;

	const auto it = m_->events.find(e.type);
	if (it != m_->events.end()) {
		for (const Rule& rule : it->second) {
			if (!AllConds(rule.when, ctx)) continue;
			for (const Play& p : rule.play) {
				if (p.chance < 1.0f && rng_.Next01() > p.chance) continue;
				const std::string name = Resolve(p.sound, ctx);
				if (name.empty()) continue;
				if (m_->Find(name) == nullptr) {
					++missing_;
					last_missing_ = name;
					continue;
				}
				PlayRequest r;
				r.sound = name;
				r.gain_db = p.gain_db;
				r.pitch = p.pitch;
				r.delay_s = p.delay_s;
				r.limit = p.limit;
				r.two_d = p.two_d;
				const int lk = IntOf(e.data["actor"], IntOf(e.data["body"], -1));
				r.limit_key = lk;
				if (p.gain_map.valid) {
					const ff::Value& fv = Field(p.gain_map.field, ctx);
					if (fv.is_number())
						r.gain_db += MapRange(fv.as_f32(), p.gain_map.in_lo, p.gain_map.in_hi, p.gain_map.db_lo, p.gain_map.db_hi);
				}
				if (!p.two_d) {
					for (const std::string& src : p.at) {
						if (ResolvePos(src, ctx, r.pos)) {
							r.has_pos = true;
							break;
						}
					}
					if (!r.has_pos) r.two_d = true;       // nothing to place it at: play it at the listener
				}
				plays.push_back(std::move(r));
			}
			if (rule.stop) break;
		}
	}

	for (const EventLoopRule& lr : m_->event_loops) {
		if (lr.event != e.type || !AllConds(lr.when, ctx)) continue;
		if (m_->Find(lr.sound) == nullptr) continue;
		HoldRequest h;
		h.sound = lr.sound;
		h.gain_db = lr.gain_db;
		h.hold_s = lr.hold_s;
		h.zone = lr.zone;
		std::string key = lr.key.empty() ? lr.sound : lr.key[0];
		for (size_t i = 1; i < lr.key.size(); ++i) key += "_" + KeyString(Field(lr.key[i], ctx));
		h.key = "e:" + key;
		if (!ResolvePos(lr.at, ctx, h.pos) && !ResolvePos("actor", ctx, h.pos) && !ResolvePos("body", ctx, h.pos)) continue;
		holds.push_back(std::move(h));
	}
}

void RuleEngine::EvaluateLoops(const ff::Snapshot& curr, const ff::Snapshot* prev, float alpha, std::vector<LoopWant>& out) {
	if (m_ == nullptr) return;
	Ctx ctx;
	for (const ff::BodyView& b : curr.bodies) {
		if (m_->body_loops.empty()) break;
		const ff::Dict f = BodyFields(b);
		ctx.fields = &f;
		ff::Vec3 pos = b.pos;
		if (prev != nullptr) {
			if (const ff::BodyView* pb = prev->FindBody(b.id)) pos = pb->pos.lerp(b.pos, alpha);
		}
		for (const LoopRule& r : m_->body_loops) {
			if (!AllConds(r.when, ctx)) continue;
			if (m_->Find(r.sound) == nullptr) continue;
			LoopWant w;
			w.key = "b" + std::to_string(b.id) + ":" + r.id;
			w.sound = r.sound;
			w.pos = pos;
			w.gain_db = r.gain_db;
			w.zone = r.zone;
			out.push_back(std::move(w));
		}
	}
	for (const ff::ActorView& a : curr.actors) {
		if (m_->actor_loops.empty()) break;
		const ff::Dict f = ActorFields(a);
		ctx.fields = &f;
		ff::Vec3 feet = a.pos;
		if (prev != nullptr) {
			if (const ff::ActorView* pa = prev->FindActor(a.id)) feet = pa->pos.lerp(a.pos, alpha);
		}
		for (const LoopRule& r : m_->actor_loops) {
			if (!AllConds(r.when, ctx)) continue;
			if (m_->Find(r.sound) == nullptr) continue;
			LoopWant w;
			w.key = "a" + std::to_string(a.id) + ":" + r.id;
			w.sound = r.sound;
			w.pos = r.at == "feet" ? feet : ff::Vec3(feet.x, feet.y + kChestY, feet.z);
			w.gain_db = r.gain_db;
			w.zone = r.zone;
			out.push_back(std::move(w));
		}
	}
}

void StepTracker::Update(const Manifest& m, const RuleEngine& rules, const ff::Snapshot& snap, float dt, std::vector<StepOut>& out) {
	const StepsDef& s = m.steps;
	for (const ff::ActorView& a : snap.actors) {
		const float spd = HSpeed(a.vel);
		if (!a.grounded || spd <= s.min_speed || a.stun > 0.0f || a.flying || a.gliding) {
			acc_[a.id] = 0.0f;
			continue;
		}
		float& acc = acc_[a.id];
		acc += dt * (s.cadence_base + spd * s.cadence_per_speed);
		if (acc < 1.0f) continue;
		acc = 0.0f;
		std::string cls = rules.ClassifySurface(a.surface);
		auto it = s.surfaces.find(cls);
		if (it == s.surfaces.end() || it->second.empty()) it = s.surfaces.find(s.def);
		if (it == s.surfaces.end() || it->second.empty()) continue;
		const std::vector<std::string>& variants = it->second;
		int pick = rng_.Index(static_cast<int>(variants.size()));
		const auto last = last_.find(cls + ":" + std::to_string(a.id));
		if (variants.size() > 1 && last != last_.end() && last->second == pick) pick = (pick + 1 + rng_.Index(static_cast<int>(variants.size()) - 1)) % static_cast<int>(variants.size());
		last_[cls + ":" + std::to_string(a.id)] = pick;
		StepOut so;
		so.actor = a.id;
		so.sound = variants[static_cast<size_t>(pick)];
		so.pos = a.pos;
		so.gain_db = rng_.Range(s.gain_lo, s.gain_hi) + MapRange(spd, s.speed_in_lo, s.speed_in_hi, s.speed_db_lo, s.speed_db_hi);
		so.pitch_var = s.pitch_var;
		int& n = count_[a.id];
		++n;
		if (s.cloth_every > 0 && !s.cloth_sounds.empty() && n % s.cloth_every == 0 && spd > 3.0f) {
			so.cloth = true;
			so.cloth_sound = s.cloth_sounds[static_cast<size_t>(rng_.Index(static_cast<int>(s.cloth_sounds.size())))];
		}
		out.push_back(std::move(so));
	}
}

}  // namespace ffa
