// Dock « Moniteur » : flux de mesures en temps réel (durée d'image, coût du jeu, rendu,
// nombre de nœuds et d'objets), sous forme de courbes défilantes.
#include "editor_node.h"

#include "core/object/object.h"
#include "core/string/ustring.h"
#include "editor_ui.h"
#include "scene/main/scene_tree.h"
#include "servers/rendering_server.h"
#include "servers/rendering_server_3d.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>

void EditorNode::_monitor_push(int p_series, float p_value) {
	if (p_series < 0 || p_series >= MONITOR_COUNT || monitor_paused) {
		return;
	}
	MonitorSeries &series = monitors[p_series];
	series.values[series.head] = p_value;
	series.head = (series.head + 1) % MONITOR_SAMPLES;
	series.count = std::min(series.count + 1, (int)MONITOR_SAMPLES);
}

// Relevé de fin d'image. Les durées (simulation, rendu du jeu) sont mesurées dans la boucle
// principale et passées ici.
void EditorNode::_monitor_sample(double p_frame_usec, double p_game_process_usec, double p_game_draw_usec) {
	const bool playing = game.is_running();
	_monitor_push(MONITOR_FPS, p_frame_usec > 0.0 ? (float)(1000000.0 / p_frame_usec) : 0.0f);
	_monitor_push(MONITOR_FRAME_TIME, (float)(p_frame_usec / 1000.0));
	_monitor_push(MONITOR_GAME_PROCESS, playing ? (float)(p_game_process_usec / 1000.0) : 0.0f);
	_monitor_push(MONITOR_GAME_DRAW, playing ? (float)(p_game_draw_usec / 1000.0) : 0.0f);
	_monitor_push(MONITOR_ITEMS_2D, (float)monitor_items_2d);
	_monitor_push(MONITOR_DRAW_CALLS_3D, (float)monitor_draw_calls_3d);
	_monitor_push(MONITOR_TRIANGLES, (float)monitor_triangles);
	SceneTree *tree = playing ? game.get_tree() : edit_tree;
	_monitor_push(MONITOR_NODES, tree ? (float)tree->get_node_count() : 0.0f);
	_monitor_push(MONITOR_OBJECTS, (float)ObjectDB::get_object_count());
}

void EditorNode::_draw_monitor_dock() {
	using EditorUI::col;
	using EditorUI::u32;
	if (!ImGui::Begin(ICON_SIGNAL "  Moniteur###Monitor")) {
		ImGui::End();
		return;
	}
	struct SeriesInfo {
		const char *name;
		const char *unit;
		const char *tip;
		int decimals;
		int color; // 0 : succès, 1 : info, 2 : accent, 3 : avertissement
	};
	static const SeriesInfo infos[MONITOR_COUNT] = {
		{ "Images par seconde", "i/s", "Cadence d'affichage de l'éditeur (jeu compris quand il tourne)", 0, 0 },
		{ "Durée d'une image", "ms", "Temps total entre deux images", 1, 1 },
		{ "Jeu : simulation", "ms", "Scripts, physique et animations du jeu en cours", 2, 2 },
		{ "Jeu : rendu", "ms", "Envoi des dessins du jeu à la carte graphique", 2, 2 },
		{ "Éléments 2D dessinés", "", "Sprites, textes, formes : vue d'édition et jeu", 0, 3 },
		{ "Appels de dessin 3D", "", "Lots envoyés à la carte graphique pour la dernière vue 3D rendue", 0, 3 },
		{ "Triangles 3D", "", "Triangles de la dernière vue 3D rendue", 0, 3 },
		{ "Nœuds", "", "Nœuds du jeu en cours, sinon de la scène éditée", 0, 1 },
		{ "Objets en mémoire", "", "Nœuds et ressources vivants : une valeur qui monte sans cesse signale une fuite", 0, 0 },
	};
	const Color colors[4] = { palette.success, palette.info, palette.accent, palette.warning };

	if (_toggle_button(monitor_paused ? ICON_PLAY "  Reprendre" : ICON_PAUSE "  Figer", monitor_paused, "Arrêter ou reprendre le défilement des courbes")) {
		monitor_paused = !monitor_paused;
	}
	ImGui::SameLine();
	if (ImGui::Button(ICON_TRASH "  Effacer")) {
		for (MonitorSeries &series : monitors) {
			series = MonitorSeries();
		}
	}
	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled("%s", game.is_running() ? "Jeu en cours : les mesures « Jeu » sont actives." : "Lancez le jeu (F5) pour mesurer sa simulation et son rendu.");

	ImGui::BeginChild("##monitors");
	const ImGuiStyle &style = ImGui::GetStyle();
	const float avail = ImGui::GetContentRegionAvail().x;
	const float min_card = ES(250);
	const int columns = std::max(1, (int)((avail + style.ItemSpacing.x) / (min_card + style.ItemSpacing.x)));
	const float card_w = std::floor((avail - style.ItemSpacing.x * (float)(columns - 1)) / (float)columns);
	const float card_h = ES(104);
	ImDrawList *dl = ImGui::GetWindowDrawList();
	for (int index = 0; index < MONITOR_COUNT; index++) {
		const MonitorSeries &series = monitors[index];
		const SeriesInfo &info = infos[index];
		if (index % columns != 0) {
			ImGui::SameLine();
		}
		ImGui::PushID(index);
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(card_w, card_h));
		ImGui::SetItemTooltip("%s", info.tip);
		const ImVec2 end(pos.x + card_w, pos.y + card_h);
		dl->AddRectFilled(pos, end, u32(palette.frame), style.FrameRounding);

		// Valeur courante, minimum, moyenne, maximum.
		float current = 0.0f, low = 0.0f, high = 0.0f, sum = 0.0f;
		for (int i = 0; i < series.count; i++) {
			const float value = series.values[(series.head - series.count + i + MONITOR_SAMPLES) % MONITOR_SAMPLES];
			low = i == 0 ? value : std::min(low, value);
			high = i == 0 ? value : std::max(high, value);
			sum += value;
			current = value;
		}
		const float average = series.count > 0 ? sum / (float)series.count : 0.0f;
		const Color color = colors[info.color];
		const float pad = ES(8);
		dl->AddText(ImVec2(pos.x + pad, pos.y + ES(6)), u32(palette.text_dim), info.name);
		const String format = vformat("%%.%df %%s", info.decimals);
		const String value_text = vformat(format.c_str(), (double)current, info.unit);
		ImFont *font = ImGui::GetFont();
		const float big = ImGui::GetFontSize() * 1.35f;
		const ImVec2 value_size = font->CalcTextSizeA(big, FLT_MAX, 0.0f, value_text.c_str());
		dl->AddText(font, big, ImVec2(end.x - pad - value_size.x, pos.y + ES(4)), u32(color), value_text.c_str());

		// Courbe : l'échelle verticale part de zéro et s'adapte au maximum observé.
		const ImVec2 graph_min(pos.x + pad, pos.y + ES(34));
		const ImVec2 graph_max(end.x - pad, end.y - ES(22));
		const float graph_w = graph_max.x - graph_min.x, graph_h = graph_max.y - graph_min.y;
		const float top = std::max(high * 1.15f, 0.0001f);
		dl->AddLine(ImVec2(graph_min.x, graph_max.y), ImVec2(graph_max.x, graph_max.y), u32(palette.border));
		dl->AddLine(ImVec2(graph_min.x, graph_min.y + graph_h * 0.5f), ImVec2(graph_max.x, graph_min.y + graph_h * 0.5f), u32(palette.border, 0.45f));
		if (series.count >= 2) {
			const float step = graph_w / (float)(MONITOR_SAMPLES - 1);
			ImVec2 previous;
			for (int i = 0; i < series.count; i++) {
				const float value = series.values[(series.head - series.count + i + MONITOR_SAMPLES) % MONITOR_SAMPLES];
				const ImVec2 point(graph_max.x - (float)(series.count - 1 - i) * step, graph_max.y - std::clamp(value / top, 0.0f, 1.0f) * graph_h);
				if (i > 0) {
					// Aire sous la courbe, puis la courbe elle-même.
					dl->AddQuadFilled(ImVec2(previous.x, graph_max.y), previous, point, ImVec2(point.x, graph_max.y), u32(color, 0.16f));
					dl->AddLine(previous, point, u32(color), std::max(1.0f, ES(1.5f)));
				}
				previous = point;
			}
		}
		const String stats = vformat(vformat("min %%.%df   moy %%.%df   max %%.%df", info.decimals, info.decimals, info.decimals).c_str(), (double)low, (double)average, (double)high);
		dl->AddText(ImVec2(pos.x + pad, end.y - ES(19)), u32(palette.text_dim), stats.c_str());
		ImGui::PopID();
	}
	ImGui::EndChild();
	ImGui::End();
}
