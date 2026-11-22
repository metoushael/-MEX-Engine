// Adapté de scene/3d/audio_stream_player_3d.cpp du moteur Godot 4.3.
// Copyright (c) 2014-present Godot Engine contributors, (c) 2007-2014 Juan Linietsky, Ariel Manzur.
// Licence MIT : thirdparty/godot/LICENSE.txt.
#include "audio_stream_player_3d.h"

#include "scene/main/scene_tree.h"
#include "servers/audio_server.h"

#include <cmath>

namespace {
const real_t EPSILON = 0.00001f;

inline real_t _db_to_linear(real_t p_db) {
	return std::exp(p_db * (real_t)0.11512925464970228420089957273422);
}

inline real_t _linear_to_db(real_t p_linear) {
	return std::log(p_linear) * (real_t)8.6858896380650365530225783783321;
}

// Répartition « Speaker-Placement Correction Amplitude Panning » (classe Spcap de Godot),
// réduite à la paire stéréo avant gauche / avant droite.
void _spcap_stereo(const Vector3 &p_source_direction, real_t p_tightness, real_t &r_left, real_t &r_right) {
	const Vector3 directions[2] = { Vector3(-1, 0, -1).normalized(), Vector3(1, 0, -1).normalized() };
	real_t squared[2];
	real_t sum = 0;
	for (int i = 0; i < 2; i++) {
		real_t effective = 0;
		for (int j = 0; j < 2; j++) {
			effective += 0.5f * (1.0f + directions[i].dot(directions[j]));
		}
		const real_t gain = 0.5f * std::pow(1.0f + directions[i].dot(p_source_direction), p_tightness) / effective;
		squared[i] = gain * gain;
		sum += squared[i];
	}
	if (sum <= 0) {
		r_left = r_right = (real_t)0.70710678;
		return;
	}
	r_left = std::sqrt(squared[0] / sum);
	r_right = std::sqrt(squared[1] / sum);
}
} // namespace

// --- AudioListener3D ---
ObjectID AudioListener3D::current_listener = 0;

void AudioListener3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (current) {
				current_listener = get_instance_id();
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (current_listener == get_instance_id()) {
				current_listener = 0;
			}
		} break;
	}
}

void AudioListener3D::set_current(bool p_current) {
	if (p_current) {
		make_current();
	} else {
		clear_current();
	}
}

void AudioListener3D::make_current() {
	current = true;
	if (is_inside_tree()) {
		current_listener = get_instance_id();
	}
}

void AudioListener3D::clear_current() {
	current = false;
	if (current_listener == get_instance_id()) {
		current_listener = 0;
	}
}

bool AudioListener3D::is_current() const {
	return is_inside_tree() && current_listener == get_instance_id();
}

Node3D *AudioListener3D::get_listener(const Node *p_from) {
	if (current_listener) {
		Node3D *listener = Object::cast_to<Node3D>(ObjectDB::get_instance(current_listener));
		if (listener && listener->is_inside_tree()) {
			return listener;
		}
	}
	if (p_from && p_from->is_inside_tree()) {
		return Object::cast_to<Node3D>(ObjectDB::get_instance(p_from->get_tree()->get_current_camera_3d()));
	}
	return nullptr;
}

void AudioListener3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_current", "current"), &AudioListener3D::set_current);
	ClassDB::bind_method(D_METHOD("get_current_flag"), &AudioListener3D::get_current_flag);
	ClassDB::bind_method(D_METHOD("make_current"), &AudioListener3D::make_current);
	ClassDB::bind_method(D_METHOD("clear_current"), &AudioListener3D::clear_current);
	ClassDB::bind_method(D_METHOD("is_current"), &AudioListener3D::is_current);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "current"), "set_current", "get_current_flag");
}

// --- AudioStreamPlayer3D ---
real_t AudioStreamPlayer3D::_get_attenuation_db(real_t p_distance) const {
	real_t att = 0;
	switch (attenuation_model) {
		case ATTENUATION_INVERSE_DISTANCE: {
			att = _linear_to_db(1.0f / ((p_distance / unit_size) + EPSILON));
		} break;
		case ATTENUATION_INVERSE_SQUARE_DISTANCE: {
			real_t d = (p_distance / unit_size);
			d *= d;
			att = _linear_to_db(1.0f / (d + EPSILON));
		} break;
		case ATTENUATION_LOGARITHMIC: {
			att = -20 * std::log(p_distance / unit_size + EPSILON);
		} break;
		default:
			break;
	}
	att += volume_db;
	if (att > max_db) {
		att = max_db;
	}
	return att;
}

void AudioStreamPlayer3D::compute_output(const Transform3D &p_listener, const Vector3 &p_relative_velocity, real_t &r_gain, real_t &r_pan, real_t &r_pitch) const {
	const Transform3D listener = p_listener.orthonormalized();
	const Vector3 global_pos = get_global_transform().origin;
	const Vector3 local_pos = listener.affine_inverse().xform(global_pos);
	const real_t dist = local_pos.length();
	r_pan = 0;
	r_pitch = pitch_scale;
	if (max_distance > 0 && dist > max_distance) {
		r_gain = 0; // hors de portée
		return;
	}
	real_t multiplier = _db_to_linear(_get_attenuation_db(dist));
	if (max_distance > 0) {
		multiplier *= std::max((real_t)0, 1.0f - (dist / max_distance));
	}
	if (emission_angle_enabled) {
		const Vector3 listener_to_pos = global_pos - listener.origin;
		const real_t c = listener_to_pos.normalized().dot(get_global_transform().basis.get_column(2).normalized()); // l'avant est -Z
		const real_t angle = (real_t)Math::rad_to_deg(std::acos(std::min(std::max(c, (real_t)-1), (real_t)1)));
		if (angle > emission_angle) {
			multiplier *= _db_to_linear(emission_angle_filter_attenuation_db);
		}
	}
	// Répartition gauche / droite, puis conversion en volume + balance pour le mixeur.
	real_t left = (real_t)0.70710678, right = (real_t)0.70710678;
	if (dist > EPSILON) {
		_spcap_stereo(local_pos.normalized(), 2.0f * panning_strength, left, right);
	}
	const real_t loudest = std::max(left, right);
	r_gain = multiplier * loudest;
	if (loudest > 0) {
		r_pan = left > right ? -(1.0f - right / left) : (1.0f - left / right);
	}
	if (doppler_tracking != DOPPLER_TRACKING_DISABLED) {
		const Vector3 local_velocity = listener.basis.xform_inv(p_relative_velocity);
		if (local_velocity.length_squared() > 0 && dist > EPSILON) {
			const real_t approaching = local_pos.normalized().dot(local_velocity.normalized());
			const real_t velocity = local_velocity.length();
			const real_t speed_of_sound = 343.0f;
			const real_t doppler = pitch_scale * speed_of_sound / (speed_of_sound + velocity * approaching);
			r_pitch = std::min(std::max(doppler, (real_t)(1 / 8.0)), (real_t)8.0); // évite les valeurs folles
		}
	}
}

void AudioStreamPlayer3D::_update_voice(double p_delta) {
	if (!is_inside_tree()) {
		return;
	}
	const Vector3 position = get_global_transform().origin;
	Node3D *listener_node = AudioListener3D::get_listener(this);
	if (!listener_node) {
		last_gain = 0;
		last_pan = 0;
		last_pitch = pitch_scale;
	} else {
		const Transform3D listener = listener_node->get_global_transform();
		Vector3 relative_velocity;
		if (has_previous && p_delta > 0) {
			relative_velocity = ((position - previous_position) - (listener.origin - previous_listener_position)) / (real_t)p_delta;
		}
		previous_listener_position = listener.origin;
		compute_output(listener, relative_velocity, last_gain, last_pan, last_pitch);
	}
	previous_position = position;
	has_previous = listener_node != nullptr;
	AudioServer *as = AudioServer::get_singleton();
	if (voice && as) {
		as->set_voice_volume(voice, last_gain);
		as->set_voice_pan(voice, last_pan);
		as->set_voice_pitch(voice, last_pitch);
	}
}

void AudioStreamPlayer3D::update_output() {
	has_previous = false;
	_update_voice(0.0);
}

real_t AudioStreamPlayer3D::get_pitch_for_velocity(const Vector3 &p_relative_velocity) const {
	Node3D *listener = AudioListener3D::get_listener(this);
	real_t gain = 0, pan = 0, pitch = pitch_scale;
	if (listener) {
		compute_output(listener->get_global_transform(), p_relative_velocity, gain, pan, pitch);
	}
	return pitch;
}

void AudioStreamPlayer3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (autoplay && !get_tree()->is_edit_mode()) {
				play();
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (was_playing && !is_playing()) {
				was_playing = false;
				set_process_internal(false);
				emit_signal("finished");
			} else {
				_update_voice(get_process_delta_time());
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			stop();
		} break;
	}
}

void AudioStreamPlayer3D::set_stream(const Ref<AudioStream> &p_stream) {
	stop();
	stream = p_stream;
}

void AudioStreamPlayer3D::play(double p_from) {
	AudioServer *as = AudioServer::get_singleton();
	if (!as || stream.is_null()) {
		return;
	}
	stop();
	voice = as->play(stream->get_samples(), 0.0f, loop, p_from, std::max(as->get_bus_index(bus), 0), pitch_scale);
	if (voice) {
		was_playing = true;
		has_previous = false;
		_update_voice(0.0);
		set_process_internal(true);
	}
}

void AudioStreamPlayer3D::stop() {
	if (voice && AudioServer::get_singleton()) {
		AudioServer::get_singleton()->stop(voice);
	}
	voice = 0;
	was_playing = false;
	set_process_internal(false);
}

bool AudioStreamPlayer3D::is_playing() const {
	return voice && AudioServer::get_singleton() && AudioServer::get_singleton()->is_playing(voice);
}

double AudioStreamPlayer3D::get_playback_position() const {
	return is_playing() ? AudioServer::get_singleton()->get_voice_position(voice) : 0.0;
}

void AudioStreamPlayer3D::_bind_methods() {
#define BIND_PLAYER_PROPERTY(m_type, m_name, m_setter, m_getter, m_hint, m_hint_string) \
	ClassDB::bind_method(D_METHOD(#m_setter, "value"), &AudioStreamPlayer3D::m_setter); \
	ClassDB::bind_method(D_METHOD(#m_getter), &AudioStreamPlayer3D::m_getter); \
	ADD_PROPERTY(PropertyInfo(m_type, m_name, m_hint, m_hint_string), #m_setter, #m_getter)
	BIND_PLAYER_PROPERTY(Variant::OBJECT, "stream", set_stream, get_stream, PROPERTY_HINT_RESOURCE_TYPE, "AudioStream");
	BIND_PLAYER_PROPERTY(Variant::INT, "attenuation_model", set_attenuation_model, get_attenuation_model, PROPERTY_HINT_ENUM, "Inverse,Inverse Square,Logarithmic,Disabled");
	BIND_PLAYER_PROPERTY(Variant::FLOAT, "volume_db", set_volume_db, get_volume_db, PROPERTY_HINT_RANGE, "-80,24,0.1");
	BIND_PLAYER_PROPERTY(Variant::FLOAT, "unit_size", set_unit_size, get_unit_size, PROPERTY_HINT_RANGE, "0.1,100,0.01");
	BIND_PLAYER_PROPERTY(Variant::FLOAT, "max_db", set_max_db, get_max_db, PROPERTY_HINT_RANGE, "-24,6,0.1");
	BIND_PLAYER_PROPERTY(Variant::FLOAT, "pitch_scale", set_pitch_scale, get_pitch_scale, PROPERTY_HINT_RANGE, "0.01,4,0.01");
	BIND_PLAYER_PROPERTY(Variant::BOOL, "autoplay", set_autoplay, is_autoplay_enabled, PROPERTY_HINT_NONE, "");
	BIND_PLAYER_PROPERTY(Variant::BOOL, "loop", set_loop, has_loop, PROPERTY_HINT_NONE, "");
	BIND_PLAYER_PROPERTY(Variant::FLOAT, "max_distance", set_max_distance, get_max_distance, PROPERTY_HINT_RANGE, "0,4096,0.01");
	BIND_PLAYER_PROPERTY(Variant::FLOAT, "panning_strength", set_panning_strength, get_panning_strength, PROPERTY_HINT_RANGE, "0,3,0.01");
	BIND_PLAYER_PROPERTY(Variant::STRING, "bus", set_bus, get_bus, PROPERTY_HINT_NONE, "");
	BIND_PLAYER_PROPERTY(Variant::BOOL, "emission_angle_enabled", set_emission_angle_enabled, is_emission_angle_enabled, PROPERTY_HINT_NONE, "");
	BIND_PLAYER_PROPERTY(Variant::FLOAT, "emission_angle_degrees", set_emission_angle, get_emission_angle, PROPERTY_HINT_RANGE, "0.1,90,0.1");
	BIND_PLAYER_PROPERTY(Variant::FLOAT, "emission_angle_filter_attenuation_db", set_emission_angle_filter_attenuation_db, get_emission_angle_filter_attenuation_db, PROPERTY_HINT_RANGE, "-80,0,0.1");
	BIND_PLAYER_PROPERTY(Variant::INT, "doppler_tracking", set_doppler_tracking, get_doppler_tracking, PROPERTY_HINT_ENUM, "Disabled,Idle");
#undef BIND_PLAYER_PROPERTY
	ClassDB::bind_method(D_METHOD("play", "from"), &AudioStreamPlayer3D::play, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("stop"), &AudioStreamPlayer3D::stop);
	ClassDB::bind_method(D_METHOD("is_playing"), &AudioStreamPlayer3D::is_playing);
	ClassDB::bind_method(D_METHOD("get_playback_position"), &AudioStreamPlayer3D::get_playback_position);
	ClassDB::bind_method(D_METHOD("update_output"), &AudioStreamPlayer3D::update_output);
	ClassDB::bind_method(D_METHOD("get_pitch_for_velocity", "relative_velocity"), &AudioStreamPlayer3D::get_pitch_for_velocity);
	ClassDB::bind_method(D_METHOD("get_output_gain"), &AudioStreamPlayer3D::get_output_gain);
	ClassDB::bind_method(D_METHOD("get_output_pan"), &AudioStreamPlayer3D::get_output_pan);
	ClassDB::bind_method(D_METHOD("get_output_pitch"), &AudioStreamPlayer3D::get_output_pitch);
	ADD_SIGNAL(MethodInfo("finished"));
}
