// Fourfold core - port of game/ui/lab/matrix_query.gd: the Lab matrix viewer's engine. A threat (a SpawnCatalog entry
// at mass / speed / heat / tier) against a counter (a registry move at a tier, perfect or not, or an environment /
// legacy guard class) answered by Interactions.predict, in a scratch CombatWorld (nothing spawned in the live world).
#pragma once

#include "ff/Value.h"
#include "ff/ViewModels.h"
#include "Sim/Agent.h"

#include <memory>
#include <string>
#include <vector>

namespace ff {

class CombatWorld;
class ActorState;
class MatBody;

namespace MatrixQuery {

struct Threat {
	bool ok = false;
	AgentRef agent;
	std::unique_ptr<CombatWorld> world;
	ActorState* player = nullptr;
	ActorState* rival = nullptr;
	MatBody* body = nullptr;
	std::string summary;
};

const std::vector<Dict>& counters();          // [{id, label, kind: move|env|legacy, max_tier, element, sub, slot}]
Dict find_counter(const std::string& id);
Threat make_threat(const std::string& entry_id, const Dict& params);
AgentRef make_counter(CombatWorld& sw, ActorState& p, const Dict& c, int tier, bool perfect);
// {ok, msg, tp, cp, cp_eff, ratio, band, perfect, outcome, rule_id, to, threat_cls, counter_cls, needs, summary}
Dict predict(const std::string& entry_id, const Dict& threat_params, const std::string& counter_id, int counter_tier, bool perfect);
MatrixCounter to_view(const Dict& c);
MatrixResult result_view(const Dict& r);

}  // namespace MatrixQuery
}  // namespace ff
