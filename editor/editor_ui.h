// Outils communs à l'interface de l'éditeur : échelle, couleurs, icônes.
#pragma once

#include "core/math/color.h"
#include "editor_icons.gen.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace EditorUI {

// Échelle de l'interface : échelle d'affichage du système x réglage de l'utilisateur. Toute
// taille écrite « en dur » dans l'éditeur passe par ES() pour rester proportionnée.
extern float scale;

inline float S(float p_value) {
	return std::floor(p_value * scale + 0.5f);
}

inline ImVec4 col(const Color &p_color) {
	return ImVec4(p_color.r, p_color.g, p_color.b, p_color.a);
}

inline ImVec4 col(const Color &p_color, float p_alpha) {
	return ImVec4(p_color.r, p_color.g, p_color.b, p_color.a * p_alpha);
}

inline ImU32 u32(const Color &p_color, float p_alpha = 1.0f) {
	return ImGui::ColorConvertFloat4ToU32(ImVec4(p_color.r, p_color.g, p_color.b, p_color.a * p_alpha));
}

// Dessine « icône + texte » à partir de p_pos, sans créer d'élément d'interface.
inline void icon_label(const ImVec2 &p_pos, const char *p_icon, ImU32 p_icon_color, const char *p_text, ImU32 p_text_color) {
	ImDrawList *dl = ImGui::GetWindowDrawList();
	dl->AddText(p_pos, p_icon_color, p_icon);
	dl->AddText(ImVec2(p_pos.x + ImGui::GetFontSize() * 1.45f, p_pos.y), p_text_color, p_text);
}

// Taille d'ouverture d'une boîte de dialogue, en unités d'interface, bornée par la fenêtre
// principale : sur un petit écran la boîte s'ouvre déjà entière.
inline ImVec2 dialog_size(float p_width, float p_height) {
	const ImVec2 work = ImGui::GetMainViewport()->WorkSize;
	const float margin = S(16.0f);
	return ImVec2(std::max(std::min(S(p_width), work.x - margin), 1.0f), std::max(std::min(S(p_height), work.y - margin), 1.0f));
}

// Texte d'aide, en couleur atténuée, qui revient à la ligne à la largeur du panneau (un
// panneau étroit ne coupe pas la phrase).
inline void hint(const char *p_text) {
	ImGui::PushTextWrapPos(0.0f);
	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	ImGui::TextUnformatted(p_text);
	ImGui::PopStyleColor();
	ImGui::PopTextWrapPos();
}

// Libellé d'un nœud d'arbre créé avec un texte vide (TreeNodeEx(id, flags, "%s", "")) :
// p_row est la position du curseur relevée juste avant l'appel à TreeNodeEx.
inline void tree_label(const ImVec2 &p_row, const char *p_icon, ImU32 p_icon_color, const char *p_text, ImU32 p_text_color) {
	const float offset = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.x * 2.0f;
	icon_label(ImVec2(p_row.x + offset, p_row.y), p_icon, p_icon_color, p_text, p_text_color);
}

} // namespace EditorUI

#define ES(m_value) EditorUI::S((float)(m_value))
