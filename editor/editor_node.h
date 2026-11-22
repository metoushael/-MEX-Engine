// EditorNode : l'éditeur visuel (arbre de scène, inspecteur, vue 2D, fichiers, script, sortie).
// L'interface est construite avec Dear ImGui ; les scènes éditées vivent dans un SceneTree
// en « mode édition » où les scripts ne s'exécutent pas (instances fantômes).
#pragma once

#include "core/object/object.h"
#include "editor/editor_export.h"
#include "main/game_runner.h"
#include "scene/resources/texture.h"
#include "servers/display_server.h"
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
struct ImDrawList;
struct ImVec2;
union SDL_Event;

class EditorNode {
public:
	// p_has_project : un projet est déjà chargé dans ProjectSettings.
	int run(bool p_has_project, const String &p_scene, int p_quit_after, const String &p_screenshot);

	EditorNode();
	~EditorNode();

	// Préférence « barre de titre intégrée », lue avant la création de la fenêtre (qui naît
	// ainsi directement avec ou sans la barre du système).
	static bool read_title_bar_preference();
	// Pilote de rendu choisi dans les préférences (« auto » par défaut), lu avant la création
	// de la fenêtre.
	static String read_render_driver_preference();
	// Réglage d'origine : barre intégrée sous Windows et Linux, barre du système sous macOS.
	static bool get_default_title_bar_preference();
	static const char *get_title_bar_preference_key();

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

	// --- Apparence : thèmes, échelle, préférences (editor_theme.cpp) --------------
	// Couleurs du thème courant, à utiliser à la place de couleurs écrites en dur : elles
	// restent lisibles sur fond clair comme sur fond sombre.
	struct Palette {
		bool dark = true;
		Color bg, bg_dark, bg_light, frame, border;
		Color text, text_dim;
		Color accent, on_accent; // on_accent : texte posé sur la couleur d'accent
		Color success, warning, error, info;
		Color node_2d, node_3d, node_gui, node_layer, node_other;
		Color folder, file_scene, file_script, file_image, file_audio, file_mesh, file_other;
	} palette;
	std::string pref_theme = "MEX Nuit";
	Color pref_custom_base = Color(0.13f, 0.15f, 0.20f, 1.0f);
	Color pref_accent = Color(1.0f, 0.478f, 0.0f, 1.0f);
	bool pref_custom_accent = false;
	float pref_ui_scale = 0.0f; // 0 : automatique (échelle d'affichage du système)
	float pref_font_size = 16.0f;
	float pref_rounding = 5.0f;
	bool pref_thumbnails = true;
	float pref_thumbnail_size = 72.0f;
	bool pref_files_grid = false;
	// Barre de titre intégrée à l'éditeur (boutons Réduire, Agrandir, Fermer dans la barre du
	// haut) ; sinon, barre de titre du système, en version sombre si le thème est sombre.
	bool pref_custom_title_bar = true;
	// Pilote de rendu (appliqué au démarrage suivant) et configuration d'affichage que
	// l'utilisateur a déjà validée (« pilote|barre »).
	String pref_render_driver = "auto";
	String pref_display_checked;
	bool display_check_forced = false; // MEX_EDITOR_DISPLAY_CHECK : tests
	bool display_restart_notice = false;
	bool display_check_dismissed = false; // réponse donnée : plus de question pendant cette session
	String _get_display_signature() const;
	void _draw_display_check(); // « L'affichage est-il correct ? », une fois par configuration
	void _enable_compatibility_mode();
	void _write_diagnostics() const; // diagnostic.txt dans le dossier de préférences
	bool prefs_open = false;
	bool style_dirty = false; // le style est réappliqué au début de l'image suivante
	String prefs_path; // editor_settings.cfg
	String prefs_dir; // dossier de préférences de l'utilisateur
	float _get_auto_ui_scale() const;

	// --- Fenêtre : barre de titre intégrée, adaptation à la taille (editor_window.cpp) ---
	// Plus petite taille utile de l'interface, en unités à 100 % : en dessous, les barres
	// ne tiennent plus. La taille minimale de la fenêtre en découle.
	static constexpr float MIN_LAYOUT_WIDTH = 960.0f;
	static constexpr float MIN_LAYOUT_HEIGHT = 600.0f;
	// Taille en dessous de laquelle l'échelle automatique réduit l'interface.
	static constexpr float COMFORT_LAYOUT_WIDTH = 1280.0f;
	static constexpr float COMFORT_LAYOUT_HEIGHT = 680.0f;
	static constexpr float MIN_WINDOW_FIT = 0.75f;
	float window_fit = 1.0f; // réduction de l'échelle automatique dans une petite fenêtre (0,75 à 1)
	struct CaptionArea {
		float x0, y0, x1, y1;
	};
	std::vector<CaptionArea> caption_areas; // zones de la barre du haut qui déplacent la fenêtre
	float last_viewport_w = 0.0f, last_viewport_h = 0.0f;
	bool _has_custom_title_bar() const;
	// Trois boutons de fenêtre, collés au bord droit ; renvoie la largeur occupée (0 si la
	// barre du système est utilisée). p_top et p_height : dans la fenêtre ImGui courante.
	float _draw_window_buttons(float p_top, float p_height);
	float _get_window_buttons_width() const;
	void _add_caption_area(float p_x0, float p_y0, float p_x1, float p_y1);
	void _update_window_fit(); // début d'image : échelle automatique selon la taille de la fenêtre
	void _update_window_chrome(); // fin d'image : zones de déplacement, bords, curseurs
	void _fit_floating_windows(); // les fenêtres flottantes restent dans la fenêtre principale
	void _rescale_dock_layout(unsigned int p_dockspace_id); // les panneaux suivent la taille de la fenêtre
	void _apply_window_settings(); // taille minimale et couleurs de la barre du système
	void _load_prefs();
	void _save_prefs();
	void _draw_preferences();
	// Bouton mis en avant (couleur d'accent) ; même comportement qu'ImGui::Button.
	// Bouton à bascule de barre d'outils : fond teinté de la couleur d'accent quand il est actif.
	bool _toggle_button(const char *p_label, bool p_active, const char *p_tip = nullptr);
	bool _accent_button(const char *p_label, float p_width = 0.0f);
	// Icône et couleur d'une classe de nœud ou d'un fichier.
	static const char *_class_icon(const String &p_class);
	Color _class_color(const String &p_class) const;
	static const char *_file_icon(const String &p_path);
	Color _file_color(const String &p_path) const;
	void _shutdown_imgui();
	void _process_event(const SDL_Event &p_event);
	void _draw_ui();
	void _draw_main_menu();
	void _draw_menus();
	void _draw_toolbar(float p_logo_end);
	float _get_play_buttons_width(float p_button_h) const;
	// Espaces de travail : les grands onglets du centre, choisis dans la barre du haut.
	enum {
		WORKSPACE_2D,
		WORKSPACE_3D,
		WORKSPACE_SCRIPT,
		WORKSPACE_GAME,
		WORKSPACE_COUNT,
	};
	int workspace_active = WORKSPACE_2D;
	void _switch_workspace(int p_workspace);
	static std::vector<String> _get_theme_names();
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
	// (editor_project_manager.cpp) La liste des projets connus est mémorisée dans
	// projects.cfg, dans le dossier de préférences de l'utilisateur.
	struct ProjectEntry {
		String path; // dossier du projet, chemin absolu
		String name;
		String description;
		String main_scene;
		int width = 0, height = 0;
		bool favorite = false;
		bool missing = false; // le dossier ou project.mex n'existe plus
		int64_t last_opened = 0; // secondes depuis 1970 ; 0 : jamais ouvert ici
		Ref<Texture2D> icon;
		bool icon_loaded = false;
	};
	enum {
		PM_DIALOG_NONE,
		PM_DIALOG_CREATE,
		PM_DIALOG_IMPORT,
		PM_DIALOG_RENAME,
	};
	// Sélecteur de dossier (le moteur n'utilise pas les boîtes de dialogue du système).
	struct DirBrowser {
		bool open_request = false;
		String title;
		String current;
		std::string typed;
		std::vector<String> dirs;
		bool dirty = true;
		bool is_project = false;
		std::function<void(const String &)> callback;
	} dir_browser;
	std::vector<ProjectEntry> pm_projects;
	std::vector<String> pm_removed; // projets retirés de la liste : ne pas les rajouter au scan
	String pm_selected; // chemin du projet sélectionné
	std::string pm_filter;
	int pm_sort = 0; // 0 : dernière ouverture, 1 : nom, 2 : chemin
	int pm_dialog = PM_DIALOG_NONE;
	bool pm_dialog_open_request = false;
	int pm_template = 0; // 0 : vide, 1 : jeu 2D, 2 : jeu 3D
	std::string pm_open_path;
	std::string pm_new_name = "MonJeu";
	std::string pm_new_parent;
	std::string pm_error;
	bool pm_scanned = false;
	bool pm_play_after_open = false;
	void _draw_project_manager();
	void _draw_project_row(ProjectEntry &p_project, float p_width);
	void _draw_project_dialogs();
	void _draw_dir_browser();
	void _browse_dir(const String &p_title, const String &p_start, std::function<void(const String &)> p_callback);
	void _pm_load_list();
	void _pm_save_list();
	ProjectEntry *_pm_add(const String &p_path, bool p_force = true);
	void _pm_refresh(ProjectEntry &p_project);
	int _pm_scan(const String &p_dir, int p_depth);
	ProjectEntry *_pm_find(const String &p_path);
	bool _create_project(const String &p_dir, const String &p_name, int p_template);
	void _close_project();
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
	Ref<Texture2D> logo; // icône de l'application (projets sans icône dans le gestionnaire de projets)
	// Logo de MEX (editor_brand.cpp) : symbole et mot rendus à la taille d'affichage.
	struct BrandImage {
		int shape = 0;
		int width = 0;
		int height = 0;
		Ref<Texture2D> texture;
	};
	std::vector<BrandImage> brand_images;
	void _collect_brand_images();
	void _free_brand_images();
	void _draw_brand_shape(ImDrawList *p_draw_list, int p_shape, const ImVec2 &p_pos, float p_width, float p_height, unsigned int p_rgb);
	float _draw_brand_symbol(ImDrawList *p_draw_list, const ImVec2 &p_pos, float p_height); // renvoie la largeur
	float _draw_brand_lockup(ImDrawList *p_draw_list, const ImVec2 &p_pos, float p_height); // renvoie la largeur
	float _draw_brand_header(float p_height, bool p_stacked = false); // renvoie le bord droit du bloc, en coordonnées d'écran
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
	String fs_selected; // fichier sélectionné : son aperçu est affiché en bas du dock
	String fs_current_dir = "res://"; // dossier affiché en mode grille
	std::string fs_filter;
	const FsEntry *_find_fs_entry(const FsEntry &p_entry, const String &p_path) const;
	void _draw_fs_grid();
	void _draw_fs_preview_panel();
	void _fs_file_interactions(const String &p_path); // double-clic, glisser-déposer, menu contextuel
	void _fs_dir_menu(const String &p_path);

	// --- Moniteur (editor_monitor.cpp) : flux de mesures en temps réel ----------------------
	enum {
		MONITOR_FPS,
		MONITOR_FRAME_TIME,
		MONITOR_GAME_PROCESS,
		MONITOR_GAME_DRAW,
		MONITOR_ITEMS_2D,
		MONITOR_DRAW_CALLS_3D,
		MONITOR_TRIANGLES,
		MONITOR_NODES,
		MONITOR_OBJECTS,
		MONITOR_COUNT,
		MONITOR_SAMPLES = 240,
	};
	struct MonitorSeries {
		float values[MONITOR_SAMPLES] = {};
		int head = 0;
		int count = 0;
	};
	MonitorSeries monitors[MONITOR_COUNT];
	bool monitor_paused = false;
	int monitor_items_2d = 0, monitor_draw_calls_3d = 0, monitor_triangles = 0;
	void _monitor_push(int p_series, float p_value);
	void _monitor_sample(double p_frame_usec, double p_game_process_usec, double p_game_draw_usec);
	void _draw_monitor_dock();

	// --- Aperçus (editor_preview.cpp) ---------------------------------------------------
	// Miniatures des fichiers (images, maillages, scènes, sons, textes) et aperçus 3D
	// interactifs de l'inspecteur (maillages, matériaux).
	enum {
		PREVIEW_NONE,
		PREVIEW_IMAGE,
		PREVIEW_MESH,
		PREVIEW_SCENE,
		PREVIEW_AUDIO,
		PREVIEW_TEXT,
	};
	struct FilePreview {
		int kind = PREVIEW_NONE;
		bool ready = false; // généré (avec succès ou non)
		bool queued = false;
		uint64_t mtime = 0;
		Ref<Texture2D> texture; // image
		RID viewport; // rendu 3D (maillage, scène 3D)
		RID target; // rendu 2D (scène 2D)
		Size2 size; // taille de l'image d'aperçu
		String info; // « 128 x 128 px », « 1 204 triangles »...
		std::vector<float> peaks; // son : amplitude par colonne
		std::vector<String> lines; // texte : premières lignes
	};
	struct LivePreview {
		RID viewport;
		Ref<Resource> mesh;
		Ref<Resource> material;
		float yaw = 0.6f, pitch = -0.35f;
		int size = 0;
		uint64_t last_used = 0;
	};
	std::map<String, FilePreview> previews;
	std::vector<String> preview_queue;
	std::map<ObjectID, LivePreview> live_previews;
	uint64_t preview_frame = 0;
	// Décor commun à tous les rendus d'aperçu 3D : un objet, deux lumières.
	RID preview_scenario, preview_instance, preview_key_light, preview_fill_light;
	Ref<Resource> preview_sphere;
	static int _get_preview_kind(const String &p_path);
	FilePreview *_get_preview(const String &p_path);
	bool _has_preview_image(const FilePreview *p_preview) const;
	// Dessine l'aperçu centré dans le rectangle ; renvoie faux s'il n'y a rien à montrer.
	bool _draw_preview(const String &p_path, const Vector2 &p_min, const Vector2 &p_max);
	void _draw_preview_tooltip(const String &p_path);
	// Aperçu 3D interactif d'une ressource (maillage ou matériau) : à glisser pour tourner.
	void _draw_resource_preview(const Ref<Resource> &p_resource);
	void _render_previews();
	void _generate_preview(const String &p_path, FilePreview &r_preview);
	void _render_scene_preview(const String &p_path, FilePreview &r_preview, int p_size);
	bool _render_preview_3d(RID p_viewport, RID p_mesh, const AABB &p_aabb, RID p_material, float p_yaw, float p_pitch);
	void _free_preview(FilePreview &r_preview);
	void _clear_previews();
	static bool _get_world_aabb(Node *p_node, AABB &r_aabb);
	static bool _get_canvas_bounds(Node *p_node, Rect2 &r_rect);
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
	Size2 edit_target_size; // en unités d'interface
	float edit_target_ratio = 1.0f; // pixels de la cible par unité d'interface
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
	Size2 game_size; // résolution logique du jeu
	Size2 game_target_size; // taille réelle de la cible, en pixels
	float pixel_ratio = 1.0f; // pixels réels par unité d'interface (2 sur un écran Retina)
	float _update_game_target(); // renvoie l'échelle de rendu
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
	std::vector<ExportPreset> export_presets; // export_presets.cfg du projet
	int export_selected = -1;
	bool export_loaded = false;
	String export_message;
	bool export_failed = false;
	void _draw_export_dialog();
	// Écrit dans p_dir l'exécutable et l'archive .pck du projet.

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
