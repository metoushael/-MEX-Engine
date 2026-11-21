#include "audio_stream_player.h"

#include "core/io/file_access.h"
#include "scene/main/scene_tree.h"

// --- AudioStreamWAV ---
Error AudioStreamWAV::load_from_file(const String &p_path) {
	Error err = OK;
	std::vector<uint8_t> bytes = FileAccess::get_file_as_bytes(p_path, &err);
	if (err != OK) {
		return err;
	}
	AudioServer *as = AudioServer::get_singleton();
	ERR_FAIL_NULL_V(as, ERR_UNAVAILABLE);
	auto decoded = std::make_shared<std::vector<float>>();
	if (!as->decode_wav(bytes, *decoded)) {
		print_error("Fichier audio illisible : " + p_path);
		return ERR_INVALID_DATA;
	}
	length = (double)(decoded->size() / 2) / (double)as->get_mix_rate();
	samples = decoded;
	return OK;
}

void AudioStreamWAV::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_length"), &AudioStreamWAV::get_length);
}

Ref<Resource> ResourceFormatLoaderWAV::load(const String &p_path, Error *r_error) {
	Ref<AudioStreamWAV> stream;
	stream.instantiate();
	Error err = stream->load_from_file(p_path);
	if (r_error) {
		*r_error = err;
	}
	if (err != OK) {
		return Ref<Resource>();
	}
	return stream;
}

// --- AudioStreamPlayer ---
void AudioStreamPlayer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (autoplay && !get_tree()->is_edit_mode()) {
				play();
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			// Détecte la fin de lecture pour émettre "finished".
			if (was_playing && !is_playing()) {
				was_playing = false;
				set_process_internal(false);
				emit_signal("finished");
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			stop();
		} break;
	}
}

void AudioStreamPlayer::set_stream(const Ref<AudioStreamWAV> &p_stream) {
	stop();
	stream = p_stream;
}

void AudioStreamPlayer::set_volume(real_t p_volume) {
	volume = std::max(0.0f, p_volume);
	if (voice && AudioServer::get_singleton()) {
		AudioServer::get_singleton()->set_voice_volume(voice, volume);
	}
}

void AudioStreamPlayer::play(double p_from) {
	AudioServer *as = AudioServer::get_singleton();
	if (!as || stream.is_null()) {
		return;
	}
	stop();
	voice = as->play(stream->get_samples(), volume, loop, p_from);
	if (voice) {
		was_playing = true;
		set_process_internal(true);
	}
}

void AudioStreamPlayer::stop() {
	if (voice && AudioServer::get_singleton()) {
		AudioServer::get_singleton()->stop(voice);
	}
	voice = 0;
	was_playing = false;
	set_process_internal(false);
}

bool AudioStreamPlayer::is_playing() const {
	return voice && AudioServer::get_singleton() && AudioServer::get_singleton()->is_playing(voice);
}

double AudioStreamPlayer::get_playback_position() const {
	return is_playing() ? AudioServer::get_singleton()->get_voice_position(voice) : 0.0;
}

void AudioStreamPlayer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_stream", "stream"), &AudioStreamPlayer::set_stream);
	ClassDB::bind_method(D_METHOD("get_stream"), &AudioStreamPlayer::get_stream);
	ClassDB::bind_method(D_METHOD("set_volume", "volume"), &AudioStreamPlayer::set_volume);
	ClassDB::bind_method(D_METHOD("get_volume"), &AudioStreamPlayer::get_volume);
	ClassDB::bind_method(D_METHOD("set_autoplay", "autoplay"), &AudioStreamPlayer::set_autoplay);
	ClassDB::bind_method(D_METHOD("is_autoplay_enabled"), &AudioStreamPlayer::is_autoplay_enabled);
	ClassDB::bind_method(D_METHOD("set_loop", "loop"), &AudioStreamPlayer::set_loop);
	ClassDB::bind_method(D_METHOD("has_loop"), &AudioStreamPlayer::has_loop);
	ClassDB::bind_method(D_METHOD("play", "from"), &AudioStreamPlayer::play, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("stop"), &AudioStreamPlayer::stop);
	ClassDB::bind_method(D_METHOD("is_playing"), &AudioStreamPlayer::is_playing);
	ClassDB::bind_method(D_METHOD("get_playback_position"), &AudioStreamPlayer::get_playback_position);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "stream", PROPERTY_HINT_RESOURCE_TYPE, "AudioStreamWAV"), "set_stream", "get_stream");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "volume", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_volume", "get_volume");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "autoplay"), "set_autoplay", "is_autoplay_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "loop"), "set_loop", "has_loop");

	ADD_SIGNAL(MethodInfo("finished"));
}
