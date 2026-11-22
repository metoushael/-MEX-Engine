#include "physics_body_3d.h"

#include "core/config/engine.h"

#include <cmath>
#include "scene/3d/collision_shape_3d.h"

#define PS PhysicsServer3D::get_singleton()

// ---------------------------------------------------------------------------
// CollisionObject3D
// ---------------------------------------------------------------------------
CollisionObject3D::CollisionObject3D(bool p_area) {
	is_area = p_area;
	rid = p_area ? PS->area_create() : PS->body_create();
	PS->collision_object_attach_instance_id(rid, get_instance_id());
}

CollisionObject3D::~CollisionObject3D() {
	if (PS) {
		PS->free_rid(rid);
	}
}

void CollisionObject3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (get_tree()->is_edit_mode()) {
				return; // pas de simulation dans l'éditeur
			}
			PS->collision_object_set_space(rid, get_tree()->get_space_3d());
			get_tree()->add_physics_listener(this);
			in_space = true;
			has_pushed = false;
			has_pulled = false;
			_space_entered();
		} break;
		case NOTIFICATION_READY: {
			if (in_space) {
				_sync_shapes();
				_push_transform();
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (in_space) {
				PS->collision_object_set_space(rid, RID());
				get_tree()->remove_physics_listener(this);
				in_space = false;
			}
		} break;
	}
}

// Le serveur ne manipule que des transformations rigides (rotation + translation) :
// l'échelle des nœuds est appliquée aux dimensions des formes.
static Transform3D _rigid_part(const Transform3D &p_global) {
	return Transform3D(p_global.basis.orthonormalized(), p_global.origin);
}

void CollisionObject3D::_sync_shapes() {
	const Transform3D global = get_global_transform();
	const Transform3D to_body = _rigid_part(global).affine_inverse() * global;

	std::vector<ShapeEntry> entries;
	std::vector<Ref<Shape3D>> shapes;
	for (Node *child : get_children_vector()) {
		CollisionShape3D *cs = Object::cast_to<CollisionShape3D>(child);
		if (!cs || cs->get_shape().is_null()) {
			continue;
		}
		const Transform3D local = to_body * cs->get_transform();
		ShapeEntry e;
		e.node = cs->get_instance_id();
		e.shape = cs->get_shape().ptr();
		e.version = cs->get_shape()->get_version();
		e.xform = _rigid_part(local);
		e.scale = local.basis.get_scale();
		e.disabled = cs->is_disabled();
		entries.push_back(e);
		shapes.push_back(cs->get_shape());
	}
	if (entries == shape_cache) {
		return;
	}
	shape_cache = entries;
	PS->collision_object_clear_shapes(rid);
	for (size_t i = 0; i < entries.size(); i++) {
		PhysicsServer3D::ShapeData sd = shapes[i]->get_physics_shape();
		const Vector3 s = entries[i].scale;
		switch (sd.type) {
			case PhysicsServer3D::SHAPE_SPHERE: {
				sd.radius *= std::max(s.x, std::max(s.y, s.z));
			} break;
			case PhysicsServer3D::SHAPE_BOX: {
				sd.half_extents = Vector3(sd.half_extents.x * s.x, sd.half_extents.y * s.y, sd.half_extents.z * s.z);
			} break;
			case PhysicsServer3D::SHAPE_CAPSULE: {
				sd.radius *= std::max(s.x, s.z);
				sd.height = std::max(sd.height * s.y, sd.radius * 2.0f);
			} break;
		}
		PS->collision_object_add_shape(rid, sd, entries[i].xform, entries[i].disabled);
	}
}

bool CollisionObject3D::_push_transform() {
	const Transform3D rigid = _rigid_part(get_global_transform());
	if (has_pushed && rigid == last_pushed) {
		return false;
	}
	last_pushed = rigid;
	has_pushed = true;
	has_pulled = false;
	PS->collision_object_set_transform(rid, rigid);
	return true;
}

void CollisionObject3D::_pull_transform() {
	const Transform3D t = PS->collision_object_get_transform(rid);
	if (has_pulled && t == last_pulled) {
		return; // corps immobile ou endormi : rien à mettre à jour
	}
	last_pulled = t;
	has_pulled = true;
	const Transform3D global = get_global_transform();
	if (t.basis == _rigid_part(global).basis) {
		set_global_position(t.origin);
	} else {
		set_global_transform(Transform3D(t.basis.scaled_local(global.basis.get_scale()), t.origin));
	}
	// Mémorise la transformation telle que _push_transform() la recalculera.
	last_pushed = _rigid_part(get_global_transform());
	has_pushed = true;
}

void CollisionObject3D::_physics_pre_step() {
	_sync_shapes();
	_push_transform();
}

void CollisionObject3D::set_collision_layer(int p_layer) {
	collision_layer = (uint32_t)p_layer;
	PS->collision_object_set_collision_layer(rid, collision_layer);
}

void CollisionObject3D::set_collision_mask(int p_mask) {
	collision_mask = (uint32_t)p_mask;
	PS->collision_object_set_collision_mask(rid, collision_mask);
}

void CollisionObject3D::set_collision_layer_value(int p_layer_number, bool p_value) {
	ERR_FAIL_COND_MSG(p_layer_number < 1 || p_layer_number > 32, "Le numéro de couche doit être compris entre 1 et 32.");
	uint32_t bit = 1u << (p_layer_number - 1);
	set_collision_layer((int)(p_value ? (collision_layer | bit) : (collision_layer & ~bit)));
}

bool CollisionObject3D::get_collision_layer_value(int p_layer_number) const {
	ERR_FAIL_COND_V_MSG(p_layer_number < 1 || p_layer_number > 32, false, "Le numéro de couche doit être compris entre 1 et 32.");
	return collision_layer & (1u << (p_layer_number - 1));
}

void CollisionObject3D::set_collision_mask_value(int p_layer_number, bool p_value) {
	ERR_FAIL_COND_MSG(p_layer_number < 1 || p_layer_number > 32, "Le numéro de couche doit être compris entre 1 et 32.");
	uint32_t bit = 1u << (p_layer_number - 1);
	set_collision_mask((int)(p_value ? (collision_mask | bit) : (collision_mask & ~bit)));
}

bool CollisionObject3D::get_collision_mask_value(int p_layer_number) const {
	ERR_FAIL_COND_V_MSG(p_layer_number < 1 || p_layer_number > 32, false, "Le numéro de couche doit être compris entre 1 et 32.");
	return collision_mask & (1u << (p_layer_number - 1));
}

void CollisionObject3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_collision_layer", "layer"), &CollisionObject3D::set_collision_layer);
	ClassDB::bind_method(D_METHOD("get_collision_layer"), &CollisionObject3D::get_collision_layer);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &CollisionObject3D::set_collision_mask);
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &CollisionObject3D::get_collision_mask);
	ClassDB::bind_method(D_METHOD("set_collision_layer_value", "layer_number", "value"), &CollisionObject3D::set_collision_layer_value);
	ClassDB::bind_method(D_METHOD("get_collision_layer_value", "layer_number"), &CollisionObject3D::get_collision_layer_value);
	ClassDB::bind_method(D_METHOD("set_collision_mask_value", "layer_number", "value"), &CollisionObject3D::set_collision_mask_value);
	ClassDB::bind_method(D_METHOD("get_collision_mask_value", "layer_number"), &CollisionObject3D::get_collision_mask_value);

	ADD_GROUP("Collision");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_layer", PROPERTY_HINT_RANGE, "0,65535,1"), "set_collision_layer", "get_collision_layer");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_RANGE, "0,65535,1"), "set_collision_mask", "get_collision_mask");
}

// ---------------------------------------------------------------------------
// PhysicsBody3D
// ---------------------------------------------------------------------------
static Dictionary _motion_to_dictionary(const PhysicsServer3D::MotionResult &p_result) {
	Dictionary d;
	d[Variant("position")] = Variant(p_result.position);
	d[Variant("normal")] = Variant(p_result.normal);
	d[Variant("travel")] = Variant(p_result.travel);
	d[Variant("remainder")] = Variant(p_result.remainder);
	d[Variant("depth")] = Variant(p_result.depth);
	d[Variant("collider")] = Variant(ObjectDB::get_instance(p_result.collider_id));
	d[Variant("collider_velocity")] = Variant(p_result.collider_velocity);
	return d;
}

Dictionary PhysicsBody3D::move_and_collide(const Vector3 &p_motion, bool p_test_only) {
	if (!_is_in_space()) {
		if (!p_test_only) {
			set_global_position(get_global_position() + p_motion);
		}
		return Dictionary();
	}
	_sync_shapes();
	_push_transform();
	PhysicsServer3D::MotionResult result;
	bool hit = PS->body_test_motion(get_rid(), p_motion, result, p_test_only);
	if (!p_test_only) {
		_pull_transform();
	}
	return hit ? _motion_to_dictionary(result) : Dictionary();
}

bool PhysicsBody3D::test_move(const Vector3 &p_motion) {
	if (!_is_in_space()) {
		return false;
	}
	_sync_shapes();
	_push_transform();
	PhysicsServer3D::MotionResult result;
	return PS->body_test_motion(get_rid(), p_motion, result, true);
}

void PhysicsBody3D::add_collision_exception_with(Node *p_node) {
	CollisionObject3D *other = Object::cast_to<CollisionObject3D>(p_node);
	ERR_FAIL_COND_MSG(!other, "add_collision_exception_with() : le nœud doit être un corps physique.");
	PS->body_add_collision_exception(get_rid(), other->get_rid());
}

void PhysicsBody3D::remove_collision_exception_with(Node *p_node) {
	CollisionObject3D *other = Object::cast_to<CollisionObject3D>(p_node);
	ERR_FAIL_COND_MSG(!other, "remove_collision_exception_with() : le nœud doit être un corps physique.");
	PS->body_remove_collision_exception(get_rid(), other->get_rid());
}

void PhysicsBody3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("move_and_collide", "motion", "test_only"), &PhysicsBody3D::move_and_collide, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("test_move", "motion"), &PhysicsBody3D::test_move);
	ClassDB::bind_method(D_METHOD("add_collision_exception_with", "body"), &PhysicsBody3D::add_collision_exception_with);
	ClassDB::bind_method(D_METHOD("remove_collision_exception_with", "body"), &PhysicsBody3D::remove_collision_exception_with);
}

// ---------------------------------------------------------------------------
// StaticBody3D
// ---------------------------------------------------------------------------
StaticBody3D::StaticBody3D() {
	PS->body_set_mode(get_rid(), PhysicsServer3D::BODY_MODE_STATIC);
}

void StaticBody3D::set_friction(real_t p_friction) {
	friction = p_friction;
	PS->body_set_friction(get_rid(), friction);
}

void StaticBody3D::set_bounce(real_t p_bounce) {
	bounce = p_bounce;
	PS->body_set_bounce(get_rid(), bounce);
}

void StaticBody3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_friction", "friction"), &StaticBody3D::set_friction);
	ClassDB::bind_method(D_METHOD("get_friction"), &StaticBody3D::get_friction);
	ClassDB::bind_method(D_METHOD("set_bounce", "bounce"), &StaticBody3D::set_bounce);
	ClassDB::bind_method(D_METHOD("get_bounce"), &StaticBody3D::get_bounce);

	ADD_GROUP("Physics Material");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "friction", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_friction", "get_friction");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bounce", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_bounce", "get_bounce");
}

// ---------------------------------------------------------------------------
// CharacterBody3D
// ---------------------------------------------------------------------------
CharacterBody3D::CharacterBody3D() {
	PS->body_set_mode(get_rid(), PhysicsServer3D::BODY_MODE_KINEMATIC);
}

void CharacterBody3D::set_up_direction(const Vector3 &p_up) {
	ERR_FAIL_COND_MSG(p_up.is_zero_approx(), "up_direction ne peut pas être nul.");
	up_direction = p_up.normalized();
}

void CharacterBody3D::_classify(const Vector3 &p_normal) {
	const real_t margin = 0.01f;
	if (std::acos(Math::clamp(p_normal.dot(up_direction), -1.0f, 1.0f)) <= floor_max_angle + margin) {
		on_floor = true;
		floor_normal = p_normal;
	} else if (std::acos(Math::clamp(p_normal.dot(-up_direction), -1.0f, 1.0f)) <= floor_max_angle + margin) {
		on_ceiling = true;
	} else {
		on_wall = true;
		wall_normal = p_normal;
	}
}

bool CharacterBody3D::move_and_slide() {
	Engine *engine = Engine::get_singleton();
	const double delta = engine->is_in_physics_frame() ? get_physics_process_delta_time() : get_process_delta_time();

	const bool was_on_floor = on_floor;
	on_floor = on_wall = on_ceiling = false;
	floor_normal = Vector3();
	wall_normal = Vector3();
	slide_collisions.clear();

	if (!_is_in_space()) {
		set_global_position(get_global_position() + velocity * (real_t)delta);
		return false;
	}
	_sync_shapes();
	_push_transform();

	const Vector3 start_velocity = velocity;
	const Vector3 lateral = start_velocity - up_direction * start_velocity.dot(up_direction);
	Vector3 motion = velocity * (real_t)delta;

	for (int i = 0; i < max_slides && !motion.is_zero_approx(); i++) {
		PhysicsServer3D::MotionResult result;
		if (!PS->body_test_motion(get_rid(), motion, result, false)) {
			break;
		}
		slide_collisions.push_back(result);
		const bool floor_before = on_floor;
		_classify(result.normal);
		const bool hit_floor = on_floor && !floor_before;

		if (hit_floor || (on_floor && result.normal == floor_normal)) {
			// Sol : on annule la chute, la vitesse latérale est conservée.
			real_t along_up = velocity.dot(up_direction);
			if (along_up < 0) {
				velocity -= up_direction * along_up;
			}
			if (floor_stop_on_slope && lateral.length() < 0.001f) {
				motion = Vector3(); // immobile sur une pente : ne glisse pas
			} else {
				motion = result.remainder.slide(result.normal);
			}
		} else {
			if (velocity.dot(result.normal) < 0) {
				velocity = velocity.slide(result.normal);
			}
			motion = result.remainder.slide(result.normal);
		}
	}

	const bool moving_up = velocity.dot(up_direction) > 0.01f;
	if (!on_floor && !moving_up) {
		// Accroche au sol (descente de pente) si on y était à l'image précédente.
		if (was_on_floor && floor_snap_length > 0) {
			PhysicsServer3D::MotionResult probe;
			if (PS->body_test_motion(get_rid(), -up_direction * floor_snap_length, probe, true)) {
				if (std::acos(Math::clamp(probe.normal.dot(up_direction), -1.0f, 1.0f)) <= floor_max_angle + 0.01f) {
					PS->body_test_motion(get_rid(), -up_direction * floor_snap_length, probe, false);
					on_floor = true;
					floor_normal = probe.normal;
				}
			}
		}
		// Sonde de contact : détecte le sol même sans mouvement vers lui.
		if (!on_floor) {
			PhysicsServer3D::MotionResult probe;
			if (PS->body_test_motion(get_rid(), -up_direction * 0.02f, probe, true)) {
				if (std::acos(Math::clamp(probe.normal.dot(up_direction), -1.0f, 1.0f)) <= floor_max_angle + 0.01f) {
					on_floor = true;
					floor_normal = probe.normal;
				}
			}
		}
	}

	_pull_transform();
	return !slide_collisions.empty();
}

Dictionary CharacterBody3D::get_slide_collision(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, slide_collisions.size(), Dictionary());
	return _motion_to_dictionary(slide_collisions[p_index]);
}

Dictionary CharacterBody3D::get_last_slide_collision() const {
	if (slide_collisions.empty()) {
		return Dictionary();
	}
	return _motion_to_dictionary(slide_collisions.back());
}

void CharacterBody3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_velocity", "velocity"), &CharacterBody3D::set_velocity);
	ClassDB::bind_method(D_METHOD("get_velocity"), &CharacterBody3D::get_velocity);
	ClassDB::bind_method(D_METHOD("set_up_direction", "up"), &CharacterBody3D::set_up_direction);
	ClassDB::bind_method(D_METHOD("get_up_direction"), &CharacterBody3D::get_up_direction);
	ClassDB::bind_method(D_METHOD("set_floor_max_angle", "radians"), &CharacterBody3D::set_floor_max_angle);
	ClassDB::bind_method(D_METHOD("get_floor_max_angle"), &CharacterBody3D::get_floor_max_angle);
	ClassDB::bind_method(D_METHOD("set_floor_stop_on_slope", "enabled"), &CharacterBody3D::set_floor_stop_on_slope);
	ClassDB::bind_method(D_METHOD("is_floor_stop_on_slope_enabled"), &CharacterBody3D::is_floor_stop_on_slope_enabled);
	ClassDB::bind_method(D_METHOD("set_floor_snap_length", "length"), &CharacterBody3D::set_floor_snap_length);
	ClassDB::bind_method(D_METHOD("get_floor_snap_length"), &CharacterBody3D::get_floor_snap_length);
	ClassDB::bind_method(D_METHOD("move_and_slide"), &CharacterBody3D::move_and_slide);
	ClassDB::bind_method(D_METHOD("is_on_floor"), &CharacterBody3D::is_on_floor);
	ClassDB::bind_method(D_METHOD("is_on_wall"), &CharacterBody3D::is_on_wall);
	ClassDB::bind_method(D_METHOD("is_on_ceiling"), &CharacterBody3D::is_on_ceiling);
	ClassDB::bind_method(D_METHOD("get_floor_normal"), &CharacterBody3D::get_floor_normal);
	ClassDB::bind_method(D_METHOD("get_wall_normal"), &CharacterBody3D::get_wall_normal);
	ClassDB::bind_method(D_METHOD("get_slide_collision_count"), &CharacterBody3D::get_slide_collision_count);
	ClassDB::bind_method(D_METHOD("get_slide_collision", "index"), &CharacterBody3D::get_slide_collision);
	ClassDB::bind_method(D_METHOD("get_last_slide_collision"), &CharacterBody3D::get_last_slide_collision);

	ADD_GROUP("Character");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "velocity", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_velocity", "get_velocity");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "up_direction"), "set_up_direction", "get_up_direction");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "floor_max_angle", PROPERTY_HINT_RADIANS), "set_floor_max_angle", "get_floor_max_angle");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "floor_stop_on_slope"), "set_floor_stop_on_slope", "is_floor_stop_on_slope_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "floor_snap_length", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_floor_snap_length", "get_floor_snap_length");
}

// ---------------------------------------------------------------------------
// RigidBody3D
// ---------------------------------------------------------------------------
RigidBody3D::RigidBody3D() {
	PS->body_set_mode(get_rid(), PhysicsServer3D::BODY_MODE_RIGID);
}

void RigidBody3D::_space_entered() {
	_update_mode();
}

void RigidBody3D::_update_mode() {
	PS->body_set_mode(get_rid(), freeze ? PhysicsServer3D::BODY_MODE_STATIC : PhysicsServer3D::BODY_MODE_RIGID);
}

void RigidBody3D::set_mass(real_t p_mass) {
	ERR_FAIL_COND_MSG(p_mass <= 0, "La masse doit être strictement positive.");
	mass = p_mass;
	PS->body_set_mass(get_rid(), mass);
}

void RigidBody3D::set_gravity_scale(real_t p_scale) {
	gravity_scale = p_scale;
	PS->body_set_gravity_scale(get_rid(), gravity_scale);
}

void RigidBody3D::set_bounce(real_t p_bounce) {
	bounce = p_bounce;
	PS->body_set_bounce(get_rid(), bounce);
}

void RigidBody3D::set_friction(real_t p_friction) {
	friction = p_friction;
	PS->body_set_friction(get_rid(), friction);
}

void RigidBody3D::set_linear_damp(real_t p_damp) {
	linear_damp = p_damp;
	PS->body_set_linear_damp(get_rid(), linear_damp);
}

void RigidBody3D::set_angular_damp(real_t p_damp) {
	angular_damp = p_damp;
	PS->body_set_angular_damp(get_rid(), angular_damp);
}

void RigidBody3D::set_lock_rotation(bool p_lock) {
	lock_rotation = p_lock;
	PS->body_set_lock_rotation(get_rid(), lock_rotation);
}

void RigidBody3D::set_freeze(bool p_freeze) {
	freeze = p_freeze;
	_update_mode();
	if (freeze) {
		PS->body_set_linear_velocity(get_rid(), Vector3());
		PS->body_set_angular_velocity(get_rid(), Vector3());
	}
}

void RigidBody3D::set_can_sleep(bool p_enabled) {
	can_sleep = p_enabled;
	PS->body_set_can_sleep(get_rid(), can_sleep);
}

bool RigidBody3D::is_sleeping() const {
	return PS->body_is_sleeping(get_rid());
}

void RigidBody3D::wake_up() {
	PS->body_wake_up(get_rid());
}

void RigidBody3D::set_contact_monitor(bool p_enabled) {
	contact_monitor = p_enabled;
	if (contact_monitor) {
		PS->body_set_contact_callback(get_rid(), [this](RID p_other, ObjectID p_other_id) {
			pending_contacts.push_back(p_other_id);
		});
	} else {
		PS->body_set_contact_callback(get_rid(), PhysicsServer3D::BodyContactCallback());
		pending_contacts.clear();
	}
}

void RigidBody3D::set_linear_velocity(const Vector3 &p_velocity) {
	PS->body_set_linear_velocity(get_rid(), p_velocity);
}

Vector3 RigidBody3D::get_linear_velocity() const {
	return PS->body_get_linear_velocity(get_rid());
}

void RigidBody3D::set_angular_velocity(const Vector3 &p_velocity) {
	PS->body_set_angular_velocity(get_rid(), p_velocity);
}

Vector3 RigidBody3D::get_angular_velocity() const {
	return PS->body_get_angular_velocity(get_rid());
}

void RigidBody3D::apply_central_impulse(const Vector3 &p_impulse) {
	PS->body_apply_central_impulse(get_rid(), p_impulse);
}

void RigidBody3D::apply_impulse(const Vector3 &p_impulse, const Vector3 &p_offset) {
	PS->body_apply_impulse(get_rid(), p_impulse, p_offset);
}

void RigidBody3D::apply_torque_impulse(const Vector3 &p_impulse) {
	PS->body_apply_torque_impulse(get_rid(), p_impulse);
}

void RigidBody3D::apply_central_force(const Vector3 &p_force) {
	PS->body_apply_central_force(get_rid(), p_force);
}

void RigidBody3D::_physics_post_step() {
	if (!freeze) {
		_pull_transform();
	}
	if (!pending_contacts.empty()) {
		std::vector<ObjectID> contacts;
		contacts.swap(pending_contacts);
		const ObjectID self_id = get_instance_id();
		for (ObjectID id : contacts) {
			Object *other = ObjectDB::get_instance(id);
			if (other) {
				emit_signal("body_entered", other);
			}
			if (!ObjectDB::get_instance(self_id)) {
				return;
			}
		}
	}
}

void RigidBody3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_mass", "mass"), &RigidBody3D::set_mass);
	ClassDB::bind_method(D_METHOD("get_mass"), &RigidBody3D::get_mass);
	ClassDB::bind_method(D_METHOD("set_gravity_scale", "scale"), &RigidBody3D::set_gravity_scale);
	ClassDB::bind_method(D_METHOD("get_gravity_scale"), &RigidBody3D::get_gravity_scale);
	ClassDB::bind_method(D_METHOD("set_bounce", "bounce"), &RigidBody3D::set_bounce);
	ClassDB::bind_method(D_METHOD("get_bounce"), &RigidBody3D::get_bounce);
	ClassDB::bind_method(D_METHOD("set_friction", "friction"), &RigidBody3D::set_friction);
	ClassDB::bind_method(D_METHOD("get_friction"), &RigidBody3D::get_friction);
	ClassDB::bind_method(D_METHOD("set_linear_damp", "damp"), &RigidBody3D::set_linear_damp);
	ClassDB::bind_method(D_METHOD("get_linear_damp"), &RigidBody3D::get_linear_damp);
	ClassDB::bind_method(D_METHOD("set_angular_damp", "damp"), &RigidBody3D::set_angular_damp);
	ClassDB::bind_method(D_METHOD("get_angular_damp"), &RigidBody3D::get_angular_damp);
	ClassDB::bind_method(D_METHOD("set_lock_rotation", "lock"), &RigidBody3D::set_lock_rotation);
	ClassDB::bind_method(D_METHOD("is_lock_rotation_enabled"), &RigidBody3D::is_lock_rotation_enabled);
	ClassDB::bind_method(D_METHOD("set_freeze", "freeze"), &RigidBody3D::set_freeze);
	ClassDB::bind_method(D_METHOD("is_freeze_enabled"), &RigidBody3D::is_freeze_enabled);
	ClassDB::bind_method(D_METHOD("set_can_sleep", "enabled"), &RigidBody3D::set_can_sleep);
	ClassDB::bind_method(D_METHOD("is_able_to_sleep"), &RigidBody3D::is_able_to_sleep);
	ClassDB::bind_method(D_METHOD("is_sleeping"), &RigidBody3D::is_sleeping);
	ClassDB::bind_method(D_METHOD("wake_up"), &RigidBody3D::wake_up);
	ClassDB::bind_method(D_METHOD("set_contact_monitor", "enabled"), &RigidBody3D::set_contact_monitor);
	ClassDB::bind_method(D_METHOD("is_contact_monitor_enabled"), &RigidBody3D::is_contact_monitor_enabled);
	ClassDB::bind_method(D_METHOD("set_linear_velocity", "velocity"), &RigidBody3D::set_linear_velocity);
	ClassDB::bind_method(D_METHOD("get_linear_velocity"), &RigidBody3D::get_linear_velocity);
	ClassDB::bind_method(D_METHOD("set_angular_velocity", "velocity"), &RigidBody3D::set_angular_velocity);
	ClassDB::bind_method(D_METHOD("get_angular_velocity"), &RigidBody3D::get_angular_velocity);
	ClassDB::bind_method(D_METHOD("apply_central_impulse", "impulse"), &RigidBody3D::apply_central_impulse);
	ClassDB::bind_method(D_METHOD("apply_impulse", "impulse", "offset"), &RigidBody3D::apply_impulse, DEFVAL(Vector3()));
	ClassDB::bind_method(D_METHOD("apply_torque_impulse", "impulse"), &RigidBody3D::apply_torque_impulse);
	ClassDB::bind_method(D_METHOD("apply_central_force", "force"), &RigidBody3D::apply_central_force);

	ADD_GROUP("Rigid Body");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mass", PROPERTY_HINT_RANGE, "0.01,1000,0.01"), "set_mass", "get_mass");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity_scale", PROPERTY_HINT_RANGE, "-8,8,0.01"), "set_gravity_scale", "get_gravity_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bounce", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_bounce", "get_bounce");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "friction", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_friction", "get_friction");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "linear_damp", PROPERTY_HINT_RANGE, "0,100,0.01"), "set_linear_damp", "get_linear_damp");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "angular_damp", PROPERTY_HINT_RANGE, "0,100,0.01"), "set_angular_damp", "get_angular_damp");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "lock_rotation"), "set_lock_rotation", "is_lock_rotation_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "freeze"), "set_freeze", "is_freeze_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "can_sleep"), "set_can_sleep", "is_able_to_sleep");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "contact_monitor"), "set_contact_monitor", "is_contact_monitor_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "linear_velocity"), "set_linear_velocity", "get_linear_velocity");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "angular_velocity"), "set_angular_velocity", "get_angular_velocity");

	ADD_SIGNAL(MethodInfo("body_entered", PropertyInfo(Variant::OBJECT, "body")));
}

// ---------------------------------------------------------------------------
// Area3D
// ---------------------------------------------------------------------------
Area3D::Area3D() :
		CollisionObject3D(true) {
	// Les événements sont mis en file puis émis après le pas de simulation.
	PS->area_set_monitor_callback(get_rid(), [this](PhysicsServer3D::AreaBodyStatus p_status, RID p_other, ObjectID p_other_id, bool p_is_area) {
		Event e;
		e.entered = p_status == PhysicsServer3D::AREA_BODY_ADDED;
		e.id = p_other_id;
		e.is_area = p_is_area;
		pending_events.push_back(e);
	});
}

void Area3D::set_monitoring(bool p_enabled) {
	monitoring = p_enabled;
	PS->area_set_monitoring(get_rid(), monitoring);
}

void Area3D::set_monitorable(bool p_enabled) {
	monitorable = p_enabled;
	PS->area_set_monitorable(get_rid(), monitorable);
}

void Area3D::_physics_post_step() {
	if (pending_events.empty()) {
		return;
	}
	std::vector<Event> events;
	events.swap(pending_events);
	const ObjectID self_id = get_instance_id();
	for (const Event &e : events) {
		Object *other = ObjectDB::get_instance(e.id);
		if (!other) {
			continue;
		}
		if (e.is_area) {
			emit_signal(e.entered ? "area_entered" : "area_exited", other);
		} else {
			emit_signal(e.entered ? "body_entered" : "body_exited", other);
		}
		if (!ObjectDB::get_instance(self_id)) {
			return;
		}
	}
}

Array Area3D::get_overlapping_bodies() const {
	std::vector<ObjectID> bodies, areas;
	PS->area_get_overlaps(get_rid(), &bodies, &areas);
	Array out;
	for (ObjectID id : bodies) {
		if (Object *obj = ObjectDB::get_instance(id)) {
			out.push_back(Variant(obj));
		}
	}
	return out;
}

Array Area3D::get_overlapping_areas() const {
	std::vector<ObjectID> bodies, areas;
	PS->area_get_overlaps(get_rid(), &bodies, &areas);
	Array out;
	for (ObjectID id : areas) {
		if (Object *obj = ObjectDB::get_instance(id)) {
			out.push_back(Variant(obj));
		}
	}
	return out;
}

bool Area3D::has_overlapping_bodies() const {
	return !get_overlapping_bodies().is_empty();
}

bool Area3D::has_overlapping_areas() const {
	return !get_overlapping_areas().is_empty();
}

bool Area3D::overlaps_body(Node *p_body) const {
	return p_body && get_overlapping_bodies().has(Variant(p_body));
}

bool Area3D::overlaps_area(Node *p_area) const {
	return p_area && get_overlapping_areas().has(Variant(p_area));
}

void Area3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_monitoring", "enabled"), &Area3D::set_monitoring);
	ClassDB::bind_method(D_METHOD("is_monitoring"), &Area3D::is_monitoring);
	ClassDB::bind_method(D_METHOD("set_monitorable", "enabled"), &Area3D::set_monitorable);
	ClassDB::bind_method(D_METHOD("is_monitorable"), &Area3D::is_monitorable);
	ClassDB::bind_method(D_METHOD("get_overlapping_bodies"), &Area3D::get_overlapping_bodies);
	ClassDB::bind_method(D_METHOD("get_overlapping_areas"), &Area3D::get_overlapping_areas);
	ClassDB::bind_method(D_METHOD("has_overlapping_bodies"), &Area3D::has_overlapping_bodies);
	ClassDB::bind_method(D_METHOD("has_overlapping_areas"), &Area3D::has_overlapping_areas);
	ClassDB::bind_method(D_METHOD("overlaps_body", "body"), &Area3D::overlaps_body);
	ClassDB::bind_method(D_METHOD("overlaps_area", "area"), &Area3D::overlaps_area);

	ADD_GROUP("Area");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "monitoring"), "set_monitoring", "is_monitoring");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "monitorable"), "set_monitorable", "is_monitorable");

	ADD_SIGNAL(MethodInfo("body_entered", PropertyInfo(Variant::OBJECT, "body")));
	ADD_SIGNAL(MethodInfo("body_exited", PropertyInfo(Variant::OBJECT, "body")));
	ADD_SIGNAL(MethodInfo("area_entered", PropertyInfo(Variant::OBJECT, "area")));
	ADD_SIGNAL(MethodInfo("area_exited", PropertyInfo(Variant::OBJECT, "area")));
}
