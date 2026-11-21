// PhysicsServer3D : simulation 3D (corps statiques, cinématiques et rigides, zones, rayons).
// Formes : sphère, boîte orientée et capsule. Détection analytique (SAT pour les boîtes),
// résolution par impulsions séquentielles avec frottement, mise en sommeil des corps au repos.
#pragma once

#include "core/math/aabb.h"
#include "servers/rendering_server.h" // RID

#include <functional>
#include <map>
#include <set>

class PhysicsServer3D {
public:
	enum BodyMode {
		BODY_MODE_STATIC,
		BODY_MODE_KINEMATIC,
		BODY_MODE_RIGID,
	};
	enum ShapeType {
		SHAPE_SPHERE,
		SHAPE_BOX,
		SHAPE_CAPSULE, // axe Y local
	};
	struct ShapeData {
		ShapeType type = SHAPE_SPHERE;
		real_t radius = 0.5f; // sphère, capsule
		real_t height = 2.0f; // capsule : hauteur totale, demi-sphères comprises
		Vector3 half_extents = Vector3(0.5f, 0.5f, 0.5f); // boîte
	};
	struct MotionResult {
		bool collided = false;
		Vector3 travel;
		Vector3 remainder;
		Vector3 normal; // de l'obstacle vers le corps
		Vector3 position; // point de contact
		real_t depth = 0;
		RID collider;
		ObjectID collider_id = 0;
		Vector3 collider_velocity;
	};
	struct RayResult {
		Vector3 position;
		Vector3 normal;
		RID rid;
		ObjectID collider_id = 0;
	};
	enum AreaBodyStatus {
		AREA_BODY_ADDED,
		AREA_BODY_REMOVED,
	};
	typedef std::function<void(AreaBodyStatus p_status, RID p_other, ObjectID p_other_id, bool p_is_area)> AreaMonitorCallback;
	typedef std::function<void(RID p_other, ObjectID p_other_id)> BodyContactCallback;

	struct Manifold {
		Vector3 normal; // de A vers B
		int count = 0;
		Vector3 points[4];
		real_t depths[4] = { 0, 0, 0, 0 };
		real_t max_depth() const {
			real_t d = 0;
			for (int i = 0; i < count; i++) {
				d = depths[i] > d ? depths[i] : d;
			}
			return d;
		}
	};

private:
	struct WorldShape {
		ShapeData data;
		Transform3D local;
		bool disabled = false;
		// Cache en coordonnées monde :
		Transform3D world;
		Vector3 seg_a, seg_b; // capsule : extrémités du segment central
		AABB aabb;
	};
	struct Space;
	struct CollisionObject {
		RID self;
		bool is_area = false;
		Space *space = nullptr;
		Transform3D xform;
		std::vector<WorldShape> shapes;
		AABB aabb;
		uint32_t layer = 1;
		uint32_t mask = 1;
		ObjectID instance_id = 0;
		virtual ~CollisionObject() {}
	};
	struct Body : CollisionObject {
		BodyMode mode = BODY_MODE_STATIC;
		Vector3 linear_velocity;
		Vector3 angular_velocity;
		real_t mass = 1, inv_mass = 0;
		Vector3 inv_inertia_local;
		Basis inv_inertia_world;
		real_t gravity_scale = 1;
		real_t bounce = 0;
		real_t friction = 1;
		real_t linear_damp = 0;
		real_t angular_damp = 0;
		bool lock_rotation = false;
		bool can_sleep = true;
		bool sleeping = false;
		real_t sleep_timer = 0;
		Vector3 force;
		std::set<uint64_t> exceptions;
		BodyContactCallback contact_callback;
		std::set<uint64_t> contacts_prev;
		// Valeurs effectives pendant la résolution (nulles si statique ou endormi).
		real_t solver_inv_mass = 0;
		Basis solver_inv_inertia;
	};
	struct Area : CollisionObject {
		bool monitoring = true;
		bool monitorable = true;
		AreaMonitorCallback callback;
		std::map<uint64_t, std::pair<ObjectID, bool>> overlaps; // rid -> (instance, is_area)
	};
	struct Space {
		RID self;
		std::vector<Body *> bodies;
		std::vector<Area *> areas;
		Vector3 gravity = Vector3(0, -9.8f, 0);
	};
	struct ContactPoint {
		Vector3 ra, rb;
		real_t depth = 0;
		real_t pn = 0, pt1 = 0, pt2 = 0; // impulsions accumulées
		real_t mass_n = 0, mass_t1 = 0, mass_t2 = 0;
		real_t bias = 0;
	};
	struct Contact {
		Body *a = nullptr;
		Body *b = nullptr;
		Vector3 normal, t1, t2;
		int count = 0;
		ContactPoint points[4];
		real_t friction = 0;
	};

	static PhysicsServer3D *singleton;
	uint64_t next_id = 1;
	std::unordered_map<uint64_t, Space *> spaces;
	std::unordered_map<uint64_t, CollisionObject *> objects;

	CollisionObject *_get_object(RID p_rid) const;
	Body *_get_body(RID p_rid) const;
	Area *_get_area(RID p_rid) const;
	void _update_cache(CollisionObject *p_object);
	void _update_mass(Body *p_body);
	void _remove_from_space(CollisionObject *p_object);
	static void _wake_space(Space *p_space);
	static bool _layers_collide(const CollisionObject *p_a, const CollisionObject *p_b);
	static bool _collide_objects(const CollisionObject *p_a, const CollisionObject *p_b, Manifold &r_deepest);
	bool _body_find_deepest(Body *p_body, Manifold &r_manifold, Body *&r_other) const;
	void _step_areas(Space *p_space);

public:
	static PhysicsServer3D *get_singleton() { return singleton; }

	// Test de collision entre deux formes placées dans le monde (exposé pour les tests).
	static bool collide_shapes(const ShapeData &p_a, const Transform3D &p_xform_a, const ShapeData &p_b, const Transform3D &p_xform_b, Manifold &r_manifold);

	// --- Espaces ---
	RID space_create();
	void space_set_gravity(RID p_space, const Vector3 &p_gravity);
	Vector3 space_get_gravity(RID p_space) const;
	void space_step(RID p_space, real_t p_delta);
	bool space_intersect_ray(RID p_space, const Vector3 &p_from, const Vector3 &p_to, RayResult &r_result, uint32_t p_mask = 0xFFFFFFFF, RID p_exclude = RID(), bool p_collide_bodies = true, bool p_collide_areas = false) const;

	// --- Commun aux corps et aux zones ---
	void collision_object_set_space(RID p_object, RID p_space);
	void collision_object_set_transform(RID p_object, const Transform3D &p_xform);
	Transform3D collision_object_get_transform(RID p_object) const;
	void collision_object_clear_shapes(RID p_object);
	void collision_object_add_shape(RID p_object, const ShapeData &p_shape, const Transform3D &p_shape_xform, bool p_disabled = false);
	void collision_object_set_collision_layer(RID p_object, uint32_t p_layer);
	void collision_object_set_collision_mask(RID p_object, uint32_t p_mask);
	void collision_object_attach_instance_id(RID p_object, ObjectID p_id);

	// --- Corps ---
	RID body_create();
	void body_set_mode(RID p_body, BodyMode p_mode);
	BodyMode body_get_mode(RID p_body) const;
	void body_set_linear_velocity(RID p_body, const Vector3 &p_velocity);
	Vector3 body_get_linear_velocity(RID p_body) const;
	void body_set_angular_velocity(RID p_body, const Vector3 &p_velocity);
	Vector3 body_get_angular_velocity(RID p_body) const;
	void body_set_mass(RID p_body, real_t p_mass);
	real_t body_get_mass(RID p_body) const;
	void body_set_gravity_scale(RID p_body, real_t p_scale);
	void body_set_bounce(RID p_body, real_t p_bounce);
	void body_set_friction(RID p_body, real_t p_friction);
	void body_set_linear_damp(RID p_body, real_t p_damp);
	void body_set_angular_damp(RID p_body, real_t p_damp);
	void body_set_lock_rotation(RID p_body, bool p_lock);
	void body_set_can_sleep(RID p_body, bool p_can_sleep);
	bool body_is_sleeping(RID p_body) const;
	void body_wake_up(RID p_body);
	void body_apply_central_impulse(RID p_body, const Vector3 &p_impulse);
	void body_apply_impulse(RID p_body, const Vector3 &p_impulse, const Vector3 &p_offset); // décalage en coordonnées monde, depuis le centre
	void body_apply_torque_impulse(RID p_body, const Vector3 &p_impulse);
	void body_apply_central_force(RID p_body, const Vector3 &p_force); // pour le prochain pas uniquement
	void body_add_collision_exception(RID p_body, RID p_other);
	void body_remove_collision_exception(RID p_body, RID p_other);
	void body_set_contact_callback(RID p_body, BodyContactCallback p_callback);
	// Déplace un corps cinématique jusqu'à la première collision (avec dépénétration).
	bool body_test_motion(RID p_body, const Vector3 &p_motion, MotionResult &r_result, bool p_test_only = false);

	// --- Zones ---
	RID area_create();
	void area_set_monitor_callback(RID p_area, AreaMonitorCallback p_callback);
	void area_set_monitoring(RID p_area, bool p_monitoring);
	void area_set_monitorable(RID p_area, bool p_monitorable);
	void area_get_overlaps(RID p_area, std::vector<ObjectID> *r_bodies, std::vector<ObjectID> *r_areas) const;

	void free_rid(RID p_rid);

	PhysicsServer3D();
	~PhysicsServer3D();
};
