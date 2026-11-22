// AudioStream (ressources WAV, Ogg Vorbis, MP3) et AudioStreamPlayer (nœud).
#pragma once

#include "core/io/resource_loader.h"
#include "scene/2d/node_2d.h"
#include "servers/audio_server.h"

// Son décodé en mémoire. Les trois formats partagent le même fonctionnement ; les classes
// dérivées portent le nom du format, comme dans Godot.
class AudioStream : public Resource {
	GDCLASS(AudioStream, Resource)

	AudioServer::SampleData samples;
	double length = 0;

protected:
	static void _bind_methods();

public:
	Error load_from_file(const String &p_path);
	AudioServer::SampleData get_samples() const { return samples; }
	double get_length() const { return length; }
};

class AudioStreamWAV : public AudioStream {
	GDCLASS(AudioStreamWAV, AudioStream)

protected:
	static void _bind_methods() {}
};

class AudioStreamOggVorbis : public AudioStream {
	GDCLASS(AudioStreamOggVorbis, AudioStream)

protected:
	static void _bind_methods() {}
};

class AudioStreamMP3 : public AudioStream {
	GDCLASS(AudioStreamMP3, AudioStream)

protected:
	static void _bind_methods() {}
};

class ResourceFormatLoaderAudio : public ResourceFormatLoader {
public:
	virtual void get_recognized_extensions(std::vector<String> *p_extensions) const override {
		p_extensions->push_back("wav");
		p_extensions->push_back("ogg");
		p_extensions->push_back("mp3");
	}
	virtual String get_resource_type(const String &p_path) const override;
	virtual Ref<Resource> load(const String &p_path, Error *r_error) override;
};

class AudioStreamPlayer : public Node {
	GDCLASS(AudioStreamPlayer, Node)

	Ref<AudioStream> stream;
	real_t volume = 1.0f;
	bool autoplay = false;
	bool loop = false;
	String bus = "Master";
	real_t pitch_scale = 1.0f;
	int voice = 0;
	bool was_playing = false;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	// Bus de l'AudioServer dans lequel le son est joué (« Master » si le nom est inconnu).
	void set_bus(const String &p_bus) { bus = p_bus; }
	String get_bus() const { return bus; }
	// Vitesse de lecture : 2 joue deux fois plus vite et une octave plus haut.
	void set_pitch_scale(real_t p_pitch);
	real_t get_pitch_scale() const { return pitch_scale; }
	void set_stream(const Ref<AudioStream> &p_stream);
	Ref<AudioStream> get_stream() const { return stream; }
	void set_volume(real_t p_volume);
	real_t get_volume() const { return volume; }
	void set_autoplay(bool p_autoplay) { autoplay = p_autoplay; }
	bool is_autoplay_enabled() const { return autoplay; }
	void set_loop(bool p_loop) { loop = p_loop; }
	bool has_loop() const { return loop; }

	void play(double p_from = 0.0);
	void stop();
	bool is_playing() const;
	double get_playback_position() const;
};

// Son placé dans la scène 2D : son volume baisse avec la distance au centre de l'écran et
// il se déplace entre les haut-parleurs gauche et droit.
class AudioStreamPlayer2D : public Node2D {
	GDCLASS(AudioStreamPlayer2D, Node2D)

	Ref<AudioStream> stream;
	real_t volume = 1.0f;
	bool autoplay = false;
	bool loop = false;
	String bus = "Master";
	real_t pitch_scale = 1.0f;
	real_t max_distance = 2000.0f;
	real_t attenuation = 1.0f;
	real_t panning_strength = 1.0f;
	int voice = 0;
	bool was_playing = false;

	void _update_voice();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_stream(const Ref<AudioStream> &p_stream);
	Ref<AudioStream> get_stream() const { return stream; }
	void set_volume(real_t p_volume) { volume = p_volume; }
	real_t get_volume() const { return volume; }
	void set_autoplay(bool p_autoplay) { autoplay = p_autoplay; }
	bool is_autoplay_enabled() const { return autoplay; }
	void set_loop(bool p_loop) { loop = p_loop; }
	bool has_loop() const { return loop; }
	void set_bus(const String &p_bus) { bus = p_bus; }
	String get_bus() const { return bus; }
	void set_pitch_scale(real_t p_pitch) { pitch_scale = p_pitch; }
	real_t get_pitch_scale() const { return pitch_scale; }
	void set_max_distance(real_t p_distance) { max_distance = p_distance; }
	real_t get_max_distance() const { return max_distance; }
	void set_attenuation(real_t p_attenuation) { attenuation = p_attenuation; }
	real_t get_attenuation() const { return attenuation; }
	void set_panning_strength(real_t p_strength) { panning_strength = p_strength; }
	real_t get_panning_strength() const { return panning_strength; }

	void play(double p_from = 0.0);
	void stop();
	bool is_playing() const;
	double get_playback_position() const;
};
