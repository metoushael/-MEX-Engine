#include "audio_server.h"

#include <cstring>

#include <SDL.h>

AudioServer *AudioServer::singleton = nullptr;

AudioServer::AudioServer() {
	singleton = this;
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
	float *out = (float *)p_stream;
	const size_t count = (size_t)p_len / sizeof(float);
	for (size_t i = 0; i < count; i++) {
		out[i] = 0.0f;
	}
	for (Voice &voice : self->voices) {
		if (!voice.data || voice.paused || voice.data->empty()) {
			continue;
		}
		const std::vector<float> &src = *voice.data;
		const float vol = voice.volume * self->master_volume;
		for (size_t i = 0; i < count; i++) {
			if (voice.position >= src.size()) {
				if (!voice.loop) {
					voice.data.reset(); // voix terminée
					break;
				}
				voice.position = 0;
			}
			out[i] += src[voice.position++] * vol;
		}
	}
	for (size_t i = 0; i < count; i++) {
		out[i] = out[i] < -1.0f ? -1.0f : (out[i] > 1.0f ? 1.0f : out[i]);
	}
}

int AudioServer::play(const SampleData &p_data, float p_volume, bool p_loop, double p_from_seconds) {
	if (!device || !p_data || p_data->empty()) {
		return 0;
	}
	Voice voice;
	voice.id = next_voice_id++;
	voice.data = p_data;
	voice.volume = p_volume;
	voice.loop = p_loop;
	size_t frame = (size_t)(std::max(0.0, p_from_seconds) * mix_rate);
	voice.position = std::min(frame * 2, p_data->size());
	_lock();
	// Nettoie les voix terminées.
	voices.erase(std::remove_if(voices.begin(), voices.end(), [](const Voice &v) { return !v.data; }), voices.end());
	voices.push_back(voice);
	_unlock();
	return voice.id;
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
			pos = (double)(v.position / 2) / (double)mix_rate;
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
