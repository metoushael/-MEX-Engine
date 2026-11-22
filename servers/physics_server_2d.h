// PhysicsServer2D : simulation 2D (corps statiques, cinématiques et rigides, zones, rayons).
// Détection par SAT (polygones convexes + cercles), résolution par impulsions séquentielles.
#pragma once

#include "servers/rendering_server.h" // RID

class PhysicsServer2D : public Object {
	GDCLASS(PhysicsServer2D, Object)

public:
	enum BodyMode {
		BODY_MODE_STATIC,
		BODY_MODE_KINEMATIC,
		BODY_MODE_RIGID,
	};

	enum ShapeType {
		SHAPE_CIRCLE,
		SHAPE_POLYGON,
	};

	// Forme en coordonnées locales du corps.
	struct ShapeData {
		ShapeType type = SHAPE_CIRCLE;
		real_t radius = 0;
		Vector2 center; // cercle
		std::vector<Vector2> points; // polygone convexe
	};

	struct MotionResult {
		bool collided = false;
		Vector2 travel;
		Vector2 remainder;
		Vector2 normal;
		Vector2 position;
		real_t depth = 0;
		RID collider;
		ObjectID collider_id = 0;
		Vector2 collider_velocity;
	};

	struct RayResult {
		Vector2 position;
		Vector2 normal;
		RID rid;
		ObjectID collider_id = 0;
	};

	enum AreaBodyStatus {
		AREA_BODY_ADDED,
		AREA_BODY_REMOVED,
	};
	typedef std::function<void(AreaBodyStatus p_status, RID p_other, ObjectID p_other_id, bool p_is_area)> AreaMonitorCallback;
	typedef std::function<void(RID p_other, ObjectID p_other_id)> BodyContactCallback;

private:
	struct WorldShape {
		ShapeData local;
		bool disabled = false;
		// Cache en coordonnées monde :
		Vector2 center;
		std::vector<Vector2> points;
		std::vector<Vector2> normals;
		Rect2 aabb;
	};

	struct Space;

	struct CollisionObject {
		RID self;
		bool is_area = false;
		Space *space = nullptr;
		Transform2D xform;
		std::vector<WorldShape> shapes;
		Rect2 aabb;
		uint32_t layer = 1;
		uint32_t mask = 1;
		ObjectID instance_id = 0;
		virtual ~CollisionObject() {}
	};

	struct Body : CollisionObject {
		BodyMode mode = BODY_MODE_STATIC;
		Vector2 linear_velocity;
		real_t angular_velocity = 0;
		real_t mass = 1, inv_mass = 0;
		real_t inertia = 0, inv_inertia = 0;
		real_t gravity_scale = 1;
		real_t bounce = 0;
		real_t friction = 1;
		real_t linear_damp = 0;
		real_t angular_damp = 0;
		bool lock_rotation = false;
		Vector2 force;
		real_t torque = 0;
		std::set<uint64_t> exceptions;
		BodyContactCallback contact_callback;
		std::set<uint64_t> contacts_prev;
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
		Vector2 gravity = Vector2(0, 980);
	};

	struct Manifold {
		Vector2 normal; // de A vers B
		real_t depth = 0;
		int count = 0;
		Vector2 points[2];
		real_t depths[2] = { 0, 0 };
	};

	struct ContactPoint {
		Vector2 position;
		Vector2 ra, rb;
		real_t depth = 0;
		real_t pn = 0, pt = 0; // impulsions accumulées
		real_t mass_normal = 0, mass_tangent = 0;
		real_t bias = 0;
	};

	struct Contact {
		Body *a = nullptr;
		Body *b = nullptr;
		Vector2 normal;
		int count = 0;
		ContactPoint points[2];
		real_t friction = 0;
		real_t restitution = 0;
	};

	static PhysicsServer2D *singleton;

	uint64_t next_id = 1;
	std::unordered_map<uint64_t, Space *> spaces;
	std::unordered_map<uint64_t, CollisionObject *> objects;

	CollisionObject *_get_object(RID p_rid) const;
	Body *_get_body(RID p_rid) const;
	Area *_get_area(RID p_rid) const;
	void _update_cache(CollisionObject *p_object);
	void _update_mass(Body *p_body);
	void _remove_from_space(CollisionObject *p_object);

	static bool _collide_shapes(const WorldShape &p_a, const WorldShape &p_b, Manifold &r_manifold);
	static bool _collide_polygons(const WorldShape &p_a, const WorldShape &p_b, Manifold &r_manifold);
	static bool _collide_polygon_circle(const WorldShape &p_poly, const WorldShape &p_circle, Manifold &r_manifold);
	static bool _collide_circles(const WorldShape &p_a, const WorldShape &p_b, Manifold &r_manifold);
	static bool _collide_objects(const CollisionObject *p_a, const CollisionObject *p_b, Manifold &r_deepest);
	bool _body_find_deepest(Body *p_body, Manifold &r_manifold, Body *&r_other) const;
	void _step_areas(Space *p_space);

protected:
	static void _bind_methods() {}

public:
	static PhysicsServer2D *get_singleton() { return singleton; }

	// --- Espaces ---
	RID space_create();
	void space_set_gravity(RID p_space, const Vector2 &p_gravity);
	Vector2 space_get_gravity(RID p_space) const;
	void space_step(RID p_space, real_t p_delta);
	bool space_intersect_ray(RID p_space, const Vector2 &p_from, const Vector2 &p_to, RayResult &r_result, uint32_t p_mask = 0xFFFFFFFF, RID p_exclude = RID(), bool p_collide_bodies = true, bool p_collide_areas = false) const;
	void space_intersect_point(RID p_space, const Vector2 &p_point, std::vector<ObjectID> *r_results, uint32_t p_mask = 0xFFFFFFFF, bool p_collide_bodies = true, bool p_collide_areas = false) const;

	// --- Commun aux corps et aux zones ---
	void collision_object_set_space(RID p_object, RID p_space);
	void collision_object_set_transform(RID p_object, const Transform2D &p_xform);
	Transform2D collision_object_get_transform(RID p_object) const;
	void collision_object_clear_shapes(RID p_object);
	void collision_object_add_shape(RID p_object, const ShapeData &p_shape, const Transform2D &p_shape_xform, bool p_disabled = false);
	void collision_object_set_collision_layer(RID p_object, uint32_t p_layer);
	void collision_object_set_collision_mask(RID p_object, uint32_t p_mask);
	void collision_object_attach_instance_id(RID p_object, ObjectID p_id);

	// --- Corps ---
	RID body_create();
	void body_set_mode(RID p_body, BodyMode p_mode);
	void body_set_linear_velocity(RID p_body, const Vector2 &p_velocity);
	Vector2 body_get_linear_velocity(RID p_body) const;
	void body_set_angular_velocity(RID p_body, real_t p_velocity);
	real_t body_get_angular_velocity(RID p_body) const;
	void body_set_mass(RID p_body, real_t p_mass);
	void body_set_gravity_scale(RID p_body, real_t p_scale);
	void body_set_bounce(RID p_body, real_t p_bounce);
	void body_set_friction(RID p_body, real_t p_friction);
	void body_set_linear_damp(RID p_body, real_t p_damp);
	void body_set_angular_damp(RID p_body, real_t p_damp);
	void body_set_lock_rotation(RID p_body, bool p_lock);
	void body_apply_central_impulse(RID p_body, const Vector2 &p_impulse);
	void body_apply_impulse(RID p_body, const Vector2 &p_impulse, const Vector2 &p_offset);
	void body_apply_torque_impulse(RID p_body, real_t p_impulse);
	void body_apply_central_force(RID p_body, const Vector2 &p_force); // pour le prochain pas uniquement
	void body_add_collision_exception(RID p_body, RID p_other);
	void body_remove_collision_exception(RID p_body, RID p_other);
	void body_set_contact_callback(RID p_body, BodyContactCallback p_callback);

	// Déplace un corps cinématique jusqu'à la première collision (avec dépénétration).
	bool body_test_motion(RID p_body, const Vector2 &p_motion, MotionResult &r_result, bool p_test_only = false);
	// Le corps touche-t-il quelque chose si on le décale de p_offset ? (sonde de sol)
	bool body_test_overlap(RID p_body, const Vector2 &p_offset, std::vector<MotionResult> *r_results);

	// --- Zones ---
	RID area_create();
	void area_set_monitor_callback(RID p_area, AreaMonitorCallback p_callback);
	void area_set_monitoring(RID p_area, bool p_monitoring);
	void area_set_monitorable(RID p_area, bool p_monitorable);
	void area_get_overlaps(RID p_area, std::vector<ObjectID> *r_bodies, std::vector<ObjectID> *r_areas) const;

	void free_rid(RID p_rid);

	PhysicsServer2D();
	~PhysicsServer2D();
};
