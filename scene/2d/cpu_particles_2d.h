// CPUParticles2D : émetteur de particules 2D simulées par le processeur (fumée, étincelles,
// pluie, explosions). Les noms des propriétés sont ceux de Godot.
#pragma once

#include "scene/2d/node_2d.h"

class CPUParticles2D : public Node2D {
	GDCLASS(CPUParticles2D, Node2D)

public:
	enum EmissionShape {
		EMISSION_SHAPE_POINT,
		EMISSION_SHAPE_SPHERE, // disque de rayon emission_sphere_radius
		EMISSION_SHAPE_RECTANGLE, // rectangle de demi-côtés emission_rect_extents
	};

private:
	struct Particle {
		bool active = false;
		Vector2 position;
		Vector2 velocity;
		real_t rotation = 0;
		real_t angular_velocity = 0;
		real_t scale = 1;
		real_t age = 0;
		real_t lifetime = 1;
	};

	bool emitting = true;
	int amount = 8;
	double lifetime = 1.0;
	bool one_shot = false;
	real_t explosiveness = 0;
	real_t lifetime_randomness = 0;
	double speed_scale = 1.0;
	bool local_coords = false;
	Ref<Texture2D> texture;

	int emission_shape = EMISSION_SHAPE_POINT;
	real_t emission_sphere_radius = 16;
	Vector2 emission_rect_extents = Vector2(16, 16);

	Vector2 direction = Vector2(1, 0);
	real_t spread = 45; // degrés, de part et d'autre de la direction
	Vector2 gravity = Vector2(0, 980);
	real_t initial_velocity_min = 0;
	real_t initial_velocity_max = 0;
	real_t angular_velocity_min = 0; // degrés par seconde
	real_t angular_velocity_max = 0;
	real_t damping = 0;
	real_t scale_amount_min = 1;
	real_t scale_amount_max = 1;
	real_t scale_amount_end = 1; // facteur appliqué à la taille en fin de vie
	real_t particle_size = 8; // côté du carré sans texture, en pixels
	Color color = Color(1, 1, 1, 1);
	Color color_end = Color(1, 1, 1, 0);

	std::vector<Particle> particles;
	double cycle_time = 0; // temps écoulé dans le cycle d'émission en cours
	int emitted_in_cycle = 0;
	bool cycle_done = false; // one_shot : toutes les particules du cycle sont parties
	Transform2D emit_transform; // transformation globale au moment du dernier pas

	void _emit_particle(Particle &p_particle);
	void _step(double p_delta);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_emitting(bool p_emitting);
	bool is_emitting() const { return emitting; }
	void set_amount(int p_amount);
	int get_amount() const { return amount; }
	void set_lifetime(double p_lifetime);
	double get_lifetime() const { return lifetime; }
	void set_one_shot(bool p_one_shot) { one_shot = p_one_shot; }
	bool get_one_shot() const { return one_shot; }
	void set_explosiveness_ratio(real_t p_ratio) { explosiveness = Math::clamp<real_t>(p_ratio, 0.0f, 1.0f); }
	real_t get_explosiveness_ratio() const { return explosiveness; }
	void set_lifetime_randomness(real_t p_ratio) { lifetime_randomness = Math::clamp<real_t>(p_ratio, 0.0f, 1.0f); }
	real_t get_lifetime_randomness() const { return lifetime_randomness; }
	void set_speed_scale(double p_scale) { speed_scale = p_scale; }
	double get_speed_scale() const { return speed_scale; }
	void set_use_local_coordinates(bool p_local) { local_coords = p_local; }
	bool get_use_local_coordinates() const { return local_coords; }
	void set_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_texture() const { return texture; }

	void set_emission_shape(int p_shape) { emission_shape = p_shape; }
	int get_emission_shape() const { return emission_shape; }
	void set_emission_sphere_radius(real_t p_radius) { emission_sphere_radius = p_radius; }
	real_t get_emission_sphere_radius() const { return emission_sphere_radius; }
	void set_emission_rect_extents(const Vector2 &p_extents) { emission_rect_extents = p_extents; }
	Vector2 get_emission_rect_extents() const { return emission_rect_extents; }

	void set_direction(const Vector2 &p_direction) { direction = p_direction; }
	Vector2 get_direction() const { return direction; }
	void set_spread(real_t p_spread) { spread = p_spread; }
	real_t get_spread() const { return spread; }
	void set_gravity(const Vector2 &p_gravity) { gravity = p_gravity; }
	Vector2 get_gravity() const { return gravity; }
	void set_initial_velocity_min(real_t p_value) { initial_velocity_min = p_value; }
	real_t get_initial_velocity_min() const { return initial_velocity_min; }
	void set_initial_velocity_max(real_t p_value) { initial_velocity_max = p_value; }
	real_t get_initial_velocity_max() const { return initial_velocity_max; }
	void set_angular_velocity_min(real_t p_value) { angular_velocity_min = p_value; }
	real_t get_angular_velocity_min() const { return angular_velocity_min; }
	void set_angular_velocity_max(real_t p_value) { angular_velocity_max = p_value; }
	real_t get_angular_velocity_max() const { return angular_velocity_max; }
	void set_damping(real_t p_value) { damping = p_value; }
	real_t get_damping() const { return damping; }
	void set_scale_amount_min(real_t p_value) { scale_amount_min = p_value; }
	real_t get_scale_amount_min() const { return scale_amount_min; }
	void set_scale_amount_max(real_t p_value) { scale_amount_max = p_value; }
	real_t get_scale_amount_max() const { return scale_amount_max; }
	void set_scale_amount_end(real_t p_value) { scale_amount_end = p_value; }
	real_t get_scale_amount_end() const { return scale_amount_end; }
	void set_particle_size(real_t p_value) { particle_size = p_value; }
	real_t get_particle_size() const { return particle_size; }
	void set_color(const Color &p_color) { color = p_color; }
	Color get_color() const { return color; }
	void set_color_end(const Color &p_color) { color_end = p_color; }
	Color get_color_end() const { return color_end; }

	void restart();
	int get_active_particle_count() const;
};
