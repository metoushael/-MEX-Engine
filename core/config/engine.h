// Engine : état global de la boucle principale + registre des singletons scriptables.
#pragma once

#include "core/object/object.h"

// Numéro de version ; CMake le fournit (option MEX_VERSION), sinon cette valeur par défaut.
#ifndef MEX_VERSION
#define MEX_VERSION "0.1.0"
#endif

class Engine : public Object {
	GDCLASS(Engine, Object)

	static Engine *singleton;

	friend class Main;
	friend class SceneTree;
	friend class EditorNode;

	uint64_t process_frames = 0;
	uint64_t physics_frames = 0;
	bool in_physics = false;
	bool editor_hint = false;
	double fps = 0;
	double time_scale = 1.0;
	int physics_ticks_per_second = 60;
	std::map<String, Object *> singletons;

protected:
	static void _bind_methods();

public:
	static Engine *get_singleton() { return singleton; }

	uint64_t get_process_frames() const { return process_frames; }
	uint64_t get_physics_frames() const { return physics_frames; }
	bool is_in_physics_frame() const { return in_physics; }
	void set_editor_hint(bool p_enabled) { editor_hint = p_enabled; }
	bool is_editor_hint() const { return editor_hint; }
	double get_frames_per_second() const { return fps; }
	void set_frames_per_second(double p_fps) { fps = p_fps; }
	void set_time_scale(double p_scale) { time_scale = std::max(0.0, p_scale); }
	double get_time_scale() const { return time_scale; }
	void set_physics_ticks_per_second(int p_ticks) { physics_ticks_per_second = Math::clamp(p_ticks, 1, 1000); }
	int get_physics_ticks_per_second() const { return physics_ticks_per_second; }
	String get_version() const { return MEX_VERSION; }

	// Avance les compteurs de frames (utilisé par la boucle principale).
	void advance_physics_frame() { physics_frames++; }
	void advance_process_frame() { process_frames++; }
	void set_in_physics_frame(bool p_in) { in_physics = p_in; }

	void add_singleton(const String &p_name, Object *p_object) { singletons[p_name] = p_object; }
	Object *get_singleton_object(const String &p_name) const {
		auto it = singletons.find(p_name);
		return it == singletons.end() ? nullptr : it->second;
	}
	bool has_singleton(const String &p_name) const { return singletons.count(p_name) > 0; }

	Engine() { singleton = this; }
	~Engine() {
		if (singleton == this) {
			singleton = nullptr;
		}
	}
};
