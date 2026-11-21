#include "main.h"

#include "core/input/input_event.h"
#include "core/input/keyboard.h"
#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/io/file_access.h"
#include "core/io/pack.h"
#include "core/os/os.h"
#include "core/register_core_types.h"
#include "core/string/ustring.h"
#include "main/game_runner.h"
#include "modules/mexscript/mexscript.h"
#include "scene/main/scene_tree.h"
#include "main/mex_logo.gen.h"
#include "scene/register_scene_types.h"
#include "scene/resources/texture.h"
#include "servers/audio_server.h"
#include "servers/display_server.h"
#include "servers/physics_server_2d.h"
#include "servers/physics_server_3d.h"
#include "servers/rendering_server.h"
#include "servers/rendering_server_3d.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"
#endif

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <fstream>
#include <SDL.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <io.h>
#include <cstdio>
#include <string>
#include <vector>
#endif

namespace {

struct CommandLine {
	String project_path;
	String scene;
	bool editor = false;
	bool show_help = false;
	String main_pack;
	bool show_version = false;
	bool hidden = false;
	int quit_after = -1; // nombre d'images avant de quitter (tests)
	String screenshot;
};

void print_help() {
	print_line("MEX - Metoushela Engine X " MEX_VERSION);
	print_line("Utilisation : mex [options] [scene.scn]");
	print_line("");
	print_line("  -e, --editor          Ouvre l'éditeur.");
	print_line("  --path <dossier>      Dossier du projet (contenant project.mex).");
	print_line("  --main-pack <f.pck>   Lance le jeu contenu dans une archive exportée.");
	print_line("  --quit-after <n>      Quitte après n images.");
	print_line("  --screenshot <f.bmp>  Enregistre une capture de la dernière image.");
	print_line("  --hidden              N'affiche pas la fenêtre.");
	print_line("  --version             Affiche la version.");
	print_line("  -h, --help            Affiche cette aide.");
	print_line("");
	print_line("Sans option, lance la scène principale du projet du dossier courant ;");
	print_line("s'il n'y a pas de projet, ouvre l'éditeur (si disponible).");
}

bool parse_command_line(int p_argc, char **p_argv, CommandLine &r_cmd) {
	for (int i = 1; i < p_argc; i++) {
		const String arg = p_argv[i];
		auto next = [&](String &r_value) -> bool {
			if (i + 1 >= p_argc) {
				print_error("Valeur manquante après l'option " + arg + ".");
				return false;
			}
			r_value = p_argv[++i];
			return true;
		};
		if (arg == "-e" || arg == "--editor") {
			r_cmd.editor = true;
		} else if (arg == "-h" || arg == "--help") {
			r_cmd.show_help = true;
		} else if (arg == "--version") {
			r_cmd.show_version = true;
		} else if (arg == "--hidden") {
			r_cmd.hidden = true;
		} else if (arg == "--path") {
			if (!next(r_cmd.project_path)) {
				return false;
			}
		} else if (arg == "--main-pack") {
			if (!next(r_cmd.main_pack)) {
				return false;
			}
		} else if (arg == "--quit-after") {
			String value;
			if (!next(value)) {
				return false;
			}
			r_cmd.quit_after = atoi(value.c_str());
		} else if (arg == "--screenshot") {
			if (!next(r_cmd.screenshot)) {
				return false;
			}
		} else if (!arg.empty() && arg[0] == '-') {
			print_error("Option inconnue : " + arg);
			return false;
		} else {
			r_cmd.scene = arg;
		}
	}
	return true;
}

// Scénario d'entrées simulées pour les tests automatiques du jeu (variable MEX_TEST_SCRIPT).
// Une commande par ligne : « <image> <commande> [arguments] », coordonnées en pixels du jeu.
//   move x y | down [bouton] | up [bouton] | click [bouton]
//   keydown <touche> | keyup <touche> | key <touche> | text <texte>
//   shot <fichier.bmp>
struct GameTestCommand {
	int frame = 0;
	String name;
	std::vector<String> args;
	String rest;
};

std::vector<GameTestCommand> load_game_test_script() {
	std::vector<GameTestCommand> commands;
	const char *path = SDL_getenv("MEX_TEST_SCRIPT");
	if (!path || !*path) {
		return commands;
	}
	std::ifstream file(path);
	std::string line;
	while (std::getline(file, line)) {
		std::istringstream in(line);
		GameTestCommand command;
		if (!(in >> command.frame >> command.name) || command.name[0] == '#') {
			continue;
		}
		std::getline(in, command.rest);
		const size_t start = command.rest.find_first_not_of(' ');
		command.rest = start == std::string::npos ? "" : command.rest.substr(start);
		std::istringstream args(command.rest);
		std::string arg;
		while (args >> arg) {
			command.args.push_back(arg);
		}
		commands.push_back(command);
	}
	return commands;
}

void run_game_test_commands(const std::vector<GameTestCommand> &p_commands, int p_frame, bool p_after_render, GameRunner &p_runner, Vector2 &r_mouse) {
	for (const GameTestCommand &command : p_commands) {
		if (command.frame != p_frame) {
			continue;
		}
		const String &name = command.name;
		if (name == "shot") {
			if (p_after_render && !command.args.empty()) {
				DisplayServer::get_singleton()->save_screenshot(command.args[0]);
			}
			continue;
		}
		if (p_after_render) {
			continue;
		}
		auto mouse_button = [&](bool p_pressed) {
			Ref<InputEventMouseButton> ev;
			ev.instantiate();
			ev->set_position(r_mouse);
			ev->set_button_index(command.args.empty() ? 1 : std::atoi(command.args[0].c_str()));
			ev->set_pressed(p_pressed);
			p_runner.push_input(ev);
		};
		auto key = [&](bool p_pressed) {
			if (command.args.empty()) {
				return;
			}
			Ref<InputEventKey> ev;
			ev.instantiate();
			ev->set_keycode(input_code_from_name(command.args[0]));
			ev->set_pressed(p_pressed);
			p_runner.push_input(ev);
		};
		if (name == "move" && command.args.size() >= 2) {
			const Vector2 pos((real_t)std::atof(command.args[0].c_str()), (real_t)std::atof(command.args[1].c_str()));
			Ref<InputEventMouseMotion> ev;
			ev.instantiate();
			ev->set_position(pos);
			ev->set_relative(pos - r_mouse);
			r_mouse = pos;
			p_runner.push_input(ev);
		} else if (name == "down") {
			mouse_button(true);
		} else if (name == "up") {
			mouse_button(false);
		} else if (name == "click") {
			mouse_button(true);
			mouse_button(false);
		} else if (name == "keydown") {
			key(true);
		} else if (name == "keyup") {
			key(false);
		} else if (name == "key") {
			key(true);
			key(false);
		} else if (name == "text") {
			Ref<InputEventKey> ev;
			ev.instantiate();
			ev->set_keycode(KEY_NONE);
			ev->set_pressed(true);
			ev->set_text(command.rest);
			p_runner.push_input(ev);
		} else {
			print_warning("Scénario de test : commande inconnue '" + name + "'.");
		}
	}
}

int run_game(const CommandLine &p_cmd) {
	DisplayServer *display = DisplayServer::get_singleton();
	RenderingServer *rs = RenderingServer::get_singleton();
	Engine *engine = Engine::get_singleton();
	OS *os = OS::get_singleton();

	const int width = (int)GLOBAL_GET("display/width").as_int();
	const int height = (int)GLOBAL_GET("display/height").as_int();
	display->set_logical_size(width, height);
	// Input.set_mouse_mode() : curseur masqué ou capturé (caméra à la souris).
	Input::get_singleton()->set_mouse_mode_handler([](int p_mode) {
		SDL_SetRelativeMouseMode(p_mode == Input::MOUSE_MODE_CAPTURED ? SDL_TRUE : SDL_FALSE);
		SDL_ShowCursor(p_mode == Input::MOUSE_MODE_VISIBLE ? SDL_ENABLE : SDL_DISABLE);
	});

	String scene = p_cmd.scene;
	if (scene.empty()) {
		scene = GLOBAL_GET("application/main_scene").as_string();
	} else if (!str_begins_with(scene, "res://")) {
		scene = "res://" + scene;
	}

	GameRunner runner;
	if (runner.start(scene, Size2((real_t)width, (real_t)height)) != OK) {
		return 1;
	}

	const bool vsync = GLOBAL_GET("display/vsync").as_bool();
	uint64_t last_ticks = os->get_ticks_usec();
	uint64_t fps_ticks = last_ticks;
	int fps_frames = 0;
	int frame_count = 0;
	bool running = true;

	const std::vector<GameTestCommand> test_commands = load_game_test_script();
	Vector2 test_mouse;

	while (running) {
		const uint64_t frame_start = os->get_ticks_usec();
		run_game_test_commands(test_commands, frame_count, false, runner, test_mouse);
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) {
				running = false;
				break;
			}
			if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
				Input::get_singleton()->release_all();
				continue;
			}
			if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F11 && !event.key.repeat) {
				display->window_set_fullscreen(!display->window_is_fullscreen());
				continue;
			}
			Ref<InputEvent> ev = DisplayServer::translate_event(event);
			if (ev.is_valid()) {
				runner.push_input(ev);
			}
		}
		if (!running) {
			break;
		}

		const uint64_t now = os->get_ticks_usec();
		double delta = (double)(now - last_ticks) / 1000000.0;
		last_ticks = now;
		if (p_cmd.quit_after > 0) {
			delta = 1.0 / 60.0; // pas déterministe pour les tests
		}

		if (!runner.iterate(delta)) {
			running = false;
		}

		// Fenêtre plus grande que la résolution du projet : la 3D et le texte sont rendus à la
		// taille réelle de l'image (voir SceneTree::set_render_scale).
		if (runner.get_tree()) {
			const Size2 output = display->window_get_size();
			runner.get_tree()->set_render_scale(std::min(output.x / (float)width, output.y / (float)height));
		}

		rs->reset_stats();
		rs->set_render_target(RID());
		runner.draw();

		run_game_test_commands(test_commands, frame_count, true, runner, test_mouse);
		frame_count++;
		const bool last_frame = !running || (p_cmd.quit_after > 0 && frame_count >= p_cmd.quit_after);
		if (last_frame && !p_cmd.screenshot.empty()) {
			if (display->save_screenshot(p_cmd.screenshot) == OK) {
				print_line("Capture enregistrée : " + p_cmd.screenshot);
			}
		}
		display->present();
		if (last_frame) {
			running = false;
		}

		fps_frames++;
		if (now - fps_ticks >= 1000000) {
			engine->set_frames_per_second(fps_frames * 1000000.0 / (double)(now - fps_ticks));
			fps_frames = 0;
			fps_ticks = now;
		}
		// Sans synchronisation verticale effective, on évite de saturer le processeur.
		const uint64_t frame_usec = os->get_ticks_usec() - frame_start;
		if (p_cmd.quit_after <= 0 && frame_usec < (vsync ? 2000u : 1000u)) {
			os->delay_msec(vsync ? 8 : 1);
		}
	}

	runner.stop();
	return 0;
}

} // namespace

int Main::run(int p_argc, char **p_argv) {
	CommandLine cmd;
	if (!parse_command_line(p_argc, p_argv, cmd)) {
		print_help();
		return 2;
	}
	if (cmd.show_help) {
		print_help();
		return 0;
	}
	if (cmd.show_version) {
		print_line(MEX_VERSION);
		return 0;
	}

	register_core_types();

	// --- Projet ---
	String project_path = cmd.project_path.empty() ? String(".") : cmd.project_path;

	// Jeu exporté : une archive « <exécutable>.pck » (ou « data.pck ») à côté de l'exécutable
	// est lancée directement, sans éditeur.
	String pack_file = cmd.main_pack;
	if (pack_file.empty() && cmd.project_path.empty() && !cmd.editor) {
		const String executable = OS::get_singleton()->get_executable_path();
		if (!executable.empty()) {
			const String dir = path_get_base_dir(executable);
			for (const String &candidate : { path_join(dir, path_get_basename(path_get_file(executable)) + ".pck"), path_join(dir, "data.pck") }) {
				if (FileAccess::exists(candidate)) {
					pack_file = candidate;
					break;
				}
			}
		}
	}
	if (!pack_file.empty()) {
		const Error err = PackedData::load(pack_file);
		if (err != OK) {
			print_error("Archive de jeu illisible : " + pack_file);
			unregister_core_types();
			return 1;
		}
		project_path = path_get_base_dir(pack_file);
		if (project_path.empty()) {
			project_path = ".";
		}
		cmd.editor = false;
	}

	bool has_project = PackedData::is_loaded() ? FileAccess::exists(String("res://") + ProjectSettings::PROJECT_FILE) : FileAccess::exists(path_join(project_path, ProjectSettings::PROJECT_FILE));
	if (PackedData::is_loaded() && !has_project) {
		print_error("L'archive ne contient pas de projet : " + pack_file);
		unregister_core_types();
		return 1;
	}
	if (!has_project && !cmd.project_path.empty() && !cmd.editor) {
		print_error("Aucun projet trouvé dans '" + project_path + "' (fichier " + String(ProjectSettings::PROJECT_FILE) + " manquant).");
		unregister_core_types();
		return 1;
	}
#ifdef TOOLS_ENABLED
	if (!has_project) {
		cmd.editor = true; // l'éditeur proposera d'ouvrir ou de créer un projet
	}
#else
	if (cmd.editor) {
		print_error("Cet exécutable a été compilé sans l'éditeur.");
		unregister_core_types();
		return 1;
	}
	if (!has_project) {
		print_error("Aucun projet trouvé dans le dossier courant. Utilisez --path <dossier>.");
		unregister_core_types();
		return 1;
	}
#endif
	if (has_project) {
		ProjectSettings::get_singleton()->setup(project_path);
	}
	Engine::get_singleton()->set_editor_hint(cmd.editor);

	// --- Serveurs ---
	DisplayServer *display = new DisplayServer;
	RenderingServer *rendering = memnew(RenderingServer);
	RenderingServer3D *rendering_3d = new RenderingServer3D;
	PhysicsServer2D *physics = memnew(PhysicsServer2D);
	PhysicsServer3D *physics_3d = new PhysicsServer3D;
	AudioServer *audio = memnew(AudioServer);

	DisplayServer::WindowOptions options;
	options.hidden = cmd.hidden;
	if (cmd.editor) {
		options.title = "MEX - Metoushela Engine X";
		options.width = 1440;
		options.height = 860;
		options.resizable = true;
		options.maximized = !cmd.hidden;
		options.vsync = true;
		options.scale_with_screen = true;
		// Taille minimale provisoire : l'éditeur la recalcule selon l'échelle de son interface.
		options.min_width = 720;
		options.min_height = 450;
#ifdef TOOLS_ENABLED
		// Barre de titre intégrée à l'éditeur (préférence), sinon barre sombre du système.
		options.borderless = EditorNode::read_title_bar_preference();
#endif
	} else {
		options.title = GLOBAL_GET("application/name").as_string();
		options.width = (int)GLOBAL_GET("display/width").as_int();
		options.height = (int)GLOBAL_GET("display/height").as_int();
		options.resizable = GLOBAL_GET("display/resizable").as_bool();
		options.fullscreen = GLOBAL_GET("display/fullscreen").as_bool();
		options.vsync = GLOBAL_GET("display/vsync").as_bool();
		options.scale_with_screen = true; // la fenêtre garde la même taille apparente sur un écran dense
		// Limite en dessous de laquelle le joueur ne peut pas réduire la fenêtre (jamais plus
		// que la taille de départ), et barre de titre sombre.
		options.min_width = std::min((int)GLOBAL_GET("display/min_width").as_int(), options.width);
		options.min_height = std::min((int)GLOBAL_GET("display/min_height").as_int(), options.height);
		options.dark_title_bar = GLOBAL_GET("display/dark_title_bar").as_bool();
	}

	int exit_code = 0;
	if (display->create(options) != OK) {
		print_error("Impossible de créer la fenêtre.");
		exit_code = 1;
	} else {
		rendering->init(display->get_renderer());
		if (has_project && GLOBAL_GET("rendering/texture_filter").as_string() == "nearest") {
			rendering->set_default_texture_filter(RenderingServer::TEXTURE_FILTER_NEAREST);
		}
		// La 3D reste facultative : sans OpenGL, le moteur fonctionne en 2D seulement.
		rendering_3d->init(display->get_renderer(), (int)GLOBAL_GET("rendering/3d/msaa").as_int(), (int)GLOBAL_GET("rendering/3d/shadow_size").as_int());
		rendering_3d->set_shadow_distance((float)GLOBAL_GET("rendering/3d/shadow_distance").as_float());
		if (!rendering_3d->is_available() && !cmd.hidden) {
			print_warning("OpenGL indisponible : le rendu 3D est désactivé.");
		}
		if (audio->init() != OK) {
			print_warning("Audio indisponible : le jeu tournera sans son.");
		}

		// Icône de la fenêtre : celle du projet (application/icon) pour un jeu, sinon celle de MEX.
		{
			Ref<Texture2D> icon;
			icon.instantiate();
			Error icon_err = ERR_FILE_NOT_FOUND;
			const String project_icon = (has_project && !cmd.editor) ? GLOBAL_GET("application/icon").as_string() : String();
			if (!project_icon.empty()) {
				icon_err = icon->load_from_file(project_icon);
			}
			if (icon_err != OK) {
				icon_err = icon->load_from_memory(MEX_ICON_PNG, MEX_ICON_PNG_SIZE);
			}
			if (icon_err == OK) {
				display->set_icon(icon->get_rgba().data(), icon->get_width(), icon->get_height());
			}
		}

		register_scene_types();
		register_mexscript_types();
		InputMap::get_singleton()->load_from_project_settings();

#ifdef TOOLS_ENABLED
		if (cmd.editor) {
			EditorNode *editor = new EditorNode;
			exit_code = editor->run(has_project, cmd.scene, cmd.quit_after, cmd.screenshot);
			delete editor;
		} else
#endif
		{
			exit_code = run_game(cmd);
		}

		unregister_mexscript_types();
		unregister_scene_types();
		audio->finish();
		rendering_3d->finish();
		rendering->finish();
		display->destroy();
	}

	memdelete(audio);
	delete physics_3d;
	memdelete(physics);
	delete rendering_3d;
	memdelete(rendering);
	delete display;
	unregister_core_types();
	SDL_Quit();
	return exit_code;
}

#ifdef _WIN32
// Sous Windows : rattache la console du terminal qui a lancé le programme (l'exécutable
// est une application graphique, sans console propre), affiche en UTF-8 et relit la
// ligne de commande en Unicode pour accepter les chemins accentués.
static void _windows_attach_console() {
	if (AttachConsole(ATTACH_PARENT_PROCESS)) {
		// Ne remplace pas une sortie déjà redirigée vers un fichier ou un tube.
		if (_fileno(stdout) < 0 || _get_osfhandle(_fileno(stdout)) < 0) {
			freopen("CONOUT$", "w", stdout);
		}
		if (_fileno(stderr) < 0 || _get_osfhandle(_fileno(stderr)) < 0) {
			freopen("CONOUT$", "w", stderr);
		}
	}
	SetConsoleOutputCP(CP_UTF8);
}

int main(int, char **) {
	_windows_attach_console();
	int wide_argc = 0;
	LPWSTR *wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
	std::vector<std::string> args;
	for (int i = 0; wide_argv && i < wide_argc; i++) {
		const int length = WideCharToMultiByte(CP_UTF8, 0, wide_argv[i], -1, nullptr, 0, nullptr, nullptr);
		std::string arg(length > 0 ? length - 1 : 0, '\0');
		if (length > 1) {
			WideCharToMultiByte(CP_UTF8, 0, wide_argv[i], -1, &arg[0], length, nullptr, nullptr);
		}
		args.push_back(arg);
	}
	if (wide_argv) {
		LocalFree(wide_argv);
	}
	if (args.empty()) {
		args.push_back("mex");
	}
	std::vector<char *> pointers;
	for (std::string &arg : args) {
		pointers.push_back(&arg[0]);
	}
	pointers.push_back(nullptr);
	return Main::run((int)args.size(), pointers.data());
}
#else
int main(int argc, char **argv) {
	return Main::run(argc, argv);
}
#endif
