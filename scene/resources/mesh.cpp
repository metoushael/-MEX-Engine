#include "mesh.h"

#include "core/io/file_access.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <tuple>

#define RS3D RenderingServer3D::get_singleton()

typedef RenderingServer3D::Vertex Vertex;

// ---------------------------------------------------------------------------
// Mesh
// ---------------------------------------------------------------------------
Mesh::Mesh() {
	if (RS3D) {
		mesh = RS3D->mesh_create();
	}
}

Mesh::~Mesh() {
	if (RS3D && mesh.is_valid()) {
		RS3D->free_rid(mesh);
	}
}

void Mesh::_commit(bool p_fix_winding) {
	if (p_fix_winding) {
		for (size_t i = 0; i + 2 < indices.size(); i += 3) {
			const Vertex &a = vertices[indices[i]], &b = vertices[indices[i + 1]], &c = vertices[indices[i + 2]];
			const Vector3 geometric = (b.position - a.position).cross(c.position - a.position);
			if (geometric.dot(a.normal + b.normal + c.normal) < 0) {
				std::swap(indices[i + 1], indices[i + 2]);
			}
		}
	}
	aabb = AABB();
	if (!vertices.empty()) {
		aabb = AABB(vertices[0].position, Vector3());
		for (const Vertex &v : vertices) {
			aabb.expand_to(v.position);
		}
	}
	if (RS3D && mesh.is_valid()) {
		RS3D->mesh_set_data(mesh, vertices, indices);
	}
	emit_changed();
}

bool Mesh::intersect_ray(const Vector3 &p_from, const Vector3 &p_dir, real_t *r_distance) const {
	if (!aabb.grow(0.001f).intersects_ray(p_from, p_dir)) {
		return false;
	}
	real_t best = 1e30f;
	bool hit = false;
	// Möller-Trumbore, les deux faces comptent.
	for (size_t i = 0; i + 2 < indices.size(); i += 3) {
		const Vector3 &a = vertices[indices[i]].position;
		const Vector3 e1 = vertices[indices[i + 1]].position - a;
		const Vector3 e2 = vertices[indices[i + 2]].position - a;
		const Vector3 p = p_dir.cross(e2);
		const real_t det = e1.dot(p);
		if (std::fabs(det) < 1e-12f) {
			continue;
		}
		const real_t inv = 1.0f / det;
		const Vector3 t = p_from - a;
		const real_t u = t.dot(p) * inv;
		if (u < 0 || u > 1) {
			continue;
		}
		const Vector3 q = t.cross(e1);
		const real_t v = p_dir.dot(q) * inv;
		if (v < 0 || u + v > 1) {
			continue;
		}
		const real_t dist = e2.dot(q) * inv;
		if (dist >= 0 && dist < best) {
			best = dist;
			hit = true;
		}
	}
	if (hit && r_distance) {
		*r_distance = best;
	}
	return hit;
}

void Mesh::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_aabb_size"), &Mesh::get_aabb_size);
	ClassDB::bind_method(D_METHOD("get_vertex_count"), &Mesh::get_vertex_count);
	ClassDB::bind_method(D_METHOD("get_triangle_count"), &Mesh::get_triangle_count);
}

// ---------------------------------------------------------------------------
// PrimitiveMesh
// ---------------------------------------------------------------------------
void PrimitiveMesh::_request_update() {
	dirty = true;
	update_now();
}

void PrimitiveMesh::update_now() {
	if (!dirty) {
		return;
	}
	dirty = false;
	vertices.clear();
	indices.clear();
	_generate();
	_commit(true);
}

void PrimitiveMesh::_add_quad(const Vector3 &a, const Vector3 &b, const Vector3 &c, const Vector3 &d, const Vector3 &p_normal) {
	const uint32_t base = (uint32_t)vertices.size();
	const Vector3 pts[4] = { a, b, c, d };
	const Vector2 uvs[4] = { Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0) };
	for (int i = 0; i < 4; i++) {
		Vertex v;
		v.position = pts[i];
		v.normal = p_normal;
		v.uv = uvs[i];
		vertices.push_back(v);
	}
	const uint32_t quad[6] = { base, base + 1, base + 2, base, base + 2, base + 3 };
	indices.insert(indices.end(), quad, quad + 6);
}

// --- BoxMesh ---
void BoxMesh::set_size(const Vector3 &p_size) {
	size = Vector3(std::max(p_size.x, 0.001f), std::max(p_size.y, 0.001f), std::max(p_size.z, 0.001f));
	_request_update();
}

void BoxMesh::_generate() {
	const Vector3 h = size * 0.5f;
	// Pour chaque face : coins bas-gauche, bas-droit, haut-droit, haut-gauche vus de l'extérieur.
	_add_quad(Vector3(-h.x, -h.y, h.z), Vector3(h.x, -h.y, h.z), Vector3(h.x, h.y, h.z), Vector3(-h.x, h.y, h.z), Vector3(0, 0, 1));
	_add_quad(Vector3(h.x, -h.y, -h.z), Vector3(-h.x, -h.y, -h.z), Vector3(-h.x, h.y, -h.z), Vector3(h.x, h.y, -h.z), Vector3(0, 0, -1));
	_add_quad(Vector3(h.x, -h.y, h.z), Vector3(h.x, -h.y, -h.z), Vector3(h.x, h.y, -h.z), Vector3(h.x, h.y, h.z), Vector3(1, 0, 0));
	_add_quad(Vector3(-h.x, -h.y, -h.z), Vector3(-h.x, -h.y, h.z), Vector3(-h.x, h.y, h.z), Vector3(-h.x, h.y, -h.z), Vector3(-1, 0, 0));
	_add_quad(Vector3(-h.x, h.y, h.z), Vector3(h.x, h.y, h.z), Vector3(h.x, h.y, -h.z), Vector3(-h.x, h.y, -h.z), Vector3(0, 1, 0));
	_add_quad(Vector3(-h.x, -h.y, -h.z), Vector3(h.x, -h.y, -h.z), Vector3(h.x, -h.y, h.z), Vector3(-h.x, -h.y, h.z), Vector3(0, -1, 0));
}

void BoxMesh::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_size", "size"), &BoxMesh::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &BoxMesh::get_size);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "size"), "set_size", "get_size");
}

// Anneaux de sommets autour de l'axe Y : profil (rayon, hauteur, normale) -> surface de révolution.
struct RevolutionRing {
	real_t radius, y;
	Vector2 normal; // (radiale, verticale)
	real_t v;
};

static void _revolve(std::vector<Vertex> &r_vertices, std::vector<uint32_t> &r_indices, const std::vector<RevolutionRing> &p_rings, int p_segments) {
	const uint32_t base = (uint32_t)r_vertices.size();
	for (const RevolutionRing &ring : p_rings) {
		for (int s = 0; s <= p_segments; s++) {
			const real_t t = (real_t)s / (real_t)p_segments;
			const real_t angle = t * (real_t)Math_TAU;
			const real_t cx = std::sin(angle), cz = std::cos(angle);
			Vertex v;
			v.position = Vector3(cx * ring.radius, ring.y, cz * ring.radius);
			v.normal = Vector3(cx * ring.normal.x, ring.normal.y, cz * ring.normal.x).normalized();
			v.uv = Vector2(t, ring.v);
			r_vertices.push_back(v);
		}
	}
	const uint32_t stride = (uint32_t)p_segments + 1;
	for (size_t r = 0; r + 1 < p_rings.size(); r++) {
		for (int s = 0; s < p_segments; s++) {
			const uint32_t a = base + (uint32_t)r * stride + (uint32_t)s;
			const uint32_t b = a + stride;
			// Les triangles dégénérés (pôles) sont ignorés.
			if (p_rings[r].radius > 1e-6f) {
				r_indices.push_back(a);
				r_indices.push_back(b);
				r_indices.push_back(a + 1);
			}
			if (p_rings[r + 1].radius > 1e-6f) {
				r_indices.push_back(a + 1);
				r_indices.push_back(b);
				r_indices.push_back(b + 1);
			}
		}
	}
}

// --- SphereMesh ---
void SphereMesh::set_radius(real_t p_radius) {
	radius = std::max(p_radius, 0.001f);
	_request_update();
}

void SphereMesh::set_height(real_t p_height) {
	height = std::max(p_height, 0.001f);
	_request_update();
}

void SphereMesh::set_radial_segments(int p_segments) {
	radial_segments = Math::clamp(p_segments, 3, 256);
	_request_update();
}

void SphereMesh::set_rings(int p_rings) {
	rings = Math::clamp(p_rings, 2, 128);
	_request_update();
}

void SphereMesh::_generate() {
	std::vector<RevolutionRing> profile;
	const real_t half_height = height * 0.5f;
	for (int i = 0; i <= rings; i++) {
		const real_t t = (real_t)i / (real_t)rings;
		const real_t phi = t * (real_t)Math_PI; // 0 au pôle nord
		const real_t s = std::sin(phi), c = std::cos(phi);
		// Normale d'un ellipsoïde : gradient de l'équation implicite.
		profile.push_back({ s * radius, c * half_height, Vector2(s / radius, c / half_height), t });
	}
	_revolve(vertices, indices, profile, radial_segments);
}

void SphereMesh::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &SphereMesh::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &SphereMesh::get_radius);
	ClassDB::bind_method(D_METHOD("set_height", "height"), &SphereMesh::set_height);
	ClassDB::bind_method(D_METHOD("get_height"), &SphereMesh::get_height);
	ClassDB::bind_method(D_METHOD("set_radial_segments", "segments"), &SphereMesh::set_radial_segments);
	ClassDB::bind_method(D_METHOD("get_radial_segments"), &SphereMesh::get_radial_segments);
	ClassDB::bind_method(D_METHOD("set_rings", "rings"), &SphereMesh::set_rings);
	ClassDB::bind_method(D_METHOD("get_rings"), &SphereMesh::get_rings);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0.01,100,0.01"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height", PROPERTY_HINT_RANGE, "0.01,100,0.01"), "set_height", "get_height");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "radial_segments", PROPERTY_HINT_RANGE, "3,128,1"), "set_radial_segments", "get_radial_segments");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "rings", PROPERTY_HINT_RANGE, "2,64,1"), "set_rings", "get_rings");
}

// --- CylinderMesh ---
void CylinderMesh::set_top_radius(real_t p_radius) {
	top_radius = std::max(p_radius, 0.0f);
	_request_update();
}

void CylinderMesh::set_bottom_radius(real_t p_radius) {
	bottom_radius = std::max(p_radius, 0.0f);
	_request_update();
}

void CylinderMesh::set_height(real_t p_height) {
	height = std::max(p_height, 0.001f);
	_request_update();
}

void CylinderMesh::set_radial_segments(int p_segments) {
	radial_segments = Math::clamp(p_segments, 3, 256);
	_request_update();
}

void CylinderMesh::_generate() {
	const real_t h = height * 0.5f;
	// Pente du flanc : la normale est perpendiculaire à la génératrice.
	const Vector2 side = Vector2(height, bottom_radius - top_radius).normalized();
	std::vector<RevolutionRing> profile;
	if (top_radius > 0) {
		profile.push_back({ 0, h, Vector2(0, 1), 0 });
		profile.push_back({ top_radius, h, Vector2(0, 1), 0.15f });
		_revolve(vertices, indices, profile, radial_segments);
		profile.clear();
	}
	profile.push_back({ top_radius, h, side, 0.15f });
	profile.push_back({ bottom_radius, -h, side, 0.85f });
	_revolve(vertices, indices, profile, radial_segments);
	if (bottom_radius > 0) {
		profile.clear();
		profile.push_back({ bottom_radius, -h, Vector2(0, -1), 0.85f });
		profile.push_back({ 0, -h, Vector2(0, -1), 1 });
		_revolve(vertices, indices, profile, radial_segments);
	}
}

void CylinderMesh::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_top_radius", "radius"), &CylinderMesh::set_top_radius);
	ClassDB::bind_method(D_METHOD("get_top_radius"), &CylinderMesh::get_top_radius);
	ClassDB::bind_method(D_METHOD("set_bottom_radius", "radius"), &CylinderMesh::set_bottom_radius);
	ClassDB::bind_method(D_METHOD("get_bottom_radius"), &CylinderMesh::get_bottom_radius);
	ClassDB::bind_method(D_METHOD("set_height", "height"), &CylinderMesh::set_height);
	ClassDB::bind_method(D_METHOD("get_height"), &CylinderMesh::get_height);
	ClassDB::bind_method(D_METHOD("set_radial_segments", "segments"), &CylinderMesh::set_radial_segments);
	ClassDB::bind_method(D_METHOD("get_radial_segments"), &CylinderMesh::get_radial_segments);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "top_radius", PROPERTY_HINT_RANGE, "0,100,0.01"), "set_top_radius", "get_top_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bottom_radius", PROPERTY_HINT_RANGE, "0,100,0.01"), "set_bottom_radius", "get_bottom_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height", PROPERTY_HINT_RANGE, "0.01,100,0.01"), "set_height", "get_height");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "radial_segments", PROPERTY_HINT_RANGE, "3,128,1"), "set_radial_segments", "get_radial_segments");
}

// --- CapsuleMesh ---
void CapsuleMesh::set_radius(real_t p_radius) {
	radius = std::max(p_radius, 0.001f);
	if (height < radius * 2) {
		height = radius * 2;
	}
	_request_update();
}

void CapsuleMesh::set_height(real_t p_height) {
	height = std::max(p_height, radius * 2);
	_request_update();
}

void CapsuleMesh::set_radial_segments(int p_segments) {
	radial_segments = Math::clamp(p_segments, 3, 256);
	_request_update();
}

void CapsuleMesh::_generate() {
	const real_t half = std::max(height * 0.5f - radius, 0.0f);
	std::vector<RevolutionRing> profile;
	for (int i = 0; i <= rings; i++) {
		const real_t phi = (real_t)i / (real_t)rings * (real_t)Math_PI * 0.5f;
		profile.push_back({ std::sin(phi) * radius, half + std::cos(phi) * radius, Vector2(std::sin(phi), std::cos(phi)), (real_t)i / (real_t)rings * 0.33f });
	}
	for (int i = 0; i <= rings; i++) {
		const real_t phi = (real_t)Math_PI * 0.5f + (real_t)i / (real_t)rings * (real_t)Math_PI * 0.5f;
		profile.push_back({ std::sin(phi) * radius, -half + std::cos(phi) * radius, Vector2(std::sin(phi), std::cos(phi)), 0.67f + (real_t)i / (real_t)rings * 0.33f });
	}
	_revolve(vertices, indices, profile, radial_segments);
}

void CapsuleMesh::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &CapsuleMesh::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &CapsuleMesh::get_radius);
	ClassDB::bind_method(D_METHOD("set_height", "height"), &CapsuleMesh::set_height);
	ClassDB::bind_method(D_METHOD("get_height"), &CapsuleMesh::get_height);
	ClassDB::bind_method(D_METHOD("set_radial_segments", "segments"), &CapsuleMesh::set_radial_segments);
	ClassDB::bind_method(D_METHOD("get_radial_segments"), &CapsuleMesh::get_radial_segments);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0.01,100,0.01"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height", PROPERTY_HINT_RANGE, "0.01,100,0.01"), "set_height", "get_height");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "radial_segments", PROPERTY_HINT_RANGE, "3,128,1"), "set_radial_segments", "get_radial_segments");
}

// --- PlaneMesh ---
void PlaneMesh::set_size(const Vector2 &p_size) {
	size = Vector2(std::max(p_size.x, 0.001f), std::max(p_size.y, 0.001f));
	_request_update();
}

void PlaneMesh::_generate() {
	const Vector2 h = size * 0.5f;
	_add_quad(Vector3(-h.x, 0, h.y), Vector3(h.x, 0, h.y), Vector3(h.x, 0, -h.y), Vector3(-h.x, 0, -h.y), Vector3(0, 1, 0));
}

void PlaneMesh::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_size", "size"), &PlaneMesh::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &PlaneMesh::get_size);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "size"), "set_size", "get_size");
}

// --- TorusMesh ---
void TorusMesh::set_inner_radius(real_t p_radius) {
	inner_radius = std::max(p_radius, 0.0f);
	_request_update();
}

void TorusMesh::set_outer_radius(real_t p_radius) {
	outer_radius = std::max(p_radius, 0.001f);
	_request_update();
}

void TorusMesh::_generate() {
	const real_t lo = std::min(inner_radius, outer_radius), hi = std::max(inner_radius, outer_radius);
	const real_t tube = (hi - lo) * 0.5f;
	const real_t center = lo + tube;
	for (int i = 0; i <= rings; i++) {
		const real_t u = (real_t)i / (real_t)rings;
		const real_t a = u * (real_t)Math_TAU;
		const Vector3 radial(std::sin(a), 0, std::cos(a));
		for (int j = 0; j <= ring_segments; j++) {
			const real_t w = (real_t)j / (real_t)ring_segments;
			const real_t b = w * (real_t)Math_TAU;
			Vertex v;
			v.normal = radial * std::cos(b) + Vector3(0, std::sin(b), 0);
			v.position = radial * center + v.normal * tube;
			v.uv = Vector2(u * 4.0f, w);
			vertices.push_back(v);
		}
	}
	const uint32_t stride = (uint32_t)ring_segments + 1;
	for (int i = 0; i < rings; i++) {
		for (int j = 0; j < ring_segments; j++) {
			const uint32_t a = (uint32_t)i * stride + (uint32_t)j;
			const uint32_t b = a + stride;
			const uint32_t quad[6] = { a, b, a + 1, a + 1, b, b + 1 };
			indices.insert(indices.end(), quad, quad + 6);
		}
	}
}

void TorusMesh::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_inner_radius", "radius"), &TorusMesh::set_inner_radius);
	ClassDB::bind_method(D_METHOD("get_inner_radius"), &TorusMesh::get_inner_radius);
	ClassDB::bind_method(D_METHOD("set_outer_radius", "radius"), &TorusMesh::set_outer_radius);
	ClassDB::bind_method(D_METHOD("get_outer_radius"), &TorusMesh::get_outer_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "inner_radius", PROPERTY_HINT_RANGE, "0,100,0.01"), "set_inner_radius", "get_inner_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "outer_radius", PROPERTY_HINT_RANGE, "0.01,100,0.01"), "set_outer_radius", "get_outer_radius");
}

// ---------------------------------------------------------------------------
// ArrayMesh
// ---------------------------------------------------------------------------
<<<<<<< HEAD
void ArrayMesh::apply_skin(const std::vector<SkinVertex> &p_skin, const std::vector<Transform3D> &p_bone_globals, std::vector<RenderingServer3D::Vertex> &r_vertices) {
	// Les normales suivent l'inverse transposée de chaque os (exacte même avec une échelle).
	std::vector<Basis> normal_bases(p_bone_globals.size());
	for (size_t i = 0; i < p_bone_globals.size(); i++) {
		normal_bases[i] = p_bone_globals[i].basis.inverse().transposed();
	}
	const size_t count = std::min(p_skin.size(), r_vertices.size());
	for (size_t i = 0; i < count; i++) {
		const SkinVertex &sv = p_skin[i];
		Vector3 position, normal;
		float total = 0;
		for (int k = 0; k < 4; k++) {
			const int bone = sv.bones[k];
			if (bone < 0 || bone >= (int)p_bone_globals.size() || sv.weights[k] <= 0) {
				continue;
			}
			position += p_bone_globals[bone].xform(sv.positions[k]) * sv.weights[k];
			normal += normal_bases[bone].xform(sv.normals[k]) * sv.weights[k];
			total += sv.weights[k];
		}
		if (total <= 0) {
			continue; // sommet sans os : laissé en place
		}
		r_vertices[i].position = position / total;
		r_vertices[i].normal = normal.length_squared() > 0 ? normal.normalized() : Vector3(0, 1, 0);
	}
}

=======
>>>>>>> ad542b7994018528bfadabd060baf4823c0564c5
void ArrayMesh::set_data(const std::vector<Vertex> &p_vertices, const std::vector<uint32_t> &p_indices) {
	vertices = p_vertices;
	indices = p_indices;
	_commit(false);
}

void ArrayMesh::set_triangles(const Array &p_positions, const Array &p_normals, const Array &p_uvs) {
	const int count = p_positions.size() - p_positions.size() % 3;
	std::vector<Vertex> verts((size_t)count);
	std::vector<uint32_t> idx((size_t)count);
	const bool has_normals = p_normals.size() >= count;
	const bool has_uvs = p_uvs.size() >= count;
	for (int i = 0; i < count; i++) {
		verts[i].position = p_positions[i].as_vector3();
		if (has_normals) {
			verts[i].normal = p_normals[i].as_vector3();
		}
		if (has_uvs) {
			verts[i].uv = p_uvs[i].as_vector2();
		}
		idx[i] = (uint32_t)i;
	}
	if (!has_normals) {
		for (int i = 0; i + 2 < count; i += 3) {
			const Vector3 n = (verts[i + 1].position - verts[i].position).cross(verts[i + 2].position - verts[i].position).normalized();
			verts[i].normal = verts[i + 1].normal = verts[i + 2].normal = n;
		}
	}
	set_data(verts, idx);
}

// Lecture d'un fichier Wavefront OBJ : positions, normales, coordonnées de texture et faces
// (triangulées en éventail). Les matériaux (.mtl) et les groupes sont ignorés.
Error ArrayMesh::load_obj(const String &p_text) {
	std::vector<Vector3> positions, normals;
	std::vector<Vector2> uvs;
	std::vector<Vertex> verts;
	std::vector<uint32_t> idx;
	std::map<std::tuple<int, int, int>, uint32_t> cache;
	bool missing_normals = false;

	size_t pos = 0;
	const size_t len = p_text.size();
	while (pos < len) {
		size_t end = p_text.find('\n', pos);
		if (end == String::npos) {
			end = len;
		}
		String line = p_text.substr(pos, end - pos);
		pos = end + 1;
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		const char *s = line.c_str();
		while (*s == ' ' || *s == '\t') {
			s++;
		}
		if (s[0] == 'v' && (s[1] == ' ' || s[1] == '\t')) {
			float x = 0, y = 0, z = 0;
			if (sscanf(s + 2, "%f %f %f", &x, &y, &z) == 3) {
				positions.push_back(Vector3(x, y, z));
			}
		} else if (s[0] == 'v' && s[1] == 'n') {
			float x = 0, y = 0, z = 0;
			if (sscanf(s + 3, "%f %f %f", &x, &y, &z) == 3) {
				normals.push_back(Vector3(x, y, z));
			}
		} else if (s[0] == 'v' && s[1] == 't') {
			float u = 0, v = 0;
			if (sscanf(s + 3, "%f %f", &u, &v) >= 1) {
				uvs.push_back(Vector2(u, 1.0f - v)); // origine en bas à gauche dans les fichiers OBJ
			}
		} else if (s[0] == 'f' && (s[1] == ' ' || s[1] == '\t')) {
			std::vector<uint32_t> face;
			const char *p = s + 2;
			while (*p) {
				while (*p == ' ' || *p == '\t') {
					p++;
				}
				if (!*p) {
					break;
				}
				int vi = 0, ti = 0, ni = 0;
				char *next = nullptr;
				vi = (int)strtol(p, &next, 10);
				if (next == p) {
					break;
				}
				p = next;
				if (*p == '/') {
					p++;
					if (*p != '/') {
						ti = (int)strtol(p, &next, 10);
						p = next;
					}
					if (*p == '/') {
						p++;
						ni = (int)strtol(p, &next, 10);
						p = next;
					}
				}
				// Les indices négatifs sont relatifs à la fin des tableaux.
				if (vi < 0) {
					vi = (int)positions.size() + vi + 1;
				}
				if (ti < 0) {
					ti = (int)uvs.size() + ti + 1;
				}
				if (ni < 0) {
					ni = (int)normals.size() + ni + 1;
				}
				if (vi < 1 || vi > (int)positions.size()) {
					return ERR_INVALID_DATA;
				}
				const auto key = std::make_tuple(vi, ti, ni);
				auto it = cache.find(key);
				// Sans normale, les sommets ne sont pas partagés (normales par face).
				if (it != cache.end() && ni > 0) {
					face.push_back(it->second);
				} else {
					Vertex v;
					v.position = positions[vi - 1];
					if (ti >= 1 && ti <= (int)uvs.size()) {
						v.uv = uvs[ti - 1];
					}
					if (ni >= 1 && ni <= (int)normals.size()) {
						v.normal = normals[ni - 1].normalized();
					} else {
						missing_normals = true;
					}
					cache[key] = (uint32_t)verts.size();
					face.push_back((uint32_t)verts.size());
					verts.push_back(v);
				}
			}
			for (size_t i = 1; i + 1 < face.size(); i++) {
				idx.push_back(face[0]);
				idx.push_back(face[i]);
				idx.push_back(face[i + 1]);
			}
		}
	}
	if (verts.empty() || idx.empty()) {
		return ERR_INVALID_DATA;
	}
	if (missing_normals) {
		for (size_t i = 0; i + 2 < idx.size(); i += 3) {
			Vertex &a = verts[idx[i]], &b = verts[idx[i + 1]], &c = verts[idx[i + 2]];
			const Vector3 n = (b.position - a.position).cross(c.position - a.position).normalized();
			if (a.normal.is_zero_approx()) {
				a.normal = n;
			}
			if (b.normal.is_zero_approx()) {
				b.normal = n;
			}
			if (c.normal.is_zero_approx()) {
				c.normal = n;
			}
		}
	}
	set_data(verts, idx);
	return OK;
}

void ArrayMesh::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_triangles", "positions", "normals", "uvs"), &ArrayMesh::set_triangles, DEFVAL(Array()), DEFVAL(Array()));
}

Ref<Resource> ResourceFormatLoaderOBJ::load(const String &p_path, Error *r_error) {
	if (!FileAccess::exists(p_path)) {
		if (r_error) {
			*r_error = ERR_FILE_NOT_FOUND;
		}
		return Ref<Resource>();
	}
	const String text = FileAccess::get_file_as_string(p_path);
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	const Error err = mesh->load_obj(text);
	if (r_error) {
		*r_error = err;
	}
	if (err != OK) {
		print_error("Fichier OBJ illisible : " + p_path);
		return Ref<Resource>();
	}
	return mesh;
}
