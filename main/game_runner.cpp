#include "game_runner.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/io/resource_loader.h"
#include "core/object/script_language.h"
#include "core/string/ustring.h"
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

	tree = memnew(SceneTree(false));
	tree->set_viewport_size(p_viewport_size);
	accumulator = 0;
	scene_path = p_scene_path;

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
