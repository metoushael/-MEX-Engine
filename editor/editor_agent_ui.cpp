// Interface de « MEX Agent » : mode Agent (grande vue centrale) et panneau latéral.
//
// Tout ce qui se voit est dessiné ici : en-tête, écran d'accueil, bulles, texte mis en forme
// (titres, listes, gras, code), étapes des outils, zone de saisie. Les mouvements (apparition
// des messages, texte qui s'écrit, survols, défilement) suivent l'horloge et se désactivent
// dans « Connexion » ou avec MEX_AGENT_ANIMATIONS=0.
#include "editor_agent.h"

#include "core/config/project_settings.h"
#include "core/io/json.h"
#include "core/string/ustring.h"
#include "editor_agent_tools.h"
#include "editor_node.h"
#include "editor_ui.h"

#include <SDL.h>
#include <algorithm>
#include <cmath>

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

namespace {

// Couleurs et horloge de l'image en cours, relevées une fois dans draw().
struct Look {
	Color bg, bg_dark, bg_light, frame, border, text, text_dim, accent, on_accent, success, warning, error;
	Color accent2; // seconde teinte du dégradé de l'emblème
	ImFont *mono = nullptr;
	bool animate = true;
	float dt = 0.016f;
	double now = 0.0;
	float column = 0.0f; // largeur de la colonne de texte
};
Look L;

Color mix(const Color &p_a, const Color &p_b, float p_t) {
	return Color(p_a.r + (p_b.r - p_a.r) * p_t, p_a.g + (p_b.g - p_a.g) * p_t, p_a.b + (p_b.b - p_a.b) * p_t, p_a.a + (p_b.a - p_a.a) * p_t);
}

// Couleur de dessin ; tient compte de la transparence en cours (apparition des messages).
ImU32 U(const Color &p_color, float p_alpha = 1.0f) {
	return ImGui::GetColorU32(ImVec4(p_color.r, p_color.g, p_color.b, p_color.a * p_alpha));
}

float ease_out(float p_t) {
	p_t = std::clamp(p_t, 0.0f, 1.0f);
	const float inverse = 1.0f - p_t;
	return 1.0f - inverse * inverse * inverse;
}

// Valeur qui glisse vers sa cible d'une image à l'autre (survols, focus).
float approach(ImGuiID p_id, float p_target, float p_speed = 14.0f) {
	float *value = ImGui::GetStateStorage()->GetFloatRef(p_id, p_target);
	if (!L.animate) {
		*value = p_target;
	} else {
		*value += (p_target - *value) * std::min(1.0f, L.dt * p_speed);
		if (std::fabs(p_target - *value) < 0.002f) {
			*value = p_target;
		}
	}
	return *value;
}

void text_bold(ImDrawList *p_dl, ImFont *p_font, float p_size, const ImVec2 &p_pos, ImU32 p_color, const char *p_begin, const char *p_end = nullptr) {
	p_dl->AddText(p_font, p_size, p_pos, p_color, p_begin, p_end);
	p_dl->AddText(p_font, p_size, ImVec2(p_pos.x + std::max(0.5f, p_size * 0.04f), p_pos.y), p_color, p_begin, p_end);
}

// Emblème : disque en dégradé qui respire, plus vif quand l'agent travaille.
void draw_orb(ImDrawList *p_dl, const ImVec2 &p_center, float p_radius, bool p_active) {
	const float pulse = L.animate ? 0.5f + 0.5f * (float)std::sin(L.now * (p_active ? 5.0 : 1.5)) : 0.5f;
	for (int i = 3; i >= 1; i--) {
		p_dl->AddCircleFilled(p_center, p_radius + p_radius * 0.16f * i * (0.55f + 0.6f * pulse), U(L.accent, 0.045f * (4 - i)), 48);
	}
	p_dl->AddCircleFilled(p_center, p_radius, U(L.accent), 48);
	const float angle = L.animate ? (float)std::fmod(L.now * (p_active ? 2.6 : 0.8), 6.283185307) : 0.8f;
	const ImVec2 spot(p_center.x + std::cos(angle) * p_radius * 0.3f, p_center.y + std::sin(angle) * p_radius * 0.3f);
	p_dl->AddCircleFilled(spot, p_radius * 0.7f, U(L.accent2, 0.6f), 40);
	const float size = p_radius * 1.15f;
	ImFont *font = ImGui::GetFont();
	const ImVec2 icon = font->CalcTextSizeA(size, FLT_MAX, 0.0f, ICON_SPARKLES);
	p_dl->AddText(font, size, ImVec2(p_center.x - icon.x * 0.5f, p_center.y - icon.y * 0.5f), U(L.on_accent), ICON_SPARKLES);
}

void draw_spinner(ImDrawList *p_dl, const ImVec2 &p_center, float p_radius, ImU32 p_color, float p_thickness) {
	const float start = L.animate ? (float)std::fmod(L.now * 5.5, 6.283185307) : 0.0f;
	p_dl->PathArcTo(p_center, p_radius, start, start + 4.2f, 24);
	p_dl->PathStroke(p_color, 0, p_thickness);
}

// Bouton carré sans fond, réduit à son icône ; le fond apparaît au survol.
bool icon_button(const char *p_id, const char *p_icon, const char *p_tip, float p_size = 0.0f, bool p_enabled = true, const Color *p_color = nullptr) {
	const float size = p_size > 0.0f ? p_size : ES(28);
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::BeginDisabled(!p_enabled);
	const bool pressed = ImGui::InvisibleButton(p_id, ImVec2(size, size));
	ImGui::EndDisabled();
	const bool hovered = p_enabled && ImGui::IsItemHovered();
	const float t = approach(ImGui::GetItemID(), hovered ? 1.0f : 0.0f);
	ImDrawList *dl = ImGui::GetWindowDrawList();
	if (t > 0.01f) {
		dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), U(L.text, (ImGui::IsItemActive() ? 0.16f : 0.10f) * t), size * 0.28f);
	}
	const ImVec2 icon = ImGui::CalcTextSize(p_icon);
	const Color color = p_color ? *p_color : mix(L.text_dim, L.text, t);
	dl->AddText(ImVec2(std::floor(pos.x + (size - icon.x) * 0.5f), std::floor(pos.y + (size - icon.y) * 0.5f)), U(color, p_enabled ? 1.0f : 0.35f), p_icon);
	if (p_tip && p_tip[0]) {
		ImGui::SetItemTooltip("%s", p_tip);
	}
	return pressed;
}

// Pastille « icône + texte » cliquable. p_max_width : largeur maximale (le texte est coupé).
bool chip(const char *p_id, const char *p_icon, const char *p_text, const char *p_tip, float p_max_width = 0.0f, bool p_highlight = false) {
	const float height = ES(26), pad = ES(10), gap = ES(6);
	const ImVec2 icon = p_icon ? ImGui::CalcTextSize(p_icon) : ImVec2(0, 0);
	const ImVec2 text = ImGui::CalcTextSize(p_text);
	float width = pad * 2.0f + text.x + (p_icon ? icon.x + gap : 0.0f);
	if (p_max_width > 0.0f) {
		width = std::min(width, p_max_width);
	}
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const bool pressed = ImGui::InvisibleButton(p_id, ImVec2(width, height));
	const float t = approach(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.0f : 0.0f);
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const Color tint = p_highlight ? L.accent : L.text;
	dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), U(tint, (p_highlight ? 0.14f : 0.06f) + 0.07f * t), height * 0.5f);
	const Color color = p_highlight ? L.accent : mix(L.text_dim, L.text, t);
	float x = pos.x + pad;
	const float y = std::floor(pos.y + (height - text.y) * 0.5f);
	if (p_icon) {
		dl->AddText(ImVec2(x, y), U(color), p_icon);
		x += icon.x + gap;
	}
	dl->PushClipRect(ImVec2(x, pos.y), ImVec2(pos.x + width - pad * 0.6f, pos.y + height), true);
	dl->AddText(ImVec2(x, y), U(color), p_text);
	dl->PopClipRect();
	if (p_tip && p_tip[0]) {
		ImGui::SetItemTooltip("%s", p_tip);
	}
	return pressed;
}

// --- Texte mis en forme --------------------------------------------------------------------
struct Span {
	const char *begin;
	const char *end;
	int style; // 0 : normal, 1 : gras, 2 : code, 3 : lien
};

void parse_inline(const char *p_begin, const char *p_end, std::vector<Span> &r_spans) {
	const char *start = p_begin;
	const char *c = p_begin;
	const auto flush = [&](const char *p_until) {
		if (p_until > start) {
			r_spans.push_back({ start, p_until, 0 });
		}
	};
	while (c < p_end) {
		if (*c == '`') {
			const char *close = (const char *)memchr(c + 1, '`', (size_t)(p_end - c - 1));
			if (close) {
				flush(c);
				r_spans.push_back({ c + 1, close, 2 });
				c = start = close + 1;
				continue;
			}
		} else if (*c == '*' && c + 1 < p_end && c[1] == '*') {
			const char *close = nullptr;
			for (const char *s = c + 2; s + 1 < p_end; s++) {
				if (s[0] == '*' && s[1] == '*') {
					close = s;
					break;
				}
			}
			if (close && close > c + 2) {
				flush(c);
				r_spans.push_back({ c + 2, close, 1 });
				c = start = close + 2;
				continue;
			}
		} else if (*c == '[') {
			// [texte](adresse) : seul le texte est affiché.
			const char *close = (const char *)memchr(c + 1, ']', (size_t)(p_end - c - 1));
			if (close && close + 1 < p_end && close[1] == '(') {
				const char *paren = (const char *)memchr(close + 2, ')', (size_t)(p_end - close - 2));
				if (paren) {
					flush(c);
					r_spans.push_back({ c + 1, close, 3 });
					c = start = paren + 1;
					continue;
				}
			}
		}
		c++;
	}
	flush(p_end);
}

int utf8_length(unsigned char p_lead) {
	return p_lead < 0x80 ? 1 : (p_lead >> 5) == 0x6 ? 2 : (p_lead >> 4) == 0xe ? 3 : (p_lead >> 3) == 0x1e ? 4 : 1;
}

// Place les morceaux mot à mot dans la largeur donnée ; dessine si p_dl n'est pas nul.
// Renvoie la hauteur occupée.
float layout_inline(ImDrawList *p_dl, const ImVec2 &p_origin, float p_width, const std::vector<Span> &p_spans, float p_size, const Color &p_color, bool p_all_bold) {
	ImFont *font = ImGui::GetFont();
	const float line_height = std::floor(p_size * 1.5f);
	float x = 0.0f, y = 0.0f;
	const auto place = [&](const char *p_begin, const char *p_end, int p_style) {
		ImFont *used = p_style == 2 && L.mono ? L.mono : font;
		const float size = p_style == 2 ? p_size * 0.94f : p_size;
		const float width = used->CalcTextSizeA(size, FLT_MAX, 0.0f, p_begin, p_end).x;
		const float pad = p_style == 2 ? std::floor(p_size * 0.22f) : 0.0f;
		if (x > 0.0f && x + width + pad * 2.0f > p_width) {
			x = 0.0f;
			y += line_height;
		}
		if (p_dl) {
			const ImVec2 pos(std::floor(p_origin.x + x + pad), std::floor(p_origin.y + y + (line_height - p_size) * 0.5f));
			if (p_style == 2) {
				p_dl->AddRectFilled(ImVec2(pos.x - pad, p_origin.y + y + 1.0f), ImVec2(pos.x + width + pad, p_origin.y + y + line_height - 1.0f), U(L.text, 0.09f), p_size * 0.25f);
				p_dl->AddText(used, size, ImVec2(pos.x, pos.y + (p_size - size) * 0.5f), U(mix(p_color, L.accent, 0.35f)), p_begin, p_end);
			} else if (p_style == 1 || p_all_bold) {
				text_bold(p_dl, used, size, pos, U(L.text), p_begin, p_end);
			} else if (p_style == 3) {
				p_dl->AddText(used, size, pos, U(L.accent), p_begin, p_end);
			} else {
				p_dl->AddText(used, size, pos, U(p_color), p_begin, p_end);
			}
		}
		x += width + pad * 2.0f;
	};
	const float space = font->CalcTextSizeA(p_size, FLT_MAX, 0.0f, " ").x;
	for (const Span &span : p_spans) {
		const char *c = span.begin;
		if (span.style == 2) {
			// Code : gardé d'un bloc tant qu'il tient dans la largeur.
			const float width = (L.mono ? L.mono : font)->CalcTextSizeA(p_size * 0.94f, FLT_MAX, 0.0f, span.begin, span.end).x;
			if (width + p_size * 0.5f <= p_width) {
				place(span.begin, span.end, 2);
				continue;
			}
		}
		while (c < span.end) {
			if (*c == ' ' || *c == '\t') {
				const char *s = c;
				while (s < span.end && (*s == ' ' || *s == '\t')) {
					s++;
				}
				if (x > 0.0f) {
					x += space * (float)(s - c);
				}
				c = s;
				continue;
			}
			const char *word = c;
			while (c < span.end && *c != ' ' && *c != '\t') {
				c++;
			}
			const float width = font->CalcTextSizeA(p_size, FLT_MAX, 0.0f, word, c).x;
			if (width <= p_width) {
				place(word, c, span.style);
			} else {
				// Mot plus large que la colonne (adresse, texte sans espaces) : coupé par caractère.
				const char *piece = word;
				while (piece < c) {
					const char *next = std::min(piece + utf8_length((unsigned char)*piece), c);
					place(piece, next, span.style);
					piece = next;
				}
			}
		}
	}
	return y + line_height;
}

// Un paragraphe (ou un titre, un élément de liste) : mesure, puis dessin s'il est visible.
void draw_paragraph(const char *p_begin, const char *p_end, float p_indent, float p_scale, const Color &p_color, bool p_bold) {
	std::vector<Span> spans;
	parse_inline(p_begin, p_end, spans);
	const float size = ImGui::GetFontSize() * p_scale;
	const float width = std::max(L.column - p_indent, ES(40));
	const ImVec2 cursor = ImGui::GetCursorScreenPos();
	const ImVec2 origin(cursor.x + p_indent, cursor.y);
	const float height = layout_inline(nullptr, origin, width, spans, size, p_color, p_bold);
	if (ImGui::IsRectVisible(origin, ImVec2(origin.x + width, origin.y + height))) {
		layout_inline(ImGui::GetWindowDrawList(), origin, width, spans, size, p_color, p_bold);
	}
	ImGui::Dummy(ImVec2(L.column, height));
}

void draw_code_block(const String &p_code, const String &p_language, int p_index) {
	ImGui::PushID(p_index);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ES(8));
	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ES(12), ES(8)));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ES(6), ES(4)));
	ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorUI::col(mix(L.bg_dark, Color(0, 0, 0, 1), 0.18f)));
	ImGui::PushStyleColor(ImGuiCol_Border, EditorUI::col(L.border, 0.9f));
	if (ImGui::BeginChild("##code", ImVec2(L.column, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
		float *copied = ImGui::GetStateStorage()->GetFloatRef(ImGui::GetID("copie"), -10.0f);
		const bool recent = (float)L.now - *copied < 1.4f;
		if (icon_button("##copier", recent ? ICON_CHECK : ICON_COPY, "Copier le code", ES(22), true, recent ? &L.success : nullptr)) {
			ImGui::SetClipboardText(p_code.c_str());
			*copied = (float)L.now;
		}
		ImGui::SameLine();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (ES(22) - ImGui::GetTextLineHeight()) * 0.5f);
		ImGui::TextDisabled("%s", recent ? "copié" : (p_language.empty() ? "code" : p_language.c_str()));
		if (L.mono) {
			ImGui::PushFont(L.mono, 0.0f);
		}
		ImGui::TextUnformatted(p_code.c_str(), p_code.c_str() + p_code.size());
		if (L.mono) {
			ImGui::PopFont();
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar(4);
	ImGui::PopID();
}

bool is_rule(const String &p_line) {
	if (p_line.size() < 3) {
		return false;
	}
	for (char c : p_line) {
		if (c != '-' && c != '*' && c != '_' && c != ' ') {
			return false;
		}
	}
	return p_line[0] != ' ';
}

} // namespace

// Texte au format Markdown courant des modèles : titres, listes, citations, gras, code.
void EditorAgent::_draw_text(const String &p_text) {
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, ES(3)));
	bool in_code = false;
	String code, language;
	int code_index = 0;
	size_t start = 0;
	while (start <= p_text.size()) {
		size_t end = p_text.find('\n', start);
		if (end == String::npos) {
			end = p_text.size();
		}
		const String line = p_text.substr(start, end - start);
		start = end + 1;
		const String stripped = str_strip(line);
		if (str_begins_with(stripped, "```")) {
			if (in_code) {
				if (!code.empty() && code.back() == '\n') {
					code.pop_back();
				}
				draw_code_block(code, language, code_index++);
				code.clear();
			} else {
				language = str_strip(stripped.substr(3));
			}
			in_code = !in_code;
			continue;
		}
		if (in_code) {
			code += line;
			code += '\n';
			continue;
		}
		if (stripped.empty()) {
			ImGui::Dummy(ImVec2(L.column, ES(5)));
			continue;
		}
		const char *b = line.c_str();
		const char *e = b + line.size();
		size_t lead = 0;
		while (lead < line.size() && (line[lead] == ' ' || line[lead] == '\t')) {
			lead++;
		}
		const char *t = b + lead;
		// Titre.
		size_t hashes = 0;
		while (t + hashes < e && t[hashes] == '#') {
			hashes++;
		}
		if (hashes >= 1 && hashes <= 6 && t + hashes < e && t[hashes] == ' ') {
			ImGui::Dummy(ImVec2(L.column, ES(4)));
			draw_paragraph(t + hashes + 1, e, 0.0f, hashes == 1 ? 1.3f : hashes == 2 ? 1.17f : 1.06f, L.text, true);
			continue;
		}
		if (is_rule(stripped)) {
			const ImVec2 p = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y + ES(5)), ImVec2(p.x + L.column, p.y + ES(5)), U(L.border));
			ImGui::Dummy(ImVec2(L.column, ES(10)));
			continue;
		}
		const float level_indent = std::min((float)(lead / 2), 4.0f) * ES(16);
		// Liste à puces.
		if (e - t >= 2 && (t[0] == '-' || t[0] == '*' || t[0] == '+') && t[1] == ' ') {
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const float size = ImGui::GetFontSize();
			ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + level_indent + ES(8), p.y + std::floor(size * 0.75f)), std::max(1.5f, size * 0.14f), U(L.accent), 12);
			draw_paragraph(t + 2, e, level_indent + ES(20), 1.0f, L.text, false);
			continue;
		}
		// Liste numérotée.
		const char *digits = t;
		while (digits < e && *digits >= '0' && *digits <= '9') {
			digits++;
		}
		if (digits > t && digits - t <= 3 && digits + 1 < e && (*digits == '.' || *digits == ')') && digits[1] == ' ') {
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const float size = ImGui::GetFontSize();
			const String number(t, (size_t)(digits - t + 1));
			const float number_width = ImGui::CalcTextSize(number.c_str()).x;
			const float body = std::max(ES(22), number_width + ES(8));
			ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + level_indent + body - number_width - ES(6), p.y + std::floor(size * 0.25f)), U(L.text_dim), number.c_str());
			draw_paragraph(digits + 2, e, level_indent + body, 1.0f, L.text, false);
			continue;
		}
		// Citation.
		if (t < e && t[0] == '>') {
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const char *body = t + 1;
			if (body < e && *body == ' ') {
				body++;
			}
			draw_paragraph(body, e, ES(14), 1.0f, L.text_dim, false);
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + 1.0f), ImVec2(p.x + ES(3), ImGui::GetItemRectMax().y - 1.0f), U(L.accent, 0.6f), ES(2));
			continue;
		}
		draw_paragraph(t, e, level_indent, 1.0f, L.text, false);
	}
	if (in_code && !code.empty()) {
		// Bloc encore ouvert : le texte est en train de s'écrire.
		if (code.back() == '\n') {
			code.pop_back();
		}
		draw_code_block(code, language, code_index++);
	}
	ImGui::PopStyleVar();
}

void EditorAgent::_draw_message(Message &p_message, int p_index) {
	ImGui::PushID(p_index);
	ImDrawList *dl = ImGui::GetWindowDrawList();
	// Apparition : le message monte de quelques pixels en devenant opaque.
	if (p_message.ui_born < 0.0) {
		p_message.ui_born = L.now;
	}
	const float appear = L.animate ? ease_out((float)(L.now - p_message.ui_born) / 0.28f) : 1.0f;
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * (0.05f + 0.95f * appear));
	if (appear < 1.0f) {
		ImGui::Dummy(ImVec2(L.column, std::floor(ES(12) * (1.0f - appear))));
	}
	const float column = L.column;

	if (p_message.kind == Message::USER) {
		ImGui::Dummy(ImVec2(column, ES(8)));
		const float pad_x = ES(14), pad_y = ES(9);
		const float max_text = std::max(ES(60), std::min(column * 0.82f, column - ES(24)) - pad_x * 2.0f);
		const ImVec2 text = ImGui::CalcTextSize(p_message.text.c_str(), nullptr, false, max_text);
		const ImVec2 size(text.x + pad_x * 2.0f, text.y + pad_y * 2.0f);
		const ImVec2 cursor = ImGui::GetCursorScreenPos();
		const ImVec2 pos(cursor.x + column - size.x, cursor.y);
		if (ImGui::IsRectVisible(pos, ImVec2(pos.x + size.x, pos.y + size.y))) {
			dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), U(mix(L.frame, L.accent, 0.2f)), ES(14));
			dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(pos.x + pad_x, pos.y + pad_y), U(L.text), p_message.text.c_str(), nullptr, max_text);
		}
		ImGui::Dummy(ImVec2(column, size.y));
	} else if (p_message.kind == Message::NOTICE) {
		ImGui::Dummy(ImVec2(column, ES(6)));
		const float pad = ES(12);
		const float icon_width = ImGui::CalcTextSize(ICON_WARNING).x + ES(10);
		const float wrap = std::max(ES(60), column - pad * 2.0f - icon_width);
		const ImVec2 text = ImGui::CalcTextSize(p_message.text.c_str(), nullptr, false, wrap);
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		const float height = text.y + pad * 2.0f;
		dl->AddRectFilled(pos, ImVec2(pos.x + column, pos.y + height), U(L.warning, 0.10f), ES(10));
		dl->AddRect(pos, ImVec2(pos.x + column, pos.y + height), U(L.warning, 0.45f), ES(10));
		dl->AddText(ImVec2(pos.x + pad, pos.y + pad), U(L.warning), ICON_WARNING);
		dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(pos.x + pad + icon_width, pos.y + pad), U(L.text), p_message.text.c_str(), nullptr, wrap);
		ImGui::Dummy(ImVec2(column, height));
	} else {
		// Un seul en-tête pour une suite de réponses du modèle.
		if (p_index == 0 || messages[(size_t)p_index - 1].kind != Message::ASSISTANT) {
			ImGui::Dummy(ImVec2(column, ES(10)));
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			const float radius = ES(10);
			draw_orb(dl, ImVec2(pos.x + radius, pos.y + radius), radius, false);
			text_bold(dl, ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(pos.x + radius * 2.0f + ES(9), pos.y + radius - ImGui::GetFontSize() * 0.5f), U(L.text), "MEX Agent");
			ImGui::Dummy(ImVec2(column, radius * 2.0f + ES(4)));
		}
		if (!p_message.text.empty()) {
			// Le texte s'écrit : la réponse arrive d'un bloc, elle est révélée progressivement.
			const float total = (float)p_message.text.size();
			if (!L.animate) {
				p_message.ui_revealed = total;
			} else if (p_message.ui_revealed < total) {
				const float speed = std::max(700.0f, (total - p_message.ui_revealed) * 3.0f);
				p_message.ui_revealed = std::min(total, p_message.ui_revealed + speed * L.dt);
			}
			size_t shown = (size_t)p_message.ui_revealed;
			while (shown > 0 && shown < p_message.text.size() && ((unsigned char)p_message.text[shown] & 0xc0) == 0x80) {
				shown--;
			}
			if (shown >= p_message.text.size()) {
				_draw_text(p_message.text);
			} else {
				_draw_text(p_message.text.substr(0, shown));
			}
			// Copie proposée pour la réponse finale, pas pour chaque phrase entre deux étapes.
			if (shown >= p_message.text.size() && p_message.calls.empty()) {
				float *copied = ImGui::GetStateStorage()->GetFloatRef(ImGui::GetID("copie"), -10.0f);
				const bool recent = (float)L.now - *copied < 1.4f;
				if (icon_button("##copier", recent ? ICON_CHECK : ICON_COPY, "Copier la réponse", ES(24), true, recent ? &L.success : nullptr)) {
					ImGui::SetClipboardText(p_message.text.c_str());
					*copied = (float)L.now;
				}
			}
		}
		const bool text_done = p_message.text.empty() || p_message.ui_revealed >= (float)p_message.text.size();
		for (size_t i = 0; text_done && i < p_message.calls.size(); i++) {
			ToolCall &call = p_message.calls[i];
			ImGui::PushID((int)i);
			if (call.ui_born < 0.0) {
				call.ui_born = L.now;
			}
			const float row_appear = L.animate ? ease_out((float)(L.now - call.ui_born) / 0.22f) : 1.0f;
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * row_appear);
			// Étape : état, description, détails dépliables.
			const float height = ES(30);
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			if (ImGui::InvisibleButton("##appel", ImVec2(column, height))) {
				call.ui_open = !call.ui_open;
			}
			const float hover = approach(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.0f : 0.0f);
			dl->AddRectFilled(pos, ImVec2(pos.x + column, pos.y + height), U(L.text, 0.045f + 0.045f * hover), ES(8));
			const ImVec2 mark(pos.x + ES(16), pos.y + height * 0.5f);
			const float line = ImGui::GetTextLineHeight();
			if (call.state == ToolCall::DONE) {
				const char *icon = call.is_error ? ICON_ERROR : ICON_CHECK;
				const ImVec2 icon_size = ImGui::CalcTextSize(icon);
				dl->AddText(ImVec2(mark.x - icon_size.x * 0.5f, mark.y - icon_size.y * 0.5f), U(call.is_error ? L.error : L.success), icon);
			} else if (call.state == ToolCall::NEEDS_APPROVAL) {
				const ImVec2 icon_size = ImGui::CalcTextSize(ICON_LOCK);
				dl->AddText(ImVec2(mark.x - icon_size.x * 0.5f, mark.y - icon_size.y * 0.5f), U(L.accent), ICON_LOCK);
			} else {
				draw_spinner(dl, mark, ES(6), U(L.accent), std::max(1.5f, ES(2)));
			}
			const String label = EditorAgentTools::describe(call);
			const char *chevron = call.ui_open ? ICON_CHEVRON_DOWN : ICON_CHEVRON_RIGHT;
			const ImVec2 chevron_size = ImGui::CalcTextSize(chevron);
			dl->PushClipRect(ImVec2(pos.x + ES(32), pos.y), ImVec2(pos.x + column - chevron_size.x - ES(16), pos.y + height), true);
			dl->AddText(ImVec2(pos.x + ES(32), pos.y + (height - line) * 0.5f), U(mix(L.text_dim, L.text, 0.45f + 0.55f * hover)), label.c_str());
			dl->PopClipRect();
			dl->AddText(ImVec2(pos.x + column - chevron_size.x - ES(10), pos.y + (height - chevron_size.y) * 0.5f), U(L.text_dim), chevron);
			if (call.ui_open) {
				String details = JSON::stringify(Variant(call.arguments), "  ");
				if (details.size() > 3000) {
					details = details.substr(0, 3000) + "\n...";
				}
				if (call.state == ToolCall::DONE) {
					String result = call.result;
					if (result.size() > 3000) {
						result = result.substr(0, 3000) + "\n...";
					}
					details += "\n\n" + result;
				}
				draw_code_block(details, call.name, 0);
			}
			if (call.state == ToolCall::NEEDS_APPROVAL) {
				// Demande d'accord : carte au liseré d'accent qui respire doucement.
				const float glow = L.animate ? 0.55f + 0.45f * (float)std::sin(L.now * 3.0) : 1.0f;
				ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ES(10));
				ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, std::max(1.0f, ES(1.5f)));
				ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ES(14), ES(12)));
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ES(8), ES(8)));
				ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ES(8));
				ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorUI::col(L.accent, 0.07f));
				ImGui::PushStyleColor(ImGuiCol_Border, EditorUI::col(L.accent, 0.35f + 0.5f * glow));
				if (ImGui::BeginChild("##accord", ImVec2(column, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders)) {
					ImGui::TextColored(EditorUI::col(L.accent), ICON_LOCK "  Votre accord est demandé");
					ImGui::PushTextWrapPos(0.0f);
					ImGui::TextUnformatted(EditorAgentTools::describe_change(call).c_str());
					ImGui::PopTextWrapPos();
					if (editor->_accent_button(ICON_CHECK "  Autoriser")) {
						call.state = ToolCall::RUNNING;
					}
					ImGui::SameLine();
					if (ImGui::Button("Refuser")) {
						_finish_call(call, "L'utilisateur a refusé cette modification. Ne la retente pas telle quelle : demande-lui ce qu'il souhaite.", true);
					}
					ImGui::SameLine();
					if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize("Toujours autoriser").x + ImGui::GetStyle().FramePadding.x * 2.0f) {
						ImGui::NewLine();
					}
					if (ImGui::Button("Toujours autoriser")) {
						ask_before_changes = false;
						save_settings();
						call.state = ToolCall::RUNNING;
					}
					ImGui::SetItemTooltip("Ne plus demander : MEX Agent modifie le projet sans attendre.\nSe règle d'un clic sous la zone de saisie.");
				}
				ImGui::EndChild();
				ImGui::PopStyleColor(2);
				ImGui::PopStyleVar(5);
			}
			ImGui::PopStyleVar();
			ImGui::PopID();
		}
	}
	ImGui::PopStyleVar();
	ImGui::PopID();
}

void EditorAgent::enter_agent_mode() {
	if (!agent_mode || !open) {
		agent_mode = true;
		open = true;
		mode_born = -1.0;
		save_settings();
	}
	dock_request = 0;
	want_focus = true;
}

void EditorAgent::leave_agent_mode() {
	if (!agent_mode) {
		return;
	}
	agent_mode = false;
	dock_request = 2; // retour dans le panneau latéral, avec l'inspecteur
	dock_tries = 0;
	save_settings();
}

void EditorAgent::_draw_welcome(float p_height) {
	static const struct {
		const char *icon;
		const char *title;
		const char *text;
	} SUGGESTIONS[] = {
		{ ICON_ROCKET, "Un jeu de plateforme", "Crée un petit jeu de plateforme : un personnage qui court et saute, trois plateformes et cinq pièces à ramasser." },
		{ ICON_NODE_BUTTON, "Des commandes tactiles", "Ajoute des commandes tactiles (stick et bouton de saut) à mon jeu." },
		{ ICON_SEARCH, "Trouver une erreur", "Mon jeu affiche une erreur au lancement : trouve-la et corrige-la." },
		{ ICON_LAYOUT, "Un menu principal", "Ajoute un menu principal avec les boutons Jouer et Quitter." },
	};
	const int count = (int)(sizeof(SUGGESTIONS) / sizeof(SUGGESTIONS[0]));
	if (welcome_born < 0.0) {
		welcome_born = L.now;
	}
	const float column = L.column;
	const bool wide = column >= ES(540);
	ImDrawList *dl = ImGui::GetWindowDrawList();
	ImFont *font = ImGui::GetFont();
	const float base = ImGui::GetFontSize();
	const float title_size = std::floor(base * (wide ? 1.75f : 1.35f));
	const float radius = wide ? ES(30) : ES(22);
	const char *title = "Que voulons-nous créer ?";
	const char *subtitle = "Décrivez votre idée : MEX Agent lit le projet, écrit les scènes et les scripts, lance le jeu et corrige ce qu'il voit dans la sortie.";
	const float subtitle_wrap = std::min(column, ES(560));
	const ImVec2 subtitle_size = ImGui::CalcTextSize(subtitle, nullptr, false, subtitle_wrap);
	const int columns = wide ? 2 : 1;
	const float gap = ES(10);
	const float card_width = std::floor((column - gap * (columns - 1)) / columns);
	const float card_pad = ES(14);
	const float icon_box = ES(30);
	float card_height = 0.0f;
	for (int i = 0; i < count; i++) {
		const float text_height = ImGui::CalcTextSize(SUGGESTIONS[i].text, nullptr, false, card_width - card_pad * 2.0f).y;
		card_height = std::max(card_height, card_pad * 2.0f + (wide ? icon_box + ES(8) + base * 1.3f : icon_box + ES(8)) + text_height);
	}
	const int rows = (count + columns - 1) / columns;
	const float total = radius * 2.0f + ES(26) + title_size + ES(10) + subtitle_size.y + ES(26) + rows * card_height + (rows - 1) * gap + ES(60);
	// Centré en hauteur quand la place le permet.
	ImGui::Dummy(ImVec2(column, std::max(ES(12), std::floor((p_height - total) * 0.42f))));

	const auto appear_at = [&](float p_delay) {
		return L.animate ? ease_out((float)(L.now - welcome_born - p_delay) / 0.45f) : 1.0f;
	};
	ImVec2 pos = ImGui::GetCursorScreenPos();
	const float center = pos.x + column * 0.5f;
	float a = appear_at(0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * a);
	draw_orb(dl, ImVec2(center, pos.y + radius + ES(8) * (1.0f - a)), radius, state != IDLE);
	ImGui::PopStyleVar();
	ImGui::Dummy(ImVec2(column, radius * 2.0f + ES(26)));

	a = appear_at(0.08f);
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * a);
	pos = ImGui::GetCursorScreenPos();
	const ImVec2 title_extent = font->CalcTextSizeA(title_size, FLT_MAX, 0.0f, title);
	text_bold(dl, font, title_size, ImVec2(std::floor(center - title_extent.x * 0.5f), pos.y + ES(8) * (1.0f - a)), U(L.text), title);
	ImGui::Dummy(ImVec2(column, title_size + ES(10)));
	pos = ImGui::GetCursorScreenPos();
	// Sous-titre centré ligne à ligne.
	{
		const char *c = subtitle;
		const char *end = subtitle + strlen(subtitle);
		float y = pos.y;
		while (c < end) {
			const char *line_end = font->CalcWordWrapPosition(base, c, end, subtitle_wrap);
			if (line_end == c) {
				line_end = end;
			}
			const float width = font->CalcTextSizeA(base, FLT_MAX, 0.0f, c, line_end).x;
			dl->AddText(font, base, ImVec2(std::floor(center - width * 0.5f), y), U(L.text_dim), c, line_end);
			y += ImGui::GetTextLineHeight();
			c = line_end;
			while (c < end && *c == ' ') {
				c++;
			}
		}
	}
	ImGui::Dummy(ImVec2(column, subtitle_size.y + ES(26)));
	ImGui::PopStyleVar();

	// Clé manquante : le dire tout de suite plutôt qu'à la première demande.
	const Provider &provider = get_provider(provider_index);
	if (provider.key_variable[0] && _effective_key(provider, _settings()).empty()) {
		const char *notice = "Aucune clé d'API pour ce service : ouvrez « Connexion » pour la saisir.";
		const float wrap = column - ES(28) - ImGui::CalcTextSize(ICON_INFO).x - ES(10);
		const ImVec2 size = ImGui::CalcTextSize(notice, nullptr, false, wrap);
		pos = ImGui::GetCursorScreenPos();
		const float height = size.y + ES(20);
		if (ImGui::InvisibleButton("##cle_manquante", ImVec2(column, height))) {
			show_settings = true;
		}
		const float hover = approach(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.0f : 0.0f);
		dl->AddRectFilled(pos, ImVec2(pos.x + column, pos.y + height), U(L.accent, 0.08f + 0.06f * hover), ES(10));
		dl->AddRect(pos, ImVec2(pos.x + column, pos.y + height), U(L.accent, 0.4f), ES(10));
		dl->AddText(ImVec2(pos.x + ES(14), pos.y + ES(10)), U(L.accent), ICON_INFO);
		dl->AddText(font, base, ImVec2(pos.x + ES(24) + ImGui::CalcTextSize(ICON_INFO).x, pos.y + ES(10)), U(L.text), notice, nullptr, wrap);
		ImGui::Dummy(ImVec2(column, ES(12)));
	}

	// Cartes de suggestion, qui arrivent l'une après l'autre.
	pos = ImGui::GetCursorScreenPos();
	for (int i = 0; i < count; i++) {
		const int row = i / columns, cell = i % columns;
		a = appear_at(0.16f + 0.06f * i);
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * a);
		ImGui::PushID(i);
		ImVec2 card(pos.x + cell * (card_width + gap), pos.y + row * (card_height + gap) + ES(14) * (1.0f - a));
		ImGui::SetCursorScreenPos(ImVec2(card.x, pos.y + row * (card_height + gap)));
		if (ImGui::InvisibleButton("##suggestion", ImVec2(card_width, card_height))) {
			prompt = SUGGESTIONS[i].text;
			focus_input = true;
		}
		const float hover = approach(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.0f : 0.0f, 12.0f);
		card.y -= std::floor(ES(2) * hover);
		const ImVec2 card_max(card.x + card_width, card.y + card_height);
		if (hover > 0.01f) {
			dl->AddRectFilled(ImVec2(card.x, card.y + ES(4)), ImVec2(card_max.x, card_max.y + ES(4)), U(Color(0, 0, 0, 1), 0.16f * hover), ES(12));
		}
		dl->AddRectFilled(card, card_max, U(mix(L.frame, L.bg_light, 0.4f + 0.6f * hover)), ES(12));
		dl->AddRect(card, card_max, U(mix(L.border, L.accent, hover), 0.5f + 0.5f * hover), ES(12));
		const ImVec2 box(card.x + card_pad, card.y + card_pad);
		dl->AddRectFilled(box, ImVec2(box.x + icon_box, box.y + icon_box), U(L.accent, 0.16f), ES(8));
		const ImVec2 icon = ImGui::CalcTextSize(SUGGESTIONS[i].icon);
		dl->AddText(ImVec2(std::floor(box.x + (icon_box - icon.x) * 0.5f), std::floor(box.y + (icon_box - icon.y) * 0.5f)), U(L.accent), SUGGESTIONS[i].icon);
		if (wide) {
			text_bold(dl, font, base, ImVec2(box.x, box.y + icon_box + ES(8)), U(L.text), SUGGESTIONS[i].title);
			dl->AddText(font, base, ImVec2(box.x, box.y + icon_box + ES(8) + base * 1.3f), U(L.text_dim), SUGGESTIONS[i].text, nullptr, card_width - card_pad * 2.0f);
		} else {
			// Panneau étroit : le titre à côté de l'icône, le texte dessous.
			text_bold(dl, font, base, ImVec2(box.x + icon_box + ES(10), std::floor(box.y + (icon_box - base) * 0.5f)), U(L.text), SUGGESTIONS[i].title);
			dl->AddText(font, base, ImVec2(box.x, box.y + icon_box + ES(8)), U(L.text_dim), SUGGESTIONS[i].text, nullptr, card_width - card_pad * 2.0f);
		}
		ImGui::PopID();
		ImGui::PopStyleVar();
	}
	ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + rows * card_height + (rows - 1) * gap));
	ImGui::Dummy(ImVec2(column, ES(18)));

	// Compétences : pastilles qui passent à la ligne.
	a = appear_at(0.45f);
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * a);
	ImGui::TextDisabled("Compétences");
	ImGui::SetItemTooltip("Fiches que MEX Agent consulte selon la demande.\nAjoutez les vôtres : des fichiers .md dans le dossier « competences » du projet.");
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ES(6), ES(6)));
	const float line_start = ImGui::GetCursorScreenPos().x;
	float x = 0.0f;
	for (size_t i = 0; i < skills.size(); i++) {
		const float width = ImGui::CalcTextSize(skills[i].name.c_str()).x + ES(20);
		if (x > 0.0f && x + width > column) {
			x = 0.0f;
		} else if (x > 0.0f) {
			ImGui::SameLine(0.0f, ES(6));
		}
		(void)line_start;
		ImGui::PushID((int)i);
		chip("##competence", nullptr, skills[i].name.c_str(), (skills[i].description + "\n(" + skills[i].origin + ")").c_str());
		ImGui::PopID();
		x += width + ES(6);
	}
	ImGui::PopStyleVar(2);
	ImGui::Dummy(ImVec2(column, ES(12)));
}

void EditorAgent::draw() {
	_poll_job();
	_process_tools();
	if (!open) {
		return;
	}
	using EditorUI::col;
	// Le jeu lancé par l'utilisateur (F5, boutons de lecture) doit se voir : le mode Agent
	// se replie. Lancé par l'agent lui-même, il reste derrière et l'agent en lit la sortie.
	const bool game_running = editor->game.is_running();
	if (agent_mode && game_running && !game_was_running && !run_started_game) {
		leave_agent_mode();
	}
	game_was_running = game_running;

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
	float mode_alpha = 1.0f;
	int pushed_vars = 1;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	if (agent_mode) {
		// Mode Agent : la conversation recouvre toute la zone de travail, sous la barre du haut.
		const ImGuiViewport *viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
		ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
		ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);
		flags |= ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		pushed_vars += 2;
		if (mode_born < 0.0) {
			mode_born = ImGui::GetTime();
			want_focus = true;
		}
		// Entrée dans le mode : la vue apparaît en fondu.
		if (animations && !animations_forced_off) {
			const float enter = std::clamp((float)(ImGui::GetTime() - mode_born) / 0.22f, 0.0f, 1.0f);
			mode_alpha = 0.15f + 0.85f * (1.0f - (1.0f - enter) * (1.0f - enter));
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * mode_alpha);
			pushed_vars++;
		}
	} else if (dock_request != 0) {
		const ImGuiWindow *target = ImGui::FindWindowByName("###Inspector");
		if (target && target->DockId != 0) {
			ImGui::SetNextWindowDockID(target->DockId, ImGuiCond_Always);
			dock_request = 0;
			want_focus = true;
		} else if (++dock_tries > 30) {
			// Pas d'inspecteur où s'ancrer : fenêtre flottante de taille raisonnable.
			ImGui::SetNextWindowSize(EditorUI::dialog_size(420, 600), ImGuiCond_Always);
			dock_request = 0;
		}
	}
	if (want_focus) {
		ImGui::SetNextWindowFocus();
	}
	if (!agent_mode) {
		ImGui::SetNextWindowSize(EditorUI::dialog_size(420, 600), ImGuiCond_FirstUseEver);
	}
	bool visible = open;
	const bool shown = ImGui::Begin(ICON_SPARKLES "  MEX Agent###Agent", agent_mode ? nullptr : &visible, flags);
	ImGui::PopStyleVar(pushed_vars);
	if (visible != open) {
		open = visible;
		save_settings();
	}
	if (!shown) {
		ImGui::End();
		if (want_focus) {
			focus_input = true;
		}
		want_focus = false;
		return;
	}
	_load_skills();
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * mode_alpha);

	// Couleurs et horloge de cette image.
	const auto &palette = editor->palette;
	L.bg = palette.bg;
	L.bg_dark = palette.bg_dark;
	L.bg_light = palette.bg_light;
	L.frame = palette.frame;
	L.border = palette.border;
	L.text = palette.text;
	L.text_dim = palette.text_dim;
	L.accent = palette.accent;
	L.on_accent = palette.on_accent;
	L.success = palette.success;
	L.warning = palette.warning;
	L.error = palette.error;
	L.accent2 = Color(L.accent.b * 0.6f + 0.35f, L.accent.r * 0.35f, std::min(1.0f, L.accent.g * 0.5f + 0.6f), 1.0f);
	L.mono = editor->font_mono;
	L.animate = animations && !animations_forced_off;
	L.dt = std::min(ImGui::GetIO().DeltaTime, 0.1f);
	L.now = ImGui::GetTime();

	const Provider &provider = get_provider(provider_index);
	const String model = _effective_model(provider, _settings());
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float margin = avail.x >= ES(520) ? ES(24) : ES(12);

	// Volet des changements : à droite, quand la vue est large.
	const bool side_panel = avail.x >= ES(1040) && !originals.empty();
	const float side_width = side_panel ? ES(270) : 0.0f;
	const float main_width = avail.x - side_width;
	const float column = std::max(ES(120), std::min(main_width - margin * 2.0f, ES(820)));
	const float column_x = std::floor((main_width - column) * 0.5f);
	L.column = column;

	// --- En-tête ---------------------------------------------------------------------------
	const float header_height = ES(44);
	{
		const float radius = ES(10);
		const float y = origin.y + header_height * 0.5f;
		draw_orb(dl, ImVec2(origin.x + margin + radius, y), radius, state != IDLE);
		const float base = ImGui::GetFontSize();
		float x = origin.x + margin + radius * 2.0f + ES(9);
		text_bold(dl, ImGui::GetFont(), base, ImVec2(x, y - base * 0.5f), U(L.text), "MEX Agent");
		x += ImGui::CalcTextSize("MEX Agent").x + ES(10);
		const float buttons = ES(28) * 4.0f + ES(4) * 3.0f;
		const float right = origin.x + avail.x - margin;
		if (agent_mode) {
			const char *badge = "mode Agent";
			const ImVec2 size = ImGui::CalcTextSize(badge);
			if (x + size.x + ES(16) < right - buttons - ES(8)) {
				dl->AddRectFilled(ImVec2(x, y - size.y * 0.5f - ES(3)), ImVec2(x + size.x + ES(16), y + size.y * 0.5f + ES(3)), U(L.accent, 0.15f), ES(10));
				dl->AddText(ImVec2(x + ES(8), y - size.y * 0.5f), U(L.accent), badge);
			}
		}
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ES(4), 0));
		ImGui::SetCursorScreenPos(ImVec2(right - buttons, y - ES(14)));
		if (icon_button("##nouveau", ICON_PLUS, "Nouvelle conversation", 0.0f, !messages.empty())) {
			new_conversation();
			welcome_born = -1.0;
		}
		ImGui::SameLine();
		const String undo_tip = "Annuler les changements : remettre les fichiers modifiés ou créés\npar MEX Agent dans leur état d'avant cette conversation (" + std::to_string(originals.size()) + " fichier(s))";
		if (icon_button("##annuler", ICON_UNDO, undo_tip.c_str(), 0.0f, !originals.empty() && state == IDLE)) {
			_revert_changes();
		}
		if (!originals.empty()) {
			// Pastille : nombre de fichiers touchés.
			const ImVec2 corner(ImGui::GetItemRectMax().x - ES(5), ImGui::GetItemRectMin().y + ES(6));
			dl->AddCircleFilled(corner, ES(4), U(L.accent), 12);
		}
		ImGui::SameLine();
		if (icon_button("##connexion", ICON_SETTINGS, "Connexion : service, clé d'API, modèle", 0.0f, true, show_settings ? &L.accent : nullptr)) {
			show_settings = !show_settings;
		}
		ImGui::SameLine();
		if (agent_mode) {
			if (icon_button("##mode", ICON_LAYOUT, "Quitter le mode Agent : revenir à l'éditeur, la conversation reste dans le panneau latéral", 0.0f)) {
				leave_agent_mode();
			}
		} else {
			if (icon_button("##mode", ICON_MAXIMIZE, "Passer en mode Agent : la conversation prend toute la fenêtre (Ctrl+Maj+A)", 0.0f)) {
				enter_agent_mode();
			}
		}
		ImGui::PopStyleVar();
		dl->AddLine(ImVec2(origin.x, origin.y + header_height), ImVec2(origin.x + avail.x, origin.y + header_height), U(L.border, 0.6f));
	}

	// --- Zone de saisie : mesures ----------------------------------------------------------
	const float composer_pad = ES(12);
	const float inner_width = column - composer_pad * 2.0f;
	const float line_height = ImGui::GetTextLineHeight();
	const ImVec2 frame_padding(ES(2), ES(3));
	int lines = 1;
	if (!prompt.empty()) {
		lines = (int)std::ceil(ImGui::CalcTextSize(prompt.c_str(), nullptr, false, inner_width - frame_padding.x * 2.0f - ES(8)).y / line_height - 0.01f);
		if (prompt.back() == '\n') {
			lines++;
		}
	}
	lines = std::clamp(lines, 1, 8);
	const float input_height = lines * line_height + frame_padding.y * 2.0f;
	const float tools_height = ES(30);
	const float composer_target = composer_pad + input_height + ES(8) + tools_height + composer_pad - ES(2);
	const float composer_height = std::floor(approach(ImGui::GetID("##hauteur_saisie"), composer_target, 18.0f) + 0.5f);
	const bool show_hint = avail.y > ES(420);
	const float footer_height = composer_height + ES(12) + (show_hint ? line_height + ES(6) : ES(2));

	// --- Conversation ----------------------------------------------------------------------
	const float body_top = origin.y + header_height + 1.0f;
	const float body_height = std::max(ES(40), avail.y - header_height - 1.0f - footer_height);
	ImGui::SetCursorScreenPos(ImVec2(origin.x, body_top));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, ES(3)));
	if (ImGui::BeginChild("##conversation", ImVec2(main_width, body_height), ImGuiChildFlags_None)) {
		ImGui::Dummy(ImVec2(1, ES(6)));
		ImGui::Indent(column_x);
		if (show_settings) {
			// Carte « Connexion ».
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ES(12));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ES(14), ES(12)));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ES(8), ES(6)));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ES(7));
			ImGui::PushStyleColor(ImGuiCol_ChildBg, col(mix(L.frame, L.bg_light, 0.4f)));
			ImGui::PushStyleColor(ImGuiCol_Border, col(L.border));
			if (ImGui::BeginChild("##connexion_carte", ImVec2(column, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders)) {
				ImGui::TextColored(col(L.accent), ICON_SETTINGS "  Connexion");
				ImGui::SameLine(ImGui::GetContentRegionAvail().x - ES(22) + ImGui::GetStyle().WindowPadding.x);
				if (icon_button("##fermer", ICON_CLOSE, "Fermer", ES(22))) {
					show_settings = false;
				}
				draw_connection_settings();
				ImGui::BeginDisabled(animations_forced_off);
				if (ImGui::Checkbox("Animations", &animations)) {
					save_settings();
				}
				ImGui::EndDisabled();
				ImGui::SetItemTooltip("Apparition des messages, texte qui s'écrit, survols et défilement en douceur.");
			}
			ImGui::EndChild();
			ImGui::PopStyleColor(2);
			ImGui::PopStyleVar(4);
			ImGui::Dummy(ImVec2(column, ES(8)));
		}
		if (messages.empty()) {
			_draw_welcome(body_height - (show_settings ? ES(40) : 0.0f));
		} else {
			welcome_born = -1.0;
		}
		for (size_t i = 0; i < messages.size(); i++) {
			_draw_message(messages[i], (int)i);
		}
		if (state != IDLE && !messages.empty()) {
			// Travail en cours : emblème, état, trois points qui ondulent.
			ImGui::Dummy(ImVec2(column, ES(8)));
			ImDrawList *body = ImGui::GetWindowDrawList();
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			const float radius = ES(9);
			const float y = pos.y + radius + ES(2);
			draw_orb(body, ImVec2(pos.x + radius, y), radius, true);
			const String label = status.empty() ? String("MEX Agent travaille") : status;
			const float wave = L.animate ? 0.65f + 0.35f * (float)std::sin(L.now * 2.4) : 1.0f;
			float x = pos.x + radius * 2.0f + ES(10);
			body->AddText(ImVec2(x, y - line_height * 0.5f), U(L.text_dim, wave), label.c_str());
			x += ImGui::CalcTextSize(label.c_str()).x + ES(8);
			for (int i = 0; i < 3; i++) {
				const float phase = L.animate ? (float)std::sin(L.now * 6.0 - i * 0.9) : 0.0f;
				body->AddCircleFilled(ImVec2(x + i * ES(8), y + ES(1) - std::max(0.0f, phase) * ES(4)), ES(2.2f), U(L.accent, 0.55f + 0.45f * std::max(0.0f, phase)), 10);
			}
			ImGui::Dummy(ImVec2(column, radius * 2.0f + ES(6)));
		}
		ImGui::Dummy(ImVec2(column, ES(14)));
		ImGui::Unindent(column_x);

		// Défilement : la vue suit la fin de la conversation tant que l'utilisateur ne remonte pas.
		ImGuiWindow *window = ImGui::GetCurrentWindow();
		const float maximum = ImGui::GetScrollMaxY(), current = ImGui::GetScrollY();
		const bool dragging_bar = ImGui::GetActiveID() == ImGui::GetWindowScrollbarID(window, ImGuiAxis_Y);
		if (dragging_bar || (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::GetIO().MouseWheel > 0.0f)) {
			stick_bottom = false;
		} else if (current >= maximum - ES(3)) {
			stick_bottom = true;
		}
		if (scroll_to_end > 0) {
			stick_bottom = true;
			scroll_to_end--;
		}
		if (messages.empty()) {
			stick_bottom = false; // écran d'accueil : il se lit depuis le haut
		}
		if (stick_bottom && current < maximum) {
			const float step = L.animate ? std::max((maximum - current) * std::min(1.0f, L.dt * 11.0f), std::min(maximum - current, ES(2))) : maximum - current;
			ImGui::SetScrollY(std::min(maximum, current + step));
		}
		// Retour en bas.
		const float away = approach(ImGui::GetID("##loin_du_bas"), (!messages.empty() && !stick_bottom && maximum - current > ES(80)) ? 1.0f : 0.0f, 10.0f);
		if (away > 0.02f) {
			const float size = ES(32);
			const ImVec2 pos(window->Pos.x + (main_width - size) * 0.5f, window->Pos.y + body_height - size - ES(10) + ES(10) * (1.0f - away));
			ImGui::SetCursorScreenPos(pos);
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * away);
			const bool pressed = ImGui::InvisibleButton("##bas", ImVec2(size, size));
			const float hover = approach(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.0f : 0.0f);
			ImDrawList *body = ImGui::GetWindowDrawList();
			const ImVec2 middle(pos.x + size * 0.5f, pos.y + size * 0.5f);
			body->AddCircleFilled(ImVec2(middle.x, middle.y + ES(2)), size * 0.5f, U(Color(0, 0, 0, 1), 0.25f), 32);
			body->AddCircleFilled(middle, size * 0.5f, U(mix(L.bg_light, L.accent, 0.25f * hover)), 32);
			body->AddCircle(middle, size * 0.5f, U(L.border), 32);
			const ImVec2 icon = ImGui::CalcTextSize(ICON_CHEVRON_DOWN);
			body->AddText(ImVec2(middle.x - icon.x * 0.5f, middle.y - icon.y * 0.5f), U(L.text), ICON_CHEVRON_DOWN);
			ImGui::SetItemTooltip("Aller à la fin de la conversation");
			ImGui::PopStyleVar();
			if (pressed) {
				stick_bottom = true;
			}
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleVar(2);

	// --- Volet des changements -------------------------------------------------------------
	if (side_panel) {
		ImGui::SetCursorScreenPos(ImVec2(origin.x + main_width, body_top));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ES(14), ES(12)));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, col(mix(L.bg, L.bg_dark, 0.5f)));
		if (ImGui::BeginChild("##changements", ImVec2(side_width, avail.y - header_height - 1.0f), ImGuiChildFlags_AlwaysUseWindowPadding)) {
			ImDrawList *side = ImGui::GetWindowDrawList();
			const ImVec2 corner = ImGui::GetWindowPos();
			side->AddLine(corner, ImVec2(corner.x, corner.y + ImGui::GetWindowHeight()), U(L.border, 0.6f));
			ImGui::TextColored(col(L.text), ICON_FILE_TEXT "  Changements");
			ImGui::SameLine();
			ImGui::TextDisabled("%d", (int)originals.size());
			ImGui::Spacing();
			const String root = ProjectSettings::get_singleton()->get_resource_path();
			int index = 0;
			for (const auto &entry : originals) {
				ImGui::PushID(index++);
				String shown_path = entry.first;
				if (!root.empty() && str_begins_with(shown_path, root)) {
					shown_path = shown_path.substr(root.size());
					while (!shown_path.empty() && (shown_path[0] == '/' || shown_path[0] == '\\')) {
						shown_path = shown_path.substr(1);
					}
				}
				const float height = ES(26);
				const ImVec2 pos = ImGui::GetCursorScreenPos();
				const float width = ImGui::GetContentRegionAvail().x;
				if (ImGui::InvisibleButton("##fichier", ImVec2(width, height))) {
					const String path = entry.first;
					EditorNode *target = editor;
					leave_agent_mode(); // le fichier s'ouvre dans l'éditeur
					target->_defer([target, path]() { target->_open_file(path); });
				}
				const float hover = approach(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.0f : 0.0f);
				if (hover > 0.01f) {
					side->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), U(L.text, 0.08f * hover), ES(6));
				}
				const Color mark = entry.second.first ? L.warning : L.success;
				side->AddCircleFilled(ImVec2(pos.x + ES(9), pos.y + height * 0.5f), ES(3.5f), U(mark), 12);
				side->PushClipRect(ImVec2(pos.x + ES(20), pos.y), ImVec2(pos.x + width - ES(4), pos.y + height), true);
				side->AddText(ImVec2(pos.x + ES(20), pos.y + (height - line_height) * 0.5f), U(mix(L.text_dim, L.text, 0.5f + 0.5f * hover)), shown_path.c_str());
				side->PopClipRect();
				ImGui::SetItemTooltip("%s\n%s. Cliquer pour l'ouvrir.", shown_path.c_str(), entry.second.first ? "Fichier modifié" : "Fichier créé");
				ImGui::PopID();
			}
			ImGui::Spacing();
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ES(8));
			ImGui::BeginDisabled(state != IDLE);
			if (ImGui::Button(ICON_UNDO "  Tout annuler", ImVec2(-FLT_MIN, 0))) {
				_revert_changes();
			}
			ImGui::EndDisabled();
			ImGui::PopStyleVar();
			ImGui::SetItemTooltip("Remettre ces fichiers dans leur état d'avant cette conversation.");
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
	}

	// --- Zone de saisie --------------------------------------------------------------------
	const ImVec2 composer(origin.x + column_x, body_top + body_height + ES(4));
	const ImVec2 composer_max(composer.x + column, composer.y + composer_height);
	const bool can_send = state == IDLE && !str_strip(prompt).empty();
	const float focus = approach(ImGui::GetID("##focus_saisie"), input_active ? 1.0f : 0.0f, 12.0f);
	if (focus > 0.01f) {
		dl->AddRect(ImVec2(composer.x - ES(2), composer.y - ES(2)), ImVec2(composer_max.x + ES(2), composer_max.y + ES(2)), U(L.accent, 0.22f * focus), ES(16), 0, ES(3));
	}
	dl->AddRectFilled(composer, composer_max, U(mix(L.frame, L.bg_light, 0.35f)), ES(14));
	dl->AddRect(composer, composer_max, U(mix(L.border, L.accent, focus * 0.9f)), ES(14), 0, 1.0f);

	ImGui::SetCursorScreenPos(ImVec2(composer.x + composer_pad, composer.y + composer_pad - ES(2)));
	if (want_focus || focus_input) {
		ImGui::SetKeyboardFocusHere();
		want_focus = false;
		focus_input = false;
	}
	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, frame_padding);
	const ImVec2 input_pos = ImGui::GetCursorScreenPos();
	bool send = ImGui::InputTextMultiline("##demande", &prompt, ImVec2(inner_width, input_height), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine | ImGuiInputTextFlags_WordWrap);
	input_active = ImGui::IsItemActive();
	if (input_active && ImGui::IsKeyPressed(ImGuiKey_Enter) && ImGui::GetIO().KeyShift) {
		send = false;
	}
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(2);
	if (prompt.empty()) {
		dl->PushClipRect(input_pos, ImVec2(input_pos.x + inner_width, input_pos.y + input_height), true);
		dl->AddText(ImVec2(input_pos.x + frame_padding.x, input_pos.y + frame_padding.y), U(L.text_dim, 0.8f), messages.empty() ? (inner_width > ES(330) ? "Décrivez ce que vous voulez créer ou corriger..." : "Décrivez votre idée...") : "Répondre à MEX Agent...");
		dl->PopClipRect();
	}

	// Rangée d'outils : modèle, accord, envoi.
	const float tools_y = composer_max.y - composer_pad - tools_height + ES(2);
	const float send_size = ES(30);
	ImGui::SetCursorScreenPos(ImVec2(composer.x + composer_pad - ES(2), tools_y + (tools_height - ES(26)) * 0.5f));
	const float chips_width = column - composer_pad * 2.0f - send_size - ES(10);
	const String model_label = model.empty() ? String(provider.label) : model;
	const float approval_width = ImGui::CalcTextSize(ask_before_changes ? "Demander" : "Automatique").x + ImGui::CalcTextSize(ICON_LOCK).x + ES(26);
	const bool room_for_approval = chips_width > approval_width + ES(90);
	if (chip("##modele", ICON_SPARKLES, model_label.c_str(), (String(provider.label) + "\nCliquer pour changer de service ou de modèle.").c_str(), std::max(ES(60), chips_width - (room_for_approval ? approval_width + ES(6) : 0.0f)))) {
		ImGui::OpenPopup("##choix_modele");
	}
	if (room_for_approval) {
		ImGui::SameLine(0.0f, ES(6));
		if (chip("##accord_mode", ask_before_changes ? ICON_LOCK : ICON_ROCKET, ask_before_changes ? "Demander" : "Automatique", ask_before_changes ? "MEX Agent attend votre accord avant chaque modification du projet.\nCliquer pour le laisser modifier sans attendre." : "MEX Agent modifie le projet sans attendre votre accord.\n« Annuler les changements » reste disponible. Cliquer pour revenir à l'accord.", 0.0f, !ask_before_changes)) {
			ask_before_changes = !ask_before_changes;
			save_settings();
		}
	}
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ES(10), ES(10)));
	ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, ES(10));
	if (ImGui::BeginPopup("##choix_modele")) {
		ImGui::TextDisabled("Service");
		for (int i = 0; i < get_provider_count(); i++) {
			if (ImGui::Selectable(get_provider(i).label, i == provider_index)) {
				provider_index = i;
				save_settings();
			}
		}
		const ProviderSettings &current = _settings();
		if (!current.models.empty()) {
			ImGui::Separator();
			ImGui::TextDisabled("Modèle");
			if (ImGui::BeginChild("##modeles", ImVec2(ES(300), std::min((float)current.models.size(), 8.0f) * ImGui::GetTextLineHeightWithSpacing() + ES(4)))) {
				for (const String &name : current.models) {
					if (ImGui::Selectable(name.c_str(), name == model)) {
						_settings().model = name;
						save_settings();
					}
				}
			}
			ImGui::EndChild();
		}
		ImGui::Separator();
		if (ImGui::Selectable(ICON_SETTINGS "  Connexion...")) {
			show_settings = true;
		}
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(2);

	// Envoi (ou arrêt pendant le travail) : bouton rond.
	{
		const ImVec2 pos(composer_max.x - composer_pad - send_size + ES(2), tools_y + (tools_height - send_size) * 0.5f);
		ImGui::SetCursorScreenPos(pos);
		const bool busy = state != IDLE;
		ImGui::BeginDisabled(!busy && !can_send);
		const bool pressed = ImGui::InvisibleButton("##envoyer", ImVec2(send_size, send_size));
		ImGui::EndDisabled();
		const float hover = approach(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.0f : 0.0f);
		const float ready = approach(ImGui::GetID("##pret"), can_send ? 1.0f : 0.0f, 12.0f);
		const ImVec2 middle(pos.x + send_size * 0.5f, pos.y + send_size * 0.5f);
		const float radius = send_size * 0.5f * (1.0f + 0.07f * hover);
		if (busy) {
			dl->AddCircleFilled(middle, radius, U(L.text, 0.12f + 0.06f * hover), 32);
			draw_spinner(dl, middle, radius - ES(1), U(L.accent), std::max(1.5f, ES(2)));
			const float half = ES(4.5f);
			dl->AddRectFilled(ImVec2(middle.x - half, middle.y - half), ImVec2(middle.x + half, middle.y + half), U(L.text), ES(1.5f));
			ImGui::SetItemTooltip("Arrêter");
			if (pressed) {
				stop();
			}
			send = false;
		} else {
			dl->AddCircleFilled(middle, radius, U(mix(mix(L.frame, L.text, 0.18f), L.accent, ready)), 32);
			const ImVec2 icon = ImGui::CalcTextSize(ICON_ARROW_UP);
			dl->AddText(ImVec2(std::floor(middle.x - icon.x * 0.5f), std::floor(middle.y - icon.y * 0.5f)), U(mix(L.text_dim, L.on_accent, ready)), ICON_ARROW_UP);
			ImGui::SetItemTooltip("Envoyer (Entrée)");
			if (pressed) {
				send = true;
			}
		}
	}
	if (show_hint) {
		const char *hint = "Entrée : envoyer  ·  Ctrl+Entrée : nouvelle ligne";
		const ImVec2 size = ImGui::CalcTextSize(hint);
		if (size.x <= column) {
			dl->AddText(ImVec2(std::floor(composer.x + (column - size.x) * 0.5f), composer_max.y + ES(6)), U(L.text_dim, 0.7f), hint);
		}
	}
	if (send && can_send) {
		const String text = prompt;
		prompt.clear();
		stick_bottom = true;
		submit(text);
		focus_input = true;
	}
	ImGui::PopStyleVar();
	ImGui::End();
}
