#include "debug_monitor.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

#include <algorithm>

Performance *Performance::singleton = nullptr;
void (*Performance::script_get_stack)(std::vector<Performance::ScriptStackEntry> *) = nullptr;
void (*Performance::script_profiling_set_enabled)(bool) = nullptr;
bool (*Performance::script_profiling_is_enabled)() = nullptr;
void (*Performance::script_profiling_get_data)(std::vector<Performance::ScriptProfileEntry> *, bool) = nullptr;

Performance::Performance() {
	singleton = this;
}

Performance::~Performance() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

void Performance::set_monitor_provider(int p_monitor, const std::function<double()> &p_provider) {
	ERR_FAIL_INDEX(p_monitor, MONITOR_MAX);
	providers[p_monitor] = p_provider;
}

void Performance::set_monitor_value(int p_monitor, double p_value) {
	ERR_FAIL_INDEX(p_monitor, MONITOR_MAX);
	values[p_monitor] = p_value;
}

double Performance::get_monitor(int p_monitor) const {
	ERR_FAIL_INDEX_V(p_monitor, MONITOR_MAX, 0.0);
	switch (p_monitor) {
		case TIME_FPS:
			return Engine::get_singleton()->get_frames_per_second();
		case OBJECT_COUNT:
			return (double)ObjectDB::get_object_count();
		default:
			return providers[p_monitor] ? providers[p_monitor]() : values[p_monitor];
	}
}

String Performance::get_monitor_name(int p_monitor) {
	static const char *names[MONITOR_MAX] = {
		"Images par seconde", "Traitement (s)", "Physique (s)", "Objets", "Nœuds",
		"Appels de dessin 2D", "Éléments 2D", "Voix audio", "Appels de script"
	};
	return p_monitor >= 0 && p_monitor < MONITOR_MAX ? names[p_monitor] : "";
}

void Performance::set_script_profiling(bool p_enabled) {
	if (script_profiling_set_enabled) {
		script_profiling_set_enabled(p_enabled);
	}
}

bool Performance::is_script_profiling() const {
	return script_profiling_is_enabled && script_profiling_is_enabled();
}

Array Performance::get_script_profile(bool p_reset) {
	Array out;
	if (!script_profiling_get_data) {
		return out;
	}
	std::vector<ScriptProfileEntry> data;
	script_profiling_get_data(&data, p_reset);
	std::sort(data.begin(), data.end(), [](const ScriptProfileEntry &a, const ScriptProfileEntry &b) {
		return a.self_usec != b.self_usec ? a.self_usec > b.self_usec : a.function < b.function;
	});
	for (const ScriptProfileEntry &entry : data) {
		Dictionary d;
		d[Variant("source")] = Variant(entry.source);
		d[Variant("function")] = Variant(entry.function);
		d[Variant("calls")] = Variant((int64_t)entry.calls);
		d[Variant("total_usec")] = Variant((int64_t)entry.total_usec);
		d[Variant("self_usec")] = Variant((int64_t)entry.self_usec);
		out.push_back(Variant(d));
	}
	return out;
}

Array Performance::get_script_stack() const {
	Array out;
	if (!script_get_stack) {
		return out;
	}
	std::vector<ScriptStackEntry> stack;
	script_get_stack(&stack);
	for (const ScriptStackEntry &entry : stack) {
		Dictionary d;
		d[Variant("source")] = Variant(entry.source);
		d[Variant("function")] = Variant(entry.function);
		d[Variant("line")] = Variant((int64_t)entry.line);
		out.push_back(Variant(d));
	}
	return out;
}

void Performance::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_monitor", "monitor"), &Performance::get_monitor);
	ClassDB::bind_method(D_METHOD("set_script_profiling", "enabled"), &Performance::set_script_profiling);
	ClassDB::bind_method(D_METHOD("is_script_profiling"), &Performance::is_script_profiling);
	ClassDB::bind_method(D_METHOD("get_script_profile", "reset"), &Performance::get_script_profile, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_script_stack"), &Performance::get_script_stack);
	BIND_ENUM_CONSTANT(TIME_FPS);
	BIND_ENUM_CONSTANT(TIME_PROCESS);
	BIND_ENUM_CONSTANT(TIME_PHYSICS_PROCESS);
	BIND_ENUM_CONSTANT(OBJECT_COUNT);
	BIND_ENUM_CONSTANT(OBJECT_NODE_COUNT);
	BIND_ENUM_CONSTANT(RENDER_2D_DRAW_CALLS);
	BIND_ENUM_CONSTANT(RENDER_2D_ITEMS);
	BIND_ENUM_CONSTANT(AUDIO_ACTIVE_VOICES);
	BIND_ENUM_CONSTANT(SCRIPT_CALL_COUNT);
}
