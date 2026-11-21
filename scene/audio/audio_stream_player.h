// AudioStreamWAV (ressource) et AudioStreamPlayer (nœud) : lecture de sons WAV.
#pragma once

#include "core/io/resource_loader.h"
#include "scene/main/node.h"
#include "servers/audio_server.h"

class AudioStreamWAV : public Resource {
	GDCLASS(AudioStreamWAV, Resource)

	AudioServer::SampleData samples;
	double length = 0;

protected:
	static void _bind_methods();

public:
	Error load_from_file(const String &p_path);
	AudioServer::SampleData get_samples() const { return samples; }
	double get_length() const { return length; }
};

class ResourceFormatLoaderWAV : public ResourceFormatLoader {
public:
	virtual void get_recognized_extensions(std::vector<String> *p_extensions) const override { p_extensions->push_back("wav"); }
	virtual String get_resource_type(const String &p_path) const override { return "AudioStreamWAV"; }
	virtual Ref<Resource> load(const String &p_path, Error *r_error) override;
};

class AudioStreamPlayer : public Node {
	GDCLASS(AudioStreamPlayer, Node)

	Ref<AudioStreamWAV> stream;
	real_t volume = 1.0f;
	bool autoplay = false;
	bool loop = false;
	int voice = 0;
	bool was_playing = false;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_stream(const Ref<AudioStreamWAV> &p_stream);
	Ref<AudioStreamWAV> get_stream() const { return stream; }
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
