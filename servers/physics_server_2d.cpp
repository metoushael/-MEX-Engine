#include "physics_server_2d.h"

#include <tuple>

PhysicsServer2D *PhysicsServer2D::singleton = nullptr;

static const real_t CONTACT_EPSILON = 0.01f; // pénétration ignorée par les corps cinématiques
static const real_t SOLVER_SLOP = 0.5f; // pénétration tolérée (pixels)
static const real_t SOLVER_BAUMGARTE = 0.2f;
static const int SOLVER_ITERATIONS = 10;
static const real_t BOUNCE_THRESHOLD = 40.0f; // vitesse minimale pour rebondir (px/s)

PhysicsServer2D::PhysicsServer2D() {
	singleton = this;
}

PhysicsServer2D::~PhysicsServer2D() {
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

PhysicsServer2D::CollisionObject *PhysicsServer2D::_get_object(RID p_rid) const {
	auto it = objects.find(p_rid.id);
	return it == objects.end() ? nullptr : it->second;
}

PhysicsServer2D::Body *PhysicsServer2D::_get_body(RID p_rid) const {
	CollisionObject *obj = _get_object(p_rid);
	return (obj && !obj->is_area) ? static_cast<Body *>(obj) : nullptr;
}

PhysicsServer2D::Area *PhysicsServer2D::_get_area(RID p_rid) const {
	CollisionObject *obj = _get_object(p_rid);
	return (obj && obj->is_area) ? static_cast<Area *>(obj) : nullptr;
}

// ---------------------------------------------------------------------------
// Cache monde et masse
// ---------------------------------------------------------------------------
void PhysicsServer2D::_update_cache(CollisionObject *p_object) {
	bool first = true;
	p_object->aabb = Rect2(p_object->xform.origin, Size2());
	for (WorldShape &shape : p_object->shapes) {
		if (shape.local.type == SHAPE_CIRCLE) {
			shape.center = p_object->xform.xform(shape.local.center);
			real_t r = shape.local.radius;
			shape.aabb = Rect2(shape.center.x - r, shape.center.y - r, r * 2, r * 2);
		} else {
			const size_t n = shape.local.points.size();
			shape.points.resize(n);
			shape.normals.resize(n);
			Vector2 sum;
			for (size_t i = 0; i < n; i++) {
				shape.points[i] = p_object->xform.xform(shape.local.points[i]);
				sum += shape.points[i];
			}
			shape.center = n ? sum / (real_t)n : p_object->xform.origin;
			shape.aabb = Rect2(n ? shape.points[0] : p_object->xform.origin, Size2());
			for (size_t i = 0; i < n; i++) {
				Vector2 e = shape.points[(i + 1) % n] - shape.points[i];
				shape.normals[i] = Vector2(e.y, -e.x).normalized();
				shape.aabb = shape.aabb.expand(shape.points[i]);
			}
		}
		if (shape.disabled) {
			continue;
		}
		p_object->aabb = first ? shape.aabb : p_object->aabb.merge(shape.aabb);
		first = false;
	}
}

void PhysicsServer2D::_update_mass(Body *p_body) {
	if (p_body->mode != BODY_MODE_RIGID) {
		p_body->inv_mass = 0;
		p_body->inv_inertia = 0;
		return;
	}
	p_body->inv_mass = p_body->mass > 0 ? 1.0f / p_body->mass : 0;
	real_t total_area = 0;
	real_t weighted = 0;
	for (const WorldShape &shape : p_body->shapes) {
		if (shape.disabled) {
			continue;
		}
		real_t area = 0, unit_inertia = 0;
		if (shape.local.type == SHAPE_CIRCLE) {
			real_t r = shape.local.radius;
			area = (real_t)Math_PI * r * r;
			unit_inertia = r * r * 0.5f + shape.local.center.length_squared();
		} else {
			const auto &pts = shape.local.points;
			real_t num = 0, den = 0;
			for (size_t i = 0; i < pts.size(); i++) {
				const Vector2 &a = pts[i];
				const Vector2 &b = pts[(i + 1) % pts.size()];
				real_t c = std::fabs(a.cross(b));
				num += c * (a.dot(a) + a.dot(b) + b.dot(b));
				den += c;
			}
			area = den * 0.5f;
			unit_inertia = den > 0 ? num / (6.0f * den) : 0;
		}
		total_area += area;
		weighted += area * unit_inertia;
	}
	p_body->inertia = total_area > 0 ? p_body->mass * weighted / total_area : p_body->mass;
	p_body->inv_inertia = (p_body->lock_rotation || p_body->inertia <= 0) ? 0 : 1.0f / p_body->inertia;
}

// ---------------------------------------------------------------------------
// Détection de collisions
// ---------------------------------------------------------------------------
static real_t _max_separation(const std::vector<Vector2> &a_points, const std::vector<Vector2> &a_normals, const std::vector<Vector2> &b_points, int &r_edge) {
	real_t best = -1e20f;
	for (size_t i = 0; i < a_points.size(); i++) {
		const Vector2 &n = a_normals[i];
		const Vector2 &v = a_points[i];
		real_t min_sep = 1e20f;
		for (const Vector2 &p : b_points) {
			min_sep = std::min(min_sep, n.dot(p - v));
		}
		if (min_sep > best) {
			best = min_sep;
			r_edge = (int)i;
		}
	}
	return best;
}

// Garde la partie du segment où n.dot(p) - offset <= 0.
static int _clip_segment(Vector2 r_out[2], const Vector2 p_in[2], const Vector2 &p_n, real_t p_offset) {
	int count = 0;
	real_t d0 = p_n.dot(p_in[0]) - p_offset;
	real_t d1 = p_n.dot(p_in[1]) - p_offset;
	if (d0 <= 0) {
		r_out[count++] = p_in[0];
	}
	if (d1 <= 0) {
		r_out[count++] = p_in[1];
	}
	if (d0 * d1 < 0 && count < 2) {
		real_t t = d0 / (d0 - d1);
		r_out[count++] = p_in[0] + (p_in[1] - p_in[0]) * t;
	}
	return count;
}

bool PhysicsServer2D::_collide_polygons(const WorldShape &p_a, const WorldShape &p_b, Manifold &m) {
	if (p_a.points.size() < 3 || p_b.points.size() < 3) {
		return false;
	}
	int edge_a = 0, edge_b = 0;
	real_t sep_a = _max_separation(p_a.points, p_a.normals, p_b.points, edge_a);
	if (sep_a > 0) {
		return false;
	}
	real_t sep_b = _max_separation(p_b.points, p_b.normals, p_a.points, edge_b);
	if (sep_b > 0) {
		return false;
	}

	const WorldShape *ref, *inc;
	int ref_edge;
	bool flip;
	if (sep_b > sep_a + 0.001f) {
		ref = &p_b;
		inc = &p_a;
		ref_edge = edge_b;
		flip = true;
	} else {
		ref = &p_a;
		inc = &p_b;
		ref_edge = edge_a;
		flip = false;
	}

	const Vector2 n = ref->normals[ref_edge];
	const int ref_n = (int)ref->points.size();
	const int inc_n = (int)inc->points.size();

	// Arête incidente : celle dont la normale est la plus opposée à n.
	int inc_edge = 0;
	real_t min_dot = 1e20f;
	for (int i = 0; i < inc_n; i++) {
		real_t d = n.dot(inc->normals[i]);
		if (d < min_dot) {
			min_dot = d;
			inc_edge = i;
		}
	}

	Vector2 incident[2] = { inc->points[inc_edge], inc->points[(inc_edge + 1) % inc_n] };
	const Vector2 v1 = ref->points[ref_edge];
	const Vector2 v2 = ref->points[(ref_edge + 1) % ref_n];
	const Vector2 tangent = (v2 - v1).normalized();

	Vector2 clip1[2], clip2[2];
	int count = _clip_segment(clip1, incident, -tangent, -tangent.dot(v1));
	if (count == 2) {
		count = _clip_segment(clip2, clip1, tangent, tangent.dot(v2));
	} else {
		clip2[0] = clip1[0];
	}

	m.normal = flip ? -n : n;
	m.depth = -(flip ? sep_b : sep_a);
	m.count = 0;
	for (int i = 0; i < count; i++) {
		real_t sep = n.dot(clip2[i] - v1);
		if (sep <= 0) {
			m.points[m.count] = clip2[i] - n * (sep * 0.5f);
			m.depths[m.count] = -sep;
			m.count++;
		}
	}
	if (m.count == 0) {
		// Repli : point le plus enfoncé du polygone incident.
		real_t best = 1e20f;
		Vector2 best_p = inc->points[0];
		for (const Vector2 &p : inc->points) {
			real_t sep = n.dot(p - v1);
			if (sep < best) {
				best = sep;
				best_p = p;
			}
		}
		m.points[0] = best_p;
		m.depths[0] = m.depth;
		m.count = 1;
	}
	return true;
}

bool PhysicsServer2D::_collide_polygon_circle(const WorldShape &p_poly, const WorldShape &p_circle, Manifold &m) {
	const int n = (int)p_poly.points.size();
	if (n < 3) {
		return false;
	}
	const Vector2 c = p_circle.center;
	const real_t r = p_circle.local.radius;

	int edge = 0;
	real_t sep = -1e20f;
	for (int i = 0; i < n; i++) {
		real_t s = p_poly.normals[i].dot(c - p_poly.points[i]);
		if (s > r) {
			return false;
		}
		if (s > sep) {
			sep = s;
			edge = i;
		}
	}
	const Vector2 v1 = p_poly.points[edge];
	const Vector2 v2 = p_poly.points[(edge + 1) % n];
	m.count = 1;
	if (sep < (real_t)CMP_EPSILON) { // centre à l'intérieur du polygone
		m.normal = p_poly.normals[edge];
		m.depth = r - sep;
		m.points[0] = c - m.normal * sep;
		m.depths[0] = m.depth;
		return true;
	}
	real_t u1 = (c - v1).dot(v2 - v1);
	real_t u2 = (c - v2).dot(v1 - v2);
	if (u1 <= 0) {
		real_t d = c.distance_to(v1);
		if (d > r) {
			return false;
		}
		m.normal = d > 0 ? (c - v1) / d : p_poly.normals[edge];
		m.depth = r - d;
		m.points[0] = v1;
	} else if (u2 <= 0) {
		real_t d = c.distance_to(v2);
		if (d > r) {
			return false;
		}
		m.normal = d > 0 ? (c - v2) / d : p_poly.normals[edge];
		m.depth = r - d;
		m.points[0] = v2;
	} else {
		m.normal = p_poly.normals[edge];
		m.depth = r - sep;
		m.points[0] = c - m.normal * sep;
	}
	m.depths[0] = m.depth;
	return true;
}

bool PhysicsServer2D::_collide_circles(const WorldShape &p_a, const WorldShape &p_b, Manifold &m) {
	Vector2 d = p_b.center - p_a.center;
	real_t dist = d.length();
	real_t rsum = p_a.local.radius + p_b.local.radius;
	if (dist > rsum) {
		return false;
	}
	m.normal = dist > 0 ? d / dist : Vector2(0, -1);
	m.depth = rsum - dist;
	m.count = 1;
	m.points[0] = p_a.center + m.normal * (p_a.local.radius - m.depth * 0.5f);
	m.depths[0] = m.depth;
	return true;
}

bool PhysicsServer2D::_collide_shapes(const WorldShape &p_a, const WorldShape &p_b, Manifold &m) {
	if (!p_a.aabb.grow(0.01f).intersects(p_b.aabb)) {
		return false;
	}
	const bool a_circle = p_a.local.type == SHAPE_CIRCLE;
	const bool b_circle = p_b.local.type == SHAPE_CIRCLE;
	if (a_circle && b_circle) {
		return _collide_circles(p_a, p_b, m);
	}
	if (!a_circle && !b_circle) {
		return _collide_polygons(p_a, p_b, m);
	}
	if (!a_circle && b_circle) {
		return _collide_polygon_circle(p_a, p_b, m);
	}
	if (!_collide_polygon_circle(p_b, p_a, m)) {
		return false;
	}
	m.normal = -m.normal;
	return true;
}

// Retourne le contact le plus profond entre deux objets (toutes formes confondues).
bool PhysicsServer2D::_collide_objects(const CollisionObject *p_a, const CollisionObject *p_b, Manifold &r_deepest) {
	if (!p_a->aabb.grow(0.01f).intersects(p_b->aabb)) {
		return false;
	}
	bool found = false;
	for (const WorldShape &sa : p_a->shapes) {
		if (sa.disabled) {
			continue;
		}
		for (const WorldShape &sb : p_b->shapes) {
			if (sb.disabled) {
				continue;
			}
			Manifold m;
			if (_collide_shapes(sa, sb, m) && (!found || m.depth > r_deepest.depth)) {
				r_deepest = m;
				found = true;
			}
		}
	}
	return found;
}

// ---------------------------------------------------------------------------
// Espaces
// ---------------------------------------------------------------------------
RID PhysicsServer2D::space_create() {
	Space *space = new Space;
	space->self.id = next_id++;
	spaces[space->self.id] = space;
	return space->self;
}

void PhysicsServer2D::space_set_gravity(RID p_space, const Vector2 &p_gravity) {
	auto it = spaces.find(p_space.id);
	if (it != spaces.end()) {
		it->second->gravity = p_gravity;
	}
}

Vector2 PhysicsServer2D::space_get_gravity(RID p_space) const {
	auto it = spaces.find(p_space.id);
	return it == spaces.end() ? Vector2() : it->second->gravity;
}

static inline Vector2 _cross_wr(real_t w, const Vector2 &r) {
	return Vector2(-w * r.y, w * r.x);
}

void PhysicsServer2D::space_step(RID p_space, real_t p_delta) {
	auto space_it = spaces.find(p_space.id);
	if (space_it == spaces.end() || p_delta <= 0) {
		return;
	}
	Space *space = space_it->second;

	// 1. Intégration des forces.
	for (Body *body : space->bodies) {
		if (body->mode != BODY_MODE_RIGID) {
			continue;
		}
		body->linear_velocity += (space->gravity * body->gravity_scale + body->force * body->inv_mass) * p_delta;
		body->angular_velocity += body->torque * body->inv_inertia * p_delta;
		body->linear_velocity *= 1.0f / (1.0f + p_delta * body->linear_damp);
		body->angular_velocity *= 1.0f / (1.0f + p_delta * body->angular_damp);
		body->force = Vector2();
		body->torque = 0;
	}

	// 2. Détection des contacts impliquant au moins un corps rigide.
	std::vector<Contact> contacts;
	std::vector<std::pair<Body *, Body *>> touching;
	const size_t body_count = space->bodies.size();
	for (size_t i = 0; i < body_count; i++) {
		Body *a = space->bodies[i];
		for (size_t j = i + 1; j < body_count; j++) {
			Body *b = space->bodies[j];
			if (a->mode != BODY_MODE_RIGID && b->mode != BODY_MODE_RIGID) {
				continue;
			}
			if (!((a->mask & b->layer) || (b->mask & a->layer))) {
				continue;
			}
			if (a->exceptions.count(b->self.id) || b->exceptions.count(a->self.id)) {
				continue;
			}
			if (!a->aabb.grow(0.01f).intersects(b->aabb)) {
				continue;
			}
			bool any = false;
			for (const WorldShape &sa : a->shapes) {
				if (sa.disabled) {
					continue;
				}
				for (const WorldShape &sb : b->shapes) {
					if (sb.disabled) {
						continue;
					}
					Manifold m;
					if (!_collide_shapes(sa, sb, m)) {
						continue;
					}
					any = true;
					Contact c;
					c.a = a;
					c.b = b;
					c.normal = m.normal;
					c.count = m.count;
					c.friction = std::sqrt(std::max(0.0f, a->friction * b->friction));
					c.restitution = std::max(a->bounce, b->bounce);
					for (int k = 0; k < m.count; k++) {
						c.points[k].position = m.points[k];
						c.points[k].depth = m.depths[k];
					}
					contacts.push_back(c);
				}
			}
			if (any) {
				touching.push_back({ a, b });
			}
		}
	}

	// 3. Pré-calcul des contraintes.
	for (Contact &c : contacts) {
		const Vector2 n = c.normal;
		const Vector2 t(-n.y, n.x);
		for (int k = 0; k < c.count; k++) {
			ContactPoint &p = c.points[k];
			p.ra = p.position - c.a->xform.origin;
			p.rb = p.position - c.b->xform.origin;
			real_t rna = p.ra.cross(n), rnb = p.rb.cross(n);
			real_t kn = c.a->inv_mass + c.b->inv_mass + c.a->inv_inertia * rna * rna + c.b->inv_inertia * rnb * rnb;
			p.mass_normal = kn > 0 ? 1.0f / kn : 0;
			real_t rta = p.ra.cross(t), rtb = p.rb.cross(t);
			real_t kt = c.a->inv_mass + c.b->inv_mass + c.a->inv_inertia * rta * rta + c.b->inv_inertia * rtb * rtb;
			p.mass_tangent = kt > 0 ? 1.0f / kt : 0;
			p.bias = SOLVER_BAUMGARTE / p_delta * std::max(0.0f, p.depth - SOLVER_SLOP);
			Vector2 dv = c.b->linear_velocity + _cross_wr(c.b->angular_velocity, p.rb) - c.a->linear_velocity - _cross_wr(c.a->angular_velocity, p.ra);
			real_t vn = dv.dot(n);
			if (vn < -BOUNCE_THRESHOLD) {
				p.bias += -c.restitution * vn;
			}
		}
	}

	// 4. Résolution itérative par impulsions.
	for (int iter = 0; iter < SOLVER_ITERATIONS; iter++) {
		for (Contact &c : contacts) {
			Body *a = c.a;
			Body *b = c.b;
			const Vector2 n = c.normal;
			const Vector2 t(-n.y, n.x);
			for (int k = 0; k < c.count; k++) {
				ContactPoint &p = c.points[k];
				// Normale
				Vector2 dv = b->linear_velocity + _cross_wr(b->angular_velocity, p.rb) - a->linear_velocity - _cross_wr(a->angular_velocity, p.ra);
				real_t dpn = p.mass_normal * (-dv.dot(n) + p.bias);
				real_t pn0 = p.pn;
				p.pn = std::max(pn0 + dpn, 0.0f);
				dpn = p.pn - pn0;
				Vector2 impulse = n * dpn;
				a->linear_velocity -= impulse * a->inv_mass;
				a->angular_velocity -= a->inv_inertia * p.ra.cross(impulse);
				b->linear_velocity += impulse * b->inv_mass;
				b->angular_velocity += b->inv_inertia * p.rb.cross(impulse);
				// Frottement
				dv = b->linear_velocity + _cross_wr(b->angular_velocity, p.rb) - a->linear_velocity - _cross_wr(a->angular_velocity, p.ra);
				real_t dpt = p.mass_tangent * (-dv.dot(t));
				real_t max_pt = c.friction * p.pn;
				real_t pt0 = p.pt;
				p.pt = Math::clamp(pt0 + dpt, -max_pt, max_pt);
				dpt = p.pt - pt0;
				impulse = t * dpt;
				a->linear_velocity -= impulse * a->inv_mass;
				a->angular_velocity -= a->inv_inertia * p.ra.cross(impulse);
				b->linear_velocity += impulse * b->inv_mass;
				b->angular_velocity += b->inv_inertia * p.rb.cross(impulse);
			}
		}
	}

	// 5. Intégration des positions.
	for (Body *body : space->bodies) {
		if (body->mode != BODY_MODE_RIGID) {
			continue;
		}
		// Seuil de repos : évite les micro-tremblements.
		if (body->linear_velocity.length_squared() < 0.25f) {
			body->linear_velocity = Vector2();
		}
		if (std::fabs(body->angular_velocity) < 0.005f) {
			body->angular_velocity = 0;
		}
		real_t rot = body->xform.get_rotation() + body->angular_velocity * p_delta;
		Vector2 origin = body->xform.origin + body->linear_velocity * p_delta;
		body->xform = Transform2D(rot, origin);
		_update_cache(body);
	}

	// 6. Notifications de contact des corps (body_entered).
	{
		std::map<uint64_t, std::set<uint64_t>> now;
		std::vector<std::pair<Body *, Body *>> entered;
		for (auto &pair : touching) {
			for (int side = 0; side < 2; side++) {
				Body *self = side == 0 ? pair.first : pair.second;
				Body *other = side == 0 ? pair.second : pair.first;
				if (!self->contact_callback) {
					continue;
				}
				now[self->self.id].insert(other->self.id);
				if (!self->contacts_prev.count(other->self.id)) {
					entered.push_back({ self, other });
				}
			}
		}
		for (Body *body : space->bodies) {
			if (body->contact_callback) {
				body->contacts_prev = now[body->self.id];
			}
		}
		std::vector<std::tuple<RID, RID, ObjectID>> events;
		for (auto &e : entered) {
			events.push_back(std::make_tuple(e.first->self, e.second->self, e.second->instance_id));
		}
		for (auto &e : events) {
			Body *self = _get_body(std::get<0>(e));
			if (self && self->contact_callback) {
				BodyContactCallback cb = self->contact_callback;
				cb(std::get<1>(e), std::get<2>(e));
			}
		}
	}

	// 7. Zones.
	if (spaces.count(p_space.id)) {
		_step_areas(space);
	}
}

void PhysicsServer2D::_step_areas(Space *p_space) {
	struct Event {
		RID area;
		AreaBodyStatus status;
		RID other;
		ObjectID other_id;
		bool is_area;
	};
	std::vector<Event> events;

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

static bool _ray_shape(const Vector2 &p_from, const Vector2 &p_dir, int p_type, const Vector2 &p_center, real_t p_radius, const std::vector<Vector2> &p_points, const std::vector<Vector2> &p_normals, real_t &r_t, Vector2 &r_normal) {
	if (p_type == PhysicsServer2D::SHAPE_CIRCLE) {
		Vector2 m = p_from - p_center;
		real_t a = p_dir.dot(p_dir);
		real_t b = m.dot(p_dir);
		real_t c = m.dot(m) - p_radius * p_radius;
		if (c <= 0 || a <= 0) {
			return false; // départ à l'intérieur : ignoré
		}
		real_t disc = b * b - a * c;
		if (disc < 0) {
			return false;
		}
		real_t t = (-b - std::sqrt(disc)) / a;
		if (t < 0 || t > 1) {
			return false;
		}
		r_t = t;
		r_normal = (p_from + p_dir * t - p_center).normalized();
		return true;
	}
	real_t tmin = 0, tmax = 1;
	int enter_edge = -1;
	for (size_t i = 0; i < p_points.size(); i++) {
		real_t denom = p_normals[i].dot(p_dir);
		real_t dist = p_normals[i].dot(p_points[i] - p_from);
		if (std::fabs(denom) < 1e-9f) {
			if (dist < 0) {
				return false;
			}
			continue;
		}
		real_t t = dist / denom;
		if (denom < 0) {
			if (t > tmin) {
				tmin = t;
				enter_edge = (int)i;
			}
		} else {
			tmax = std::min(tmax, t);
		}
		if (tmin > tmax) {
			return false;
		}
	}
	if (enter_edge < 0) {
		return false;
	}
	r_t = tmin;
	r_normal = p_normals[enter_edge];
	return true;
}

bool PhysicsServer2D::space_intersect_ray(RID p_space, const Vector2 &p_from, const Vector2 &p_to, RayResult &r_result, uint32_t p_mask, RID p_exclude, bool p_collide_bodies, bool p_collide_areas) const {
	auto it = spaces.find(p_space.id);
	if (it == spaces.end()) {
		return false;
	}
	const Space *space = it->second;
	const Vector2 dir = p_to - p_from;
	Rect2 ray_aabb = Rect2(p_from, Size2()).expand(p_to).grow(1.0f);
	real_t best_t = 2.0f;
	bool hit = false;

	auto test = [&](const CollisionObject *obj) {
		if (obj->self == p_exclude || !(p_mask & obj->layer) || !ray_aabb.intersects(obj->aabb.grow(0.5f))) {
			return;
		}
		for (const WorldShape &shape : obj->shapes) {
			if (shape.disabled) {
				continue;
			}
			real_t t;
			Vector2 normal;
			if (_ray_shape(p_from, dir, shape.local.type, shape.center, shape.local.radius, shape.points, shape.normals, t, normal) && t < best_t) {
				best_t = t;
				hit = true;
				r_result.position = p_from + dir * t;
				r_result.normal = normal;
				r_result.rid = obj->self;
				r_result.collider_id = obj->instance_id;
			}
		}
	};
	if (p_collide_bodies) {
		for (const Body *body : space->bodies) {
			test(body);
		}
	}
	if (p_collide_areas) {
		for (const Area *area : space->areas) {
			test(area);
		}
	}
	return hit;
}

void PhysicsServer2D::space_intersect_point(RID p_space, const Vector2 &p_point, std::vector<ObjectID> *r_results, uint32_t p_mask, bool p_collide_bodies, bool p_collide_areas) const {
	auto it = spaces.find(p_space.id);
	if (it == spaces.end()) {
		return;
	}
	const Space *space = it->second;
	auto test = [&](const CollisionObject *obj) {
		if (!(p_mask & obj->layer) || !obj->aabb.grow(0.01f).has_point(p_point)) {
			return;
		}
		for (const WorldShape &shape : obj->shapes) {
			if (shape.disabled) {
				continue;
			}
			bool inside = true;
			if (shape.local.type == SHAPE_CIRCLE) {
				inside = shape.center.distance_squared_to(p_point) <= shape.local.radius * shape.local.radius;
			} else {
				for (size_t i = 0; i < shape.points.size() && inside; i++) {
					inside = shape.normals[i].dot(p_point - shape.points[i]) <= 0;
				}
			}
			if (inside) {
				r_results->push_back(obj->instance_id);
				return;
			}
		}
	};
	if (p_collide_bodies) {
		for (const Body *body : space->bodies) {
			test(body);
		}
	}
	if (p_collide_areas) {
		for (const Area *area : space->areas) {
			test(area);
		}
	}
}

// ---------------------------------------------------------------------------
// Objets de collision
// ---------------------------------------------------------------------------
void PhysicsServer2D::_remove_from_space(CollisionObject *p_object) {
	if (!p_object->space) {
		return;
	}
	Space *space = p_object->space;
	if (p_object->is_area) {
		auto &v = space->areas;
		v.erase(std::remove(v.begin(), v.end(), static_cast<Area *>(p_object)), v.end());
		static_cast<Area *>(p_object)->overlaps.clear();
	} else {
		auto &v = space->bodies;
		v.erase(std::remove(v.begin(), v.end(), static_cast<Body *>(p_object)), v.end());
		static_cast<Body *>(p_object)->contacts_prev.clear();
	}
	p_object->space = nullptr;
}

void PhysicsServer2D::collision_object_set_space(RID p_object, RID p_space) {
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
	if (space) {
		obj->space = space;
		if (obj->is_area) {
			space->areas.push_back(static_cast<Area *>(obj));
		} else {
			space->bodies.push_back(static_cast<Body *>(obj));
		}
	}
}

void PhysicsServer2D::collision_object_set_transform(RID p_object, const Transform2D &p_xform) {
	CollisionObject *obj = _get_object(p_object);
	if (!obj) {
		return;
	}
	// Le serveur ne garde que rotation + translation (l'échelle est cuite dans les formes).
	obj->xform = Transform2D(p_xform.get_rotation(), p_xform.origin);
	_update_cache(obj);
}

Transform2D PhysicsServer2D::collision_object_get_transform(RID p_object) const {
	CollisionObject *obj = _get_object(p_object);
	return obj ? obj->xform : Transform2D();
}

void PhysicsServer2D::collision_object_clear_shapes(RID p_object) {
	CollisionObject *obj = _get_object(p_object);
	if (!obj) {
		return;
	}
	obj->shapes.clear();
	_update_cache(obj);
	if (!obj->is_area) {
		_update_mass(static_cast<Body *>(obj));
	}
}

void PhysicsServer2D::collision_object_add_shape(RID p_object, const ShapeData &p_shape, const Transform2D &p_shape_xform, bool p_disabled) {
	CollisionObject *obj = _get_object(p_object);
	if (!obj) {
		return;
	}
	WorldShape shape;
	shape.disabled = p_disabled;
	shape.local.type = p_shape.type;
	if (p_shape.type == SHAPE_CIRCLE) {
		Size2 scale = p_shape_xform.get_scale().abs();
		shape.local.center = p_shape_xform.xform(p_shape.center);
		shape.local.radius = p_shape.radius * std::max(scale.x, scale.y);
	} else {
		real_t area = 0;
		const size_t n = p_shape.points.size();
		shape.local.points.resize(n);
		for (size_t i = 0; i < n; i++) {
			shape.local.points[i] = p_shape_xform.xform(p_shape.points[i]);
		}
		for (size_t i = 0; i < n; i++) {
			area += shape.local.points[i].cross(shape.local.points[(i + 1) % n]);
		}
		if (area < 0) { // impose un sens de parcours cohérent (normales sortantes)
			std::reverse(shape.local.points.begin(), shape.local.points.end());
		}
	}
	obj->shapes.push_back(shape);
	_update_cache(obj);
	if (!obj->is_area) {
		_update_mass(static_cast<Body *>(obj));
	}
}

void PhysicsServer2D::collision_object_set_collision_layer(RID p_object, uint32_t p_layer) {
	if (CollisionObject *obj = _get_object(p_object)) {
		obj->layer = p_layer;
	}
}

void PhysicsServer2D::collision_object_set_collision_mask(RID p_object, uint32_t p_mask) {
	if (CollisionObject *obj = _get_object(p_object)) {
		obj->mask = p_mask;
	}
}

void PhysicsServer2D::collision_object_attach_instance_id(RID p_object, ObjectID p_id) {
	if (CollisionObject *obj = _get_object(p_object)) {
		obj->instance_id = p_id;
	}
}

// ---------------------------------------------------------------------------
// Corps
// ---------------------------------------------------------------------------
RID PhysicsServer2D::body_create() {
	Body *body = new Body;
	body->self.id = next_id++;
	objects[body->self.id] = body;
	return body->self;
}

void PhysicsServer2D::body_set_mode(RID p_body, BodyMode p_mode) {
	if (Body *body = _get_body(p_body)) {
		body->mode = p_mode;
		_update_mass(body);
	}
}

void PhysicsServer2D::body_set_linear_velocity(RID p_body, const Vector2 &p_velocity) {
	if (Body *body = _get_body(p_body)) {
		body->linear_velocity = p_velocity;
	}
}

Vector2 PhysicsServer2D::body_get_linear_velocity(RID p_body) const {
	Body *body = _get_body(p_body);
	return body ? body->linear_velocity : Vector2();
}

void PhysicsServer2D::body_set_angular_velocity(RID p_body, real_t p_velocity) {
	if (Body *body = _get_body(p_body)) {
		body->angular_velocity = p_velocity;
	}
}

real_t PhysicsServer2D::body_get_angular_velocity(RID p_body) const {
	Body *body = _get_body(p_body);
	return body ? body->angular_velocity : 0;
}

void PhysicsServer2D::body_set_mass(RID p_body, real_t p_mass) {
	if (Body *body = _get_body(p_body)) {
		body->mass = std::max(p_mass, 0.001f);
		_update_mass(body);
	}
}

void PhysicsServer2D::body_set_gravity_scale(RID p_body, real_t p_scale) {
	if (Body *body = _get_body(p_body)) {
		body->gravity_scale = p_scale;
	}
}

void PhysicsServer2D::body_set_bounce(RID p_body, real_t p_bounce) {
	if (Body *body = _get_body(p_body)) {
		body->bounce = Math::clamp(p_bounce, 0.0f, 1.0f);
	}
}

void PhysicsServer2D::body_set_friction(RID p_body, real_t p_friction) {
	if (Body *body = _get_body(p_body)) {
		body->friction = std::max(p_friction, 0.0f);
	}
}

void PhysicsServer2D::body_set_linear_damp(RID p_body, real_t p_damp) {
	if (Body *body = _get_body(p_body)) {
		body->linear_damp = std::max(p_damp, 0.0f);
	}
}

void PhysicsServer2D::body_set_angular_damp(RID p_body, real_t p_damp) {
	if (Body *body = _get_body(p_body)) {
		body->angular_damp = std::max(p_damp, 0.0f);
	}
}

void PhysicsServer2D::body_set_lock_rotation(RID p_body, bool p_lock) {
	if (Body *body = _get_body(p_body)) {
		body->lock_rotation = p_lock;
		_update_mass(body);
	}
}

void PhysicsServer2D::body_apply_central_impulse(RID p_body, const Vector2 &p_impulse) {
	if (Body *body = _get_body(p_body)) {
		body->linear_velocity += p_impulse * body->inv_mass;
	}
}

void PhysicsServer2D::body_apply_impulse(RID p_body, const Vector2 &p_impulse, const Vector2 &p_offset) {
	if (Body *body = _get_body(p_body)) {
		body->linear_velocity += p_impulse * body->inv_mass;
		body->angular_velocity += body->inv_inertia * p_offset.cross(p_impulse);
	}
}

void PhysicsServer2D::body_apply_torque_impulse(RID p_body, real_t p_impulse) {
	if (Body *body = _get_body(p_body)) {
		body->angular_velocity += body->inv_inertia * p_impulse;
	}
}

void PhysicsServer2D::body_apply_central_force(RID p_body, const Vector2 &p_force) {
	if (Body *body = _get_body(p_body)) {
		body->force += p_force;
	}
}

void PhysicsServer2D::body_add_collision_exception(RID p_body, RID p_other) {
	if (Body *body = _get_body(p_body)) {
		body->exceptions.insert(p_other.id);
	}
}

void PhysicsServer2D::body_remove_collision_exception(RID p_body, RID p_other) {
	if (Body *body = _get_body(p_body)) {
		body->exceptions.erase(p_other.id);
	}
}

void PhysicsServer2D::body_set_contact_callback(RID p_body, BodyContactCallback p_callback) {
	if (Body *body = _get_body(p_body)) {
		body->contact_callback = p_callback;
		body->contacts_prev.clear();
	}
}

bool PhysicsServer2D::_body_find_deepest(Body *p_body, Manifold &r_manifold, Body *&r_other) const {
	bool found = false;
	r_manifold.depth = CONTACT_EPSILON;
	for (Body *other : p_body->space->bodies) {
		if (other == p_body || !(p_body->mask & other->layer)) {
			continue;
		}
		if (p_body->exceptions.count(other->self.id) || other->exceptions.count(p_body->self.id)) {
			continue;
		}
		Manifold m;
		// Normale de `other` vers le corps testé : c'est la direction de sortie.
		if (_collide_objects(other, p_body, m) && m.depth > r_manifold.depth) {
			r_manifold = m;
			r_other = other;
			found = true;
		}
	}
	return found;
}

bool PhysicsServer2D::body_test_motion(RID p_body, const Vector2 &p_motion, MotionResult &r_result, bool p_test_only) {
	r_result = MotionResult();
	Body *body = _get_body(p_body);
	if (!body) {
		return false;
	}
	const Transform2D start = body->xform;
	if (!body->space) {
		r_result.travel = p_motion;
		if (!p_test_only) {
			body->xform.origin += p_motion;
			_update_cache(body);
		}
		return false;
	}

	// Avance par petits pas pour ne pas traverser les obstacles fins.
	const real_t length = p_motion.length();
	const real_t min_extent = std::max(1.0f, std::min(body->aabb.size.x, body->aabb.size.y));
	const real_t step_length = std::max(min_extent * 0.25f, 1.0f);
	const int steps = Math::clamp((int)std::ceil(length / step_length), 1, 64);

	for (int i = 1; i <= steps && !r_result.collided; i++) {
		const real_t t = (real_t)i / (real_t)steps;
		body->xform.origin = start.origin + p_motion * t;
		_update_cache(body);
		for (int iter = 0; iter < 8; iter++) {
			Manifold m;
			Body *other = nullptr;
			if (!_body_find_deepest(body, m, other)) {
				break;
			}
			if (!r_result.collided) {
				r_result.collided = true;
				r_result.normal = m.normal;
				r_result.depth = m.depth;
				r_result.position = m.points[0];
				r_result.collider = other->self;
				r_result.collider_id = other->instance_id;
				r_result.collider_velocity = other->linear_velocity;
				r_result.remainder = p_motion * (1.0f - t);
			}
			body->xform.origin += m.normal * m.depth;
			_update_cache(body);
		}
	}
	r_result.travel = body->xform.origin - start.origin;
	if (p_test_only) {
		body->xform = start;
		_update_cache(body);
	}
	return r_result.collided;
}

bool PhysicsServer2D::body_test_overlap(RID p_body, const Vector2 &p_offset, std::vector<MotionResult> *r_results) {
	Body *body = _get_body(p_body);
	if (!body || !body->space) {
		return false;
	}
	const Transform2D start = body->xform;
	body->xform.origin += p_offset;
	_update_cache(body);
	bool any = false;
	for (Body *other : body->space->bodies) {
		if (other == body || !(body->mask & other->layer)) {
			continue;
		}
		if (body->exceptions.count(other->self.id) || other->exceptions.count(body->self.id)) {
			continue;
		}
		Manifold m;
		if (_collide_objects(other, body, m) && m.depth > CONTACT_EPSILON) {
			any = true;
			if (r_results) {
				MotionResult r;
				r.collided = true;
				r.normal = m.normal;
				r.depth = m.depth;
				r.position = m.points[0];
				r.collider = other->self;
				r.collider_id = other->instance_id;
				r.collider_velocity = other->linear_velocity;
				r_results->push_back(r);
			}
		}
	}
	body->xform = start;
	_update_cache(body);
	return any;
}

// ---------------------------------------------------------------------------
// Zones
// ---------------------------------------------------------------------------
RID PhysicsServer2D::area_create() {
	Area *area = new Area;
	area->self.id = next_id++;
	area->is_area = true;
	objects[area->self.id] = area;
	return area->self;
}

void PhysicsServer2D::area_set_monitor_callback(RID p_area, AreaMonitorCallback p_callback) {
	if (Area *area = _get_area(p_area)) {
		area->callback = p_callback;
	}
}

void PhysicsServer2D::area_set_monitoring(RID p_area, bool p_monitoring) {
	if (Area *area = _get_area(p_area)) {
		area->monitoring = p_monitoring;
	}
}

void PhysicsServer2D::area_set_monitorable(RID p_area, bool p_monitorable) {
	if (Area *area = _get_area(p_area)) {
		area->monitorable = p_monitorable;
	}
}

void PhysicsServer2D::area_get_overlaps(RID p_area, std::vector<ObjectID> *r_bodies, std::vector<ObjectID> *r_areas) const {
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

void PhysicsServer2D::free_rid(RID p_rid) {
	if (!p_rid.is_valid()) {
		return;
	}
	auto space_it = spaces.find(p_rid.id);
	if (space_it != spaces.end()) {
		Space *space = space_it->second;
		for (Body *body : space->bodies) {
			body->space = nullptr;
		}
		for (Area *area : space->areas) {
			area->space = nullptr;
		}
		delete space;
		spaces.erase(space_it);
		return;
	}
	auto obj_it = objects.find(p_rid.id);
	if (obj_it != objects.end()) {
		_remove_from_space(obj_it->second);
		delete obj_it->second;
		objects.erase(obj_it);
	}
}
