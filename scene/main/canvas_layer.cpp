#include "canvas_layer.h"

#include "scene/main/scene_tree.h"

CanvasLayer::CanvasLayer() {
	canvas = RenderingServer::get_singleton()->canvas_create();
}

CanvasLayer::~CanvasLayer() {
	if (RenderingServer::get_singleton()) {
		RenderingServer::get_singleton()->free_rid(canvas);
	}
}

void CanvasLayer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			get_tree()->register_canvas_layer(this);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			get_tree()->unregister_canvas_layer(this);
		} break;
	}
}

void CanvasLayer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_layer", "layer"), &CanvasLayer::set_layer);
	ClassDB::bind_method(D_METHOD("get_layer"), &CanvasLayer::get_layer);
	ClassDB::bind_method(D_METHOD("set_offset", "offset"), &CanvasLayer::set_offset);
	ClassDB::bind_method(D_METHOD("get_offset"), &CanvasLayer::get_offset);
	ClassDB::bind_method(D_METHOD("set_scale", "scale"), &CanvasLayer::set_scale);
	ClassDB::bind_method(D_METHOD("get_scale"), &CanvasLayer::get_scale);
	ClassDB::bind_method(D_METHOD("set_visible", "visible"), &CanvasLayer::set_visible);
	ClassDB::bind_method(D_METHOD("is_visible"), &CanvasLayer::is_visible);
	ClassDB::bind_method(D_METHOD("show"), &CanvasLayer::set_visible, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("hide"), &CanvasLayer::set_visible, DEFVAL(false));

	ADD_PROPERTY(PropertyInfo(Variant::INT, "layer", PROPERTY_HINT_RANGE, "-128,128,1"), "set_layer", "get_layer");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "offset"), "set_offset", "get_offset");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "scale"), "set_scale", "get_scale");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "visible"), "set_visible", "is_visible");
}
