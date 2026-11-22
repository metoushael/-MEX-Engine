// DisplayServer : fenêtre, contexte de rendu et traduction des événements SDL.
#pragma once

#include "core/input/input_event.h"
#include "core/math/color.h"

#include <vector>

struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;

class DisplayServer {
	static DisplayServer *singleton;

	SDL_Window *window = nullptr;
	SDL_Renderer *renderer = nullptr;
	bool fullscreen = false;
	bool borderless = false;
	bool resizable = true;
	bool dark_title_bar = true;
	float screen_scale = 1.0f;
	int min_width = 0, min_height = 0;
	String render_driver; // pilote de rendu de SDL réellement utilisé (« opengl », « direct3d »...)
	String render_details; // version d'OpenGL et carte graphique, quand elles sont connues
	String render_note; // pourquoi le pilote demandé n'a pas été gardé (vide : rien à signaler)

public:
	// Rectangle en unités de fenêtre (celles des événements de souris).
	struct WindowRect {
		int x = 0, y = 0, w = 0, h = 0;
	};

private:
	// Barre de titre intégrée : zones par lesquelles on déplace la fenêtre, épaisseur des
	// bords par lesquels on la redimensionne.
	std::vector<WindowRect> caption_areas;
	int resize_border = 0;
	uint32_t caption_click_ticks = 0;
	int caption_click_x = 0, caption_click_y = 0;
	bool maximize_toggle_pending = false;

	bool title_bar_has_colors = false;
	Color title_bar_background;
	Color title_bar_text;

	void _apply_min_size();
	void _apply_title_bar_theme();

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
		// Sans barre de titre du système : l'application dessine la sienne, avec ses boutons
		// Réduire, Agrandir et Fermer (voir window_set_caption_areas).
		bool borderless = false;
		// Barre de titre du système en version sombre, là où le système le permet.
		bool dark_title_bar = true;
		// Plus petite taille que l'utilisateur peut donner à la fenêtre (0 = pas de limite).
		// Suit scale_with_screen, et ne dépasse jamais la place disponible à l'écran.
		int min_width = 0;
		int min_height = 0;
		// Pilote de rendu de SDL : « opengl », « direct3d », « direct3d11 », « software »...
		// Vide ou « auto » : OpenGL (3D, shaders, lumières 2D) si le pilote de la carte graphique
		// est jugé fiable, sinon le pilote habituel du système (Direct3D sous Windows), puis le
		// rendu logiciel. La variable d'environnement MEX_RENDER_DRIVER passe avant.
		String render_driver;
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
	// Pilote de rendu retenu, détail (version d'OpenGL, carte) et raison d'un éventuel repli.
	String get_render_driver() const { return render_driver; }
	String get_render_details() const { return render_details; }
	String get_render_note() const { return render_note; }
	// Pilote de rendu sans risque sur ce système (mode de compatibilité).
	static String get_compatible_render_driver();
	// État de l'affichage, en clair : système, écran, fenêtre, pilote de rendu. Sert au
	// fichier diagnostic.txt et à la fenêtre « À propos ».
	String get_diagnostics() const;

	Size2 window_get_size() const; // taille de sortie du rendu, en pixels
	void window_set_title(const String &p_title);
	void window_set_fullscreen(bool p_fullscreen);
	bool window_is_fullscreen() const { return fullscreen; }

	// --- Taille et état de la fenêtre ---
	Size2 window_get_logical_size() const; // en unités de fenêtre (points)
	// Place disponible sur l'écran qui porte la fenêtre, hors barre des tâches, en unités de fenêtre.
	Size2 screen_get_usable_size() const;
	// Taille minimale, en unités de fenêtre ; ramenée à la place disponible à l'écran.
	void window_set_min_size(int p_width, int p_height);
	Size2 window_get_min_size() const { return Size2((real_t)min_width, (real_t)min_height); }
	void window_minimize();
	void window_set_maximized(bool p_maximized);
	bool window_is_maximized() const;
	bool window_is_minimized() const;

	// --- Barre de titre ---
	// Barre intégrée (fenêtre sans décoration du système) ou barre du système.
	void window_set_borderless(bool p_borderless);
	bool window_is_borderless() const { return borderless; }
	// Zones de la barre intégrée qui déplacent la fenêtre (double-clic : agrandir ou restaurer)
	// et épaisseur des bords qui la redimensionnent. À redonner quand l'interface change.
	void window_set_caption_areas(const std::vector<WindowRect> &p_areas, int p_resize_border);
	int window_get_resize_border() const { return resize_border; }
	// Barre du système : sombre ou claire ; couleurs exactes là où le système les accepte
	// (Windows 11). Sans effet sur une barre intégrée.
	void window_set_title_bar_theme(bool p_dark, const Color &p_background, const Color &p_text);
	// À appeler une fois par image : applique les demandes venues de la barre intégrée.
	void window_process_requests();
	// Rôle de ce point de la fenêtre (valeur de SDL_HitTestResult) : normal, déplacement,
	// redimensionnement. p_click : appui réel signalé par SDL (repère les doubles-clics).
	int window_hit_test(int p_x, int p_y, bool p_click = false);
	// Résolution logique : le jeu est mis à l'échelle (bandes noires si besoin).
	void set_logical_size(int p_width, int p_height);
	void present();
	Error save_screenshot(const String &p_path); // BMP

	// Convertit un événement SDL en InputEvent (nul si non pertinent).
	static Ref<InputEvent> translate_event(const SDL_Event &p_event);

	DisplayServer();
	~DisplayServer();
};
