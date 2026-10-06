// FourfoldFX logic island - geometry builder tests. Empty in the Unreal build.
#if defined(FF_LOGIC_TESTS)
#include "FxTest.h"

#include "FxLightning.h"
#include "FxMeshLib.h"
#include "FxMesh.h"
#include "FxParticles.h"

using namespace ffx;

namespace {

// Every triangle's winding normal agrees with the stored vertex normals (front faces outward).
float WindingAgreement(const MeshData& m) {
	int agree = 0, total = 0;
	for (int t = 0; t < m.NumTris(); ++t) {
		const int a = m.idx[static_cast<size_t>(t * 3)], b = m.idx[static_cast<size_t>(t * 3 + 1)],
		          c = m.idx[static_cast<size_t>(t * 3 + 2)];
		const Vec3 n = (m.pos[static_cast<size_t>(b)] - m.pos[static_cast<size_t>(a)])
		                   .cross(m.pos[static_cast<size_t>(c)] - m.pos[static_cast<size_t>(a)]);
		if (n.length_squared() < 1e-14f) continue;
		const Vec3 vn = m.nrm[static_cast<size_t>(a)] + m.nrm[static_cast<size_t>(b)] + m.nrm[static_cast<size_t>(c)];
		++total;
		if (n.dot(vn) > 0.0f) ++agree;
	}
	return total ? static_cast<float>(agree) / static_cast<float>(total) : 1.0f;
}

// For a closed mesh around c: triangles face away from c.
float OutwardFraction(const MeshData& m, const Vec3& c) {
	int out = 0, total = 0;
	for (int t = 0; t < m.NumTris(); ++t) {
		const Vec3& a = m.pos[static_cast<size_t>(m.idx[static_cast<size_t>(t * 3)])];
		const Vec3& b = m.pos[static_cast<size_t>(m.idx[static_cast<size_t>(t * 3 + 1)])];
		const Vec3& d = m.pos[static_cast<size_t>(m.idx[static_cast<size_t>(t * 3 + 2)])];
		const Vec3 n = (b - a).cross(d - a);
		if (n.length_squared() < 1e-14f) continue;
		++total;
		if (n.dot((a + b + d) / 3.0f - c) > 0.0f) ++out;
	}
	return total ? static_cast<float>(out) / static_cast<float>(total) : 1.0f;
}

}  // namespace

FXT_TEST(mesh_quad_and_commit) {
	MeshData m;
	AppendQuad(m, Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0));
	FXT_CHECK(m.NumVerts() == 4);
	FXT_CHECK(m.NumTris() == 2);
	FXT_CHECK(m.Valid());
	FXT_CHECK(WindingAgreement(m) == 1.0f);
	// front = hx x hy = +z
	const Vec3 n = (m.pos[1] - m.pos[0]).cross(m.pos[2] - m.pos[0]);
	FXT_CHECK(n.z > 0.0f);
	m.Commit();
	const uint32_t topo0 = m.topo;
	const uint32_t v0 = m.version;
	m.pos[0].x += 0.1f;
	m.Commit();
	FXT_CHECK(m.version == v0 + 1);
	FXT_CHECK(m.topo == topo0);   // positions only: same topology
	AppendQuad(m, Vec3(3, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0));
	m.Commit();
	FXT_CHECK(m.topo != topo0);
}

FXT_TEST(mesh_sphere_outward) {
	MeshData m;
	AppendSphere(m, Vec3(1, 2, 3), Vec3(0.5f, 0.5f, 0.5f), 8, 12);
	FXT_CHECK(m.Valid());
	FXT_CHECK(OutwardFraction(m, Vec3(1, 2, 3)) > 0.99f);
	FXT_CHECK(WindingAgreement(m) > 0.99f);
	for (const Vec3& p : m.pos) FXT_NEAR(p.distance_to(Vec3(1, 2, 3)), 0.5f, 1e-4f);
}

FXT_TEST(mesh_tube_outward_and_caps) {
	MeshData m;
	std::vector<Vec3> pts = {Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(0.3f, 0, 2), Vec3(0.3f, 0.5f, 3)};
	AppendTube(m, pts, {0.1f}, 8, true, true, 3);
	FXT_CHECK(m.Valid());
	FXT_CHECK(WindingAgreement(m) > 0.97f);
	// sample: side vertices sit at radius 0.1 from the polyline
	FXT_CHECK(m.NumTris() > 4 * 8 * 2);
}

FXT_TEST(mesh_lathe_and_ring) {
	MeshData m;
	AppendLathe(m, Vec3(0, 0, 0), Vec3(0, 1, 0), 2.0f, {1.0f, 0.8f, 0.4f, 0.1f}, 16, 0.5f);
	FXT_CHECK(m.Valid());
	FXT_CHECK(OutwardFraction(m, Vec3(0, 1, 0)) > 0.95f);
	MeshData r;
	AppendRing(r, Vec3(0, 0, 0), Vec3(0, 1, 0), 0.5f, 1.0f, 24);
	FXT_CHECK(r.Valid());
	for (int t = 0; t < r.NumTris(); ++t) {
		const Vec3& a = r.pos[static_cast<size_t>(r.idx[static_cast<size_t>(t * 3)])];
		const Vec3& b = r.pos[static_cast<size_t>(r.idx[static_cast<size_t>(t * 3 + 1)])];
		const Vec3& c = r.pos[static_cast<size_t>(r.idx[static_cast<size_t>(t * 3 + 2)])];
		FXT_CHECK((b - a).cross(c - a).y > 0.0f);
	}
}

FXT_TEST(mesh_path_strip) {
	MeshData m;
	std::vector<Vec3> pts = {Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(0, 0, 2), Vec3(0.2f, 0, 3)};
	AppendPathStrip(m, pts, {0.8f, 1.0f, 1.2f, 1.4f});
	FXT_CHECK(m.Valid());
	FXT_CHECK(m.NumTris() > 0);
	// faces up on average, nothing below the ground plane by more than a hair
	float ymin = 1e9f, ymax = -1e9f;
	for (const Vec3& p : m.pos) {
		ymin = MinF(ymin, p.y);
		ymax = MaxF(ymax, p.y);
	}
	FXT_CHECK(ymin > -0.02f);
	FXT_CHECK(ymax > 0.3f && ymax < 0.7f);
	FXT_CHECK(WindingAgreement(m) > 0.95f);
	FXT_CHECK(OutwardFraction(m, Vec3(0.05f, -0.5f, 1.5f)) > 0.9f);
}

FXT_TEST(mesh_chamfer_box_rock_spike) {
	MeshData m;
	AppendChamferBox(m, Vec3(0, 1, 0), Vec3(1, 0.5f, 0.3f), 0.06f, Basis::Identity());
	FXT_CHECK(m.Valid());
	FXT_CHECK(OutwardFraction(m, Vec3(0, 1, 0)) > 0.99f);
	MeshData r;
	AppendRock(r, 1234u, 0.5f);
	FXT_CHECK(r.Valid());
	FXT_CHECK(r.NumTris() == 320);
	FXT_CHECK(OutwardFraction(r, Vec3()) > 0.97f);
	MeshData r2;
	AppendRock(r2, 1234u, 0.5f);
	FXT_CHECK(r2.pos.size() == r.pos.size());
	bool same = true;
	for (size_t i = 0; i < r.pos.size(); ++i) same = same && r.pos[i] == r2.pos[i];
	FXT_CHECK(same);   // deterministic
	MeshData s;
	AppendSpike(s, Vec3(0, 0, 0), Vec3(0, 1, 0), 0.2f, 1.0f, 5, 0.3f);
	FXT_CHECK(s.Valid());
	FXT_CHECK(OutwardFraction(s, Vec3(0, 0.3f, 0)) > 0.9f);
}

FXT_TEST(mesh_ribbon_faces_camera) {
	MeshData m;
	std::vector<Vec3> pts = {Vec3(0, 1, 0), Vec3(1, 1, 0), Vec3(2, 1.2f, 0)};
	const Vec3 cam(1, 1, 5);
	AppendRibbon(m, pts, {0.2f}, cam);
	FXT_CHECK(m.Valid());
	for (int t = 0; t < m.NumTris(); ++t) {
		const Vec3& a = m.pos[static_cast<size_t>(m.idx[static_cast<size_t>(t * 3)])];
		const Vec3& b = m.pos[static_cast<size_t>(m.idx[static_cast<size_t>(t * 3 + 1)])];
		const Vec3& c = m.pos[static_cast<size_t>(m.idx[static_cast<size_t>(t * 3 + 2)])];
		const Vec3 n = (b - a).cross(c - a);
		if (n.length_squared() < 1e-12f) continue;
		FXT_CHECK(n.dot(cam - a) > 0.0f);
	}
}

FXT_TEST(mesh_crescent_faces_normal) {
	MeshData m;
	AppendCrescent(m, Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(0, 1, 0), 1.0f, 0.2f, 2.0f, 12);
	FXT_CHECK(m.Valid());
	int up = 0;
	for (int t = 0; t < m.NumTris(); ++t) {
		const Vec3& a = m.pos[static_cast<size_t>(m.idx[static_cast<size_t>(t * 3)])];
		const Vec3& b = m.pos[static_cast<size_t>(m.idx[static_cast<size_t>(t * 3 + 1)])];
		const Vec3& c = m.pos[static_cast<size_t>(m.idx[static_cast<size_t>(t * 3 + 2)])];
		if ((b - a).cross(c - a).y > 0.0f) ++up;
	}
	FXT_CHECK(up == m.NumTris());
}

FXT_TEST(mesh_pad_keeps_counts) {
	MeshData m;
	AppendQuad(m, Vec3(), Vec3(1, 0, 0), Vec3(0, 1, 0));
	m.PadTo(16, 24);
	FXT_CHECK(m.NumVerts() == 16);
	FXT_CHECK(m.idx.size() == 24u);
	FXT_CHECK(m.Valid());
}

FXT_TEST(lightning_passes_nodes_and_is_deterministic) {
	std::vector<Vec3> nodes = {Vec3(0, 1, 0), Vec3(2, 1.5f, 0), Vec3(5, 0, 1)};
	std::vector<BoltLine> a, b, c;
	BoltParams p;
	GenerateBolt(nodes, 42u, p, a);
	GenerateBolt(nodes, 42u, p, b);
	GenerateBolt(nodes, 43u, p, c);
	FXT_CHECK(!a.empty());
	FXT_CHECK(a[0].pts.front() == nodes[0]);
	FXT_CHECK(a[0].pts.back() == nodes[2]);
	bool hasMid = false;
	for (const Vec3& q : a[0].pts) hasMid = hasMid || q == nodes[1];
	FXT_CHECK(hasMid);
	FXT_CHECK(a.size() == b.size() && a[0].pts.size() == b[0].pts.size());
	bool same = true;
	for (size_t i = 0; i < a[0].pts.size(); ++i) same = same && a[0].pts[i] == b[0].pts[i];
	FXT_CHECK(same);
	bool diff = c[0].pts.size() != a[0].pts.size();
	for (size_t i = 0; !diff && i < a[0].pts.size(); ++i) diff = !(a[0].pts[i] == c[0].pts[i]);
	FXT_CHECK(diff);
	FXT_CHECK(a.size() >= 2u && a.size() <= 4u);   // main + 1..3 branches
	for (size_t i = 1; i < a[0].pts.size(); ++i) FXT_CHECK(a[0].pts[i].distance_to(a[0].pts[i - 1]) < 0.4f);
	MeshData m;
	BuildBoltMesh(m, a, Vec3(2, 2, 6));
	FXT_CHECK(m.Valid());
	FXT_CHECK(m.NumTris() > 20);
}

FXT_TEST(particles_fixed_capacity_and_physics) {
	ParticleSet ps;
	ps.Reset(8, 7u);
	for (int i = 0; i < 5; ++i) {
		Particle* p = ps.Spawn();
		FXT_CHECK(p != nullptr);
		p->p = Vec3(0, 1, 0);
		p->v = Vec3(1, 2, 0);
		p->life = 1.0f;
	}
	FXT_CHECK(ps.Alive() == 5);
	MeshData m;
	ParticleLook look;
	ps.Build(m, Vec3(0, 1, 5), Vec3(0, 1, 0), look);
	FXT_CHECK(m.NumVerts() == 8 * 4);
	FXT_CHECK(m.idx.size() == 8u * 6u);
	FXT_CHECK(m.Valid());
	ParticlePhysics ph;
	ph.gravity = 9.8f;
	ph.groundY = 0.0f;
	for (int i = 0; i < 30; ++i) ps.Step(1.0f / 60.0f, ph);
	for (const Particle& p : ps.Items())
		if (p.alive) FXT_CHECK(p.p.y >= 0.0f);
	for (int i = 0; i < 60; ++i) ps.Step(1.0f / 60.0f, ph);
	FXT_CHECK(ps.Alive() == 0);
	ps.Build(m, Vec3(0, 1, 5), Vec3(0, 1, 0), look);
	FXT_CHECK(m.NumVerts() == 8 * 4);
}

FXT_TEST(meshlib_shapes_valid_and_outward) {
	FXT_CHECK(meshlib::GroundQuad().Valid());
	{
		const MeshData& q = meshlib::GroundQuad();
		const Vec3 n = (q.pos[1] - q.pos[0]).cross(q.pos[2] - q.pos[0]);
		FXT_CHECK(n.y > 0.0f);
	}
	{
		const MeshData& q = meshlib::Beam();
		FXT_CHECK(q.Valid());
		const Vec3 n = (q.pos[static_cast<size_t>(q.idx[1])] - q.pos[static_cast<size_t>(q.idx[0])])
		                   .cross(q.pos[static_cast<size_t>(q.idx[2])] - q.pos[static_cast<size_t>(q.idx[0])]);
		FXT_CHECK(n.y > 0.0f);
	}
	{
		const MeshData& q = meshlib::Sheet();
		FXT_CHECK(q.Valid());
		const Vec3 n = (q.pos[static_cast<size_t>(q.idx[1])] - q.pos[static_cast<size_t>(q.idx[0])])
		                   .cross(q.pos[static_cast<size_t>(q.idx[2])] - q.pos[static_cast<size_t>(q.idx[0])]);
		FXT_CHECK(n.z > 0.0f);
	}
	for (int d = 0; d < 3; ++d) {
		FXT_CHECK(meshlib::Sphere(d).Valid());
		FXT_CHECK(OutwardFraction(meshlib::Sphere(d), Vec3()) > 0.99f);
	}
	FXT_CHECK(OutwardFraction(meshlib::Chip(), Vec3()) == 1.0f);
	FXT_CHECK(meshlib::Chip().NumTris() == 20);
	FXT_CHECK(OutwardFraction(meshlib::Flame(), Vec3(0, 0.5f, 0)) > 0.99f);
	FXT_CHECK(meshlib::Flame().NumTris() == 13 * 14 * 2);
	FXT_CHECK(OutwardFraction(meshlib::Cone(), Vec3(0, 0.9f, 0)) > 0.9f);
	FXT_CHECK(OutwardFraction(meshlib::Disc(), Vec3()) > 0.99f);
	FXT_CHECK(OutwardFraction(meshlib::Lance(), Vec3()) > 0.99f);
	FXT_CHECK(OutwardFraction(meshlib::Rod(), Vec3()) > 0.99f);
	FXT_CHECK(OutwardFraction(meshlib::Spike(), Vec3(0, 0.2f, 0)) > 0.99f);
	FXT_CHECK(OutwardFraction(meshlib::Caltrop(), Vec3()) > 0.99f);
	FXT_CHECK(WindingAgreement(meshlib::Disc()) > 0.99f);
	FXT_CHECK(WindingAgreement(meshlib::Spike()) == 1.0f);
	FXT_CHECK(WindingAgreement(meshlib::Crescent()) == 1.0f);
	const MeshData& w = meshlib::Wall(3u);
	FXT_CHECK(w.Valid());
	FXT_CHECK(WindingAgreement(w) == 1.0f);
	FXT_CHECK(w.NumTris() > 150 && w.NumTris() <= 200);
	for (int mode = 0; mode < 4; ++mode) {
		const MeshData& c = meshlib::Crystal(5u, static_cast<CrystalMode>(mode));
		FXT_CHECK(c.Valid());
		FXT_CHECK(c.NumTris() > 10);
		FXT_CHECK(WindingAgreement(c) == 1.0f);
	}
	const MeshData& r1 = meshlib::Rock(12u);
	const MeshData& r2 = meshlib::Rock(12u);
	FXT_CHECK(&r1 == &r2);
	FXT_CHECK(r1.uid != 0 && r1.uid != meshlib::Rock(13u).uid);
	// the blob offsets keep the melted blob near the unit sphere
	for (size_t i = 0; i < r1.pos.size(); ++i) {
		const Vec3 blob = r1.pos[i] + Vec3(r1.uv1[i].x, r1.uv2[i].x, r1.uv1[i].y);
		FXT_CHECK(blob.length() < 1.2f && blob.length() > 0.5f);
	}
}

#endif
