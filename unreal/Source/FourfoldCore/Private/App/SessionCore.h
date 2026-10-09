// Fourfold core - SessionCore: the state and per-tick logic behind ff::Session (Session::Impl derives from it). Port of
// the engine-free parts of game/game.gd (scenario load, the 60 Hz tick, KO and round reset, practice dummies, the boulder
// launcher, lava vents, mastery challenges, Lab actions) plus game/actors/autoplay.gd's duel / soak modes. Private:
// included by Session.cpp and by CoreTests (test_regressions_game drives it like Godot's GameProbe drives Game).
#pragma once

#include "ff/OpenWorld.h"
#include "ff/Session.h"

#include "AI/AiBrain.h"
#include "App/HudBuilder.h"
#include "App/PlayerController.h"
#include "App/Progression.h"
#include "App/Scenarios.h"
#include "App/SnapshotBuilder.h"
#include "Combat/Acts.h"
#include "Combat/Moves.h"
#include "Lab/LabCombos.h"
#include "Lab/LabScript.h"
#include "Lab/LabSession.h"
#include "Lab/SpawnCatalog.h"
#include "Sim/CombatWorld.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"
#include "Util/Rng.h"

#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ff {

namespace SessionDetail {
inline constexpr double kDummyReviveS = 2.0;   // a downed practice target stays down this long, then stands at full health
inline constexpr double kKoResetS = 2.5;       // a downed player or rival: the knockdown plays, then the round resets
// Open world: the bubble follows a calm player once they are this far from its centre, and always near its edge.
inline constexpr double kRoamRecentreR = 6.0;
inline constexpr double kRoamForceMargin = 3.0;
inline constexpr double kRoamFleeMargin = 1.5;  // reaching the window edge mid-fight breaks the encounter off
inline constexpr double kRoamRearm = 4.0;       // a fled site re-arms once the player is this far past its aggro ring

inline std::string ss_mastery_toast(const Dict& ch) {
	std::string unlock = dstr(ch, "unlock");
	for (char& c : unlock)
		if (c == '_') c = ' ';
	return "Mastered " + dstr(ch, "title") + " \xE2\x80\x94 " + unlock + " now in your Free Spar kit";
}
}  // namespace SessionDetail

struct SessionCore {
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
	// open world (roam): see ff/OpenWorld.h
	std::shared_ptr<const WorldDef> roam;
	RoamOptions roam_opts;
	double origin_x = 0.0, origin_y = 0.0, origin_z = 0.0;
	std::vector<uint8_t> site_state;   // 0 waiting, 1 engaged, 2 defeated
	std::vector<uint8_t> site_armed;   // 0 after a fled / lost fight until the player walks away
	int engaged = -1;

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
		snap.origin_x = origin_x;
		snap.origin_y = origin_y;
		snap.origin_z = origin_z;
	}

	void refresh_arena_view() {
		const int64_t rev = arena.revision + 1;
		arena = SnapshotBuilder::arena(*world);
		arena.revision = rev;
		arena.world = roam;
		arena.origin_x = origin_x;
		arena.origin_y = origin_y;
		arena.origin_z = origin_z;
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
		roam.reset();
		engaged = -1;
		origin_x = origin_y = origin_z = 0.0;
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
		refresh_arena_view();
		push_app("app_scenario_loaded", D({{"id", scenario_id}, {"title", dstr(scen_def, "title")}}));
		rebuild_snapshot();
	}

	// ------------------------------------------------------------------ open world (roam)
	void load_roam(std::shared_ptr<const WorldDef> def, const RoamOptions& o) {
		roam_opts = o;
		scenario_id = "roam";
		scen_def = D({{"id", "roam"}, {"title", "Open World"}});
		ai.reset();
		duel_ai.reset();
		opponent = nullptr;
		player_script.reset();
		rival_script.reset();
		world = std::make_unique<CombatWorld>(seed);
		roam = std::move(def);
		const WorldDef& wd = *roam;
		origin_x = wd.player_spawn.x;
		origin_y = wd.FloorAt(wd.player_spawn.x, wd.player_spawn.z);
		origin_z = wd.player_spawn.z;
		world->arena = ArenaMap::make_window(roam, origin_x, origin_y, origin_z);
		place_pool();
		const Dict kit = o.kit == "all" ? Progression::lab_kit() : prog().kit();
		player = world->add_actor("You", V3(0, 0, 0), 0, kit, o.player_element >= 0 && o.player_element < 4 ? o.player_element : 0);
		player->facing = wd.spawn_facing;
		site_state.assign(wd.sites.size(), 0);
		site_armed.assign(wd.sites.size(), 1);
		engaged = -1;
		reset_lab_for_scenario(scen_def);
		launcher = Dict();
		challenge_n = 0;
		down.clear();
		intents.clear();
		player_intent.clear();
		bind_autoplay();
		update_challenge_text();
		refresh_arena_view();
		push_app("app_scenario_loaded", D({{"id", scenario_id}, {"title", dstr(scen_def, "title")}}));
		rebuild_snapshot();
	}

	// The lake inside the window is the sim's pool body (parked far below when the window has no water).
	void place_pool() {
		MatBody* p = world->pool;
		if (p == nullptr) return;
		const ArenaMap& m = world->arena;
		if (m.pool_max.x > m.pool_min.x && m.pool_min.x < 1.0e5f) {
			p->pos = V3((m.pool_min.x + m.pool_max.x) * 0.5, m.pool_level, (m.pool_min.y + m.pool_max.y) * 0.5);
			p->mass = maxf(p->mass, 2000.0);
		} else {
			p->pos = V3(0.0, -1000.0, 0.0);
		}
	}

	// Moves the bubble so world (wx, wz) is its new centre; local state shifts by -d, bodies left outside decay.
	void recentre(double wx, double wz) {
		const double ny = roam->FloorAt(wx, wz);
		const Vec3 d(f32(wx - origin_x), f32(ny - origin_y), f32(wz - origin_z));
		if (d.x == 0.0f && d.y == 0.0f && d.z == 0.0f) return;
		world->translate(d);
		origin_x += d.x;
		origin_y += d.y;
		origin_z += d.z;
		world->arena = ArenaMap::make_window(roam, origin_x, origin_y, origin_z);
		const double lim = world->arena.half_size + 1.0;
		for (MatBody* b : world->body_list())
			if (b->alive && b != world->pool && (std::fabs(b->pos.x) > lim || std::fabs(b->pos.z) > lim)) world->decay_body(*b, "out_of_bubble");
		place_pool();
		refresh_arena_view();
		push_app("app_recenter", D({{"dx", d.x}, {"dy", d.y}, {"dz", d.z}}));
	}

	double site_dist(const EncounterSite& s, double wx, double wz) const {
		return Vec2(f32(s.pos.x - wx), f32(s.pos.z - wz)).length();
	}

	void engage(int i) {
		const EncounterSite& s = roam->sites[static_cast<size_t>(i)];
		recentre((player->pos.x + origin_x + s.pos.x) * 0.5, (player->pos.z + origin_z + s.pos.z) * 0.5);
		const Vec3 local(f32(s.pos.x - origin_x), f32(s.pos.y - origin_y), f32(s.pos.z - origin_z));
		Dict okit;
		for (const std::string& t : Progression::ALL()) okit.set(t, true);
		opponent = world->add_actor(s.name, local, 1, okit, s.element);
		opponent->elements = {false, false, false, false};
		opponent->elements[static_cast<size_t>(s.element)] = true;
		if (s.sub >= 0 && s.sub < 4) opponent->subs[static_cast<size_t>(s.element)] = s.sub;
		opponent->facing = std::atan2(static_cast<double>(player->pos.x - opponent->pos.x), static_cast<double>(player->pos.z - opponent->pos.z));
		Dict ai_def = D({{"preset", s.preset}, {"elements", A({s.element})}});
		if (s.sub >= 0) {
			Dict subs;
			subs.set(itos(s.element), A({s.sub}));
			ai_def.set("subs", subs);
		}
		ai = std::make_unique<AiBrain>(*world, *opponent, ai_def, seed + 7 + static_cast<uint64_t>(i));
		ai->configure(ai_def);
		lab.ai_enabled = true;
		site_state[static_cast<size_t>(i)] = 1;
		engaged = i;
		push_app("app_encounter", D({{"id", s.id}, {"name", s.name}, {"state", "engaged"}, {"element", s.element}, {"actor", opponent->id}}));
		toast(s.name + " challenges you", "warning");
	}

	// result: won | fled | lost
	void end_encounter(const std::string& result) {
		if (engaged < 0) return;
		const size_t i = static_cast<size_t>(engaged);
		const EncounterSite& s = roam->sites[i];
		if (opponent != nullptr) {
			const int id = opponent->id;
			ai.reset();
			rival_script.reset();
			intents.erase(id);
			down.erase(id);
			world->remove_actor(id);
			opponent = nullptr;
		}
		site_state[i] = result == "won" ? 2 : 0;
		if (result != "won") site_armed[i] = 0;
		engaged = -1;
		push_app("app_encounter", D({{"id", s.id}, {"name", s.name}, {"state", result}, {"element", s.element}}));
		if (result == "won") toast(s.name + " defeated", "mastery");
		else if (result == "fled") toast("You slipped away from " + s.name);
	}

	void roam_ko(ActorState& a) {
		if (&a == opponent) {
			end_encounter("won");
			return;
		}
		end_encounter("lost");
		// Back to the nearest shrine (the spawn when the world has none), on your feet.
		const double wx = player->pos.x + origin_x;
		const double wz = player->pos.z + origin_z;
		Vec3 to = roam->player_spawn;
		double best = 1.0e30;
		for (const Shrine& sh : roam->shrines) {
			const double dd = Vec2(f32(sh.pos.x - wx), f32(sh.pos.z - wz)).length();
			if (dd < best) {
				best = dd;
				to = sh.pos;
			}
		}
		recentre(to.x, to.z);
		player->pos = V3(to.x - origin_x, roam->FloorAt(to.x, to.z) - origin_y, to.z - origin_z);
		player->vel = Vec3();
		player->ground_y = player->pos.y;
		player->grounded = true;
		player->action.reset();
		player->stun = 0.0;
		player->stun_kind.clear();
		player->health = Sim::HEALTH_MAX;
		player->balance = Sim::BALANCE_MAX;
		player->status = Dict();
		down.erase(player->id);
		push_app("app_respawn", D({{"actor", player->id}}));
		toast("You recover at the shrine", "info");
	}

	void roam_tick() {
		if (!roam || player == nullptr) return;
		const double px = player->pos.x;
		const double pz = player->pos.z;
		const double lim = world->arena.half_size;
		if (engaged >= 0) {
			if (std::fabs(px) > lim - SessionDetail::kRoamFleeMargin || std::fabs(pz) > lim - SessionDetail::kRoamFleeMargin) end_encounter("fled");
			return;   // while fighting, the window is the arena
		}
		const double wx = px + origin_x;
		const double wy = player->pos.y + origin_y;
		const double wz = pz + origin_z;
		for (size_t i = 0; i < roam->sites.size(); ++i) {
			const EncounterSite& s = roam->sites[i];
			if (!site_armed[i] && site_dist(s, wx, wz) > s.aggro_radius + SessionDetail::kRoamRearm) site_armed[i] = 1;
		}
		if (roam_opts.encounters && player->health > 0.0) {
			for (size_t i = 0; i < roam->sites.size(); ++i) {
				const EncounterSite& s = roam->sites[i];
				if (site_state[i] == 0 && site_armed[i] && site_dist(s, wx, wz) < s.aggro_radius && std::fabs(wy - s.pos.y) < 6.0) {
					engage(static_cast<int>(i));
					return;
				}
			}
		}
		const bool calm = player->action == nullptr && player->grounded && player->held_body < 0;
		const double r = Vec2(f32(px), f32(pz)).length();
		const double force = lim - SessionDetail::kRoamForceMargin;
		if ((calm && r > SessionDetail::kRoamRecentreR) || std::fabs(px) > force || std::fabs(pz) > force) recentre(wx, wz);
	}

	RoamView roam_view() const {
		RoamView v;
		if (!roam) return v;
		v.active = true;
		v.origin_x = origin_x;
		v.origin_y = origin_y;
		v.origin_z = origin_z;
		v.engaged = engaged;
		const double wx = player ? player->pos.x + origin_x : 0.0;
		const double wz = player ? player->pos.z + origin_z : 0.0;
		double best = 1.0e30;
		for (size_t i = 0; i < roam->sites.size(); ++i) {
			const EncounterSite& s = roam->sites[i];
			EncounterView e;
			e.id = s.id;
			e.name = s.name;
			e.region = s.region;
			e.pos = s.pos;
			e.element = s.element;
			e.state = site_state[i] == 2 ? "defeated" : (site_state[i] == 1 ? "engaged" : "waiting");
			v.sites.push_back(e);
			const double dd = site_dist(s, wx, wz);
			if (dd < best) {
				best = dd;
				v.region = s.region;
			}
		}
		return v;
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
		const double r = static_cast<double>(soak_rng.randf());   // GDScript float (double) compares
		const bool guarding = f.guard_held;
		f.attack_held = false;
		f.guard_held = false;
		f.evade_held = false;
		if (f.tech_held && static_cast<double>(soak_rng.randf()) < 0.5) {
			f.tech_held = false;
			f.tech_released = true;
		}
		if (r < 0.06) {
			f.element_select = soak_rng.randi_range(0, 3);
		} else if (r < 0.12) {
			f.sub_select = soak_rng.randi_range(0, 3);
		} else if (r < 0.36) {
			f.attack_pressed = true;
			f.attack_held = static_cast<double>(soak_rng.randf()) < 0.35;
			if (static_cast<double>(soak_rng.randf()) < 0.5) f.attack_gesture = static_cast<Gesture>(soak_rng.randi_range(1, 3));
		} else if (r < 0.46) {
			f.guard_pressed = true;
			f.guard_held = true;
		} else if (r < 0.52 && guarding) {
			f.guard_held = true;
			f.guard_gesture = static_cast<double>(soak_rng.randf()) < 0.5 ? Gesture::Up : Gesture::Down;
		} else if (r < 0.62) {
			f.evade_pressed = true;
			f.evade_held = static_cast<double>(soak_rng.randf()) < 0.4;
		} else if (r < 0.82) {
			f.tech_pressed = true;
			f.tech_held = true;
			if (static_cast<double>(soak_rng.randf()) < 0.4) {
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
				world->_stagger(a, "knockdown", a.is_dummy ? SessionDetail::kDummyReviveS : SessionDetail::kKoResetS + 0.5, Dict());
				if (!a.is_dummy) {
					push_app("app_ko", D({{"actor", a.id}}));
					toast(&a == player ? "You're down" : a.name + " down", "warning");
				}
			}
			t += Sim::DT;
			down[a.id] = t;
			if (a.is_dummy && t >= SessionDetail::kDummyReviveS) {
				a.health = Sim::HEALTH_MAX;
				down.erase(a.id);
			} else if (!a.is_dummy && t >= SessionDetail::kKoResetS && roam) {
				push_sim(world->take_events());
				roam_ko(a);
				rebuild_snapshot();
				return true;
			} else if (!a.is_dummy && t >= SessionDetail::kKoResetS) {
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
				toast(SessionDetail::ss_mastery_toast(ch), "mastery");
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
		roam_tick();
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

}  // namespace ff
