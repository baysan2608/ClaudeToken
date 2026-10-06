// Fourfold core - port of game/core/conduction.gd.
#include "Sim/Conduction.h"

#include "Sim/ActorState.h"
#include "Sim/Agent.h"
#include "Sim/CombatWorld.h"
#include "Sim/Interactions.h"
#include "Sim/Materials.h"
#include "Sim/MatBody.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"

#include <algorithm>
#include <cmath>
#include <deque>

namespace ff {
namespace Conduction {
namespace {

Array cd_path(std::initializer_list<Vec3> pts) {
	Array a;
	for (const Vec3& p : pts) a.append(p);
	return a;
}

std::vector<std::string> cd_keys_in_order(const std::vector<std::string>& order) { return order; }

}  // namespace

Dict discharge(CombatWorld& w, ActorState& caster, Vec3 aim, const Dict& def, int attack_id, bool allow_redirect, double dmg_scale) {
	const Vec3 start = dvec(def, "start", caster.hand_point() + Vec3(0, 0.25f, 0));
	const double rng_m = dnum(def, "range");
	ActorState* target = w.get_actor(dint(def, "force_target", caster.lock_target));
	Vec3 end = aim;
	if (def.has("force_target") && target != nullptr) {
		end = target->chest();
	} else if (target != nullptr && target->chest().distance_to(start) <= rng_m) {
		const Vec3 to_t = (target->chest() - start).normalized();
		const Vec3 to_aim = (aim - start).normalized();
		if (to_t.dot(to_aim) > 0.9f) end = target->chest();
		else target = nullptr;
	} else {
		target = nullptr;
	}
	if (start.distance_to(end) > rng_m) {
		end = start + (end - start).normalized() * rng_m;
		target = nullptr;
	}
	if (target == nullptr) {
		// Aimed at the ground / a surface: strike where the line meets the floor.
		const Vec3 dir = (end - start).normalized();
		double t = 0.0;
		while (t < rng_m) {
			const Vec3 p = start + dir * t;
			if (p.y <= w.arena.ground_height(p.x, p.z, p.y + 0.5) + 0.05) {
				end = p;
				break;
			}
			t += 0.25;
		}
	}
	Dict out = D({{"path", cd_path({start, end})}, {"arcs", Array()}, {"hits", Array()}, {"blocked", false}});
	double e_val = dnum(def, "E", dnum(def, "damage")) * dmg_scale;
	AgentRef bolt = Agent::of_volume(&w, &caster, nullptr, "lightning", start, (end - start).normalized(), D({{"E", e_val}}));
	bolt->tier = dint(def, "tier", 0);
	// Barriers along the path, nearest first.
	for (const BarrierHit& hb : barriers_on(w, start, end)) {
		const Vec3 stop = start.lerp(end, f32(hb.t));
		if (hb.body == nullptr) {
			AgentRef env = Agent::of_env(&w, "arena_wall", stop);
			IxCtx ctx;
			ctx.site = "bolt";
			Interactions::resolve(w, *bolt, *env, ctx);
			return _blocked(w, caster, out, start, stop);
		}
		AgentRef counter = Agent::of_body(w, *hb.body);
		counter->actor = w.get_actor(hb.body->form == Form::Wall ? hb.body->last_actor : hb.body->owner);
		IxCtx ctx;
		ctx.site = "bolt";
		const IxResult res = Interactions::resolve(w, *bolt, *counter, ctx);
		if (res.stopped || res.pass_scale <= 0.0) return _blocked(w, caster, out, start, stop);
		e_val *= res.pass_scale;
		dmg_scale *= res.pass_scale;
		bolt->ch.E = e_val;
	}
	if (dbool(def, "meet_bodies", false)) {
		const Dict mres = meet_path_bodies(w, caster, *bolt, start, end);
		e_val *= dnum(mres, "scale", 1.0);
		dmg_scale *= dnum(mres, "scale", 1.0);
		bolt->ch.E = e_val;
		if (dbool(mres, "stopped")) {
			out.set("e", e_val);
			return _blocked(w, caster, out, start, dvec(mres, "at"));
		}
	}
	out.set("e", e_val);
	const double budget = dnum(def, "conduct_budget") * dmg_scale;
	const double base_dmg = dnum(def, "damage") * dmg_scale;
	std::vector<std::string> seeds;
	Array hits = out.get("hits").as_array();
	if (target != nullptr) {
		// Redirect: equipped technique + Fire element + perfect-timed guard (legacy cell aura_flame x lightning).
		if (allow_redirect && target->guarding && w.perfect_guard(*target) && target->has("redirect_current") &&
		    w.guard_element(*target) == Sim::FIRE) {
			AgentRef g = Agent::of_guard(w, *target);
			if (Interactions::predict(&w, *bolt, *g).outcome == "redirect") {
				ActorState* tgt = target;
				ActorState* cst = &caster;
				const Value path_v = out.get("path");
				auto cb = [&w, tgt, cst, path_v, attack_id, def, dmg_scale](double factor) {
					w.emit("lightning", D({{"actor", cst->id}, {"path", path_v}, {"arcs", Array()}, {"blocked", false}, {"hits", Array()},
					                       {"redirected", true}}));
					w.emit("lightning_redirect", D({{"actor", tgt->id}, {"from", cst->id}}));
					tgt->hits_taken[attack_id] = w.tick;
					const int back = w.new_attack_id();
					const int saved = tgt->lock_target;
					tgt->lock_target = cst->id;
					discharge(w, *tgt, cst->chest(), def, back, false, factor * dmg_scale);
					tgt->lock_target = saved;
				};
				IxCtx ctx;
				ctx.allow_redirect = true;
				ctx.redirect_cb = cb;
				const IxResult rres = Interactions::resolve(w, *bolt, *g, ctx);
				if (rres.result == "redirected") return out;
			}
		}
		// Grounded stance (legacy cell lightning x ground): Earth guard on stone takes 40 %, replacing the guard cell.
		double gscale = 1.0;
		bool grounded = false;
		if (target->guarding && w.guard_element(*target) == Sim::EARTH && target->surface == "stone" && target->grounded &&
		    Agent::of_guard(w, *target)->ccls == "guard_earth") {
			AgentRef env = Agent::of_env(&w, "ground", target->pos, target);
			IxCtx ctx;
			ctx.site = "bolt";
			const IxResult gres = Interactions::resolve(w, *bolt, *env, ctx);
			gscale = gres.pass_scale;
			grounded = true;
		}
		const double dmg = base_dmg * gscale;
		w.hit_actor(*target,
		            D({{"attacker", caster.id}, {"attack_id", attack_id}, {"damage", dmg}, {"balance", dnum(def, "balance") * gscale},
		               {"knock", (end - start).normalized() * 2.0f}, {"kind", "lightning"}, {"from", start}, {"power", e_val},
		               {"unblockable", grounded}}),
		            bolt);
		hits.append(target->id);
		const std::string tn = actor_surface_node(w, *target);
		if (!tn.empty()) seeds.push_back(tn);
	} else {
		const std::string sn = surface_node_at(w, end);
		if (!sn.empty()) seeds.push_back(sn);
		const std::string bn = body_node_at(w, end);
		if (!bn.empty() && std::find(seeds.begin(), seeds.end(), bn) == seeds.end()) seeds.push_back(bn);
	}
	if (!seeds.empty()) {
		const Graph graph = build_graph(w);
		std::vector<std::string> order;
		const std::map<std::string, int> reached = bfs(graph, seeds, dint(def, "max_hops"), &order);
		std::vector<ActorState*> victims;
		std::map<int, double> factors;
		for (auto& ap : w.actors) {
			ActorState& a = *ap;
			if (hits.has(Value(a.id)) || a.health <= 0.0) continue;
			if (Status::immune(a, "conduct")) continue;
			double f = 0.0;
			for (const std::string& n : actor_nodes(w, a))
				if (reached.count(n)) f = maxf(f, node_factor(w, n));
			if (f > 0.0) {
				victims.push_back(&a);
				factors[a.id] = f;
			}
		}
		Array arcs = out.get("arcs").as_array();
		if (!victims.empty()) {
			const double share = maxf(MIN_SHARE, budget / static_cast<double>(victims.size()));
			double left = budget;
			for (ActorState* v : victims) {
				if (left < MIN_SHARE * 0.5) break;
				const double dmg = minf(share, left);
				left -= dmg;
				w.hit_actor(*v, D({{"attacker", caster.id}, {"attack_id", attack_id}, {"damage", dmg * factors[v->id]}, {"balance", 22.0},
				                   {"kind", "lightning"}, {"from", end}, {"unblockable", true}}));
				hits.append(v->id);
				arcs.append(cd_path({node_point(w, _reached_node(w, *v, reached), end), v->chest()}));
			}
		}
		Array nodes;
		for (const std::string& n : order) {
			nodes.append(n);
			if (std::find(seeds.begin(), seeds.end(), n) == seeds.end())
				arcs.append(cd_path({node_point(w, seeds[0], end), node_point(w, n, end)}));
		}
		w.emit("conduct", D({{"actor", caster.id}, {"nodes", nodes}, {"victims", hits}}));
	}
	const int nf = dint(def, "forks", 0);
	if (nf > 0) _forks(w, caster, out, end, nf, e_val, attack_id, def);
	w.emit("lightning", D({{"actor", caster.id}, {"path", out.get("path")}, {"arcs", out.get("arcs")}, {"blocked", false},
	                       {"hits", out.get("hits")}, {"e", e_val}}));
	return out;
}

Dict meet_path_bodies(CombatWorld& w, ActorState& caster, Agent& bolt, Vec3 p0, Vec3 p1) {
	Dict out = D({{"scale", 1.0}, {"stopped", false}, {"at", p1}});
	const Vec3 seg = p1 - p0;
	const double seg_len = seg.length();
	if (seg_len < 0.05) return out;
	const Vec3 dir = seg / seg_len;
	struct PathHit {
		double t;
		MatBody* b;
	};
	std::vector<PathHit> hits;
	for (const BodyRef& bp : w.bodies) {
		MatBody& b = *bp;
		if (!b.alive || b.static_body || b.form == Form::Wall || b.form == Form::Zone || b.form == Form::Pool || b.form == Form::Puddle ||
		    b.controller == caster.id || b.form == Form::Cloud)
			continue;
		const double t = (b.pos - p0).dot(dir);
		if (t < 0.3 || t > seg_len) continue;
		if ((p0 + dir * t).distance_to(b.pos) <= b.radius + 0.45) hits.push_back({t, &b});
	}
	std::stable_sort(hits.begin(), hits.end(), [](const PathHit& x, const PathHit& y) {
		return x.t < y.t || (x.t == y.t && x.b->id < y.b->id);
	});
	for (const PathHit& h : hits) {
		MatBody& b = *h.b;
		if (!b.alive) continue;
		AgentRef th = Agent::of_body(w, b, &caster);
		IxCtx ctx;
		ctx.site = "bolt_path";
		const IxResult res = Interactions::resolve(w, *th, bolt, ctx, &Interactions::PASS_RULE());
		const double ps = res.pass_scale;
		if (ps < 1.0) {
			out.set("scale", dnum(out, "scale") * ps);
			bolt.ch.E = bolt.ch.E * ps;
		}
		if (ps <= 0.0) {
			out.set("stopped", true);
			out.set("at", p0 + dir * h.t);
			return out;
		}
	}
	return out;
}

void _forks(CombatWorld& w, ActorState& caster, Dict& out, Vec3 at, int n, double e_val, int attack_id, const Dict& def) {
	struct Cand {
		double d;
		MatBody* b;
	};
	std::vector<Cand> cands;
	for (const BodyRef& bp : w.bodies) {
		MatBody& b = *bp;
		if (!b.alive || b.form == Form::Pool || b.form == Form::Zone) continue;
		if (!(Materials::conducts(b) || (b.form == Form::Puddle && b.phase == Phase::Liquid))) continue;
		const double d = b.pos.distance_to(at);
		if (d <= 6.0 && d > 0.2) cands.push_back({d, &b});
	}
	std::stable_sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) { return x.d < y.d || (x.d == y.d && x.b->id < y.b->id); });
	Array arcs = out.get("arcs").as_array();
	Array hits = out.get("hits").as_array();
	const int m = mini(n, static_cast<int>(cands.size()));
	for (int k = 0; k < m; ++k) {
		MatBody& b = *cands[static_cast<size_t>(k)].b;
		arcs.append(cd_path({at, b.pos}));
		b.charge = maxf(b.charge, e_val * 0.25);
		if (b.controller >= 0) {
			ActorState* h = w.get_actor(b.controller);
			if (h != nullptr && h->id != caster.id && !hits.has(Value(h->id)) && !Status::immune(*h, "conduct")) {
				w.hit_actor(*h, D({{"attacker", caster.id}, {"attack_id", attack_id}, {"damage", dnum(def, "damage") * 0.35},
				                   {"balance", 20.0}, {"kind", "lightning"}, {"from", b.pos}, {"unblockable", true}}));
				hits.append(h->id);
			}
		}
	}
	w.emit("fork", D({{"actor", caster.id}, {"at", at}, {"n", m}}));
}

Dict rail(CombatWorld& w, ActorState& caster, Vec3 dir, const Dict& def, int attack_id) {
	const Vec3 start = caster.hand_point() + Vec3(0, 0.25f, 0);
	Vec3 end = start + dir.normalized() * dnum(def, "range");
	Dict out = D({{"path", cd_path({start, end})}, {"arcs", Array()}, {"hits", Array()}, {"blocked", false}});
	double e_val = dnum(def, "E", dnum(def, "damage"));
	AgentRef bolt = Agent::of_volume(&w, &caster, nullptr, "lightning", start, dir, D({{"E", e_val}}));
	bolt->tier = dint(def, "tier", 0);
	double stop_t = 1.0;
	for (const BarrierHit& hb : barriers_on(w, start, end)) {
		const Vec3 stop = start.lerp(end, f32(hb.t));
		if (hb.body == nullptr) {
			AgentRef env = Agent::of_env(&w, "arena_wall", stop);
			IxCtx ctx;
			ctx.site = "bolt";
			Interactions::resolve(w, *bolt, *env, ctx);
			stop_t = hb.t;
			out.set("blocked", true);
			break;
		}
		AgentRef counter = Agent::of_body(w, *hb.body);
		counter->actor = w.get_actor(hb.body->form == Form::Wall ? hb.body->last_actor : hb.body->owner);
		IxCtx ctx;
		ctx.site = "bolt";
		const IxResult res = Interactions::resolve(w, *bolt, *counter, ctx);
		if (res.stopped || res.pass_scale <= 0.0) {
			stop_t = hb.t;
			out.set("blocked", true);
			break;
		}
		e_val *= res.pass_scale;
		bolt->ch.E = e_val;
	}
	end = start.lerp(end, f32(stop_t));
	out.set("path", cd_path({start, end}));
	const double scale = e_val / maxf(dnum(def, "E", dnum(def, "damage")), 0.01);
	const Vec3 seg = end - start;
	const double seg_len = maxf(seg.length(), 0.01);
	const Vec3 d = seg / seg_len;
	// The first fighter on the line.
	ActorState* first = nullptr;
	double ft = kInf;
	for (auto& tp : w.actors) {
		ActorState& t = *tp;
		if (&t == &caster || t.team == caster.team || t.health <= 0.0) continue;
		const double tt = clampf((t.chest() - start).dot(d), 0.0, seg_len);
		if ((start + d * tt).distance_to(t.chest()) <= Sim::ACTOR_RADIUS + 0.45 && tt < ft) {
			ft = tt;
			first = &t;
		}
	}
	// Conductors the line touches (bodies and zones), nearest first, before the first fighter.
	std::vector<std::string> seeds;
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody& b = *w.bodies[i];
		if (!b.alive || !(_is_body_node(b) || (b.form == Form::Puddle && b.phase == Phase::Liquid))) continue;
		const double tb = clampf((b.pos - start).dot(d), 0.0, seg_len);
		const double r = b.form == Form::Zone ? b.zone_radius : b.radius;
		if (tb > ft || (start + d * tb).distance_to(b.pos) > r + 0.5) continue;
		const std::string key = (b.form == Form::Puddle ? "puddle:" : "body:") + itos(b.id);
		if (std::find(seeds.begin(), seeds.end(), key) == seeds.end()) {
			seeds.push_back(key);
			if (b.form != Form::Puddle) {
				AgentRef th = Agent::of_body(w, b, &caster);
				IxCtx ctx;
				ctx.site = "rail";
				Interactions::resolve(w, *th, *bolt, ctx, &Interactions::PASS_RULE());
			}
		}
	}
	Array hits = out.get("hits").as_array();
	if (first != nullptr) {
		w.hit_actor(*first,
		            D({{"attacker", caster.id}, {"attack_id", attack_id}, {"damage", dnum(def, "damage") * scale},
		               {"balance", dnum(def, "balance") * scale}, {"knock", d * 2.0f}, {"kind", "lightning"}, {"from", start},
		               {"power", e_val}}),
		            bolt);
		hits.append(first->id);
		end = first->chest();
		out.set("path", cd_path({start, end}));
		const std::string tn = actor_surface_node(w, *first);
		if (!tn.empty() && std::find(seeds.begin(), seeds.end(), tn) == seeds.end()) seeds.push_back(tn);
	}
	const std::string sn = surface_node_at(w, end);
	if (!sn.empty() && std::find(seeds.begin(), seeds.end(), sn) == seeds.end()) seeds.push_back(sn);
	_conduct_from(w, caster, out, seeds, end, dnum(def, "conduct_budget") * scale, dint(def, "max_hops"), attack_id);
	out.set("e", e_val);
	w.emit("lightning", D({{"actor", caster.id}, {"path", out.get("path")}, {"arcs", out.get("arcs")}, {"blocked", out.get("blocked")},
	                       {"hits", out.get("hits")}, {"e", e_val}, {"rail", true}}));
	return out;
}

Dict relay(CombatWorld& w, ActorState& caster, const std::vector<MatBody*>& nodes, const Dict& def, int attack_id) {
	Array pts = cd_path({caster.hand_point() + Vec3(0, 0.25f, 0)});
	Dict out = D({{"path", pts}, {"arcs", Array()}, {"hits", Array()}, {"blocked", false}});
	double e_val = dnum(def, "E", dnum(def, "damage"));
	AgentRef bolt = Agent::of_volume(&w, &caster, nullptr, "lightning", pts[0].as_vec3(), Vec3(), D({{"E", e_val}}));
	bolt->tier = dint(def, "tier", 0);
	Vec3 last = pts[0].as_vec3();
	std::vector<Vec3> legs;
	for (MatBody* b : nodes) {
		if (b == nullptr || !b->alive) continue;
		legs.push_back(b->pos + Vec3(0, 0.2f, 0));
	}
	const Vec3 anchor = !legs.empty() ? legs.back() : last;
	ActorState* tgt = nullptr;
	double best = kInf;
	for (auto& tp : w.actors) {
		ActorState& t = *tp;
		if (&t == &caster || t.team == caster.team || t.health <= 0.0) continue;
		const double dd = t.chest().distance_to(anchor);
		if (dd <= dnum(def, "relay_range", 8.0) && dd < best && w.arena.has_los(anchor, t.chest())) {
			best = dd;
			tgt = &t;
		}
	}
	if (tgt != nullptr) legs.push_back(tgt->chest());
	for (const Vec3& p : legs) {
		bool hit_stop = false;
		for (const BarrierHit& hb : barriers_on(w, last, p)) {
			const Vec3 stop = last.lerp(p, f32(hb.t));
			if (hb.body == nullptr) {
				AgentRef env = Agent::of_env(&w, "arena_wall", stop);
				IxCtx ctx;
				ctx.site = "bolt";
				Interactions::resolve(w, *bolt, *env, ctx);
				pts.append(stop);
				hit_stop = true;
				break;
			}
			AgentRef counter = Agent::of_body(w, *hb.body);
			counter->actor = w.get_actor(hb.body->form == Form::Wall ? hb.body->last_actor : hb.body->owner);
			IxCtx ctx;
			ctx.site = "bolt";
			const IxResult res = Interactions::resolve(w, *bolt, *counter, ctx);
			if (res.stopped || res.pass_scale <= 0.0) {
				pts.append(stop);
				hit_stop = true;
				break;
			}
			e_val *= res.pass_scale;
			bolt->ch.E = e_val;
		}
		if (hit_stop) {
			out.set("blocked", true);
			break;
		}
		pts.append(p);
		last = p;
	}
	out.set("path", pts);
	out.set("e", e_val);
	const double scale = e_val / maxf(dnum(def, "E", dnum(def, "damage")), 0.01);
	if (!dbool(out, "blocked") && tgt != nullptr) {
		w.hit_actor(*tgt,
		            D({{"attacker", caster.id}, {"attack_id", attack_id}, {"damage", dnum(def, "damage") * scale},
		               {"balance", dnum(def, "balance") * scale}, {"knock", (tgt->chest() - anchor).normalized() * 2.0f},
		               {"kind", "lightning"}, {"from", anchor}, {"power", e_val}, {"relay", true}}),
		            bolt);
		out.get("hits").as_array().append(tgt->id);
		const std::string tn = actor_surface_node(w, *tgt);
		if (!tn.empty())
			_conduct_from(w, caster, out, {tn}, tgt->chest(), dnum(def, "conduct_budget") * scale, dint(def, "max_hops"), attack_id);
	}
	w.emit("lightning", D({{"actor", caster.id}, {"path", pts}, {"arcs", out.get("arcs")}, {"blocked", out.get("blocked")},
	                       {"hits", out.get("hits")}, {"e", e_val}, {"relay", true}}));
	return out;
}

void _conduct_from(CombatWorld& w, ActorState& caster, Dict& out, const std::vector<std::string>& seeds, Vec3 at, double budget,
                   int max_hops, int attack_id) {
	if (seeds.empty()) return;
	const Graph graph = build_graph(w);
	std::vector<std::string> order;
	const std::map<std::string, int> reached = bfs(graph, seeds, max_hops, &order);
	Array hits = out.get("hits").as_array();
	Array arcs = out.get("arcs").as_array();
	std::vector<ActorState*> victims;
	std::map<int, double> factors;
	for (auto& ap : w.actors) {
		ActorState& a = *ap;
		if (hits.has(Value(a.id)) || a.health <= 0.0 || Status::immune(a, "conduct")) continue;
		double f = 0.0;
		for (const std::string& n : actor_nodes(w, a))
			if (reached.count(n)) f = maxf(f, node_factor(w, n));
		if (f > 0.0) {
			victims.push_back(&a);
			factors[a.id] = f;
		}
	}
	if (!victims.empty()) {
		const double share = maxf(MIN_SHARE, budget / static_cast<double>(victims.size()));
		double left = budget;
		for (ActorState* v : victims) {
			if (left < MIN_SHARE * 0.5) break;
			const double dmg = minf(share, left);
			left -= dmg;
			w.hit_actor(*v, D({{"attacker", caster.id}, {"attack_id", attack_id}, {"damage", dmg * factors[v->id]}, {"balance", 22.0},
			                   {"kind", "lightning"}, {"from", at}, {"unblockable", true}}));
			hits.append(v->id);
			arcs.append(cd_path({node_point(w, _reached_node(w, *v, reached), at), v->chest()}));
		}
	}
	Array nodes;
	for (const std::string& n : cd_keys_in_order(order)) {
		nodes.append(n);
		if (std::find(seeds.begin(), seeds.end(), n) == seeds.end()) arcs.append(cd_path({node_point(w, seeds[0], at), node_point(w, n, at)}));
	}
	w.emit("conduct", D({{"actor", caster.id}, {"nodes", nodes}, {"victims", hits}}));
}

Dict _blocked(CombatWorld& w, ActorState& caster, Dict& out, Vec3 start, Vec3 stop) {
	out.set("path", cd_path({start, stop}));
	out.set("blocked", true);
	w.emit("lightning", D({{"actor", caster.id}, {"path", out.get("path")}, {"arcs", Array()}, {"blocked", true}, {"hits", Array()}}));
	return out;
}

std::vector<BarrierHit> barriers_on(CombatWorld& w, Vec3 p0, Vec3 p1) {
	std::vector<BarrierHit> out;
	const double ta = w.arena.segment_hit(p0, p1);
	if (ta >= 0.0) out.push_back({ta, nullptr});
	for (const BodyRef& bp : w.bodies) {
		MatBody& b = *bp;
		if (!b.alive) continue;
		double t = -1.0;
		if (b.form == Form::Wall && b.wall_rise > 0.5) t = w.wall_segment_t(p0, p1, b);
		else if (b.form == Form::Zone && dtruthy(b.props, "barrier")) t = _cyl_t(p0, p1, b.pos, b.zone_radius, dnum(b.props, "height", 2.5));
		if (t >= 0.0) out.push_back({t, &b});
	}
	std::stable_sort(out.begin(), out.end(), [](const BarrierHit& x, const BarrierHit& y) {
		if (!is_equal_approx(x.t, y.t)) return x.t < y.t;
		return (x.body != nullptr ? x.body->id : -1) < (y.body != nullptr ? y.body->id : -1);
	});
	return out;
}

double _cyl_t(Vec3 p0, Vec3 p1, Vec3 c, double r, double h) {
	const Vec2 d(p1.x - p0.x, p1.z - p0.z);
	const Vec2 f(p0.x - c.x, p0.z - c.z);
	if (f.length() <= r) return -1.0;
	const double a = d.dot(d);
	if (a < 1e-9) return -1.0;
	const double b = 2.0 * static_cast<double>(f.dot(d));
	const double cc = static_cast<double>(f.dot(f)) - r * r;
	const double disc = b * b - 4.0 * a * cc;
	if (disc < 0.0) return -1.0;
	const double t = (-b - std::sqrt(disc)) / (2.0 * a);
	if (t < 0.0 || t > 1.0) return -1.0;
	const double y = lerpf(p0.y, p1.y, t);
	if (y < c.y - 0.2 || y > c.y + h) return -1.0;
	return t;
}

std::string actor_surface_node(CombatWorld& w, const ActorState& a) {
	if (!a.grounded) return "";
	if (a.in_water) return "pool";
	if (w.arena.on_metal(a.pos.x, a.pos.z) && std::fabs(a.pos.y - w.arena.metal_top) < 0.15) return "metal";
	MatBody* pd = w.puddle_at(a.pos);
	if (pd != nullptr && pd->phase == Phase::Liquid) return "puddle:" + itos(pd->id);
	return "";
}

std::vector<std::string> actor_nodes(CombatWorld& w, const ActorState& a) {
	std::vector<std::string> out;
	const std::string sn = actor_surface_node(w, a);
	if (!sn.empty()) out.push_back(sn);
	for (const BodyRef& bp : w.bodies) {
		const MatBody& b = *bp;
		if (!b.alive || !_is_body_node(b)) continue;
		if (b.controller == a.id) {
			out.push_back("body:" + itos(b.id));
		} else if (b.form == Form::Zone && w._in_zone(b, a.pos + Vec3(0, 0.9f, 0), Sim::ACTOR_RADIUS)) {
			if (a.grounded || b.tag == "fog") out.push_back("body:" + itos(b.id));
		}
	}
	return out;
}

std::string _reached_node(CombatWorld& w, const ActorState& a, const std::map<std::string, int>& reached) {
	for (const std::string& n : actor_nodes(w, a))
		if (reached.count(n)) return n;
	return actor_surface_node(w, a);
}

double node_factor(CombatWorld& w, const std::string& n) {
	if (begins_with(n, "body:")) {
		MatBody* b = w.get_body(to_int(std::string_view(n).substr(5)));
		if (b != nullptr) return Materials::conduction_factor(*b);
	}
	return 1.0;
}

std::string surface_node_at(CombatWorld& w, Vec3 p) {
	if (w.arena.in_pool(p.x, p.z) && p.y < w.arena.pool_level + 0.3) return "pool";
	if (w.arena.on_metal(p.x, p.z) && p.y < w.arena.metal_top + 0.3) return "metal";
	for (const BodyRef& bp : w.bodies) {
		const MatBody& b = *bp;
		if (b.alive && b.form == Form::Puddle && b.phase == Phase::Liquid)
			if (Vec2(p.x - b.pos.x, p.z - b.pos.z).length() < b.radius + 0.2) return "puddle:" + itos(b.id);
	}
	return "";
}

std::string body_node_at(CombatWorld& w, Vec3 p) {
	for (const BodyRef& bp : w.bodies) {
		const MatBody& b = *bp;
		if (!b.alive || !_is_body_node(b)) continue;
		if (b.form == Form::Zone) {
			if (w._in_zone(b, p, 0.2)) return "body:" + itos(b.id);
		} else if (b.pos.distance_to(p) < b.radius + 0.4) {
			return "body:" + itos(b.id);
		}
	}
	return "";
}

Vec3 node_point(CombatWorld& w, const std::string& n, Vec3 fallback) {
	const ArenaMap& ar = w.arena;
	if (n == "pool") return V3((ar.pool_min.x + ar.pool_max.x) * 0.5, ar.pool_level, (ar.pool_min.y + ar.pool_max.y) * 0.5);
	if (n == "metal") return V3((ar.metal_min.x + ar.metal_max.x) * 0.5, ar.metal_top, (ar.metal_min.y + ar.metal_max.y) * 0.5);
	if (begins_with(n, "puddle:")) {
		MatBody* b = w.get_body(to_int(std::string_view(n).substr(7)));
		if (b != nullptr) return b->pos;
	}
	if (begins_with(n, "body:")) {
		MatBody* b2 = w.get_body(to_int(std::string_view(n).substr(5)));
		if (b2 != nullptr) return b2->pos;
	}
	return fallback;
}

bool _circle_rect(Vec2 c, double r, Vec2 mn, Vec2 mx) {
	const Vec2 q(f32(clampf(c.x, mn.x, mx.x)), f32(clampf(c.y, mn.y, mx.y)));
	return q.distance_to(c) <= r;
}

bool _is_body_node(const MatBody& b) {
	if (b.form == Form::Puddle || b.form == Form::Pool) return false;
	return Materials::conducts(b);
}

void _link(Graph& g, const std::string& a, const std::string& b) {
	g[a].push_back(b);
	g[b].push_back(a);
}

Graph build_graph(CombatWorld& w) {
	Graph g;
	g["pool"];
	g["metal"];
	const ArenaMap& ar = w.arena;
	std::vector<MatBody*> puddles;
	for (const BodyRef& bp : w.bodies) {
		MatBody& b = *bp;
		if (b.alive && b.form == Form::Puddle && b.phase == Phase::Liquid) {
			puddles.push_back(&b);
			g["puddle:" + itos(b.id)];
		}
	}
	for (MatBody* b : puddles) {
		const std::string key = "puddle:" + itos(b->id);
		const Vec2 c(b->pos.x, b->pos.z);
		if (_circle_rect(c, b->radius, ar.pool_min, ar.pool_max)) {
			g[key].push_back("pool");
			g["pool"].push_back(key);
		}
		if (_circle_rect(c, b->radius, ar.metal_min, ar.metal_max)) {
			g[key].push_back("metal");
			g["metal"].push_back(key);
		}
		for (MatBody* o : puddles) {
			if (o->id > b->id && c.distance_to(Vec2(o->pos.x, o->pos.z)) <= b->radius + o->radius) {
				const std::string ok = "puddle:" + itos(o->id);
				g[key].push_back(ok);
				g[ok].push_back(key);
			}
		}
	}
	const double gap_x = maxf(ar.metal_min.x - ar.pool_max.x, ar.pool_min.x - ar.metal_max.x);
	const double gap_z = maxf(ar.metal_min.y - ar.pool_max.y, ar.pool_min.y - ar.metal_max.y);
	if (maxf(gap_x, gap_z) <= 0.05) {
		g["pool"].push_back("metal");
		g["metal"].push_back("pool");
	}
	std::vector<MatBody*> nodes;
	for (const BodyRef& bp : w.bodies) {
		MatBody& b = *bp;
		if (b.alive && _is_body_node(b)) {
			nodes.push_back(&b);
			g["body:" + itos(b.id)];
		}
	}
	for (MatBody* b : nodes) {
		const std::string key = "body:" + itos(b->id);
		const double r = b->form == Form::Zone ? b->zone_radius : b->radius;
		const Vec2 c2(b->pos.x, b->pos.z);
		const bool low = b->pos.y - r < ar.pool_level + 0.3 || b->form == Form::Zone;
		if (low && _circle_rect(c2, r, ar.pool_min, ar.pool_max) && b->pos.y < ar.pool_level + r + 0.3) _link(g, key, "pool");
		if (low && _circle_rect(c2, r, ar.metal_min, ar.metal_max) && b->pos.y < ar.metal_top + r + 0.3) _link(g, key, "metal");
		for (MatBody* p : puddles)
			if (c2.distance_to(Vec2(p->pos.x, p->pos.z)) <= r + p->radius && std::fabs(b->pos.y - p->pos.y) < r + 0.35)
				_link(g, key, "puddle:" + itos(p->id));
		for (MatBody* o : nodes) {
			if (o->id <= b->id) continue;
			const double ro = o->form == Form::Zone ? o->zone_radius : o->radius;
			bool touch = false;
			if (b->form == Form::Zone && o->form != Form::Zone) touch = w._in_zone(*b, o->pos, o->radius);
			else if (o->form == Form::Zone && b->form != Form::Zone) touch = w._in_zone(*o, b->pos, b->radius);
			else touch = b->pos.distance_to(o->pos) <= r + ro + 0.1;
			if (touch) _link(g, key, "body:" + itos(o->id));
		}
	}
	return g;
}

std::map<std::string, int> bfs(const Graph& g, const std::vector<std::string>& seeds, int max_hops, std::vector<std::string>* order) {
	std::map<std::string, int> seen;
	std::deque<std::string> frontier;
	for (const std::string& s : seeds) {
		if (g.count(s) && !seen.count(s)) {
			seen[s] = 0;
			if (order) order->push_back(s);
			frontier.push_back(s);
		}
	}
	while (!frontier.empty()) {
		const std::string n = frontier.front();
		frontier.pop_front();
		const int depth = seen[n];
		if (depth >= max_hops) continue;
		auto it = g.find(n);
		if (it == g.end()) continue;
		for (const std::string& m : it->second) {
			if (!seen.count(m)) {
				seen[m] = depth + 1;
				if (order) order->push_back(m);
				frontier.push_back(m);
			}
		}
	}
	return seen;
}

}  // namespace Conduction
}  // namespace ff
