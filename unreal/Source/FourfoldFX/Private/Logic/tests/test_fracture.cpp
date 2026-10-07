// FourfoldFX logic island - Voronoi fracture pieces (FxFracture). Empty in the Unreal build.
#if defined(FF_LOGIC_TESTS)
#include "FxTest.h"

#include "FxDirector.h"
#include "FxFracture.h"
#include "FxMeshLib.h"

#include <array>
#include <cmath>
#include <map>

using namespace ffx;

namespace {

MeshData Cube(float h) {
	MeshData m;
	const Vec3 X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);
	// AppendQuad's front face is hx x hy: pick each pair so it points outward
	AppendQuad(m, X * h, Y * h, Z * h);
	AppendQuad(m, X * -h, Z * h, Y * h);
	AppendQuad(m, Y * h, Z * h, X * h);
	AppendQuad(m, Y * -h, X * h, Z * h);
	AppendQuad(m, Z * h, X * h, Y * h);
	AppendQuad(m, Z * -h, Y * h, X * h);
	return m;
}

using Key = std::array<long long, 3>;
Key Q(const Vec3& p) {
	return {std::llround(static_cast<double>(p.x) * 1e4), std::llround(static_cast<double>(p.y) * 1e4),
	        std::llround(static_cast<double>(p.z) * 1e4)};
}

// Directed edges without their reverse (0 for a closed, consistently wound surface).
int OpenEdges(const MeshData& m) {
	std::map<std::pair<Key, Key>, int> count;
	for (size_t i = 0; i + 2 < m.idx.size(); i += 3)
		for (size_t k = 0; k < 3; ++k) {
			const Key a = Q(m.pos[static_cast<size_t>(m.idx[i + k])]);
			const Key b = Q(m.pos[static_cast<size_t>(m.idx[i + (k + 1) % 3])]);
			if (a != b) ++count[{a, b}];
		}
	int open = 0;
	for (const auto& kv : count) {
		const auto rev = count.find({kv.first.second, kv.first.first});
		if (rev == count.end() || rev->second != kv.second) ++open;
	}
	return open;
}

}  // namespace

FXT_TEST(fracture_clip_closes_the_cut) {
	const MeshData cube = Cube(1.0f);
	FXT_NEAR(MeshVolume(cube), 8.0f, 1e-4);
	FXT_CHECK(OpenEdges(cube) == 0);
	MeshData half;
	const Vec3 n = Norm(Vec3(0.3f, 1.0f, -0.2f));
	FXT_CHECK(ClipClosedMesh(cube, n, 0.0f, half));
	FXT_CHECK(half.Valid());
	FXT_CHECK(OpenEdges(half) == 0);
	FXT_NEAR(MeshVolume(half), 4.0f, 1e-3);   // a plane through the centre halves the cube
	int capTris = 0;
	for (size_t i = 0; i + 2 < half.idx.size(); i += 3) {
		const Vec3& a = half.pos[static_cast<size_t>(half.idx[i])];
		const Vec3& b = half.pos[static_cast<size_t>(half.idx[i + 1])];
		const Vec3& c = half.pos[static_cast<size_t>(half.idx[i + 2])];
		const Vec3 fn = Norm((b - a).cross(c - a));
		if (std::fabs(n.dot(a)) < 1e-4f && std::fabs(n.dot(b)) < 1e-4f && std::fabs(n.dot(c)) < 1e-4f) {
			++capTris;
			FXT_CHECK(fn.dot(n) > 0.99f);   // cap faces outward (+n)
		}
		for (int k = 0; k < 3; ++k) FXT_CHECK(n.dot(half.pos[static_cast<size_t>(half.idx[i + static_cast<size_t>(k)])]) < 1e-4f);
	}
	FXT_CHECK(capTris >= 3);
	// everything kept / nothing kept
	MeshData all, none;
	FXT_CHECK(ClipClosedMesh(cube, n, 5.0f, all));
	FXT_NEAR(MeshVolume(all), 8.0f, 1e-4);
	FXT_CHECK(!ClipClosedMesh(cube, n, -5.0f, none));
}

FXT_TEST(fracture_rock_pieces_rebuild_the_rock) {
	for (uint32_t seed : {3u, 17u, 40u}) {
		const MeshData& rock = meshlib::Rock(seed);
		const float vol = MeshVolume(rock);
		FXT_CHECK(vol > 0.5f);
		FXT_CHECK(OpenEdges(rock) == 0);
		const std::vector<FracturePiece>& ps = meshlib::RockPieces(seed, 6);
		FXT_CHECK(ps.size() >= 4 && ps.size() <= 6);
		float sum = 0.0f;
		for (const FracturePiece& p : ps) {
			FXT_CHECK(p.mesh.Valid());
			FXT_CHECK(OpenEdges(p.mesh) == 0);
			FXT_CHECK(p.volume > 0.0f);
			FXT_CHECK(p.hull.size() >= 4 && p.hull.size() <= 42);
			FXT_CHECK(p.centre.length() < 1.2f);
			FXT_CHECK(p.mesh.uid != rock.uid);
			sum += p.volume;
		}
		FXT_NEAR(sum, vol, vol * 0.02f);   // dropped slivers stay under 2 %
		// cached and stable
		FXT_CHECK(&meshlib::RockPieces(seed, 6) == &ps);
	}
}

FXT_TEST(fracture_wall_pieces_and_sites) {
	std::vector<MeshData> open, blocks;
	meshlib::BuildWallBlocks(open, 5u, false);
	meshlib::BuildWallBlocks(blocks, 5u, true);
	FXT_CHECK(blocks.size() == 5 && open.size() == 5);
	MeshData wall, joined;
	meshlib::BuildWall(wall, 5u);
	for (const MeshData& b : open) joined.Append(b);
	FXT_CHECK(joined.pos.size() == wall.pos.size() && joined.idx == wall.idx);   // the drawn wall is unchanged
	float vol = 0.0f;
	for (const MeshData& b : blocks) {
		FXT_CHECK(OpenEdges(b) == 0);
		vol += MeshVolume(b);
	}
	FXT_CHECK(vol > 0.3f);
	for (int per : {1, 3}) {
		const std::vector<FracturePiece>& ps = meshlib::WallPieces(5u, per);
		FXT_CHECK(static_cast<int>(ps.size()) >= 5 * per - 2 && static_cast<int>(ps.size()) <= 5 * per);
		float sum = 0.0f;
		for (const FracturePiece& p : ps) {
			FXT_CHECK(OpenEdges(p.mesh) == 0);
			sum += p.volume;
		}
		FXT_NEAR(sum, vol, vol * 0.02f);
	}
	// sites: deterministic, inside the ellipsoid, spread out
	const std::vector<Vec3> a = FractureSites(9u, 6, Vec3(1, 2, 3), Vec3(0.5f, 0.3f, 0.5f));
	const std::vector<Vec3> b = FractureSites(9u, 6, Vec3(1, 2, 3), Vec3(0.5f, 0.3f, 0.5f));
	FXT_CHECK(a.size() == 6);
	float minD = 1e9f;
	for (size_t i = 0; i < a.size(); ++i) {
		FXT_CHECK(a[i] == b[i]);
		const Vec3 u = a[i] - Vec3(1, 2, 3);
		FXT_CHECK((u.x / 0.5f) * (u.x / 0.5f) + (u.y / 0.3f) * (u.y / 0.3f) + (u.z / 0.5f) * (u.z / 0.5f) <= 1.0001f);
		for (size_t j = i + 1; j < a.size(); ++j) minD = MinF(minD, (a[i] - a[j]).length());
	}
	FXT_CHECK(minD > 0.12f);
}

namespace {

ff::BodyView FBody(int id, ff::Mat m, ff::Form f, const Vec3& pos, const std::string& tag = "") {
	ff::BodyView b;
	b.id = id;
	b.mat = m;
	b.form = f;
	b.tag = tag;
	b.phase = m == ff::Mat::Water ? ff::Phase::Frozen : ff::Phase::Solid;
	b.pos = pos;
	b.radius = 0.3f;
	b.mass = 10.0f;
	b.props = ff::Value(ff::Dict());
	return b;
}

ff::Event FEv(const std::string& type, int body) {
	ff::Event e;
	e.type = type;
	ff::Dict d;
	d.set("body", ff::Value(body));
	e.data = ff::Value(d);
	return e;
}

struct FFrame {
	ff::Snapshot prev, curr;
	std::vector<ff::Event> events;
	FxFrameIn in;
	FFrame() {
		in.prev = &prev;
		in.curr = &curr;
		in.events = &events;
		in.dt = 1.0f / 60.0f;
		in.quality = 2;
	}
	void Next() {
		prev = curr;
		++curr.tick;
		events.clear();
	}
};

int CountMat(const DrawList& dl, MatSlot m) {
	int n = 0;
	for (const DrawItem& it : dl.items) n += it.mat == m ? 1 : 0;
	return n;
}

}  // namespace

FXT_TEST(fracture_stone_shatter_requests_pieces) {
	for (bool physics : {true, false}) {
		FxDirector d;
		FFrame f;
		f.in.physicsDebris = physics;
		f.curr.bodies = {FBody(7, ff::Mat::Stone, ff::Form::Chunk, Vec3(1.0f, 0.5f, 2.0f))};
		f.curr.bodies[0].vel = Vec3(0.0f, 0.0f, 6.0f);
		d.Update(f.in);
		f.Next();
		f.events.push_back(FEv("shatter", 7));
		const DrawList& dl = d.Update(f.in);
		if (physics) {
			FXT_CHECK(dl.fractures.size() == 1);
			const FractureReq& r = dl.fractures[0];
			FXT_CHECK(r.pieces && r.pieces->size() >= 4 && r.pieces->size() <= 6);
			FXT_NEAR((r.xform.pos - Vec3(1.0f, 0.5f, 2.0f)).length(), 0.0f, 1e-3);
			FXT_NEAR(r.xform.basis.x.length(), 0.3f, 1e-3);   // the stone's drawn size
			FXT_CHECK(r.mat == MatSlot::Rock && r.scale > 0.0f && r.scale <= 1.0f && r.life > 0.5f);
			FXT_CHECK(r.vel.z > 0.0f);
			FXT_CHECK(CountMat(dl, MatSlot::Rock) >= 1);   // the stone itself lives on (the sim keeps the body)
			FXT_CHECK(d.Stats().fractures == 1);
		} else {
			FXT_CHECK(dl.fractures.empty());
			FXT_CHECK(d.Stats().oneShots > 0);   // procedural chips instead
		}
	}
	// low quality: no physics pieces even when the glue could
	FxDirector d;
	FFrame f;
	f.in.physicsDebris = true;
	f.in.quality = 0;
	f.curr.bodies = {FBody(3, ff::Mat::Stone, ff::Form::Chunk, Vec3())};
	d.Update(f.in);
	f.Next();
	f.events.push_back(FEv("shatter", 3));
	FXT_CHECK(d.Update(f.in).fractures.empty());
	// ice keeps its splash + shards
	FxDirector di;
	FFrame fi;
	fi.in.physicsDebris = true;
	fi.curr.bodies = {FBody(4, ff::Mat::Water, ff::Form::Chunk, Vec3(), "ice")};
	di.Update(fi.in);
	fi.Next();
	fi.events.push_back(FEv("shatter", 4));
	FXT_CHECK(di.Update(fi.in).fractures.empty());
	FXT_CHECK(di.Stats().oneShots > 0);
}

FXT_TEST(fracture_wall_crumble_replaces_the_wall) {
	for (const char* style : {"", "sand"}) {
		FxDirector d;
		FFrame f;
		f.in.physicsDebris = true;
		ff::BodyView w = FBody(9, style[0] ? ff::Mat::Sand : ff::Mat::Stone, ff::Form::Wall, Vec3(0.0f, 0.0f, -3.0f));
		w.wall_rise = 1.0f;
		w.wall_half = Vec3(1.2f, 0.6f, 0.25f);
		f.curr.bodies = {w};
		const DrawList& first = d.Update(f.in);
		FXT_CHECK(CountMat(first, MatSlot::Rock) == 1);
		// the sim removes the wall in the tick that emits wall_crumble
		f.Next();
		f.curr.bodies.clear();
		f.events.push_back(FEv("wall_crumble", 9));
		const DrawList& dl = d.Update(f.in);
		if (style[0]) {
			FXT_CHECK(dl.fractures.empty());   // sand slumps
			continue;
		}
		FXT_CHECK(first.colliders.empty() || first.colliders.size() == 1);
		FXT_CHECK(dl.colliders.empty());   // broken: no longer a solid
		FXT_CHECK(dl.fractures.size() == 1);
		const FractureReq& r = dl.fractures[0];
		FXT_CHECK(r.pieces && r.pieces->size() >= 10);
		FXT_NEAR((r.xform.pos - Vec3(0.0f, 0.0f, -3.0f)).length(), 0.0f, 1e-3);
		FXT_CHECK(CountMat(dl, MatSlot::Rock) == 0);   // the wall stops drawing at once
		bool dustNearWall = false;
		for (const SystemReq& sr : dl.systems) dustNearWall = dustNearWall || (sr.pos - Vec3(0.0f, 0.0f, -3.0f)).length() < 1.0f;
		for (const DrawItem& it : dl.items) dustNearWall = dustNearWall || (it.xform.pos - Vec3(0.0f, 0.0f, -3.0f)).length() < 1.5f;
		FXT_CHECK(dustNearWall);   // the cue finds the removed wall (not the world origin)
	}
}

FXT_TEST(fracture_walls_are_colliders_for_debris) {
	FxDirector d;
	FFrame f;
	ff::BodyView w = FBody(5, ff::Mat::Stone, ff::Form::Wall, Vec3(2.0f, 0.0f, 0.0f));
	w.wall_rise = 1.0f;
	w.wall_half = Vec3(1.5f, 0.75f, 0.25f);
	w.wall_yaw = 0.4f;
	f.curr.bodies = {w};
	FXT_CHECK(d.Update(f.in).colliders.empty());   // no physics debris: nothing to bounce
	f.Next();
	f.in.physicsDebris = true;
	const DrawList& dl = d.Update(f.in);
	FXT_CHECK(dl.colliders.size() == 1);
	const ColliderReq& c = dl.colliders[0];
	FXT_NEAR(c.xform.pos.y, 0.75f, 1e-3);                 // centre half way up
	FXT_NEAR(c.xform.basis.x.length(), 1.5f, 1e-3);        // half length
	FXT_NEAR(c.xform.basis.y.length(), 0.75f, 1e-3);       // half height
	FXT_NEAR(c.xform.basis.z.length(), 0.27f, 1e-3);       // half thickness (unit wall z +-0.27 x 4 half.z)
	const uint32_t key = c.key;
	f.Next();
	const DrawList& again = d.Update(f.in);
	FXT_CHECK(again.colliders.size() == 1 && again.colliders[0].key == key);   // stable per body
}

FXT_TEST(fracture_impacts_kick_up_dust) {
	FxDirector d;
	FFrame f;
	d.Update(f.in);
	const int before = d.Stats().oneShots;
	std::vector<DebrisImpact> hits = {{Vec3(1.0f, 0.0f, 1.0f), 0.5f, 0.3f},   // too soft: nothing
	                                  {Vec3(2.0f, 0.0f, 1.0f), 6.0f, 0.3f}};
	f.Next();
	f.in.debrisImpacts = &hits;
	const DrawList& dl = d.Update(f.in);
	FXT_CHECK(d.Stats().oneShots == before + 1);
	bool near = false;
	for (const SystemReq& s : dl.systems) near = near || (s.cue == NCue::Dust && (s.pos - Vec3(2.0f, 0.0f, 1.0f)).length() < 0.5f);
	FXT_CHECK(near);
}

#endif  // FF_LOGIC_TESTS
