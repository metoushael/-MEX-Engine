#include "light_2d.h"

#include "scene/main/canvas_layer.h"
#include "scene/main/scene_tree.h"

// Canevas dans lequel un nœud est dessiné (celui de sa CanvasLayer, sinon celui de l'arbre).
static RID _canvas_of(CanvasItem *p_node) {
	if (CanvasLayer *layer = p_node->get_canvas_layer_node()) {
		return layer->get_canvas();
	}
	return p_node->get_tree()->get_canvas();
}

// --- CanvasModulate ---
void CanvasModulate::_update() {
	if (canvas.is_valid()) {
		RenderingServer::get_singleton()->canvas_set_ambient(canvas, is_visible_in_tree(), color);
	}
}

void CanvasModulate::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			canvas = _canvas_of(this);
			_update();
		} break;
		case NOTIFICATION_VISIBILITY_CHANGED: {
			_update();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (canvas.is_valid()) {
				RenderingServer::get_singleton()->canvas_set_ambient(canvas, false);
			}
			canvas = RID();
		} break;
	}
}

void CanvasModulate::set_color(const Color &p_color) {
	color = p_color;
	_update();
}

void CanvasModulate::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_color", "color"), &CanvasModulate::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &CanvasModulate::get_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color"), "set_color", "get_color");
}

// --- PointLight2D ---
PointLight2D::PointLight2D() {
	light = RenderingServer::get_singleton()->canvas_light_create();
}

PointLight2D::~PointLight2D() {
	if (RenderingServer::get_singleton()) {
		RenderingServer::get_singleton()->free_rid(light);
	}
}

void PointLight2D::_notification(int p_what) {
	RenderingServer *rs = RenderingServer::get_singleton();
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			rs->canvas_light_set_canvas(light, _canvas_of(this));
			rs->canvas_light_set_position(light, get_global_position());
			rs->canvas_light_set_enabled(light, enabled && is_visible_in_tree());
			set_process_internal(true);
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			// La lumière suit le nœud ; son échelle agrandit le halo.
			rs->canvas_light_set_position(light, get_global_position());
			rs->canvas_light_set_scale(light, texture_scale * get_global_scale().x);
			rs->canvas_light_set_enabled(light, enabled && is_visible_in_tree());
		} break;
		case NOTIFICATION_EXIT_TREE: {
			rs->canvas_light_set_canvas(light, RID());
		} break;
		case NOTIFICATION_DRAW: {
			if (get_tree()->is_edit_mode()) {
				// Repère dans l'éditeur : un petit soleil.
				const Color mark(1.0f, 0.85f, 0.3f, 0.9f);
				draw_circle(Vector2(), 5, mark);
				for (int i = 0; i < 8; i++) {
					const Vector2 dir = Vector2(1, 0).rotated((real_t)(i * Math_PI / 4.0));
					draw_line(dir * 8, dir * 12, mark, 1.5f);
				}
			}
		} break;
	}
}

void PointLight2D::set_enabled(bool p_enabled) {
	enabled = p_enabled;
	RenderingServer::get_singleton()->canvas_light_set_enabled(light, enabled && is_inside_tree() && is_visible_in_tree());
}

void PointLight2D::set_texture(const Ref<Texture2D> &p_texture) {
	texture = p_texture;
	RenderingServer::get_singleton()->canvas_light_set_texture(light, texture.is_valid() ? texture->get_rid() : RID());
}

void PointLight2D::set_texture_scale(real_t p_scale) {
	texture_scale = p_scale;
	RenderingServer::get_singleton()->canvas_light_set_scale(light, texture_scale);
}

void PointLight2D::set_color(const Color &p_color) {
	color = p_color;
	RenderingServer::get_singleton()->canvas_light_set_color(light, color);
}

void PointLight2D::set_energy(real_t p_energy) {
	energy = p_energy;
	RenderingServer::get_singleton()->canvas_light_set_energy(light, energy);
}

void PointLight2D::set_shadow_enabled(bool p_enabled) {
	shadow_enabled = p_enabled;
	RenderingServer::get_singleton()->canvas_light_set_shadow_enabled(light, shadow_enabled);
}

void PointLight2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &PointLight2D::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &PointLight2D::is_enabled);
	ClassDB::bind_method(D_METHOD("set_texture", "texture"), &PointLight2D::set_texture);
	ClassDB::bind_method(D_METHOD("get_texture"), &PointLight2D::get_texture);
	ClassDB::bind_method(D_METHOD("set_texture_scale", "scale"), &PointLight2D::set_texture_scale);
	ClassDB::bind_method(D_METHOD("get_texture_scale"), &PointLight2D::get_texture_scale);
	ClassDB::bind_method(D_METHOD("set_color", "color"), &PointLight2D::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &PointLight2D::get_color);
	ClassDB::bind_method(D_METHOD("set_energy", "energy"), &PointLight2D::set_energy);
	ClassDB::bind_method(D_METHOD("get_energy"), &PointLight2D::get_energy);
	ClassDB::bind_method(D_METHOD("set_shadow_enabled", "enabled"), &PointLight2D::set_shadow_enabled);
	ClassDB::bind_method(D_METHOD("is_shadow_enabled"), &PointLight2D::is_shadow_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_texture", "get_texture");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "texture_scale", PROPERTY_HINT_RANGE, "0.01,50,0.01"), "set_texture_scale", "get_texture_scale");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color"), "set_color", "get_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "energy", PROPERTY_HINT_RANGE, "0,8,0.01"), "set_energy", "get_energy");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shadow_enabled"), "set_shadow_enabled", "is_shadow_enabled");
}

// --- LightOccluder2D ---
LightOccluder2D::LightOccluder2D() {
	occluder = RenderingServer::get_singleton()->canvas_occluder_create();
}

LightOccluder2D::~LightOccluder2D() {
	if (RenderingServer::get_singleton()) {
		RenderingServer::get_singleton()->free_rid(occluder);
	}
}

void LightOccluder2D::_update_polygon() {
	std::vector<Vector2> points;
	for (int i = 0; i < polygon.size(); i++) {
		points.push_back(polygon[i].as_vector2());
	}
	RenderingServer::get_singleton()->canvas_occluder_set_polygon(occluder, points, closed);
	queue_redraw();
}

void LightOccluder2D::_notification(int p_what) {
	RenderingServer *rs = RenderingServer::get_singleton();
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			rs->canvas_occluder_set_canvas(occluder, _canvas_of(this));
			rs->canvas_occluder_set_transform(occluder, get_global_transform());
			set_process_internal(true);
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			rs->canvas_occluder_set_transform(occluder, get_global_transform());
			rs->canvas_occluder_set_enabled(occluder, enabled && is_visible_in_tree());
		} break;
		case NOTIFICATION_EXIT_TREE: {
			rs->canvas_occluder_set_canvas(occluder, RID());
		} break;
		case NOTIFICATION_DRAW: {
			if (get_tree()->is_edit_mode() && polygon.size() >= 2) {
				std::vector<Vector2> points;
				for (int i = 0; i < polygon.size(); i++) {
					points.push_back(polygon[i].as_vector2());
				}
				draw_polyline(points, Color(0.2f, 0.2f, 0.25f, 0.9f), 2.0f, closed);
			}
		} break;
	}
}

void LightOccluder2D::set_polygon(const Array &p_polygon) {
	polygon = p_polygon;
	_update_polygon();
}

void LightOccluder2D::set_closed(bool p_closed) {
	closed = p_closed;
	_update_polygon();
}

void LightOccluder2D::set_enabled(bool p_enabled) {
	enabled = p_enabled;
	RenderingServer::get_singleton()->canvas_occluder_set_enabled(occluder, enabled);
}

void LightOccluder2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_polygon", "polygon"), &LightOccluder2D::set_polygon);
	ClassDB::bind_method(D_METHOD("get_polygon"), &LightOccluder2D::get_polygon);
	ClassDB::bind_method(D_METHOD("set_closed", "closed"), &LightOccluder2D::set_closed);
	ClassDB::bind_method(D_METHOD("is_closed"), &LightOccluder2D::is_closed);
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &LightOccluder2D::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &LightOccluder2D::is_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "polygon"), "set_polygon", "get_polygon");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "closed"), "set_closed", "is_closed");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
}
