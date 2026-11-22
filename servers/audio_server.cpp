#include "audio_server.h"

#include "core/string/ustring.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <SDL.h>

AudioServer *AudioServer::singleton = nullptr;

AudioServer::AudioServer() {
	singleton = this;
	Bus master;
	master.name = "Master";
	buses.push_back(master);
}

AudioServer::~AudioServer() {
	finish();
	if (singleton == this) {
		singleton = nullptr;
	}
}

void AudioServer::_lock() const {
	if (device) {
		SDL_LockAudioDevice(device);
	}
}

void AudioServer::_unlock() const {
	if (device) {
		SDL_UnlockAudioDevice(device);
	}
}

Error AudioServer::init() {
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		print_warning(String("Audio indisponible : ") + SDL_GetError());
		return ERR_UNAVAILABLE;
	}
	SDL_AudioSpec want, have;
	SDL_zero(want);
	want.freq = mix_rate;
	want.format = AUDIO_F32SYS;
	want.channels = 2;
	want.samples = 1024;
	want.callback = &AudioServer::_mix_callback;
	want.userdata = this;
	device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
	if (!device) {
		print_warning(String("Audio indisponible : ") + SDL_GetError());
		return ERR_UNAVAILABLE;
	}
	mix_rate = have.freq;
	SDL_PauseAudioDevice(device, 0);
	return OK;
}

void AudioServer::finish() {
	if (device) {
		SDL_CloseAudioDevice(device);
		device = 0;
	}
	voices.clear();
}

void AudioServer::_mix_callback(void *p_userdata, uint8_t *p_stream, int p_len) {
	AudioServer *self = (AudioServer *)p_userdata;
	self->_mix((float *)p_stream, (size_t)p_len / (sizeof(float) * 2));
}

static inline float _db_to_linear(float p_db) {
	return p_db <= -80.0f ? 0.0f : std::pow(10.0f, p_db / 20.0f);
}

void AudioServer::_mix(float *p_out, size_t p_frames) {
	const size_t count = p_frames * 2;
	for (Bus &bus : buses) {
		bus.buffer.assign(count, 0.0f);
	}
	// --- Voix : chacune s'ajoute au tampon de son bus ---
	for (Voice &voice : voices) {
		if (!voice.data || voice.paused || voice.data->size() < 2) {
			continue;
		}
		const std::vector<float> &src = *voice.data;
		const size_t src_frames = src.size() / 2;
		float *dst = buses[(size_t)(voice.bus >= 0 && voice.bus < (int)buses.size() ? voice.bus : 0)].buffer.data();
		// Panoramique à puissance constante au centre.
		const float left = voice.volume * std::min(1.0f, 1.0f - voice.pan);
		const float right = voice.volume * std::min(1.0f, 1.0f + voice.pan);
		const double step = (double)std::min(std::max(voice.pitch, 0.01f), 16.0f);
		for (size_t i = 0; i < p_frames; i++) {
			if (voice.position >= (double)src_frames) {
				if (!voice.loop) {
					voice.data.reset(); // voix terminée
					break;
				}
				voice.position = std::fmod(voice.position, (double)src_frames);
			}
			// Interpolation linéaire entre deux images : nécessaire dès que pitch != 1.
			const size_t index = (size_t)voice.position;
			const float t = (float)(voice.position - (double)index);
			const size_t next = index + 1 < src_frames ? index + 1 : (voice.loop ? 0 : index);
			dst[i * 2] += (src[index * 2] + (src[next * 2] - src[index * 2]) * t) * left;
			dst[i * 2 + 1] += (src[index * 2 + 1] + (src[next * 2 + 1] - src[index * 2 + 1]) * t) * right;
			voice.position += step;
		}
	}
	// --- Bus, du dernier au premier : effets, volume, puis envoi vers le bus de destination ---
	for (int b = (int)buses.size() - 1; b >= 0; b--) {
		Bus &bus = buses[(size_t)b];
		if (!bus.bypass) {
			for (BusEffect &entry : bus.effects) {
				if (entry.enabled && entry.effect.is_valid()) {
					entry.effect->process(entry.state, bus.buffer.data(), p_frames, mix_rate);
				}
			}
		}
		const float gain = bus.mute ? 0.0f : _db_to_linear(bus.volume_db) * (b == 0 ? master_volume : 1.0f);
		float peak = 0.0f;
		for (size_t i = 0; i < count; i++) {
			bus.buffer[i] *= gain;
			peak = std::max(peak, std::fabs(bus.buffer[i]));
		}
		bus.peak = peak;
		if (b > 0) {
			float *dst = buses[(size_t)(bus.send >= 0 && bus.send < b ? bus.send : 0)].buffer.data();
			for (size_t i = 0; i < count; i++) {
				dst[i] += bus.buffer[i];
			}
		}
	}
	const float *master = buses[0].buffer.data();
	for (size_t i = 0; i < count; i++) {
		p_out[i] = master[i] < -1.0f ? -1.0f : (master[i] > 1.0f ? 1.0f : master[i]);
	}
}

std::vector<float> AudioServer::mix_offline(int p_frames) {
	std::vector<float> out((size_t)std::max(p_frames, 0) * 2, 0.0f);
	if (!out.empty()) {
		_lock();
		_mix(out.data(), (size_t)p_frames);
		_unlock();
	}
	return out;
}

int AudioServer::play(const SampleData &p_data, float p_volume, bool p_loop, double p_from_seconds, int p_bus, float p_pitch) {
	if (!device || !p_data || p_data->empty()) {
		return 0;
	}
	Voice voice;
	voice.id = next_voice_id++;
	voice.data = p_data;
	voice.volume = p_volume;
	voice.loop = p_loop;
	voice.bus = p_bus;
	voice.pitch = p_pitch;
	voice.position = std::min(std::max(0.0, p_from_seconds) * mix_rate, (double)(p_data->size() / 2));
	_lock();
	// Nettoie les voix terminées.
	voices.erase(std::remove_if(voices.begin(), voices.end(), [](const Voice &v) { return !v.data; }), voices.end());
	voices.push_back(voice);
	_unlock();
	return voice.id;
}

void AudioServer::set_voice_pitch(int p_voice, float p_pitch) {
	_lock();
	for (Voice &v : voices) {
		if (v.id == p_voice) {
			v.pitch = p_pitch;
		}
	}
	_unlock();
}

void AudioServer::set_voice_pan(int p_voice, float p_pan) {
	_lock();
	for (Voice &v : voices) {
		if (v.id == p_voice) {
			v.pan = std::min(std::max(p_pan, -1.0f), 1.0f);
		}
	}
	_unlock();
}

// ---------------------------------------------------------------------------
// Bus
// ---------------------------------------------------------------------------
void AudioServer::add_bus(const String &p_name) {
	Bus bus;
	bus.name = p_name.empty() ? "Bus " + itos((int64_t)buses.size()) : p_name;
	_lock();
	buses.push_back(bus);
	_unlock();
}

void AudioServer::remove_bus(int p_bus) {
	ERR_FAIL_COND_MSG(p_bus <= 0 || p_bus >= (int)buses.size(), "AudioServer : ce bus ne peut pas être retiré.");
	_lock();
	buses.erase(buses.begin() + p_bus);
	for (Bus &bus : buses) {
		bus.send = bus.send == p_bus ? 0 : (bus.send > p_bus ? bus.send - 1 : bus.send);
	}
	for (Voice &voice : voices) {
		voice.bus = voice.bus == p_bus ? 0 : (voice.bus > p_bus ? voice.bus - 1 : voice.bus);
	}
	_unlock();
}

int AudioServer::get_bus_index(const String &p_name) const {
	for (size_t i = 0; i < buses.size(); i++) {
		if (buses[i].name == p_name) {
			return (int)i;
		}
	}
	return -1;
}

String AudioServer::get_bus_name(int p_bus) const {
	ERR_FAIL_INDEX_V(p_bus, buses.size(), String());
	return buses[(size_t)p_bus].name;
}

void AudioServer::set_bus_name(int p_bus, const String &p_name) {
	ERR_FAIL_INDEX(p_bus, buses.size());
	ERR_FAIL_COND_MSG(p_bus == 0, "AudioServer : le bus « Master » ne se renomme pas.");
	buses[(size_t)p_bus].name = p_name;
}

void AudioServer::set_bus_volume_db(int p_bus, float p_db) {
	ERR_FAIL_INDEX(p_bus, buses.size());
	buses[(size_t)p_bus].volume_db = p_db;
}

float AudioServer::get_bus_volume_db(int p_bus) const {
	ERR_FAIL_INDEX_V(p_bus, buses.size(), 0.0f);
	return buses[(size_t)p_bus].volume_db;
}

void AudioServer::set_bus_mute(int p_bus, bool p_mute) {
	ERR_FAIL_INDEX(p_bus, buses.size());
	buses[(size_t)p_bus].mute = p_mute;
}

bool AudioServer::is_bus_mute(int p_bus) const {
	ERR_FAIL_INDEX_V(p_bus, buses.size(), false);
	return buses[(size_t)p_bus].mute;
}

void AudioServer::set_bus_bypass_effects(int p_bus, bool p_bypass) {
	ERR_FAIL_INDEX(p_bus, buses.size());
	buses[(size_t)p_bus].bypass = p_bypass;
}

bool AudioServer::is_bus_bypassing_effects(int p_bus) const {
	ERR_FAIL_INDEX_V(p_bus, buses.size(), false);
	return buses[(size_t)p_bus].bypass;
}

void AudioServer::set_bus_send(int p_bus, const String &p_send) {
	ERR_FAIL_INDEX(p_bus, buses.size());
	const int target = get_bus_index(p_send);
	ERR_FAIL_COND_MSG(target < 0 || target >= p_bus, "AudioServer : un bus ne s'envoie que vers un bus placé avant lui.");
	buses[(size_t)p_bus].send = target;
}

String AudioServer::get_bus_send(int p_bus) const {
	ERR_FAIL_INDEX_V(p_bus, buses.size(), String());
	return p_bus == 0 ? String() : buses[(size_t)buses[(size_t)p_bus].send].name;
}

float AudioServer::get_bus_peak_volume_db(int p_bus) const {
	ERR_FAIL_INDEX_V(p_bus, buses.size(), -80.0f);
	const float peak = buses[(size_t)p_bus].peak;
	return peak <= 0.0001f ? -80.0f : 20.0f * std::log10(peak);
}

void AudioServer::add_bus_effect(int p_bus, const Ref<AudioEffect> &p_effect) {
	ERR_FAIL_INDEX(p_bus, buses.size());
	ERR_FAIL_COND_MSG(p_effect.is_null(), "AudioServer : effet nul.");
	BusEffect entry;
	entry.effect = p_effect;
	_lock();
	buses[(size_t)p_bus].effects.push_back(entry);
	_unlock();
}

void AudioServer::remove_bus_effect(int p_bus, int p_effect) {
	ERR_FAIL_INDEX(p_bus, buses.size());
	ERR_FAIL_INDEX(p_effect, buses[(size_t)p_bus].effects.size());
	_lock();
	buses[(size_t)p_bus].effects.erase(buses[(size_t)p_bus].effects.begin() + p_effect);
	_unlock();
}

int AudioServer::get_bus_effect_count(int p_bus) const {
	ERR_FAIL_INDEX_V(p_bus, buses.size(), 0);
	return (int)buses[(size_t)p_bus].effects.size();
}

Ref<AudioEffect> AudioServer::get_bus_effect(int p_bus, int p_effect) const {
	ERR_FAIL_INDEX_V(p_bus, buses.size(), Ref<AudioEffect>());
	ERR_FAIL_INDEX_V(p_effect, buses[(size_t)p_bus].effects.size(), Ref<AudioEffect>());
	return buses[(size_t)p_bus].effects[(size_t)p_effect].effect;
}

void AudioServer::set_bus_effect_enabled(int p_bus, int p_effect, bool p_enabled) {
	ERR_FAIL_INDEX(p_bus, buses.size());
	ERR_FAIL_INDEX(p_effect, buses[(size_t)p_bus].effects.size());
	buses[(size_t)p_bus].effects[(size_t)p_effect].enabled = p_enabled;
}

bool AudioServer::is_bus_effect_enabled(int p_bus, int p_effect) const {
	ERR_FAIL_INDEX_V(p_bus, buses.size(), false);
	ERR_FAIL_INDEX_V(p_effect, buses[(size_t)p_bus].effects.size(), false);
	return buses[(size_t)p_bus].effects[(size_t)p_effect].enabled;
}

void AudioServer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_mix_rate"), &AudioServer::get_mix_rate);
	ClassDB::bind_method(D_METHOD("get_bus_count"), &AudioServer::get_bus_count);
	ClassDB::bind_method(D_METHOD("add_bus", "name"), &AudioServer::add_bus, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("remove_bus", "bus"), &AudioServer::remove_bus);
	ClassDB::bind_method(D_METHOD("get_bus_index", "name"), &AudioServer::get_bus_index);
	ClassDB::bind_method(D_METHOD("get_bus_name", "bus"), &AudioServer::get_bus_name);
	ClassDB::bind_method(D_METHOD("set_bus_name", "bus", "name"), &AudioServer::set_bus_name);
	ClassDB::bind_method(D_METHOD("set_bus_volume_db", "bus", "db"), &AudioServer::set_bus_volume_db);
	ClassDB::bind_method(D_METHOD("get_bus_volume_db", "bus"), &AudioServer::get_bus_volume_db);
	ClassDB::bind_method(D_METHOD("set_bus_mute", "bus", "mute"), &AudioServer::set_bus_mute);
	ClassDB::bind_method(D_METHOD("is_bus_mute", "bus"), &AudioServer::is_bus_mute);
	ClassDB::bind_method(D_METHOD("set_bus_bypass_effects", "bus", "bypass"), &AudioServer::set_bus_bypass_effects);
	ClassDB::bind_method(D_METHOD("is_bus_bypassing_effects", "bus"), &AudioServer::is_bus_bypassing_effects);
	ClassDB::bind_method(D_METHOD("set_bus_send", "bus", "send"), &AudioServer::set_bus_send);
	ClassDB::bind_method(D_METHOD("get_bus_send", "bus"), &AudioServer::get_bus_send);
	ClassDB::bind_method(D_METHOD("get_bus_peak_volume_db", "bus"), &AudioServer::get_bus_peak_volume_db);
	ClassDB::bind_method(D_METHOD("add_bus_effect", "bus", "effect"), &AudioServer::add_bus_effect);
	ClassDB::bind_method(D_METHOD("remove_bus_effect", "bus", "effect"), &AudioServer::remove_bus_effect);
	ClassDB::bind_method(D_METHOD("get_bus_effect_count", "bus"), &AudioServer::get_bus_effect_count);
	ClassDB::bind_method(D_METHOD("get_bus_effect", "bus", "effect"), &AudioServer::get_bus_effect);
	ClassDB::bind_method(D_METHOD("set_bus_effect_enabled", "bus", "effect", "enabled"), &AudioServer::set_bus_effect_enabled);
	ClassDB::bind_method(D_METHOD("is_bus_effect_enabled", "bus", "effect"), &AudioServer::is_bus_effect_enabled);
	ClassDB::bind_method(D_METHOD("set_master_volume", "volume"), &AudioServer::set_master_volume);
	ClassDB::bind_method(D_METHOD("get_master_volume"), &AudioServer::get_master_volume);
	ClassDB::bind_method(D_METHOD("stop_all"), &AudioServer::stop_all);
}

void AudioServer::stop(int p_voice) {
	_lock();
	for (Voice &v : voices) {
		if (v.id == p_voice) {
			v.data.reset();
		}
	}
	_unlock();
}

void AudioServer::stop_all() {
	_lock();
	voices.clear();
	_unlock();
}

bool AudioServer::is_playing(int p_voice) const {
	bool playing = false;
	_lock();
	for (const Voice &v : voices) {
		if (v.id == p_voice && v.data) {
			playing = true;
		}
	}
	_unlock();
	return playing;
}

void AudioServer::set_voice_volume(int p_voice, float p_volume) {
	_lock();
	for (Voice &v : voices) {
		if (v.id == p_voice) {
			v.volume = p_volume;
		}
	}
	_unlock();
}

void AudioServer::set_voice_paused(int p_voice, bool p_paused) {
	_lock();
	for (Voice &v : voices) {
		if (v.id == p_voice) {
			v.paused = p_paused;
		}
	}
	_unlock();
}

double AudioServer::get_voice_position(int p_voice) const {
	double pos = 0;
	_lock();
	for (const Voice &v : voices) {
		if (v.id == p_voice && v.data) {
			pos = v.position / (double)mix_rate;
		}
	}
	_unlock();
	return pos;
}

bool AudioServer::decode_wav(const std::vector<uint8_t> &p_file, std::vector<float> &r_samples) const {
	if (p_file.empty()) {
		return false;
	}
	SDL_RWops *rw = SDL_RWFromConstMem(p_file.data(), (int)p_file.size());
	SDL_AudioSpec spec;
	Uint8 *buffer = nullptr;
	Uint32 length = 0;
	if (!SDL_LoadWAV_RW(rw, 1, &spec, &buffer, &length)) {
		print_error(String("WAV invalide : ") + SDL_GetError());
		return false;
	}
	SDL_AudioCVT cvt;
	int res = SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_F32SYS, 2, mix_rate);
	if (res < 0) {
		SDL_FreeWAV(buffer);
		print_error(String("Conversion audio impossible : ") + SDL_GetError());
		return false;
	}
	if (res == 0) { // déjà au bon format
		r_samples.resize(length / sizeof(float));
		memcpy(r_samples.data(), buffer, r_samples.size() * sizeof(float));
		SDL_FreeWAV(buffer);
		return true;
	}
	cvt.len = (int)length;
	cvt.buf = (Uint8 *)SDL_malloc((size_t)cvt.len * cvt.len_mult);
	memcpy(cvt.buf, buffer, length);
	SDL_FreeWAV(buffer);
	if (SDL_ConvertAudio(&cvt) != 0) {
		SDL_free(cvt.buf);
		print_error(String("Conversion audio impossible : ") + SDL_GetError());
		return false;
	}
	r_samples.resize((size_t)cvt.len_cvt / sizeof(float));
	memcpy(r_samples.data(), cvt.buf, r_samples.size() * sizeof(float));
	SDL_free(cvt.buf);
	return true;
}
