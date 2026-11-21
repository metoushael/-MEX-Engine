// DisplayServer : fenêtre, contexte de rendu et traduction des événements SDL.
#pragma once

#include "core/input/input_event.h"

struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;

class DisplayServer {
	static DisplayServer *singleton;

	SDL_Window *window = nullptr;
	SDL_Renderer *renderer = nullptr;
	bool fullscreen = false;
	float screen_scale = 1.0f;

public:
	struct WindowOptions {
		String title = "MEX";
		int width = 1152;
		int height = 648;
		bool resizable = true;
		bool fullscreen = false;
		bool maximized = false;
		bool vsync = true;
		bool hidden = false;
		// Agrandit la fenêtre selon l'échelle d'affichage du système (écrans à haute densité).
		bool scale_with_screen = false;
	};

	static DisplayServer *get_singleton() { return singleton; }

	Error create(const WindowOptions &p_options);
	// Échelle d'affichage du système : 1 = 100 %, 1.5 = 150 %... (toujours 1 sous macOS, où
	// la densité est donnée par get_pixel_ratio()).
	float get_screen_scale() const { return screen_scale; }
	// Pixels réels par unité de fenêtre (2 sur un écran Retina, 1 ailleurs en général).
	float get_pixel_ratio() const;
	void set_icon(const uint8_t *p_rgba, int p_width, int p_height);
	void destroy();

	SDL_Window *get_window() const { return window; }
	SDL_Renderer *get_renderer() const { return renderer; }

	Size2 window_get_size() const; // taille de sortie du rendu, en pixels
	void window_set_title(const String &p_title);
	void window_set_fullscreen(bool p_fullscreen);
	bool window_is_fullscreen() const { return fullscreen; }
	// Résolution logique : le jeu est mis à l'échelle (bandes noires si besoin).
	void set_logical_size(int p_width, int p_height);
	void present();
	Error save_screenshot(const String &p_path); // BMP

	// Convertit un événement SDL en InputEvent (nul si non pertinent).
	static Ref<InputEvent> translate_event(const SDL_Event &p_event);

	DisplayServer();
	~DisplayServer();
};
