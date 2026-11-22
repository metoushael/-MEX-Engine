// Son placé dans l'espace 3D et auditeur.
//
// Adapté de scene/3d/audio_stream_player_3d.{h,cpp} et scene/3d/audio_listener_3d.{h,cpp} du
// moteur Godot 4.3 (licence MIT, voir thirdparty/godot/LICENSE.txt) : modèles d'atténuation,
// taille d'unité, plafond de volume, répartition entre haut-parleurs (classe Spcap), cône
// d'émission et effet Doppler. Différences avec Godot : sortie stéréo seulement ; hors du cône
// d'émission et avec la distance, le son est baissé au lieu d'être filtré dans les aigus ; pas
// de zones de réverbération (Area3D).
#pragma once

#include "scene/3d/node_3d.h"
#include "scene/audio/audio_stream_player.h"

// Point d'écoute. Sans AudioListener3D courant, on écoute depuis la caméra 3D courante.
class AudioListener3D : public Node3D {
	GDCLASS(AudioListener3D, Node3D)

	bool current = false;
	static ObjectID current_listener;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_current(bool p_current);
	bool get_current_flag() const { return current; }
	void make_current();
	void clear_current();
	bool is_current() const;

	// Auditeur courant de l'arbre : AudioListener3D courant, sinon caméra courante, sinon nul.
	static Node3D *get_listener(const Node *p_from);
};

class AudioStreamPlayer3D : public Node3D {
	GDCLASS(AudioStreamPlayer3D, Node3D)

public:
	enum AttenuationModel {
		ATTENUATION_INVERSE_DISTANCE,
		ATTENUATION_INVERSE_SQUARE_DISTANCE,
		ATTENUATION_LOGARITHMIC,
		ATTENUATION_DISABLED,
	};
	enum DopplerTracking {
		DOPPLER_TRACKING_DISABLED,
		DOPPLER_TRACKING_IDLE_STEP,
	};

private:
	Ref<AudioStream> stream;
	int attenuation_model = ATTENUATION_INVERSE_DISTANCE;
	real_t volume_db = 0.0f;
	real_t unit_size = 10.0f;
	real_t max_db = 3.0f;
	real_t pitch_scale = 1.0f;
	bool autoplay = false;
	bool loop = false;
	real_t max_distance = 0.0f; // 0 : pas de limite
	real_t panning_strength = 1.0f;
	String bus = "Master";
	bool emission_angle_enabled = false;
	real_t emission_angle = 45.0f;
	real_t emission_angle_filter_attenuation_db = -12.0f;
	int doppler_tracking = DOPPLER_TRACKING_DISABLED;

	int voice = 0;
	bool was_playing = false;
	bool has_previous = false;
	Vector3 previous_position;
	Vector3 previous_listener_position;
	real_t last_gain = 0.0f; // pour les essais et le débogage
	real_t last_pan = 0.0f;
	real_t last_pitch = 1.0f;

	real_t _get_attenuation_db(real_t p_distance) const;
	void _update_voice(double p_delta);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_stream(const Ref<AudioStream> &p_stream);
	Ref<AudioStream> get_stream() const { return stream; }
	void set_attenuation_model(int p_model) { attenuation_model = std::min(std::max(p_model, 0), (int)ATTENUATION_DISABLED); }
	int get_attenuation_model() const { return attenuation_model; }
	void set_volume_db(real_t p_db) { volume_db = p_db; }
	real_t get_volume_db() const { return volume_db; }
	void set_unit_size(real_t p_size) { unit_size = std::max(p_size, (real_t)0.1); }
	real_t get_unit_size() const { return unit_size; }
	void set_max_db(real_t p_db) { max_db = p_db; }
	real_t get_max_db() const { return max_db; }
	void set_pitch_scale(real_t p_pitch) { pitch_scale = std::max(p_pitch, (real_t)0.01); }
	real_t get_pitch_scale() const { return pitch_scale; }
	void set_autoplay(bool p_autoplay) { autoplay = p_autoplay; }
	bool is_autoplay_enabled() const { return autoplay; }
	void set_loop(bool p_loop) { loop = p_loop; }
	bool has_loop() const { return loop; }
	void set_max_distance(real_t p_distance) { max_distance = std::max(p_distance, (real_t)0); }
	real_t get_max_distance() const { return max_distance; }
	void set_panning_strength(real_t p_strength) { panning_strength = std::max(p_strength, (real_t)0); }
	real_t get_panning_strength() const { return panning_strength; }
	void set_bus(const String &p_bus) { bus = p_bus; }
	String get_bus() const { return bus; }
	void set_emission_angle_enabled(bool p_enabled) { emission_angle_enabled = p_enabled; }
	bool is_emission_angle_enabled() const { return emission_angle_enabled; }
	void set_emission_angle(real_t p_degrees) { emission_angle = std::min(std::max(p_degrees, (real_t)0.1), (real_t)90); }
	real_t get_emission_angle() const { return emission_angle; }
	void set_emission_angle_filter_attenuation_db(real_t p_db) { emission_angle_filter_attenuation_db = p_db; }
	real_t get_emission_angle_filter_attenuation_db() const { return emission_angle_filter_attenuation_db; }
	void set_doppler_tracking(int p_mode) { doppler_tracking = p_mode ? DOPPLER_TRACKING_IDLE_STEP : DOPPLER_TRACKING_DISABLED; }
	int get_doppler_tracking() const { return doppler_tracking; }

	void play(double p_from = 0.0);
	void stop();
	bool is_playing() const;
	double get_playback_position() const;

	// Calcule le volume, la balance (-1 gauche, 1 droite) et la hauteur qu'aurait le son pour
	// un auditeur donné : sert au mixage, aux scripts et aux essais (aucun son n'est requis).
	void compute_output(const Transform3D &p_listener, const Vector3 &p_relative_velocity, real_t &r_gain, real_t &r_pan, real_t &r_pitch) const;
	real_t get_output_gain() const { return last_gain; }
	real_t get_output_pan() const { return last_pan; }
	real_t get_output_pitch() const { return last_pitch; }
	void update_output(); // recalcule tout de suite (sans attendre l'image suivante)
	// Hauteur entendue si le son se déplace à cette vitesse (m/s) par rapport à l'auditeur.
	real_t get_pitch_for_velocity(const Vector3 &p_relative_velocity) const;
};
