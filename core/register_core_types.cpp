#include "register_core_types.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/io/resource.h"
#include "core/io/resource_loader.h"
#include "core/object/script_language.h"
#include "core/os/os.h"

static Engine *engine = nullptr;
static OS *os = nullptr;
static ProjectSettings *project_settings = nullptr;
static InputMap *input_map = nullptr;
static Input *input = nullptr;

void register_core_types() {
	GDREGISTER_CLASS(Object);
	GDREGISTER_CLASS(RefCounted);
	GDREGISTER_CLASS(Resource);
	GDREGISTER_ABSTRACT_CLASS(Script);
	GDREGISTER_ABSTRACT_CLASS(InputEvent);
	GDREGISTER_CLASS(InputEventKey);
	GDREGISTER_ABSTRACT_CLASS(InputEventMouse);
	GDREGISTER_CLASS(InputEventMouseButton);
	GDREGISTER_CLASS(InputEventMouseMotion);
	GDREGISTER_ABSTRACT_CLASS(Engine);
	GDREGISTER_ABSTRACT_CLASS(OS);
	GDREGISTER_ABSTRACT_CLASS(ProjectSettings);
	GDREGISTER_ABSTRACT_CLASS(InputMap);
	GDREGISTER_ABSTRACT_CLASS(Input);

	engine = memnew(Engine);
	os = memnew(OS);
	project_settings = memnew(ProjectSettings);
	input_map = memnew(InputMap);
	input = memnew(Input);

	engine->add_singleton("Engine", engine);
	engine->add_singleton("OS", os);
	engine->add_singleton("Time", os);
	engine->add_singleton("ProjectSettings", project_settings);
	engine->add_singleton("InputMap", input_map);
	engine->add_singleton("Input", input);
}

void unregister_core_types() {
	ResourceLoader::cleanup();
	memdelete(input);
	memdelete(input_map);
	memdelete(project_settings);
	memdelete(os);
	memdelete(engine);
	ClassDB::cleanup();
}
