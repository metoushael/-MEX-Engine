#include "sprite_2d.h"

Rect2 Sprite2D::_get_source_rect() const {
	if (texture.is_null()) {
		return Rect2();
	}
	Rect2 base = region_enabled ? region_rect : Rect2(Vector2(), texture->get_size());
	if (hframes <= 1 && vframes <= 1) {
		return base;
	}
	const Size2 frame_size(base.size.x / (real_t)hframes, base.size.y / (real_t)vframes);
	const int fx = frame % hframes;
	const int fy = frame / hframes;
	return Rect2(base.position + Vector2(frame_size.x * fx, frame_size.y * fy), frame_size);
}

Rect2 Sprite2D::get_rect() const {
	Rect2 src = _get_source_rect();
	Vector2 pos = offset;
	if (centered) {
		pos -= src.size * 0.5f;
	}
	return Rect2(pos, src.size);
}

void Sprite2D::_notification(int p_what) {
	if (p_what == NOTIFICATION_DRAW) {
		if (texture.is_null()) {
			return;
		}
		Rect2 src = _get_source_rect();
		Rect2 dst = get_rect();
		// Une taille négative retourne l'image.
		if (flip_h) {
			dst.position.x += dst.size.x;
			dst.size.x = -dst.size.x;
		}
		if (flip_v) {
			dst.position.y += dst.size.y;
			dst.size.y = -dst.size.y;
		}
		draw_texture_rect_region(texture, dst, src);
	}
}

void Sprite2D::set_texture(const Ref<Texture2D> &p_texture) {
	texture = p_texture;
	queue_redraw();
}

void Sprite2D::set_centered(bool p_centered) {
	centered = p_centered;
	queue_redraw();
}

void Sprite2D::set_offset(const Vector2 &p_offset) {
	offset = p_offset;
	queue_redraw();
}

void Sprite2D::set_flip_h(bool p_flip) {
	flip_h = p_flip;
	queue_redraw();
}

void Sprite2D::set_flip_v(bool p_flip) {
	flip_v = p_flip;
	queue_redraw();
}

void Sprite2D::set_hframes(int p_frames) {
	hframes = std::max(1, p_frames);
	frame = Math::clamp(frame, 0, hframes * vframes - 1);
	queue_redraw();
}

void Sprite2D::set_vframes(int p_frames) {
	vframes = std::max(1, p_frames);
	frame = Math::clamp(frame, 0, hframes * vframes - 1);
	queue_redraw();
}

void Sprite2D::set_frame(int p_frame) {
	const int count = hframes * vframes;
	ERR_FAIL_COND_MSG(p_frame < 0 || p_frame >= count, "Sprite2D : image " + std::to_string(p_frame) + " hors limites (" + std::to_string(count) + " images).");
	if (frame != p_frame) {
		frame = p_frame;
		queue_redraw();
		emit_signal("frame_changed");
	}
}

void Sprite2D::set_region_enabled(bool p_enabled) {
	region_enabled = p_enabled;
	queue_redraw();
}

void Sprite2D::set_region_rect(const Rect2 &p_rect) {
	region_rect = p_rect;
	queue_redraw();
}

void Sprite2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_texture", "texture"), &Sprite2D::set_texture);
	ClassDB::bind_method(D_METHOD("get_texture"), &Sprite2D::get_texture);
	ClassDB::bind_method(D_METHOD("set_centered", "centered"), &Sprite2D::set_centered);
	ClassDB::bind_method(D_METHOD("is_centered"), &Sprite2D::is_centered);
	ClassDB::bind_method(D_METHOD("set_offset", "offset"), &Sprite2D::set_offset);
	ClassDB::bind_method(D_METHOD("get_offset"), &Sprite2D::get_offset);
	ClassDB::bind_method(D_METHOD("set_flip_h", "flip"), &Sprite2D::set_flip_h);
	ClassDB::bind_method(D_METHOD("is_flipped_h"), &Sprite2D::is_flipped_h);
	ClassDB::bind_method(D_METHOD("set_flip_v", "flip"), &Sprite2D::set_flip_v);
	ClassDB::bind_method(D_METHOD("is_flipped_v"), &Sprite2D::is_flipped_v);
	ClassDB::bind_method(D_METHOD("set_hframes", "frames"), &Sprite2D::set_hframes);
	ClassDB::bind_method(D_METHOD("get_hframes"), &Sprite2D::get_hframes);
	ClassDB::bind_method(D_METHOD("set_vframes", "frames"), &Sprite2D::set_vframes);
	ClassDB::bind_method(D_METHOD("get_vframes"), &Sprite2D::get_vframes);
	ClassDB::bind_method(D_METHOD("set_frame", "frame"), &Sprite2D::set_frame);
	ClassDB::bind_method(D_METHOD("get_frame"), &Sprite2D::get_frame);
	ClassDB::bind_method(D_METHOD("set_region_enabled", "enabled"), &Sprite2D::set_region_enabled);
	ClassDB::bind_method(D_METHOD("is_region_enabled"), &Sprite2D::is_region_enabled);
	ClassDB::bind_method(D_METHOD("set_region_rect", "rect"), &Sprite2D::set_region_rect);
	ClassDB::bind_method(D_METHOD("get_region_rect"), &Sprite2D::get_region_rect);
	ClassDB::bind_method(D_METHOD("get_rect"), &Sprite2D::get_rect);

	ADD_GROUP("Sprite");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_texture", "get_texture");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "centered"), "set_centered", "is_centered");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "offset"), "set_offset", "get_offset");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "flip_h"), "set_flip_h", "is_flipped_h");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "flip_v"), "set_flip_v", "is_flipped_v");
	ADD_GROUP("Animation");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "hframes", PROPERTY_HINT_RANGE, "1,256,1"), "set_hframes", "get_hframes");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "vframes", PROPERTY_HINT_RANGE, "1,256,1"), "set_vframes", "get_vframes");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "frame", PROPERTY_HINT_RANGE, "0,65535,1"), "set_frame", "get_frame");
	ADD_GROUP("Region");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "region_enabled"), "set_region_enabled", "is_region_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::RECT2, "region_rect"), "set_region_rect", "get_region_rect");

	ADD_SIGNAL(MethodInfo("frame_changed"));
}
