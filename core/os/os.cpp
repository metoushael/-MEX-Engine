#include "os.h"

#include "core/config/engine.h"

#include <chrono>
#include <filesystem>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

OS *OS::singleton = nullptr;
Engine *Engine::singleton = nullptr;

static uint64_t _now_usec() {
	return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

uint64_t OS::get_ticks_usec() const {
	return _now_usec() - start_usec;
}

void OS::delay_msec(int p_msec) const {
	if (p_msec > 0) {
		std::this_thread::sleep_for(std::chrono::milliseconds(p_msec));
	}
}

String OS::get_name() const {
#if defined(_WIN32)
	return "Windows";
#elif defined(__APPLE__)
	return "macOS";
#elif defined(__linux__)
	return "Linux";
#else
	return "Unknown";
#endif
}

String OS::get_executable_path() const {
	namespace fs = std::filesystem;
	std::error_code ec;
#if defined(_WIN32)
	std::wstring buffer(32768, L'\0');
	const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), (DWORD)buffer.size());
	if (length == 0 || length >= buffer.size()) {
		return String();
	}
	buffer.resize(length);
	String path = fs::path(buffer).u8string();
	for (char &c : path) {
		if (c == '\\') {
			c = '/';
		}
	}
	return path;
#elif defined(__APPLE__)
	uint32_t size = 0;
	_NSGetExecutablePath(nullptr, &size);
	std::string buffer(size, '\0');
	if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
		return String();
	}
	const fs::path path = fs::canonical(fs::path(buffer.c_str()), ec);
	return ec ? String(buffer.c_str()) : path.u8string();
#else
	const fs::path path = fs::read_symlink("/proc/self/exe", ec);
	return ec ? String() : path.u8string();
#endif
}

double OS::get_unix_time() const {
	return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void OS::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_ticks_usec"), &OS::get_ticks_usec);
	ClassDB::bind_method(D_METHOD("get_ticks_msec"), &OS::get_ticks_msec);
	ClassDB::bind_method(D_METHOD("get_name"), &OS::get_name);
	ClassDB::bind_method(D_METHOD("get_unix_time"), &OS::get_unix_time);
	ClassDB::bind_method(D_METHOD("get_unix_time_from_system"), &OS::get_unix_time);
}

OS::OS() {
	singleton = this;
	start_usec = _now_usec();
}

OS::~OS() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

void Engine::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_process_frames"), &Engine::get_process_frames);
	ClassDB::bind_method(D_METHOD("get_physics_frames"), &Engine::get_physics_frames);
	ClassDB::bind_method(D_METHOD("is_in_physics_frame"), &Engine::is_in_physics_frame);
	ClassDB::bind_method(D_METHOD("is_editor_hint"), &Engine::is_editor_hint);
	ClassDB::bind_method(D_METHOD("get_frames_per_second"), &Engine::get_frames_per_second);
	ClassDB::bind_method(D_METHOD("set_time_scale", "scale"), &Engine::set_time_scale);
	ClassDB::bind_method(D_METHOD("get_time_scale"), &Engine::get_time_scale);
	ClassDB::bind_method(D_METHOD("get_physics_ticks_per_second"), &Engine::get_physics_ticks_per_second);
	ClassDB::bind_method(D_METHOD("get_version"), &Engine::get_version);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "time_scale"), "set_time_scale", "get_time_scale");
}
