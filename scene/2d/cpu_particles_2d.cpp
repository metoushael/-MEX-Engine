#include "cpu_particles_2d.h"

#include "scene/main/scene_tree.h"

void CPUParticles2D::_emit_particle(Particle &p_particle) {
	Vector2 pos;
	switch (emission_shape) {
		case EMISSION_SHAPE_SPHERE: {
			// Tirage uniforme dans le disque : la racine évite l'entassement au centre.
			const real_t radius = emission_sphere_radius * (real_t)std::sqrt(Math::randf());
			pos = Vector2(radius, 0).rotated((real_t)(Math::randf() * 2.0 * Math_PI));
		} break;
		case EMISSION_SHAPE_RECTANGLE: {
			pos = Vector2((real_t)Math::randf_range(-emission_rect_extents.x, emission_rect_extents.x), (real_t)Math::randf_range(-emission_rect_extents.y, emission_rect_extents.y));
		} break;
		default:
			break;
	}
	const Vector2 base = direction.length() > 0 ? direction.normalized() : Vector2(1, 0);
	const real_t angle = (real_t)Math::deg_to_rad(Math::randf_range(-spread, spread));
	Vector2 velocity = base.rotated(angle) * (real_t)Math::randf_range(initial_velocity_min, initial_velocity_max);
	if (!local_coords) {
		// Coordonnées globales : la particule ne suit plus l'émetteur une fois partie.
		pos = emit_transform.xform(pos);
		velocity = emit_transform.basis_xform(velocity);
	}
	p_particle.active = true;
	p_particle.position = pos;
	p_particle.velocity = velocity;
	p_particle.rotation = 0;
	p_particle.angular_velocity = (real_t)Math::deg_to_rad(Math::randf_range(angular_velocity_min, angular_velocity_max));
	p_particle.scale = (real_t)Math::randf_range(scale_amount_min, scale_amount_max);
	p_particle.age = 0;
	p_particle.lifetime = (real_t)(lifetime * (1.0 - lifetime_randomness * Math::randf()));
}

void CPUParticles2D::_step(double p_delta) {
	if ((int)particles.size() != amount) {
		particles.assign((size_t)amount, Particle());
	}
	emit_transform = get_global_transform();

	// --- Vie des particules ---
	const real_t delta = (real_t)p_delta;
	for (Particle &particle : particles) {
		if (!particle.active) {
			continue;
		}
		particle.age += delta;
		if (particle.age >= particle.lifetime) {
			particle.active = false;
			continue;
		}
		particle.velocity += gravity * delta;
		if (damping > 0) {
			const real_t speed = particle.velocity.length();
			if (speed > 0) {
				particle.velocity = particle.velocity * (std::max(speed - damping * delta, (real_t)0) / speed);
			}
		}
		particle.position += particle.velocity * delta;
		particle.rotation += particle.angular_velocity * delta;
	}

	// --- Émission ---
	// Un cycle dure « lifetime » et émet « amount » particules : réparties sur tout le cycle,
	// ou regroupées au début selon « explosiveness » (1 : toutes d'un coup).
	if (emitting && !cycle_done) {
		cycle_time += p_delta;
		const double window = lifetime * (1.0 - explosiveness);
		int target = window <= 0.0 ? amount : (int)((cycle_time / window) * amount) + 1;
		target = std::min(target, amount);
		while (emitted_in_cycle < target) {
			bool found = false;
			for (Particle &particle : particles) {
				if (!particle.active) {
					_emit_particle(particle);
					found = true;
					break;
				}
			}
			if (!found) {
				break; // toutes les particules sont encore en vie
			}
			emitted_in_cycle++;
		}
		if (cycle_time >= lifetime) {
			cycle_time -= lifetime;
			emitted_in_cycle = 0;
			if (one_shot) {
				cycle_done = true;
			}
		}
	}

	// Émission unique terminée : le signal part quand la dernière particule s'éteint.
	if (one_shot && cycle_done && emitting && get_active_particle_count() == 0) {
		emitting = false;
		emit_signal("finished");
	}
	if (!emitting && get_active_particle_count() == 0) {
		set_process_internal(false);
	}
	queue_redraw();
}

void CPUParticles2D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			set_process_internal(emitting);
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			_step(get_process_delta_time() * speed_scale);
		} break;
		case NOTIFICATION_DRAW: {
			const Transform2D to_local = local_coords ? Transform2D() : get_global_transform().affine_inverse();
			for (const Particle &particle : particles) {
				if (!particle.active) {
					continue;
				}
				const real_t t = particle.lifetime > 0 ? Math::clamp<real_t>(particle.age / particle.lifetime, 0.0f, 1.0f) : 1.0f;
				const Color tint(color.r + (color_end.r - color.r) * t, color.g + (color_end.g - color.g) * t, color.b + (color_end.b - color.b) * t, color.a + (color_end.a - color.a) * t);
				const real_t scale = particle.scale * (1.0f + (scale_amount_end - 1.0f) * t);
				const Vector2 center = to_local.xform(particle.position);
				if (texture.is_valid()) {
					const Size2 size = texture->get_size() * scale;
					draw_texture_rect(texture, Rect2(center - size * 0.5f, size), tint);
				} else {
					const real_t half = particle_size * scale * 0.5f;
					std::vector<Vector2> corners = { Vector2(-half, -half), Vector2(half, -half), Vector2(half, half), Vector2(-half, half) };
					for (Vector2 &corner : corners) {
						corner = center + corner.rotated(particle.rotation);
					}
					draw_colored_polygon(corners, tint);
				}
			}
		} break;
	}
}

void CPUParticles2D::set_emitting(bool p_emitting) {
	if (p_emitting && !emitting) {
		cycle_time = 0;
		emitted_in_cycle = 0;
		cycle_done = false;
	}
	emitting = p_emitting;
	if (emitting && is_inside_tree()) {
		set_process_internal(true);
	}
}

void CPUParticles2D::set_amount(int p_amount) {
	ERR_FAIL_COND_MSG(p_amount < 1, "CPUParticles2D : amount doit valoir au moins 1.");
	amount = p_amount;
}

void CPUParticles2D::set_lifetime(double p_lifetime) {
	ERR_FAIL_COND_MSG(p_lifetime <= 0, "CPUParticles2D : lifetime doit être strictement positif.");
	lifetime = p_lifetime;
}

void CPUParticles2D::set_texture(const Ref<Texture2D> &p_texture) {
	texture = p_texture;
	queue_redraw();
}

void CPUParticles2D::restart() {
	for (Particle &particle : particles) {
		particle.active = false;
	}
	cycle_time = 0;
	emitted_in_cycle = 0;
	cycle_done = false;
	emitting = true;
	if (is_inside_tree()) {
		set_process_internal(true);
	}
}

int CPUParticles2D::get_active_particle_count() const {
	int count = 0;
	for (const Particle &particle : particles) {
		count += particle.active ? 1 : 0;
	}
	return count;
}

void CPUParticles2D::_bind_methods() {
#define BIND_PARTICLE_PROPERTY(m_type, m_name, m_setter, m_getter, m_hint, m_hint_string) \
	ClassDB::bind_method(D_METHOD(#m_setter, "value"), &CPUParticles2D::m_setter); \
	ClassDB::bind_method(D_METHOD(#m_getter), &CPUParticles2D::m_getter); \
	ADD_PROPERTY(PropertyInfo(m_type, m_name, m_hint, m_hint_string), #m_setter, #m_getter)

	BIND_PARTICLE_PROPERTY(Variant::BOOL, "emitting", set_emitting, is_emitting, PROPERTY_HINT_NONE, "");
	BIND_PARTICLE_PROPERTY(Variant::INT, "amount", set_amount, get_amount, PROPERTY_HINT_RANGE, "1,10000,1");
	BIND_PARTICLE_PROPERTY(Variant::FLOAT, "lifetime", set_lifetime, get_lifetime, PROPERTY_HINT_RANGE, "0.01,600,0.01");
	BIND_PARTICLE_PROPERTY(Variant::BOOL, "one_shot", set_one_shot, get_one_shot, PROPERTY_HINT_NONE, "");
	BIND_PARTICLE_PROPERTY(Variant::FLOAT, "explosiveness", set_explosiveness_ratio, get_explosiveness_ratio, PROPERTY_HINT_RANGE, "0,1,0.01");
	BIND_PARTICLE_PROPERTY(Variant::FLOAT, "lifetime_randomness", set_lifetime_randomness, get_lifetime_randomness, PROPERTY_HINT_RANGE, "0,1,0.01");
	BIND_PARTICLE_PROPERTY(Variant::FLOAT, "speed_scale", set_speed_scale, get_speed_scale, PROPERTY_HINT_RANGE, "0,16,0.01");
	BIND_PARTICLE_PROPERTY(Variant::BOOL, "local_coords", set_use_local_coordinates, get_use_local_coordinates, PROPERTY_HINT_NONE, "");
	BIND_PARTICLE_PROPERTY(Variant::OBJECT, "texture", set_texture, get_texture, PROPERTY_HINT_RESOURCE_TYPE, "Texture2D");
	BIND_PARTICLE_PROPERTY(Variant::INT, "emission_shape", set_emission_shape, get_emission_shape, PROPERTY_HINT_ENUM, "Point,Sphere,Rectangle");
	BIND_PARTICLE_PROPERTY(Variant::FLOAT, "emission_sphere_radius", set_emission_sphere_radius, get_emission_sphere_radius, PROPERTY_HINT_RANGE, "0,4096,0.1");
	BIND_PARTICLE_PROPERTY(Variant::VECTOR2, "emission_rect_extents", set_emission_rect_extents, get_emission_rect_extents, PROPERTY_HINT_NONE, "");
	BIND_PARTICLE_PROPERTY(Variant::VECTOR2, "direction", set_direction, get_direction, PROPERTY_HINT_NONE, "");
	BIND_PARTICLE_PROPERTY(Variant::FLOAT, "spread", set_spread, get_spread, PROPERTY_HINT_RANGE, "0,180,0.1");
	BIND_PARTICLE_PROPERTY(Variant::VECTOR2, "gravity", set_gravity, get_gravity, PROPERTY_HINT_NONE, "");
	BIND_PARTICLE_PROPERTY(Variant::FLOAT, "initial_velocity_min", set_initial_velocity_min, get_initial_velocity_min, PROPERTY_HINT_RANGE, "0,10000,0.1");
	BIND_PARTICLE_PROPERTY(Variant::FLOAT, "initial_velocity_max", set_initial_velocity_max, get_initial_velocity_max, PROPERTY_HINT_RANGE, "0,10000,0.1");
	BIND_PARTICLE_PROPERTY(Variant::FLOAT, "angular_velocity_min", set_angular_velocity_min, get_angular_velocity_min, PROPERTY_HINT_RANGE, "-3600,3600,0.1");
	BIND_PARTICLE_PROPERTY(Variant::FLOAT, "angular_velocity_max", set_angular_velocity_max, get_angular_velocity_max, PROPERTY_HINT_RANGE, "-3600,3600,0.1");
	BIND_PARTICLE_PROPERTY(Variant::FLOAT, "damping", set_damping, get_damping, PROPERTY_HINT_RANGE, "0,10000,0.1");
	BIND_PARTICLE_PROPERTY(Variant::FLOAT, "scale_amount_min", set_scale_amount_min, get_scale_amount_min, PROPERTY_HINT_RANGE, "0,100,0.01");
	BIND_PARTICLE_PROPERTY(Variant::FLOAT, "scale_amount_max", set_scale_amount_max, get_scale_amount_max, PROPERTY_HINT_RANGE, "0,100,0.01");
	BIND_PARTICLE_PROPERTY(Variant::FLOAT, "scale_amount_end", set_scale_amount_end, get_scale_amount_end, PROPERTY_HINT_RANGE, "0,100,0.01");
	BIND_PARTICLE_PROPERTY(Variant::FLOAT, "particle_size", set_particle_size, get_particle_size, PROPERTY_HINT_RANGE, "1,512,0.5");
	BIND_PARTICLE_PROPERTY(Variant::COLOR, "color", set_color, get_color, PROPERTY_HINT_NONE, "");
	BIND_PARTICLE_PROPERTY(Variant::COLOR, "color_end", set_color_end, get_color_end, PROPERTY_HINT_NONE, "");
#undef BIND_PARTICLE_PROPERTY

	ClassDB::bind_method(D_METHOD("restart"), &CPUParticles2D::restart);
	ClassDB::bind_method(D_METHOD("get_active_particle_count"), &CPUParticles2D::get_active_particle_count);

	BIND_ENUM_CONSTANT(EMISSION_SHAPE_POINT);
	BIND_ENUM_CONSTANT(EMISSION_SHAPE_SPHERE);
	BIND_ENUM_CONSTANT(EMISSION_SHAPE_RECTANGLE);

	ADD_SIGNAL(MethodInfo("finished"));
}
