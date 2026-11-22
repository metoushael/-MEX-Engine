#include "register_core_types.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/debug_monitor.h"
#include "core/math/a_star.h"
#include "core/string/translation.h"
#include "core/input/input.h"
#include "core/io/net_socket.h"
#include "core/io/resource.h"
#include "core/io/resource_loader.h"
#include "core/object/script_language.h"
#include "core/os/os.h"

static Engine *engine = nullptr;
static OS *os = nullptr;
static ProjectSettings *project_settings = nullptr;
static InputMap *input_map = nullptr;
static Input *input = nullptr;
static TranslationServer *translation_server = nullptr;
static Performance *performance = nullptr;

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
	GDREGISTER_ABSTRACT_CLASS(TranslationServer);
	GDREGISTER_ABSTRACT_CLASS(Performance);
	GDREGISTER_CLASS(AStar2D);
	GDREGISTER_CLASS(AStarGrid2D);
	GDREGISTER_CLASS(StreamPeerTCP);
	GDREGISTER_CLASS(TCPServer);
	GDREGISTER_CLASS(PacketPeerUDP);

	engine = memnew(Engine);
	os = memnew(OS);
	project_settings = memnew(ProjectSettings);
	input_map = memnew(InputMap);
	input = memnew(Input);
	translation_server = memnew(TranslationServer);
	performance = memnew(Performance);

	engine->add_singleton("Engine", engine);
	engine->add_singleton("OS", os);
	engine->add_singleton("Time", os);
	engine->add_singleton("ProjectSettings", project_settings);
	engine->add_singleton("InputMap", input_map);
	engine->add_singleton("Input", input);
	engine->add_singleton("TranslationServer", translation_server);
	engine->add_singleton("Performance", performance);
}

void unregister_core_types() {
	ResourceLoader::cleanup();
	memdelete(performance);
	memdelete(translation_server);
	memdelete(input);
	memdelete(input_map);
	memdelete(project_settings);
	memdelete(os);
	memdelete(engine);
	ClassDB::cleanup();
}
