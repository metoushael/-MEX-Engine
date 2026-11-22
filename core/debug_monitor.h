// Performance : mesures du moteur lisibles par les scripts et par l'éditeur
// (images par seconde, durée des traitements, nombre d'objets, appels de dessin...),
// et profileur de scripts (temps passé dans chaque fonction).
#pragma once

#include "core/object/object.h"

#include <functional>

class Performance : public Object {
	GDCLASS(Performance, Object)

public:
	enum Monitor {
		TIME_FPS,
		TIME_PROCESS, // secondes passées dans la dernière image (_process, animations...)
		TIME_PHYSICS_PROCESS,
		OBJECT_COUNT,
		OBJECT_NODE_COUNT,
		RENDER_2D_DRAW_CALLS,
		RENDER_2D_ITEMS,
		AUDIO_ACTIVE_VOICES,
		SCRIPT_CALL_COUNT, // appels de fonctions de script depuis la dernière lecture du profil
		MONITOR_MAX
	};

	struct ScriptStackEntry {
		String source;
		String function;
		int line = 0;
	};
	struct ScriptProfileEntry {
		String source;
		String function;
		uint64_t calls = 0;
		uint64_t total_usec = 0; // avec les fonctions appelées
		uint64_t self_usec = 0; // sans elles
	};

private:
	static Performance *singleton;
	std::function<double()> providers[MONITOR_MAX];
	double values[MONITOR_MAX] = {};

protected:
	static void _bind_methods();

public:
	static Performance *get_singleton() { return singleton; }

	// Branchés par le langage de script (le cœur ne le connaît pas).
	static void (*script_get_stack)(std::vector<ScriptStackEntry> *r_stack);
	static void (*script_profiling_set_enabled)(bool p_enabled);
	static bool (*script_profiling_is_enabled)();
	static void (*script_profiling_get_data)(std::vector<ScriptProfileEntry> *r_data, bool p_reset);

	void set_monitor_provider(int p_monitor, const std::function<double()> &p_provider);
	void set_monitor_value(int p_monitor, double p_value);
	double get_monitor(int p_monitor) const;
	static String get_monitor_name(int p_monitor);

	void set_script_profiling(bool p_enabled);
	bool is_script_profiling() const;
	// Tableau de dictionnaires { source, function, calls, total_usec, self_usec }, du plus coûteux au moins coûteux.
	Array get_script_profile(bool p_reset = false);
	Array get_script_stack() const;

	Performance();
	~Performance();
};
