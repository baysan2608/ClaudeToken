// FourfoldAudio logic island - manifest parser (engine-free).
#include "FFAManifest.h"

#include "ff/Json.h"

namespace ffa {
namespace {

float F(const ff::Dict& d, std::string_view k, float def) {
	const ff::Value& v = d.get(k);
	return v.is_number() ? v.as_f32() : def;
}

int I(const ff::Dict& d, std::string_view k, int def) {
	const ff::Value& v = d.get(k);
	return v.is_number() ? static_cast<int>(v.as_int()) : def;
}

bool B(const ff::Dict& d, std::string_view k, bool def) {
	const ff::Value& v = d.get(k);
	return (v.is_bool() || v.is_number()) ? v.as_bool() : def;
}

std::string S(const ff::Dict& d, std::string_view k, const std::string& def = std::string()) {
	const ff::Value& v = d.get(k);
	return v.is_string() ? v.as_string() : def;
}

std::vector<std::string> Strings(const ff::Value& v) {
	std::vector<std::string> out;
	if (v.is_string()) {
		out.push_back(v.as_string());
	} else if (const ff::Array* a = v.array_ptr()) {
		for (const ff::Value& e : *a)
			if (e.is_string()) out.push_back(e.as_string());
	}
	return out;
}

bool ParseOp(const std::string& k, Op& op) {
	static const struct { const char* name; Op op; } kOps[] = {
	    {"eq", Op::Eq},   {"ne", Op::Ne},   {"gt", Op::Gt},   {"gte", Op::Gte},       {"lt", Op::Lt},         {"lte", Op::Lte},
	    {"in", Op::In},   {"nin", Op::Nin}, {"has", Op::Has}, {"nhas", Op::Nhas},     {"truthy", Op::Truthy}, {"falsy", Op::Falsy}};
	for (const auto& e : kOps) {
		if (k == e.name) {
			op = e.op;
			return true;
		}
	}
	return false;
}

std::vector<Cond> Conds(const ff::Value& v) {
	std::vector<Cond> out;
	const ff::Array* a = v.array_ptr();
	if (a == nullptr) return out;
	for (const ff::Value& cv : *a) {
		const ff::Dict* d = cv.dict_ptr();
		if (d == nullptr) continue;
		Cond c;
		c.field = S(*d, "f");
		bool found = false;
		for (const auto& it : d->items()) {
			if (it.first == "f") continue;
			if (ParseOp(it.first, c.op)) {
				c.arg = it.second;
				found = true;
				break;
			}
		}
		if (found && !c.field.empty()) out.push_back(std::move(c));
	}
	return out;
}

SoundRef ParseSoundRef(const ff::Value& v) {
	SoundRef r;
	if (v.is_string()) {
		r.text = v.as_string();
		r.kind = r.text.find('{') != std::string::npos ? SoundRef::Kind::Template : SoundRef::Kind::Literal;
	} else if (v.is_array()) {
		r.list = Strings(v);
		r.kind = SoundRef::Kind::List;
	} else if (const ff::Dict* d = v.dict_ptr()) {
		r.kind = SoundRef::Kind::Table;
		r.table = S(*d, "table");
		r.key = Strings(d->get("key"));
		r.def = S(*d, "default");
	}
	return r;
}

Play ParsePlay(const ff::Dict& d) {
	Play p;
	p.sound = ParseSoundRef(d.get("sound"));
	p.at = Strings(d.get("at"));
	p.gain_db = F(d, "gain_db", 0.0f);
	p.pitch = F(d, "pitch", 1.0f);
	p.chance = F(d, "chance", 1.0f);
	p.limit = S(d, "limit");
	p.two_d = B(d, "two_d", false);
	p.delay_s = F(d, "delay_s", 0.0f);
	if (const ff::Dict* g = d.get("gain_map").dict_ptr()) {
		p.gain_map.valid = true;
		p.gain_map.field = S(*g, "f");
		const ff::Array in = g->get("in").as_array();
		const ff::Array db = g->get("db").as_array();
		if (in.size() >= 2 && db.size() >= 2) {
			p.gain_map.in_lo = in[0].as_f32();
			p.gain_map.in_hi = in[1].as_f32();
			p.gain_map.db_lo = db[0].as_f32();
			p.gain_map.db_hi = db[1].as_f32();
		} else {
			p.gain_map.valid = false;
		}
	}
	return p;
}

LoopRule ParseLoopRule(const ff::Dict& d) {
	LoopRule r;
	r.id = S(d, "id");
	r.when = Conds(d.get("when"));
	r.sound = S(d, "sound");
	r.gain_db = F(d, "gain_db", 0.0f);
	r.zone = S(d, "fade") == "zone";
	r.at = S(d, "at", "body");
	return r;
}

}  // namespace

const SoundDef* Manifest::Find(const std::string& name) const {
	const auto it = sounds.find(name);
	return it == sounds.end() ? nullptr : &it->second;
}

int Manifest::BusIndex(const std::string& bus) const {
	if (bus == "ui") return 1;
	if (bus == "ambience") return 2;
	return 0;
}

int Manifest::CountEventRules() const {
	int n = 0;
	for (const auto& e : events) n += static_cast<int>(e.second.size());
	return n;
}

bool Manifest::Parse(std::string_view text, std::string* error) {
	ff::Value root;
	ff::JsonError je;
	if (!ff::ParseJson(text, root, &je)) {
		if (error) *error = "json: " + je.message + " at line " + std::to_string(je.line);
		return false;
	}
	const ff::Dict* rd = root.dict_ptr();
	if (rd == nullptr || S(*rd, "schema") != "fourfold.sfx/1") {
		if (error) *error = "missing schema fourfold.sfx/1";
		return false;
	}
	*this = Manifest();

	// ---- mix
	if (const ff::Dict* m = rd->get("mix").dict_ptr()) {
		mix.global_voices = I(*m, "global_voices", mix.global_voices);
		mix.loop_voices = I(*m, "loop_voices", mix.loop_voices);
		mix.steal_fade_s = F(*m, "steal_fade_s", mix.steal_fade_s);
		if (const ff::Dict* g = m->get("bus_gain_db").dict_ptr()) {
			mix.bus_gain_db[0] = F(*g, "sfx", 0.0f);
			mix.bus_gain_db[1] = F(*g, "ui", 0.0f);
			mix.bus_gain_db[2] = F(*g, "ambience", 0.0f);
		}
		if (const ff::Dict* f = m->get("fade_db_per_s").dict_ptr()) {
			mix.fade.body_in = F(*f, "body_in", mix.fade.body_in);
			mix.fade.body_out = F(*f, "body_out", mix.fade.body_out);
			mix.fade.zone_in = F(*f, "zone_in", mix.fade.zone_in);
			mix.fade.zone_out = F(*f, "zone_out", mix.fade.zone_out);
			mix.fade.floor_db = F(*f, "floor_db", mix.fade.floor_db);
		}
		if (const ff::Dict* d = m->get("duck").dict_ptr()) {
			mix.duck.bus = S(*d, "bus", "ambience");
			mix.duck.db = F(*d, "db", mix.duck.db);
			mix.duck.attack_s = F(*d, "attack_s", mix.duck.attack_s);
			mix.duck.hold_s = F(*d, "hold_s", mix.duck.hold_s);
			mix.duck.release_s = F(*d, "release_s", mix.duck.release_s);
			mix.duck.triggers = Strings(d->get("triggers"));
		}
		if (const ff::Dict* l = m->get("limiters").dict_ptr()) {
			for (const auto& it : l->items()) {
				if (const ff::Dict* ld = it.second.dict_ptr()) {
					LimiterDef def;
					def.gap_s = F(*ld, "gap_s", 0.0f);
					def.per = S(*ld, "per");
					mix.limiters[it.first] = def;
				}
			}
		}
		if (const ff::Dict* a = m->get("attenuation").dict_ptr()) {
			for (const auto& it : a->items()) {
				if (const ff::Dict* ad = it.second.dict_ptr()) {
					AttenDef def;
					def.inner_cm = F(*ad, "inner_cm", def.inner_cm);
					def.max_cm = F(*ad, "max_cm", def.max_cm);
					def.db_at_max = F(*ad, "db_at_max", def.db_at_max);
					mix.attenuation[it.first] = def;
				}
			}
		}
	}

	// ---- sounds
	if (const ff::Dict* sd = rd->get("sounds").dict_ptr()) {
		for (const auto& it : sd->items()) {
			const ff::Dict* d = it.second.dict_ptr();
			if (d == nullptr) continue;
			SoundDef s;
			s.name = it.first;
			s.asset = S(*d, "asset");
			s.category = S(*d, "category", "element");
			s.attenuation = S(*d, "attenuation", "mid");
			s.bus = S(*d, "bus", "sfx");
			s.family = S(*d, "family");
			s.gain_db = F(*d, "gain_db", 0.0f);
			s.pitch_var = F(*d, "pitch_var", 0.0f);
			s.duration_s = F(*d, "duration_s", 1.0f);
			s.max_voices = I(*d, "max_voices", 2);
			s.priority = I(*d, "priority", 2);
			s.loop = B(*d, "loop", false);
			s.stinger = B(*d, "stinger", false);
			sounds[s.name] = std::move(s);
		}
	}

	// ---- tables
	if (const ff::Dict* td = rd->get("tables").dict_ptr()) {
		for (const auto& it : td->items())
			if (const ff::Dict* t = it.second.dict_ptr()) tables[it.first] = *t;
	}

	// ---- events
	if (const ff::Dict* ed = rd->get("events").dict_ptr()) {
		for (const auto& it : ed->items()) {
			const ff::Array* arr = it.second.array_ptr();
			if (arr == nullptr) continue;
			std::vector<Rule>& rules = events[it.first];
			for (const ff::Value& rv : *arr) {
				const ff::Dict* r = rv.dict_ptr();
				if (r == nullptr) continue;
				Rule rule;
				rule.id = S(*r, "id");
				rule.when = Conds(r->get("when"));
				rule.stop = B(*r, "stop", false);
				if (const ff::Array* pa = r->get("play").array_ptr())
					for (const ff::Value& pv : *pa)
						if (const ff::Dict* p = pv.dict_ptr()) rule.play.push_back(ParsePlay(*p));
				rules.push_back(std::move(rule));
			}
		}
	}

	// ---- loops
	if (const ff::Dict* ld = rd->get("loops").dict_ptr()) {
		if (const ff::Array* a = ld->get("bodies").array_ptr())
			for (const ff::Value& v : *a)
				if (const ff::Dict* d = v.dict_ptr()) body_loops.push_back(ParseLoopRule(*d));
		if (const ff::Array* a = ld->get("actors").array_ptr())
			for (const ff::Value& v : *a)
				if (const ff::Dict* d = v.dict_ptr()) actor_loops.push_back(ParseLoopRule(*d));
		if (const ff::Array* a = ld->get("events").array_ptr()) {
			for (const ff::Value& v : *a) {
				const ff::Dict* d = v.dict_ptr();
				if (d == nullptr) continue;
				EventLoopRule r;
				r.event = S(*d, "event");
				r.when = Conds(d->get("when"));
				r.sound = S(*d, "sound");
				r.key = Strings(d->get("key"));
				r.hold_s = F(*d, "hold_s", 0.3f);
				r.gain_db = F(*d, "gain_db", 0.0f);
				r.zone = S(*d, "fade") == "zone";
				r.at = S(*d, "at", "body");
				event_loops.push_back(std::move(r));
			}
		}
	}

	// ---- steps
	if (const ff::Dict* sd = rd->get("steps").dict_ptr()) {
		steps.min_speed = F(*sd, "min_speed", steps.min_speed);
		steps.cadence_base = F(*sd, "cadence_base", steps.cadence_base);
		steps.cadence_per_speed = F(*sd, "cadence_per_speed", steps.cadence_per_speed);
		const ff::Array g = sd->get("gain_db").as_array();
		if (g.size() >= 2) {
			steps.gain_lo = g[0].as_f32();
			steps.gain_hi = g[1].as_f32();
		}
		if (const ff::Dict* sg = sd->get("speed_gain").dict_ptr()) {
			const ff::Array in = sg->get("in").as_array();
			const ff::Array db = sg->get("db").as_array();
			if (in.size() >= 2 && db.size() >= 2) {
				steps.speed_in_lo = in[0].as_f32();
				steps.speed_in_hi = in[1].as_f32();
				steps.speed_db_lo = db[0].as_f32();
				steps.speed_db_hi = db[1].as_f32();
			}
		}
		steps.pitch_var = F(*sd, "pitch_var", steps.pitch_var);
		steps.def = S(*sd, "default", "stone");
		if (const ff::Dict* sf = sd->get("surfaces").dict_ptr())
			for (const auto& it : sf->items()) steps.surfaces[it.first] = Strings(it.second);
		if (const ff::Dict* ex = sd->get("surface_exact").dict_ptr())
			for (const auto& it : ex->items())
				if (it.second.is_string()) steps.exact[it.first] = it.second.as_string();
		if (const ff::Array* co = sd->get("surface_contains").array_ptr()) {
			for (const ff::Value& pv : *co) {
				const ff::Array pair = pv.as_array();
				if (pair.size() >= 2 && pair[0].is_string() && pair[1].is_string())
					steps.contains.emplace_back(pair[0].as_string(), pair[1].as_string());
			}
		}
		steps.cloth_every = I(*sd, "cloth_every", steps.cloth_every);
		steps.cloth_sounds = Strings(sd->get("cloth_sounds"));
	}

	// ---- ambience
	if (const ff::Dict* ad = rd->get("ambience").dict_ptr()) {
		ambience_fade_in_s = F(*ad, "fade_in_s", ambience_fade_in_s);
		if (const ff::Array* a = ad->get("beds").array_ptr()) {
			for (const ff::Value& v : *a) {
				if (const ff::Dict* d = v.dict_ptr()) {
					BedDef b;
					b.sound = S(*d, "sound");
					b.gain_db = F(*d, "gain_db", 0.0f);
					beds.push_back(std::move(b));
				}
			}
		}
		if (const ff::Array* a = ad->get("accents").array_ptr()) {
			for (const ff::Value& v : *a) {
				if (const ff::Dict* d = v.dict_ptr()) {
					AccentDef x;
					x.sounds = Strings(d->get("sounds"));
					x.min_gap_s = F(*d, "min_gap_s", x.min_gap_s);
					x.max_gap_s = F(*d, "max_gap_s", x.max_gap_s);
					x.gain_db = F(*d, "gain_db", 0.0f);
					x.pitch_var = F(*d, "pitch_var", 0.0f);
					accents.push_back(std::move(x));
				}
			}
		}
	}

	// ---- ui
	if (const ff::Dict* ud = rd->get("ui").dict_ptr())
		for (const auto& it : ud->items())
			if (it.second.is_string()) ui[it.first] = it.second.as_string();
	return true;
}

}  // namespace ffa
