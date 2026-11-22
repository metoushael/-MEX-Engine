#include "audio_effects.h"

#include <cmath>

static inline float _db_to_linear(float p_db) {
	return std::pow(10.0f, p_db / 20.0f);
}

#define BIND_EFFECT_PROPERTY(m_class, m_name, m_setter, m_getter, m_range) \
	ClassDB::bind_method(D_METHOD(#m_setter, "value"), &m_class::m_setter); \
	ClassDB::bind_method(D_METHOD(#m_getter), &m_class::m_getter); \
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, m_name, PROPERTY_HINT_RANGE, m_range), #m_setter, #m_getter)

// --- Amplify ---
void AudioEffectAmplify::process(State &, float *p_frames, size_t p_count, int) const {
	const float gain = _db_to_linear(volume_db);
	for (size_t i = 0; i < p_count * 2; i++) {
		p_frames[i] *= gain;
	}
}

void AudioEffectAmplify::_bind_methods() {
	BIND_EFFECT_PROPERTY(AudioEffectAmplify, "volume_db", set_volume_db, get_volume_db, "-80,24,0.1");
}

// --- Filtres (biquad de Robert Bristow-Johnson) ---
void AudioEffectFilter::process(State &p_state, float *p_frames, size_t p_count, int p_mix_rate) const {
	const float nyquist = p_mix_rate * 0.5f;
	const float cutoff = std::min(std::max(cutoff_hz, 10.0f), nyquist * 0.95f);
	const float q = 0.5f + std::min(std::max(resonance, 0.0f), 1.0f) * 4.0f * 0.25f + 0.2f;
	const float w0 = 2.0f * 3.14159265f * cutoff / (float)p_mix_rate;
	const float cs = std::cos(w0), alpha = std::sin(w0) / (2.0f * q);
	float b0, b1, b2;
	if (high_pass) {
		b0 = (1.0f + cs) * 0.5f;
		b1 = -(1.0f + cs);
		b2 = b0;
	} else {
		b0 = (1.0f - cs) * 0.5f;
		b1 = 1.0f - cs;
		b2 = b0;
	}
	const float a0 = 1.0f + alpha, a1 = -2.0f * cs, a2 = 1.0f - alpha;
	b0 /= a0;
	b1 /= a0;
	b2 /= a0;
	const float na1 = a1 / a0, na2 = a2 / a0;
	// memory : [x1, x2, y1, y2] pour chaque canal.
	for (int channel = 0; channel < 2; channel++) {
		float *m = &p_state.memory[channel * 4];
		for (size_t i = 0; i < p_count; i++) {
			const float x = p_frames[i * 2 + channel];
			const float y = b0 * x + b1 * m[0] + b2 * m[1] - na1 * m[2] - na2 * m[3];
			m[1] = m[0];
			m[0] = x;
			m[3] = m[2];
			m[2] = y;
			p_frames[i * 2 + channel] = y;
		}
		if (!std::isfinite(m[2])) {
			m[0] = m[1] = m[2] = m[3] = 0.0f;
		}
	}
}

void AudioEffectFilter::_bind_methods() {
	BIND_EFFECT_PROPERTY(AudioEffectFilter, "cutoff_hz", set_cutoff, get_cutoff, "10,20000,1");
	BIND_EFFECT_PROPERTY(AudioEffectFilter, "resonance", set_resonance, get_resonance, "0,1,0.01");
}

// --- Écho ---
void AudioEffectDelay::process(State &p_state, float *p_frames, size_t p_count, int p_mix_rate) const {
	const size_t max_frames = (size_t)p_mix_rate * 2; // jusqu'à deux secondes
	if (p_state.buffer.size() != max_frames * 2) {
		p_state.buffer.assign(max_frames * 2, 0.0f);
		p_state.position = 0;
	}
	const size_t delay = std::min(std::max((size_t)(delay_ms * 0.001f * p_mix_rate), (size_t)1), max_frames - 1);
	const float fb = std::min(std::max(feedback, 0.0f), 0.95f);
	for (size_t i = 0; i < p_count; i++) {
		const size_t write = p_state.position;
		const size_t read = (write + max_frames - delay) % max_frames;
		for (int channel = 0; channel < 2; channel++) {
			const float in = p_frames[i * 2 + channel];
			const float echo = p_state.buffer[read * 2 + channel];
			p_state.buffer[write * 2 + channel] = in + echo * fb;
			p_frames[i * 2 + channel] = in * dry + echo * wet;
		}
		p_state.position = (write + 1) % max_frames;
	}
}

void AudioEffectDelay::_bind_methods() {
	BIND_EFFECT_PROPERTY(AudioEffectDelay, "delay_ms", set_delay_ms, get_delay_ms, "1,2000,1");
	BIND_EFFECT_PROPERTY(AudioEffectDelay, "feedback", set_feedback, get_feedback, "0,0.95,0.01");
	BIND_EFFECT_PROPERTY(AudioEffectDelay, "dry", set_dry, get_dry, "0,1,0.01");
	BIND_EFFECT_PROPERTY(AudioEffectDelay, "wet", set_wet, get_wet, "0,1,0.01");
}

// --- Réverbération ---
void AudioEffectReverb::process(State &p_state, float *p_frames, size_t p_count, int p_mix_rate) const {
	// Longueurs (en images à 44,1 kHz) des peignes et des passe-tout, comme Freeverb.
	static const int COMB[4] = { 1116, 1277, 1422, 1557 };
	static const int ALLPASS[2] = { 556, 341 };
	static const int SPREAD = 23; // décalage du canal droit : élargit l'image stéréo
	const float rate_scale = (float)p_mix_rate / 44100.0f;
	const size_t slot = (size_t)(1600 * rate_scale) + 64; // taille réservée à chaque ligne
	const size_t lines = 12; // (4 peignes + 2 passe-tout) × 2 canaux
	if (p_state.buffer.size() != slot * lines) {
		p_state.buffer.assign(slot * lines, 0.0f);
		p_state.position = 0;
		for (float &value : p_state.memory) {
			value = 0.0f;
		}
	}
	const float fb = 0.7f + std::min(std::max(room_size, 0.0f), 1.0f) * 0.28f;
	const float damp = std::min(std::max(damping, 0.0f), 1.0f) * 0.4f;
	const size_t n = p_state.position;
	for (size_t i = 0; i < p_count; i++) {
		const float input = (p_frames[i * 2] + p_frames[i * 2 + 1]) * 0.015f;
		for (int channel = 0; channel < 2; channel++) {
			float out = 0.0f;
			for (int c = 0; c < 4; c++) {
				const size_t length = (size_t)((COMB[c] + channel * SPREAD) * rate_scale);
				float *line = &p_state.buffer[slot * (size_t)(channel * 6 + c)];
				float &filter = p_state.memory[channel * 4 + c];
				const size_t index = (n + i) % length;
				const float value = line[index];
				filter = value * (1.0f - damp) + filter * damp;
				line[index] = input + filter * fb;
				out += value;
			}
			for (int a = 0; a < 2; a++) {
				const size_t length = (size_t)((ALLPASS[a] + channel * SPREAD) * rate_scale);
				float *line = &p_state.buffer[slot * (size_t)(channel * 6 + 4 + a)];
				const size_t index = (n + i) % length;
				const float value = line[index];
				line[index] = out + value * 0.5f;
				out = value - out;
			}
			p_frames[i * 2 + channel] = p_frames[i * 2 + channel] * dry + out * wet;
		}
	}
	p_state.position = n + p_count;
}

void AudioEffectReverb::_bind_methods() {
	BIND_EFFECT_PROPERTY(AudioEffectReverb, "room_size", set_room_size, get_room_size, "0,1,0.01");
	BIND_EFFECT_PROPERTY(AudioEffectReverb, "damping", set_damping, get_damping, "0,1,0.01");
	BIND_EFFECT_PROPERTY(AudioEffectReverb, "dry", set_dry, get_dry, "0,1,0.01");
	BIND_EFFECT_PROPERTY(AudioEffectReverb, "wet", set_wet, get_wet, "0,1,0.01");
}

// --- Distorsion ---
void AudioEffectDistortion::process(State &, float *p_frames, size_t p_count, int) const {
	const float amount = 1.0f + std::min(std::max(drive, 0.0f), 1.0f) * 24.0f;
	const float normalize = 1.0f / std::tanh(amount);
	const float gain = _db_to_linear(post_gain_db);
	for (size_t i = 0; i < p_count * 2; i++) {
		p_frames[i] = std::tanh(p_frames[i] * amount) * normalize * gain;
	}
}

void AudioEffectDistortion::_bind_methods() {
	BIND_EFFECT_PROPERTY(AudioEffectDistortion, "drive", set_drive, get_drive, "0,1,0.01");
	BIND_EFFECT_PROPERTY(AudioEffectDistortion, "post_gain", set_post_gain, get_post_gain, "-60,24,0.1");
}

// --- Limiteur ---
void AudioEffectLimiter::process(State &p_state, float *p_frames, size_t p_count, int p_mix_rate) const {
	const float ceiling = _db_to_linear(ceiling_db);
	const float release = std::exp(-1.0f / (std::max(release_ms, 1.0f) * 0.001f * (float)p_mix_rate));
	float &gain = p_state.memory[0];
	if (!p_state.ready) {
		p_state.ready = true;
		gain = 1.0f;
	}
	for (size_t i = 0; i < p_count; i++) {
		const float peak = std::max(std::fabs(p_frames[i * 2]), std::fabs(p_frames[i * 2 + 1]));
		const float wanted = peak * gain > ceiling ? ceiling / peak : 1.0f;
		// Attaque immédiate, relâchement progressif.
		gain = wanted < gain ? wanted : wanted + (gain - wanted) * release;
		p_frames[i * 2] *= gain;
		p_frames[i * 2 + 1] *= gain;
	}
}

void AudioEffectLimiter::_bind_methods() {
	BIND_EFFECT_PROPERTY(AudioEffectLimiter, "ceiling_db", set_ceiling_db, get_ceiling_db, "-20,0,0.1");
	BIND_EFFECT_PROPERTY(AudioEffectLimiter, "release_ms", set_release_ms, get_release_ms, "1,2000,1");
}
