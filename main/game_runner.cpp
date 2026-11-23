#include "game_runner.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/string/translation.h"
#include "core/string/ustring.h"
#include "servers/audio_server.h"

#include <SDL.h>
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/packed_scene.h"
#include "servers/rendering_server.h"

Error GameRunner::start(const String &p_scene_path, const Size2 &p_viewport_size) {
	ERR_FAIL_COND_V_MSG(tree != nullptr, ERR_ALREADY_EXISTS, "Le jeu est déjà en cours d'exécution.");
	if (p_scene_path.empty()) {
		print_error("Aucune scène à lancer : définissez la scène principale du projet (application/main_scene).");
		return ERR_INVALID_PARAMETER;
	}

	// Les scripts s'exécutent réellement pendant le jeu (l'éditeur les désactive).
	const bool scripting_was_enabled = ScriptServer::is_scripting_enabled();
	ScriptServer::set_scripting_enabled(true);

	Ref<PackedScene> packed = ResourceLoader::load(p_scene_path);
	if (packed.is_null()) {
		print_error("Impossible de charger la scène : " + p_scene_path);
		ScriptServer::set_scripting_enabled(scripting_was_enabled);
		return ERR_FILE_CANT_OPEN;
	}

	Input::get_singleton()->release_all();
	Engine::get_singleton()->set_time_scale(1.0);
	Engine::get_singleton()->set_physics_ticks_per_second((int)GLOBAL_GET("physics/ticks_per_second").as_int());
	Input::get_singleton()->set_emulate_touch_from_mouse(GLOBAL_GET("input_devices/pointing/emulate_touch_from_mouse").as_bool());

	// Bus audio du projet : [audio] buses = ["Musique", "Effets"].
	if (AudioServer *audio = AudioServer::get_singleton()) {
		const Array bus_names = GLOBAL_GET("audio/buses").as_array();
		for (int i = 0; i < bus_names.size(); i++) {
			if (audio->get_bus_index(bus_names[i].as_string()) < 0) {
				audio->add_bus(bus_names[i].as_string());
			}
		}
	}
	// Traductions du projet : [locale] translations = ["res://textes.csv"], fallback = "en",
	// locale = "fr" pour imposer une langue (sinon celle du système).
	if (TranslationServer *translations = TranslationServer::get_singleton()) {
		translations->clear();
		const Array files = GLOBAL_GET("locale/translations").as_array();
		for (int i = 0; i < files.size(); i++) {
			translations->load_csv(files[i].as_string());
		}
		const String fallback = ProjectSettings::get_singleton()->get_setting("locale/fallback", Variant("en")).as_string();
		translations->set_fallback_locale(fallback);
		String locale = ProjectSettings::get_singleton()->get_setting("locale/locale", Variant("")).as_string();
		if (locale.empty()) {
			if (SDL_Locale *preferred = SDL_GetPreferredLocales()) {
				// Première langue du système pour laquelle le jeu a des textes.
				for (SDL_Locale *l = preferred; l->language && locale.empty(); l++) {
					const String name = l->country ? String(l->language) + "_" + l->country : String(l->language);
					if (!translations->find_closest_locale(name).empty()) {
						locale = name;
					}
				}
				SDL_free(preferred);
			}
		}
		translations->set_locale(locale.empty() ? fallback : locale);
	}

	tree = memnew(SceneTree(false));
	tree->set_viewport_size(p_viewport_size);
	accumulator = 0;
	scene_path = p_scene_path;

	// Chargements automatiques du projet : [autoload] Nom = "res://fichier.mx" (ou .scn).
	// Chaque nœud est ajouté à la racine avant la scène, y reste d'une scène à l'autre, et
	// les scripts l'appellent par son nom (comme les « Autoload » de Godot).
	for (const auto &entry : ProjectSettings::get_singleton()->get_all_settings()) {
		if (!str_begins_with(entry.first, "autoload/")) {
			continue;
		}
		const String name = entry.first.substr(9);
		const String path = entry.second.as_string();
		if (name.empty() || path.empty()) {
			continue;
		}
		if (Engine::get_singleton()->has_singleton(name) || ClassDB::class_exists(name)) {
			print_error("Chargement automatique « " + name + " » ignoré : ce nom est déjà celui d'une classe ou d'un objet du moteur.");
			continue;
		}
		Ref<Resource> resource = ResourceLoader::load(path);
		Node *node = nullptr;
		if (PackedScene *autoload_scene = Object::cast_to<PackedScene>(resource.ptr())) {
			node = autoload_scene->instantiate();
		} else if (Script *script = Object::cast_to<Script>(resource.ptr())) {
			Object *object = ClassDB::instantiate(script->get_instance_base_type());
			node = Object::cast_to<Node>(object);
			if (node) {
				node->set_script(Variant(resource));
			} else if (object) {
				memdelete(object);
			}
		}
		if (!node) {
			print_error("Chargement automatique « " + name + " » impossible : " + path + " n'est ni une scène ni un script de nœud.");
			continue;
		}
		node->set_name(name);
		tree->get_root()->add_child(node);
		Engine::get_singleton()->add_singleton(name, node);
		autoloads.push_back(name);
	}

	Node *scene = packed->instantiate();
	if (!scene) {
		print_error("Impossible d'instancier la scène : " + p_scene_path);
		memdelete(tree);
		tree = nullptr;
		ScriptServer::set_scripting_enabled(scripting_was_enabled);
		return ERR_CANT_CREATE;
	}
	tree->get_root()->add_child(scene);
	tree->set_current_scene(scene);
	ScriptServer::set_scripting_enabled(scripting_was_enabled);
	return OK;
}

void GameRunner::stop() {
	if (!tree) {
		return;
	}
	for (const String &name : autoloads) {
		Engine::get_singleton()->remove_singleton(name);
	}
	autoloads.clear();
	memdelete(tree);
	tree = nullptr;
	Input::get_singleton()->release_all();
	Engine::get_singleton()->set_time_scale(1.0);
}

void GameRunner::push_input(const Ref<InputEvent> &p_event) {
	if (!tree || p_event.is_null()) {
		return;
	}
	const bool scripting_was_enabled = ScriptServer::is_scripting_enabled();
	ScriptServer::set_scripting_enabled(true);
	Input::get_singleton()->parse_input_event(p_event);
	tree->input_event(p_event);
	// Souris utilisée comme écran tactile (emulate_touch_from_mouse) : l'événement tactile suit.
	const Ref<InputEvent> touch = Input::get_singleton()->make_touch_from_mouse(p_event);
	if (touch.is_valid()) {
		tree->input_event(touch);
	}
	ScriptServer::set_scripting_enabled(scripting_was_enabled);
}

bool GameRunner::iterate(double p_delta) {
	if (!tree) {
		return false;
	}
	Engine *engine = Engine::get_singleton();
	const bool scripting_was_enabled = ScriptServer::is_scripting_enabled();
	ScriptServer::set_scripting_enabled(true); // scènes instanciées en cours de jeu

	const double delta = std::min(p_delta, 0.25) * engine->get_time_scale();
	const double step = 1.0 / engine->get_physics_ticks_per_second();
	accumulator += delta;
	int steps = 0;
	while (accumulator >= step && steps < 8) {
		tree->physics_process(step);
		engine->advance_physics_frame();
		accumulator -= step;
		steps++;
	}
	if (accumulator > step) {
		accumulator = 0; // trop de retard : on abandonne le rattrapage
	}
	tree->process(delta);
	engine->advance_process_frame();

	ScriptServer::set_scripting_enabled(scripting_was_enabled);
	return !tree->is_quit_requested();
}

void GameRunner::draw() {
	RenderingServer *rs = RenderingServer::get_singleton();
	rs->clear(GLOBAL_GET("rendering/clear_color").as_color());
	if (tree) {
		tree->draw();
	}
}

GameRunner::~GameRunner() {
	stop();
}
