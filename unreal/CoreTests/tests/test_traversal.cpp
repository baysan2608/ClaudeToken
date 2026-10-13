// Traversal: the leap (jump_pressed) and the cloud step, through the real input path (ff::Session).
#include "ff_test.h"

#include "ff/FourfoldCore.h"

#include <cmath>
#include <string>
#include <vector>

using namespace ff;
using fft::S;

namespace {
constexpr float kYaw = 3.14159265f;   // camera along -z

struct LeapFx : fft::TestCase {
	InputFrame in;
	std::vector<Event> log;
	void step(Session& s, int n = 1) {
		for (int i = 0; i < n; ++i) {
			s.Step(in, kYaw);
			in.ClearEdges();
		}
		s.TakeEvents(log);
	}
	const ActorView& me(const Session& s) const { return *s.GetSnapshot().FindActor(s.GetSnapshot().player_id); }
	int jumps(const std::string& kind) const {
		int n = 0;
		for (const Event& e : log)
			if (e.type == "jump" && e.data.get("kind").as_string() == kind) ++n;
		return n;
	}
};
}  // namespace

FF_TEST_F(test_traversal, LeapFx, test_standing_hop_reaches_about_a_metre) {
	Session s;
	s.LoadScenario("lab");
	step(s, 30);
	const float y0 = me(s).pos.y;
	in.jump_pressed = true;
	float peak = y0;
	for (int t = 0; t < 60; ++t) {
		step(s);
		peak = std::fmax(peak, me(s).pos.y);
	}
	check(jumps("leap") == 1, "one leap event");
	near(peak - y0, 1.2, 0.12, "hop height");
	check(me(s).grounded, "landed again");
}

FF_TEST_F(test_traversal, LeapFx, test_running_leap_carries_and_cloud_step_climbs) {
	Session s;
	s.LoadScenario("lab");
	step(s, 30);
	in.move = Vec2(0.0f, 1.0f);
	step(s, 40);   // up to speed
	const Vec3 p0 = me(s).pos;
	const float focus0 = s.BuildHud().focus;
	in.jump_pressed = true;
	step(s, 20);
	in.jump_pressed = true;   // cloud step near the top
	float peak = p0.y;
	for (int t = 0; t < 90 && !(t > 5 && me(s).grounded); ++t) {
		step(s);
		peak = std::fmax(peak, me(s).pos.y);
	}
	check(jumps("cloud_step") == 1, "cloud step fired");
	check(peak - p0.y > 1.8, S("two jumps clear the high ledge height (peak ", peak - p0.y, ")"));
	check(std::fabs(me(s).pos.z - p0.z) > 4.0, S("the leap carried forward (dz ", me(s).pos.z - p0.z, ")"));
	check(s.BuildHud().focus < focus0, "cloud step cost focus");
}

FF_TEST_F(test_traversal, LeapFx, test_no_leap_during_a_move_and_only_one_cloud_step) {
	Session s;
	s.LoadScenario("lab");
	step(s, 30);
	in.attack_pressed = true;
	step(s, 2);
	in.jump_pressed = true;
	step(s, 2);
	check(jumps("leap") == 0, "a strike in progress cannot be cancelled by a leap");
	step(s, 60);
	in.jump_pressed = true;
	step(s, 10);
	in.jump_pressed = true;
	step(s, 6);
	in.jump_pressed = true;
	step(s, 6);
	check(jumps("leap") == 1 && jumps("cloud_step") == 1, S("leap ", jumps("leap"), " cloud ", jumps("cloud_step")));
}
