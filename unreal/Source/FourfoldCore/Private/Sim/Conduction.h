// Fourfold core - port of game/core/conduction.gd: lightning as a bounded conduction graph, built once per discharge.
// Nodes: the pool, the metal plate, liquid puddles and conductive bodies (metal, rods, caltrops zones, liquid water
// bodies, fog zones at 60 %, charged bodies) plus actors standing on / in / holding them. Barriers on the path are
// counters (Interactions). One discharge per bolt, max hops, fixed damage budget, one redirect per bolt.
// Result dictionaries: {path: [Vec3...], arcs: [[Vec3, Vec3]...], hits: [actor ids], blocked: bool, e: float}.
#pragma once

#include "ff/Math.h"
#include "ff/Value.h"

#include <map>
#include <string>
#include <vector>

namespace ff {

class CombatWorld;
class ActorState;
class MatBody;
class Agent;

namespace Conduction {

inline constexpr double MIN_SHARE = 5.0;

struct BarrierHit {
	double t = 0.0;
	MatBody* body = nullptr;    // nullptr = arena solid
};

using Graph = std::map<std::string, std::vector<std::string>>;

Dict discharge(CombatWorld& w, ActorState& caster, Vec3 aim, const Dict& def, int attack_id, bool allow_redirect, double dmg_scale = 1.0);
Dict meet_path_bodies(CombatWorld& w, ActorState& caster, Agent& bolt, Vec3 p0, Vec3 p1);
void _forks(CombatWorld& w, ActorState& caster, Dict& out, Vec3 at, int n, double e_val, int attack_id, const Dict& def);
Dict rail(CombatWorld& w, ActorState& caster, Vec3 dir, const Dict& def, int attack_id);
Dict relay(CombatWorld& w, ActorState& caster, const std::vector<MatBody*>& nodes, const Dict& def, int attack_id);
void _conduct_from(CombatWorld& w, ActorState& caster, Dict& out, const std::vector<std::string>& seeds, Vec3 at, double budget,
                   int max_hops, int attack_id);
Dict _blocked(CombatWorld& w, ActorState& caster, Dict& out, Vec3 start, Vec3 stop);
std::vector<BarrierHit> barriers_on(CombatWorld& w, Vec3 p0, Vec3 p1);
double _cyl_t(Vec3 p0, Vec3 p1, Vec3 c, double r, double h);
std::string actor_surface_node(CombatWorld& w, const ActorState& a);
std::vector<std::string> actor_nodes(CombatWorld& w, const ActorState& a);
std::string _reached_node(CombatWorld& w, const ActorState& a, const std::map<std::string, int>& reached);
double node_factor(CombatWorld& w, const std::string& n);
std::string surface_node_at(CombatWorld& w, Vec3 p);
std::string body_node_at(CombatWorld& w, Vec3 p);
Vec3 node_point(CombatWorld& w, const std::string& n, Vec3 fallback);
bool _circle_rect(Vec2 c, double r, Vec2 mn, Vec2 mx);
bool _is_body_node(const MatBody& b);
Graph build_graph(CombatWorld& w);
void _link(Graph& g, const std::string& a, const std::string& b);
// node -> depth, plus the insertion order (Godot dictionary order) in `order`.
std::map<std::string, int> bfs(const Graph& g, const std::vector<std::string>& seeds, int max_hops, std::vector<std::string>* order = nullptr);

}  // namespace Conduction
}  // namespace ff
