// Logo de MEX dans l'éditeur. Le symbole et le mot MEX ne sont pas des images figées : leurs
// tracés, lus dans le pack de logo (branding/svg) par branding/generer_icones.py, sont rendus
// à la taille exacte d'affichage, puis teintés selon le fond comme le demande le guide du
// logo (branding/GUIDE.md) : vert vif et blanc cassé sur fond sombre, vert profond et noir
// sur fond clair.

#include "editor_node.h"

#include "editor_ui.h"
#include "mex_brand.gen.h"
#include "scene/resources/texture.h"
#include "servers/rendering_server.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

enum BrandShape {
	BRAND_SYMBOL, // dessin standard : fonds clairs
	BRAND_SYMBOL_DARK, // détails agrandis : fonds sombres
	BRAND_SYMBOL_SMALL, // croix et boutons plus gros : sous 32 px
	BRAND_WORDMARK,
	BRAND_SHAPE_COUNT,
};

const MexBrandShape *const BRAND_SHAPES[BRAND_SHAPE_COUNT] = {
	&MEX_BRAND_SYMBOL,
	&MEX_BRAND_SYMBOL_DARK,
	&MEX_BRAND_SYMBOL_SMALL,
	&MEX_BRAND_WORDMARK,
};

// En dessous de cette largeur en pixels, le guide demande le symbole « petites tailles ».
const int SMALL_SYMBOL_BELOW = 32;

ImU32 _brand_color(unsigned int p_rgb) {
	return IM_COL32((p_rgb >> 16) & 0xFF, (p_rgb >> 8) & 0xFF, p_rgb & 0xFF, 255);
}

// Remplit une forme dans une image de p_width x p_height pixels : blanc, l'alpha donnant la
// part de chaque pixel couverte par la forme. Balayage par sous-lignes ; sur chaque
// sous-ligne, la couverture horizontale est calculée exactement.
std::vector<uint8_t> _rasterize(const MexBrandShape &p_shape, int p_width, int p_height) {
	const int SUBLINES = 8;
	const float weight = 1.0f / SUBLINES;
	const float scale_x = p_width / p_shape.width;
	const float scale_y = p_height / p_shape.height;
	std::vector<float> coverage((size_t)p_width * p_height, 0.0f);

	struct Crossing {
		float x;
		int direction;
	};
	std::vector<Crossing> crossings;
	for (int line = 0; line < p_height * SUBLINES; line++) {
		const float y = (line + 0.5f) * weight;
		crossings.clear();
		const float *points = p_shape.points;
		for (int c = 0; c < p_shape.contour_count; c++) {
			const int count = p_shape.contours[c];
			for (int i = 0; i < count; i++) {
				const int j = (i + 1) % count;
				const float ax = points[i * 2] * scale_x, ay = points[i * 2 + 1] * scale_y;
				const float bx = points[j * 2] * scale_x, by = points[j * 2 + 1] * scale_y;
				if ((ay <= y) == (by <= y)) {
					continue; // l'arête ne coupe pas la sous-ligne
				}
				crossings.push_back({ ax + (y - ay) / (by - ay) * (bx - ax), by > ay ? 1 : -1 });
			}
			points += count * 2;
		}
		std::sort(crossings.begin(), crossings.end(), [](const Crossing &p_a, const Crossing &p_b) { return p_a.x < p_b.x; });

		float *row = coverage.data() + (size_t)(line / SUBLINES) * p_width;
		int winding = 0;
		for (size_t k = 0; k + 1 < crossings.size(); k++) {
			winding += crossings[k].direction;
			const bool inside = p_shape.even_odd ? ((k + 1) & 1) != 0 : winding != 0;
			if (!inside) {
				continue;
			}
			const float x0 = std::clamp(crossings[k].x, 0.0f, (float)p_width);
			const float x1 = std::clamp(crossings[k + 1].x, 0.0f, (float)p_width);
			if (x1 <= x0) {
				continue;
			}
			const int first = std::min((int)x0, p_width - 1);
			const int last = std::min((int)x1, p_width - 1);
			if (first == last) {
				row[first] += (x1 - x0) * weight;
				continue;
			}
			row[first] += (first + 1 - x0) * weight;
			for (int x = first + 1; x < last; x++) {
				row[x] += weight;
			}
			row[last] += (x1 - last) * weight;
		}
	}

	std::vector<uint8_t> rgba((size_t)p_width * p_height * 4, 255);
	for (size_t i = 0; i < coverage.size(); i++) {
		rgba[i * 4 + 3] = (uint8_t)(std::clamp(coverage[i], 0.0f, 1.0f) * 255.0f + 0.5f);
	}
	return rgba;
}

} // namespace

// Les images du logo sont gardées par taille. Les tailles ne changent qu'avec l'échelle de
// l'interface : la liste reste courte ; au-delà, elle est vidée en début d'image, quand plus
// aucun dessin en attente ne s'en sert.
void EditorNode::_collect_brand_images() {
	if (brand_images.size() > 24) {
		brand_images.clear();
	}
}

void EditorNode::_free_brand_images() {
	brand_images.clear();
}

// Dessine une forme du logo dans le rectangle donné, alignée sur les pixels de l'écran.
void EditorNode::_draw_brand_shape(ImDrawList *p_draw_list, int p_shape, const ImVec2 &p_pos, float p_width, float p_height, unsigned int p_rgb) {
	if (p_shape < 0 || p_shape >= BRAND_SHAPE_COUNT || p_width < 1.0f || p_height < 1.0f) {
		return;
	}
	// Écran à haute densité : l'image est rendue en pixels réels, pas en points.
	ImVec2 density = ImGui::GetIO().DisplayFramebufferScale;
	density.x = density.x > 0.0f ? density.x : 1.0f;
	density.y = density.y > 0.0f ? density.y : 1.0f;
	const int width = std::max((int)std::floor(p_width * density.x + 0.5f), 1);
	const int height = std::max((int)std::floor(p_height * density.y + 0.5f), 1);

	Ref<Texture2D> texture;
	for (const BrandImage &image : brand_images) {
		if (image.shape == p_shape && image.width == width && image.height == height) {
			texture = image.texture;
			break;
		}
	}
	if (texture.is_null()) {
		const std::vector<uint8_t> rgba = _rasterize(*BRAND_SHAPES[p_shape], width, height);
		texture = Texture2D::create_from_rgba(width, height, rgba.data(), RenderingServer::TEXTURE_FILTER_LINEAR);
		if (texture.is_null() || !texture->get_rid().is_valid()) {
			return;
		}
		brand_images.push_back({ p_shape, width, height, texture });
	}
	const ImVec2 pos(std::floor(p_pos.x * density.x + 0.5f) / density.x, std::floor(p_pos.y * density.y + 0.5f) / density.y);
	const ImTextureID id = (ImTextureID)(intptr_t)RenderingServer::get_singleton()->texture_get_native(texture->get_rid());
	p_draw_list->AddImage(id, pos, ImVec2(pos.x + width / density.x, pos.y + height / density.y), ImVec2(0, 0), ImVec2(1, 1), _brand_color(p_rgb));
}

// Symbole seul, de hauteur p_height ; renvoie sa largeur.
float EditorNode::_draw_brand_symbol(ImDrawList *p_draw_list, const ImVec2 &p_pos, float p_height) {
	const float width = std::floor(p_height * MEX_BRAND_SYMBOL.width / MEX_BRAND_SYMBOL.height + 0.5f);
	const float density = std::max(ImGui::GetIO().DisplayFramebufferScale.x, 1.0f);
	const int shape = width * density < SMALL_SYMBOL_BELOW ? BRAND_SYMBOL_SMALL : (palette.dark ? BRAND_SYMBOL_DARK : BRAND_SYMBOL);
	_draw_brand_shape(p_draw_list, shape, p_pos, width, p_height, palette.dark ? MEX_BRAND_GREEN : MEX_BRAND_DEEP_GREEN);
	return width;
}

// Logo horizontal (symbole et mot MEX), de hauteur p_height ; renvoie sa largeur. Les
// proportions et l'écart entre les deux parties sont ceux de mex-horizontal-*.svg.
float EditorNode::_draw_brand_lockup(ImDrawList *p_draw_list, const ImVec2 &p_pos, float p_height) {
	const float unit = p_height / MEX_BRAND_LOCKUP_HEIGHT;
	const float *symbol = MEX_BRAND_LOCKUP_SYMBOL;
	const float *wordmark = MEX_BRAND_LOCKUP_WORDMARK;
	const float density = std::max(ImGui::GetIO().DisplayFramebufferScale.x, 1.0f);
	const int shape = symbol[2] * unit * density < SMALL_SYMBOL_BELOW ? BRAND_SYMBOL_SMALL : (palette.dark ? BRAND_SYMBOL_DARK : BRAND_SYMBOL);
	_draw_brand_shape(p_draw_list, shape, ImVec2(p_pos.x + symbol[0] * unit, p_pos.y + symbol[1] * unit), symbol[2] * unit, symbol[3] * unit,
			palette.dark ? MEX_BRAND_GREEN : MEX_BRAND_DEEP_GREEN);
	_draw_brand_shape(p_draw_list, BRAND_WORDMARK, ImVec2(p_pos.x + wordmark[0] * unit, p_pos.y + wordmark[1] * unit), wordmark[2] * unit, wordmark[3] * unit,
			palette.dark ? MEX_BRAND_OFF_WHITE : MEX_BRAND_BLACK);
	return MEX_BRAND_LOCKUP_WIDTH * unit;
}

// En-tête de marque : le logo horizontal, le nom complet et la version. Occupe p_height en
// hauteur ; renvoie le bord droit du bloc, en coordonnées d'écran.
// - en ligne (gestionnaire de projets) : le nom complet est à droite du logo ;
// - empilé (« À propos ») : le nom complet est sous le logo.
// Dans les deux cas le texte reste hors de la zone de protection du logo, la moitié de la
// hauteur du M.
float EditorNode::_draw_brand_header(float p_height, bool p_stacked) {
	const ImVec2 start = ImGui::GetCursorScreenPos();
	ImDrawList *draw_list = ImGui::GetWindowDrawList();
	char subtitle[96];
	snprintf(subtitle, sizeof(subtitle), "Metoushela Engine X  ·  version %s", MEX_VERSION);
	const ImVec2 text_size = ImGui::CalcTextSize(subtitle);
	const ImU32 text_color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
	float right = start.x;
	if (p_stacked) {
		const float logo_height = std::max(std::floor((p_height - text_size.y) / 1.5f), 8.0f);
		const float logo_width = _draw_brand_lockup(draw_list, start, logo_height);
		draw_list->AddText(ImVec2(start.x, start.y + p_height - text_size.y), text_color, subtitle);
		right = start.x + std::max(logo_width, text_size.x);
	} else {
		const float logo_height = std::max(std::floor(p_height * 0.6f), 8.0f);
		const float logo_width = _draw_brand_lockup(draw_list, ImVec2(start.x, start.y + (p_height - logo_height) * 0.5f), logo_height);
		const float text_x = start.x + logo_width + std::ceil(logo_height * 0.5f) + ES(6);
		draw_list->AddText(ImVec2(text_x, start.y + std::floor((p_height - text_size.y) * 0.5f)), text_color, subtitle);
		right = text_x + text_size.x;
	}
	ImGui::Dummy(ImVec2(right - start.x, p_height));
	ImGui::Dummy(ImVec2(0, ES(4)));
	return right;
}
