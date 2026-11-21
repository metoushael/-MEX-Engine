// EditorNode : l'éditeur visuel (arbre de scène, inspecteur, vue 2D, fichiers, script, sortie).
// L'interface est construite avec Dear ImGui ; les scènes éditées vivent dans un SceneTree
// en « mode édition » où les scripts ne s'exécutent pas (instances fantômes).
#pragma once

#include "core/object/object.h"
#include "main/game_runner.h"
#include "scene/resources/texture.h"
#include "servers/rendering_server.h"
#include "servers/rendering_server_3d.h"

#include <functional>

class Node;
class CanvasItem;
class SceneTree;
class TileMap;
class AnimatedSprite2D;
class AnimationPlayer;
struct ImFont;
union SDL_Event;

class EditorNode {
public:
	// p_has_project : un projet est déjà chargé dans ProjectSettings.
	int run(bool p_has_project, const String &p_scene, int p_quit_after, const String &p_screenshot);

	EditorNode();
	~EditorNode();

private:
	// --- Général -------------------------------------------------------------
	bool quit_requested = false;
	bool project_loaded = false;
	bool layout_built = false;
	bool window_focused = true;
	double frame_delta = 1.0 / 60.0;
	String ini_path;
	ImFont *font_ui = nullptr;
	ImFont *font_mono = nullptr;
	int print_handler_id = -1;
	std::vector<std::function<void()>> deferred;

	void _setup_imgui();
	void _apply_style();
	void _shutdown_imgui();
	void _process_event(const SDL_Event &p_event);
	void _draw_ui();
	void _draw_main_menu();
	void _draw_toolbar();
	void _build_default_layout(unsigned int p_dockspace_id);
	void _handle_shortcuts();
	void _request_quit();
	void _update_window_title();
	// Scénario d'entrées simulées (tests automatiques, variable MEX_EDITOR_TEST_SCRIPT).
	struct TestCommand {
		int frame = 0;
		String name;
		std::vector<String> args;
	};
	std::vector<TestCommand> test_commands;
	void _load_test_script();
	void _run_test_commands(int p_frame, bool p_after_render);
	void _defer(std::function<void()> p_action) { deferred.push_back(std::move(p_action)); }

	// --- Gestionnaire de projets ------------------------------------------------
	std::string pm_open_path;
	std::string pm_new_name = "MonJeu";
	std::string pm_new_parent;
	std::string pm_error;
	std::vector<String> pm_found;
	bool pm_scanned = false;
	void _draw_project_manager();
	bool _open_project(const String &p_path);
	void _project_opened();
	void _apply_project_settings();

	// --- Scène éditée -----------------------------------------------------------
	SceneTree *edit_tree = nullptr;
	Node *scene_root = nullptr;
	String scene_path;
	bool scene_modified = false;
	ObjectID selected_id = 0;

	Node *_get_selected() const;
	void _select(Node *p_node);
	void _mark_modified();
	void _new_scene();
	bool _open_scene(const String &p_path);
	bool _save_scene(const String &p_path);
	void _save_scene_or_ask();
	void _set_scene_root(Node *p_root, const String &p_path);
	void _check_unsaved_then(std::function<void()> p_next);
	Node *_create_node(const String &p_type, Node *p_parent);
	Node *_instance_scene(const String &p_path, Node *p_parent);
	void _delete_node(Node *p_node);
	void _duplicate_node(Node *p_node);
	bool _is_editable(const Node *p_node) const; // appartient à la scène (pas l'intérieur d'une instance)
	void _attach_script(Node *p_node, const String &p_path);
	String _make_script_template(const String &p_base) const;

	// --- Annuler / rétablir (instantanés sérialisés de la scène) ---------------
	std::vector<String> undo_stack;
	std::vector<String> redo_stack;
	String undo_base;
	bool action_dirty = false;
	String _serialize_scene() const;
	void _restore_scene(const String &p_state);
	void _commit_undo_if_idle();
	void _reset_undo();
	void _undo();
	void _redo();

	// --- Boîtes de dialogue -----------------------------------------------------
	struct FileDialog {
		bool open_request = false;
		bool save_mode = false;
		String title;
		std::vector<String> extensions;
		std::string filter;
		std::string filename;
		std::function<void(const String &)> callback;
	} file_dialog;
	struct ConfirmDialog {
		bool open_request = false;
		String title;
		String message;
		std::vector<std::pair<String, std::function<void()>>> buttons;
	} confirm_dialog;
	struct CreateNodeDialog {
		bool open_request = false;
		ObjectID parent_id = 0;
		bool as_root = false;
		std::string filter;
		String selected;
	} create_dialog;
	bool about_open = false;
	Ref<Texture2D> logo; // icône de MEX (fenêtre « À propos », gestionnaire de projets)
	void _draw_brand_header(float p_logo_size);
	bool settings_open = false;

	void _open_file_dialog(const String &p_title, const std::vector<String> &p_extensions, bool p_save, std::function<void(const String &)> p_callback, const String &p_default = "");
	void _confirm(const String &p_title, const String &p_message, std::vector<std::pair<String, std::function<void()>>> p_buttons);
	void _draw_dialogs();
	void _draw_create_node_dialog();
	void _draw_create_class_tree(const String &p_class, const String &p_filter);
	void _draw_project_settings();
	void _draw_about();

	// --- Dock « Scène » -----------------------------------------------------------
	ObjectID rename_id = 0;
	std::string rename_buffer;
	bool rename_focus = false;
	void _draw_scene_dock();
	void _draw_scene_node(Node *p_node);
	void _node_context_menu(Node *p_node);

	// --- Dock « Fichiers » --------------------------------------------------------
	struct FsEntry {
		String name;
		String path; // res://...
		bool is_dir = false;
		std::vector<FsEntry> children;
	};
	FsEntry fs_root;
	std::vector<String> fs_files; // tous les fichiers (res://...)
	bool fs_dirty = true;
	std::string fs_new_name;
	String fs_new_dir;
	int fs_new_kind = 0; // 1 = dossier, 2 = script
	void _scan_filesystem();
	void _scan_dir(FsEntry &r_entry);
	void _draw_filesystem_dock();
	void _draw_fs_entry(const FsEntry &p_entry);
	void _open_file(const String &p_path);

	// --- Dock « Sortie » ------------------------------------------------------------
	struct ConsoleLine {
		PrintLevel level;
		String text;
		int count = 1;
	};
	std::vector<ConsoleLine> console_lines;
	bool console_scroll = false;
	int focus_console_frames = 0; // affiche l'onglet « Sortie » une fois la disposition en place
	void _draw_console_dock();

	// --- Inspecteur -----------------------------------------------------------------
	struct ConnectDialog {
		bool open_request = false;
		ObjectID source_id = 0;
		String signal;
		String arguments; // noms des arguments du signal, séparés par des virgules
		String target_path = ".";
		std::string method;
	} connect_dialog;
	std::string group_buffer;
	std::map<String, std::string> text_edit_buffers; // édition textuelle de valeurs (Array...)
	void _draw_inspector_dock();
	void _draw_object_properties(Object *p_object, bool p_is_node);
	bool _edit_property(Object *p_object, const PropertyInfo &p_info, Variant &r_value);
	bool _edit_resource(Object *p_object, const PropertyInfo &p_info, Variant &r_value);
	void _draw_node_signals(Node *p_node);
	void _draw_node_groups(Node *p_node);
	void _draw_connect_dialog();

	// --- Vue 2D -----------------------------------------------------------------------
	enum Tool {
		TOOL_SELECT,
		TOOL_ROTATE,
		TOOL_SCALE,
	};
	enum DragMode {
		DRAG_NONE,
		DRAG_PAN,
		DRAG_MOVE,
		DRAG_ROTATE,
		DRAG_SCALE,
		DRAG_PAINT,
		DRAG_ERASE,
	};
	RID edit_target;
	Size2 edit_target_size;
	Vector2 view_offset = Vector2(-120, -80); // position monde du coin haut-gauche
	real_t view_zoom = 1.0f;
	Tool tool = TOOL_SELECT;
	DragMode drag = DRAG_NONE;
	Vector2 drag_start_world;
	Vector2 drag_start_value;
	real_t drag_start_rotation = 0;
	bool show_grid = true;
	bool snap_enabled = false;
	float snap_step = 16.0f;
	bool show_collision_shapes = true;
	bool tile_paint = false;
	int tile_selected = 0;
	Vector2 tile_last_cell;
	bool tile_has_last_cell = false;
	bool focus_viewport = false;
	bool view_autofit = true; // recadre pendant que la disposition des docks se stabilise
	int view_autofit_frames = 6;
	bool viewport_visible = false; // le dock est affiché : la vue doit être rendue
	Vector2 viewport_mouse_world;

	void _draw_viewport_dock();
	void _render_edit_view();
	void _viewport_input(const Vector2 &p_origin, const Size2 &p_size, bool p_hovered);
	void _viewport_overlay(const Vector2 &p_origin, const Size2 &p_size);
	void _draw_tile_palette(TileMap *p_map);
	Node *_pick_node(const Vector2 &p_world) const;
	void _pick_recursive(Node *p_node, const Vector2 &p_world, bool p_with_rect, Node *&r_found) const;
	void _focus_selection();
	Transform2D _get_view_transform() const;
	Vector2 _snap(const Vector2 &p_value) const;

	// --- Vue 3D -----------------------------------------------------------------------
	enum Drag3D {
		DRAG3D_NONE,
		DRAG3D_ORBIT,
		DRAG3D_PAN,
		DRAG3D_GIZMO,
	};
	bool view_3d = false; // le dock « Vue » affiche la scène en 3D
	ObjectID view_mode_reference = 0; // nœud ayant déterminé le mode 2D/3D
	RID view3d_viewport;
	Size2 view3d_size;
	Vector3 cam3d_target = Vector3(0, 0.5f, 0);
	real_t cam3d_distance = 12.0f;
	real_t cam3d_yaw = 0.65f;
	real_t cam3d_pitch = -0.45f;
	bool cam3d_orthogonal = false;
	bool gizmo_local = false;
	float snap_step_3d = 0.5f;
	Drag3D drag3d = DRAG3D_NONE;
	int gizmo_axis = -1; // 0..2 : axe X/Y/Z, 3 : libre (plan de la caméra) ou uniforme
	int gizmo_hover = -1;
	Transform3D drag3d_start_global;
	Vector3 drag3d_start_scale;
	Vector3 drag3d_start_point;
	Vector3 drag3d_axis;
	real_t drag3d_start_param = 0;
	Vector2 drag3d_start_mouse;
	std::vector<RenderingServer3D::Line> view3d_lines;
	std::vector<RenderingServer3D::Line> view3d_overlay_lines;

	void _update_view_mode();
	void _draw_viewport_3d();
	void _render_edit_view_3d();
	void _viewport_3d_input(const Vector2 &p_origin, const Size2 &p_size, bool p_hovered);
	void _viewport_3d_overlay(const Vector2 &p_origin, const Size2 &p_size);
	void _collect_gizmo_lines_3d(Node *p_node);
	RenderingServer3D::CameraData _get_camera_3d() const;
	void _get_mouse_ray_3d(const Vector2 &p_mouse, const Size2 &p_size, Vector3 &r_origin, Vector3 &r_direction) const;
	bool _project_3d(const Vector3 &p_world, const Size2 &p_size, Vector2 &r_screen) const;
	Node *_pick_node_3d(const Vector3 &p_origin, const Vector3 &p_direction) const;
	void _pick_recursive_3d(Node *p_node, const Vector3 &p_origin, const Vector3 &p_direction, real_t &r_best, Node *&r_found) const;
	void _focus_selection_3d();

	// --- Jeu (mode « Jouer ») -----------------------------------------------------------
	GameRunner game;
	RID game_target;
	Size2 game_size;
	bool game_paused = false;
	bool focus_game = false;
	bool game_view_focused = false;
	bool game_view_hovered = false;
	Vector2 game_image_pos;
	Size2 game_image_size;
	void _play(const String &p_scene);
	void _play_main();
	void _play_current();
	void _stop();
	void _draw_game_dock();
	void _forward_game_event(const SDL_Event &p_event);

	// --- Dock « Animation » ------------------------------------------------------------------
	struct AnimBackup {
		ObjectID id = 0;
		String property;
		Variant value;
	};
	ObjectID anim_target_id = 0; // AnimationPlayer ou AnimatedSprite2D affiché
	String anim_target_path;
	String anim_current;
	double anim_time = 0;
	bool anim_playing = false;
	bool anim_drag_key = false;
	bool focus_animation = false;
	std::string anim_new_name;
	String anim_track_node = ".";
	String anim_track_property;
	String anim_selected_track;
	int anim_selected_key = -1;
	ObjectID anim_last_selected = 0;
	// Valeurs d'origine des propriétés modifiées par l'aperçu.
	mutable std::vector<AnimBackup> anim_backup;
	void _draw_animation_dock();
	void _draw_animated_sprite_editor(AnimatedSprite2D *p_sprite);
	void _draw_animation_player_editor(AnimationPlayer *p_player);
	void _anim_backup_property(Object *p_object, const String &p_property);
	void _anim_swap_backup() const;
	void _anim_preview_apply(AnimationPlayer *p_player);
	void _anim_end_preview();

	// --- Export du jeu ------------------------------------------------------------------------
	bool export_open = false;
	std::string export_dir;
	std::string export_template;
	String export_message;
	bool export_failed = false;
	void _draw_export_dialog();
	// Écrit dans p_dir l'exécutable et l'archive .pck du projet.
	bool _export_project(const String &p_dir, const String &p_template, String &r_message);

	// --- Éditeur de script ----------------------------------------------------------------
	String script_path;
	std::string script_text;
	bool script_dirty = false;
	bool focus_script = false;
	String script_error;
	int script_error_line = 0;
	int script_cursor_line = 1;
	int script_cursor_column = 1;
	int script_last_length = 0;
	bool script_goto_error = false;
	uint64_t script_mtime = 0;
	void _open_script(const String &p_path);
	void _save_script();
	void _draw_script_dock();
	void _reload_script_resource(const String &p_path, const String &p_source);
	void _check_external_changes();

	// --- Paramètres du projet -------------------------------------------------------------
	String capture_action; // action en attente d'une touche
	std::string new_action_name;
	void _save_input_map();
};
