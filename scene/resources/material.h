// Matériaux et environnement 3D.
#pragma once

#include "scene/resources/texture.h"
#include "servers/rendering_server_3d.h"

class Material : public Resource {
	GDCLASS(Material, Resource)

protected:
	RID material;
	static void _bind_methods() {}

public:
	RID get_rid() const { return material; }
	Material();
	~Material();
};

// Matériau standard : couleur ou texture, éclairage de type métal/rugosité, émission.
class StandardMaterial3D : public Material {
	GDCLASS(StandardMaterial3D, Material)

public:
	enum CullMode {
		CULL_BACK,
		CULL_FRONT,
		CULL_DISABLED,
	};

private:
	Color albedo_color = Color(1, 1, 1, 1);
	Ref<Texture2D> albedo_texture;
	real_t metallic = 0.0f;
	real_t roughness = 0.7f;
	Color emission = Color(0, 0, 0, 1);
	real_t emission_energy = 1.0f;
	Vector2 uv_scale = Vector2(1, 1);
	bool unshaded = false;
	bool transparency = false;
	int cull_mode = CULL_BACK;

	void _update();

protected:
	static void _bind_methods();

public:
	void set_albedo_color(const Color &p_color);
	Color get_albedo_color() const { return albedo_color; }
	void set_albedo_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_albedo_texture() const { return albedo_texture; }
	void set_metallic(real_t p_metallic);
	real_t get_metallic() const { return metallic; }
	void set_roughness(real_t p_roughness);
	real_t get_roughness() const { return roughness; }
	void set_emission(const Color &p_color);
	Color get_emission() const { return emission; }
	void set_emission_energy(real_t p_energy);
	real_t get_emission_energy() const { return emission_energy; }
	void set_uv_scale(const Vector2 &p_scale);
	Vector2 get_uv_scale() const { return uv_scale; }
	void set_unshaded(bool p_unshaded);
	bool is_unshaded() const { return unshaded; }
	void set_transparency(bool p_enabled);
	bool has_transparency() const { return transparency; }
	void set_cull_mode(int p_mode);
	int get_cull_mode() const { return cull_mode; }
	StandardMaterial3D();
};

// Environment : fond (couleur ou ciel), lumière ambiante et brouillard.
class Environment : public Resource {
	GDCLASS(Environment, Resource)

public:
	enum BackgroundMode {
		BG_COLOR,
		BG_SKY,
	};

private:
	RenderingServer3D::EnvironmentData data;

protected:
	static void _bind_methods();

public:
	const RenderingServer3D::EnvironmentData &get_data() const { return data; }
	void set_background_mode(int p_mode);
	int get_background_mode() const { return (int)data.background; }
	void set_background_color(const Color &p_color);
	Color get_background_color() const { return data.background_color; }
	void set_sky_top_color(const Color &p_color);
	Color get_sky_top_color() const { return data.sky_top_color; }
	void set_sky_horizon_color(const Color &p_color);
	Color get_sky_horizon_color() const { return data.sky_horizon_color; }
	void set_ground_color(const Color &p_color);
	Color get_ground_color() const { return data.ground_color; }
	void set_ambient_light_color(const Color &p_color);
	Color get_ambient_light_color() const { return data.ambient_color; }
	void set_ambient_light_energy(real_t p_energy);
	real_t get_ambient_light_energy() const { return data.ambient_energy; }
	void set_fog_enabled(bool p_enabled);
	bool is_fog_enabled() const { return data.fog_enabled; }
	void set_fog_color(const Color &p_color);
	Color get_fog_color() const { return data.fog_color; }
	void set_fog_density(real_t p_density);
	real_t get_fog_density() const { return data.fog_density; }
};
