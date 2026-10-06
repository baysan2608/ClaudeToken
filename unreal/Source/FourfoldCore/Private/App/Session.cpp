// Fourfold core - ff::Session: the facade the Unreal modules use. Port of the engine-free parts of game/game.gd
// (scenario load, the 60 Hz tick with PlayerController / rival AI / Lab scripts, KO and round reset, practice dummies,
// the boulder launcher, lava vents, mastery challenges, Lab actions) plus game/actors/autoplay.gd's duel / soak modes.
#include "ff/Session.h"

#include "AI/AiBrain.h"
#include "App/HudBuilder.h"
#include "App/PlayerController.h"
#include "App/Progression.h"
#include "App/Scenarios.h"
#include "App/SnapshotBuilder.h"
#include "Combat/Acts.h"
#include "Combat/Moves.h"
#include "Data/GameData.h"
#include "Lab/LabCombos.h"
#include "Lab/LabScript.h"
#include "Lab/LabSession.h"
#include "Lab/LabTuning.h"
#include "Lab/MatrixQuery.h"
#include "Lab/MoveListData.h"
#include "Lab/SpawnCatalog.h"
#include "Sim/CombatWorld.h"
#include "Sim/Interactions.h"
#include "Sim/Status.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"
#include "Util/Rng.h"

#include <cmath>
#include <map>

namespace ff {

namespace {
constexpr double kDummyReviveS = 2.0;   // a downed practice target stays down this long, then stands at full health
constexpr double kKoResetS = 2.5;       // a downed player or rival: the knockdown plays, then the round resets

std::string ss_mastery_toast(const Dict& ch) {
	std::string unlock = dstr(ch, "unlock");
	for (char& c : unlock)
		if (c == '_') c = ' ';
	return "Mastered " + dstr(ch, "title") + " \xE2\x80\x94 " + unlock + " now in your Free Spar kit";
}

Dict ss_params(const LabSpawnParams& p) {
	Dict d;
	if (p.mass >= 0.0f) d.set("mass", static_cast<double>(p.mass));
	if (p.speed >= 0.0f) d.set("speed", static_cast<double>(p.speed));
	if (p.temp >= 0.0f) d.set("temp", static_cast<double>(p.temp));
	if (p.tier >= 0) d.set("tier", p.tier);
	return d;
}

LabParamSpec ss_spec(const Dict& specs, const char* k) {
	LabParamSpec s;
	if (!specs.has(k)) return s;
	const Array a = specs.get(k).as_array();
	s.present = true;
	s.def = static_cast<float>(vnum(a.get(0)));
	s.min = static_cast<float>(vnum(a.get(1)));
	s.max = static_cast<float>(vnum(a.get(2)));
	return s;
}
}  // namespace

struct Session::Impl {
	SessionConfig cfg;
	Progression progress;
	Progression auto_progress;      // autoplay captures never touch the player's real save
	std::string scenario_id;
	Dict scen_def;
	ScenarioOptions opts;
	std::unique_ptr<CombatWorld> world;
	ActorState* player = nullptr;
	ActorState* opponent = nullptr;
	std::unique_ptr<AiBrain> ai;
	PlayerController pc;
	ActorIntent player_intent;
	std::map<int, ActorIntent> intents;
	Dict launcher;
	double launch_t = 0.0;
	int launch_i = 0;
	int launch_body = -1;
	int challenge_n = 0;
	std::map<int, double> down;
	LabSession lab;
	LabState lab_view;
	std::unique_ptr<LabScript> player_script;
	std::unique_ptr<LabScript> rival_script;
	std::vector<Dict> lab_queue;
	ActorIntent lab_idle;
	ComboTracker tracker;
	bool combo_demo = false;
	// autoplay (title-screen attract mode / soak)
	std::string autoplay;
	std::unique_ptr<AiBrain> duel_ai;
	uint64_t duel_seed = 77;
	Rng soak_rng{42};
	double auto_t = 0.0;
	double soak_next = 0.0;
	InputFrame auto_f;
	// outputs
	Snapshot snap;
	ArenaView arena;
	std::vector<Event> events;
	std::string challenge_text;
	uint64_t seed = 1;

	Progression& prog() { return autoplay.empty() ? progress : auto_progress; }

	void push_app(const std::string& type, Dict data) {
		if (!cfg.record_events) return;
		Event e;
		e.type = type;
		e.tick = world ? world->tick : 0;
		e.data = Value(std::move(data));
		events.push_back(std::move(e));
	}
	void toast(const std::string& text, const char* kind = "info") { push_app("app_toast", D({{"text", text}, {"kind", kind}})); }

	void push_sim(const std::vector<Dict>& evs) {
		if (!cfg.record_events) return;
		for (const Dict& d : evs) {
			Event e;
			e.type = dstr(d, "type");
			e.tick = d.get("tick").as_int(world ? world->tick : 0);
			e.data = Value(d);
			events.push_back(std::move(e));
		}
	}

	void rebuild_snapshot() {
		if (!world) return;
		SnapshotBuilder::Roles r;
		r.player_id = player ? player->id : -1;
		r.rival_id = opponent ? opponent->id : -1;
		r.player_ai = autoplay == "duel";
		r.rival_ai = ai != nullptr && lab.ai_enabled && rival_script == nullptr;
		SnapshotBuilder::build(*world, r, snap);
	}

	// ------------------------------------------------------------------ scenario
	void reset_lab_for_scenario(const Dict& d) {
		const Dict od = ddict(d, "opponent");
		lab.ai_enabled = dbool(od, "ai_default", true);
		lab.ai_kit = "mixed";
		lab.ai_sub = -1;
		lab.ai_drill = "";
		if (dbool(d, "spar", false)) lab.ai_preset = prog().spar_difficulty;
		player_script.reset();
		rival_script.reset();
		lab_queue.clear();
		tracker.stop();
	}

	void update_challenge_text() {
		const Dict ch = ddict(scen_def, "challenge");
		if (ch.empty()) challenge_text.clear();
		else if (prog().is_done(dstr(ch, "id"))) challenge_text = "\xE2\x9C\x93 " + dstr(ch, "title");
		else challenge_text = dstr(ch, "text") + "  " + itos(mini(challenge_n, dint(ch, "count"))) + "/" + itos(dint(ch, "count"));
	}

	void bind_autoplay() {
		if (autoplay != "soak" && autoplay != "duel") return;
		auto_progress.lab_mode = true;
		player->kit = auto_progress.kit();
		player->elements = {true, true, true, true};
		if (autoplay == "duel") {
			const Dict master = D({{"preset", "master"}, {"elements", A({0, 1, 2, 3})}});
			if (ai) ai->configure(master);
			duel_ai = std::make_unique<AiBrain>(*world, *player, Dict(), duel_seed);
			duel_ai->configure(master);
			duel_seed += 1;
		}
	}

	void load_scenario(const std::string& id) {
		Scenarios::Built r = Scenarios::build(id, prog(), seed);
		scenario_id = dstr(r.def, "id");
		scen_def = r.def;
		ai.reset();
		duel_ai.reset();
		world = std::move(r.world);
		player = r.player;
		opponent = r.opponent;
		if (opponent != nullptr) {
			ai = std::make_unique<AiBrain>(*world, *opponent, r.ai_cfg, seed + 7);
			if (r.ai_cfg.has("preset")) ai->configure(r.ai_cfg);
		}
		reset_lab_for_scenario(r.def);
		launcher = r.launcher;
		launch_t = 1.5;
		launch_i = 0;
		launch_body = -1;
		challenge_n = 0;
		down.clear();
		intents.clear();
		player_intent.clear();
		if (opts.player_element >= 0 && opts.player_element < 4) {
			player->element = opts.player_element;
			player->elements[static_cast<size_t>(opts.player_element)] = true;
		}
		if (opts.player_sub >= 0 && opts.player_sub < 4) player->subs[static_cast<size_t>(clampi(player->element, 0, 3))] = opts.player_sub;
		bind_autoplay();
		update_challenge_text();
		if (autoplay.empty()) progress.last_scenario = scenario_id;
		arena = SnapshotBuilder::arena(*world);
		push_app("app_scenario_loaded", D({{"id", scenario_id}, {"title", dstr(scen_def, "title")}}));
		rebuild_snapshot();
	}

	// ------------------------------------------------------------------ autoplay
	InputFrame duel_frame(float yaw) {
		if (!duel_ai || duel_ai->w != world.get()) {
			// The scenario reloaded (KO reset): rebind the player's brain to the new world.
			duel_ai = std::make_unique<AiBrain>(*world, *player, Dict(), duel_seed);
			duel_ai->configure(D({{"preset", "master"}, {"elements", A({0, 1, 2, 3})}}));
			duel_seed += 1;
		}
		if (ai && !ai->planner) ai->configure(D({{"preset", "master"}, {"elements", A({0, 1, 2, 3})}}));
		const ActorIntent& it = duel_ai->think(Sim::DT);
		return PlayerController::frame_from_intent(it, yaw);
	}

	InputFrame soak_frame() {
		InputFrame& f = auto_f;
		auto_t += Sim::DT;
		f.ClearEdges();
		f.tech_aim_active = false;
		if (auto_t < soak_next) return f;
		soak_next = auto_t + soak_rng.randf_range(0.15f, 0.6f);
		const float mx = soak_rng.randf_range(-1.0f, 1.0f);
		const float my = soak_rng.randf_range(-1.0f, 1.0f);
		f.move = Vec2(mx, my).limit_length(1.0f);
		const float cx = soak_rng.randf_range(-0.05f, 0.05f);
		f.cam_delta = Vec2(cx, 0.0f);
		const float r = soak_rng.randf();
		const bool guarding = f.guard_held;
		f.attack_held = false;
		f.guard_held = false;
		f.evade_held = false;
		if (f.tech_held && soak_rng.randf() < 0.5f) {
			f.tech_held = false;
			f.tech_released = true;
		}
		if (r < 0.06f) {
			f.element_select = soak_rng.randi_range(0, 3);
		} else if (r < 0.12f) {
			f.sub_select = soak_rng.randi_range(0, 3);
		} else if (r < 0.36f) {
			f.attack_pressed = true;
			f.attack_held = soak_rng.randf() < 0.35f;
			if (soak_rng.randf() < 0.5f) f.attack_gesture = static_cast<Gesture>(soak_rng.randi_range(1, 3));
		} else if (r < 0.46f) {
			f.guard_pressed = true;
			f.guard_held = true;
		} else if (r < 0.52f && guarding) {
			f.guard_held = true;
			f.guard_gesture = soak_rng.randf() < 0.5f ? Gesture::Up : Gesture::Down;
		} else if (r < 0.62f) {
			f.evade_pressed = true;
			f.evade_held = soak_rng.randf() < 0.4f;
		} else if (r < 0.82f) {
			f.tech_pressed = true;
			f.tech_held = true;
			if (soak_rng.randf() < 0.4f) {
				const float ax = soak_rng.randf_range(-1.0f, 1.0f);
				const float ay = soak_rng.randf_range(0.2f, 1.0f);
				f.tech_aim = Vec2(ax, ay).limit_length(1.0f);
				f.tech_aim_active = true;
			}
		}
		return f;
	}

	// ------------------------------------------------------------------ tick
	const ActorIntent& rival_intent() {
		if (rival_script) {
			lab_idle.clear();
			LabScript::apply_dict(lab_idle, rival_script->next());
			opponent->lock_target = player->id;
			if (rival_script->is_done()) rival_script.reset();
			return lab_idle;
		}
		if (ai && lab.ai_enabled) return ai->think(Sim::DT);
		lab_idle.clear();
		return lab_idle;
	}

	void scenario_tick() {
		CombatWorld& w = *world;
		if (!launcher.empty()) {
			launch_t -= Sim::DT;
			MatBody* lb = w.get_body(launch_body);
			if (lb != nullptr && lb->controller >= 0) {
				// Seized off the plinth during the telegraph: it is the holder's stone now and the launcher reloads.
				lb->static_body = false;
				lb->max_life = Sim::REMNANT_LIFETIME;
				lb = nullptr;
				launch_body = -1;
				launch_t = dnum(launcher, "interval");
			}
			if (lb == nullptr && launch_t <= 0.6) {
				// Telegraph: the next stone appears on the plinth 0.6 s before it fires.
				const Array masses = darr(launcher, "masses");
				const double m = masses.empty() ? 20.0 : vnum(masses.get(static_cast<size_t>(launch_i) % masses.size()));
				launch_i += 1;
				lb = w.spawn_body(Mat::Stone, Form::Chunk, m, dvec(launcher, "pos"), "launcher");
				lb->static_body = true;
				launch_body = lb->id;
				w.mass_ledger.ground_taken += m;
			}
			if (lb != nullptr && lb->static_body && launch_t <= 0.0) {
				lb->static_body = false;
				const double spd = lb->mass < 100.0 ? 15.0 : 11.0;
				lb->vel = ActEarth::launch_vel(lb->pos, player->chest(), spd);
				lb->attack_id = w.new_attack_id();
				lb->attack_owner = -1;
				lb->damage = 12.0 * std::sqrt(lb->mass / 20.0);
				lb->balance_damage = minf(90.0, 24.0 * std::sqrt(lb->mass / 20.0));
				lb->max_life = Sim::REMNANT_LIFETIME;
				w.emit("launch", D({{"actor", -1}, {"body", lb->id}, {"speed", spd}, {"heavy", lb->mass > 40.0}}));
				launch_body = -1;
				launch_t = dnum(launcher, "interval");
			}
		}
		// Vents keep training lava molten until someone draws it solid.
		for (size_t i = 0; i < w.bodies.size(); ++i) {
			MatBody& b = *w.bodies[i];
			if (b.alive && b.origin == "vent" && b.phase != Phase::Solid && b.liquid > 0.0 && b.liquid < 1.0 && b.controller < 0) {
				const double e = 55.0 * Sim::DT;
				const double used = Thermal::heat(b, e);
				w.ledger.generated += used;
			}
		}
	}

	void lab_tick() {
		if (lab_queue.empty()) return;
		std::vector<Dict> keep;
		std::vector<Dict> due;
		for (Dict& q : lab_queue) {
			q.set("t", dnum(q, "t") - Sim::DT);
			if (dnum(q, "t") <= 0.0) due.push_back(q);
			else keep.push_back(q);
		}
		lab_queue = keep;
		for (const Dict& q : due) lab_spawn(dstr(q, "id"), ddict(q, "params"), dbool(q, "launch"));
	}

	// Returns true when the scenario was reloaded (KO reset).
	bool ko_tick() {
		for (auto& ap : world->actors) {
			ActorState& a = *ap;
			if (a.health > 0.0) {
				down.erase(a.id);
				continue;
			}
			double t = down.count(a.id) ? down[a.id] : 0.0;
			if (t == 0.0) {
				world->_stagger(a, "knockdown", a.is_dummy ? kDummyReviveS : kKoResetS + 0.5, Dict());
				if (!a.is_dummy) {
					push_app("app_ko", D({{"actor", a.id}}));
					toast(&a == player ? "You're down" : a.name + " down", "warning");
				}
			}
			t += Sim::DT;
			down[a.id] = t;
			if (a.is_dummy && t >= kDummyReviveS) {
				a.health = Sim::HEALTH_MAX;
				down.erase(a.id);
			} else if (!a.is_dummy && t >= kKoResetS) {
				push_sim(world->take_events());
				const int n = challenge_n;
				load_scenario(scenario_id);
				challenge_n = n;
				update_challenge_text();
				push_app("app_round_reset", D({{"reason", "ko"}}));
				return true;
			}
		}
		return false;
	}

	void challenges(const std::vector<Dict>& evs) {
		const Dict ch = ddict(scen_def, "challenge");
		if (ch.empty() || prog().is_done(dstr(ch, "id"))) return;
		const int before = challenge_n;
		const std::string cid = dstr(ch, "id");
		const int pid = player->id;
		if (cid == "m_stone_reader") {
			for (const Dict& e : evs)
				if (dstr(e, "type") == "perfect_deflect" && dint(e, "actor", -1) == pid && dstr(e, "verb", "") == "redirect") challenge_n += 1;
		} else if (cid == "m_storm_eye") {
			for (const Dict& e : evs) {
				if (dstr(e, "type") == "conduct" && dint(e, "actor", -1) == pid) {
					// A caster standing in the connected water is a victim too; only the others count.
					const Array victims = darr(e, "victims");
					if (static_cast<int>(victims.size()) - (victims.has(pid) ? 1 : 0) >= 2) challenge_n += 1;
				}
			}
		} else if (cid == "m_return") {
			for (const Dict& e : evs)
				if (dstr(e, "type") == "lightning_redirect" && dint(e, "actor", -1) == pid) challenge_n += 1;
		} else if (cid == "m_cold_hands") {
			for (const Dict& e : evs) {
				if (dstr(e, "type") == "transform" && dstr(e, "to") == "rock") {
					MatBody* b = world->get_body(dint(e, "body", -1));
					if (b != nullptr && b->origin == "vent" && b->last_actor == pid) challenge_n += 1;
				}
			}
		} else if (cid == "m_updraft") {
			const ArenaSolid* hl = world->arena.solid_named("high_ledge");
			if (hl != nullptr && player->grounded && player->pos.y > 1.7f && player->pos.x < hl->max.x && player->pos.z < hl->max.z) challenge_n = 1;
		}
		if (challenge_n != before) {
			update_challenge_text();
			const bool done = challenge_n >= dint(ch, "count");
			push_app("app_challenge", D({{"id", cid}, {"progress", mini(challenge_n, dint(ch, "count"))}, {"count", dint(ch, "count")}, {"done", done}}));
			if (done) {
				prog().complete(cid, dstr(ch, "unlock"));
				toast(ss_mastery_toast(ch), "mastery");
				update_challenge_text();
			}
		}
	}

	void step(const InputFrame& input, float yaw) {
		if (!world) return;
		InputFrame f = input;
		if (autoplay == "duel") f = duel_frame(yaw);
		else if (autoplay == "soak") f = soak_frame();
		if (player_script) {
			LabScript::apply_dict(f, player_script->next());
			if (player_script->is_done()) player_script.reset();
		}
		player_intent = pc.build(f, yaw);
		intents[player->id] = player_intent;
		if (opponent != nullptr) intents[opponent->id] = rival_intent();
		scenario_tick();
		lab_tick();
		lab.pre_step(*world);
		world->step(intents);
		lab.post_step(player);
		if (ko_tick()) return;   // round reset: the fresh scenario runs from the next tick
		const std::vector<Dict> evs = world->take_events();
		challenges(evs);
		if (tracker.is_running()) {
			const size_t step0 = tracker.step;
			const ComboTracker::State st0 = tracker.state;
			const std::string fail0 = tracker.fail_reason;
			tracker.update(Sim::DT, evs);
			const std::string cid = dstr(tracker.combo, "id");
			if (tracker.state == ComboTracker::State::Success && st0 != ComboTracker::State::Success)
				push_app("app_combo", D({{"id", cid}, {"state", "success"}, {"step", static_cast<int64_t>(tracker.step)}}));
			else if (!tracker.fail_reason.empty() && tracker.fail_reason != fail0)
				push_app("app_combo", D({{"id", cid}, {"state", "fail"}, {"step", static_cast<int64_t>(tracker.step)}}));
			else if (tracker.step != step0)
				push_app("app_combo", D({{"id", cid}, {"state", "step"}, {"step", static_cast<int64_t>(tracker.step)}}));
		}
		push_sim(evs);
		rebuild_snapshot();
	}

	// ------------------------------------------------------------------ Lab actions
	Vec3 lab_aim_point() const {
		const Vec3 p = player->pos + player->forward() * 5.0;
		return V3(p.x, world->arena.ground_height(p.x, p.z, player->pos.y + 0.5), p.z);
	}

	bool lab_spawn(const std::string& id, const Dict& params, bool launch, std::string* msg = nullptr) {
		if (!world) return false;
		ActorState* owner = SpawnCatalog::owner_of(*world, *player);
		SpawnCatalog::SpawnResult res = SpawnCatalog::spawn(*world, id, params, *player, owner, lab_aim_point(), launch);
		if (res.script) {
			rival_script = std::move(res.script);
			if (opponent != nullptr && owner == opponent) {
				opponent->elements = {true, true, true, true};
				SpawnCatalog::top_up(*world, *opponent);
			}
		}
		const std::string m = res.ok ? "spawned " + id : res.msg;
		if (msg) *msg = m;
		push_app("app_lab", D({{"action", "spawn"}, {"ok", res.ok}, {"message", m}}));
		return res.ok;
	}

	void lab_clear() {
		for (MatBody* b : world->body_list())
			if (b->alive && b->form != Form::Pool && b != world->pool) world->decay_body(*b, "lab_clear");
		player_script.reset();
		rival_script.reset();
	}

	void lab_heal() {
		for (auto& a : world->actors) {
			a->health = Sim::HEALTH_MAX;
			a->balance = Sim::BALANCE_MAX;
			a->stun = 0.0;
			SpawnCatalog::top_up(*world, *a);
		}
	}

	void lab_apply_ai() {
		if (opponent == nullptr) return;
		const Dict c = lab.ai_config();
		const std::vector<int> els = lab.ai_elements();
		if (!els.empty()) {
			opponent->elements = {false, false, false, false};
			for (int e : els) opponent->elements[static_cast<size_t>(e)] = true;
			opponent->element = els[0];
			if (lab.ai_sub >= 0 && els.size() == 1) opponent->subs[static_cast<size_t>(opponent->element)] = lab.ai_sub;
		}
		if (ai) ai->configure(c);
	}

	void lab_start_combo(const std::string& id) {
		const Dict c = LabCombos::find(id);
		if (c.empty()) return;
		lab_clear();
		lab_heal();
		double t = 0.8;
		for (const Value& sp : darr(ddict(c, "setup"), "spawn")) {
			lab_queue.push_back(D({{"t", t}, {"id", dstr(sp, "id")}, {"params", sp.get("params", Dict())}, {"launch", dbool(sp, "launch", false)}}));
			t += 0.4;
		}
		const Dict first = darr(c, "steps").get(0).as_dict();
		player_script = std::make_unique<LabScript>();
		player_script->frames.push_back(D({{"element_select", dint(first, "el")}, {"sub_select", dint(first, "sub")}}));
		tracker.start(c, player->id);
		toast("Combo: " + dstr(c, "name"));
		push_app("app_combo", D({{"id", id}, {"state", "started"}, {"step", 0}}));
	}

	void lab_demo_combo(const std::string& id) {
		const Dict c = LabCombos::find(id);
		if (c.empty()) return;
		lab_start_combo(id);
		auto s = std::make_unique<LabScript>();
		size_t tick = 40;
		for (const Value& stv : darr(c, "steps")) {
			const Dict st = stv.as_dict();
			if (dstr(st, "kind", "move") == "shape") {
				s->_at(tick).set("attack_pressed", true);
				tick += 20;
				continue;
			}
			const LabScript one = LabScript::for_move(dint(st, "el"), dint(st, "sub"), dstr(st, "slot"), dint(st, "tier", 0));
			const size_t hold_extra = static_cast<size_t>(std::ceil(dnum(st, "hold", 0.0) / Sim::DT));
			for (size_t i = 0; i < one.frames.size(); ++i) {
				Dict& dst = s->_at(tick + i);
				for (const auto& kv : one.frames[i]) dst.set(kv.first, kv.second);
			}
			if (hold_extra > 0 && dstr(st, "slot") == "tech")
				for (size_t i = one.frames.size(); i < hold_extra + LabScript::LEAD; ++i) s->_at(tick + i).set("tech_held", true);
			tick += std::max(one.frames.size() + 12, hold_extra + 14);
		}
		player_script = std::move(s);
	}
};

// ============================================================================ Session

Session::Session(const SessionConfig& cfg) : impl_(std::make_unique<Impl>()) {
	impl_->cfg = cfg;
	impl_->seed = cfg.seed;
	Moves::ensure_ready();
	impl_->arena = SnapshotBuilder::arena(CombatWorld(cfg.seed));
}

Session::~Session() = default;

bool Session::DataOk(std::string* error) {
	const bool ok = GameData::ok();
	if (error) *error = GameData::error();
	if (ok) Moves::ensure_ready();
	return ok;
}

std::vector<PracticeItem> Session::PracticeItems() const { return Scenarios::practice_items(impl_->progress); }

bool Session::LoadScenario(const std::string& id, const ScenarioOptions& opts) {
	Impl& m = *impl_;
	if (id == "__lab_mode") {
		SetLabMode(!m.progress.lab_mode);
		m.toast(std::string("Lab mode ") + (m.progress.lab_mode ? "on" : "off"));
		return true;
	}
	if (!Scenarios::has(id)) return false;
	m.opts = opts;
	if (Scenarios::is_spar_difficulty(opts.spar_difficulty)) m.progress.spar_difficulty = opts.spar_difficulty;
	if (!opts.spar_kit.empty() && (Scenarios::is_spar_kit(opts.spar_kit) || opts.spar_kit.find('/') != std::string::npos))
		m.progress.spar_kit = opts.spar_kit;
	m.autoplay = opts.autoplay;
	m.auto_progress = Progression();
	m.auto_progress.spar_difficulty = m.progress.spar_difficulty;
	m.auto_progress.spar_kit = m.progress.spar_kit;
	m.auto_t = 0.0;
	m.soak_next = 0.0;
	m.auto_f = InputFrame();
	m.load_scenario(id);
	return true;
}

void Session::ResetScenario() {
	if (impl_->scenario_id.empty()) return;
	impl_->load_scenario(impl_->scenario_id);
}

const std::string& Session::ScenarioId() const { return impl_->scenario_id; }

void Session::Step(const InputFrame& player_input, float camera_yaw) { impl_->step(player_input, camera_yaw); }

int64_t Session::Tick() const { return impl_->world ? impl_->world->tick : 0; }
float Session::TimeScale() const { return static_cast<float>(impl_->lab.time_scale); }
bool Session::Frozen() const { return impl_->lab.frozen; }

int Session::TakeStepRequests() {
	const int n = impl_->lab._steps;
	impl_->lab._steps = 0;
	return n;
}

const Snapshot& Session::GetSnapshot() const { return impl_->snap; }

void Session::TakeEvents(std::vector<Event>& out) {
	for (Event& e : impl_->events) out.push_back(std::move(e));
	impl_->events.clear();
}

const ArenaView& Session::Arena() const { return impl_->arena; }

HudModel Session::BuildHud() const {
	Impl& m = *impl_;
	HudBuilder::Context c;
	c.world = m.world.get();
	c.player = m.player;
	c.player_intent = &m.player_intent;
	c.scenario_title = dstr(m.scen_def, "title");
	c.objective = dstr(m.scen_def, "objective");
	c.challenge_text = m.challenge_text;
	return HudBuilder::build(c);
}

std::vector<std::string> Session::DebugLines() const {
	if (!impl_->world) return {};
	return HudBuilder::debug_lines(*impl_->world, impl_->scenario_id, impl_->ai ? impl_->ai->debug_state : std::string());
}

std::vector<MoveInfo> Session::ListMoves(int element, int sub) const {
	std::vector<MoveInfo> out;
	for (int s = 0; s < kNumSlots; ++s) {
		const std::string id = Moves::resolve(element, sub, std::string(SlotName(static_cast<Slot>(s))));
		if (id.empty() || !Moves::defs().has(id)) continue;
		MoveInfo mi = MoveListData::move_info(id);
		mi.slot = static_cast<Slot>(s);
		out.push_back(mi);
	}
	return out;
}

MoveInfo Session::GetMove(const std::string& id) const { return MoveListData::move_info(id); }

std::string Session::ResolveMove(int element, int sub, Slot slot) const { return Moves::resolve(element, sub, std::string(SlotName(slot))); }

void Session::SetRivalAi(const std::string& preset, const std::string& kit, int sub) {
	Impl& m = *impl_;
	m.lab.ai_preset = preset;
	m.lab.ai_kit = kit;
	m.lab.ai_sub = sub;
	m.lab_apply_ai();
}

std::string Session::AiDebug() const { return impl_->ai ? impl_->ai->debug_state : std::string(); }

const LabState& Session::Lab() const {
	impl_->lab_view = impl_->lab.to_state();
	return impl_->lab_view;
}

void Session::SetLab(const LabState& s) {
	Impl& m = *impl_;
	const bool ai_changed = s.ai_preset != m.lab.ai_preset || s.ai_kit != m.lab.ai_kit || s.ai_sub != m.lab.ai_sub || s.ai_drill != m.lab.ai_drill;
	m.lab.from_state(s);
	if (ai_changed) m.lab_apply_ai();
}

void Session::LabRequestStep(int n) { impl_->lab.request_step(n); }

std::vector<LabSpawnEntry> Session::LabSpawnEntries() const {
	std::vector<LabSpawnEntry> out;
	for (const Dict& e : SpawnCatalog::entries()) {
		LabSpawnEntry v;
		v.id = dstr(e, "id");
		v.label = dstr(e, "label");
		v.group = dstr(e, "group");
		v.build = dstr(e, "build");
		v.inert_only = dbool(e, "inert_only");
		v.launch_only = dbool(e, "launch_only");
		const Dict specs = SpawnCatalog::param_specs(e);
		v.mass = ss_spec(specs, "mass");
		v.speed = ss_spec(specs, "speed");
		v.temp = ss_spec(specs, "temp");
		v.tier = ss_spec(specs, "tier");
		v.summary = SpawnCatalog::describe(e, Dict());
		out.push_back(v);
	}
	return out;
}

bool Session::LabSpawn(const std::string& entry_id, const LabSpawnParams& params, bool launch, std::string* message) {
	if (!impl_->world) return false;
	const bool ok = impl_->lab_spawn(entry_id, ss_params(params), launch, message);
	impl_->rebuild_snapshot();
	return ok;
}

void Session::LabTry(int element, int sub, Slot slot, int tier) {
	impl_->player_script = std::make_unique<LabScript>(LabScript::for_move(element, sub, std::string(SlotName(slot)), tier));
}

void Session::LabClear() {
	if (!impl_->world) return;
	impl_->lab_clear();
	impl_->rebuild_snapshot();
}

void Session::LabHeal() {
	if (!impl_->world) return;
	impl_->lab_heal();
	impl_->rebuild_snapshot();
}

std::vector<LabCombo> Session::LabCombos() const {
	std::vector<LabCombo> out;
	for (const Dict& c : LabCombos::all()) out.push_back(LabCombos::to_view(c));
	return out;
}

void Session::LabStartCombo(const std::string& id, bool demo) {
	if (!impl_->world) return;
	impl_->combo_demo = demo;
	if (demo) impl_->lab_demo_combo(id);
	else impl_->lab_start_combo(id);
}

void Session::LabStopCombo() {
	impl_->tracker.stop();
	impl_->combo_demo = false;
}

ComboTrackerView Session::LabComboState() const {
	const ComboTracker& t = impl_->tracker;
	ComboTrackerView v;
	v.active = t.state != ComboTracker::State::Idle;
	v.demo = impl_->combo_demo;
	v.success = t.state == ComboTracker::State::Success;
	v.failed = t.state == ComboTracker::State::Failed || (!t.fail_reason.empty() && t.state == ComboTracker::State::Running && t.step == 0);
	v.combo_id = dstr(t.combo, "id");
	v.message = t.status_text();
	if (!t.fail_reason.empty() && t.state == ComboTracker::State::Running) v.message = t.fail_reason + " | " + v.message;
	v.step = static_cast<int>(t.step);
	v.steps = static_cast<int>(t.steps().size());
	v.time_left = static_cast<float>(t.time_left());
	return v;
}

std::vector<MatrixCounter> Session::LabMatrixCounters() const {
	std::vector<MatrixCounter> out;
	for (const Dict& c : MatrixQuery::counters()) out.push_back(MatrixQuery::to_view(c));
	return out;
}

MatrixResult Session::LabMatrixPredict(const std::string& threat_entry, const LabSpawnParams& params, const std::string& counter_id,
                                       int counter_tier, bool perfect) const {
	return MatrixQuery::result_view(MatrixQuery::predict(threat_entry, ss_params(params), counter_id, counter_tier, perfect));
}

void Session::LabMatrixStage(const std::string& threat_entry, const LabSpawnParams& params, const std::string& counter_id) {
	Impl& m = *impl_;
	if (!m.world) return;
	const Dict c = MatrixQuery::find_counter(counter_id);
	const int el = dint(c, "element", -1);
	if (el >= 0) {
		m.player_script = std::make_unique<LabScript>();
		m.player_script->frames.push_back(D({{"element_select", el}, {"sub_select", dint(c, "sub", 0)}}));
	}
	m.lab_spawn(threat_entry, ss_params(params), true);
	m.rebuild_snapshot();
}

std::vector<TuningField> Session::LabTuningFields(const std::string& move_id) const {
	std::vector<TuningField> out;
	for (const LabTuning::Field& f : LabTuning::numeric_fields(move_id)) {
		TuningField t;
		t.move_id = move_id;
		t.key = f.path;
		t.value = f.value;
		t.original = vnum(LabTuning::original_value(move_id, f.path), f.value);
		t.overridden = LabTuning::is_changed(move_id, f.path);
		out.push_back(t);
	}
	return out;
}

void Session::LabSetTuning(const std::string& move_id, const std::string& key, double value) { LabTuning::set_value(move_id, key, value); }
void Session::LabClearTuning() { LabTuning::reset_all(); }
std::string Session::LabSaveTuning() const { return LabTuning::save_json(); }
bool Session::LabLoadTuning(const std::string& json) { return LabTuning::load_json(json) >= 0; }

std::string Session::SaveProgress() const { return impl_->progress.to_json(); }

bool Session::LoadProgress(const std::string& json) { return impl_->progress.from_json(json); }

void Session::ResetProgress() {
	impl_->progress.reset();
	if (!impl_->scenario_id.empty()) impl_->load_scenario(impl_->scenario_id);
}

void Session::SetLabMode(bool on) {
	Impl& m = *impl_;
	m.progress.lab_mode = on;
	if (!m.scenario_id.empty()) m.load_scenario(m.scenario_id);
}

bool Session::LabMode() const { return impl_->progress.lab_mode; }

std::vector<RuleCellInfo> Session::LabRuleCells() const {
	std::vector<RuleCellInfo> out;
	for (const LabTuning::RuleCell& c : LabTuning::rule_cells()) {
		RuleCellInfo r;
		r.key = c.key;
		r.idx = c.idx;
		r.id = c.id;
		r.label = c.label;
		out.push_back(r);
	}
	return out;
}

std::vector<TuningField> Session::LabRuleFields(const std::string& key, int idx) const {
	std::vector<TuningField> out;
	for (const LabTuning::Field& f : LabTuning::rule_fields(key, idx)) {
		TuningField t;
		t.move_id = key + "#" + itos(idx);
		t.key = f.path;
		t.value = f.value;
		t.original = vnum(LabTuning::rule_original(key, idx, f.path), f.value);
		t.overridden = LabTuning::rule_changed(key, idx, f.path);
		out.push_back(t);
	}
	return out;
}

bool Session::LabSetRuleValue(const std::string& key, int idx, const std::string& field, double value) {
	return LabTuning::set_rule_value(key, idx, field, value);
}

std::string Session::MoveInputText(Slot slot, const std::string& device) const { return MoveListData::input_text(std::string(SlotName(slot)), device); }
std::string Session::MoveCostText(const std::string& id) const { return MoveListData::cost_text(Moves::defs().get(id).as_dict()); }
std::string Session::MoveFramesText(const std::string& id) const { return MoveListData::frames_text(Moves::defs().get(id).as_dict()); }

void Session::SetSparOptions(const std::string& difficulty, const std::string& kit) {
	Impl& m = *impl_;
	if (Scenarios::is_spar_difficulty(difficulty)) m.progress.spar_difficulty = difficulty;
	if (Scenarios::is_spar_kit(kit) || kit.find('/') != std::string::npos) m.progress.spar_kit = kit;
}

}  // namespace ff
