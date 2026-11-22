// Nœuds de physique 3D : StaticBody3D, CharacterBody3D, RigidBody3D et Area3D.
#pragma once

#include "scene/3d/node_3d.h"
#include "scene/main/scene_tree.h"
#include "servers/physics_server_3d.h"

class CollisionObject3D : public Node3D, public PhysicsSyncListener {
	GDCLASS(CollisionObject3D, Node3D)

	struct ShapeEntry {
		ObjectID node = 0;
		const void *shape = nullptr;
		uint32_t version = 0;
		Transform3D xform;
		Vector3 scale = Vector3(1, 1, 1);
		bool disabled = false;
		bool operator==(const ShapeEntry &o) const {
			// Comparaison approchée : la décomposition échelle / rotation d'un corps qui tourne
			// varie de quelques bits d'une image à l'autre.
			bool same = node == o.node && shape == o.shape && version == o.version && disabled == o.disabled;
			same = same && scale.is_equal_approx(o.scale) && xform.origin.is_equal_approx(o.xform.origin);
			for (int i = 0; i < 3 && same; i++) {
				same = xform.basis.rows[i].is_equal_approx(o.xform.basis.rows[i]);
			}
			return same;
		}
	};

	RID rid;
	bool is_area = false;
	uint32_t collision_layer = 1;
	uint32_t collision_mask = 1;
	std::vector<ShapeEntry> shape_cache;
	bool in_space = false;

protected:
	Transform3D last_pushed; // dernière transformation envoyée au serveur ou lue depuis lui
	Transform3D last_pulled; // dernière transformation lue depuis le serveur
	bool has_pulled = false;
	bool has_pushed = false;

	void _notification(int p_what);
	static void _bind_methods();

	void _sync_shapes();
	// Envoie la transformation du nœud au serveur si elle a changé. Retourne vrai si envoyée.
	bool _push_transform();
	void _pull_transform(); // serveur -> nœud
	bool _is_in_space() const { return in_space; }
	virtual void _space_entered() {}

public:
	RID get_rid() const { return rid; }
	void set_collision_layer(int p_layer);
	int get_collision_layer() const { return (int)collision_layer; }
	void set_collision_mask(int p_mask);
	int get_collision_mask() const { return (int)collision_mask; }
	void set_collision_layer_value(int p_layer_number, bool p_value);
	bool get_collision_layer_value(int p_layer_number) const;
	void set_collision_mask_value(int p_layer_number, bool p_value);
	bool get_collision_mask_value(int p_layer_number) const;

	virtual void _physics_pre_step() override;
	virtual void _physics_post_step() override {}
	virtual void _get_debug_collision_lines(std::vector<RenderingServer3D::Line> *r_lines) const override {}

	CollisionObject3D(bool p_area = false);
	~CollisionObject3D();
};

class PhysicsBody3D : public CollisionObject3D {
	GDCLASS(PhysicsBody3D, CollisionObject3D)

protected:
	static void _bind_methods();

public:
	// Déplace le corps ; retourne un dictionnaire décrivant la collision (vide si aucune).
	Dictionary move_and_collide(const Vector3 &p_motion, bool p_test_only = false);
	bool test_move(const Vector3 &p_motion);
	void add_collision_exception_with(Node *p_node);
	void remove_collision_exception_with(Node *p_node);
};

class StaticBody3D : public PhysicsBody3D {
	GDCLASS(StaticBody3D, PhysicsBody3D)

	real_t friction = 0.6f;
	real_t bounce = 0.0f;

protected:
	static void _bind_methods();

public:
	void set_friction(real_t p_friction);
	real_t get_friction() const { return friction; }
	void set_bounce(real_t p_bounce);
	real_t get_bounce() const { return bounce; }

	StaticBody3D();
};

class CharacterBody3D : public PhysicsBody3D {
	GDCLASS(CharacterBody3D, PhysicsBody3D)

	Vector3 velocity;
	Vector3 up_direction = Vector3(0, 1, 0);
	real_t floor_max_angle = 0.785398f; // 45°
	bool floor_stop_on_slope = true;
	real_t floor_snap_length = 0.1f;
	int max_slides = 4;

	bool on_floor = false;
	bool on_wall = false;
	bool on_ceiling = false;
	Vector3 floor_normal;
	Vector3 wall_normal;
	std::vector<PhysicsServer3D::MotionResult> slide_collisions;

	void _classify(const Vector3 &p_normal);

protected:
	static void _bind_methods();

public:
	void set_velocity(const Vector3 &p_velocity) { velocity = p_velocity; }
	Vector3 get_velocity() const { return velocity; }
	void set_up_direction(const Vector3 &p_up);
	Vector3 get_up_direction() const { return up_direction; }
	void set_floor_max_angle(real_t p_radians) { floor_max_angle = p_radians; }
	real_t get_floor_max_angle() const { return floor_max_angle; }
	void set_floor_stop_on_slope(bool p_enabled) { floor_stop_on_slope = p_enabled; }
	bool is_floor_stop_on_slope_enabled() const { return floor_stop_on_slope; }
	void set_floor_snap_length(real_t p_length) { floor_snap_length = std::max(0.0f, p_length); }
	real_t get_floor_snap_length() const { return floor_snap_length; }

	bool move_and_slide();
	bool is_on_floor() const { return on_floor; }
	bool is_on_wall() const { return on_wall; }
	bool is_on_ceiling() const { return on_ceiling; }
	Vector3 get_floor_normal() const { return floor_normal; }
	Vector3 get_wall_normal() const { return wall_normal; }
	int get_slide_collision_count() const { return (int)slide_collisions.size(); }
	Dictionary get_slide_collision(int p_index) const;
	Dictionary get_last_slide_collision() const;

	CharacterBody3D();
};

class RigidBody3D : public PhysicsBody3D {
	GDCLASS(RigidBody3D, PhysicsBody3D)

	real_t mass = 1.0f;
	real_t gravity_scale = 1.0f;
	real_t bounce = 0.0f;
	real_t friction = 0.6f;
	real_t linear_damp = 0.0f;
	real_t angular_damp = 0.0f;
	bool lock_rotation = false;
	bool freeze = false;
	bool can_sleep = true;
	bool contact_monitor = false;
	std::vector<ObjectID> pending_contacts;

	void _update_mode();

protected:
	static void _bind_methods();
	virtual void _space_entered() override;

public:
	void set_mass(real_t p_mass);
	real_t get_mass() const { return mass; }
	void set_gravity_scale(real_t p_scale);
	real_t get_gravity_scale() const { return gravity_scale; }
	void set_bounce(real_t p_bounce);
	real_t get_bounce() const { return bounce; }
	void set_friction(real_t p_friction);
	real_t get_friction() const { return friction; }
	void set_linear_damp(real_t p_damp);
	real_t get_linear_damp() const { return linear_damp; }
	void set_angular_damp(real_t p_damp);
	real_t get_angular_damp() const { return angular_damp; }
	void set_lock_rotation(bool p_lock);
	bool is_lock_rotation_enabled() const { return lock_rotation; }
	void set_freeze(bool p_freeze);
	bool is_freeze_enabled() const { return freeze; }
	void set_can_sleep(bool p_enabled);
	bool is_able_to_sleep() const { return can_sleep; }
	bool is_sleeping() const;
	void wake_up();
	void set_contact_monitor(bool p_enabled);
	bool is_contact_monitor_enabled() const { return contact_monitor; }

	void set_linear_velocity(const Vector3 &p_velocity);
	Vector3 get_linear_velocity() const;
	void set_angular_velocity(const Vector3 &p_velocity);
	Vector3 get_angular_velocity() const;
	void apply_central_impulse(const Vector3 &p_impulse);
	void apply_impulse(const Vector3 &p_impulse, const Vector3 &p_offset);
	void apply_torque_impulse(const Vector3 &p_impulse);
	void apply_central_force(const Vector3 &p_force);

	virtual void _physics_post_step() override;

	RigidBody3D();
};

class Area3D : public CollisionObject3D {
	GDCLASS(Area3D, CollisionObject3D)

	struct Event {
		bool entered = false;
		ObjectID id = 0;
		bool is_area = false;
	};

	bool monitoring = true;
	bool monitorable = true;
	std::vector<Event> pending_events;

protected:
	static void _bind_methods();

public:
	void set_monitoring(bool p_enabled);
	bool is_monitoring() const { return monitoring; }
	void set_monitorable(bool p_enabled);
	bool is_monitorable() const { return monitorable; }

	Array get_overlapping_bodies() const;
	Array get_overlapping_areas() const;
	bool has_overlapping_bodies() const;
	bool has_overlapping_areas() const;
	bool overlaps_body(Node *p_body) const;
	bool overlaps_area(Node *p_area) const;

	virtual void _physics_post_step() override;

	Area3D();
};
