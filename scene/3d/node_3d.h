// Node3D : nœud 3D avec position, rotation (angles d'Euler) et échelle.
// Repère droit : X vers la droite, Y vers le haut, -Z vers l'avant (comme Godot).
#pragma once

#include "core/math/aabb.h"
#include "scene/main/node.h"
#include "servers/rendering_server_3d.h"

class Node3D : public Node {
	GDCLASS(Node3D, Node)

	Vector3 position;
	Vector3 rotation; // radians, ordre YXZ
	Vector3 scale = Vector3(1, 1, 1);
	bool visible = true;
	mutable Transform3D global_cache;
	mutable bool global_dirty = true;

	void _propagate_transform_changed();
	void _propagate_visibility_changed();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	enum {
		NOTIFICATION_TRANSFORM_CHANGED = 2000,
		NOTIFICATION_VISIBILITY_CHANGED_3D = 2001,
	};

	void set_position(const Vector3 &p_position);
	Vector3 get_position() const { return position; }
	void set_rotation(const Vector3 &p_radians);
	Vector3 get_rotation() const { return rotation; }
	void set_rotation_degrees(const Vector3 &p_degrees);
	Vector3 get_rotation_degrees() const;
	void set_scale(const Vector3 &p_scale);
	Vector3 get_scale() const { return scale; }

	Transform3D get_transform() const;
	void set_transform(const Transform3D &p_transform);
	Transform3D get_global_transform() const;
	void set_global_transform(const Transform3D &p_transform);
	void set_global_position(const Vector3 &p_position);
	Vector3 get_global_position() const { return get_global_transform().origin; }
	void set_global_rotation(const Vector3 &p_radians);
	Vector3 get_global_rotation() const;

	void translate(const Vector3 &p_offset); // dans le repère local
	void global_translate(const Vector3 &p_offset);
	void rotate(const Vector3 &p_axis, real_t p_angle); // axe exprimé dans le repère du parent
	void rotate_x(real_t p_angle) { rotate(Vector3(1, 0, 0), p_angle); }
	void rotate_y(real_t p_angle) { rotate(Vector3(0, 1, 0), p_angle); }
	void rotate_z(real_t p_angle) { rotate(Vector3(0, 0, 1), p_angle); }
	void rotate_object_local(const Vector3 &p_axis, real_t p_angle);
	void look_at(const Vector3 &p_target, const Vector3 &p_up = Vector3(0, 1, 0));
	Vector3 to_local(const Vector3 &p_global) const { return get_global_transform().affine_inverse().xform(p_global); }
	Vector3 to_global(const Vector3 &p_local) const { return get_global_transform().xform(p_local); }
	// Axes du nœud dans le monde (normalisés).
	Vector3 get_forward() const { return -get_global_transform().basis.get_column(2).normalized(); }
	Vector3 get_right() const { return get_global_transform().basis.get_column(0).normalized(); }
	Vector3 get_up() const { return get_global_transform().basis.get_column(1).normalized(); }

	void set_visible(bool p_visible);
	bool is_visible() const { return visible; }
	bool is_visible_in_tree() const;
	void show() { set_visible(true); }
	void hide() { set_visible(false); }

	// Boîte englobante locale utilisée par l'éditeur (sélection, cadrage). Vide par défaut.
	virtual bool _edit_get_aabb(AABB &r_aabb) const { return false; }
	// Lignes d'aide dessinées par l'éditeur (caméra, lumière, forme...), en coordonnées monde.
	virtual void _edit_get_gizmo_lines(std::vector<RenderingServer3D::Line> *r_lines) const {}
	// Lignes dessinées en jeu quand « formes de collision visibles » est actif.
	virtual void _get_debug_collision_lines(std::vector<RenderingServer3D::Line> *r_lines) const {}
};

// Simple repère visible dans l'éditeur (point d'apparition, etc.).
class Marker3D : public Node3D {
	GDCLASS(Marker3D, Node3D)

protected:
	static void _bind_methods() {}

public:
	virtual void _edit_get_gizmo_lines(std::vector<RenderingServer3D::Line> *r_lines) const override;
};
