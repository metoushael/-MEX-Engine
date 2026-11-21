// Nœuds de rendu 3D : MeshInstance3D, Camera3D, lumières et WorldEnvironment.
#pragma once

#include "scene/3d/node_3d.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"

class MeshInstance3D : public Node3D {
	GDCLASS(MeshInstance3D, Node3D)

	Ref<Mesh> mesh;
	Ref<Material> material_override;
	bool cast_shadow = true;
	RID instance;

	void _update_instance();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_mesh(const Ref<Mesh> &p_mesh);
	Ref<Mesh> get_mesh() const { return mesh; }
	void set_material_override(const Ref<Material> &p_material);
	Ref<Material> get_material_override() const { return material_override; }
	void set_cast_shadow(bool p_enabled);
	bool get_cast_shadow() const { return cast_shadow; }
	AABB get_aabb() const { return mesh.is_valid() ? mesh->get_aabb() : AABB(); }

	virtual bool _edit_get_aabb(AABB &r_aabb) const override;
	~MeshInstance3D();
};

class Camera3D : public Node3D {
	GDCLASS(Camera3D, Node3D)

public:
	enum ProjectionType {
		PROJECTION_PERSPECTIVE,
		PROJECTION_ORTHOGONAL,
	};

private:
	int projection = PROJECTION_PERSPECTIVE;
	real_t fov = 70.0f;
	real_t size = 10.0f;
	real_t z_near = 0.05f;
	real_t z_far = 500.0f;
	bool current = false;

	real_t _get_aspect() const;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_projection(int p_projection) { projection = p_projection == PROJECTION_ORTHOGONAL ? PROJECTION_ORTHOGONAL : PROJECTION_PERSPECTIVE; }
	int get_projection() const { return projection; }
	void set_fov(real_t p_fov) { fov = Math::clamp(p_fov, (real_t)1, (real_t)179); }
	real_t get_fov() const { return fov; }
	void set_size(real_t p_size) { size = std::max(p_size, (real_t)0.001); }
	real_t get_size() const { return size; }
	void set_near(real_t p_near) { z_near = std::max(p_near, (real_t)0.001); }
	real_t get_near() const { return z_near; }
	void set_far(real_t p_far) { z_far = std::max(p_far, (real_t)0.01); }
	real_t get_far() const { return z_far; }
	void set_current(bool p_current);
	bool get_current_flag() const { return current; }
	void make_current();
	void clear_current();
	bool is_current() const;

	RenderingServer3D::CameraData get_camera_data() const;
	// Conversions écran <-> monde (coordonnées écran en pixels du viewport du jeu).
	Vector2 unproject_position(const Vector3 &p_world) const;
	bool is_position_behind(const Vector3 &p_world) const;
	Vector3 project_ray_origin(const Vector2 &p_screen) const;
	Vector3 project_ray_normal(const Vector2 &p_screen) const;
	Vector3 project_position(const Vector2 &p_screen, real_t p_depth) const;

	virtual void _edit_get_gizmo_lines(std::vector<RenderingServer3D::Line> *r_lines) const override;
};

class Light3D : public Node3D {
	GDCLASS(Light3D, Node3D)

	RID light;

protected:
	RenderingServer3D::LightData data;

	void _update_light();
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_color(const Color &p_color);
	Color get_color() const { return data.color; }
	void set_energy(real_t p_energy);
	real_t get_energy() const { return data.energy; }
	~Light3D();
};

// Lumière parallèle (soleil), dirigée selon l'axe -Z du nœud. Peut projeter des ombres.
class DirectionalLight3D : public Light3D {
	GDCLASS(DirectionalLight3D, Light3D)

protected:
	static void _bind_methods();

public:
	void set_shadow(bool p_enabled);
	bool has_shadow() const { return data.shadow; }
	virtual void _edit_get_gizmo_lines(std::vector<RenderingServer3D::Line> *r_lines) const override;
	DirectionalLight3D() { data.type = RenderingServer3D::LIGHT_DIRECTIONAL; }
};

// Lumière ponctuelle rayonnant dans toutes les directions.
class OmniLight3D : public Light3D {
	GDCLASS(OmniLight3D, Light3D)

protected:
	static void _bind_methods();

public:
	void set_range(real_t p_range);
	real_t get_range() const { return data.range; }
	void set_attenuation(real_t p_attenuation);
	real_t get_attenuation() const { return data.attenuation; }
	virtual void _edit_get_gizmo_lines(std::vector<RenderingServer3D::Line> *r_lines) const override;
	OmniLight3D() { data.type = RenderingServer3D::LIGHT_OMNI; }
};

// Projecteur : cône dirigé selon l'axe -Z du nœud.
class SpotLight3D : public Light3D {
	GDCLASS(SpotLight3D, Light3D)

protected:
	static void _bind_methods();

public:
	void set_range(real_t p_range);
	real_t get_range() const { return data.range; }
	void set_attenuation(real_t p_attenuation);
	real_t get_attenuation() const { return data.attenuation; }
	void set_spot_angle(real_t p_degrees);
	real_t get_spot_angle() const { return data.spot_angle; }
	virtual void _edit_get_gizmo_lines(std::vector<RenderingServer3D::Line> *r_lines) const override;
	SpotLight3D() { data.type = RenderingServer3D::LIGHT_SPOT; }
};

// Applique un Environment (ciel, lumière ambiante, brouillard) à la scène 3D.
class WorldEnvironment : public Node {
	GDCLASS(WorldEnvironment, Node)

	Ref<Environment> environment;

	void _update();
	void _environment_changed() { _update(); }

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_environment(const Ref<Environment> &p_environment);
	Ref<Environment> get_environment() const { return environment; }
};
