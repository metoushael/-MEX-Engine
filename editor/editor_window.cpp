// Fenêtre de l'éditeur : barre de titre intégrée (boutons Réduire, Agrandir, Fermer),
// taille minimale, et adaptation de l'interface à la taille de la fenêtre et de l'écran.
#include "editor_node.h"

#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/string/ustring.h"
#include "editor_ui.h"
#include "servers/display_server.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

// ---------------------------------------------------------------------------
// Préférence lue avant la création de la fenêtre
// ---------------------------------------------------------------------------
bool EditorNode::get_default_title_bar_preference() {
#if defined(__APPLE__)
	// macOS : la barre du système suit déjà l'apparence sombre, et ses boutons sont à gauche.
	return false;
#elif defined(_WIN32)
	// Windows : barre du système par défaut. Avec la barre intégrée, la fenêtre garde les styles
	// WS_CAPTION et WS_THICKFRAME et masque son cadre (WM_NCCALCSIZE) ; certains pilotes OpenGL
	// d'Intel (HD Graphics des générations Haswell et Broadwell) dessinent alors l'image décalée
	// de la hauteur de ce cadre alors que la souris reste à sa place : plus aucun bouton ne
	// répond là où il est affiché.
	return false;
#else
	return true;
#endif
}

// Clé de la préférence. Sous Windows, elle a changé de nom en 0.7.1 : l'ancienne valeur, laissée
// à « vrai » par les versions précédentes, ne doit plus activer la barre intégrée toute seule.
const char *EditorNode::get_title_bar_preference_key() {
#if defined(_WIN32)
	return "barre_integree_windows";
#else
	return "barre_de_titre_integree";
#endif
}

bool EditorNode::read_title_bar_preference() {
	bool custom = get_default_title_bar_preference();
	if (char *pref = SDL_GetPrefPath("Metoushela", "MEX")) {
		const String path = String(pref) + "editor_settings.cfg";
		SDL_free(pref);
		if (FileAccess::exists(path)) {
			ConfigFile config;
			if (config.load(path) == OK) {
				custom = config.get_value("fenetre", get_title_bar_preference_key(), Variant(custom)).as_bool();
			}
		}
	}
	// Variable d'environnement : tests et captures, sans toucher aux préférences.
	if (const char *forced = SDL_getenv("MEX_EDITOR_TITLE_BAR")) {
		custom = SDL_strcmp(forced, "systeme") != 0 && SDL_strcmp(forced, "0") != 0;
	}
	return custom;
}

String EditorNode::read_render_driver_preference() {
	String driver = "auto";
	if (char *pref = SDL_GetPrefPath("Metoushela", "MEX")) {
		const String path = String(pref) + "editor_settings.cfg";
		SDL_free(pref);
		if (FileAccess::exists(path)) {
			ConfigFile config;
			if (config.load(path) == OK) {
				driver = config.get_value("affichage", "pilote_de_rendu", Variant(driver)).as_string();
			}
		}
	}
	return driver.empty() ? String("auto") : driver;
}

// ---------------------------------------------------------------------------
// Contrôle de l'affichage et mode de compatibilité
// ---------------------------------------------------------------------------
// L'image dépend du pilote graphique de chaque machine : une combinaison (pilote de rendu,
// barre de titre) jamais vue ici est soumise une fois à l'utilisateur, qui peut revenir d'un
// clic à la combinaison la plus sûre.
String EditorNode::_get_display_signature() const {
	DisplayServer *display = DisplayServer::get_singleton();
	return (display ? display->get_render_driver() : String()) + "|" + (display && display->window_is_borderless() ? "integree" : "systeme");
}

void EditorNode::_enable_compatibility_mode() {
	DisplayServer *display = DisplayServer::get_singleton();
	const String safe_driver = DisplayServer::get_compatible_render_driver();
	pref_render_driver = safe_driver;
	pref_custom_title_bar = false;
	// La barre de titre du système revient tout de suite ; le pilote de rendu ne peut changer
	// qu'au démarrage.
	_defer([]() { DisplayServer::get_singleton()->window_set_borderless(false); });
	pref_display_checked = safe_driver + "|systeme";
	display_check_dismissed = true;
	display_restart_notice = display && display->get_render_driver() != safe_driver;
	_save_prefs();
}

void EditorNode::_write_diagnostics() const {
	DisplayServer *display = DisplayServer::get_singleton();
	if (!display || prefs_dir.empty()) {
		return;
	}
	String text = String("MEX ") + MEX_VERSION + "\n" + display->get_diagnostics();
	text += String("Préférences : pilote de rendu « ") + pref_render_driver + " », barre de titre " + (pref_custom_title_bar ? "intégrée" : "du système") + "\n";
	if (FILE *file = fopen((prefs_dir + "diagnostic.txt").c_str(), "wb")) {
		fwrite(text.data(), 1, text.size(), file);
		fclose(file);
	}
}

void EditorNode::_draw_display_check() {
	DisplayServer *display = DisplayServer::get_singleton();
	if (!display) {
		return;
	}
	if (display_restart_notice) {
		ImGui::OpenPopup("Mode de compatibilité###CompatNotice");
		display_restart_notice = false;
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("Mode de compatibilité###CompatNotice", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::PushTextWrapPos(ES(440));
		ImGui::TextUnformatted("La barre de titre du système est en place.");
		ImGui::TextUnformatted("Le pilote de rendu le plus sûr sera utilisé au prochain démarrage : fermez MEX puis relancez-le.");
		ImGui::TextDisabled("La vue 3D, les shaders et les lumières 2D ont besoin d'OpenGL : ils restent désactivés dans ce mode. Tout se règle dans Éditeur > Préférences.");
		ImGui::PopTextWrapPos();
		ImGui::Spacing();
		if (_accent_button("Quitter MEX", ES(150))) {
			ImGui::CloseCurrentPopup();
			_defer([this]() { _request_quit(); });
		}
		ImGui::SameLine();
		if (ImGui::Button("Plus tard", ImVec2(ES(120), 0))) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	const String signature = _get_display_signature();
	const String safe_signature = DisplayServer::get_compatible_render_driver() + "|systeme";
	// « * » : contrôle désactivé (scénarios de test).
	if (display_check_dismissed || ImGui::GetTopMostPopupModal()) {
		return;
	}
	if (!display_check_forced && (pref_display_checked == "*" || signature == pref_display_checked || signature == safe_signature)) {
		return;
	}
	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	const float margin = ES(16);
	ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - margin, viewport->WorkPos.y + viewport->WorkSize.y - margin), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
	ImGui::SetNextWindowSize(ImVec2(std::min(ES(400), viewport->WorkSize.x - margin * 2.0f), 0.0f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, std::max(1.0f, ES(1.5f)));
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
	if (ImGui::Begin("###DisplayCheck", nullptr, flags)) {
		ImGui::TextUnformatted("L'affichage est-il correct ?");
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextDisabled("Image décalée, coupée, lente ou qui scintille : passez en mode de compatibilité (barre de titre du système, rendu le plus sûr).");
		ImGui::PopTextWrapPos();
		ImGui::Spacing();
		if (_accent_button("Oui, tout va bien", ES(160))) {
			pref_display_checked = signature;
			display_check_forced = false;
			_save_prefs();
		}
		ImGui::SameLine();
		if (ImGui::Button("Mode de compatibilité")) {
			display_check_forced = false;
			_enable_compatibility_mode();
		}
	}
	ImGui::End();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
	// Toujours au-dessus des panneaux, sans prendre le focus.
	if (ImGuiWindow *window = ImGui::FindWindowByName("###DisplayCheck")) {
		if (!ImGui::GetTopMostPopupModal()) {
			ImGui::BringWindowToDisplayFront(window);
		}
	}
}

bool EditorNode::_has_custom_title_bar() const {
	const DisplayServer *display = DisplayServer::get_singleton();
	return display && display->window_is_borderless() && !display->window_is_fullscreen();
}

// ---------------------------------------------------------------------------
// Boutons de fenêtre
// ---------------------------------------------------------------------------
float EditorNode::_get_window_buttons_width() const {
	return _has_custom_title_bar() ? ES(46) * 3.0f : 0.0f;
}

float EditorNode::_draw_window_buttons(float p_top, float p_height) {
	if (!_has_custom_title_bar()) {
		return 0.0f;
	}
	using EditorUI::u32;
	DisplayServer *display = DisplayServer::get_singleton();
	const bool maximized = display->window_is_maximized();
	const float button_w = ES(46);
	const float total_w = button_w * 3.0f;
	const ImVec2 origin = ImGui::GetWindowPos();
	const float right = ImGui::GetWindowWidth();
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const float glyph = std::floor(ES(10) * 0.5f); // demi-côté du pictogramme
	const float thickness = std::max(1.0f, std::round(EditorUI::scale * 1.1f));
	const char *tips[3] = { "Réduire", maximized ? "Restaurer" : "Agrandir", "Fermer" };

	ImGui::PushID("##window_buttons");
	for (int i = 0; i < 3; i++) {
		const float x = right - total_w + button_w * (float)i;
		ImGui::SetCursorPos(ImVec2(x, p_top));
		ImGui::PushID(i);
		const bool clicked = ImGui::InvisibleButton("##button", ImVec2(button_w, p_height));
		ImGui::PopID();
		const bool hovered = ImGui::IsItemHovered();
		const bool held = ImGui::IsItemActive();
		const ImVec2 min(origin.x + x, origin.y + p_top);
		const ImVec2 max(min.x + button_w, min.y + p_height);
		ImU32 color = u32(palette.text);
		if (hovered || held) {
			if (i == 2) {
				// Fermer : rouge, comme sur toutes les fenêtres.
				dl->AddRectFilled(min, max, held ? IM_COL32(0xB5, 0x1B, 0x26, 255) : IM_COL32(0xE8, 0x11, 0x23, 255));
				color = IM_COL32_WHITE;
			} else {
				dl->AddRectFilled(min, max, u32(palette.text, held ? 0.20f : 0.12f));
			}
		}
		// Pictogrammes tracés à la main : nets à toutes les échelles, au pixel près.
		const ImVec2 c(std::floor((min.x + max.x) * 0.5f) + 0.5f, std::floor((min.y + max.y) * 0.5f) + 0.5f);
		switch (i) {
			case 0:
				dl->AddLine(ImVec2(c.x - glyph, c.y), ImVec2(c.x + glyph, c.y), color, thickness);
				break;
			case 1:
				if (maximized) {
					// Deux carrés décalés : « restaurer ».
					const float d = std::max(2.0f, std::floor(glyph * 0.4f));
					dl->AddRect(ImVec2(c.x - glyph, c.y - glyph + d), ImVec2(c.x + glyph - d, c.y + glyph), color, 0.0f, 0, thickness);
					dl->AddLine(ImVec2(c.x - glyph + d, c.y - glyph + d), ImVec2(c.x - glyph + d, c.y - glyph), color, thickness);
					dl->AddLine(ImVec2(c.x - glyph + d, c.y - glyph), ImVec2(c.x + glyph, c.y - glyph), color, thickness);
					dl->AddLine(ImVec2(c.x + glyph, c.y - glyph), ImVec2(c.x + glyph, c.y + glyph - d), color, thickness);
					dl->AddLine(ImVec2(c.x + glyph, c.y + glyph - d), ImVec2(c.x + glyph - d, c.y + glyph - d), color, thickness);
				} else {
					dl->AddRect(ImVec2(c.x - glyph, c.y - glyph), ImVec2(c.x + glyph, c.y + glyph), color, 0.0f, 0, thickness);
				}
				break;
			default:
				dl->AddLine(ImVec2(c.x - glyph, c.y - glyph), ImVec2(c.x + glyph, c.y + glyph), color, thickness);
				dl->AddLine(ImVec2(c.x - glyph, c.y + glyph), ImVec2(c.x + glyph, c.y - glyph), color, thickness);
				break;
		}
		ImGui::SetItemTooltip("%s", tips[i]);
		if (clicked) {
			switch (i) {
				case 0:
					_defer([]() { DisplayServer::get_singleton()->window_minimize(); });
					break;
				case 1:
					_defer([maximized]() { DisplayServer::get_singleton()->window_set_maximized(!maximized); });
					break;
				default:
					_request_quit();
					break;
			}
		}
	}
	ImGui::PopID();
	return total_w;
}

void EditorNode::_add_caption_area(float p_x0, float p_y0, float p_x1, float p_y1) {
	if (p_x1 - p_x0 >= 1.0f && p_y1 - p_y0 >= 1.0f) {
		caption_areas.push_back({ p_x0, p_y0, p_x1, p_y1 });
	}
}

// ---------------------------------------------------------------------------
// Fin d'image : zones de déplacement, bords de redimensionnement, curseurs
// ---------------------------------------------------------------------------
void EditorNode::_update_window_chrome() {
	DisplayServer *display = DisplayServer::get_singleton();
	const bool custom = _has_custom_title_bar();
	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImGuiContext &g = *GImGui;
	std::vector<DisplayServer::WindowRect> areas;

	// Un menu ou une boîte de dialogue ouverts : un clic dans la barre doit les fermer, pas
	// déplacer la fenêtre.
	if (custom && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
		// Une fenêtre flottante posée sur la barre garde ses propres clics.
		for (const ImGuiWindow *window : g.Windows) {
			if (!window->WasActive || window->Hidden || window->DockIsActive) {
				continue;
			}
			if (window->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoBringToFrontOnFocus)) {
				continue;
			}
			if (SDL_strcmp(window->Name, "##MainMenuBar") == 0) {
				continue;
			}
			const float wx0 = window->Pos.x, wy0 = window->Pos.y, wx1 = window->Pos.x + window->Size.x, wy1 = window->Pos.y + window->Size.y;
			std::vector<CaptionArea> kept;
			for (const CaptionArea &area : caption_areas) {
				if (wy1 <= area.y0 || wy0 >= area.y1 || wx1 <= area.x0 || wx0 >= area.x1) {
					kept.push_back(area);
					continue;
				}
				if (wx0 > area.x0) {
					kept.push_back({ area.x0, area.y0, wx0, area.y1 });
				}
				if (wx1 < area.x1) {
					kept.push_back({ wx1, area.y0, area.x1, area.y1 });
				}
			}
			caption_areas.swap(kept);
		}
		for (const CaptionArea &area : caption_areas) {
			DisplayServer::WindowRect rect;
			rect.x = (int)std::ceil(area.x0 - viewport->Pos.x);
			rect.y = (int)std::ceil(area.y0 - viewport->Pos.y);
			rect.w = (int)std::floor(area.x1 - viewport->Pos.x) - rect.x;
			rect.h = (int)std::floor(area.y1 - viewport->Pos.y) - rect.y;
			if (rect.w > 0 && rect.h > 0) {
				areas.push_back(rect);
			}
		}
	}
	caption_areas.clear();
	const int border = custom ? std::max(4, (int)ES(5)) : 0;
	display->window_set_caption_areas(areas, border);

	if (custom && !display->window_is_maximized()) {
		// Liseré : sans cadre du système, il sépare la fenêtre de ce qui se trouve derrière.
		const ImVec2 min = viewport->Pos;
		const ImVec2 max(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
		ImGui::GetForegroundDrawList()->AddRect(min, max, EditorUI::u32(palette.border), 0.0f, 0, 1.0f);

		// Curseur de redimensionnement au survol des bords (Windows le montre de lui-même).
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		const int x = (int)std::floor(mouse.x - viewport->Pos.x), y = (int)std::floor(mouse.y - viewport->Pos.y);
		if (ImGui::IsMousePosValid(&mouse) && x >= 0 && y >= 0 && x < (int)viewport->Size.x && y < (int)viewport->Size.y && !ImGui::IsAnyMouseDown()) {
			switch (display->window_hit_test(x, y)) {
				case SDL_HITTEST_RESIZE_TOPLEFT:
				case SDL_HITTEST_RESIZE_BOTTOMRIGHT:
					ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
					break;
				case SDL_HITTEST_RESIZE_TOPRIGHT:
				case SDL_HITTEST_RESIZE_BOTTOMLEFT:
					ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW);
					break;
				case SDL_HITTEST_RESIZE_TOP:
				case SDL_HITTEST_RESIZE_BOTTOM:
					ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
					break;
				case SDL_HITTEST_RESIZE_LEFT:
				case SDL_HITTEST_RESIZE_RIGHT:
					ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
					break;
				default:
					break;
			}
		}
	}
	display->window_process_requests();
}

// ---------------------------------------------------------------------------
// Adaptation à la taille de la fenêtre
// ---------------------------------------------------------------------------
// Échelle automatique : dans une fenêtre (ou sur un écran) trop petite pour l'interface à sa
// taille normale, tout est réduit, jusqu'à 75 %, par paliers de 5 %.
void EditorNode::_update_window_fit() {
	const DisplayServer *display = DisplayServer::get_singleton();
	const Size2 size = display->window_get_logical_size();
	const float density = std::max(display->get_screen_scale(), 0.5f);
	float fit = 1.0f;
	if (size.x > 0 && size.y > 0) {
		fit = std::min((float)size.x / (COMFORT_LAYOUT_WIDTH * density), (float)size.y / (COMFORT_LAYOUT_HEIGHT * density));
		fit = std::clamp(std::floor(fit * 20.0f + 0.001f) / 20.0f, MIN_WINDOW_FIT, 1.0f);
	}
	if (std::abs(fit - window_fit) > 0.001f) {
		window_fit = fit;
		if (pref_ui_scale <= 0.0f) {
			style_dirty = true;
		}
	}
}

// Taille minimale de la fenêtre et couleurs de la barre de titre du système : dépendent de
// l'échelle et du thème, donc réappliquées avec le style.
void EditorNode::_apply_window_settings() {
	DisplayServer *display = DisplayServer::get_singleton();
	if (!display || !display->get_window()) {
		return;
	}
	// À la taille minimale, l'échelle automatique vaut 75 % de celle de l'écran.
	const float scale_at_min = pref_ui_scale > 0.0f ? pref_ui_scale : display->get_screen_scale() * MIN_WINDOW_FIT;
	display->window_set_min_size((int)std::ceil(MIN_LAYOUT_WIDTH * scale_at_min), (int)std::ceil(MIN_LAYOUT_HEIGHT * scale_at_min));
	display->window_set_title_bar_theme(palette.dark, palette.bg_dark, palette.text);
}

// Les fenêtres flottantes (préférences, paramètres, boîtes de dialogue...) ne dépassent jamais
// de la fenêtre principale : elles rétrécissent avec elle et y sont ramenées.
void EditorNode::_fit_floating_windows() {
	ImGuiContext &g = *GImGui;
	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	const bool resized = std::abs(viewport->Size.x - last_viewport_w) > 0.5f || std::abs(viewport->Size.y - last_viewport_h) > 0.5f;
	last_viewport_w = viewport->Size.x;
	last_viewport_h = viewport->Size.y;
	const float margin = ES(8);
	const ImVec2 area_min(viewport->WorkPos.x + margin, viewport->WorkPos.y + margin);
	const ImVec2 area_max(viewport->WorkPos.x + viewport->WorkSize.x - margin, viewport->WorkPos.y + viewport->WorkSize.y - margin);
	const ImVec2 max_size(std::max(area_max.x - area_min.x, 1.0f), std::max(area_max.y - area_min.y, 1.0f));
	for (ImGuiWindow *window : g.Windows) {
		if (!window->WasActive || window->Hidden || window->DockIsActive) {
			continue;
		}
		// Les menus, listes déroulantes et infobulles s'ajustent déjà seuls à l'écran.
		if (window->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_ChildMenu)) {
			continue;
		}
		if ((window->Flags & ImGuiWindowFlags_NoMove) && (window->Flags & ImGuiWindowFlags_NoResize)) {
			continue; // barre de menus, fonds
		}
		ImVec2 size = window->SizeFull;
		if (size.x > max_size.x || size.y > max_size.y) {
			size = ImVec2(std::min(size.x, max_size.x), std::min(size.y, max_size.y));
			ImGui::SetWindowSize(window, size);
		}
		if (resized) {
			ImVec2 pos = window->Pos;
			pos.x = std::max(std::min(pos.x, area_max.x - size.x), area_min.x);
			pos.y = std::max(std::min(pos.y, area_max.y - size.y), area_min.y);
			if (pos.x != window->Pos.x || pos.y != window->Pos.y) {
				ImGui::SetWindowPos(window, pos);
			}
		}
	}
}

// Les panneaux ancrés gardent leurs proportions quand la fenêtre change de taille (sans cela,
// seuls les panneaux du centre absorbent la différence, et finissent écrasés).
static void _scale_dock_node(ImGuiDockNode *p_node, const ImVec2 &p_ratio) {
	if (!p_node) {
		return;
	}
	p_node->SizeRef = ImVec2(p_node->SizeRef.x * p_ratio.x, p_node->SizeRef.y * p_ratio.y);
	p_node->Size = ImVec2(p_node->Size.x * p_ratio.x, p_node->Size.y * p_ratio.y);
	_scale_dock_node(p_node->ChildNodes[0], p_ratio);
	_scale_dock_node(p_node->ChildNodes[1], p_ratio);
}

void EditorNode::_rescale_dock_layout(unsigned int p_dockspace_id) {
	ImGuiDockNode *root = ImGui::DockBuilderGetNode(p_dockspace_id);
	if (!root || root->IsLeafNode()) {
		return;
	}
	const ImVec2 target = ImGui::GetMainViewport()->WorkSize;
	if (root->Size.x < 16.0f || root->Size.y < 16.0f || target.x < 16.0f || target.y < 16.0f) {
		return;
	}
	if (std::abs(root->Size.x - target.x) < 0.5f && std::abs(root->Size.y - target.y) < 0.5f) {
		return;
	}
	const ImVec2 ratio(target.x / root->Size.x, target.y / root->Size.y);
	_scale_dock_node(root->ChildNodes[0], ratio);
	_scale_dock_node(root->ChildNodes[1], ratio);
}
