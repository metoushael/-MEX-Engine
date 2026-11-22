#include "audio_stream_player.h"

#include "core/io/file_access.h"
#include "core/string/ustring.h"
#include "scene/main/scene_tree.h"

// --- AudioStream ---
Error AudioStream::load_from_file(const String &p_path) {
	Error err = OK;
	std::vector<uint8_t> bytes = FileAccess::get_file_as_bytes(p_path, &err);
	if (err != OK) {
		return err;
	}
	AudioServer *as = AudioServer::get_singleton();
	ERR_FAIL_NULL_V(as, ERR_UNAVAILABLE);
	auto decoded = std::make_shared<std::vector<float>>();
	if (!as->decode(bytes, *decoded)) {
		print_error("Fichier audio illisible : " + p_path);
		return ERR_INVALID_DATA;
	}
	length = (double)(decoded->size() / 2) / (double)as->get_mix_rate();
	samples = decoded;
	return OK;
}

void AudioStream::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_length"), &AudioStream::get_length);
}

String ResourceFormatLoaderAudio::get_resource_type(const String &p_path) const {
	const String ext = path_get_extension(p_path);
	return ext == "ogg" ? "AudioStreamOggVorbis" : (ext == "mp3" ? "AudioStreamMP3" : "AudioStreamWAV");
}

Ref<Resource> ResourceFormatLoaderAudio::load(const String &p_path, Error *r_error) {
	const String ext = path_get_extension(p_path);
	Ref<AudioStream> stream;
	if (ext == "ogg") {
		stream = Ref<AudioStream>(memnew(AudioStreamOggVorbis));
	} else if (ext == "mp3") {
		stream = Ref<AudioStream>(memnew(AudioStreamMP3));
	} else {
		stream = Ref<AudioStream>(memnew(AudioStreamWAV));
	}
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

void AudioStreamPlayer::set_stream(const Ref<AudioStream> &p_stream) {
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
	voice = as->play(stream->get_samples(), volume, loop, p_from, std::max(as->get_bus_index(bus), 0), pitch_scale);
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

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "stream", PROPERTY_HINT_RESOURCE_TYPE, "AudioStream"), "set_stream", "get_stream");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "volume", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_volume", "get_volume");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "autoplay"), "set_autoplay", "is_autoplay_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "loop"), "set_loop", "has_loop");
	ClassDB::bind_method(D_METHOD("set_bus", "bus"), &AudioStreamPlayer::set_bus);
	ClassDB::bind_method(D_METHOD("get_bus"), &AudioStreamPlayer::get_bus);
	ClassDB::bind_method(D_METHOD("set_pitch_scale", "pitch"), &AudioStreamPlayer::set_pitch_scale);
	ClassDB::bind_method(D_METHOD("get_pitch_scale"), &AudioStreamPlayer::get_pitch_scale);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pitch_scale", PROPERTY_HINT_RANGE, "0.01,4,0.01"), "set_pitch_scale", "get_pitch_scale");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "bus"), "set_bus", "get_bus");

	ADD_SIGNAL(MethodInfo("finished"));
}

void AudioStreamPlayer::set_pitch_scale(real_t p_pitch) {
	pitch_scale = std::max(p_pitch, (real_t)0.01);
	if (voice && AudioServer::get_singleton()) {
		AudioServer::get_singleton()->set_voice_pitch(voice, pitch_scale);
	}
}

// --- AudioStreamPlayer2D ---
void AudioStreamPlayer2D::_update_voice() {
	AudioServer *as = AudioServer::get_singleton();
	if (!voice || !as || !is_inside_tree()) {
		return;
	}
	// L'auditeur est au centre de l'écran, dans le repère du canevas.
	const Size2 viewport = get_viewport_size();
	const Vector2 listener = get_canvas_transform().affine_inverse().xform(viewport * 0.5f);
	const Vector2 offset = get_global_position() - listener;
	const real_t ratio = max_distance > 0 ? std::min(offset.length() / max_distance, (real_t)1) : 0;
	const real_t gain = std::pow(1.0f - ratio, std::max(attenuation, (real_t)0.01));
	const real_t half_width = std::max(viewport.x * 0.5f, (real_t)1);
	as->set_voice_volume(voice, volume * gain);
	as->set_voice_pan(voice, Math::clamp<real_t>(offset.x / half_width, -1, 1) * panning_strength);
	as->set_voice_pitch(voice, pitch_scale);
}

void AudioStreamPlayer2D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (autoplay && !get_tree()->is_edit_mode()) {
				play();
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (was_playing && !is_playing()) {
				was_playing = false;
				set_process_internal(false);
				emit_signal("finished");
			} else {
				_update_voice();
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			stop();
		} break;
	}
}

void AudioStreamPlayer2D::set_stream(const Ref<AudioStream> &p_stream) {
	stop();
	stream = p_stream;
}

void AudioStreamPlayer2D::play(double p_from) {
	AudioServer *as = AudioServer::get_singleton();
	if (!as || stream.is_null()) {
		return;
	}
	stop();
	voice = as->play(stream->get_samples(), 0.0f, loop, p_from, std::max(as->get_bus_index(bus), 0), pitch_scale);
	if (voice) {
		was_playing = true;
		_update_voice();
		set_process_internal(true);
	}
}

void AudioStreamPlayer2D::stop() {
	if (voice && AudioServer::get_singleton()) {
		AudioServer::get_singleton()->stop(voice);
	}
	voice = 0;
	was_playing = false;
	set_process_internal(false);
}

bool AudioStreamPlayer2D::is_playing() const {
	return voice && AudioServer::get_singleton() && AudioServer::get_singleton()->is_playing(voice);
}

double AudioStreamPlayer2D::get_playback_position() const {
	return is_playing() ? AudioServer::get_singleton()->get_voice_position(voice) : 0.0;
}

void AudioStreamPlayer2D::_bind_methods() {
#define BIND_PLAYER_PROPERTY(m_type, m_name, m_setter, m_getter, m_hint, m_hint_string) \
	ClassDB::bind_method(D_METHOD(#m_setter, "value"), &AudioStreamPlayer2D::m_setter); \
	ClassDB::bind_method(D_METHOD(#m_getter), &AudioStreamPlayer2D::m_getter); \
	ADD_PROPERTY(PropertyInfo(m_type, m_name, m_hint, m_hint_string), #m_setter, #m_getter)
	BIND_PLAYER_PROPERTY(Variant::OBJECT, "stream", set_stream, get_stream, PROPERTY_HINT_RESOURCE_TYPE, "AudioStream");
	BIND_PLAYER_PROPERTY(Variant::FLOAT, "volume", set_volume, get_volume, PROPERTY_HINT_RANGE, "0,2,0.01");
	BIND_PLAYER_PROPERTY(Variant::FLOAT, "pitch_scale", set_pitch_scale, get_pitch_scale, PROPERTY_HINT_RANGE, "0.01,4,0.01");
	BIND_PLAYER_PROPERTY(Variant::BOOL, "autoplay", set_autoplay, is_autoplay_enabled, PROPERTY_HINT_NONE, "");
	BIND_PLAYER_PROPERTY(Variant::BOOL, "loop", set_loop, has_loop, PROPERTY_HINT_NONE, "");
	BIND_PLAYER_PROPERTY(Variant::FLOAT, "max_distance", set_max_distance, get_max_distance, PROPERTY_HINT_RANGE, "1,100000,1");
	BIND_PLAYER_PROPERTY(Variant::FLOAT, "attenuation", set_attenuation, get_attenuation, PROPERTY_HINT_RANGE, "0.01,8,0.01");
	BIND_PLAYER_PROPERTY(Variant::FLOAT, "panning_strength", set_panning_strength, get_panning_strength, PROPERTY_HINT_RANGE, "0,1,0.01");
	BIND_PLAYER_PROPERTY(Variant::STRING, "bus", set_bus, get_bus, PROPERTY_HINT_NONE, "");
#undef BIND_PLAYER_PROPERTY
	ClassDB::bind_method(D_METHOD("play", "from"), &AudioStreamPlayer2D::play, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("stop"), &AudioStreamPlayer2D::stop);
	ClassDB::bind_method(D_METHOD("is_playing"), &AudioStreamPlayer2D::is_playing);
	ClassDB::bind_method(D_METHOD("get_playback_position"), &AudioStreamPlayer2D::get_playback_position);
	ADD_SIGNAL(MethodInfo("finished"));
}
