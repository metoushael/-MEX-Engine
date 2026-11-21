// SceneTree : boucle de jeu, gestion de l'arbre de nœuds, des groupes et du rendu.
#pragma once

#include "core/object/ref_counted.h"
#include "scene/main/node.h"
#include "servers/rendering_server.h"
#include "servers/rendering_server_3d.h"

class PackedScene;
class CanvasLayer;

// Minuteur à usage unique : get_tree().create_timer(1.0).timeout.connect(...)
class SceneTreeTimer : public RefCounted {
	GDCLASS(SceneTreeTimer, RefCounted)

	double time_left = 0;

protected:
	static void _bind_methods();

public:
	void set_time_left(double p_time) { time_left = p_time; }
	double get_time_left() const { return time_left; }
};

// Interface des nœuds synchronisés avec le serveur de physique à chaque pas.
class PhysicsSyncListener {
public:
	virtual void _physics_pre_step() = 0; // nœud -> serveur
	virtual void _physics_post_step() = 0; // serveur -> nœud, signaux
	virtual ~PhysicsSyncListener() {}
};

class SceneTree : public Object {
	GDCLASS(SceneTree, Object)

	friend class Node;

public:
	enum {
		// Envoyée à la caméra courante après _process, juste avant le rendu.
		NOTIFICATION_UPDATE_CAMERA = 1000,
	};

private:
	Node *root = nullptr;
	Node *current_scene = nullptr;
	bool edit_mode = false;
	bool paused = false;
	bool quit_requested = false;
	bool input_handled = false;
	bool debug_collisions_hint = false;
	double process_time = 0;
	double physics_process_time = 0;

	std::map<String, std::vector<Node *>> group_map;
	std::vector<Ref<SceneTreeTimer>> timers;
	static std::vector<ObjectID> delete_queue;

	RID canvas;
	RID space;
	Size2 viewport_size = Size2(1152, 648);
	float render_scale = 1.0f;
	float drawn_render_scale = 1.0f;
	Transform2D canvas_transform;
	std::vector<CanvasLayer *> canvas_layers;
	std::vector<PhysicsSyncListener *> physics_listeners;
	bool canvas_order_dirty = true;
	ObjectID current_camera = 0;

	// --- 3D ---
	RID scenario;
	RID space_3d;
	RID viewport_3d;
	ObjectID current_camera_3d = 0;
	std::vector<RenderingServer3D::Line> debug_lines_3d;

	void _draw_3d();
	void _collect_debug_lines_3d(Node *p_node);

	String pending_scene_path;
	bool pending_scene_change = false;

	void _add_to_group(const String &p_group, Node *p_node);
	void _remove_from_group(const String &p_group, Node *p_node);
	void _collect_nodes(Node *p_node, std::vector<ObjectID> &r_list, int p_kind) const;
	void _notify_nodes(int p_kind);
	void _flush_scene_change();
	void _update_canvas_order(Node *p_node, int &r_index);
	Variant _call_group_bind(const Variant **p_args, int p_argc, CallError &r_error);

protected:
	static void _bind_methods();

public:
	Node *get_root() const { return root; }
	Node *get_current_scene() const { return current_scene; }
	void set_current_scene(Node *p_scene) { current_scene = p_scene; }

	bool is_edit_mode() const { return edit_mode; }
	void set_pause(bool p_paused);
	bool is_paused() const { return paused; }
	void quit() { quit_requested = true; }
	bool is_quit_requested() const { return quit_requested; }
	void set_debug_collisions_hint(bool p_enabled) { debug_collisions_hint = p_enabled; }
	bool is_debugging_collisions_hint() const { return debug_collisions_hint; }

	// --- Boucle principale ---
	void physics_process(double p_delta);
	void process(double p_delta);
	void input_event(const Ref<InputEvent> &p_event);
	void set_input_as_handled() { input_handled = true; }
	// Dessine l'arbre dans la cible courante. p_override_xform : vue de l'éditeur.
	void draw(const Transform2D *p_override_xform = nullptr);
	double get_process_time() const { return process_time; }
	double get_physics_process_time() const { return physics_process_time; }

	// --- Scènes ---
	Error change_scene_to_file(const String &p_path);
	Error reload_current_scene();
	Ref<SceneTreeTimer> create_timer(double p_seconds);

	// --- Groupes ---
	Array get_nodes_in_group(const String &p_group) const;
	Node *get_first_node_in_group(const String &p_group) const;
	bool has_group(const String &p_group) const;
	int get_node_count() const;

	// --- Suppression différée ---
	static void queue_delete(Object *p_object);
	static void flush_delete_queue();

	// --- Rendu / physique ---
	RID get_canvas() const { return canvas; }
	RID get_space() const { return space; }
	void set_viewport_size(const Size2 &p_size);
	Size2 get_viewport_size() const { return viewport_size; }
	// Rapport entre les pixels réellement affichés et la résolution logique (fenêtre agrandie,
	// écran à haute densité). La 3D est rendue à cette définition et le texte tracé à cette
	// taille : l'image reste nette quelle que soit la taille de la fenêtre.
	void set_render_scale(float p_scale);
	float get_render_scale() const { return render_scale; }
	void set_canvas_transform(const Transform2D &p_xform) { canvas_transform = p_xform; }
	Transform2D get_canvas_transform() const { return canvas_transform; }
	void set_current_camera(ObjectID p_camera) { current_camera = p_camera; }
	ObjectID get_current_camera() const { return current_camera; }
	// 3D : scénario de rendu, espace de physique et caméra active.
	RID get_scenario() const { return scenario; }
	RID get_space_3d() const { return space_3d; }
	void set_current_camera_3d(ObjectID p_camera) { current_camera_3d = p_camera; }
	ObjectID get_current_camera_3d() const { return current_camera_3d; }
	void register_canvas_layer(CanvasLayer *p_layer);
	void unregister_canvas_layer(CanvasLayer *p_layer);
	void add_physics_listener(PhysicsSyncListener *p_listener);
	void remove_physics_listener(PhysicsSyncListener *p_listener);
	void mark_canvas_order_dirty() { canvas_order_dirty = true; }

	SceneTree(bool p_edit_mode = false);
	~SceneTree();
};
