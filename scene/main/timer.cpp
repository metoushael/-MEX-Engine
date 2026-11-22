#include "timer.h"

#include "scene/main/scene_tree.h"

void Timer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (autostart && !get_tree()->is_edit_mode()) {
				start();
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (paused || time_left < 0) {
				return;
			}
			time_left -= get_process_delta_time();
			if (time_left <= 0) {
				if (one_shot) {
					stop();
				} else {
					time_left += wait_time;
					if (time_left <= 0) {
						time_left = wait_time;
					}
				}
				emit_signal("timeout");
			}
		} break;
	}
}

void Timer::set_wait_time(double p_time) {
	ERR_FAIL_COND_MSG(p_time <= 0, "Timer : wait_time doit être strictement positif.");
	wait_time = p_time;
}

void Timer::start(double p_time) {
	ERR_FAIL_COND_MSG(!is_inside_tree(), "Timer : le nœud doit être dans l'arbre pour démarrer.");
	if (p_time > 0) {
		wait_time = p_time;
	}
	time_left = wait_time;
	set_process_internal(true);
}

void Timer::stop() {
	time_left = -1.0;
	set_process_internal(false);
}

void Timer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_wait_time", "time"), &Timer::set_wait_time);
	ClassDB::bind_method(D_METHOD("get_wait_time"), &Timer::get_wait_time);
	ClassDB::bind_method(D_METHOD("set_one_shot", "one_shot"), &Timer::set_one_shot);
	ClassDB::bind_method(D_METHOD("is_one_shot"), &Timer::is_one_shot);
	ClassDB::bind_method(D_METHOD("set_autostart", "autostart"), &Timer::set_autostart);
	ClassDB::bind_method(D_METHOD("has_autostart"), &Timer::has_autostart);
	ClassDB::bind_method(D_METHOD("set_paused", "paused"), &Timer::set_paused);
	ClassDB::bind_method(D_METHOD("is_paused"), &Timer::is_paused);
	ClassDB::bind_method(D_METHOD("start", "time"), &Timer::start, DEFVAL(-1.0));
	ClassDB::bind_method(D_METHOD("stop"), &Timer::stop);
	ClassDB::bind_method(D_METHOD("is_stopped"), &Timer::is_stopped);
	ClassDB::bind_method(D_METHOD("get_time_left"), &Timer::get_time_left);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wait_time", PROPERTY_HINT_RANGE, "0.001,4096,0.001"), "set_wait_time", "get_wait_time");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "one_shot"), "set_one_shot", "is_one_shot");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "autostart"), "set_autostart", "has_autostart");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "time_left", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_time_left");

	ADD_SIGNAL(MethodInfo("timeout"));
}
