// Effets audio : ressources posées sur un bus de l'AudioServer (AudioServer.add_bus_effect).
// Chaque effet traite le signal stéréo du bus, bloc par bloc, dans le fil audio.
#pragma once

#include "core/io/resource.h"

class AudioEffect : public Resource {
	GDCLASS(AudioEffect, Resource)

protected:
	static void _bind_methods() {}

public:
	// État propre à un bus (mémoire des filtres, lignes à retard).
	struct State {
		std::vector<float> buffer;
		size_t position = 0;
		float memory[16] = { 0 };
		bool ready = false;
	};
	// p_frames : tableau stéréo entrelacé de p_count images, modifié sur place.
	virtual void process(State &p_state, float *p_frames, size_t p_count, int p_mix_rate) const = 0;
};

// Gain fixe, en décibels.
class AudioEffectAmplify : public AudioEffect {
	GDCLASS(AudioEffectAmplify, AudioEffect)
	float volume_db = 0.0f;

protected:
	static void _bind_methods();

public:
	void set_volume_db(float p_db) { volume_db = p_db; }
	float get_volume_db() const { return volume_db; }
	virtual void process(State &p_state, float *p_frames, size_t p_count, int p_mix_rate) const override;
};

// Filtres passe-bas (son étouffé : sous l'eau, derrière un mur) et passe-haut (son de radio).
class AudioEffectFilter : public AudioEffect {
	GDCLASS(AudioEffectFilter, AudioEffect)

protected:
	float cutoff_hz = 2000.0f;
	float resonance = 0.5f;
	bool high_pass = false;
	static void _bind_methods();

public:
	void set_cutoff(float p_hz) { cutoff_hz = p_hz; }
	float get_cutoff() const { return cutoff_hz; }
	void set_resonance(float p_resonance) { resonance = p_resonance; }
	float get_resonance() const { return resonance; }
	virtual void process(State &p_state, float *p_frames, size_t p_count, int p_mix_rate) const override;
};

class AudioEffectLowPassFilter : public AudioEffectFilter {
	GDCLASS(AudioEffectLowPassFilter, AudioEffectFilter)

protected:
	static void _bind_methods() {}
};

class AudioEffectHighPassFilter : public AudioEffectFilter {
	GDCLASS(AudioEffectHighPassFilter, AudioEffectFilter)

protected:
	static void _bind_methods() {}

public:
	AudioEffectHighPassFilter() { high_pass = true; }
};

// Écho : le signal est répété après un délai, de plus en plus faible.
class AudioEffectDelay : public AudioEffect {
	GDCLASS(AudioEffectDelay, AudioEffect)
	float delay_ms = 250.0f;
	float feedback = 0.4f;
	float dry = 1.0f;
	float wet = 0.5f;

protected:
	static void _bind_methods();

public:
	void set_delay_ms(float p_ms) { delay_ms = p_ms; }
	float get_delay_ms() const { return delay_ms; }
	void set_feedback(float p_feedback) { feedback = p_feedback; }
	float get_feedback() const { return feedback; }
	void set_dry(float p_dry) { dry = p_dry; }
	float get_dry() const { return dry; }
	void set_wet(float p_wet) { wet = p_wet; }
	float get_wet() const { return wet; }
	virtual void process(State &p_state, float *p_frames, size_t p_count, int p_mix_rate) const override;
};

// Réverbération (salle, grotte) : quatre filtres en peigne et deux passe-tout par canal.
class AudioEffectReverb : public AudioEffect {
	GDCLASS(AudioEffectReverb, AudioEffect)
	float room_size = 0.8f;
	float damping = 0.5f;
	float dry = 1.0f;
	float wet = 0.5f;

protected:
	static void _bind_methods();

public:
	void set_room_size(float p_size) { room_size = p_size; }
	float get_room_size() const { return room_size; }
	void set_damping(float p_damping) { damping = p_damping; }
	float get_damping() const { return damping; }
	void set_dry(float p_dry) { dry = p_dry; }
	float get_dry() const { return dry; }
	void set_wet(float p_wet) { wet = p_wet; }
	float get_wet() const { return wet; }
	virtual void process(State &p_state, float *p_frames, size_t p_count, int p_mix_rate) const override;
};

// Saturation douce.
class AudioEffectDistortion : public AudioEffect {
	GDCLASS(AudioEffectDistortion, AudioEffect)
	float drive = 0.5f;
	float post_gain_db = 0.0f;

protected:
	static void _bind_methods();

public:
	void set_drive(float p_drive) { drive = p_drive; }
	float get_drive() const { return drive; }
	void set_post_gain(float p_db) { post_gain_db = p_db; }
	float get_post_gain() const { return post_gain_db; }
	virtual void process(State &p_state, float *p_frames, size_t p_count, int p_mix_rate) const override;
};

// Limiteur : empêche le signal de dépasser un plafond (évite la saturation du mixage).
class AudioEffectLimiter : public AudioEffect {
	GDCLASS(AudioEffectLimiter, AudioEffect)
	float ceiling_db = -0.3f;
	float release_ms = 100.0f;

protected:
	static void _bind_methods();

public:
	void set_ceiling_db(float p_db) { ceiling_db = p_db; }
	float get_ceiling_db() const { return ceiling_db; }
	void set_release_ms(float p_ms) { release_ms = p_ms; }
	float get_release_ms() const { return release_ms; }
	virtual void process(State &p_state, float *p_frames, size_t p_count, int p_mix_rate) const override;
};
