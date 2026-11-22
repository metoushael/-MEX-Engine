#include "physics_server_3d.h"

#include "core/string/ustring.h"

#include <algorithm>
#include <cstring>

PhysicsServer3D *PhysicsServer3D::singleton = nullptr;

namespace {

typedef PhysicsServer3D::Manifold Manifold;
typedef PhysicsServer3D::ShapeData ShapeData;

const real_t CONTACT_SLOP = 0.005f;
const real_t SLEEP_LINEAR = 0.12f;
const real_t SLEEP_ANGULAR = 0.25f;
const real_t SLEEP_TIME = 0.6f;

// ---------------------------------------------------------------------------
// Géométrie
// ---------------------------------------------------------------------------
Vector3 _closest_on_segment(const Vector3 &a, const Vector3 &b, const Vector3 &p) {
	const Vector3 ab = b - a;
	const real_t len2 = ab.length_squared();
	if (len2 < 1e-12f) {
		return a;
	}
	return a + ab * Math::clamp((p - a).dot(ab) / len2, (real_t)0, (real_t)1);
}

// Points les plus proches entre deux segments (Ericson, « Real-Time Collision Detection »).
void _closest_segments(const Vector3 &p1, const Vector3 &q1, const Vector3 &p2, const Vector3 &q2, Vector3 &c1, Vector3 &c2) {
	const Vector3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
	const real_t a = d1.dot(d1), e = d2.dot(d2), f = d2.dot(r);
	real_t s = 0, t = 0;
	if (a <= 1e-10f && e <= 1e-10f) {
		c1 = p1;
		c2 = p2;
		return;
	}
	if (a <= 1e-10f) {
		t = Math::clamp(f / e, (real_t)0, (real_t)1);
	} else {
		const real_t c = d1.dot(r);
		if (e <= 1e-10f) {
			s = Math::clamp(-c / a, (real_t)0, (real_t)1);
		} else {
			const real_t b = d1.dot(d2);
			const real_t denom = a * e - b * b;
			s = denom > 1e-10f ? Math::clamp((b * f - c * e) / denom, (real_t)0, (real_t)1) : 0;
			t = (b * s + f) / e;
			if (t < 0) {
				t = 0;
				s = Math::clamp(-c / a, (real_t)0, (real_t)1);
			} else if (t > 1) {
				t = 1;
				s = Math::clamp((b - c) / a, (real_t)0, (real_t)1);
			}
		}
	}
	c1 = p1 + d1 * s;
	c2 = p2 + d2 * t;
}

Vector3 _closest_on_box(const Transform3D &p_box, const Vector3 &p_half, const Vector3 &p_point, bool *r_inside = nullptr) {
	const Vector3 local = p_box.basis.xform_inv(p_point - p_box.origin);
	const Vector3 clamped(Math::clamp(local.x, -p_half.x, p_half.x), Math::clamp(local.y, -p_half.y, p_half.y), Math::clamp(local.z, -p_half.z, p_half.z));
	if (r_inside) {
		*r_inside = clamped == local;
	}
	return p_box.xform(clamped);
}

void _add_point(Manifold &m, const Vector3 &p_point, real_t p_depth) {
	if (m.count < 4) {
		m.points[m.count] = p_point;
		m.depths[m.count] = p_depth;
		m.count++;
		return;
	}
	// Manifold plein : on remplace le point le moins profond si le nouveau l'est davantage.
	int shallow = 0;
	for (int i = 1; i < 4; i++) {
		if (m.depths[i] < m.depths[shallow]) {
			shallow = i;
		}
	}
	if (p_depth > m.depths[shallow]) {
		m.points[shallow] = p_point;
		m.depths[shallow] = p_depth;
	}
}

// Sphère A (centre, rayon) contre sphère B. Normale de A vers B.
bool _sphere_sphere(const Vector3 &ca, real_t ra, const Vector3 &cb, real_t rb, Manifold &m) {
	const Vector3 d = cb - ca;
	const real_t dist2 = d.length_squared();
	const real_t sum = ra + rb;
	if (dist2 >= sum * sum) {
		return false;
	}
	const real_t dist = std::sqrt(dist2);
	const Vector3 n = dist > 1e-6f ? d / dist : Vector3(0, 1, 0);
	if (m.count == 0) {
		m.normal = n;
	}
	_add_point(m, ca + n * (ra - (sum - dist) * 0.5f), sum - dist);
	return true;
}

// Sphère (A) contre boîte (B). Normale de la sphère vers la boîte.
bool _sphere_box(const Vector3 &p_center, real_t p_radius, const Transform3D &p_box, const Vector3 &p_half, Manifold &m) {
	const Vector3 local = p_box.basis.xform_inv(p_center - p_box.origin);
	const Vector3 clamped(Math::clamp(local.x, -p_half.x, p_half.x), Math::clamp(local.y, -p_half.y, p_half.y), Math::clamp(local.z, -p_half.z, p_half.z));
	Vector3 normal_local; // de la boîte vers la sphère
	real_t depth;
	Vector3 point_local = clamped;
	if (clamped == local) {
		// Centre à l'intérieur : sortie par la face la plus proche.
		int axis = 0;
		real_t best = p_half.x - std::fabs(local.x);
		for (int i = 1; i < 3; i++) {
			const real_t dd = p_half[i] - std::fabs(local[i]);
			if (dd < best) {
				best = dd;
				axis = i;
			}
		}
		normal_local[axis] = local[axis] >= 0 ? 1.0f : -1.0f;
		depth = best + p_radius;
		point_local[axis] = normal_local[axis] * p_half[axis];
	} else {
		const Vector3 d = local - clamped;
		const real_t dist = d.length();
		if (dist >= p_radius) {
			return false;
		}
		normal_local = d / dist;
		depth = p_radius - dist;
	}
	const Vector3 n = -p_box.basis.xform(normal_local);
	if (m.count == 0) {
		m.normal = n;
	}
	_add_point(m, p_box.xform(point_local), depth);
	return true;
}

// Capsule (A) contre boîte (B) : la capsule est testée comme trois sphères (extrémités et
// point du segment le plus proche de la boîte). Normale de la capsule vers la boîte.
bool _capsule_box(const Vector3 &p_a, const Vector3 &p_b, real_t p_radius, const Transform3D &p_box, const Vector3 &p_half, Manifold &m) {
	// Projections alternées : converge vers la paire de points la plus proche.
	Vector3 on_segment = (p_a + p_b) * 0.5f;
	for (int i = 0; i < 8; i++) {
		const Vector3 on_box = _closest_on_box(p_box, p_half, on_segment);
		on_segment = _closest_on_segment(p_a, p_b, on_box);
	}
	Manifold best;
	bool hit = _sphere_box(on_segment, p_radius, p_box, p_half, best);
	if (!hit) {
		return false;
	}
	m.normal = best.normal;
	_add_point(m, best.points[0], best.depths[0]);
	const Vector3 ends[2] = { p_a, p_b };
	for (const Vector3 &e : ends) {
		if ((e - on_segment).length_squared() < 1e-6f) {
			continue;
		}
		Manifold extra;
		// Les contacts secondaires ne comptent que s'ils vont dans le même sens.
		if (_sphere_box(e, p_radius, p_box, p_half, extra) && extra.normal.dot(m.normal) > 0.9f) {
			_add_point(m, extra.points[0], extra.depths[0]);
		}
	}
	return true;
}

int _clip_polygon(const Vector3 *p_in, int p_count, const Vector3 &p_normal, real_t p_d, Vector3 *r_out) {
	// Conserve la partie telle que point . normale <= d.
	int out = 0;
	for (int i = 0; i < p_count; i++) {
		const Vector3 &a = p_in[i];
		const Vector3 &b = p_in[(i + 1) % p_count];
		const real_t da = a.dot(p_normal) - p_d;
		const real_t db = b.dot(p_normal) - p_d;
		if (da <= 0) {
			r_out[out++] = a;
		}
		if ((da < 0 && db > 0) || (da > 0 && db < 0)) {
			r_out[out++] = a + (b - a) * (da / (da - db));
		}
	}
	return out;
}

// Boîte A contre boîte B : théorème des axes séparateurs (15 axes), puis points de contact
// par découpage de la face incidente (contact face) ou points les plus proches (arêtes).
bool _box_box(const Transform3D &p_a, const Vector3 &ha, const Transform3D &p_b, const Vector3 &hb, Manifold &m) {
	const Vector3 ca = p_a.origin, cb = p_b.origin;
	Vector3 ax[3], bx[3];
	for (int i = 0; i < 3; i++) {
		ax[i] = p_a.basis.get_column(i);
		bx[i] = p_b.basis.get_column(i);
	}
	const Vector3 d = cb - ca;
	real_t AR[3][3];
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			AR[i][j] = std::fabs(ax[i].dot(bx[j])) + 1e-5f;
		}
	}
	real_t best = 1e30f;
	int best_axis = -1;
	Vector3 best_n;
	for (int i = 0; i < 3; i++) {
		const real_t t = d.dot(ax[i]);
		const real_t pen = ha[i] + hb[0] * AR[i][0] + hb[1] * AR[i][1] + hb[2] * AR[i][2] - std::fabs(t);
		if (pen < 0) {
			return false;
		}
		if (pen < best) {
			best = pen;
			best_axis = i;
			best_n = t < 0 ? -ax[i] : ax[i];
		}
	}
	for (int j = 0; j < 3; j++) {
		const real_t t = d.dot(bx[j]);
		const real_t pen = ha[0] * AR[0][j] + ha[1] * AR[1][j] + ha[2] * AR[2][j] + hb[j] - std::fabs(t);
		if (pen < 0) {
			return false;
		}
		if (pen < best - 1e-4f) {
			best = pen;
			best_axis = 3 + j;
			best_n = t < 0 ? -bx[j] : bx[j];
		}
	}
	real_t best_edge = 1e30f;
	int edge_i = -1, edge_j = -1;
	Vector3 edge_n;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			Vector3 axis = ax[i].cross(bx[j]);
			const real_t len = axis.length();
			if (len < 1e-3f) {
				continue; // arêtes parallèles : déjà couvert par les faces
			}
			axis = axis / len;
			real_t ra = 0, rb = 0;
			for (int k = 0; k < 3; k++) {
				ra += ha[k] * std::fabs(ax[k].dot(axis));
				rb += hb[k] * std::fabs(bx[k].dot(axis));
			}
			const real_t t = d.dot(axis);
			const real_t pen = ra + rb - std::fabs(t);
			if (pen < 0) {
				return false;
			}
			if (pen < best_edge) {
				best_edge = pen;
				edge_i = i;
				edge_j = j;
				edge_n = t < 0 ? -axis : axis;
			}
		}
	}

	if (edge_i >= 0 && best_edge < best * 0.95f - 0.001f) {
		// Contact arête contre arête.
		Vector3 pa = ca, pb = cb;
		for (int k = 0; k < 3; k++) {
			if (k != edge_i) {
				pa += ax[k] * (ha[k] * (edge_n.dot(ax[k]) > 0 ? 1.0f : -1.0f));
			}
			if (k != edge_j) {
				pb -= bx[k] * (hb[k] * (edge_n.dot(bx[k]) > 0 ? 1.0f : -1.0f));
			}
		}
		Vector3 c1, c2;
		_closest_segments(pa - ax[edge_i] * ha[edge_i], pa + ax[edge_i] * ha[edge_i], pb - bx[edge_j] * hb[edge_j], pb + bx[edge_j] * hb[edge_j], c1, c2);
		m.normal = edge_n;
		m.count = 0;
		_add_point(m, (c1 + c2) * 0.5f, best_edge);
		return true;
	}

	// Contact face : boîte de référence (celle qui porte l'axe) et boîte incidente.
	const bool a_is_ref = best_axis < 3;
	const int r = best_axis % 3;
	const Vector3 *rx = a_is_ref ? ax : bx;
	const Vector3 *ix = a_is_ref ? bx : ax;
	const Vector3 cr = a_is_ref ? ca : cb;
	const Vector3 ci = a_is_ref ? cb : ca;
	const Vector3 hr = a_is_ref ? ha : hb;
	const Vector3 hi = a_is_ref ? hb : ha;
	const Vector3 n_ref = a_is_ref ? best_n : -best_n; // de la référence vers l'incidente

	int k = 0;
	real_t most = -1;
	for (int i = 0; i < 3; i++) {
		const real_t v = std::fabs(ix[i].dot(n_ref));
		if (v > most) {
			most = v;
			k = i;
		}
	}
	const real_t sign = ix[k].dot(n_ref) > 0 ? -1.0f : 1.0f;
	const Vector3 face_center = ci + ix[k] * (hi[k] * sign);
	const Vector3 u = ix[(k + 1) % 3] * hi[(k + 1) % 3];
	const Vector3 v = ix[(k + 2) % 3] * hi[(k + 2) % 3];
	Vector3 poly_a[12], poly_b[12];
	poly_a[0] = face_center + u + v;
	poly_a[1] = face_center - u + v;
	poly_a[2] = face_center - u - v;
	poly_a[3] = face_center + u - v;
	int count = 4;
	const int r1 = (r + 1) % 3, r2 = (r + 2) % 3;
	count = _clip_polygon(poly_a, count, rx[r1], cr.dot(rx[r1]) + hr[r1], poly_b);
	count = _clip_polygon(poly_b, count, -rx[r1], -cr.dot(rx[r1]) + hr[r1], poly_a);
	count = _clip_polygon(poly_a, count, rx[r2], cr.dot(rx[r2]) + hr[r2], poly_b);
	count = _clip_polygon(poly_b, count, -rx[r2], -cr.dot(rx[r2]) + hr[r2], poly_a);
	const real_t plane = cr.dot(n_ref) + hr[r];
	m.normal = best_n;
	m.count = 0;

	// Points sous la face de référence. S'il y en a plus de quatre, on garde les plus écartés.
	Vector3 pts[12];
	real_t depths[12];
	int n = 0;
	for (int i = 0; i < count; i++) {
		const real_t depth = plane - poly_a[i].dot(n_ref);
		if (depth >= -0.001f) {
			pts[n] = poly_a[i];
			depths[n] = std::max(depth, (real_t)0);
			n++;
		}
	}
	if (n == 0) {
		// Cas dégénéré (imprécision numérique) : un contact au milieu.
		_add_point(m, (ca + cb) * 0.5f, best);
		return true;
	}
	if (n <= 4) {
		for (int i = 0; i < n; i++) {
			_add_point(m, pts[i], depths[i]);
		}
		return true;
	}
	bool used[12] = {};
	int first = 0;
	for (int i = 1; i < n; i++) {
		if (depths[i] > depths[first]) {
			first = i;
		}
	}
	used[first] = true;
	_add_point(m, pts[first], depths[first]);
	int second = -1;
	real_t far = -1;
	for (int i = 0; i < n; i++) {
		const real_t dd = (pts[i] - pts[first]).length_squared();
		if (!used[i] && dd > far) {
			far = dd;
			second = i;
		}
	}
	used[second] = true;
	_add_point(m, pts[second], depths[second]);
	const Vector3 line = pts[second] - pts[first];
	int third = -1, fourth = -1;
	real_t max_side = 0, min_side = 0;
	for (int i = 0; i < n; i++) {
		if (used[i]) {
			continue;
		}
		const real_t side = line.cross(pts[i] - pts[first]).dot(n_ref);
		if (side > max_side) {
			max_side = side;
			third = i;
		}
		if (side < min_side) {
			min_side = side;
			fourth = i;
		}
	}
	if (third >= 0) {
		_add_point(m, pts[third], depths[third]);
	}
	if (fourth >= 0) {
		_add_point(m, pts[fourth], depths[fourth]);
	}
	return true;
}

void _flip(Manifold &m) {
	m.normal = -m.normal;
}

struct ShapeRef {
	const ShapeData *data;
	const Transform3D *world;
	Vector3 seg_a, seg_b;
};

bool _collide(const ShapeRef &a, const ShapeRef &b, Manifold &m) {
	typedef PhysicsServer3D PS;
	m.count = 0;
	const PS::ShapeType ta = a.data->type, tb = b.data->type;
	if (ta == PS::SHAPE_SPHERE && tb == PS::SHAPE_SPHERE) {
		return _sphere_sphere(a.world->origin, a.data->radius, b.world->origin, b.data->radius, m);
	}
	if (ta == PS::SHAPE_SPHERE && tb == PS::SHAPE_BOX) {
		return _sphere_box(a.world->origin, a.data->radius, *b.world, b.data->half_extents, m);
	}
	if (ta == PS::SHAPE_SPHERE && tb == PS::SHAPE_CAPSULE) {
		return _sphere_sphere(a.world->origin, a.data->radius, _closest_on_segment(b.seg_a, b.seg_b, a.world->origin), b.data->radius, m);
	}
	if (ta == PS::SHAPE_BOX && tb == PS::SHAPE_BOX) {
		return _box_box(*a.world, a.data->half_extents, *b.world, b.data->half_extents, m);
	}
	if (ta == PS::SHAPE_CAPSULE && tb == PS::SHAPE_CAPSULE) {
		Vector3 c1, c2;
		_closest_segments(a.seg_a, a.seg_b, b.seg_a, b.seg_b, c1, c2);
		return _sphere_sphere(c1, a.data->radius, c2, b.data->radius, m);
	}
	if (ta == PS::SHAPE_CAPSULE && tb == PS::SHAPE_BOX) {
		return _capsule_box(a.seg_a, a.seg_b, a.data->radius, *b.world, b.data->half_extents, m);
	}
	// Paires restantes : test dans l'autre sens, normale inversée.
	const bool hit = _collide(b, a, m);
	if (hit) {
		_flip(m);
	}
	return hit;
}

void _capsule_segment(const ShapeData &p_data, const Transform3D &p_world, Vector3 &r_a, Vector3 &r_b) {
	const real_t half = std::max(p_data.height * 0.5f - p_data.radius, (real_t)0);
	const Vector3 axis = p_world.basis.get_column(1) * half;
	r_a = p_world.origin - axis;
	r_b = p_world.origin + axis;
}

AABB _shape_aabb(const ShapeData &p_data, const Transform3D &p_world, const Vector3 &p_seg_a, const Vector3 &p_seg_b) {
	typedef PhysicsServer3D PS;
	switch (p_data.type) {
		case PS::SHAPE_SPHERE: {
			const Vector3 r(p_data.radius, p_data.radius, p_data.radius);
			return AABB(p_world.origin - r, r * 2.0f);
		}
		case PS::SHAPE_CAPSULE: {
			const Vector3 r(p_data.radius, p_data.radius, p_data.radius);
			AABB box(p_seg_a, Vector3());
			box.expand_to(p_seg_b);
			return AABB(box.position - r, box.size + r * 2.0f);
		}
		default: {
			Vector3 e;
			for (int i = 0; i < 3; i++) {
				e[i] = std::fabs(p_world.basis.rows[i].x) * p_data.half_extents.x + std::fabs(p_world.basis.rows[i].y) * p_data.half_extents.y + std::fabs(p_world.basis.rows[i].z) * p_data.half_extents.z;
			}
			return AABB(p_world.origin - e, e * 2.0f);
		}
	}
}

// Rayon contre forme. r_t : fraction dans [0, 1] le long de p_dir (non normalisé).
bool _ray_shape(const Vector3 &p_from, const Vector3 &p_dir, const ShapeData &p_data, const Transform3D &p_world, const Vector3 &p_seg_a, const Vector3 &p_seg_b, real_t &r_t, Vector3 &r_normal) {
	typedef PhysicsServer3D PS;
	auto ray_sphere = [&](const Vector3 &p_center, real_t p_radius, real_t &t, Vector3 &n) {
		const Vector3 mm = p_from - p_center;
		const real_t a = p_dir.dot(p_dir);
		const real_t b = mm.dot(p_dir);
		const real_t c = mm.dot(mm) - p_radius * p_radius;
		if (c <= 0 || a <= 0) {
			return false; // départ à l'intérieur : ignoré
		}
		const real_t disc = b * b - a * c;
		if (disc < 0) {
			return false;
		}
		t = (-b - std::sqrt(disc)) / a;
		if (t < 0 || t > 1) {
			return false;
		}
		n = (p_from + p_dir * t - p_center).normalized();
		return true;
	};
	switch (p_data.type) {
		case PS::SHAPE_SPHERE:
			return ray_sphere(p_world.origin, p_data.radius, r_t, r_normal);
		case PS::SHAPE_BOX: {
			const Vector3 from = p_world.basis.xform_inv(p_from - p_world.origin);
			const Vector3 dir = p_world.basis.xform_inv(p_dir);
			real_t tmin = 0, tmax = 1;
			int axis = -1;
			real_t axis_sign = 1;
			for (int i = 0; i < 3; i++) {
				const real_t h = p_data.half_extents[i];
				if (std::fabs(dir[i]) < 1e-9f) {
					if (from[i] < -h || from[i] > h) {
						return false;
					}
					continue;
				}
				real_t t1 = (-h - from[i]) / dir[i];
				real_t t2 = (h - from[i]) / dir[i];
				real_t s = -1;
				if (t1 > t2) {
					std::swap(t1, t2);
					s = 1;
				}
				if (t1 > tmin) {
					tmin = t1;
					axis = i;
					axis_sign = s;
				}
				tmax = std::min(tmax, t2);
				if (tmin > tmax) {
					return false;
				}
			}
			if (axis < 0) {
				return false; // départ à l'intérieur
			}
			r_t = tmin;
			Vector3 n;
			n[axis] = axis_sign;
			r_normal = p_world.basis.xform(n);
			return true;
		}
		case PS::SHAPE_CAPSULE: {
			bool hit = false;
			real_t best = 2;
			// Cylindre infini autour du segment, limité à sa longueur.
			const Vector3 ab = p_seg_b - p_seg_a;
			const real_t len2 = ab.length_squared();
			if (len2 > 1e-10f) {
				const Vector3 axis = ab / std::sqrt(len2);
				const Vector3 mm = p_from - p_seg_a;
				const Vector3 m_perp = mm - axis * mm.dot(axis);
				const Vector3 d_perp = p_dir - axis * p_dir.dot(axis);
				const real_t a = d_perp.dot(d_perp);
				const real_t b = m_perp.dot(d_perp);
				const real_t c = m_perp.dot(m_perp) - p_data.radius * p_data.radius;
				if (a > 1e-10f && c > 0) {
					const real_t disc = b * b - a * c;
					if (disc >= 0) {
						const real_t t = (-b - std::sqrt(disc)) / a;
						const Vector3 p = p_from + p_dir * t;
						const real_t along = (p - p_seg_a).dot(axis);
						if (t >= 0 && t <= 1 && along >= 0 && along * along <= len2) {
							hit = true;
							best = t;
							r_normal = (p - (p_seg_a + axis * along)).normalized();
						}
					}
				}
			}
			const Vector3 ends[2] = { p_seg_a, p_seg_b };
			for (const Vector3 &e : ends) {
				real_t t;
				Vector3 n;
				if (ray_sphere(e, p_data.radius, t, n) && t < best) {
					hit = true;
					best = t;
					r_normal = n;
				}
			}
			r_t = best;
			return hit;
		}
	}
	return false;
}

Basis _zero_basis() {
	Basis b;
	b.rows[0] = b.rows[1] = b.rows[2] = Vector3();
	return b;
}

void _tangents(const Vector3 &n, Vector3 &t1, Vector3 &t2) {
	t1 = std::fabs(n.x) > 0.7f ? Vector3(0, 1, 0).cross(n) : Vector3(1, 0, 0).cross(n);
	t1 = t1.normalized();
	t2 = n.cross(t1);
}

} // namespace

// ---------------------------------------------------------------------------
// Accès
// ---------------------------------------------------------------------------
PhysicsServer3D::PhysicsServer3D() {
	singleton = this;
}

PhysicsServer3D::~PhysicsServer3D() {
	for (auto &kv : objects) {
		delete kv.second;
	}
	for (auto &kv : spaces) {
		delete kv.second;
	}
	if (singleton == this) {
		singleton = nullptr;
	}
}

PhysicsServer3D::CollisionObject *PhysicsServer3D::_get_object(RID p_rid) const {
	auto it = objects.find(p_rid.id);
	return it == objects.end() ? nullptr : it->second;
}

PhysicsServer3D::Body *PhysicsServer3D::_get_body(RID p_rid) const {
	CollisionObject *obj = _get_object(p_rid);
	return (obj && !obj->is_area) ? static_cast<Body *>(obj) : nullptr;
}

PhysicsServer3D::Area *PhysicsServer3D::_get_area(RID p_rid) const {
	CollisionObject *obj = _get_object(p_rid);
	return (obj && obj->is_area) ? static_cast<Area *>(obj) : nullptr;
}

bool PhysicsServer3D::collide_shapes(const ShapeData &p_a, const Transform3D &p_xform_a, const ShapeData &p_b, const Transform3D &p_xform_b, Manifold &r_manifold) {
	ShapeRef a = { &p_a, &p_xform_a, Vector3(), Vector3() };
	ShapeRef b = { &p_b, &p_xform_b, Vector3(), Vector3() };
	_capsule_segment(p_a, p_xform_a, a.seg_a, a.seg_b);
	_capsule_segment(p_b, p_xform_b, b.seg_a, b.seg_b);
	return _collide(a, b, r_manifold);
}

void PhysicsServer3D::_update_cache(CollisionObject *p_object) {
	bool first = true;
	p_object->aabb = AABB(p_object->xform.origin, Vector3());
	for (WorldShape &shape : p_object->shapes) {
		shape.world = (p_object->xform * shape.local).orthonormalized();
		_capsule_segment(shape.data, shape.world, shape.seg_a, shape.seg_b);
		shape.aabb = _shape_aabb(shape.data, shape.world, shape.seg_a, shape.seg_b);
		if (shape.disabled) {
			continue;
		}
		p_object->aabb = first ? shape.aabb : p_object->aabb.merge(shape.aabb);
		first = false;
	}
	if (!p_object->is_area) {
		Body *body = static_cast<Body *>(p_object);
		const Basis &r = body->xform.basis;
		Basis scaled = r;
		for (int i = 0; i < 3; i++) {
			scaled.set_column(i, r.get_column(i) * body->inv_inertia_local[i]);
		}
		body->inv_inertia_world = scaled * r.transposed();
	}
}

void PhysicsServer3D::_update_mass(Body *p_body) {
	if (p_body->mode != BODY_MODE_RIGID) {
		p_body->inv_mass = 0;
		p_body->inv_inertia_local = Vector3();
		return;
	}
	const real_t m = std::max(p_body->mass, (real_t)0.001);
	p_body->inv_mass = 1.0f / m;
	Vector3 inertia(1, 1, 1);
	const WorldShape *single = nullptr;
	int enabled = 0;
	for (const WorldShape &s : p_body->shapes) {
		if (!s.disabled) {
			single = &s;
			enabled++;
		}
	}
	if (enabled == 1 && single->local.origin.length_squared() < 1e-6f && single->data.type == SHAPE_SPHERE) {
		const real_t i = 0.4f * m * single->data.radius * single->data.radius;
		inertia = Vector3(i, i, i);
	} else if (enabled >= 1) {
		// Boîte englobante de l'ensemble des formes, en coordonnées locales du corps.
		AABB box;
		bool first = true;
		for (const WorldShape &s : p_body->shapes) {
			if (s.disabled) {
				continue;
			}
			Vector3 a, b;
			_capsule_segment(s.data, s.local, a, b);
			const AABB sb = _shape_aabb(s.data, s.local, a, b);
			box = first ? sb : box.merge(sb);
			first = false;
		}
		Vector3 e = box.size;
		const Vector3 c = box.get_center();
		// Théorème des axes parallèles pour un centre décalé.
		inertia.x = m / 12.0f * (e.y * e.y + e.z * e.z) + m * (c.y * c.y + c.z * c.z);
		inertia.y = m / 12.0f * (e.x * e.x + e.z * e.z) + m * (c.x * c.x + c.z * c.z);
		inertia.z = m / 12.0f * (e.x * e.x + e.y * e.y) + m * (c.x * c.x + c.y * c.y);
	}
	for (int i = 0; i < 3; i++) {
		p_body->inv_inertia_local[i] = (p_body->lock_rotation || inertia[i] <= 1e-9f) ? 0.0f : 1.0f / inertia[i];
	}
}

void PhysicsServer3D::_wake_space(Space *p_space) {
	if (!p_space) {
		return;
	}
	for (Body *b : p_space->bodies) {
		b->sleeping = false;
		b->sleep_timer = 0;
	}
}

void PhysicsServer3D::_remove_from_space(CollisionObject *p_object) {
	Space *space = p_object->space;
	if (!space) {
		return;
	}
	if (p_object->is_area) {
		Area *area = static_cast<Area *>(p_object);
		space->areas.erase(std::remove(space->areas.begin(), space->areas.end(), area), space->areas.end());
		area->overlaps.clear();
	} else {
		Body *body = static_cast<Body *>(p_object);
		space->bodies.erase(std::remove(space->bodies.begin(), space->bodies.end(), body), space->bodies.end());
		body->contacts_prev.clear();
		_wake_space(space);
	}
	p_object->space = nullptr;
}

bool PhysicsServer3D::_layers_collide(const CollisionObject *p_a, const CollisionObject *p_b) {
	return (p_a->mask & p_b->layer) || (p_b->mask & p_a->layer);
}

bool PhysicsServer3D::_collide_objects(const CollisionObject *p_a, const CollisionObject *p_b, Manifold &r_deepest) {
	if (!p_a->aabb.intersects(p_b->aabb)) {
		return false;
	}
	bool hit = false;
	real_t deepest = -1;
	Manifold m;
	for (const WorldShape &sa : p_a->shapes) {
		if (sa.disabled) {
			continue;
		}
		for (const WorldShape &sb : p_b->shapes) {
			if (sb.disabled || !sa.aabb.intersects(sb.aabb)) {
				continue;
			}
			const ShapeRef ra = { &sa.data, &sa.world, sa.seg_a, sa.seg_b };
			const ShapeRef rb = { &sb.data, &sb.world, sb.seg_a, sb.seg_b };
			if (_collide(ra, rb, m) && m.count > 0) {
				const real_t depth = m.max_depth();
				if (depth > deepest) {
					deepest = depth;
					r_deepest = m;
				}
				hit = true;
			}
		}
	}
	return hit;
}

// ---------------------------------------------------------------------------
// Espaces
// ---------------------------------------------------------------------------
RID PhysicsServer3D::space_create() {
	Space *space = new Space;
	space->self.id = next_id++;
	spaces[space->self.id] = space;
	return space->self;
}

void PhysicsServer3D::space_set_gravity(RID p_space, const Vector3 &p_gravity) {
	auto it = spaces.find(p_space.id);
	if (it != spaces.end()) {
		it->second->gravity = p_gravity;
		_wake_space(it->second);
	}
}

Vector3 PhysicsServer3D::space_get_gravity(RID p_space) const {
	auto it = spaces.find(p_space.id);
	return it == spaces.end() ? Vector3() : it->second->gravity;
}

void PhysicsServer3D::space_step(RID p_space, real_t p_delta) {
	auto sit = spaces.find(p_space.id);
	if (sit == spaces.end() || p_delta <= 0) {
		return;
	}
	Space *space = sit->second;

	// 1. Forces -> vitesses.
	for (Body *body : space->bodies) {
		_update_cache(body);
		if (body->mode != BODY_MODE_RIGID) {
			body->solver_inv_mass = 0;
			body->solver_inv_inertia = _zero_basis();
			continue;
		}
		if (body->sleeping) {
			body->linear_velocity = Vector3();
			body->angular_velocity = Vector3();
			body->force = Vector3();
			continue;
		}
		body->linear_velocity += (space->gravity * body->gravity_scale + body->force * body->inv_mass) * p_delta;
		body->linear_velocity *= 1.0f / (1.0f + p_delta * body->linear_damp);
		body->angular_velocity *= 1.0f / (1.0f + p_delta * (body->angular_damp + 0.05f));
		body->force = Vector3();
	}

	// 2. Détection des contacts (au moins un corps rigide par paire).
	std::vector<Contact> contacts;
	std::map<uint64_t, std::set<uint64_t>> touching;
	const size_t body_count = space->bodies.size();
	for (size_t i = 0; i < body_count; i++) {
		Body *a = space->bodies[i];
		for (size_t j = i + 1; j < body_count; j++) {
			Body *b = space->bodies[j];
			if (a->mode != BODY_MODE_RIGID && b->mode != BODY_MODE_RIGID) {
				continue;
			}
			const bool a_active = a->mode == BODY_MODE_RIGID && !a->sleeping;
			const bool b_active = b->mode == BODY_MODE_RIGID && !b->sleeping;
			if (!a_active && !b_active) {
				const bool kinematic_moving = (a->mode == BODY_MODE_KINEMATIC && a->linear_velocity.length_squared() > 1e-4f) || (b->mode == BODY_MODE_KINEMATIC && b->linear_velocity.length_squared() > 1e-4f);
				if (!kinematic_moving) {
					continue;
				}
			}
			if (!_layers_collide(a, b) || a->exceptions.count(b->self.id) || b->exceptions.count(a->self.id)) {
				continue;
			}
			if (!a->aabb.intersects(b->aabb)) {
				continue;
			}
			for (const WorldShape &sa : a->shapes) {
				if (sa.disabled) {
					continue;
				}
				for (const WorldShape &sb : b->shapes) {
					if (sb.disabled || !sa.aabb.intersects(sb.aabb)) {
						continue;
					}
					Manifold m;
					const ShapeRef ra = { &sa.data, &sa.world, sa.seg_a, sa.seg_b };
					const ShapeRef rb = { &sb.data, &sb.world, sb.seg_a, sb.seg_b };
					if (!_collide(ra, rb, m) || m.count == 0) {
						continue;
					}
					// Un corps actif et rapide réveille le corps endormi qu'il touche.
					if (a->sleeping && (b->linear_velocity.length() > SLEEP_LINEAR * 2.5f || b->mode == BODY_MODE_KINEMATIC)) {
						a->sleeping = false;
						a->sleep_timer = 0;
					}
					if (b->sleeping && (a->linear_velocity.length() > SLEEP_LINEAR * 2.5f || a->mode == BODY_MODE_KINEMATIC)) {
						b->sleeping = false;
						b->sleep_timer = 0;
					}
					Contact c;
					c.a = a;
					c.b = b;
					c.normal = m.normal;
					c.count = m.count;
					c.friction = std::sqrt(std::max(a->friction, (real_t)0) * std::max(b->friction, (real_t)0));
					for (int k = 0; k < m.count; k++) {
						c.points[k].ra = m.points[k] - a->xform.origin;
						c.points[k].rb = m.points[k] - b->xform.origin;
						c.points[k].depth = m.depths[k];
					}
					contacts.push_back(c);
					touching[a->self.id].insert(b->self.id);
					touching[b->self.id].insert(a->self.id);
				}
			}
		}
	}

	// 3. Préparation des contraintes.
	for (Body *body : space->bodies) {
		const bool active = body->mode == BODY_MODE_RIGID && !body->sleeping;
		body->solver_inv_mass = active ? body->inv_mass : 0;
		body->solver_inv_inertia = active ? body->inv_inertia_world : _zero_basis();
	}
	for (Contact &c : contacts) {
		Body *a = c.a, *b = c.b;
		_tangents(c.normal, c.t1, c.t2);
		const real_t restitution = std::max(a->bounce, b->bounce);
		for (int k = 0; k < c.count; k++) {
			ContactPoint &p = c.points[k];
			auto effective_mass = [&](const Vector3 &dir) {
				const Vector3 ia = a->solver_inv_inertia.xform(p.ra.cross(dir)).cross(p.ra);
				const Vector3 ib = b->solver_inv_inertia.xform(p.rb.cross(dir)).cross(p.rb);
				const real_t k_value = a->solver_inv_mass + b->solver_inv_mass + ia.dot(dir) + ib.dot(dir);
				return k_value > 1e-9f ? 1.0f / k_value : 0.0f;
			};
			p.mass_n = effective_mass(c.normal);
			p.mass_t1 = effective_mass(c.t1);
			p.mass_t2 = effective_mass(c.t2);
			const Vector3 dv = b->linear_velocity + b->angular_velocity.cross(p.rb) - a->linear_velocity - a->angular_velocity.cross(p.ra);
			const real_t vn = dv.dot(c.normal);
			p.bias = std::min(0.2f / p_delta * std::max(p.depth - CONTACT_SLOP, (real_t)0), (real_t)4.0);
			if (vn < -1.0f) {
				p.bias += -restitution * vn;
			}
		}
	}

	// 4. Résolution itérative des vitesses.
	for (int iteration = 0; iteration < 10; iteration++) {
		for (Contact &c : contacts) {
			Body *a = c.a, *b = c.b;
			for (int k = 0; k < c.count; k++) {
				ContactPoint &p = c.points[k];
				auto apply = [&](const Vector3 &impulse) {
					a->linear_velocity -= impulse * a->solver_inv_mass;
					a->angular_velocity -= a->solver_inv_inertia.xform(p.ra.cross(impulse));
					b->linear_velocity += impulse * b->solver_inv_mass;
					b->angular_velocity += b->solver_inv_inertia.xform(p.rb.cross(impulse));
				};
				// Frottement (deux directions tangentes).
				const real_t max_friction = c.friction * p.pn;
				for (int t = 0; t < 2; t++) {
					const Vector3 &dir = t == 0 ? c.t1 : c.t2;
					real_t &accumulated = t == 0 ? p.pt1 : p.pt2;
					const Vector3 dv = b->linear_velocity + b->angular_velocity.cross(p.rb) - a->linear_velocity - a->angular_velocity.cross(p.ra);
					real_t dp = -dv.dot(dir) * (t == 0 ? p.mass_t1 : p.mass_t2);
					const real_t old = accumulated;
					accumulated = Math::clamp(old + dp, -max_friction, max_friction);
					dp = accumulated - old;
					apply(dir * dp);
				}
				// Non-pénétration.
				const Vector3 dv = b->linear_velocity + b->angular_velocity.cross(p.rb) - a->linear_velocity - a->angular_velocity.cross(p.ra);
				real_t dp = (-dv.dot(c.normal) + p.bias) * p.mass_n;
				const real_t old = p.pn;
				p.pn = std::max(old + dp, (real_t)0);
				dp = p.pn - old;
				apply(c.normal * dp);
			}
		}
	}

	// 5. Vitesses -> positions, mise en sommeil.
	for (Body *body : space->bodies) {
		if (body->mode != BODY_MODE_RIGID || body->sleeping) {
			continue;
		}
		if (body->lock_rotation) {
			body->angular_velocity = Vector3();
		}
		body->xform.origin += body->linear_velocity * p_delta;
		const real_t angle = body->angular_velocity.length() * p_delta;
		if (angle > 1e-7f) {
			body->xform.basis = (Basis(body->angular_velocity.normalized(), angle) * body->xform.basis).orthonormalized();
		}
		_update_cache(body);

		const bool slow = body->linear_velocity.length() < SLEEP_LINEAR && body->angular_velocity.length() < SLEEP_ANGULAR;
		const bool supported = touching.count(body->self.id) > 0;
		if (body->can_sleep && slow && supported) {
			body->sleep_timer += p_delta;
			if (body->sleep_timer >= SLEEP_TIME) {
				body->sleeping = true;
				body->linear_velocity = Vector3();
				body->angular_velocity = Vector3();
			}
		} else {
			body->sleep_timer = 0;
		}
	}

	// 6. Signalement des nouveaux contacts.
	struct Event {
		RID self, other;
		ObjectID other_id;
	};
	std::vector<Event> events;
	for (Body *body : space->bodies) {
		if (!body->contact_callback) {
			continue;
		}
		if (body->sleeping) {
			continue; // les contacts d'un corps endormi restent ceux d'avant
		}
		const std::set<uint64_t> &now = touching[body->self.id];
		for (uint64_t other_id : now) {
			if (!body->contacts_prev.count(other_id)) {
				RID other_rid;
				other_rid.id = other_id;
				CollisionObject *other = _get_object(other_rid);
				events.push_back({ body->self, other_rid, other ? other->instance_id : 0 });
			}
		}
		body->contacts_prev = now;
	}
	for (const Event &e : events) {
		Body *self = _get_body(e.self);
		if (self && self->contact_callback) {
			BodyContactCallback cb = self->contact_callback;
			cb(e.other, e.other_id);
		}
	}

	if (spaces.count(p_space.id)) {
		_step_areas(space);
	}
}

void PhysicsServer3D::_step_areas(Space *p_space) {
	struct Event {
		RID area;
		AreaBodyStatus status;
		RID other;
		ObjectID other_id;
		bool is_area;
	};
	std::vector<Event> events;

	for (Area *area : p_space->areas) {
		_update_cache(area);
	}
	for (Area *area : p_space->areas) {
		std::map<uint64_t, std::pair<ObjectID, bool>> current;
		if (area->monitoring) {
			Manifold m;
			for (Body *body : p_space->bodies) {
				if ((area->mask & body->layer) && _collide_objects(area, body, m)) {
					current[body->self.id] = { body->instance_id, false };
				}
			}
			for (Area *other : p_space->areas) {
				if (other != area && other->monitorable && (area->mask & other->layer) && _collide_objects(area, other, m)) {
					current[other->self.id] = { other->instance_id, true };
				}
			}
		}
		for (auto &kv : current) {
			if (!area->overlaps.count(kv.first)) {
				RID rid;
				rid.id = kv.first;
				events.push_back({ area->self, AREA_BODY_ADDED, rid, kv.second.first, kv.second.second });
			}
		}
		for (auto &kv : area->overlaps) {
			if (!current.count(kv.first)) {
				RID rid;
				rid.id = kv.first;
				events.push_back({ area->self, AREA_BODY_REMOVED, rid, kv.second.first, kv.second.second });
			}
		}
		area->overlaps = current;
	}

	// Les callbacks sont appelés après le parcours : ils peuvent modifier le monde.
	for (const Event &e : events) {
		Area *area = _get_area(e.area);
		if (area && area->callback) {
			AreaMonitorCallback cb = area->callback;
			cb(e.status, e.other, e.other_id, e.is_area);
		}
	}
}

bool PhysicsServer3D::space_intersect_ray(RID p_space, const Vector3 &p_from, const Vector3 &p_to, RayResult &r_result, uint32_t p_mask, RID p_exclude, bool p_collide_bodies, bool p_collide_areas) const {
	auto sit = spaces.find(p_space.id);
	if (sit == spaces.end()) {
		return false;
	}
	const Vector3 dir = p_to - p_from;
	if (dir.length_squared() < 1e-12f) {
		return false;
	}
	AABB ray_aabb(p_from, Vector3());
	ray_aabb.expand_to(p_to);
	real_t best = 2;
	bool hit = false;
	auto test = [&](CollisionObject *obj) {
		if (obj->self == p_exclude || !(p_mask & obj->layer) || !ray_aabb.intersects(obj->aabb)) {
			return;
		}
		for (const WorldShape &shape : obj->shapes) {
			if (shape.disabled) {
				continue;
			}
			real_t t;
			Vector3 n;
			if (_ray_shape(p_from, dir, shape.data, shape.world, shape.seg_a, shape.seg_b, t, n) && t < best) {
				best = t;
				hit = true;
				r_result.position = p_from + dir * t;
				r_result.normal = n;
				r_result.rid = obj->self;
				r_result.collider_id = obj->instance_id;
			}
		}
	};
	if (p_collide_bodies) {
		for (Body *b : sit->second->bodies) {
			test(b);
		}
	}
	if (p_collide_areas) {
		for (Area *a : sit->second->areas) {
			test(a);
		}
	}
	return hit;
}

// ---------------------------------------------------------------------------
// Objets
// ---------------------------------------------------------------------------
void PhysicsServer3D::collision_object_set_space(RID p_object, RID p_space) {
	CollisionObject *obj = _get_object(p_object);
	if (!obj) {
		return;
	}
	auto it = spaces.find(p_space.id);
	Space *space = it == spaces.end() ? nullptr : it->second;
	if (obj->space == space) {
		return;
	}
	_remove_from_space(obj);
	obj->space = space;
	if (space) {
		if (obj->is_area) {
			space->areas.push_back(static_cast<Area *>(obj));
		} else {
			space->bodies.push_back(static_cast<Body *>(obj));
		}
		_update_cache(obj);
	}
}

void PhysicsServer3D::collision_object_set_transform(RID p_object, const Transform3D &p_xform) {
	CollisionObject *obj = _get_object(p_object);
	if (!obj) {
		return;
	}
	obj->xform = p_xform.orthonormalized();
	_update_cache(obj);
	if (!obj->is_area) {
		Body *body = static_cast<Body *>(obj);
		if (body->mode == BODY_MODE_RIGID) {
			body->sleeping = false;
			body->sleep_timer = 0;
		} else if (body->mode == BODY_MODE_STATIC) {
			_wake_space(obj->space); // un support a bougé
		}
	}
}

Transform3D PhysicsServer3D::collision_object_get_transform(RID p_object) const {
	CollisionObject *obj = _get_object(p_object);
	return obj ? obj->xform : Transform3D();
}

void PhysicsServer3D::collision_object_clear_shapes(RID p_object) {
	CollisionObject *obj = _get_object(p_object);
	if (obj) {
		obj->shapes.clear();
		_update_cache(obj);
		if (!obj->is_area) {
			Body *body = static_cast<Body *>(obj);
			if (body->mode == BODY_MODE_RIGID) {
				body->sleeping = false;
				body->sleep_timer = 0;
			} else {
				_wake_space(obj->space); // un support a changé de forme
			}
		}
	}
}

void PhysicsServer3D::collision_object_add_shape(RID p_object, const ShapeData &p_shape, const Transform3D &p_shape_xform, bool p_disabled) {
	CollisionObject *obj = _get_object(p_object);
	if (!obj) {
		return;
	}
	WorldShape shape;
	shape.data = p_shape;
	shape.local = p_shape_xform;
	shape.disabled = p_disabled;
	obj->shapes.push_back(shape);
	if (!obj->is_area) {
		_update_mass(static_cast<Body *>(obj));
	}
	_update_cache(obj);
}

void PhysicsServer3D::collision_object_set_collision_layer(RID p_object, uint32_t p_layer) {
	if (CollisionObject *obj = _get_object(p_object)) {
		obj->layer = p_layer;
	}
}

void PhysicsServer3D::collision_object_set_collision_mask(RID p_object, uint32_t p_mask) {
	if (CollisionObject *obj = _get_object(p_object)) {
		obj->mask = p_mask;
	}
}

void PhysicsServer3D::collision_object_attach_instance_id(RID p_object, ObjectID p_id) {
	if (CollisionObject *obj = _get_object(p_object)) {
		obj->instance_id = p_id;
	}
}

// ---------------------------------------------------------------------------
// Corps
// ---------------------------------------------------------------------------
RID PhysicsServer3D::body_create() {
	Body *body = new Body;
	body->self.id = next_id++;
	body->inv_inertia_world = _zero_basis();
	body->solver_inv_inertia = _zero_basis();
	objects[body->self.id] = body;
	return body->self;
}

void PhysicsServer3D::body_set_mode(RID p_body, BodyMode p_mode) {
	if (Body *body = _get_body(p_body)) {
		body->mode = p_mode;
		body->sleeping = false;
		body->sleep_timer = 0;
		if (p_mode != BODY_MODE_RIGID) {
			body->angular_velocity = Vector3();
			if (p_mode == BODY_MODE_STATIC) {
				body->linear_velocity = Vector3();
			}
		}
		_update_mass(body);
		_update_cache(body);
	}
}

PhysicsServer3D::BodyMode PhysicsServer3D::body_get_mode(RID p_body) const {
	Body *body = _get_body(p_body);
	return body ? body->mode : BODY_MODE_STATIC;
}

void PhysicsServer3D::body_set_linear_velocity(RID p_body, const Vector3 &p_velocity) {
	if (Body *body = _get_body(p_body)) {
		body->linear_velocity = p_velocity;
		if (p_velocity.length_squared() > 1e-8f) {
			body->sleeping = false;
			body->sleep_timer = 0;
		}
	}
}

Vector3 PhysicsServer3D::body_get_linear_velocity(RID p_body) const {
	Body *body = _get_body(p_body);
	return body ? body->linear_velocity : Vector3();
}

void PhysicsServer3D::body_set_angular_velocity(RID p_body, const Vector3 &p_velocity) {
	if (Body *body = _get_body(p_body)) {
		body->angular_velocity = p_velocity;
		if (p_velocity.length_squared() > 1e-8f) {
			body->sleeping = false;
			body->sleep_timer = 0;
		}
	}
}

Vector3 PhysicsServer3D::body_get_angular_velocity(RID p_body) const {
	Body *body = _get_body(p_body);
	return body ? body->angular_velocity : Vector3();
}

void PhysicsServer3D::body_set_mass(RID p_body, real_t p_mass) {
	if (Body *body = _get_body(p_body)) {
		body->mass = p_mass;
		_update_mass(body);
		_update_cache(body);
	}
}

real_t PhysicsServer3D::body_get_mass(RID p_body) const {
	Body *body = _get_body(p_body);
	return body ? body->mass : 0;
}

void PhysicsServer3D::body_set_gravity_scale(RID p_body, real_t p_scale) {
	if (Body *body = _get_body(p_body)) {
		body->gravity_scale = p_scale;
		body->sleeping = false;
	}
}

void PhysicsServer3D::body_set_bounce(RID p_body, real_t p_bounce) {
	if (Body *body = _get_body(p_body)) {
		body->bounce = p_bounce;
	}
}

void PhysicsServer3D::body_set_friction(RID p_body, real_t p_friction) {
	if (Body *body = _get_body(p_body)) {
		body->friction = p_friction;
	}
}

void PhysicsServer3D::body_set_linear_damp(RID p_body, real_t p_damp) {
	if (Body *body = _get_body(p_body)) {
		body->linear_damp = p_damp;
	}
}

void PhysicsServer3D::body_set_angular_damp(RID p_body, real_t p_damp) {
	if (Body *body = _get_body(p_body)) {
		body->angular_damp = p_damp;
	}
}

void PhysicsServer3D::body_set_lock_rotation(RID p_body, bool p_lock) {
	if (Body *body = _get_body(p_body)) {
		body->lock_rotation = p_lock;
		if (p_lock) {
			body->angular_velocity = Vector3();
		}
		_update_mass(body);
		_update_cache(body);
	}
}

void PhysicsServer3D::body_set_can_sleep(RID p_body, bool p_can_sleep) {
	if (Body *body = _get_body(p_body)) {
		body->can_sleep = p_can_sleep;
		if (!p_can_sleep) {
			body->sleeping = false;
		}
	}
}

bool PhysicsServer3D::body_is_sleeping(RID p_body) const {
	Body *body = _get_body(p_body);
	return body && body->sleeping;
}

void PhysicsServer3D::body_wake_up(RID p_body) {
	if (Body *body = _get_body(p_body)) {
		body->sleeping = false;
		body->sleep_timer = 0;
	}
}

void PhysicsServer3D::body_apply_central_impulse(RID p_body, const Vector3 &p_impulse) {
	Body *body = _get_body(p_body);
	if (body && body->mode == BODY_MODE_RIGID) {
		body->linear_velocity += p_impulse * body->inv_mass;
		body->sleeping = false;
		body->sleep_timer = 0;
	}
}

void PhysicsServer3D::body_apply_impulse(RID p_body, const Vector3 &p_impulse, const Vector3 &p_offset) {
	Body *body = _get_body(p_body);
	if (body && body->mode == BODY_MODE_RIGID) {
		body->linear_velocity += p_impulse * body->inv_mass;
		body->angular_velocity += body->inv_inertia_world.xform(p_offset.cross(p_impulse));
		body->sleeping = false;
		body->sleep_timer = 0;
	}
}

void PhysicsServer3D::body_apply_torque_impulse(RID p_body, const Vector3 &p_impulse) {
	Body *body = _get_body(p_body);
	if (body && body->mode == BODY_MODE_RIGID) {
		body->angular_velocity += body->inv_inertia_world.xform(p_impulse);
		body->sleeping = false;
		body->sleep_timer = 0;
	}
}

void PhysicsServer3D::body_apply_central_force(RID p_body, const Vector3 &p_force) {
	Body *body = _get_body(p_body);
	if (body && body->mode == BODY_MODE_RIGID) {
		body->force += p_force;
		body->sleeping = false;
		body->sleep_timer = 0;
	}
}

void PhysicsServer3D::body_add_collision_exception(RID p_body, RID p_other) {
	if (Body *body = _get_body(p_body)) {
		body->exceptions.insert(p_other.id);
	}
}

void PhysicsServer3D::body_remove_collision_exception(RID p_body, RID p_other) {
	if (Body *body = _get_body(p_body)) {
		body->exceptions.erase(p_other.id);
	}
}

void PhysicsServer3D::body_set_contact_callback(RID p_body, BodyContactCallback p_callback) {
	if (Body *body = _get_body(p_body)) {
		body->contact_callback = p_callback;
		body->contacts_prev.clear();
	}
}

// Contact le plus profond entre p_body et les autres corps de son espace.
// La normale retournée va de l'obstacle vers p_body.
bool PhysicsServer3D::_body_find_deepest(Body *p_body, Manifold &r_manifold, Body *&r_other) const {
	bool found = false;
	real_t deepest = 0;
	for (Body *other : p_body->space->bodies) {
		if (other == p_body || !(p_body->mask & other->layer)) {
			continue;
		}
		if (p_body->exceptions.count(other->self.id) || other->exceptions.count(p_body->self.id)) {
			continue;
		}
		Manifold m;
		if (!_collide_objects(other, p_body, m)) {
			continue;
		}
		const real_t depth = m.max_depth();
		if (!found || depth > deepest) {
			found = true;
			deepest = depth;
			r_manifold = m;
			r_other = other;
		}
	}
	return found;
}

bool PhysicsServer3D::body_test_motion(RID p_body, const Vector3 &p_motion, MotionResult &r_result, bool p_test_only) {
	Body *body = _get_body(p_body);
	r_result = MotionResult();
	if (!body || !body->space) {
		return false;
	}
	const Transform3D start = body->xform;
	auto place = [&](const Vector3 &p_origin) {
		body->xform.origin = p_origin;
		_update_cache(body);
	};

	// 1. Dépénétration : le corps peut déjà chevaucher quelque chose (corps rigide venu
	// se poser contre lui, apparition dans un mur...).
	Vector3 origin = start.origin;
	for (int i = 0; i < 4; i++) {
		Manifold m;
		Body *other = nullptr;
		if (!_body_find_deepest(body, m, other) || m.max_depth() <= 0.001f) {
			break;
		}
		origin += m.normal * (m.max_depth() - 0.0005f);
		place(origin);
	}
	const Vector3 recovered = origin;

	// 2. Avance par petits pas (pas de traversée des objets fins), puis recherche
	// dichotomique de la position de contact.
	const real_t length = p_motion.length();
	real_t smallest = 1e30f;
	for (const WorldShape &s : body->shapes) {
		if (s.disabled) {
			continue;
		}
		real_t extent = s.data.radius;
		if (s.data.type == SHAPE_BOX) {
			extent = std::min(s.data.half_extents.x, std::min(s.data.half_extents.y, s.data.half_extents.z));
		}
		smallest = std::min(smallest, extent);
	}
	const real_t step_length = Math::clamp(smallest * 0.5f, (real_t)0.02, (real_t)1.0);
	const int steps = length > 1e-7f ? std::max(1, (int)std::ceil(length / step_length)) : 0;
	real_t safe = 0;
	bool collided = false;
	Manifold hit;
	Body *hit_body = nullptr;
	for (int i = 1; i <= steps; i++) {
		const real_t t = (real_t)i / (real_t)steps;
		place(recovered + p_motion * t);
		Manifold m;
		Body *other = nullptr;
		if (_body_find_deepest(body, m, other) && m.max_depth() > 0.001f) {
			real_t lo = safe, hi = t;
			hit = m;
			hit_body = other;
			for (int k = 0; k < 8; k++) {
				const real_t mid = (lo + hi) * 0.5f;
				place(recovered + p_motion * mid);
				Manifold mm;
				Body *oo = nullptr;
				if (_body_find_deepest(body, mm, oo) && mm.max_depth() > 0.001f) {
					hi = mid;
					hit = mm;
					hit_body = oo;
				} else {
					lo = mid;
				}
			}
			safe = lo;
			collided = true;
			break;
		}
		safe = t;
	}

	const Vector3 end = recovered + p_motion * safe;
	r_result.travel = end - start.origin;
	r_result.remainder = p_motion * (1.0f - safe);
	if (collided) {
		r_result.collided = true;
		r_result.normal = hit.normal;
		r_result.depth = hit.max_depth();
		r_result.position = hit.count > 0 ? hit.points[0] : end;
		if (hit_body) {
			r_result.collider = hit_body->self;
			r_result.collider_id = hit_body->instance_id;
			r_result.collider_velocity = hit_body->linear_velocity;
		}
	}
	if (p_test_only) {
		body->xform = start;
	} else {
		body->xform.origin = end;
	}
	_update_cache(body);
	return collided;
}

// ---------------------------------------------------------------------------
// Zones
// ---------------------------------------------------------------------------
RID PhysicsServer3D::area_create() {
	Area *area = new Area;
	area->self.id = next_id++;
	area->is_area = true;
	objects[area->self.id] = area;
	return area->self;
}

void PhysicsServer3D::area_set_monitor_callback(RID p_area, AreaMonitorCallback p_callback) {
	if (Area *area = _get_area(p_area)) {
		area->callback = p_callback;
	}
}

void PhysicsServer3D::area_set_monitoring(RID p_area, bool p_monitoring) {
	if (Area *area = _get_area(p_area)) {
		area->monitoring = p_monitoring;
	}
}

void PhysicsServer3D::area_set_monitorable(RID p_area, bool p_monitorable) {
	if (Area *area = _get_area(p_area)) {
		area->monitorable = p_monitorable;
	}
}

void PhysicsServer3D::area_get_overlaps(RID p_area, std::vector<ObjectID> *r_bodies, std::vector<ObjectID> *r_areas) const {
	Area *area = _get_area(p_area);
	if (!area) {
		return;
	}
	for (auto &kv : area->overlaps) {
		if (kv.second.second) {
			if (r_areas) {
				r_areas->push_back(kv.second.first);
			}
		} else if (r_bodies) {
			r_bodies->push_back(kv.second.first);
		}
	}
}

void PhysicsServer3D::free_rid(RID p_rid) {
	auto oit = objects.find(p_rid.id);
	if (oit != objects.end()) {
		_remove_from_space(oit->second);
		delete oit->second;
		objects.erase(oit);
		return;
	}
	auto sit = spaces.find(p_rid.id);
	if (sit != spaces.end()) {
		for (Body *b : sit->second->bodies) {
			b->space = nullptr;
		}
		for (Area *a : sit->second->areas) {
			a->space = nullptr;
		}
		delete sit->second;
		spaces.erase(sit);
	}
}
