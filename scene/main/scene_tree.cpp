#include "scene_tree.h"

#include "scene/gui/control.h"

#include "core/config/engine.h"
#include "core/debug_monitor.h"
#include "core/os/os.h"
#include "core/string/translation.h"
#include "scene/animation/tween.h"
#include "core/input/input.h"
#include "core/config/project_settings.h"
#include "core/io/resource_loader.h"
#include "scene/main/canvas_item.h"
#include "scene/main/canvas_layer.h"
#include "scene/resources/font.h"
#include "scene/resources/packed_scene.h"
#include "scene/3d/visual_3d.h"
#include "servers/physics_server_2d.h"
#include "servers/physics_server_3d.h"

std::vector<ObjectID> SceneTree::delete_queue;

enum {
	KIND_PROCESS,
	KIND_PHYSICS,
	KIND_INPUT,
	KIND_UNHANDLED_INPUT,
};

SceneTree::SceneTree(bool p_edit_mode) {
	edit_mode = p_edit_mode;
	canvas = RenderingServer::get_singleton()->canvas_create();
	space = PhysicsServer2D::get_singleton()->space_create();
	real_t gravity = (real_t)GLOBAL_GET("physics/gravity").as_float();
	PhysicsServer2D::get_singleton()->space_set_gravity(space, Vector2(0, gravity));
	debug_collisions_hint = GLOBAL_GET("debug/visible_collision_shapes").as_bool();
	if (RenderingServer3D::get_singleton()) {
		scenario = RenderingServer3D::get_singleton()->scenario_create();
	}
	space_3d = PhysicsServer3D::get_singleton()->space_create();
	PhysicsServer3D::get_singleton()->space_set_gravity(space_3d, Vector3(0, -(real_t)GLOBAL_GET("physics/3d/gravity").as_float(), 0));

	root = memnew(Node);
	root->set_name("root");
	root->tree = this;
	root->_propagate_enter_tree();
	root->_propagate_ready();
}

SceneTree::~SceneTree() {
	if (root) {
		root->_propagate_exit_tree();
		memdelete(root);
		root = nullptr;
	}
	flush_delete_queue();
	PhysicsServer2D::get_singleton()->free_rid(space);
	PhysicsServer3D::get_singleton()->free_rid(space_3d);
	if (RenderingServer3D::get_singleton()) {
		if (viewport_3d.is_valid()) {
			RenderingServer3D::get_singleton()->free_rid(viewport_3d);
		}
		RenderingServer3D::get_singleton()->free_rid(scenario);
	}
	RenderingServer::get_singleton()->free_rid(canvas);
}

void SceneTree::set_pause(bool p_paused) {
	if (paused == p_paused) {
		return;
	}
	paused = p_paused;
	root->propagate_notification(p_paused ? Node::NOTIFICATION_PAUSED : Node::NOTIFICATION_UNPAUSED);
}

// ---------------------------------------------------------------------------
// Boucle
// ---------------------------------------------------------------------------
void SceneTree::_collect_nodes(Node *p_node, std::vector<ObjectID> &r_list, int p_kind) const {
	bool wanted = false;
	switch (p_kind) {
		case KIND_PROCESS: wanted = p_node->process || p_node->process_internal; break;
		case KIND_PHYSICS: wanted = p_node->physics_process || p_node->physics_process_internal; break;
		case KIND_INPUT: wanted = p_node->process_input; break;
		case KIND_UNHANDLED_INPUT: wanted = p_node->process_unhandled_input; break;
	}
	if (wanted) {
		r_list.push_back(p_node->get_instance_id());
	}
	for (Node *c : p_node->children) {
		_collect_nodes(c, r_list, p_kind);
	}
}

void SceneTree::_notify_nodes(int p_kind) {
	// On fige la liste (par ID) : les scripts peuvent ajouter/supprimer des nœuds.
	std::vector<ObjectID> list;
	_collect_nodes(root, list, p_kind);
	for (ObjectID id : list) {
		Node *node = static_cast<Node *>(ObjectDB::get_instance(id));
		if (!node || !node->inside_tree || node->tree != this || !node->can_process()) {
			continue;
		}
		if (p_kind == KIND_PROCESS) {
			if (node->process_internal) {
				node->notification(Node::NOTIFICATION_INTERNAL_PROCESS);
			}
			if (ObjectDB::get_instance(id) && node->process && node->inside_tree) {
				node->notification(Node::NOTIFICATION_PROCESS);
			}
		} else {
			if (node->physics_process_internal) {
				node->notification(Node::NOTIFICATION_INTERNAL_PHYSICS_PROCESS);
			}
			if (ObjectDB::get_instance(id) && node->physics_process && node->inside_tree) {
				node->notification(Node::NOTIFICATION_PHYSICS_PROCESS);
			}
		}
	}
}

void SceneTree::physics_process(double p_delta) {
	Engine *engine = Engine::get_singleton();
	const uint64_t physics_start = OS::get_singleton()->get_ticks_usec();
	engine->set_in_physics_frame(true);
	physics_process_time = p_delta;
	if (!paused) {
		// Nœuds -> serveur, simulation, puis serveur -> nœuds (et signaux différés).
		std::vector<PhysicsSyncListener *> listeners = physics_listeners;
		for (PhysicsSyncListener *l : listeners) {
			if (std::find(physics_listeners.begin(), physics_listeners.end(), l) != physics_listeners.end()) {
				l->_physics_pre_step();
			}
		}
		PhysicsServer2D::get_singleton()->space_step(space, (real_t)p_delta);
		PhysicsServer3D::get_singleton()->space_step(space_3d, (real_t)p_delta);
		listeners = physics_listeners;
		for (PhysicsSyncListener *l : listeners) {
			if (std::find(physics_listeners.begin(), physics_listeners.end(), l) != physics_listeners.end()) {
				l->_physics_post_step();
			}
		}
	}
	_notify_nodes(KIND_PHYSICS);
	flush_delete_queue();
	engine->set_in_physics_frame(false);
	if (!edit_mode && Performance::get_singleton()) {
		Performance::get_singleton()->set_monitor_value(Performance::TIME_PHYSICS_PROCESS, (double)(OS::get_singleton()->get_ticks_usec() - physics_start) / 1000000.0);
	}
}

void SceneTree::process(double p_delta) {
	process_time = p_delta;
	const uint64_t process_start = OS::get_singleton()->get_ticks_usec();
	// Langue changée : les textes affichés se mettent à jour.
	if (TranslationServer *translations = TranslationServer::get_singleton()) {
		if (translations->get_generation() != translation_generation) {
			const bool first = translation_generation == 0;
			translation_generation = translations->get_generation();
			if (!first && root) {
				root->propagate_notification(Node::NOTIFICATION_TRANSLATION_CHANGED);
			}
		}
	}
	_notify_nodes(KIND_PROCESS);

	if (!paused && !timers.empty()) {
		std::vector<Ref<SceneTreeTimer>> copy = timers;
		for (Ref<SceneTreeTimer> &timer : copy) {
			timer->set_time_left(timer->get_time_left() - p_delta);
			if (timer->get_time_left() <= 0) {
				timers.erase(std::remove(timers.begin(), timers.end(), timer), timers.end());
				timer->emit_signal("timeout");
			}
		}
	}

	if (!paused && !tweens.empty()) {
		// Copie : un rappel peut créer ou tuer des tweens.
		const std::vector<Ref<Tween>> copy = tweens;
		for (const Ref<Tween> &tween : copy) {
			if (!tween->is_dead() && !tween->is_running()) {
				continue; // en pause
			}
			if (!const_cast<Tween *>(tween.ptr())->custom_step(p_delta)) {
				tweens.erase(std::remove(tweens.begin(), tweens.end(), tween), tweens.end());
			}
		}
	}

	flush_delete_queue();
	_flush_scene_change();
	if (!edit_mode) {
		if (Performance *performance = Performance::get_singleton()) {
			performance->set_monitor_value(Performance::TIME_PROCESS, (double)(OS::get_singleton()->get_ticks_usec() - process_start) / 1000000.0);
			performance->set_monitor_value(Performance::OBJECT_NODE_COUNT, (double)get_node_count());
		}
	}

	// La caméra suit sa cible après tous les déplacements de l'image.
	if (Object *camera = ObjectDB::get_instance(current_camera)) {
		camera->notification(NOTIFICATION_UPDATE_CAMERA);
	}
}

void SceneTree::input_event(const Ref<InputEvent> &p_event) {
	input_handled = false;
	Control::begin_input_event(p_event); // manette : équivalent clavier pour l'interface
	std::vector<ObjectID> list;
	_collect_nodes(root, list, KIND_INPUT);
	// Comme dans Godot : du dernier nœud de l'arbre vers le premier.
	for (auto it = list.rbegin(); it != list.rend() && !input_handled; ++it) {
		Node *node = static_cast<Node *>(ObjectDB::get_instance(*it));
		if (node && node->inside_tree && node->tree == this && node->can_process()) {
			node->_call_input(p_event);
		}
	}
	if (input_handled) {
		return;
	}
	list.clear();
	_collect_nodes(root, list, KIND_UNHANDLED_INPUT);
	for (auto it = list.rbegin(); it != list.rend() && !input_handled; ++it) {
		Node *node = static_cast<Node *>(ObjectDB::get_instance(*it));
		if (node && node->inside_tree && node->tree == this && node->can_process()) {
			node->_call_unhandled_input(p_event);
		}
	}
	// Tab et les flèches déplacent le focus de l'interface si personne n'a utilisé la touche.
	if (!input_handled && !edit_mode && Control::handle_focus_navigation(this, p_event)) {
		input_handled = true;
	}
}

void SceneTree::_update_canvas_order(Node *p_node, int &r_index) {
	if (CanvasItem *ci = Object::cast_to<CanvasItem>(p_node)) {
		RenderingServer::get_singleton()->canvas_item_set_draw_index(ci->get_canvas_item(), r_index++);
	}
	for (Node *c : p_node->children) {
		_update_canvas_order(c, r_index);
	}
}

void SceneTree::set_render_scale(float p_scale) {
	// Par quarts : un redimensionnement progressif de la fenêtre ne régénère pas tout à chaque image.
	render_scale = Math::clamp(std::round(p_scale * 4.0f) / 4.0f, 0.25f, 4.0f);
}

static void _queue_redraw_all(Node *p_node) {
	if (CanvasItem *item = Object::cast_to<CanvasItem>(p_node)) {
		item->queue_redraw();
	}
	for (Node *child : p_node->get_children_vector()) {
		_queue_redraw_all(child);
	}
}

void SceneTree::draw(const Transform2D *p_override_xform) {
	Font::set_oversampling(render_scale);
	if (render_scale != drawn_render_scale) {
		drawn_render_scale = render_scale;
		_queue_redraw_all(root); // le texte est retracé à la nouvelle taille
	}
	CanvasItem::flush_redraws();
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!edit_mode && !p_override_xform) {
		_draw_3d(); // la 3D d'abord : le canvas 2D et l'interface se dessinent par-dessus
	}
	if (canvas_order_dirty) {
		// L'ordre de dessin suit l'ordre de l'arbre (parcours en profondeur).
		canvas_order_dirty = false;
		int index = 0;
		_update_canvas_order(root, index);
	}
	std::vector<CanvasLayer *> layers = canvas_layers;
	std::stable_sort(layers.begin(), layers.end(), [](const CanvasLayer *a, const CanvasLayer *b) {
		return a->get_layer() < b->get_layer();
	});
	auto draw_layer = [&](CanvasLayer *layer) {
		if (!layer->is_visible()) {
			return;
		}
		Transform2D xform = layer->get_layer_transform();
		if (p_override_xform) {
			xform = *p_override_xform * xform;
		}
		rs->draw_canvas(layer->get_canvas(), xform);
	};
	for (CanvasLayer *layer : layers) {
		if (layer->get_layer() < 0) {
			draw_layer(layer);
		}
	}
	rs->draw_canvas(canvas, p_override_xform ? *p_override_xform : canvas_transform);
	for (CanvasLayer *layer : layers) {
		if (layer->get_layer() >= 0) {
			draw_layer(layer);
		}
	}
}

void SceneTree::_collect_debug_lines_3d(Node *p_node) {
	if (Node3D *n = Object::cast_to<Node3D>(p_node)) {
		if (!n->is_visible()) {
			return;
		}
		n->_get_debug_collision_lines(&debug_lines_3d);
	}
	for (Node *c : p_node->get_children_vector()) {
		_collect_debug_lines_3d(c);
	}
}

// Rendu de la scène 3D par la Camera3D active, puis copie dans la cible 2D courante.
// Sans caméra 3D, rien n'est fait : un jeu purement 2D ne paie aucun coût.
void SceneTree::_draw_3d() {
	RenderingServer3D *rs3d = RenderingServer3D::get_singleton();
	Camera3D *camera = Object::cast_to<Camera3D>(ObjectDB::get_instance(current_camera_3d));
	if (!rs3d || !rs3d->is_available() || !camera) {
		return;
	}
	// La 3D est rendue à la définition réellement affichée, puis posée sur la résolution logique.
	const int w = std::max(1, (int)std::round(viewport_size.x * render_scale)), h = std::max(1, (int)std::round(viewport_size.y * render_scale));
	if (!viewport_3d.is_valid()) {
		viewport_3d = rs3d->viewport_create(w, h);
	} else if (rs3d->viewport_get_size(viewport_3d) != Size2((real_t)w, (real_t)h)) {
		rs3d->viewport_set_size(viewport_3d, w, h);
	}
	RenderingServer3D::RenderExtras extras;
	extras.fallback_background = GLOBAL_GET("rendering/clear_color").as_color();
	debug_lines_3d.clear();
	if (debug_collisions_hint) {
		_collect_debug_lines_3d(root);
		extras.lines = &debug_lines_3d;
	}
	rs3d->render(viewport_3d, scenario, camera->get_camera_data(), extras);
	RenderingServer::get_singleton()->draw_texture_immediate(Rect2(Vector2(), viewport_size), rs3d->viewport_get_texture(viewport_3d));
}

// ---------------------------------------------------------------------------
// Scènes
// ---------------------------------------------------------------------------
Error SceneTree::change_scene_to_file(const String &p_path) {
	if (!ResourceLoader::exists(p_path)) {
		print_error("change_scene_to_file(): scène introuvable : " + p_path);
		return ERR_FILE_NOT_FOUND;
	}
	pending_scene_path = p_path;
	pending_scene_change = true; // appliqué en fin de frame
	return OK;
}

Error SceneTree::reload_current_scene() {
	ERR_FAIL_COND_V_MSG(!current_scene, ERR_UNAVAILABLE, "reload_current_scene(): aucune scène courante.");
	String path = current_scene->get_scene_file_path();
	ERR_FAIL_COND_V_MSG(path.empty(), ERR_UNAVAILABLE, "reload_current_scene(): la scène courante n'a pas de fichier.");
	return change_scene_to_file(path);
}

void SceneTree::_flush_scene_change() {
	if (!pending_scene_change) {
		return;
	}
	pending_scene_change = false;
	Ref<PackedScene> packed = ResourceLoader::load(pending_scene_path);
	if (packed.is_null()) {
		print_error("Impossible de charger la scène : " + pending_scene_path);
		return;
	}
	if (current_scene) {
		memdelete(current_scene);
		current_scene = nullptr;
	}
	canvas_transform = Transform2D();
	Node *scene = packed->instantiate();
	if (!scene) {
		return;
	}
	root->add_child(scene);
	current_scene = scene;
}

Ref<SceneTreeTimer> SceneTree::create_timer(double p_seconds) {
	Ref<SceneTreeTimer> timer;
	timer.instantiate();
	timer->set_time_left(p_seconds);
	timers.push_back(timer);
	return timer;
}

Ref<Tween> SceneTree::create_tween() {
	Ref<Tween> tween;
	tween.instantiate();
	tweens.push_back(tween);
	return tween;
}

// ---------------------------------------------------------------------------
// Groupes
// ---------------------------------------------------------------------------
void SceneTree::_add_to_group(const String &p_group, Node *p_node) {
	std::vector<Node *> &nodes = group_map[p_group];
	if (std::find(nodes.begin(), nodes.end(), p_node) == nodes.end()) {
		nodes.push_back(p_node);
	}
}

void SceneTree::_remove_from_group(const String &p_group, Node *p_node) {
	auto it = group_map.find(p_group);
	if (it == group_map.end()) {
		return;
	}
	std::vector<Node *> &nodes = it->second;
	nodes.erase(std::remove(nodes.begin(), nodes.end(), p_node), nodes.end());
	if (nodes.empty()) {
		group_map.erase(it);
	}
}

Array SceneTree::get_nodes_in_group(const String &p_group) const {
	Array out;
	auto it = group_map.find(p_group);
	if (it != group_map.end()) {
		for (Node *n : it->second) {
			out.push_back(Variant(n));
		}
	}
	return out;
}

Node *SceneTree::get_first_node_in_group(const String &p_group) const {
	auto it = group_map.find(p_group);
	return (it == group_map.end() || it->second.empty()) ? nullptr : it->second.front();
}

bool SceneTree::has_group(const String &p_group) const {
	return group_map.count(p_group) > 0;
}

static int _count_nodes(const Node *p_node) {
	int n = 1;
	for (Node *c : p_node->get_children_vector()) {
		n += _count_nodes(c);
	}
	return n;
}

int SceneTree::get_node_count() const {
	return _count_nodes(root);
}

Variant SceneTree::_call_group_bind(const Variant **p_args, int p_argc, CallError &r_error) {
	if (p_argc < 2) {
		r_error.error = CallError::CALL_ERROR_TOO_FEW_ARGUMENTS;
		r_error.expected = 2;
		return Variant();
	}
	Array nodes = get_nodes_in_group(p_args[0]->as_string());
	String method = p_args[1]->as_string();
	for (const Variant &v : nodes.vec()) {
		Object *obj = v.as_object();
		if (obj && obj->has_method(method)) {
			CallError ce;
			obj->callp(method, p_args + 2, p_argc - 2, ce);
		}
	}
	return Variant();
}

// ---------------------------------------------------------------------------
// Suppression différée
// ---------------------------------------------------------------------------
void SceneTree::queue_delete(Object *p_object) {
	if (!p_object) {
		return;
	}
	ObjectID id = p_object->get_instance_id();
	if (std::find(delete_queue.begin(), delete_queue.end(), id) == delete_queue.end()) {
		delete_queue.push_back(id);
	}
}

void SceneTree::flush_delete_queue() {
	while (!delete_queue.empty()) {
		std::vector<ObjectID> queue;
		queue.swap(delete_queue);
		for (ObjectID id : queue) {
			Object *obj = ObjectDB::get_instance(id);
			if (obj) {
				memdelete(obj);
			}
		}
	}
}

void SceneTree::register_canvas_layer(CanvasLayer *p_layer) {
	if (std::find(canvas_layers.begin(), canvas_layers.end(), p_layer) == canvas_layers.end()) {
		canvas_layers.push_back(p_layer);
	}
}

void SceneTree::unregister_canvas_layer(CanvasLayer *p_layer) {
	canvas_layers.erase(std::remove(canvas_layers.begin(), canvas_layers.end(), p_layer), canvas_layers.end());
}

void SceneTree::add_physics_listener(PhysicsSyncListener *p_listener) {
	if (std::find(physics_listeners.begin(), physics_listeners.end(), p_listener) == physics_listeners.end()) {
		physics_listeners.push_back(p_listener);
	}
}

void SceneTree::remove_physics_listener(PhysicsSyncListener *p_listener) {
	physics_listeners.erase(std::remove(physics_listeners.begin(), physics_listeners.end(), p_listener), physics_listeners.end());
}

void SceneTree::push_input(const Ref<InputEvent> &p_event) {
	if (p_event.is_null()) {
		return;
	}
	Input::get_singleton()->parse_input_event(p_event);
	input_event(p_event);
}

void SceneTree::_bind_methods() {
	ClassDB::bind_method(D_METHOD("push_input", "event"), &SceneTree::push_input);
	ClassDB::bind_method(D_METHOD("get_root"), &SceneTree::get_root);
	ClassDB::bind_method(D_METHOD("get_current_scene"), &SceneTree::get_current_scene);
	ClassDB::bind_method(D_METHOD("set_pause", "paused"), &SceneTree::set_pause);
	ClassDB::bind_method(D_METHOD("is_paused"), &SceneTree::is_paused);
	ClassDB::bind_method(D_METHOD("quit"), &SceneTree::quit);
	ClassDB::bind_method(D_METHOD("change_scene_to_file", "path"), &SceneTree::change_scene_to_file);
	ClassDB::bind_method(D_METHOD("reload_current_scene"), &SceneTree::reload_current_scene);
	ClassDB::bind_method(D_METHOD("create_timer", "seconds"), &SceneTree::create_timer);
	ClassDB::bind_method(D_METHOD("create_tween"), &SceneTree::create_tween);
	ClassDB::bind_method(D_METHOD("get_nodes_in_group", "group"), &SceneTree::get_nodes_in_group);
	ClassDB::bind_method(D_METHOD("get_first_node_in_group", "group"), &SceneTree::get_first_node_in_group);
	ClassDB::bind_method(D_METHOD("has_group", "group"), &SceneTree::has_group);
	ClassDB::bind_method(D_METHOD("get_node_count"), &SceneTree::get_node_count);
	ClassDB::bind_method(D_METHOD("set_input_as_handled"), &SceneTree::set_input_as_handled);
	ClassDB::bind_method(D_METHOD("get_viewport_size"), &SceneTree::get_viewport_size);
	ClassDB::bind_method(D_METHOD("set_debug_collisions_hint", "enabled"), &SceneTree::set_debug_collisions_hint);
	ClassDB::bind_method(D_METHOD("is_debugging_collisions_hint"), &SceneTree::is_debugging_collisions_hint);
	ClassDB::bind_vararg_method("call_group", &SceneTree::_call_group_bind);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "paused"), "set_pause", "is_paused");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "root", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_root");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "current_scene", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_current_scene");
}

void SceneTreeTimer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_time_left", "time"), &SceneTreeTimer::set_time_left);
	ClassDB::bind_method(D_METHOD("get_time_left"), &SceneTreeTimer::get_time_left);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "time_left"), "set_time_left", "get_time_left");
	ADD_SIGNAL(MethodInfo("timeout"));
}

static void _reanchor_controls(Node *p_node) {
	if (Control *c = Object::cast_to<Control>(p_node)) {
		// Les enfants suivent d'eux-mêmes quand leur parent change de taille.
		c->update_rect_from_anchors();
		return;
	}
	for (Node *child : p_node->get_children_vector()) {
		_reanchor_controls(child);
	}
}

void SceneTree::set_viewport_size(const Size2 &p_size) {
	if (viewport_size == p_size) {
		return;
	}
	viewport_size = p_size;
	if (root) {
		_reanchor_controls(root);
	}
}
