// AudioServer : mixeur audio simple (voix PCM flottantes stéréo) au-dessus de SDL.
#pragma once

#include "core/object/object.h"

class AudioServer : public Object {
	GDCLASS(AudioServer, Object)

public:
	typedef std::shared_ptr<const std::vector<float>> SampleData; // stéréo entrelacé

private:
	struct Voice {
		int id = 0;
		SampleData data;
		size_t position = 0; // en échantillons (pas en frames)
		float volume = 1.0f;
		bool loop = false;
		bool paused = false;
	};

	static AudioServer *singleton;

	uint32_t device = 0;
	int mix_rate = 44100;
	float master_volume = 1.0f;
	int next_voice_id = 1;
	std::vector<Voice> voices;

	static void _mix_callback(void *p_userdata, uint8_t *p_stream, int p_len);
	void _lock() const;
	void _unlock() const;

protected:
	static void _bind_methods() {}

public:
	static AudioServer *get_singleton() { return singleton; }

	Error init();
	void finish();
	bool is_available() const { return device != 0; }
	int get_mix_rate() const { return mix_rate; }

	int play(const SampleData &p_data, float p_volume, bool p_loop, double p_from_seconds = 0.0);
	void stop(int p_voice);
	void stop_all();
	bool is_playing(int p_voice) const;
	void set_voice_volume(int p_voice, float p_volume);
	void set_voice_paused(int p_voice, bool p_paused);
	double get_voice_position(int p_voice) const;
	void set_master_volume(float p_volume) { master_volume = p_volume; }
	float get_master_volume() const { return master_volume; }

	// Décode un fichier WAV en float stéréo à la fréquence du mixeur.
	bool decode_wav(const std::vector<uint8_t> &p_file, std::vector<float> &r_samples) const;

	AudioServer();
	~AudioServer();
};
