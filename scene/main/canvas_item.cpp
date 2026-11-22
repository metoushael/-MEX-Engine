#include "canvas_item.h"

#include "core/input/input.h"
#include "scene/main/canvas_layer.h"
#include "scene/main/scene_tree.h"

std::vector<ObjectID> CanvasItem::redraw_queue;

CanvasItem::CanvasItem() {
	canvas_item = RenderingServer::get_singleton()->canvas_item_create();
}

CanvasItem::~CanvasItem() {
	if (RenderingServer::get_singleton()) {
		RenderingServer::get_singleton()->free_rid(canvas_item);
	}
}

void CanvasItem::_notification(int p_what) {
	RenderingServer *rs = RenderingServer::get_singleton();
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			CanvasItem *parent_item = Object::cast_to<CanvasItem>(get_parent());
			if (parent_item) {
				rs->canvas_item_set_parent(canvas_item, parent_item->canvas_item);
			} else if (CanvasLayer *layer = get_canvas_layer_node()) {
				rs->canvas_item_set_parent(canvas_item, layer->get_canvas());
			} else {
				rs->canvas_item_set_parent(canvas_item, get_tree()->get_canvas());
			}
			get_tree()->mark_canvas_order_dirty();
			_update_canvas_transform();
			queue_redraw();
		} break;
		case NOTIFICATION_MOVED_IN_PARENT: {
			if (is_inside_tree()) {
				get_tree()->mark_canvas_order_dirty();
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			rs->canvas_item_set_parent(canvas_item, RID());
			pending_redraw = false;
		} break;
	}
}

void CanvasItem::set_material(const Ref<CanvasItemMaterial> &p_material) {
	material = p_material;
	RenderingServer::get_singleton()->canvas_item_set_material(canvas_item, material.is_valid() ? material->get_rid() : RID());
}

void CanvasItem::set_clip_rect(bool p_enabled, const Rect2 &p_rect) {
	RenderingServer::get_singleton()->canvas_item_set_clip(canvas_item, p_enabled, p_rect);
}

void CanvasItem::_update_canvas_transform() {
	RenderingServer::get_singleton()->canvas_item_set_transform(canvas_item, get_transform());
}

Transform2D CanvasItem::get_global_transform() const {
	const CanvasItem *parent_item = Object::cast_to<CanvasItem>(get_parent());
	if (parent_item) {
		return parent_item->get_global_transform() * get_transform();
	}
	return get_transform();
}

CanvasLayer *CanvasItem::get_canvas_layer_node() const {
	// Remonte jusqu'au premier CanvasItem "racine", puis cherche un CanvasLayer au-dessus.
	const Node *n = this;
	while (Object::cast_to<CanvasItem>(n->get_parent())) {
		n = n->get_parent();
	}
	for (Node *p = n->get_parent(); p; p = p->get_parent()) {
		if (CanvasLayer *layer = Object::cast_to<CanvasLayer>(p)) {
			return layer;
		}
		if (Object::cast_to<CanvasItem>(p)) {
			return static_cast<CanvasItem *>(p)->get_canvas_layer_node();
		}
	}
	return nullptr;
}

Transform2D CanvasItem::get_canvas_transform() const {
	if (CanvasLayer *layer = get_canvas_layer_node()) {
		return layer->get_layer_transform();
	}
	return get_tree() ? get_tree()->get_canvas_transform() : Transform2D();
}

Vector2 CanvasItem::get_global_mouse_position() const {
	return get_canvas_transform().affine_inverse().xform(Input::get_singleton()->get_mouse_position());
}

Vector2 CanvasItem::get_local_mouse_position() const {
	return get_global_transform().affine_inverse().xform(get_global_mouse_position());
}

Size2 CanvasItem::get_viewport_size() const {
	return get_tree() ? get_tree()->get_viewport_size() : Size2();
}

void CanvasItem::set_visible(bool p_visible) {
	if (visible == p_visible) {
		return;
	}
	visible = p_visible;
	RenderingServer::get_singleton()->canvas_item_set_visible(canvas_item, visible);
	notification(NOTIFICATION_VISIBILITY_CHANGED);
	emit_signal("visibility_changed");
}

bool CanvasItem::is_visible_in_tree() const {
	if (!is_inside_tree()) {
		return false;
	}
	for (const Node *n = this; n; n = n->get_parent()) {
		if (const CanvasItem *ci = Object::cast_to<CanvasItem>(n)) {
			if (!ci->visible) {
				return false;
			}
		} else if (const CanvasLayer *layer = Object::cast_to<CanvasLayer>(n)) {
			if (!layer->is_visible()) {
				return false;
			}
		}
	}
	return true;
}

void CanvasItem::set_modulate(const Color &p_color) {
	modulate = p_color;
	RenderingServer::get_singleton()->canvas_item_set_modulate(canvas_item, modulate);
}

void CanvasItem::set_self_modulate(const Color &p_color) {
	self_modulate = p_color;
	RenderingServer::get_singleton()->canvas_item_set_self_modulate(canvas_item, self_modulate);
}

void CanvasItem::set_z_index(int p_z) {
	z_index = p_z;
	RenderingServer::get_singleton()->canvas_item_set_z_index(canvas_item, z_index);
}

void CanvasItem::set_z_as_relative(bool p_relative) {
	z_as_relative = p_relative;
	RenderingServer::get_singleton()->canvas_item_set_z_as_relative(canvas_item, z_as_relative);
}

// ---------------------------------------------------------------------------
// Dessin
// ---------------------------------------------------------------------------
void CanvasItem::queue_redraw() {
	if (pending_redraw || !is_inside_tree()) {
		return;
	}
	pending_redraw = true;
	redraw_queue.push_back(get_instance_id());
}

void CanvasItem::_redraw() {
	pending_redraw = false;
	if (!is_inside_tree()) {
		return;
	}
	RenderingServer::get_singleton()->canvas_item_clear(canvas_item);
	drawing = true;
	notification(NOTIFICATION_DRAW);
	_call_script("_draw");
	drawing = false;
}

void CanvasItem::flush_redraws() {
	// Un _draw peut redemander un dessin : on borne le nombre de passes.
	for (int pass = 0; pass < 4 && !redraw_queue.empty(); pass++) {
		std::vector<ObjectID> queue;
		queue.swap(redraw_queue);
		for (ObjectID id : queue) {
			CanvasItem *ci = static_cast<CanvasItem *>(ObjectDB::get_instance(id));
			if (ci && ci->pending_redraw) {
				ci->_redraw();
			}
		}
	}
}

#define DRAW_CHECK() ERR_FAIL_COND_MSG(!drawing, "Les fonctions draw_*() ne peuvent être appelées que depuis _draw().")

void CanvasItem::draw_line(const Vector2 &p_from, const Vector2 &p_to, const Color &p_color, real_t p_width) {
	DRAW_CHECK();
	RenderingServer::get_singleton()->canvas_item_add_line(canvas_item, p_from, p_to, p_color, p_width);
}

void CanvasItem::draw_rect(const Rect2 &p_rect, const Color &p_color, bool p_filled, real_t p_width) {
	DRAW_CHECK();
	RenderingServer *rs = RenderingServer::get_singleton();
	if (p_filled) {
		rs->canvas_item_add_rect(canvas_item, p_rect, p_color);
	} else {
		Rect2 r = p_rect.abs();
		std::vector<Vector2> pts = { r.position, Vector2(r.position.x + r.size.x, r.position.y), r.position + r.size, Vector2(r.position.x, r.position.y + r.size.y) };
		rs->canvas_item_add_polyline(canvas_item, pts, p_color, p_width, true);
	}
}

void CanvasItem::draw_circle(const Vector2 &p_pos, real_t p_radius, const Color &p_color) {
	DRAW_CHECK();
	RenderingServer::get_singleton()->canvas_item_add_circle(canvas_item, p_pos, p_radius, p_color);
}

void CanvasItem::draw_arc(const Vector2 &p_center, real_t p_radius, real_t p_start, real_t p_end, int p_points, const Color &p_color, real_t p_width) {
	DRAW_CHECK();
	p_points = Math::clamp(p_points, 2, 256);
	std::vector<Vector2> pts;
	for (int i = 0; i < p_points; i++) {
		real_t a = p_start + (p_end - p_start) * (real_t)i / (real_t)(p_points - 1);
		pts.push_back(p_center + Vector2::from_angle(a) * p_radius);
	}
	RenderingServer::get_singleton()->canvas_item_add_polyline(canvas_item, pts, p_color, p_width, false);
}

void CanvasItem::draw_colored_polygon(const std::vector<Vector2> &p_points, const Color &p_color) {
	DRAW_CHECK();
	RenderingServer::get_singleton()->canvas_item_add_polygon(canvas_item, p_points, p_color);
}

void CanvasItem::draw_polyline(const std::vector<Vector2> &p_points, const Color &p_color, real_t p_width, bool p_closed) {
	DRAW_CHECK();
	RenderingServer::get_singleton()->canvas_item_add_polyline(canvas_item, p_points, p_color, p_width, p_closed);
}

void CanvasItem::draw_texture(const Ref<Texture2D> &p_texture, const Vector2 &p_pos, const Color &p_modulate) {
	DRAW_CHECK();
	if (p_texture.is_null()) {
		return;
	}
	RenderingServer::get_singleton()->canvas_item_add_texture_rect(canvas_item, Rect2(p_pos, p_texture->get_size()), p_texture->get_rid(), p_modulate);
}

void CanvasItem::draw_texture_rect(const Ref<Texture2D> &p_texture, const Rect2 &p_rect, const Color &p_modulate) {
	DRAW_CHECK();
	if (p_texture.is_null()) {
		return;
	}
	RenderingServer::get_singleton()->canvas_item_add_texture_rect(canvas_item, p_rect, p_texture->get_rid(), p_modulate);
}

void CanvasItem::draw_texture_rect_region(const Ref<Texture2D> &p_texture, const Rect2 &p_rect, const Rect2 &p_src_rect, const Color &p_modulate) {
	DRAW_CHECK();
	if (p_texture.is_null()) {
		return;
	}
	RenderingServer::get_singleton()->canvas_item_add_texture_rect_region(canvas_item, p_rect, p_texture->get_rid(), p_src_rect, p_modulate);
}

void CanvasItem::draw_string(const Ref<Font> &p_font, const Vector2 &p_pos, const String &p_text, int p_font_size, const Color &p_color) {
	DRAW_CHECK();
	Ref<Font> font = p_font.is_valid() ? p_font : Font::get_default();
	font->draw_string(canvas_item, p_pos, p_text, p_font_size, p_color);
}

static std::vector<Vector2> _array_to_points(const Array &p_points) {
	std::vector<Vector2> pts;
	for (const Variant &v : p_points.vec()) {
		pts.push_back(v.as_vector2());
	}
	return pts;
}

void CanvasItem::_draw_polygon_bind(const Array &p_points, const Color &p_color) {
	draw_colored_polygon(_array_to_points(p_points), p_color);
}

void CanvasItem::_draw_polyline_bind(const Array &p_points, const Color &p_color, real_t p_width) {
	draw_polyline(_array_to_points(p_points), p_color, p_width, false);
}

void CanvasItem::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_visible", "visible"), &CanvasItem::set_visible);
	ClassDB::bind_method(D_METHOD("is_visible"), &CanvasItem::is_visible);
	ClassDB::bind_method(D_METHOD("is_visible_in_tree"), &CanvasItem::is_visible_in_tree);
	ClassDB::bind_method(D_METHOD("show"), &CanvasItem::show);
	ClassDB::bind_method(D_METHOD("hide"), &CanvasItem::hide);
	ClassDB::bind_method(D_METHOD("set_modulate", "color"), &CanvasItem::set_modulate);
	ClassDB::bind_method(D_METHOD("get_modulate"), &CanvasItem::get_modulate);
	ClassDB::bind_method(D_METHOD("set_self_modulate", "color"), &CanvasItem::set_self_modulate);
	ClassDB::bind_method(D_METHOD("get_self_modulate"), &CanvasItem::get_self_modulate);
	ClassDB::bind_method(D_METHOD("set_z_index", "z"), &CanvasItem::set_z_index);
	ClassDB::bind_method(D_METHOD("get_z_index"), &CanvasItem::get_z_index);
	ClassDB::bind_method(D_METHOD("set_z_as_relative", "relative"), &CanvasItem::set_z_as_relative);
	ClassDB::bind_method(D_METHOD("is_z_relative"), &CanvasItem::is_z_relative);
	ClassDB::bind_method(D_METHOD("set_material", "material"), &CanvasItem::set_material);
	ClassDB::bind_method(D_METHOD("get_material"), &CanvasItem::get_material);
	ClassDB::bind_method(D_METHOD("set_clip_rect", "enabled", "rect"), &CanvasItem::set_clip_rect, DEFVAL(Rect2()));
	ClassDB::bind_method(D_METHOD("get_global_mouse_position"), &CanvasItem::get_global_mouse_position);
	ClassDB::bind_method(D_METHOD("get_local_mouse_position"), &CanvasItem::get_local_mouse_position);
	ClassDB::bind_method(D_METHOD("get_viewport_size"), &CanvasItem::get_viewport_size);
	ClassDB::bind_method(D_METHOD("queue_redraw"), &CanvasItem::queue_redraw);
	ClassDB::bind_method(D_METHOD("draw_line", "from", "to", "color", "width"), &CanvasItem::draw_line, DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("draw_rect", "rect", "color", "filled", "width"), &CanvasItem::draw_rect, DEFVAL(true), DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("draw_circle", "position", "radius", "color"), &CanvasItem::draw_circle);
	ClassDB::bind_method(D_METHOD("draw_arc", "center", "radius", "start", "end", "points", "color", "width"), &CanvasItem::draw_arc, DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("draw_colored_polygon", "points", "color"), &CanvasItem::_draw_polygon_bind);
	ClassDB::bind_method(D_METHOD("draw_polyline", "points", "color", "width"), &CanvasItem::_draw_polyline_bind, DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("draw_texture", "texture", "position", "modulate"), &CanvasItem::draw_texture, DEFVAL(Color(1, 1, 1, 1)));
	ClassDB::bind_method(D_METHOD("draw_texture_rect", "texture", "rect", "modulate"), &CanvasItem::draw_texture_rect, DEFVAL(Color(1, 1, 1, 1)));
	ClassDB::bind_method(D_METHOD("draw_texture_rect_region", "texture", "rect", "src_rect", "modulate"), &CanvasItem::draw_texture_rect_region, DEFVAL(Color(1, 1, 1, 1)));
	ClassDB::bind_method(D_METHOD("draw_string", "font", "position", "text", "font_size", "color"), &CanvasItem::draw_string, DEFVAL(16), DEFVAL(Color(1, 1, 1, 1)));

	ADD_GROUP("Visibility");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "visible"), "set_visible", "is_visible");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "modulate"), "set_modulate", "get_modulate");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "self_modulate"), "set_self_modulate", "get_self_modulate");
	ADD_GROUP("Ordering");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "z_index", PROPERTY_HINT_RANGE, "-4096,4096,1"), "set_z_index", "get_z_index");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "z_as_relative"), "set_z_as_relative", "is_z_relative");
	ADD_GROUP("Material");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "CanvasItemMaterial"), "set_material", "get_material");

	ADD_SIGNAL(MethodInfo("visibility_changed"));
	BIND_CONSTANT(NOTIFICATION_DRAW);
}
