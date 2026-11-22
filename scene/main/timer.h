// Timer : compte à rebours émettant le signal "timeout".
#pragma once

#include "scene/main/node.h"

class Timer : public Node {
	GDCLASS(Timer, Node)

	double wait_time = 1.0;
	bool one_shot = false;
	bool autostart = false;
	bool paused = false;
	double time_left = -1.0;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_wait_time(double p_time);
	double get_wait_time() const { return wait_time; }
	void set_one_shot(bool p_one_shot) { one_shot = p_one_shot; }
	bool is_one_shot() const { return one_shot; }
	void set_autostart(bool p_autostart) { autostart = p_autostart; }
	bool has_autostart() const { return autostart; }
	void set_paused(bool p_paused) { paused = p_paused; }
	bool is_paused() const { return paused; }

	void start(double p_time = -1.0);
	void stop();
	bool is_stopped() const { return time_left < 0; }
	double get_time_left() const { return time_left > 0 ? time_left : 0; }
};
