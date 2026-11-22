// AudioServer : mixeur audio (voix PCM flottantes stéréo, bus et effets) au-dessus de SDL.
#pragma once

#include "core/object/object.h"
#include "servers/audio_effects.h"

class AudioServer : public Object {
	GDCLASS(AudioServer, Object)

public:
	typedef std::shared_ptr<const std::vector<float>> SampleData; // stéréo entrelacé

private:
	struct Voice {
		int id = 0;
		SampleData data;
		double position = 0; // en images (une image = un échantillon gauche + un droit)
		float volume = 1.0f;
		float pitch = 1.0f;
		float pan = 0.0f; // -1 : gauche, 0 : centre, 1 : droite
		int bus = 0;
		bool loop = false;
		bool paused = false;
	};

	struct BusEffect {
		Ref<AudioEffect> effect;
		AudioEffect::State state;
		bool enabled = true;
	};

	struct Bus {
		String name;
		float volume_db = 0.0f;
		bool mute = false;
		bool bypass = false;
		int send = 0; // bus de destination (toujours d'indice inférieur)
		std::vector<BusEffect> effects;
		std::vector<float> buffer;
		float peak = 0.0f;
	};

	static AudioServer *singleton;

	uint32_t device = 0;
	int mix_rate = 44100;
	float master_volume = 1.0f;
	int next_voice_id = 1;
	std::vector<Voice> voices;

public:
	int get_active_voice_count() const { return (int)voices.size(); }

private:
	std::vector<Bus> buses; // buses[0] : « Master », toujours présent

	static void _mix_callback(void *p_userdata, uint8_t *p_stream, int p_len);
	void _mix(float *p_out, size_t p_frames);
	void _lock() const;
	void _unlock() const;

protected:
	static void _bind_methods();

public:
	static AudioServer *get_singleton() { return singleton; }

	Error init();
	void finish();
	bool is_available() const { return device != 0; }
	int get_mix_rate() const { return mix_rate; }

	int play(const SampleData &p_data, float p_volume, bool p_loop, double p_from_seconds = 0.0, int p_bus = 0, float p_pitch = 1.0f);
	void stop(int p_voice);
	void stop_all();
	bool is_playing(int p_voice) const;
	void set_voice_volume(int p_voice, float p_volume);
	void set_voice_pitch(int p_voice, float p_pitch);
	void set_voice_pan(int p_voice, float p_pan);
	void set_voice_paused(int p_voice, bool p_paused);
	double get_voice_position(int p_voice) const;
	void set_master_volume(float p_volume) { master_volume = p_volume; }
	float get_master_volume() const { return master_volume; }

	// --- Bus ---
	// Chaque son joue dans un bus ; un bus a son volume, ses effets, et se déverse dans un
	// autre bus (« Master » par défaut). Exemple : un bus « Musique » et un bus « Effets »
	// pour deux réglages de volume dans les options du jeu.
	int get_bus_count() const { return (int)buses.size(); }
	void add_bus(const String &p_name = String());
	void remove_bus(int p_bus);
	int get_bus_index(const String &p_name) const; // -1 si inconnu
	String get_bus_name(int p_bus) const;
	void set_bus_name(int p_bus, const String &p_name);
	void set_bus_volume_db(int p_bus, float p_db);
	float get_bus_volume_db(int p_bus) const;
	void set_bus_mute(int p_bus, bool p_mute);
	bool is_bus_mute(int p_bus) const;
	void set_bus_bypass_effects(int p_bus, bool p_bypass);
	bool is_bus_bypassing_effects(int p_bus) const;
	void set_bus_send(int p_bus, const String &p_send);
	String get_bus_send(int p_bus) const;
	float get_bus_peak_volume_db(int p_bus) const; // niveau de crête du dernier bloc mixé
	void add_bus_effect(int p_bus, const Ref<AudioEffect> &p_effect);
	void remove_bus_effect(int p_bus, int p_effect);
	int get_bus_effect_count(int p_bus) const;
	Ref<AudioEffect> get_bus_effect(int p_bus, int p_effect) const;
	void set_bus_effect_enabled(int p_bus, int p_effect, bool p_enabled);
	bool is_bus_effect_enabled(int p_bus, int p_effect) const;
	// Mixe p_frames images sans carte son (essais automatiques, export d'un fichier).
	std::vector<float> mix_offline(int p_frames);

	// Décodent un fichier en float stéréo à la fréquence du mixeur (audio_decoders.cpp pour
	// Ogg Vorbis et MP3). decode() reconnaît le format à l'en-tête du fichier.
	bool decode(const std::vector<uint8_t> &p_file, std::vector<float> &r_samples) const;
	bool decode_wav(const std::vector<uint8_t> &p_file, std::vector<float> &r_samples) const;
	bool decode_ogg(const std::vector<uint8_t> &p_file, std::vector<float> &r_samples) const;
	bool decode_mp3(const std::vector<uint8_t> &p_file, std::vector<float> &r_samples) const;
	bool convert_pcm16(const int16_t *p_samples, size_t p_frames, int p_channels, int p_rate, std::vector<float> &r_samples) const;

	AudioServer();
	~AudioServer();
};
