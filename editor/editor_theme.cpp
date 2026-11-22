// Apparence de l'éditeur : thèmes, échelle de l'interface, préférences de l'utilisateur.
#include "editor_node.h"

#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/string/ustring.h"
#include "editor_ui.h"
#include "servers/display_server.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <SDL.h>

#include <algorithm>

float EditorUI::scale = 1.0f;

namespace {

struct ThemeDef {
	const char *name;
	const char *description;
	Color base; // fond des panneaux ; tout le reste en est dérivé
	Color accent;
	bool high_contrast;
};

Color _rgb(unsigned int p_hex) {
	return Color(((p_hex >> 16) & 0xFF) / 255.0f, ((p_hex >> 8) & 0xFF) / 255.0f, (p_hex & 0xFF) / 255.0f, 1.0f);
}

const char *THEME_CUSTOM = "Personnalisé";

const std::vector<ThemeDef> &_themes() {
	static const std::vector<ThemeDef> themes = {
		// Le premier thème est celui d'origine.
		{ "MEX Nuit", "Indigo et orange", _rgb(0x1A1C30), _rgb(0xFF7A00), false },
		// Couleurs du logo (branding/GUIDE.md) : accent Vert Mex ; le fond est choisi pour que
		// sa teinte la plus sombre, celle des barres, soit le Noir Mex #0C110E.
		{ "MEX Vert", "Noir et vert du logo", _rgb(0x111814), _rgb(0x19D46A), false },
		{ "Ardoise bleue", "Gris-bleu doux, façon Godot", _rgb(0x363D4A), _rgb(0x70BAFA), false },
		{ "Graphite", "Gris neutre, couleurs fidèles", _rgb(0x262626), _rgb(0xFF8A1F), false },
		{ "Minuit", "Noir profond et bleu vif", _rgb(0x101014), _rgb(0x4CC2FF), false },
		{ "Aurore", "Violet et rose", _rgb(0x2A2238), _rgb(0xFF6AC1), false },
		{ "Forêt", "Vert sombre", _rgb(0x1D2925), _rgb(0x5FD38D), false },
		{ "Clair", "Fond clair, pièces lumineuses", _rgb(0xE9EBF1), _rgb(0xE85400), false },
		{ "Contraste élevé", "Noir, blanc, bordures marquées", _rgb(0x000000), _rgb(0xFFD21F), true },
	};
	return themes;
}

float _luminance(const Color &p_color) {
	return 0.2126f * p_color.r + 0.7152f * p_color.g + 0.0722f * p_color.b;
}

Color _mix(const Color &p_a, const Color &p_b, float p_t) {
	Color c = p_a.lerp(p_b, p_t);
	c.a = 1.0f;
	return c;
}

} // namespace

// ---------------------------------------------------------------------------
// Préférences
// ---------------------------------------------------------------------------
std::vector<String> EditorNode::_get_theme_names() {
	std::vector<String> names;
	for (const ThemeDef &theme : _themes()) {
		names.push_back(theme.name);
	}
	names.push_back(THEME_CUSTOM);
	return names;
}

// Échelle « Automatique » : celle de l'écran (densité), réduite quand la fenêtre est trop
// petite pour l'interface à sa taille normale (voir _update_window_fit).
float EditorNode::_get_auto_ui_scale() const {
	DisplayServer *display = DisplayServer::get_singleton();
	return (display ? display->get_screen_scale() : 1.0f) * window_fit;
}

void EditorNode::_load_prefs() {
	if (char *pref = SDL_GetPrefPath("Metoushela", "MEX")) {
		prefs_dir = String(pref);
		SDL_free(pref);
		prefs_path = prefs_dir + "editor_settings.cfg";
	}
	if (!prefs_path.empty() && FileAccess::exists(prefs_path)) {
		ConfigFile config;
		if (config.load(prefs_path) == OK) {
			pref_theme = config.get_value("apparence", "theme", Variant(String(pref_theme))).as_string();
			pref_custom_base = config.get_value("apparence", "couleur_de_fond", Variant(pref_custom_base)).as_color();
			pref_custom_accent = config.get_value("apparence", "accent_personnalise", Variant(pref_custom_accent)).as_bool();
			pref_accent = config.get_value("apparence", "accent", Variant(pref_accent)).as_color();
			pref_rounding = (float)config.get_value("apparence", "arrondi", Variant((double)pref_rounding)).as_float();
			pref_ui_scale = (float)config.get_value("affichage", "echelle", Variant((double)pref_ui_scale)).as_float();
			pref_font_size = (float)config.get_value("affichage", "taille_du_texte", Variant((double)pref_font_size)).as_float();
			pref_thumbnails = config.get_value("fichiers", "miniatures", Variant(pref_thumbnails)).as_bool();
			pref_thumbnail_size = (float)config.get_value("fichiers", "taille_des_miniatures", Variant((double)pref_thumbnail_size)).as_float();
			pref_files_grid = config.get_value("fichiers", "grille", Variant(pref_files_grid)).as_bool();
		}
	}
	// Lue par la même fonction qu'avant la création de la fenêtre : les deux restent d'accord.
	pref_custom_title_bar = read_title_bar_preference();
	// Variables d'environnement : captures d'écran et tests, sans toucher aux préférences.
	if (const char *forced = SDL_getenv("MEX_EDITOR_THEME")) {
		pref_theme = forced;
	}
	if (const char *forced = SDL_getenv("MEX_EDITOR_SCALE")) {
		pref_ui_scale = (float)SDL_atof(forced);
	}
	pref_ui_scale = pref_ui_scale <= 0.0f ? 0.0f : std::clamp(pref_ui_scale, 0.5f, 4.0f);
	pref_font_size = std::clamp(pref_font_size, 11.0f, 26.0f);
	pref_rounding = std::clamp(pref_rounding, 0.0f, 12.0f);
	pref_thumbnail_size = std::clamp(pref_thumbnail_size, 40.0f, 160.0f);
}

void EditorNode::_save_prefs() {
	if (prefs_path.empty()) {
		return;
	}
	ConfigFile config;
	config.set_value("apparence", "theme", Variant(String(pref_theme)));
	config.set_value("apparence", "couleur_de_fond", Variant(pref_custom_base));
	config.set_value("apparence", "accent_personnalise", Variant(pref_custom_accent));
	config.set_value("apparence", "accent", Variant(pref_accent));
	config.set_value("apparence", "arrondi", Variant((double)pref_rounding));
	config.set_value("affichage", "echelle", Variant((double)pref_ui_scale));
	config.set_value("affichage", "taille_du_texte", Variant((double)pref_font_size));
	config.set_value("fichiers", "miniatures", Variant(pref_thumbnails));
	config.set_value("fichiers", "taille_des_miniatures", Variant((double)pref_thumbnail_size));
	config.set_value("fichiers", "grille", Variant(pref_files_grid));
	config.set_value("fenetre", "barre_de_titre_integree", Variant(pref_custom_title_bar));
	if (config.save(prefs_path) != OK) {
		print_warning("Impossible d'enregistrer les préférences de l'éditeur : " + prefs_path);
	}
}

// ---------------------------------------------------------------------------
// Style
// ---------------------------------------------------------------------------
void EditorNode::_apply_style() {
	style_dirty = false;

	// --- Thème choisi ---
	Color base = _themes()[0].base;
	Color accent = _themes()[0].accent;
	bool high_contrast = false;
	if (pref_theme == THEME_CUSTOM) {
		base = pref_custom_base;
		base.a = 1.0f;
	} else {
		bool found = false;
		for (const ThemeDef &theme : _themes()) {
			if (pref_theme == theme.name) {
				base = theme.base;
				accent = theme.accent;
				high_contrast = theme.high_contrast;
				found = true;
			}
		}
		if (!found) {
			pref_theme = _themes()[0].name;
		}
	}
	if (pref_custom_accent || pref_theme == THEME_CUSTOM) {
		accent = pref_accent;
		accent.a = 1.0f;
	}

	// --- Palette dérivée du fond et de l'accent ---
	const Color black(0, 0, 0, 1), white(1, 1, 1, 1);
	Palette &p = palette;
	p.dark = _luminance(base) < 0.5f;
	const Color toward = p.dark ? white : black; // « vers le premier plan »
	p.bg = base;
	p.bg_dark = p.dark ? _mix(base, black, 0.30f) : _mix(base, black, 0.08f);
	p.bg_light = _mix(base, toward, p.dark ? 0.11f : 0.10f);
	p.frame = p.dark ? _mix(base, black, 0.45f) : _mix(base, white, 0.80f);
	p.border = p.dark ? _mix(base, black, 0.55f) : _mix(base, black, 0.24f);
	p.text = p.dark ? Color(0.90f, 0.91f, 0.95f, 1.0f) : Color(0.10f, 0.11f, 0.17f, 1.0f);
	p.text_dim = _mix(p.text, base, 0.42f);
	p.accent = accent;
	p.on_accent = _luminance(accent) > 0.55f ? Color(0.06f, 0.06f, 0.10f, 1.0f) : white;
	if (high_contrast) {
		p.bg_dark = black;
		p.bg_light = Color(0.16f, 0.16f, 0.16f, 1.0f);
		p.frame = Color(0.07f, 0.07f, 0.07f, 1.0f);
		p.border = Color(0.75f, 0.75f, 0.75f, 1.0f);
		p.text = white;
		p.text_dim = Color(0.78f, 0.78f, 0.78f, 1.0f);
	}
	if (p.dark) {
		p.success = Color(0.52f, 0.88f, 0.56f, 1.0f);
		p.warning = Color(1.0f, 0.83f, 0.40f, 1.0f);
		p.error = Color(1.0f, 0.47f, 0.45f, 1.0f);
		p.info = Color(0.55f, 0.78f, 1.0f, 1.0f);
		p.node_2d = Color(0.55f, 0.72f, 0.98f, 1.0f);
		p.node_3d = Color(0.98f, 0.56f, 0.52f, 1.0f);
		p.node_gui = Color(0.56f, 0.86f, 0.56f, 1.0f);
		p.node_layer = Color(0.90f, 0.78f, 0.48f, 1.0f);
		p.node_other = Color(0.86f, 0.87f, 0.90f, 1.0f);
		p.folder = Color(0.95f, 0.78f, 0.42f, 1.0f);
		p.file_scene = Color(0.55f, 0.72f, 0.98f, 1.0f);
		p.file_script = Color(0.58f, 0.90f, 0.70f, 1.0f);
		p.file_image = Color(0.95f, 0.64f, 0.82f, 1.0f);
		p.file_audio = Color(0.98f, 0.76f, 0.46f, 1.0f);
		p.file_mesh = Color(0.98f, 0.56f, 0.52f, 1.0f);
	} else {
		p.success = Color(0.09f, 0.50f, 0.18f, 1.0f);
		p.warning = Color(0.68f, 0.43f, 0.0f, 1.0f);
		p.error = Color(0.80f, 0.14f, 0.14f, 1.0f);
		p.info = Color(0.10f, 0.40f, 0.82f, 1.0f);
		p.node_2d = Color(0.14f, 0.38f, 0.86f, 1.0f);
		p.node_3d = Color(0.82f, 0.24f, 0.20f, 1.0f);
		p.node_gui = Color(0.11f, 0.52f, 0.20f, 1.0f);
		p.node_layer = Color(0.62f, 0.45f, 0.06f, 1.0f);
		p.node_other = Color(0.30f, 0.32f, 0.40f, 1.0f);
		p.folder = Color(0.72f, 0.50f, 0.04f, 1.0f);
		p.file_scene = Color(0.14f, 0.38f, 0.86f, 1.0f);
		p.file_script = Color(0.08f, 0.52f, 0.30f, 1.0f);
		p.file_image = Color(0.74f, 0.22f, 0.52f, 1.0f);
		p.file_audio = Color(0.76f, 0.42f, 0.04f, 1.0f);
		p.file_mesh = Color(0.82f, 0.24f, 0.20f, 1.0f);
	}
	p.file_other = p.text_dim;

	// --- Échelle ---
	const float ui_scale = pref_ui_scale > 0.0f ? pref_ui_scale : _get_auto_ui_scale();
	EditorUI::scale = ui_scale;

	// --- Dimensions (en unités à 100 %, mises à l'échelle à la fin) ---
	ImGuiStyle style;
	const float r = pref_rounding;
	style.WindowRounding = r;
	style.ChildRounding = r;
	style.FrameRounding = r;
	style.PopupRounding = r;
	style.GrabRounding = r;
	style.TabRounding = r;
	style.ScrollbarRounding = std::max(r, 6.0f);
	style.WindowPadding = ImVec2(10, 10);
	style.FramePadding = ImVec2(9, 5);
	style.CellPadding = ImVec2(6, 4);
	style.ItemSpacing = ImVec2(8, 6);
	style.ItemInnerSpacing = ImVec2(6, 5);
	style.IndentSpacing = 18.0f;
	style.ScrollbarSize = 12.0f;
	style.GrabMinSize = 11.0f;
	style.WindowBorderSize = 1.0f;
	style.ChildBorderSize = 1.0f;
	style.PopupBorderSize = 1.0f;
	style.FrameBorderSize = high_contrast ? 1.0f : 0.0f;
	style.TabBarBorderSize = 1.0f;
	style.TabBarOverlineSize = 2.0f;
	style.SeparatorTextBorderSize = 1.0f;
	style.SeparatorTextPadding = ImVec2(12, 6);
	style.DockingSeparatorSize = 3.0f;
	style.WindowMenuButtonPosition = ImGuiDir_None;
	style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
	style.DisabledAlpha = 0.45f;
	// Plus petite taille d'un panneau ou d'une fenêtre flottante : on ne peut pas les écraser
	// jusqu'à les faire disparaître en tirant sur une séparation.
	style.WindowMinSize = ImVec2(150, 96);
	style.ScaleAllSizes(ui_scale);
	style.FontSizeBase = pref_font_size;
	style.FontScaleDpi = ui_scale;

	// --- Couleurs ---
	ImVec4 *c = style.Colors;
	ImGui::StyleColorsDark(&style);
	using EditorUI::col;
	const Color hover = _mix(p.bg_light, toward, 0.10f);
	c[ImGuiCol_Text] = col(p.text);
	c[ImGuiCol_TextDisabled] = col(p.text_dim);
	c[ImGuiCol_WindowBg] = col(p.bg);
	c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_PopupBg] = col(p.dark ? _mix(p.bg, black, 0.18f) : _mix(p.bg, white, 0.55f), 0.99f);
	c[ImGuiCol_Border] = col(p.border);
	c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_FrameBg] = col(p.frame);
	c[ImGuiCol_FrameBgHovered] = col(_mix(p.frame, toward, 0.07f));
	c[ImGuiCol_FrameBgActive] = col(_mix(p.frame, toward, 0.13f));
	c[ImGuiCol_TitleBg] = col(p.bg_dark);
	c[ImGuiCol_TitleBgActive] = col(p.bg_dark);
	c[ImGuiCol_TitleBgCollapsed] = col(p.bg_dark);
	c[ImGuiCol_MenuBarBg] = col(p.bg_dark);
	c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_ScrollbarGrab] = col(_mix(p.bg, toward, 0.22f));
	c[ImGuiCol_ScrollbarGrabHovered] = col(_mix(p.bg, toward, 0.34f));
	c[ImGuiCol_ScrollbarGrabActive] = col(p.accent);
	c[ImGuiCol_CheckMark] = col(p.accent);
	c[ImGuiCol_CheckboxSelectedBg] = col(_mix(p.frame, p.accent, 0.22f));
	c[ImGuiCol_SliderGrab] = col(p.accent);
	c[ImGuiCol_SliderGrabActive] = col(_mix(p.accent, white, 0.25f));
	c[ImGuiCol_Button] = col(p.bg_light);
	c[ImGuiCol_ButtonHovered] = col(hover);
	c[ImGuiCol_ButtonActive] = col(p.accent, 0.45f);
	c[ImGuiCol_Header] = col(_mix(p.bg, toward, 0.10f));
	c[ImGuiCol_HeaderHovered] = col(_mix(p.bg, toward, 0.16f));
	c[ImGuiCol_HeaderActive] = col(p.accent, 0.40f);
	c[ImGuiCol_Separator] = col(p.border);
	c[ImGuiCol_SeparatorHovered] = col(p.accent, 0.55f);
	c[ImGuiCol_SeparatorActive] = col(p.accent);
	c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_ResizeGripHovered] = col(p.accent, 0.45f);
	c[ImGuiCol_ResizeGripActive] = col(p.accent);
	c[ImGuiCol_Tab] = col(p.bg_dark);
	c[ImGuiCol_TabHovered] = col(hover);
	c[ImGuiCol_TabSelected] = col(p.bg);
	c[ImGuiCol_TabSelectedOverline] = col(p.accent);
	c[ImGuiCol_TabDimmed] = col(p.bg_dark);
	c[ImGuiCol_TabDimmedSelected] = col(p.bg);
	c[ImGuiCol_TabDimmedSelectedOverline] = col(p.accent, 0.35f);
	c[ImGuiCol_DockingPreview] = col(p.accent, 0.45f);
	c[ImGuiCol_DockingEmptyBg] = col(p.bg_dark);
	c[ImGuiCol_PlotLines] = col(p.accent);
	c[ImGuiCol_PlotHistogram] = col(p.accent);
	c[ImGuiCol_TableHeaderBg] = col(p.bg_dark);
	c[ImGuiCol_TableBorderStrong] = col(p.border);
	c[ImGuiCol_TableBorderLight] = col(_mix(p.bg, p.border, 0.5f));
	c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_TableRowBgAlt] = col(toward, 0.03f);
	c[ImGuiCol_TextSelectedBg] = col(p.accent, 0.40f);
	c[ImGuiCol_DragDropTarget] = col(p.accent);
	c[ImGuiCol_NavCursor] = col(p.accent);
	c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, p.dark ? 0.55f : 0.35f);

	ImGui::GetStyle() = style;
	_apply_window_settings();
}

bool EditorNode::_toggle_button(const char *p_label, bool p_active, const char *p_tip) {
	using EditorUI::col;
	if (p_active) {
		ImGui::PushStyleColor(ImGuiCol_Button, col(palette.accent, 0.22f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(palette.accent, 0.32f));
		ImGui::PushStyleColor(ImGuiCol_Text, col(palette.accent));
	}
	const bool pressed = ImGui::Button(p_label);
	if (p_active) {
		ImGui::PopStyleColor(3);
	}
	if (p_tip) {
		ImGui::SetItemTooltip("%s", p_tip);
	}
	return pressed;
}

bool EditorNode::_accent_button(const char *p_label, float p_width) {
	using EditorUI::col;
	const Color white(1, 1, 1, 1);
	ImGui::PushStyleColor(ImGuiCol_Button, col(palette.accent));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(_mix(palette.accent, white, 0.18f)));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, col(_mix(palette.accent, Color(0, 0, 0, 1), 0.15f)));
	ImGui::PushStyleColor(ImGuiCol_Text, col(palette.on_accent));
	const bool pressed = ImGui::Button(p_label, ImVec2(p_width, 0));
	ImGui::PopStyleColor(4);
	return pressed;
}

// ---------------------------------------------------------------------------
// Icônes et couleurs des classes et des fichiers
// ---------------------------------------------------------------------------
const char *EditorNode::_class_icon(const String &p_class) {
	static const std::map<String, const char *> icons = {
		{ "Node", ICON_NODE },
		{ "Node2D", ICON_NODE_2D },
		{ "Node3D", ICON_NODE_3D },
		{ "Sprite2D", ICON_NODE_SPRITE },
		{ "AnimatedSprite2D", ICON_NODE_ANIMATED_SPRITE },
		{ "AnimationPlayer", ICON_NODE_ANIMATION },
		{ "Area2D", ICON_NODE_AREA },
		{ "Area3D", ICON_NODE_AREA },
		{ "AudioStreamPlayer", ICON_NODE_AUDIO },
		{ "HBoxContainer", ICON_NODE_HBOX },
		{ "VBoxContainer", ICON_NODE_VBOX },
		{ "Button", ICON_NODE_BUTTON },
		{ "Camera2D", ICON_NODE_CAMERA },
		{ "Camera3D", ICON_NODE_CAMERA },
		{ "CanvasLayer", ICON_NODE_LAYER },
		{ "CenterContainer", ICON_NODE_CENTER },
		{ "CharacterBody2D", ICON_NODE_CHARACTER },
		{ "CharacterBody3D", ICON_NODE_CHARACTER },
		{ "CollisionShape2D", ICON_NODE_SHAPE },
		{ "CollisionShape3D", ICON_NODE_SHAPE },
		{ "ColorRect", ICON_NODE_COLOR_RECT },
		{ "Container", ICON_NODE_CONTAINER },
		{ "Control", ICON_NODE_CONTROL },
		{ "DirectionalLight3D", ICON_NODE_SUN },
		{ "Label", ICON_NODE_LABEL },
		{ "LineEdit", ICON_NODE_LINE_EDIT },
		{ "MarginContainer", ICON_NODE_MARGIN },
		{ "Marker2D", ICON_NODE_MARKER },
		{ "Marker3D", ICON_NODE_MARKER },
		{ "MeshInstance3D", ICON_NODE_MESH },
		{ "OmniLight3D", ICON_NODE_OMNI },
		{ "Panel", ICON_NODE_PANEL },
		{ "PanelContainer", ICON_NODE_PANEL },
		{ "ProgressBar", ICON_NODE_PROGRESS },
		{ "RayCast2D", ICON_NODE_RAY },
		{ "RayCast3D", ICON_NODE_RAY },
		{ "RigidBody2D", ICON_NODE_RIGID },
		{ "RigidBody3D", ICON_NODE_RIGID },
		{ "SpotLight3D", ICON_NODE_SPOT },
		{ "StaticBody2D", ICON_NODE_STATIC },
		{ "StaticBody3D", ICON_NODE_STATIC },
		{ "TextureRect", ICON_NODE_SPRITE },
		{ "TileMap", ICON_NODE_TILEMAP },
		{ "Timer", ICON_NODE_TIMER },
		{ "WorldEnvironment", ICON_NODE_WORLD },
	};
	// Une classe sans icône propre prend celle de son parent le plus proche.
	String cls = p_class;
	for (int depth = 0; depth < 16 && !cls.empty(); depth++) {
		auto it = icons.find(cls);
		if (it != icons.end()) {
			return it->second;
		}
		cls = ClassDB::get_parent_class(cls);
	}
	return ICON_NODE;
}

Color EditorNode::_class_color(const String &p_class) const {
	if (ClassDB::is_parent_class(p_class, "Control")) {
		return palette.node_gui;
	}
	if (ClassDB::is_parent_class(p_class, "Node2D")) {
		return palette.node_2d;
	}
	if (ClassDB::is_parent_class(p_class, "Node3D")) {
		return palette.node_3d;
	}
	if (ClassDB::is_parent_class(p_class, "CanvasLayer")) {
		return palette.node_layer;
	}
	return palette.node_other;
}

static int _file_kind(const String &p_path) {
	const String ext = path_get_extension(p_path);
	if (ext == "scn") {
		return 1;
	}
	if (ext == "mx") {
		return 2;
	}
	if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" || ext == "tga" || ext == "gif") {
		return 3;
	}
	if (ext == "wav") {
		return 4;
	}
	if (ext == "obj") {
		return 5;
	}
	if (ext == "ttf" || ext == "otf") {
		return 6;
	}
	if (ext == "txt" || ext == "md" || ext == "json" || ext == "cfg" || ext == "mex") {
		return 7;
	}
	return 0;
}

const char *EditorNode::_file_icon(const String &p_path) {
	switch (_file_kind(p_path)) {
		case 1:
			return ICON_FILE_SCENE;
		case 2:
			return ICON_FILE_SCRIPT;
		case 3:
			return ICON_FILE_IMAGE;
		case 4:
			return ICON_FILE_AUDIO;
		case 5:
			return ICON_FILE_MESH;
		case 6:
			return ICON_FILE_FONT;
		case 7:
			return ICON_FILE_TEXT;
		default:
			return ICON_FILE;
	}
}

Color EditorNode::_file_color(const String &p_path) const {
	switch (_file_kind(p_path)) {
		case 1:
			return palette.file_scene;
		case 2:
			return palette.file_script;
		case 3:
			return palette.file_image;
		case 4:
			return palette.file_audio;
		case 5:
			return palette.file_mesh;
		default:
			return palette.file_other;
	}
}

// ---------------------------------------------------------------------------
// Fenêtre « Préférences de l'éditeur »
// ---------------------------------------------------------------------------
void EditorNode::_draw_preferences() {
	if (prefs_open) {
		prefs_open = false;
		ImGui::OpenPopup("###EditorPrefs");
	}
	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(std::min(ES(720), viewport->WorkSize.x - ES(40)), 0), ImGuiCond_Always);
	bool open = true;
	if (!ImGui::BeginPopupModal(ICON_SETTINGS "  Préférences de l'éditeur###EditorPrefs", &open, ImGuiWindowFlags_NoSavedSettings)) {
		return;
	}
	bool changed = false;
	using EditorUI::col;
	using EditorUI::u32;

	// --- Thèmes : une carte par thème, avec un aperçu de ses couleurs ---
	ImGui::SeparatorText(ICON_PALETTE "  Thème");
	const float card_w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f;
	const float card_h = ES(74);
	int index = 0;
	auto theme_card = [&](const char *p_name, const char *p_description, const Color &p_base, const Color &p_accent) {
		if (index % 3 != 0) {
			ImGui::SameLine();
		}
		index++;
		ImGui::PushID(p_name);
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		const bool clicked = ImGui::InvisibleButton("##card", ImVec2(card_w, card_h));
		const bool hovered = ImGui::IsItemHovered();
		const bool current = pref_theme == p_name;
		ImDrawList *dl = ImGui::GetWindowDrawList();
		const bool dark = _luminance(p_base) < 0.5f;
		const Color fg = dark ? Color(0.92f, 0.93f, 0.96f, 1) : Color(0.10f, 0.11f, 0.17f, 1);
		const Color side = dark ? _mix(p_base, Color(0, 0, 0, 1), 0.30f) : _mix(p_base, Color(0, 0, 0, 1), 0.08f);
		const float rounding = ES(6);
		const ImVec2 end(pos.x + card_w, pos.y + card_h);
		dl->AddRectFilled(pos, end, u32(p_base), rounding);
		dl->AddRectFilled(pos, ImVec2(pos.x + ES(26), end.y), u32(side), rounding, ImDrawFlags_RoundCornersLeft);
		// Maquette miniature : trois lignes de « texte » et un bouton à la couleur d'accent.
		for (int line = 0; line < 3; line++) {
			const float y = pos.y + ES(12) + line * ES(9);
			dl->AddRectFilled(ImVec2(pos.x + ES(7), y), ImVec2(pos.x + ES(19), y + ES(3)), u32(fg, line == 0 ? 0.9f : 0.35f), ES(2));
		}
		dl->AddRectFilled(ImVec2(end.x - ES(46), pos.y + ES(10)), ImVec2(end.x - ES(10), pos.y + ES(24)), u32(p_accent), ES(4));
		dl->AddText(ImVec2(pos.x + ES(36), pos.y + ES(30)), u32(fg), p_name);
		if (p_description) {
			ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.82f);
			const ImVec4 clip(pos.x + ES(36), pos.y, end.x - ES(6), end.y);
			dl->AddText(nullptr, 0.0f, ImVec2(pos.x + ES(36), pos.y + ES(50)), u32(fg, 0.6f), p_description, nullptr, 0.0f, &clip);
			ImGui::PopFont();
		}
		if (current || hovered) {
			dl->AddRect(pos, end, u32(current ? palette.accent : palette.text_dim), rounding, 0, current ? ES(2.5f) : ES(1.5f));
		}
		if (hovered && p_description) {
			ImGui::SetTooltip("%s", p_description);
		}
		if (clicked && !current) {
			pref_theme = p_name;
			changed = true;
		}
		ImGui::PopID();
	};
	for (const ThemeDef &theme : _themes()) {
		theme_card(theme.name, theme.description, theme.base, theme.accent);
	}
	theme_card(THEME_CUSTOM, "Vos couleurs de fond et d'accent", pref_custom_base, pref_accent);

	ImGui::Spacing();
	const float label_w = ES(210);
	auto row = [&](const char *p_label) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(p_label);
		ImGui::SameLine(label_w);
	};
	if (pref_theme == THEME_CUSTOM) {
		row("Couleur de fond");
		float base[3] = { pref_custom_base.r, pref_custom_base.g, pref_custom_base.b };
		if (ImGui::ColorEdit3("##base", base, ImGuiColorEditFlags_NoInputs)) {
			pref_custom_base = Color(base[0], base[1], base[2], 1.0f);
			changed = true;
		}
	} else {
		row("Couleur d'accent personnalisée");
		changed |= ImGui::Checkbox("##custom_accent", &pref_custom_accent);
	}
	if (pref_custom_accent || pref_theme == THEME_CUSTOM) {
		row("Couleur d'accent");
		float accent[3] = { pref_accent.r, pref_accent.g, pref_accent.b };
		if (ImGui::ColorEdit3("##accent", accent, ImGuiColorEditFlags_NoInputs)) {
			pref_accent = Color(accent[0], accent[1], accent[2], 1.0f);
			changed = true;
		}
	}
	row("Arrondi des angles");
	ImGui::SetNextItemWidth(ES(260));
	changed |= ImGui::SliderFloat("##rounding", &pref_rounding, 0.0f, 12.0f, "%.0f px");

	// --- Affichage ---
	ImGui::SeparatorText(ICON_MONITOR "  Affichage");
	row("Échelle de l'interface");
	const float automatic = _get_auto_ui_scale();
	const float scales[] = { 0.0f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f };
	const String auto_label = vformat("Automatique (%d %%)", (int)std::lround(automatic * 100.0f));
	const String current_label = pref_ui_scale <= 0.0f ? auto_label : vformat("%d %%", (int)std::lround(pref_ui_scale * 100.0f));
	ImGui::SetNextItemWidth(ES(260));
	if (ImGui::BeginCombo("##scale", current_label.c_str())) {
		for (float value : scales) {
			const String label = value <= 0.0f ? auto_label : vformat("%d %%", (int)std::lround(value * 100.0f));
			if (ImGui::Selectable(label.c_str(), std::abs(value - pref_ui_scale) < 0.01f)) {
				pref_ui_scale = value;
				changed = true;
			}
		}
		ImGui::EndCombo();
	}
	row("Taille du texte");
	ImGui::SetNextItemWidth(ES(260));
	ImGui::SliderFloat("##font", &pref_font_size, 12.0f, 24.0f, "%.0f px");
	// Appliquée au relâchement : le curseur ne bouge pas sous la souris pendant le réglage.
	changed |= ImGui::IsItemDeactivatedAfterEdit();
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextDisabled("« Automatique » suit la densité de l'écran et réduit l'interface (jusqu'à 75 %%) quand la fenêtre ou l'écran sont petits. Le texte et les icônes sont vectoriels : ils restent nets à toutes les échelles.");
	ImGui::PopTextWrapPos();

	// --- Fenêtre ---
	ImGui::SeparatorText(ICON_MAXIMIZE "  Fenêtre");
	row("Barre de titre intégrée");
	if (ImGui::Checkbox("##title_bar", &pref_custom_title_bar)) {
		changed = true;
		const bool custom = pref_custom_title_bar;
		_defer([custom]() { DisplayServer::get_singleton()->window_set_borderless(custom); });
	}
	ImGui::SetItemTooltip("Cochée : les boutons Réduire, Agrandir et Fermer sont dans la barre du haut de l'éditeur.\nDécochée : barre de titre du système, sombre avec un thème sombre.");

	// --- Fichiers ---
	ImGui::SeparatorText(ICON_DOCK_FILES "  Fichiers");
	row("Aperçus des fichiers");
	if (ImGui::Checkbox("##thumbs", &pref_thumbnails)) {
		changed = true;
	}
	row("Taille des miniatures");
	ImGui::SetNextItemWidth(ES(260));
	ImGui::SliderFloat("##thumb_size", &pref_thumbnail_size, 40.0f, 160.0f, "%.0f px");
	changed |= ImGui::IsItemDeactivatedAfterEdit();

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	if (ImGui::Button(ICON_REFRESH "  Rétablir les réglages d'origine")) {
		pref_theme = _themes()[0].name;
		pref_custom_accent = false;
		pref_accent = _themes()[0].accent;
		pref_ui_scale = 0.0f;
		pref_font_size = 16.0f;
		pref_rounding = 5.0f;
		pref_thumbnails = true;
		pref_thumbnail_size = 72.0f;
		{
			// Barre de titre : réglage d'origine du système (intégrée sous Windows et Linux).
			const bool custom = get_default_title_bar_preference();
			if (custom != pref_custom_title_bar) {
				pref_custom_title_bar = custom;
				_defer([custom]() { DisplayServer::get_singleton()->window_set_borderless(custom); });
			}
		}
		changed = true;
	}
	// Fenêtre étroite : « Fermer » passe sous le premier bouton au lieu de le recouvrir.
	const float close_x = ImGui::GetWindowWidth() - ES(130) - ImGui::GetStyle().WindowPadding.x;
	if (close_x > ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetStyle().ItemSpacing.x) {
		ImGui::SameLine(close_x);
	}
	if (_accent_button("Fermer", ES(130)) || !open) {
		ImGui::CloseCurrentPopup();
	}
	if (changed) {
		style_dirty = true;
		_save_prefs();
	}
	ImGui::EndPopup();
}
