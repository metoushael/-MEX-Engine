#include "editor_node.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/object/script_language.h"
#include "core/os/os.h"
#include "core/string/ustring.h"
#include "main/mex_logo.gen.h"
#include "scene/main/canvas_item.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/font.h"
#include "scene/resources/packed_scene.h"
#include "servers/display_server.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

#include <SDL.h>

#include <algorithm>
#include <set>

static void _redraw_recursive(Node *p_node) {
	if (CanvasItem *ci = Object::cast_to<CanvasItem>(p_node)) {
		ci->queue_redraw();
	}
	for (Node *child : p_node->get_children_vector()) {
		_redraw_recursive(child);
	}
}

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------
EditorNode::EditorNode() {
}

EditorNode::~EditorNode() {
}

void EditorNode::_setup_imgui() {
	DisplayServer *display = DisplayServer::get_singleton();
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigWindowsMoveFromTitleBarOnly = true;
	io.ConfigDragClickToInputText = true;

	// La disposition des panneaux est mémorisée dans le dossier de préférences de l'utilisateur.
	if (char *pref = SDL_GetPrefPath("Metoushela", "MEX")) {
		ini_path = String(pref) + "editor_layout.ini";
		SDL_free(pref);
		io.IniFilename = ini_path.c_str();
	} else {
		io.IniFilename = nullptr;
	}

	unsigned int font_size = 0;
	const unsigned char *font_data = Font::get_embedded_data(font_size);
	ImFontConfig cfg;
	cfg.FontDataOwnedByAtlas = false;
	font_ui = io.Fonts->AddFontFromMemoryTTF((void *)font_data, (int)font_size, 16.0f, &cfg);
	font_mono = io.Fonts->AddFontDefaultVector();

	_apply_style();

	ImGui_ImplSDL2_InitForSDLRenderer(display->get_window(), display->get_renderer());
	ImGui_ImplSDLRenderer2_Init(display->get_renderer());
}

void EditorNode::_apply_style() {
	ImGui::StyleColorsDark();
	ImGuiStyle &style = ImGui::GetStyle();
	style.WindowRounding = 4.0f;
	style.ChildRounding = 4.0f;
	style.FrameRounding = 3.0f;
	style.PopupRounding = 4.0f;
	style.GrabRounding = 3.0f;
	style.TabRounding = 4.0f;
	style.ScrollbarRounding = 6.0f;
	style.WindowPadding = ImVec2(8, 8);
	style.FramePadding = ImVec2(7, 4);
	style.ItemSpacing = ImVec2(7, 5);
	style.IndentSpacing = 16.0f;
	style.WindowBorderSize = 1.0f;
	style.FrameBorderSize = 0.0f;
	style.WindowMenuButtonPosition = ImGuiDir_None;

	ImVec4 *c = style.Colors;
	// Couleurs de la marque (voir branding/CHARTE.md) : fonds tirés de l'Indigo nuit,
	// accent Orange MEX.
	const ImVec4 bg(0.102f, 0.110f, 0.188f, 1.0f);
	const ImVec4 bg_dark(0.071f, 0.075f, 0.145f, 1.0f);
	const ImVec4 bg_light(0.165f, 0.176f, 0.290f, 1.0f);
	const ImVec4 frame(0.055f, 0.059f, 0.122f, 1.0f);
	const ImVec4 border(0.043f, 0.047f, 0.098f, 1.0f);
	const ImVec4 accent(1.0f, 0.478f, 0.0f, 1.0f);
	const ImVec4 accent_dim(1.0f, 0.478f, 0.0f, 0.42f);
	c[ImGuiCol_Text] = ImVec4(0.89f, 0.90f, 0.94f, 1.0f);
	c[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.52f, 0.66f, 1.0f);
	c[ImGuiCol_WindowBg] = bg;
	c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_PopupBg] = ImVec4(0.086f, 0.090f, 0.165f, 0.98f);
	c[ImGuiCol_Border] = border;
	c[ImGuiCol_FrameBg] = frame;
	c[ImGuiCol_FrameBgHovered] = ImVec4(0.110f, 0.118f, 0.220f, 1.0f);
	c[ImGuiCol_FrameBgActive] = ImVec4(0.150f, 0.160f, 0.290f, 1.0f);
	c[ImGuiCol_TitleBg] = bg_dark;
	c[ImGuiCol_TitleBgActive] = bg_dark;
	c[ImGuiCol_TitleBgCollapsed] = bg_dark;
	c[ImGuiCol_MenuBarBg] = bg_dark;
	c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0.12f);
	c[ImGuiCol_ScrollbarGrab] = ImVec4(0.27f, 0.29f, 0.43f, 1.0f);
	c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.37f, 0.53f, 1.0f);
	c[ImGuiCol_ScrollbarGrabActive] = accent;
	c[ImGuiCol_CheckMark] = accent;
	c[ImGuiCol_SliderGrab] = accent;
	c[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.64f, 0.10f, 1.0f);
	c[ImGuiCol_Button] = bg_light;
	c[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.24f, 0.38f, 1.0f);
	c[ImGuiCol_ButtonActive] = accent_dim;
	c[ImGuiCol_Header] = ImVec4(0.18f, 0.19f, 0.32f, 1.0f);
	c[ImGuiCol_HeaderHovered] = ImVec4(0.23f, 0.25f, 0.40f, 1.0f);
	c[ImGuiCol_HeaderActive] = accent_dim;
	c[ImGuiCol_Separator] = border;
	c[ImGuiCol_SeparatorHovered] = accent_dim;
	c[ImGuiCol_SeparatorActive] = accent;
	c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_ResizeGripHovered] = accent_dim;
	c[ImGuiCol_ResizeGripActive] = accent;
	c[ImGuiCol_Tab] = bg_dark;
	c[ImGuiCol_TabHovered] = ImVec4(0.21f, 0.23f, 0.37f, 1.0f);
	c[ImGuiCol_TabSelected] = bg;
	c[ImGuiCol_TabSelectedOverline] = accent;
	c[ImGuiCol_TabDimmed] = bg_dark;
	c[ImGuiCol_TabDimmedSelected] = bg;
	c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_DockingPreview] = accent_dim;
	c[ImGuiCol_DockingEmptyBg] = bg_dark;
	c[ImGuiCol_TableHeaderBg] = bg_dark;
	c[ImGuiCol_TableBorderStrong] = border;
	c[ImGuiCol_TableBorderLight] = ImVec4(0.09f, 0.10f, 0.18f, 1.0f);
	c[ImGuiCol_TextSelectedBg] = accent_dim;
	c[ImGuiCol_DragDropTarget] = accent;
	c[ImGuiCol_NavCursor] = accent;
	c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.5f);
}

void EditorNode::_shutdown_imgui() {
	ImGui_ImplSDLRenderer2_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
}

// ---------------------------------------------------------------------------
// Boucle principale
// ---------------------------------------------------------------------------
int EditorNode::run(bool p_has_project, const String &p_scene, int p_quit_after, const String &p_screenshot) {
	DisplayServer *display = DisplayServer::get_singleton();
	RenderingServer *rs = RenderingServer::get_singleton();
	OS *os = OS::get_singleton();
	SDL_Renderer *renderer = display->get_renderer();

	// Dans l'éditeur, les scripts ne s'exécutent pas : seules leurs variables exportées sont éditables.
	ScriptServer::set_scripting_enabled(false);
	Engine::get_singleton()->set_editor_hint(true);

	_setup_imgui();
	logo.instantiate();
	if (logo->load_from_memory(MEX_ICON_PNG, MEX_ICON_PNG_SIZE) != OK) {
		logo.unref();
	}
	print_handler_id = add_print_handler([this](PrintLevel p_level, const String &p_text) {
		if (!console_lines.empty() && console_lines.back().level == p_level && console_lines.back().text == p_text) {
			console_lines.back().count++;
		} else {
			console_lines.push_back({ p_level, p_text, 1 });
			if (console_lines.size() > 2000) {
				console_lines.erase(console_lines.begin(), console_lines.begin() + 500);
			}
		}
		console_scroll = true;
	});

	if (p_has_project) {
		project_loaded = true;
		_project_opened();
		if (!p_scene.empty()) {
			_open_scene(str_begins_with(p_scene, "res://") ? p_scene : "res://" + p_scene);
		}
	}

	_load_test_script();
	uint64_t last_ticks = os->get_ticks_usec();
	int frame_count = 0;
	bool running = true;

	while (running) {
		const uint64_t frame_start = os->get_ticks_usec();
		_run_test_commands(frame_count, false);
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			_process_event(event);
		}
		if ((SDL_GetWindowFlags(display->get_window()) & SDL_WINDOW_MINIMIZED) && p_quit_after <= 0) {
			SDL_Delay(40);
			last_ticks = os->get_ticks_usec();
			continue;
		}

		const uint64_t now = os->get_ticks_usec();
		frame_delta = std::min((double)(now - last_ticks) / 1000000.0, 0.25);
		last_ticks = now;
		if (p_quit_after > 0) {
			frame_delta = 1.0 / 60.0;
		}

		ImGui_ImplSDLRenderer2_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		_draw_ui();
		_commit_undo_if_idle();

		// --- Simulation du jeu en cours ---
		if (game.is_running() && !game_paused) {
			Engine::get_singleton()->set_editor_hint(false);
			const bool keep_running = game.iterate(frame_delta);
			Engine::get_singleton()->set_editor_hint(true);
			if (!keep_running) {
				_defer([this]() { _stop(); });
			}
		}

		// --- Rendu des vues dans leurs textures ---
		rs->reset_stats();
		if (project_loaded) {
			SceneTree::flush_delete_queue();
			_render_edit_view();
			if (game.is_running() && game_target.is_valid()) {
				rs->set_render_target(game_target);
				game.draw();
			}
		}
		rs->set_render_target(RID());

		ImGui::Render();
		ImGuiIO &io = ImGui::GetIO();
		SDL_RenderSetScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
		SDL_SetRenderDrawColor(renderer, 18, 19, 37, 255);
		SDL_RenderClear(renderer);
		ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);

		_run_test_commands(frame_count, true);
		frame_count++;
		const bool last_frame = quit_requested || (p_quit_after > 0 && frame_count >= p_quit_after);
		if (last_frame && !p_screenshot.empty()) {
			if (display->save_screenshot(p_screenshot) == OK) {
				print_line("Capture enregistrée : " + p_screenshot);
			}
		}
		display->present();

		// Actions différées (ouverture de scène, suppression de nœuds...) : hors du parcours de l'interface.
		if (!deferred.empty()) {
			std::vector<std::function<void()>> actions;
			actions.swap(deferred);
			for (auto &action : actions) {
				if (action) {
					action();
				}
			}
		}

		if (last_frame) {
			running = false;
		}
		const uint64_t frame_usec = os->get_ticks_usec() - frame_start;
		if (p_quit_after <= 0 && frame_usec < 4000) {
			os->delay_msec(window_focused ? 4 : 30);
		}
	}

	// --- Arrêt ---
	game.stop();
	if (scene_root) {
		memdelete(scene_root);
		scene_root = nullptr;
	}
	if (edit_tree) {
		memdelete(edit_tree);
		edit_tree = nullptr;
	}
	if (edit_target.is_valid()) {
		rs->free_rid(edit_target);
	}
	if (view3d_viewport.is_valid() && RenderingServer3D::get_singleton()) {
		RenderingServer3D::get_singleton()->free_rid(view3d_viewport);
	}
	if (game_target.is_valid()) {
		rs->free_rid(game_target);
	}
	logo.unref();
	remove_print_handler(print_handler_id);
	_shutdown_imgui();
	ScriptServer::set_scripting_enabled(true);
	return 0;
}

// ---------------------------------------------------------------------------
// Tests automatiques : rejoue des entrées depuis un fichier texte.
// Chaque ligne : <image> <commande> [arguments]
//   move x y | down [bouton] | up [bouton] | click [bouton] | wheel dy
//   key <touche SDL> [ctrl] [shift] | text <chaîne> | shot <fichier.bmp>
// ---------------------------------------------------------------------------
void EditorNode::_load_test_script() {
	const char *path = SDL_getenv("MEX_EDITOR_TEST_SCRIPT");
	if (!path || !*path) {
		return;
	}
	SDL_RWops *file = SDL_RWFromFile(path, "rb");
	if (!file) {
		print_error(String("Scénario de test introuvable : ") + path);
		return;
	}
	String text;
	char buffer[1024];
	size_t read = 0;
	while ((read = SDL_RWread(file, buffer, 1, sizeof(buffer))) > 0) {
		text.append(buffer, read);
	}
	SDL_RWclose(file);
	for (const String &raw : str_split(text, "\n", false)) {
		const String line = str_strip(raw);
		if (line.empty() || line[0] == '#') {
			continue;
		}
		std::vector<String> parts = str_split(line, " ", false);
		if (parts.size() < 2) {
			continue;
		}
		TestCommand command;
		command.frame = atoi(parts[0].c_str());
		command.name = parts[1];
		command.args.assign(parts.begin() + 2, parts.end());
		test_commands.push_back(command);
	}
}

void EditorNode::_run_test_commands(int p_frame, bool p_after_render) {
	if (test_commands.empty()) {
		return;
	}
	static int mouse_x = 0, mouse_y = 0;
	const Uint32 window_id = SDL_GetWindowID(DisplayServer::get_singleton()->get_window());
	for (const TestCommand &command : test_commands) {
		if (command.frame != p_frame) {
			continue;
		}
		const std::vector<String> &args = command.args;
		if (command.name == "shot") {
			if (p_after_render && !args.empty()) {
				DisplayServer::get_singleton()->save_screenshot(args[0]);
			}
			continue;
		}
		if (p_after_render) {
			continue;
		}
		auto push_button = [&](bool p_down, int p_button) {
			SDL_Event ev;
			SDL_zero(ev);
			ev.type = p_down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
			ev.button.windowID = window_id;
			ev.button.button = (Uint8)p_button;
			ev.button.state = p_down ? SDL_PRESSED : SDL_RELEASED;
			ev.button.clicks = 1;
			ev.button.x = mouse_x;
			ev.button.y = mouse_y;
			SDL_PushEvent(&ev);
		};
		const int button = args.empty() ? 1 : atoi(args[0].c_str());
		if (command.name == "move" && args.size() >= 2) {
			SDL_Event ev;
			SDL_zero(ev);
			ev.type = SDL_MOUSEMOTION;
			ev.motion.windowID = window_id;
			ev.motion.xrel = atoi(args[0].c_str()) - mouse_x;
			ev.motion.yrel = atoi(args[1].c_str()) - mouse_y;
			mouse_x = atoi(args[0].c_str());
			mouse_y = atoi(args[1].c_str());
			ev.motion.x = mouse_x;
			ev.motion.y = mouse_y;
			SDL_PushEvent(&ev);
		} else if (command.name == "down") {
			push_button(true, button);
		} else if (command.name == "up") {
			push_button(false, button);
		} else if (command.name == "click") {
			push_button(true, button);
			push_button(false, button);
		} else if (command.name == "wheel" && !args.empty()) {
			SDL_Event ev;
			SDL_zero(ev);
			ev.type = SDL_MOUSEWHEEL;
			ev.wheel.windowID = window_id;
			ev.wheel.y = atoi(args[0].c_str());
			ev.wheel.preciseY = (float)atof(args[0].c_str());
			ev.wheel.mouseX = mouse_x;
			ev.wheel.mouseY = mouse_y;
			SDL_PushEvent(&ev);
		} else if ((command.name == "key" || command.name == "keydown" || command.name == "keyup") && !args.empty()) {
			Uint16 mod = KMOD_NONE;
			for (size_t i = 1; i < args.size(); i++) {
				if (args[i] == "ctrl") {
					mod |= KMOD_LCTRL;
				} else if (args[i] == "shift") {
					mod |= KMOD_LSHIFT;
				}
			}
			String key_name = args[0];
			std::replace(key_name.begin(), key_name.end(), '_', ' ');
			const SDL_Keycode keycode = SDL_GetKeyFromName(key_name.c_str());
			auto push_key = [&](bool p_down, SDL_Keycode p_key, Uint16 p_mod) {
				SDL_Event ev;
				SDL_zero(ev);
				ev.type = p_down ? SDL_KEYDOWN : SDL_KEYUP;
				ev.key.windowID = window_id;
				ev.key.state = p_down ? SDL_PRESSED : SDL_RELEASED;
				ev.key.keysym.sym = p_key;
				ev.key.keysym.scancode = SDL_GetScancodeFromKey(p_key);
				ev.key.keysym.mod = p_mod;
				SDL_PushEvent(&ev);
			};
			const bool down = command.name != "keyup";
			const bool up = command.name != "keydown";
			if (down) {
				if (mod & KMOD_LCTRL) {
					push_key(true, SDLK_LCTRL, mod);
				}
				if (mod & KMOD_LSHIFT) {
					push_key(true, SDLK_LSHIFT, mod);
				}
				push_key(true, keycode, mod);
			}
			if (up) {
				push_key(false, keycode, mod);
				if (mod & KMOD_LSHIFT) {
					push_key(false, SDLK_LSHIFT, KMOD_NONE);
				}
				if (mod & KMOD_LCTRL) {
					push_key(false, SDLK_LCTRL, KMOD_NONE);
				}
			}
		} else if (command.name == "text" && !args.empty()) {
			SDL_Event ev;
			SDL_zero(ev);
			ev.type = SDL_TEXTINPUT;
			ev.text.windowID = window_id;
			const String joined = str_join(args, " ");
			SDL_strlcpy(ev.text.text, joined.c_str(), sizeof(ev.text.text));
			SDL_PushEvent(&ev);
		}
	}
}

void EditorNode::_process_event(const SDL_Event &p_event) {
	ImGui_ImplSDL2_ProcessEvent(&p_event);
	DisplayServer *display = DisplayServer::get_singleton();

	if (p_event.type == SDL_QUIT) {
		_request_quit();
		return;
	}
	if (p_event.type == SDL_WINDOWEVENT) {
		switch (p_event.window.event) {
			case SDL_WINDOWEVENT_CLOSE:
				if (p_event.window.windowID == SDL_GetWindowID(display->get_window())) {
					_request_quit();
				}
				break;
			case SDL_WINDOWEVENT_FOCUS_GAINED:
				window_focused = true;
				fs_dirty = true;
				_defer([this]() { _check_external_changes(); });
				break;
			case SDL_WINDOWEVENT_FOCUS_LOST:
				window_focused = false;
				Input::get_singleton()->release_all();
				break;
			default:
				break;
		}
		return;
	}

	// Capture d'une touche pour la configuration des contrôles.
	if (!capture_action.empty()) {
		if (p_event.type == SDL_KEYDOWN && !p_event.key.repeat) {
			if (p_event.key.keysym.sym != SDLK_ESCAPE) {
				Ref<InputEvent> ev = DisplayServer::translate_event(p_event);
				if (ev.is_valid() && ev->get_input_code() != 0) {
					InputMap::get_singleton()->action_add_code(capture_action, ev->get_input_code());
					_save_input_map();
				}
			}
			capture_action.clear();
		}
		return;
	}

	_forward_game_event(p_event);
}

void EditorNode::_request_quit() {
	if (!project_loaded) {
		quit_requested = true;
		return;
	}
	_check_unsaved_then([this]() {
		if (script_dirty) {
			_confirm("Script non enregistré", "Le script « " + script_path + " » a été modifié.", {
																								   { "Enregistrer", [this]() {
																										_save_script();
																										quit_requested = true;
																									} },
																								   { "Ne pas enregistrer", [this]() { quit_requested = true; } },
																								   { "Annuler", nullptr },
																						   });
		} else {
			quit_requested = true;
		}
	});
}

void EditorNode::_update_window_title() {
	String title = "MEX - Metoushela Engine X";
	if (project_loaded) {
		title = GLOBAL_GET("application/name").as_string() + " - ";
		title += scene_path.empty() ? String("[scène non enregistrée]") : path_get_file(scene_path);
		if (scene_modified) {
			title += " (*)";
		}
		title += " - MEX";
	}
	DisplayServer::get_singleton()->window_set_title(title);
}

// ---------------------------------------------------------------------------
// Interface
// ---------------------------------------------------------------------------
// En-tête de marque : icône, mot « MEX » (le X en Orange MEX), nom complet et version.
void EditorNode::_draw_brand_header(float p_logo_size) {
	const ImVec2 start = ImGui::GetCursorScreenPos();
	float text_x = start.x;
	if (logo.is_valid()) {
		ImTextureID id = (ImTextureID)(intptr_t)RenderingServer::get_singleton()->texture_get_native(logo->get_rid());
		ImGui::GetWindowDrawList()->AddImage(id, start, ImVec2(start.x + p_logo_size, start.y + p_logo_size));
		text_x += p_logo_size + 14.0f;
	}
	const float word_size = p_logo_size * 0.56f;
	const float line_gap = 2.0f;
	const float block_height = word_size + line_gap + ImGui::GetFontSize();
	ImGui::SetCursorScreenPos(ImVec2(text_x, start.y + (p_logo_size - block_height) * 0.5f));
	ImGui::BeginGroup();
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, line_gap));
	ImGui::PushFont(nullptr, word_size);
	ImGui::TextUnformatted("ME");
	ImGui::SameLine(0, 0);
	ImGui::TextColored(ImVec4(1.0f, 0.478f, 0.0f, 1.0f), "X");
	ImGui::PopFont();
	ImGui::TextDisabled("Metoushela Engine X  ·  version %s", MEX_VERSION);
	ImGui::PopStyleVar();
	ImGui::EndGroup();
	ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + p_logo_size));
	ImGui::Dummy(ImVec2(0, 4));
}

void EditorNode::_draw_ui() {
	if (!project_loaded) {
		_draw_project_manager();
		_draw_dialogs();
		return;
	}

	_handle_shortcuts();
	_draw_main_menu();

	const ImGuiID dock_id = ImGui::GetID("MexDockSpace");
	if (!layout_built) {
		layout_built = true;
		if (ImGui::DockBuilderGetNode(dock_id) == nullptr) {
			_build_default_layout(dock_id);
		}
	}
	ImGui::DockSpaceOverViewport(dock_id, ImGui::GetMainViewport());

	if (fs_dirty) {
		_scan_filesystem();
	}

	_draw_scene_dock();
	_draw_filesystem_dock();
	_draw_inspector_dock();
	_draw_console_dock();
	_draw_animation_dock();
	_draw_script_dock();
	_draw_game_dock();
	_draw_viewport_dock();
	_draw_export_dialog();
	_draw_dialogs();
}

void EditorNode::_build_default_layout(unsigned int p_dockspace_id) {
	ImGuiID center = p_dockspace_id;
	ImGui::DockBuilderRemoveNode(center);
	ImGui::DockBuilderAddNode(center, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(center, ImGui::GetMainViewport()->WorkSize);
	ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.19f, nullptr, &center);
	ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.27f, nullptr, &center);
	ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, nullptr, &center);
	ImGuiID left_bottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.42f, nullptr, &left);
	ImGui::DockBuilderDockWindow("Scène###SceneDock", left);
	ImGui::DockBuilderDockWindow("Fichiers###FilesDock", left_bottom);
	ImGui::DockBuilderDockWindow("Inspecteur###Inspector", right);
	// Le dernier onglet ancré est celui affiché au départ : la sortie.
	ImGui::DockBuilderDockWindow("Animation###Animation", bottom);
	ImGui::DockBuilderDockWindow("Sortie###Console", bottom);
	ImGui::DockBuilderDockWindow("Vue 2D###Viewport", center);
	ImGui::DockBuilderDockWindow("Jeu###Game", center);
	ImGui::DockBuilderDockWindow("Script###Script", center);
	ImGui::DockBuilderFinish(p_dockspace_id);
	focus_console_frames = 3;
	focus_viewport = true;
}

void EditorNode::_handle_shortcuts() {
	const ImGuiInputFlags global = ImGuiInputFlags_RouteGlobal;
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, global)) {
		if (script_dirty) {
			_save_script();
		}
		if (scene_root && (scene_modified || scene_path.empty())) {
			_save_scene_or_ask();
		}
	}
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, global)) {
		_defer([this]() { _undo(); });
	}
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, global) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, global)) {
		_defer([this]() { _redo(); });
	}
	if (ImGui::Shortcut(ImGuiKey_F5, global)) {
		_defer([this]() { _play_main(); });
	}
	if (ImGui::Shortcut(ImGuiKey_F6, global)) {
		_defer([this]() { _play_current(); });
	}
	if (ImGui::Shortcut(ImGuiKey_F8, global)) {
		_defer([this]() { _stop(); });
	}
}

void EditorNode::_draw_main_menu() {
	if (!ImGui::BeginMainMenuBar()) {
		return;
	}
	if (ImGui::BeginMenu("Fichier")) {
		if (ImGui::MenuItem("Nouvelle scène")) {
			_check_unsaved_then([this]() { _new_scene(); });
		}
		if (ImGui::MenuItem("Ouvrir une scène...")) {
			_check_unsaved_then([this]() {
				_open_file_dialog("Ouvrir une scène", { "scn" }, false, [this](const String &p_path) { _open_scene(p_path); });
			});
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Enregistrer la scène", "Ctrl+S", false, scene_root != nullptr)) {
			_save_scene_or_ask();
		}
		if (ImGui::MenuItem("Enregistrer la scène sous...", nullptr, false, scene_root != nullptr)) {
			_open_file_dialog("Enregistrer la scène sous", { "scn" }, true, [this](const String &p_path) { _save_scene(p_path); }, scene_path);
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Quitter")) {
			_request_quit();
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Édition")) {
		if (ImGui::MenuItem("Annuler", "Ctrl+Z", false, !undo_stack.empty() || action_dirty)) {
			_defer([this]() { _undo(); });
		}
		if (ImGui::MenuItem("Rétablir", "Ctrl+Y", false, !redo_stack.empty())) {
			_defer([this]() { _redo(); });
		}
		ImGui::Separator();
		Node *selected = _get_selected();
		if (ImGui::MenuItem("Dupliquer le nœud", "Ctrl+D", false, selected && selected != scene_root)) {
			_defer([this, selected]() { _duplicate_node(selected); });
		}
		if (ImGui::MenuItem("Supprimer le nœud", "Suppr", false, selected != nullptr)) {
			_defer([this, selected]() { _delete_node(selected); });
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Projet")) {
		if (ImGui::MenuItem("Paramètres du projet...")) {
			settings_open = true;
		}
		if (ImGui::MenuItem("Définir la scène courante comme scène principale", nullptr, false, !scene_path.empty())) {
			ProjectSettings::get_singleton()->set_setting("application/main_scene", Variant(scene_path));
			ProjectSettings::get_singleton()->save();
			print_line("Scène principale : " + scene_path);
		}
		if (ImGui::MenuItem("Exporter le jeu...")) {
			export_open = true;
			export_message.clear();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Actualiser les fichiers")) {
			fs_dirty = true;
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Affichage")) {
		ImGui::MenuItem("Grille", nullptr, &show_grid);
		ImGui::MenuItem("Aimanter à la grille", nullptr, &snap_enabled);
		if (ImGui::MenuItem("Formes de collision", nullptr, &show_collision_shapes)) {
			if (edit_tree) {
				edit_tree->set_debug_collisions_hint(show_collision_shapes);
				if (scene_root) {
					_redraw_recursive(scene_root);
				}
			}
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Recentrer la vue", "F")) {
			_focus_selection();
		}
		if (ImGui::MenuItem("Réinitialiser la disposition")) {
			_defer([this]() { layout_built = false; ImGui::DockBuilderRemoveNode(ImGui::GetID("MexDockSpace")); });
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Aide")) {
		if (ImGui::MenuItem("À propos de MEX")) {
			about_open = true;
		}
		ImGui::EndMenu();
	}

	_draw_toolbar();
	ImGui::EndMainMenuBar();
}

void EditorNode::_draw_toolbar() {
	const bool playing = game.is_running();
	const ImGuiStyle &style = ImGui::GetStyle();
	const char *labels[4] = { "Jouer (F5)", "Jouer la scène (F6)", playing && game_paused ? "Reprendre" : "Pause", "Arrêter (F8)" };
	float width = 0;
	for (const char *label : labels) {
		width += ImGui::CalcTextSize(label).x + style.FramePadding.x * 2 + style.ItemSpacing.x;
	}
	const float start = (ImGui::GetWindowWidth() - width) * 0.5f;
	if (start > ImGui::GetCursorPosX() + 20) {
		ImGui::SetCursorPosX(start);
	} else {
		ImGui::Separator();
	}

	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.42f, 0.25f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.52f, 0.31f, 1.0f));
	if (ImGui::Button(labels[0])) {
		_defer([this]() { _play_main(); });
	}
	ImGui::PopStyleColor(2);
	if (ImGui::Button(labels[1])) {
		_defer([this]() { _play_current(); });
	}
	ImGui::BeginDisabled(!playing);
	if (ImGui::Button(labels[2])) {
		game_paused = !game_paused;
	}
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.20f, 0.20f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.62f, 0.25f, 0.25f, 1.0f));
	if (ImGui::Button(labels[3])) {
		_defer([this]() { _stop(); });
	}
	ImGui::PopStyleColor(2);
	ImGui::EndDisabled();

	// Indicateur à droite.
	const String info = vformat("%.0f i/s", ImGui::GetIO().Framerate);
	const float info_width = ImGui::CalcTextSize(info.c_str()).x + 12;
	if (ImGui::GetWindowWidth() - info_width > ImGui::GetCursorPosX()) {
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - info_width);
		ImGui::TextDisabled("%s", info.c_str());
	}
}

// ---------------------------------------------------------------------------
// Gestionnaire de projets
// ---------------------------------------------------------------------------
static bool _is_project_dir(const String &p_dir) {
	return FileAccess::exists(path_join(p_dir, ProjectSettings::PROJECT_FILE));
}

void EditorNode::_draw_project_manager() {
	if (!pm_scanned) {
		pm_scanned = true;
		std::vector<String> roots = { ".", "projects", "../projects", "../../projects" };
		if (char *base = SDL_GetBasePath()) {
			roots.push_back(path_join(base, "projects"));
			roots.push_back(path_join(base, "../projects"));
			roots.push_back(path_join(base, "../../projects"));
			SDL_free(base);
		}
		std::set<String> seen;
		for (const String &root : roots) {
			if (!FileAccess::dir_exists(root)) {
				continue;
			}
			std::vector<String> candidates = { root };
			std::vector<String> dirs;
			FileAccess::list_dir(root, nullptr, &dirs);
			for (const String &d : dirs) {
				candidates.push_back(path_join(root, d));
			}
			for (const String &candidate : candidates) {
				if (!_is_project_dir(candidate)) {
					continue;
				}
				const String simple = path_simplify(candidate);
				if (seen.insert(path_get_file(simple) + "|" + FileAccess::get_file_as_string(path_join(candidate, ProjectSettings::PROJECT_FILE))).second) {
					pm_found.push_back(simple);
				}
			}
			if (pm_new_parent.empty() && path_get_file(path_simplify(root)) == "projects") {
				pm_new_parent = path_simplify(root);
			}
		}
		if (pm_new_parent.empty()) {
			pm_new_parent = "projects";
		}
	}

	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(640, 0));
	ImGui::Begin("Gestionnaire de projets###ProjectManager", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	_draw_brand_header(64.0f);

	ImGui::SeparatorText("Projets trouvés");
	if (pm_found.empty()) {
		ImGui::TextDisabled("Aucun projet trouvé à proximité.");
	}
	for (const String &path : pm_found) {
		ImGui::PushID(path.c_str());
		if (ImGui::Button("Ouvrir")) {
			const String target = path;
			_defer([this, target]() { _open_project(target); });
		}
		ImGui::SameLine();
		ImGui::TextUnformatted(path.c_str());
		ImGui::PopID();
	}

	ImGui::SeparatorText("Ouvrir un projet existant");
	ImGui::SetNextItemWidth(-110);
	ImGui::InputTextWithHint("##open_path", "Dossier contenant project.mex", &pm_open_path);
	ImGui::SameLine();
	if (ImGui::Button("Ouvrir##path", ImVec2(-FLT_MIN, 0))) {
		const String target = pm_open_path;
		_defer([this, target]() { _open_project(target); });
	}

	ImGui::SeparatorText("Créer un nouveau projet");
	ImGui::InputText("Nom", &pm_new_name);
	ImGui::InputText("Dossier parent", &pm_new_parent);
	if (ImGui::Button("Créer et ouvrir")) {
		_defer([this]() {
			const String name = str_strip(pm_new_name);
			if (name.empty()) {
				pm_error = "Le nom du projet est vide.";
				return;
			}
			const String dir = path_join(pm_new_parent, name);
			if (_is_project_dir(dir)) {
				pm_error = "Un projet existe déjà dans : " + dir;
				return;
			}
			if (FileAccess::make_dir_recursive(dir) != OK) {
				pm_error = "Impossible de créer le dossier : " + dir;
				return;
			}
			const String content = "[application]\n\nname = \"" + str_c_escape(name) + "\"\n";
			if (FileAccess::write_string(path_join(dir, ProjectSettings::PROJECT_FILE), content) != OK) {
				pm_error = "Impossible d'écrire le fichier du projet dans : " + dir;
				return;
			}
			_open_project(dir);
		});
	}
	if (!pm_error.empty()) {
		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
		ImGui::TextWrapped("%s", pm_error.c_str());
		ImGui::PopStyleColor();
	}
	ImGui::End();
}

bool EditorNode::_open_project(const String &p_path) {
	if (p_path.empty() || !_is_project_dir(p_path)) {
		pm_error = "Aucun fichier " + String(ProjectSettings::PROJECT_FILE) + " dans : " + p_path;
		return false;
	}
	if (ProjectSettings::get_singleton()->setup(p_path) != OK) {
		pm_error = "Impossible de charger le projet : " + p_path;
		return false;
	}
	pm_error.clear();
	project_loaded = true;
	_project_opened();
	return true;
}

void EditorNode::_apply_project_settings() {
	const String filter = GLOBAL_GET("rendering/texture_filter").as_string();
	RenderingServer::get_singleton()->set_default_texture_filter(filter == "nearest" ? RenderingServer::TEXTURE_FILTER_NEAREST : RenderingServer::TEXTURE_FILTER_LINEAR);
}

void EditorNode::_project_opened() {
	InputMap::get_singleton()->load_from_project_settings();
	_apply_project_settings();
	if (!edit_tree) {
		edit_tree = memnew(SceneTree(true));
		edit_tree->set_debug_collisions_hint(show_collision_shapes);
	}
	const Size2 size((real_t)GLOBAL_GET("display/width").as_int(), (real_t)GLOBAL_GET("display/height").as_int());
	edit_tree->set_viewport_size(size);
	fs_dirty = true;
	print_line("Projet ouvert : " + ProjectSettings::get_singleton()->get_resource_path());

	const String main_scene = GLOBAL_GET("application/main_scene").as_string();
	if (main_scene.empty() || !FileAccess::exists(main_scene) || !_open_scene(main_scene)) {
		_new_scene();
	}
	_update_window_title();
}

// ---------------------------------------------------------------------------
// Scène éditée
// ---------------------------------------------------------------------------
Node *EditorNode::_get_selected() const {
	return Object::cast_to<Node>(ObjectDB::get_instance(selected_id));
}

void EditorNode::_select(Node *p_node) {
	selected_id = p_node ? p_node->get_instance_id() : 0;
	tile_paint = false;
	text_edit_buffers.clear();
}

void EditorNode::_mark_modified() {
	action_dirty = true;
	if (!scene_modified) {
		scene_modified = true;
		_update_window_title();
	}
}

bool EditorNode::_is_editable(const Node *p_node) const {
	return p_node && (p_node == scene_root || p_node->get_owner() == scene_root);
}

void EditorNode::_set_scene_root(Node *p_root, const String &p_path) {
	anim_backup.clear();
	anim_playing = false;
	anim_target_id = 0;
	anim_target_path.clear();
	anim_current.clear();
	anim_selected_key = -1;
	if (scene_root) {
		memdelete(scene_root);
		scene_root = nullptr;
	}
	scene_root = p_root;
	scene_path = p_path;
	if (scene_root) {
		edit_tree->get_root()->add_child(scene_root);
	}
	_select(scene_root);
	scene_modified = false;
	rename_id = 0;
	_reset_undo();
	_update_window_title();
}

void EditorNode::_new_scene() {
	_set_scene_root(nullptr, "");
}

bool EditorNode::_open_scene(const String &p_path) {
	Ref<PackedScene> packed = ResourceLoader::load(p_path, nullptr, true);
	if (packed.is_null()) {
		print_error("Impossible d'ouvrir la scène : " + p_path);
		return false;
	}
	Node *root = nullptr;
	{
		ScriptingDisabledScope no_scripts;
		root = packed->instantiate();
	}
	if (!root) {
		print_error("La scène est vide ou invalide : " + p_path);
		return false;
	}
	root->set_scene_file_path(p_path);
	_set_scene_root(root, p_path);
	_focus_selection();
	focus_viewport = true;
	return true;
}

bool EditorNode::_save_scene(const String &p_path) {
	if (!scene_root) {
		print_warning("Rien à enregistrer : la scène est vide.");
		return false;
	}
	_anim_end_preview();
	String path = p_path;
	if (!str_begins_with(path, "res://")) {
		path = "res://" + path;
	}
	if (path_get_extension(path) != "scn") {
		path += ".scn";
	}
	Ref<PackedScene> packed;
	packed.instantiate();
	if (packed->pack(scene_root) != OK) {
		print_error("Impossible de sérialiser la scène.");
		return false;
	}
	FileAccess::make_dir_recursive(path_get_base_dir(path));
	if (ResourceSaver::save(path, packed) != OK) {
		return false;
	}
	// Une version déjà chargée (scène instanciée ailleurs, jeu en cours) est mise à jour.
	Ref<PackedScene> cached = ResourceCache::get(path);
	if (cached.is_valid()) {
		cached->load_from_text(packed->save_to_text(), path);
	}
	scene_path = path;
	scene_root->set_scene_file_path(path);
	scene_modified = false;
	fs_dirty = true;
	_update_window_title();
	print_line("Scène enregistrée : " + path);
	return true;
}

void EditorNode::_save_scene_or_ask() {
	if (!scene_root) {
		return;
	}
	if (scene_path.empty()) {
		_open_file_dialog("Enregistrer la scène sous", { "scn" }, true, [this](const String &p_path) { _save_scene(p_path); }, "res://" + str_to_lower(scene_root->get_name()) + ".scn");
	} else {
		_save_scene(scene_path);
	}
}

void EditorNode::_check_unsaved_then(std::function<void()> p_next) {
	if (!scene_modified || !scene_root) {
		p_next();
		return;
	}
	const String name = scene_path.empty() ? String("La scène courante") : "La scène « " + path_get_file(scene_path) + " »";
	_confirm("Modifications non enregistrées", name + " a été modifiée. Voulez-vous l'enregistrer ?", {
																											  { "Enregistrer", [this, p_next]() {
																												   if (scene_path.empty()) {
																													   _open_file_dialog("Enregistrer la scène sous", { "scn" }, true, [this, p_next](const String &p_path) {
																														   if (_save_scene(p_path)) {
																															   p_next();
																														   }
																													   });
																												   } else if (_save_scene(scene_path)) {
																													   p_next();
																												   }
																											   } },
																											  { "Ne pas enregistrer", p_next },
																											  { "Annuler", nullptr },
																									  });
}

Node *EditorNode::_create_node(const String &p_type, Node *p_parent) {
	Node *node = Object::cast_to<Node>(ClassDB::instantiate(p_type));
	if (!node) {
		print_error("Impossible de créer un nœud de type " + p_type + ".");
		return nullptr;
	}
	node->set_name(p_type);
	if (!scene_root) {
		scene_root = node;
		edit_tree->get_root()->add_child(node);
	} else {
		Node *parent = p_parent ? p_parent : scene_root;
		parent->add_child(node);
		node->set_owner(scene_root);
	}
	_select(node);
	_mark_modified();
	return node;
}

Node *EditorNode::_instance_scene(const String &p_path, Node *p_parent) {
	if (!scene_root) {
		print_warning("Créez d'abord un nœud racine avant d'instancier une scène.");
		return nullptr;
	}
	if (p_path == scene_path) {
		print_error("Une scène ne peut pas s'instancier elle-même.");
		return nullptr;
	}
	Ref<PackedScene> packed = ResourceLoader::load(p_path);
	if (packed.is_null()) {
		return nullptr;
	}
	Node *node = nullptr;
	{
		ScriptingDisabledScope no_scripts;
		node = packed->instantiate();
	}
	if (!node) {
		return nullptr;
	}
	node->set_scene_file_path(p_path);
	Node *parent = p_parent ? p_parent : scene_root;
	parent->add_child(node);
	node->set_owner(scene_root);
	_select(node);
	_mark_modified();
	return node;
}

void EditorNode::_delete_node(Node *p_node) {
	if (!p_node || !ObjectDB::get_instance(p_node->get_instance_id())) {
		return;
	}
	Node *parent = p_node->get_parent();
	const bool was_root = p_node == scene_root;
	memdelete(p_node);
	if (was_root) {
		scene_root = nullptr;
		_select(nullptr);
	} else {
		_select(parent);
	}
	_mark_modified();
}

void EditorNode::_duplicate_node(Node *p_node) {
	if (!p_node || p_node == scene_root || !p_node->get_parent()) {
		return;
	}
	Node *copy = nullptr;
	{
		ScriptingDisabledScope no_scripts;
		copy = p_node->duplicate();
	}
	if (!copy) {
		return;
	}
	Node *parent = p_node->get_parent();
	parent->add_child(copy);
	parent->move_child(copy, p_node->get_index() + 1);
	copy->set_owner(scene_root);
	_select(copy);
	_mark_modified();
}

String EditorNode::_make_script_template(const String &p_base) const {
	String code = "extends " + p_base + "\n\n";
	code += "# Appelée une fois, quand le nœud et ses enfants sont entrés dans la scène.\n";
	code += "func _ready():\n\tpass\n\n";
	code += "# Appelée à chaque image ; delta est le temps écoulé (en secondes) depuis l'image précédente.\n";
	code += "func _process(delta):\n\tpass\n";
	return code;
}

void EditorNode::_attach_script(Node *p_node, const String &p_path) {
	if (!p_node) {
		return;
	}
	String path = p_path;
	if (!str_begins_with(path, "res://")) {
		path = "res://" + path;
	}
	if (path_get_extension(path) != "mx") {
		path += ".mx";
	}
	if (!FileAccess::exists(path)) {
		FileAccess::make_dir_recursive(path_get_base_dir(path));
		if (FileAccess::write_string(path, _make_script_template(p_node->get_class())) != OK) {
			print_error("Impossible de créer le script : " + path);
			return;
		}
		fs_dirty = true;
	}
	Ref<Resource> script = ResourceLoader::load(path);
	if (script.is_null()) {
		return;
	}
	{
		ScriptingDisabledScope no_scripts;
		p_node->set("script", Variant(script));
	}
	_mark_modified();
	_open_script(path);
}

// ---------------------------------------------------------------------------
// Annuler / rétablir
// ---------------------------------------------------------------------------
String EditorNode::_serialize_scene() const {
	if (!scene_root) {
		return String();
	}
	// L'aperçu d'animation ne fait pas partie de la scène : on sérialise les valeurs d'origine.
	_anim_swap_backup();
	Ref<PackedScene> packed;
	packed.instantiate();
	String text;
	if (packed->pack(scene_root) == OK) {
		text = packed->save_to_text();
	}
	_anim_swap_backup();
	return text;
}

void EditorNode::_restore_scene(const String &p_state) {
	const bool was_painting = tile_paint;
	anim_backup.clear(); // l'état restauré contient déjà les valeurs d'origine
	anim_playing = false;
	String selected_path;
	if (Node *selected = _get_selected()) {
		if (scene_root && (selected == scene_root || scene_root->is_ancestor_of(selected))) {
			selected_path = scene_root->get_path_to(selected);
		}
	}
	if (scene_root) {
		memdelete(scene_root);
		scene_root = nullptr;
	}
	selected_id = 0;
	rename_id = 0;
	if (!p_state.empty()) {
		Ref<PackedScene> packed;
		packed.instantiate();
		if (packed->load_from_text(p_state, scene_path) == OK) {
			ScriptingDisabledScope no_scripts;
			scene_root = packed->instantiate();
		}
		if (scene_root) {
			scene_root->set_scene_file_path(scene_path);
			edit_tree->get_root()->add_child(scene_root);
			Node *selected = selected_path.empty() ? nullptr : scene_root->get_node_or_null(selected_path);
			_select(selected ? selected : scene_root);
			// Annuler un coup de pinceau ne quitte pas le mode « Peindre les tuiles ».
			tile_paint = was_painting && selected && selected->is_class("TileMap");
		}
	}
}

void EditorNode::_reset_undo() {
	undo_stack.clear();
	redo_stack.clear();
	action_dirty = false;
	undo_base = _serialize_scene();
}

void EditorNode::_commit_undo_if_idle() {
	if (!action_dirty || !project_loaded) {
		return;
	}
	if (ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right) || drag != DRAG_NONE) {
		return; // l'action est encore en cours (glissement, saisie...)
	}
	action_dirty = false;
	const String now = _serialize_scene();
	if (now == undo_base) {
		return;
	}
	undo_stack.push_back(undo_base);
	if (undo_stack.size() > 100) {
		undo_stack.erase(undo_stack.begin());
	}
	redo_stack.clear();
	undo_base = now;
}

void EditorNode::_undo() {
	if (action_dirty) {
		// Valide d'abord l'action en cours pour pouvoir la défaire.
		action_dirty = false;
		const String now = _serialize_scene();
		if (now != undo_base) {
			undo_stack.push_back(undo_base);
			redo_stack.clear();
			undo_base = now;
		}
	}
	if (undo_stack.empty()) {
		return;
	}
	redo_stack.push_back(undo_base);
	const String state = undo_stack.back();
	undo_stack.pop_back();
	_restore_scene(state);
	undo_base = state;
	scene_modified = true;
	_update_window_title();
}

void EditorNode::_redo() {
	if (redo_stack.empty() || action_dirty) {
		return;
	}
	undo_stack.push_back(undo_base);
	const String state = redo_stack.back();
	redo_stack.pop_back();
	_restore_scene(state);
	undo_base = state;
	scene_modified = true;
	_update_window_title();
}

// ---------------------------------------------------------------------------
// Mode « Jouer »
// ---------------------------------------------------------------------------
void EditorNode::_play(const String &p_scene) {
	if (game.is_running()) {
		_stop();
	}
	if (script_dirty) {
		_save_script();
	}
	if (scene_root && scene_modified && !scene_path.empty()) {
		_save_scene(scene_path);
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	const Size2 size((real_t)GLOBAL_GET("display/width").as_int(), (real_t)GLOBAL_GET("display/height").as_int());
	if (!game_target.is_valid() || game_size != size) {
		if (game_target.is_valid()) {
			rs->free_rid(game_target);
		}
		game_target = rs->texture_create_target((int)size.x, (int)size.y);
		game_size = size;
	}
	_apply_project_settings();
	InputMap::get_singleton()->load_from_project_settings();
	print_line("--- Lancement de " + p_scene + " ---");
	Engine::get_singleton()->set_editor_hint(false);
	const Error err = game.start(p_scene, size);
	Engine::get_singleton()->set_editor_hint(true);
	if (err != OK) {
		return;
	}
	game_paused = false;
	focus_game = true;
}

void EditorNode::_play_main() {
	const String main_scene = GLOBAL_GET("application/main_scene").as_string();
	if (!main_scene.empty() && FileAccess::exists(main_scene)) {
		_play(main_scene);
		return;
	}
	if (scene_path.empty()) {
		_confirm("Aucune scène principale", "Le projet n'a pas encore de scène principale. Enregistrez d'abord la scène courante, puis relancez.", { { "OK", nullptr } });
		return;
	}
	_confirm("Aucune scène principale", "Le projet n'a pas de scène principale. Utiliser « " + scene_path + " » ?", {
																															   { "Utiliser cette scène", [this]() {
																																	ProjectSettings::get_singleton()->set_setting("application/main_scene", Variant(scene_path));
																																	ProjectSettings::get_singleton()->save();
																																	_play(scene_path);
																																} },
																															   { "Annuler", nullptr },
																													   });
}

void EditorNode::_play_current() {
	if (!scene_root) {
		return;
	}
	if (scene_path.empty()) {
		_open_file_dialog("Enregistrer la scène avant de la lancer", { "scn" }, true, [this](const String &p_path) {
			if (_save_scene(p_path)) {
				_play(scene_path);
			}
		}, "res://" + str_to_lower(scene_root->get_name()) + ".scn");
		return;
	}
	_play(scene_path);
}

void EditorNode::_stop() {
	if (!game.is_running()) {
		return;
	}
	game.stop();
	game_paused = false;
	focus_viewport = true;
	print_line("--- Jeu arrêté ---");
}

void EditorNode::_forward_game_event(const SDL_Event &p_event) {
	if (!game.is_running() || game_paused) {
		return;
	}
	switch (p_event.type) {
		case SDL_KEYDOWN:
			if (!game_view_focused) {
				return;
			}
			if (p_event.key.keysym.sym == SDLK_F5 || p_event.key.keysym.sym == SDLK_F6 || p_event.key.keysym.sym == SDLK_F8) {
				return;
			}
			break;
		case SDL_TEXTINPUT:
			if (!game_view_focused) {
				return;
			}
			break;
		case SDL_KEYUP:
			break; // toujours transmis : évite les touches « coincées »
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEWHEEL:
		case SDL_MOUSEMOTION:
			if (!game_view_hovered) {
				return;
			}
			break;
		case SDL_MOUSEBUTTONUP:
			break;
		default:
			return;
	}
	Ref<InputEvent> ev = DisplayServer::translate_event(p_event);
	if (ev.is_null()) {
		return;
	}
	if (InputEventMouse *mouse = Object::cast_to<InputEventMouse>(ev.ptr())) {
		if (game_image_size.x <= 0 || game_image_size.y <= 0) {
			return;
		}
		const Vector2 ratio(game_size.x / game_image_size.x, game_size.y / game_image_size.y);
		mouse->set_position((mouse->get_position() - game_image_pos) * ratio);
		if (InputEventMouseMotion *motion = Object::cast_to<InputEventMouseMotion>(ev.ptr())) {
			motion->set_relative(motion->get_relative() * ratio);
		}
	}
	Engine::get_singleton()->set_editor_hint(false);
	game.push_input(ev);
	Engine::get_singleton()->set_editor_hint(true);
}
