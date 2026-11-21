// Nœuds de physique 2D : StaticBody2D, CharacterBody2D, RigidBody2D et Area2D.
#pragma once

#include "scene/2d/node_2d.h"
#include "scene/main/scene_tree.h"
#include "servers/physics_server_2d.h"

class CollisionObject2D : public Node2D, public PhysicsSyncListener {
	GDCLASS(CollisionObject2D, Node2D)

	struct ShapeEntry {
		ObjectID node = 0;
		const void *shape = nullptr;
		uint32_t version = 0;
		Transform2D xform;
		bool disabled = false;
		bool operator==(const ShapeEntry &o) const {
			return node == o.node && shape == o.shape && version == o.version && xform == o.xform && disabled == o.disabled;
		}
	};

	RID rid;
	bool is_area = false;
	uint32_t collision_layer = 1;
	uint32_t collision_mask = 1;
	std::vector<ShapeEntry> shape_cache;
	bool in_space = false;

protected:
	Transform2D last_pushed; // dernière transformation envoyée au serveur ou lue depuis lui
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

	CollisionObject2D(bool p_area = false);
	~CollisionObject2D();
};

class PhysicsBody2D : public CollisionObject2D {
	GDCLASS(PhysicsBody2D, CollisionObject2D)

protected:
	static void _bind_methods();

public:
	// Déplace le corps ; retourne un dictionnaire décrivant la collision (vide si aucune).
	Dictionary move_and_collide(const Vector2 &p_motion, bool p_test_only = false);
	bool test_move(const Vector2 &p_motion);
	void add_collision_exception_with(Node *p_node);
	void remove_collision_exception_with(Node *p_node);
};

class StaticBody2D : public PhysicsBody2D {
	GDCLASS(StaticBody2D, PhysicsBody2D)

	real_t friction = 1.0f;
	real_t bounce = 0.0f;

protected:
	static void _bind_methods();

public:
	void set_friction(real_t p_friction);
	real_t get_friction() const { return friction; }
	void set_bounce(real_t p_bounce);
	real_t get_bounce() const { return bounce; }

	StaticBody2D();
};

class CharacterBody2D : public PhysicsBody2D {
	GDCLASS(CharacterBody2D, PhysicsBody2D)

	Vector2 velocity;
	Vector2 up_direction = Vector2(0, -1);
	real_t floor_max_angle = 0.785398f; // 45°
	bool floor_stop_on_slope = true;
	real_t floor_snap_length = 4.0f;
	int max_slides = 4;

	bool on_floor = false;
	bool on_wall = false;
	bool on_ceiling = false;
	Vector2 floor_normal;
	Vector2 wall_normal;
	std::vector<PhysicsServer2D::MotionResult> slide_collisions;

	void _classify(const Vector2 &p_normal);

protected:
	static void _bind_methods();

public:
	void set_velocity(const Vector2 &p_velocity) { velocity = p_velocity; }
	Vector2 get_velocity() const { return velocity; }
	void set_up_direction(const Vector2 &p_up);
	Vector2 get_up_direction() const { return up_direction; }
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
	Vector2 get_floor_normal() const { return floor_normal; }
	Vector2 get_wall_normal() const { return wall_normal; }
	int get_slide_collision_count() const { return (int)slide_collisions.size(); }
	Dictionary get_slide_collision(int p_index) const;
	Dictionary get_last_slide_collision() const;

	CharacterBody2D();
};

class RigidBody2D : public PhysicsBody2D {
	GDCLASS(RigidBody2D, PhysicsBody2D)

	real_t mass = 1.0f;
	real_t gravity_scale = 1.0f;
	real_t bounce = 0.0f;
	real_t friction = 1.0f;
	real_t linear_damp = 0.0f;
	real_t angular_damp = 0.0f;
	bool lock_rotation = false;
	bool freeze = false;
	bool contact_monitor = false;
	Vector2 initial_linear_velocity;
	real_t initial_angular_velocity = 0;
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
	void set_contact_monitor(bool p_enabled);
	bool is_contact_monitor_enabled() const { return contact_monitor; }

	void set_linear_velocity(const Vector2 &p_velocity);
	Vector2 get_linear_velocity() const;
	void set_angular_velocity(real_t p_velocity);
	real_t get_angular_velocity() const;
	void apply_central_impulse(const Vector2 &p_impulse);
	void apply_impulse(const Vector2 &p_impulse, const Vector2 &p_offset);
	void apply_torque_impulse(real_t p_impulse);
	void apply_central_force(const Vector2 &p_force);

	virtual void _physics_post_step() override;

	RigidBody2D();
};

class Area2D : public CollisionObject2D {
	GDCLASS(Area2D, CollisionObject2D)

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

	Area2D();
};
