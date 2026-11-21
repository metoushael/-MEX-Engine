// OS : horloge et informations système.
#pragma once

#include "core/object/object.h"

class OS : public Object {
	GDCLASS(OS, Object)

	static OS *singleton;
	uint64_t start_usec = 0;

protected:
	static void _bind_methods();

public:
	static OS *get_singleton() { return singleton; }

	uint64_t get_ticks_usec() const;
	uint64_t get_ticks_msec() const { return get_ticks_usec() / 1000; }
	void delay_msec(int p_msec) const;
	String get_name() const;
	double get_unix_time() const;
	// Chemin absolu de l'exécutable en cours (vide s'il est inconnu).
	String get_executable_path() const;

	OS();
	~OS();
};
