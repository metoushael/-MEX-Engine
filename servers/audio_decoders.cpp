// Décodeurs audio : Ogg Vorbis (stb_vorbis) et MP3 (dr_mp3), en plus du WAV lu par SDL.
// Les fichiers sont décodés en entier, en flottants stéréo à la fréquence du mixeur.

#include "audio_server.h"

#include <SDL.h>

#include <cstring>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb/stb_vorbis.c"
#undef L
#undef C
#undef R

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "dr_libs/dr_mp3.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// Convertit des échantillons PCM 16 bits entrelacés vers le format du mixeur.
bool AudioServer::convert_pcm16(const int16_t *p_samples, size_t p_frames, int p_channels, int p_rate, std::vector<float> &r_samples) const {
	r_samples.clear();
	if (!p_samples || p_frames == 0 || p_channels < 1 || p_rate < 1) {
		return false;
	}
	const size_t bytes = p_frames * (size_t)p_channels * sizeof(int16_t);
	SDL_AudioCVT cvt;
	const int res = SDL_BuildAudioCVT(&cvt, AUDIO_S16SYS, (Uint8)p_channels, p_rate, AUDIO_F32SYS, 2, mix_rate);
	if (res < 0) {
		print_error(String("Conversion audio impossible : ") + SDL_GetError());
		return false;
	}
	cvt.len = (int)bytes;
	cvt.buf = (Uint8 *)SDL_malloc(bytes * (size_t)cvt.len_mult);
	if (!cvt.buf) {
		return false;
	}
	memcpy(cvt.buf, p_samples, bytes);
	if (res > 0 && SDL_ConvertAudio(&cvt) != 0) {
		SDL_free(cvt.buf);
		print_error(String("Conversion audio impossible : ") + SDL_GetError());
		return false;
	}
	const size_t out_bytes = res > 0 ? (size_t)cvt.len_cvt : bytes;
	r_samples.resize(out_bytes / sizeof(float));
	memcpy(r_samples.data(), cvt.buf, r_samples.size() * sizeof(float));
	SDL_free(cvt.buf);
	return !r_samples.empty();
}

bool AudioServer::decode_ogg(const std::vector<uint8_t> &p_file, std::vector<float> &r_samples) const {
	if (p_file.empty()) {
		return false;
	}
	int channels = 0, rate = 0;
	short *decoded = nullptr;
	const int frames = stb_vorbis_decode_memory(p_file.data(), (int)p_file.size(), &channels, &rate, &decoded);
	if (frames <= 0 || !decoded) {
		print_error("Ogg Vorbis invalide.");
		if (decoded) {
			free(decoded);
		}
		return false;
	}
	const bool ok = convert_pcm16(decoded, (size_t)frames, channels, rate, r_samples);
	free(decoded);
	return ok;
}

bool AudioServer::decode_mp3(const std::vector<uint8_t> &p_file, std::vector<float> &r_samples) const {
	if (p_file.empty()) {
		return false;
	}
	drmp3_config config;
	memset(&config, 0, sizeof(config));
	drmp3_uint64 frames = 0;
	drmp3_int16 *decoded = drmp3_open_memory_and_read_pcm_frames_s16(p_file.data(), p_file.size(), &config, &frames, nullptr);
	if (!decoded || frames == 0) {
		print_error("MP3 invalide.");
		if (decoded) {
			drmp3_free(decoded, nullptr);
		}
		return false;
	}
	const bool ok = convert_pcm16(decoded, (size_t)frames, (int)config.channels, (int)config.sampleRate, r_samples);
	drmp3_free(decoded, nullptr);
	return ok;
}

// Reconnaît le format à l'en-tête du fichier, pas à son extension.
bool AudioServer::decode(const std::vector<uint8_t> &p_file, std::vector<float> &r_samples) const {
	if (p_file.size() < 4) {
		return false;
	}
	if (memcmp(p_file.data(), "RIFF", 4) == 0) {
		return decode_wav(p_file, r_samples);
	}
	if (memcmp(p_file.data(), "OggS", 4) == 0) {
		return decode_ogg(p_file, r_samples);
	}
	return decode_mp3(p_file, r_samples);
}
