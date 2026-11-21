#include "animation_player.h"

#include "scene/main/scene_tree.h"

Variant AnimationPlayer::_deep_copy(const Variant &p_value) {
	if (p_value.get_type() == Variant::DICTIONARY) {
		Dictionary out;
		for (const auto &kv : p_value.as_dictionary().map()) {
			out[kv.first] = _deep_copy(kv.second);
		}
		return Variant(out);
	}
	if (p_value.get_type() == Variant::ARRAY) {
		Array out;
		for (const Variant &v : p_value.as_array().vec()) {
			out.push_back(_deep_copy(v));
		}
		return Variant(out);
	}
	return p_value;
}

void AnimationPlayer::set_animations(const Dictionary &p_animations) {
	animations = _deep_copy(Variant(p_animations)).as_dictionary();
}

Dictionary AnimationPlayer::_get_animation(const String &p_name) const {
	return animations.get(Variant(p_name), Variant(Dictionary())).as_dictionary();
}

Array AnimationPlayer::_get_track(const String &p_name, const String &p_track, bool p_create) {
	if (!has_animation(p_name)) {
		return Array();
	}
	Dictionary anim = _get_animation(p_name);
	if (!anim.has(Variant("tracks"))) {
		if (!p_create) {
			return Array();
		}
		anim[Variant("tracks")] = Variant(Dictionary());
	}
	Dictionary tracks = anim[Variant("tracks")].as_dictionary();
	if (!tracks.has(Variant(p_track))) {
		if (!p_create) {
			return Array();
		}
		tracks[Variant(p_track)] = Variant(Array());
	}
	return tracks[Variant(p_track)].as_array();
}

void AnimationPlayer::add_animation(const String &p_name, real_t p_length, bool p_loop) {
	if (p_name.empty() || has_animation(p_name)) {
		return;
	}
	Dictionary anim;
	anim[Variant("length")] = Variant((double)std::max((real_t)0.01, p_length));
	anim[Variant("loop")] = Variant(p_loop);
	anim[Variant("tracks")] = Variant(Dictionary());
	animations[Variant(p_name)] = Variant(anim);
}

void AnimationPlayer::remove_animation(const String &p_name) {
	animations.erase(Variant(p_name));
	if (current == p_name) {
		current.clear();
		playing = false;
	}
}

void AnimationPlayer::set_animation_length(const String &p_name, real_t p_length) {
	if (has_animation(p_name)) {
		_get_animation(p_name)[Variant("length")] = Variant((double)std::max((real_t)0.01, p_length));
	}
}

real_t AnimationPlayer::get_animation_length(const String &p_name) const {
	return (real_t)_get_animation(p_name).get(Variant("length"), Variant(1.0)).as_float();
}

void AnimationPlayer::set_animation_loop(const String &p_name, bool p_loop) {
	if (has_animation(p_name)) {
		_get_animation(p_name)[Variant("loop")] = Variant(p_loop);
	}
}

bool AnimationPlayer::get_animation_loop(const String &p_name) const {
	return _get_animation(p_name).get(Variant("loop"), Variant(false)).as_bool();
}

Array AnimationPlayer::get_track_list(const String &p_name) const {
	return _get_animation(p_name).get(Variant("tracks"), Variant(Dictionary())).as_dictionary().keys();
}

void AnimationPlayer::track_insert_key(const String &p_name, const String &p_track, real_t p_time, const Variant &p_value) {
	if (!has_animation(p_name) || p_track.find(':') == String::npos) {
		print_error("AnimationPlayer : piste invalide '" + p_track + "' (format attendu : Chemin:propriete).");
		return;
	}
	Array keys = _get_track(p_name, p_track, true);
	std::vector<Variant> &v = keys.vec();
	// Remplace la clé existante au même instant, sinon insère en gardant l'ordre chronologique.
	size_t i = 0;
	for (; i + 1 < v.size(); i += 2) {
		const real_t t = (real_t)v[i].as_float();
		if (std::fabs(t - p_time) < 0.0005f) {
			v[i + 1] = p_value;
			return;
		}
		if (t > p_time) {
			break;
		}
	}
	v.insert(v.begin() + i, { Variant((double)p_time), p_value });
}

void AnimationPlayer::track_remove_key(const String &p_name, const String &p_track, int p_index) {
	Array keys = _get_track(p_name, p_track, false);
	std::vector<Variant> &v = keys.vec();
	if (p_index < 0 || (size_t)(p_index * 2 + 1) >= v.size()) {
		return;
	}
	v.erase(v.begin() + p_index * 2, v.begin() + p_index * 2 + 2);
	if (v.empty()) {
		remove_track(p_name, p_track);
	}
}

void AnimationPlayer::remove_track(const String &p_name, const String &p_track) {
	Dictionary anim = _get_animation(p_name);
	if (anim.has(Variant("tracks"))) {
		anim[Variant("tracks")].as_dictionary().erase(Variant(p_track));
	}
}

int AnimationPlayer::track_get_key_count(const String &p_name, const String &p_track) const {
	return const_cast<AnimationPlayer *>(this)->_get_track(p_name, p_track, false).size() / 2;
}

real_t AnimationPlayer::track_get_key_time(const String &p_name, const String &p_track, int p_index) const {
	const Array keys = const_cast<AnimationPlayer *>(this)->_get_track(p_name, p_track, false);
	return p_index >= 0 && p_index * 2 + 1 < keys.size() ? (real_t)keys[p_index * 2].as_float() : 0;
}

Variant AnimationPlayer::track_get_key_value(const String &p_name, const String &p_track, int p_index) const {
	const Array keys = const_cast<AnimationPlayer *>(this)->_get_track(p_name, p_track, false);
	return p_index >= 0 && p_index * 2 + 1 < keys.size() ? keys[p_index * 2 + 1] : Variant();
}

Variant AnimationPlayer::interpolate(const Variant &p_a, const Variant &p_b, real_t p_t) {
	if (p_a.get_type() != p_b.get_type()) {
		if (p_a.is_num() && p_b.is_num()) {
			return Variant(Math::lerp(p_a.as_float(), p_b.as_float(), (double)p_t));
		}
		return p_a;
	}
	switch (p_a.get_type()) {
		case Variant::FLOAT:
			return Variant(Math::lerp(p_a.as_float(), p_b.as_float(), (double)p_t));
		case Variant::VECTOR2:
			return Variant(p_a.as_vector2().lerp(p_b.as_vector2(), p_t));
		case Variant::VECTOR3:
			return Variant(p_a.as_vector3().lerp(p_b.as_vector3(), p_t));
		case Variant::COLOR:
			return Variant(p_a.as_color().lerp(p_b.as_color(), p_t));
		default:
			return p_a; // entiers (numéros d'image), booléens, textes... : pas d'interpolation
	}
}

void AnimationPlayer::apply(const String &p_name, double p_time) {
	Node *root = get_parent();
	if (!root || !has_animation(p_name)) {
		return;
	}
	const Dictionary tracks = _get_animation(p_name).get(Variant("tracks"), Variant(Dictionary())).as_dictionary();
	for (const auto &kv : tracks.map()) {
		const String track = kv.first.as_string();
		const size_t colon = track.rfind(':');
		if (colon == String::npos) {
			continue;
		}
		const String path = track.substr(0, colon);
		const String property = track.substr(colon + 1);
		Node *node = (path.empty() || path == ".") ? root : root->get_node_or_null(path);
		if (!node) {
			continue;
		}
		const Array key_array = kv.second.as_array();
		const std::vector<Variant> &keys = key_array.vec();
		const size_t count = keys.size() / 2;
		if (count == 0) {
			continue;
		}
		// Clés encadrant l'instant demandé.
		size_t next = 0;
		while (next < count && keys[next * 2].as_float() <= p_time) {
			next++;
		}
		Variant value;
		if (next == 0) {
			value = keys[1];
		} else if (next >= count) {
			value = keys[(count - 1) * 2 + 1];
		} else {
			const double t0 = keys[(next - 1) * 2].as_float();
			const double t1 = keys[next * 2].as_float();
			const real_t t = t1 > t0 ? (real_t)((p_time - t0) / (t1 - t0)) : 0.0f;
			value = interpolate(keys[(next - 1) * 2 + 1], keys[next * 2 + 1], t);
		}
		node->set(property, value);
	}
}

void AnimationPlayer::play(const String &p_name) {
	const String name = p_name.empty() ? current : p_name;
	if (!has_animation(name)) {
		if (!name.empty()) {
			print_error("AnimationPlayer : animation inconnue '" + name + "'.");
		}
		return;
	}
	if (name != current || !playing) {
		if (name != current || position >= get_animation_length(name)) {
			position = 0;
		}
		current = name;
		playing = true;
		emit_signal("animation_started", current);
		apply(current, position);
	}
}

void AnimationPlayer::stop() {
	playing = false;
	position = 0;
}

void AnimationPlayer::seek(double p_time) {
	position = std::max(0.0, p_time);
	if (!current.empty()) {
		apply(current, position);
	}
}

void AnimationPlayer::advance(double p_delta) {
	if (!playing || !has_animation(current)) {
		return;
	}
	const double length = get_animation_length(current);
	position += p_delta * speed_scale;
	if (position >= length) {
		if (get_animation_loop(current)) {
			position = std::fmod(position, length);
		} else {
			position = length;
			apply(current, position);
			playing = false;
			emit_signal("animation_finished", current);
			return;
		}
	}
	apply(current, position);
}

void AnimationPlayer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_process_internal(!get_tree()->is_edit_mode());
		} break;
		case NOTIFICATION_READY: {
			if (!get_tree()->is_edit_mode() && !autoplay.empty()) {
				play(autoplay);
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			advance(get_process_delta_time());
		} break;
	}
}

void AnimationPlayer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_animations", "animations"), &AnimationPlayer::set_animations);
	ClassDB::bind_method(D_METHOD("get_animations"), &AnimationPlayer::get_animations);
	ClassDB::bind_method(D_METHOD("set_autoplay", "name"), &AnimationPlayer::set_autoplay);
	ClassDB::bind_method(D_METHOD("get_autoplay"), &AnimationPlayer::get_autoplay);
	ClassDB::bind_method(D_METHOD("set_speed_scale", "scale"), &AnimationPlayer::set_speed_scale);
	ClassDB::bind_method(D_METHOD("get_speed_scale"), &AnimationPlayer::get_speed_scale);
	ClassDB::bind_method(D_METHOD("add_animation", "name", "length", "loop"), &AnimationPlayer::add_animation, DEFVAL(1.0), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("remove_animation", "name"), &AnimationPlayer::remove_animation);
	ClassDB::bind_method(D_METHOD("has_animation", "name"), &AnimationPlayer::has_animation);
	ClassDB::bind_method(D_METHOD("get_animation_list"), &AnimationPlayer::get_animation_list);
	ClassDB::bind_method(D_METHOD("get_animation_length", "name"), &AnimationPlayer::get_animation_length);
	ClassDB::bind_method(D_METHOD("track_insert_key", "name", "track", "time", "value"), &AnimationPlayer::track_insert_key);
	ClassDB::bind_method(D_METHOD("play", "name"), &AnimationPlayer::play, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("stop"), &AnimationPlayer::stop);
	ClassDB::bind_method(D_METHOD("pause"), &AnimationPlayer::pause);
	ClassDB::bind_method(D_METHOD("is_playing"), &AnimationPlayer::is_playing);
	ClassDB::bind_method(D_METHOD("get_current_animation"), &AnimationPlayer::get_current_animation);
	ClassDB::bind_method(D_METHOD("get_current_animation_position"), &AnimationPlayer::get_current_animation_position);
	ClassDB::bind_method(D_METHOD("seek", "time"), &AnimationPlayer::seek);

	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "animations", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR), "set_animations", "get_animations");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "autoplay"), "set_autoplay", "get_autoplay");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed_scale", PROPERTY_HINT_RANGE, "0,8,0.05"), "set_speed_scale", "get_speed_scale");

	ADD_SIGNAL(MethodInfo("animation_started", PropertyInfo(Variant::STRING, "name")));
	ADD_SIGNAL(MethodInfo("animation_finished", PropertyInfo(Variant::STRING, "name")));
}
