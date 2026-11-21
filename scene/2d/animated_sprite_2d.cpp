#include "animated_sprite_2d.h"

#include "scene/main/scene_tree.h"

bool AnimatedSprite2D::_get_range(const String &p_name, int &r_first, int &r_last, real_t &r_fps, bool &r_loop) const {
	if (!animations.has(Variant(p_name))) {
		return false;
	}
	const Array data = animations.get(Variant(p_name), Variant()).as_array();
	if (data.size() < 2) {
		return false;
	}
	const int count = std::max(1, get_hframes() * get_vframes());
	r_first = Math::clamp((int)data[0].as_int(), 0, count - 1);
	r_last = Math::clamp((int)data[1].as_int(), 0, count - 1);
	r_fps = data.size() > 2 ? (real_t)data[2].as_float() : 8.0f;
	r_loop = data.size() > 3 ? data[3].as_bool() : true;
	return true;
}

void AnimatedSprite2D::set_animations(const Dictionary &p_animations) {
	// Copie profonde : deux nœuds ne partagent jamais la même table.
	animations = Dictionary();
	for (const auto &kv : p_animations.map()) {
		animations[kv.first] = Variant(kv.second.as_array().duplicate());
	}
}

void AnimatedSprite2D::add_animation(const String &p_name, int p_first, int p_last, real_t p_fps, bool p_loop) {
	Array data;
	data.push_back(Variant(p_first));
	data.push_back(Variant(p_last));
	data.push_back(Variant((double)p_fps));
	data.push_back(Variant(p_loop));
	animations[Variant(p_name)] = Variant(data);
}

void AnimatedSprite2D::remove_animation(const String &p_name) {
	animations.erase(Variant(p_name));
	if (animation == p_name) {
		animation.clear();
		playing = false;
	}
}

void AnimatedSprite2D::set_animation(const String &p_name) {
	if (animation == p_name) {
		return;
	}
	animation = p_name;
	frame_time = 0;
	int first = 0, last = 0;
	real_t fps = 0;
	bool loop = false;
	if (_get_range(animation, first, last, fps, loop)) {
		set_frame(first);
	}
}

void AnimatedSprite2D::play(const String &p_name) {
	const String name = p_name.empty() ? animation : p_name;
	if (!has_animation(name)) {
		if (!name.empty()) {
			print_error("AnimatedSprite2D : animation inconnue '" + name + "'.");
		}
		return;
	}
	if (name != animation || !playing) {
		const bool restart = name != animation;
		animation = name;
		int first = 0, last = 0;
		real_t fps = 0;
		bool loop = false;
		_get_range(animation, first, last, fps, loop);
		// Redémarre si l'animation change ou si elle était terminée.
		if (restart || get_frame() < std::min(first, last) || get_frame() > std::max(first, last) || (!loop && get_frame() == last)) {
			frame_time = 0;
			set_frame(first);
		}
	}
	playing = true;
}

void AnimatedSprite2D::stop() {
	playing = false;
	frame_time = 0;
	int first = 0, last = 0;
	real_t fps = 0;
	bool loop = false;
	if (_get_range(animation, first, last, fps, loop)) {
		set_frame(first);
	}
}

void AnimatedSprite2D::advance(double p_delta) {
	int first = 0, last = 0;
	real_t fps = 0;
	bool loop = false;
	if (!playing || !_get_range(animation, first, last, fps, loop) || fps <= 0) {
		return;
	}
	const int direction = last >= first ? 1 : -1;
	const int count = std::abs(last - first) + 1;
	frame_time += p_delta * speed_scale * fps;
	int guard = 0;
	while (frame_time >= 1.0 && guard++ < 1000) {
		frame_time -= 1.0;
		int index = (get_frame() - first) * direction; // rang dans l'animation
		if (index < 0 || index >= count) {
			index = 0;
		}
		index++;
		if (index >= count) {
			if (loop) {
				index = 0;
				set_frame(first);
				emit_signal("animation_looped");
			} else {
				playing = false;
				frame_time = 0;
				emit_signal("animation_finished");
				return;
			}
		} else {
			set_frame(first + index * direction);
		}
	}
}

void AnimatedSprite2D::_notification(int p_what) {
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

void AnimatedSprite2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_animations", "animations"), &AnimatedSprite2D::set_animations);
	ClassDB::bind_method(D_METHOD("get_animations"), &AnimatedSprite2D::get_animations);
	ClassDB::bind_method(D_METHOD("set_animation", "name"), &AnimatedSprite2D::set_animation);
	ClassDB::bind_method(D_METHOD("get_animation"), &AnimatedSprite2D::get_animation);
	ClassDB::bind_method(D_METHOD("set_autoplay", "name"), &AnimatedSprite2D::set_autoplay);
	ClassDB::bind_method(D_METHOD("get_autoplay"), &AnimatedSprite2D::get_autoplay);
	ClassDB::bind_method(D_METHOD("set_speed_scale", "scale"), &AnimatedSprite2D::set_speed_scale);
	ClassDB::bind_method(D_METHOD("get_speed_scale"), &AnimatedSprite2D::get_speed_scale);
	ClassDB::bind_method(D_METHOD("add_animation", "name", "first_frame", "last_frame", "fps", "loop"), &AnimatedSprite2D::add_animation, DEFVAL(8.0), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("remove_animation", "name"), &AnimatedSprite2D::remove_animation);
	ClassDB::bind_method(D_METHOD("has_animation", "name"), &AnimatedSprite2D::has_animation);
	ClassDB::bind_method(D_METHOD("get_animation_list"), &AnimatedSprite2D::get_animation_list);
	ClassDB::bind_method(D_METHOD("play", "name"), &AnimatedSprite2D::play, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("stop"), &AnimatedSprite2D::stop);
	ClassDB::bind_method(D_METHOD("pause"), &AnimatedSprite2D::pause);
	ClassDB::bind_method(D_METHOD("is_playing"), &AnimatedSprite2D::is_playing);

	ADD_GROUP("Animation");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "animations", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR), "set_animations", "get_animations");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "animation"), "set_animation", "get_animation");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "autoplay"), "set_autoplay", "get_autoplay");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed_scale", PROPERTY_HINT_RANGE, "0,8,0.05"), "set_speed_scale", "get_speed_scale");

	ADD_SIGNAL(MethodInfo("animation_finished"));
	ADD_SIGNAL(MethodInfo("animation_looped"));
}
