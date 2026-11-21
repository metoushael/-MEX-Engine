// Shape3D : formes de collision 3D (ressources partagées entre CollisionShape3D).
#pragma once

#include "core/io/resource.h"
#include "servers/physics_server_3d.h"
#include "servers/rendering_server_3d.h"

class Shape3D : public Resource {
	GDCLASS(Shape3D, Resource)

	uint32_t version = 1;

protected:
	static void _bind_methods() {}
	void _shape_changed() {
		version++;
		emit_changed();
	}

public:
	uint32_t get_version() const { return version; }
	virtual PhysicsServer3D::ShapeData get_physics_shape() const { return PhysicsServer3D::ShapeData(); }
	// Représentation filaire (éditeur, débogage), dans le repère de la forme.
	virtual void get_debug_lines(std::vector<RenderingServer3D::Line> *r_lines, const Transform3D &p_xform, const Color &p_color) const {}
	virtual AABB get_aabb() const { return AABB(); }
};

class SphereShape3D : public Shape3D {
	GDCLASS(SphereShape3D, Shape3D)

	real_t radius = 0.5f;

protected:
	static void _bind_methods();

public:
	void set_radius(real_t p_radius);
	real_t get_radius() const { return radius; }
	virtual PhysicsServer3D::ShapeData get_physics_shape() const override;
	virtual void get_debug_lines(std::vector<RenderingServer3D::Line> *r_lines, const Transform3D &p_xform, const Color &p_color) const override;
	virtual AABB get_aabb() const override { return AABB(Vector3(-radius, -radius, -radius), Vector3(radius, radius, radius) * 2.0f); }
};

class BoxShape3D : public Shape3D {
	GDCLASS(BoxShape3D, Shape3D)

	Vector3 size = Vector3(1, 1, 1);

protected:
	static void _bind_methods();

public:
	void set_size(const Vector3 &p_size);
	Vector3 get_size() const { return size; }
	virtual PhysicsServer3D::ShapeData get_physics_shape() const override;
	virtual void get_debug_lines(std::vector<RenderingServer3D::Line> *r_lines, const Transform3D &p_xform, const Color &p_color) const override;
	virtual AABB get_aabb() const override { return AABB(size * -0.5f, size); }
};

class CapsuleShape3D : public Shape3D {
	GDCLASS(CapsuleShape3D, Shape3D)

	real_t radius = 0.5f;
	real_t height = 2.0f; // hauteur totale, demi-sphères comprises

protected:
	static void _bind_methods();

public:
	void set_radius(real_t p_radius);
	real_t get_radius() const { return radius; }
	void set_height(real_t p_height);
	real_t get_height() const { return height; }
	virtual PhysicsServer3D::ShapeData get_physics_shape() const override;
	virtual void get_debug_lines(std::vector<RenderingServer3D::Line> *r_lines, const Transform3D &p_xform, const Color &p_color) const override;
	virtual AABB get_aabb() const override { return AABB(Vector3(-radius, -height * 0.5f, -radius), Vector3(radius * 2, height, radius * 2)); }
};

// Outils de tracé filaire partagés avec l'éditeur.
namespace DebugLines3D {
void add_box(std::vector<RenderingServer3D::Line> *r_lines, const Transform3D &p_xform, const AABB &p_box, const Color &p_color);
// Arc de cercle dans le plan (p_axis_u, p_axis_v), de l'angle p_from à p_to (radians).
void add_arc(std::vector<RenderingServer3D::Line> *r_lines, const Transform3D &p_xform, const Vector3 &p_center, const Vector3 &p_axis_u, const Vector3 &p_axis_v, real_t p_radius, real_t p_from, real_t p_to, const Color &p_color, int p_segments = 32);
} // namespace DebugLines3D
