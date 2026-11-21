// Shape2D : formes de collision (ressources partagées entre CollisionShape2D).
#pragma once

#include "core/io/resource.h"
#include "servers/physics_server_2d.h"

class CanvasItem;

class Shape2D : public Resource {
	GDCLASS(Shape2D, Resource)

	uint32_t version = 1;

protected:
	static void _bind_methods() {}
	void _shape_changed() {
		version++;
		emit_changed();
	}

public:
	uint32_t get_version() const { return version; }
	// Une forme peut produire plusieurs primitives convexes (capsule = 2 cercles + 1 rectangle).
	virtual void get_physics_shapes(std::vector<PhysicsServer2D::ShapeData> *r_shapes) const {}
	virtual void draw(CanvasItem *p_item, const Color &p_color) const {}
	virtual Rect2 get_rect() const { return Rect2(); }
};

class RectangleShape2D : public Shape2D {
	GDCLASS(RectangleShape2D, Shape2D)

	Vector2 size = Vector2(32, 32);

protected:
	static void _bind_methods();

public:
	void set_size(const Vector2 &p_size);
	Vector2 get_size() const { return size; }
	virtual void get_physics_shapes(std::vector<PhysicsServer2D::ShapeData> *r_shapes) const override;
	virtual void draw(CanvasItem *p_item, const Color &p_color) const override;
	virtual Rect2 get_rect() const override { return Rect2(-size * 0.5f, size); }
};

class CircleShape2D : public Shape2D {
	GDCLASS(CircleShape2D, Shape2D)

	real_t radius = 16;

protected:
	static void _bind_methods();

public:
	void set_radius(real_t p_radius);
	real_t get_radius() const { return radius; }
	virtual void get_physics_shapes(std::vector<PhysicsServer2D::ShapeData> *r_shapes) const override;
	virtual void draw(CanvasItem *p_item, const Color &p_color) const override;
	virtual Rect2 get_rect() const override { return Rect2(-radius, -radius, radius * 2, radius * 2); }
};

class CapsuleShape2D : public Shape2D {
	GDCLASS(CapsuleShape2D, Shape2D)

	real_t radius = 12;
	real_t height = 48; // hauteur totale, demi-cercles compris

protected:
	static void _bind_methods();

public:
	void set_radius(real_t p_radius);
	real_t get_radius() const { return radius; }
	void set_height(real_t p_height);
	real_t get_height() const { return height; }
	virtual void get_physics_shapes(std::vector<PhysicsServer2D::ShapeData> *r_shapes) const override;
	virtual void draw(CanvasItem *p_item, const Color &p_color) const override;
	virtual Rect2 get_rect() const override { return Rect2(-radius, -height * 0.5f, radius * 2, height); }
};

class ConvexPolygonShape2D : public Shape2D {
	GDCLASS(ConvexPolygonShape2D, Shape2D)

	std::vector<Vector2> points;

protected:
	static void _bind_methods();

public:
	void set_points(const Array &p_points);
	Array get_points() const;
	virtual void get_physics_shapes(std::vector<PhysicsServer2D::ShapeData> *r_shapes) const override;
	virtual void draw(CanvasItem *p_item, const Color &p_color) const override;
	virtual Rect2 get_rect() const override;

	ConvexPolygonShape2D();
};
