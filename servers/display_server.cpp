#include "display_server.h"

#include "window_platform.h"

#include "core/input/input.h"

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

DisplayServer *DisplayServer::singleton = nullptr;

static SDL_HitTestResult SDLCALL _sdl_hit_test(SDL_Window *p_window, const SDL_Point *p_point, void *p_data);
namespace {
void _joypads_init();
}

DisplayServer::DisplayServer() {
	singleton = this;
}

DisplayServer::~DisplayServer() {
	destroy();
	if (singleton == this) {
		singleton = nullptr;
	}
}

// Échelle d'affichage du système pour l'écran p_display.
static float _detect_screen_scale(int p_display) {
	if (const char *forced = SDL_getenv("MEX_SCREEN_SCALE")) {
		const float value = (float)SDL_atof(forced);
		if (value >= 0.5f && value <= 4.0f) {
			return value;
		}
	}
#if defined(__APPLE__)
	return 1.0f; // la fenêtre est en points ; SDL fournit une surface de rendu plus dense
#elif defined(_WIN32)
	// Le processus étant déclaré « conscient de la densité », Windows donne ici 96 x l'échelle
	// choisie dans les paramètres d'affichage (120 pour 125 %, 144 pour 150 %...).
	float ddpi = 0, hdpi = 0, vdpi = 0;
	if (SDL_GetDisplayDPI(p_display, &ddpi, &hdpi, &vdpi) == 0 && hdpi > 0) {
		return SDL_clamp(hdpi / 96.0f, 1.0f, 4.0f);
	}
	return 1.0f;
#else
	// X11 ne donne pas d'échelle fiable : on se fie à la définition de l'écran. Sous Wayland,
	// la densité passe par get_pixel_ratio().
	const char *driver = SDL_GetCurrentVideoDriver();
	SDL_DisplayMode mode;
	if (driver && SDL_strcmp(driver, "x11") == 0 && SDL_GetDesktopDisplayMode(p_display, &mode) == 0) {
		if (mode.h >= 2000) {
			return 2.0f;
		}
		if (mode.h >= 1700) {
			return 1.5f;
		}
	}
	return 1.0f;
#endif
}

float DisplayServer::get_pixel_ratio() const {
	if (!window || !renderer) {
		return 1.0f;
	}
	int window_w = 0, window_h = 0, output_w = 0, output_h = 0;
	SDL_GetWindowSize(window, &window_w, &window_h);
	SDL_GetRendererOutputSize(renderer, &output_w, &output_h);
	return window_w > 0 && output_w > 0 ? (float)output_w / (float)window_w : 1.0f;
}

// ---------------------------------------------------------------------------
// Choix du pilote de rendu
// ---------------------------------------------------------------------------
#if defined(_WIN32)
#define MEX_DISPLAY_GL_CALL __stdcall
#else
#define MEX_DISPLAY_GL_CALL
#endif

// Le pilote OpenGL que SDL vient d'ouvrir est-il digne de confiance ? Sinon r_reason dit
// pourquoi. Un mauvais pilote OpenGL ne refuse pas de démarrer : il affiche une image fausse
// (vues blanches, image décalée, lenteur). On le repère donc avant de s'en servir.
static bool _opengl_is_reliable(SDL_Renderer *p_renderer, String &r_details, String &r_reason) {
	SDL_RendererInfo info;
	SDL_zero(info);
	SDL_GetRendererInfo(p_renderer, &info);
	typedef const unsigned char *(MEX_DISPLAY_GL_CALL * GetStringFn)(unsigned int);
	GetStringFn get_string = (GetStringFn)SDL_GL_GetProcAddress("glGetString");
	const char *version = get_string ? (const char *)get_string(0x1F02) : nullptr; // GL_VERSION
	const char *card = get_string ? (const char *)get_string(0x1F01) : nullptr; // GL_RENDERER
	const char *vendor = get_string ? (const char *)get_string(0x1F00) : nullptr; // GL_VENDOR
	r_details = String("OpenGL ") + (version ? version : "?") + " - " + (card ? card : "?") + " (" + (vendor ? vendor : "?") + ")";
	if (!version || !card) {
		r_reason = "le pilote OpenGL ne répond pas";
		return false;
	}
	if (strstr(card, "GDI Generic")) {
		// Windows sans pilote du fabricant de la carte : OpenGL 1.1 calculé par le processeur.
		r_reason = "aucun pilote OpenGL du fabricant de la carte graphique n'est installé (rendu de secours de Windows)";
		return false;
	}
	int major = 0, minor = 0;
	sscanf(version, "%d.%d", &major, &minor);
#if defined(_WIN32)
	// Sous Windows, Direct3D est toujours là et sûr : on n'accepte qu'un OpenGL récent. Les
	// pilotes OpenGL des anciennes puces (OpenGL 2.x et 3.0 à 3.2) y sont trop souvent fautifs.
	const int need_major = 3, need_minor = 3;
#else
	const int need_major = 2, need_minor = 1;
#endif
	if (major < need_major || (major == need_major && minor < need_minor)) {
		char text[96];
		snprintf(text, sizeof(text), "OpenGL %d.%d est trop ancien (%d.%d au moins)", major, minor, need_major, need_minor);
		r_reason = text;
		return false;
	}
	if (!(info.flags & SDL_RENDERER_TARGETTEXTURE)) {
		r_reason = "le pilote OpenGL ne sait pas dessiner dans une texture";
		return false;
	}
	return true;
}

String DisplayServer::get_compatible_render_driver() {
#if defined(_WIN32)
	return "direct3d";
#else
	return "software";
#endif
}

String DisplayServer::get_diagnostics() const {
	String text;
	char line[512];
	SDL_version linked;
	SDL_GetVersion(&linked);
	const char *video = SDL_GetCurrentVideoDriver();
	snprintf(line, sizeof(line), "Système : %s - SDL %d.%d.%d (%s)\n", SDL_GetPlatform(), linked.major, linked.minor, linked.patch, video ? video : "?");
	text += line;
	const int displays = SDL_GetNumVideoDisplays();
	for (int i = 0; i < displays; i++) {
		SDL_Rect bounds = { 0, 0, 0, 0 }, usable = { 0, 0, 0, 0 };
		SDL_GetDisplayBounds(i, &bounds);
		SDL_GetDisplayUsableBounds(i, &usable);
		float ddpi = 0, hdpi = 0, vdpi = 0;
		SDL_GetDisplayDPI(i, &ddpi, &hdpi, &vdpi);
		snprintf(line, sizeof(line), "Écran %d : %d x %d en (%d, %d) - zone utile %d x %d en (%d, %d) - %.0f ppp\n", i + 1, bounds.w, bounds.h, bounds.x, bounds.y, usable.w, usable.h, usable.x, usable.y, hdpi);
		text += line;
	}
	snprintf(line, sizeof(line), "Échelle d'affichage : %.0f %%\n", screen_scale * 100.0f);
	text += line;
	if (window) {
		int x = 0, y = 0, w = 0, h = 0, ow = 0, oh = 0;
		SDL_GetWindowPosition(window, &x, &y);
		SDL_GetWindowSize(window, &w, &h);
		if (renderer) {
			SDL_GetRendererOutputSize(renderer, &ow, &oh);
		}
		const Uint32 flags = SDL_GetWindowFlags(window);
		snprintf(line, sizeof(line), "Fenêtre : %d x %d en (%d, %d) - image %d x %d - écran %d - %s%s%s\n", w, h, x, y, ow, oh, SDL_GetWindowDisplayIndex(window) + 1,
				borderless ? "barre de titre intégrée" : "barre de titre du système",
				(flags & SDL_WINDOW_MAXIMIZED) ? ", agrandie" : "", fullscreen ? ", plein écran" : "");
		text += line;
	}
	text += String("Pilote de rendu : ") + (render_driver.empty() ? String("?") : render_driver);
	if (!render_details.empty()) {
		text += " - " + render_details;
	}
	text += "\n";
	if (!render_note.empty()) {
		text += "Remarque : " + render_note + "\n";
	}
	return text;
}

Error DisplayServer::create(const WindowOptions &p_options) {
	// Windows : sans cette déclaration, le système étire l'image de la fenêtre sur les écrans
	// réglés à 125 %, 150 %... et tout paraît flou. À faire avant l'initialisation de la vidéo.
	SDL_SetHint("SDL_WINDOWS_DPI_AWARENESS", "permonitorv2");
	if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
		if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
			print_error(String("SDL_Init: ") + SDL_GetError());
			return ERR_CANT_CREATE;
		}
	}
	_joypads_init();
	if (Input::get_singleton()) {
		Input::get_singleton()->set_touchscreen_present(SDL_GetNumTouchDevices() > 0);
	}
	screen_scale = _detect_screen_scale(0);
	int width = p_options.width, height = p_options.height;
	if (p_options.scale_with_screen && screen_scale > 1.0f) {
		width = (int)(width * screen_scale);
		height = (int)(height * screen_scale);
	}
	SDL_Rect usable;
	if (!p_options.fullscreen && SDL_GetDisplayUsableBounds(0, &usable) == 0 && usable.w > 0 && usable.h > 0 && (width > usable.w || height > usable.h)) {
		// Écran plus petit que la fenêtre demandée, quelle que soit l'échelle (portable en
		// 1366 x 768 par exemple) : la fenêtre ne dépasse pas de l'écran, sa barre du haut reste
		// accessible, et ses proportions sont conservées.
		const float fit = SDL_min((float)usable.w * 0.95f / (float)width, (float)usable.h * 0.9f / (float)height);
		if (fit < 1.0f) {
			width = SDL_max((int)(width * fit), 320);
			height = SDL_max((int)(height * fit), 200);
		}
	}
	// Fenêtre sans décoration sous Windows : elle garde le style d'une fenêtre ordinaire
	// (ancrage aux bords de l'écran, animations, ombre, redimensionnement par les bords).
	SDL_SetHint("SDL_BORDERLESS_WINDOWED_STYLE", "1");
	SDL_SetHint("SDL_BORDERLESS_RESIZABLE_STYLE", "1");
	// La fenêtre est créée cachée, puis montrée une fois entièrement réglée (barre de titre,
	// taille minimale, agrandissement) : elle n'apparaît jamais dans un état intermédiaire.
	Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN;
	if (p_options.resizable) {
		flags |= SDL_WINDOW_RESIZABLE;
	}
	if (p_options.fullscreen) {
		flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}
	if (p_options.borderless) {
		flags |= SDL_WINDOW_BORDERLESS;
	}
	fullscreen = p_options.fullscreen;

	Uint32 rflags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE;
	if (p_options.vsync) {
		rflags |= SDL_RENDERER_PRESENTVSYNC;
	}
	// Le rendu 3D, les shaders et les lumières 2D partagent le contexte OpenGL du moteur de
	// rendu 2D de SDL : OpenGL est donc essayé en premier. Mais un pilote OpenGL ancien ou
	// absent donne une image fausse au lieu d'échouer : il est contrôlé, et s'il n'est pas
	// fiable on prend le pilote habituel du système (Direct3D sous Windows, Metal sous macOS),
	// sans la 3D. Un pilote demandé (préférence, MEX_RENDER_DRIVER) est pris tel quel.
	String requested = p_options.render_driver;
	if (const char *forced = SDL_getenv("MEX_RENDER_DRIVER")) {
		if (*forced) {
			requested = forced;
		}
	}
	if (requested == "auto") {
		requested = String();
	}
	std::vector<String> candidates;
	if (!requested.empty()) {
		candidates.push_back(requested);
	}
#ifdef __EMSCRIPTEN__
	// Navigateur : un seul pilote existe (WebGL, « opengles2 » pour SDL).
	candidates.clear();
	candidates.push_back("");
#else
	for (const char *name : { "opengl", "", "software" }) {
		if (requested.empty() || requested != name) {
			candidates.push_back(name);
		}
	}
#endif
	render_driver = String();
	render_details = String();
	render_note = String();
	String last_error;
	for (size_t i = 0; i < candidates.size() && !renderer; i++) {
		const String &candidate = candidates[i];
		const bool is_requested = i == 0 && !requested.empty();
		// Une fenêtre neuve à chaque essai : SDL la transforme pour OpenGL, et un autre pilote
		// ne doit pas hériter d'une fenêtre déjà liée à OpenGL.
		window = SDL_CreateWindow(p_options.title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, flags);
		if (!window) {
			last_error = String("SDL_CreateWindow: ") + SDL_GetError();
			continue;
		}
		SDL_SetHint(SDL_HINT_RENDER_DRIVER, candidate.c_str());
		renderer = SDL_CreateRenderer(window, -1, candidate == "software" ? (Uint32)(SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE) : rflags);
		if (!renderer) {
			last_error = String("SDL_CreateRenderer: ") + SDL_GetError();
			SDL_DestroyWindow(window);
			window = nullptr;
			if (is_requested) {
				render_note = "le pilote de rendu « " + requested + " » demandé n'a pas pu être ouvert";
			}
			continue;
		}
		SDL_RendererInfo info;
		SDL_zero(info);
		SDL_GetRendererInfo(renderer, &info);
		const String actual = info.name ? info.name : "";
		String details, reason;
		if (actual == "opengl") {
			const bool reliable = _opengl_is_reliable(renderer, details, reason);
			// Pilote choisi par l'utilisateur : gardé même s'il est jugé peu fiable.
			if (!reliable && !(is_requested && requested == "opengl")) {
				render_note = "OpenGL écarté : " + reason + " [" + details + "]. Rendu 3D, shaders et lumières 2D désactivés.";
				SDL_DestroyRenderer(renderer);
				renderer = nullptr;
				SDL_DestroyWindow(window);
				window = nullptr;
				continue;
			}
		}
		if (is_requested && actual != requested) {
			render_note = "le pilote de rendu « " + requested + " » demandé n'a pas pu être ouvert";
		}
		render_driver = actual;
		render_details = details;
	}
	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "");
	if (!renderer || !window) {
		print_error(last_error.empty() ? String("Aucun pilote de rendu utilisable.") : last_error);
		return ERR_CANT_CREATE;
	}
	if (!render_note.empty() && !p_options.hidden) {
		print_warning("Affichage : " + render_note + " Pilote utilisé : " + render_driver + ".");
	}

	// Réglages de la fenêtre, après la création du moteur de rendu : SDL peut recréer la
	// fenêtre du système à ce moment-là (passage à OpenGL).
	resizable = p_options.resizable;
	borderless = p_options.borderless;
	dark_title_bar = p_options.dark_title_bar;
	WindowPlatform::install_maximize_fix(window);
	if (borderless) {
		SDL_SetWindowHitTest(window, _sdl_hit_test, this);
	}
	_apply_title_bar_theme();
	min_width = p_options.min_width;
	min_height = p_options.min_height;
	if (p_options.scale_with_screen && screen_scale > 1.0f) {
		min_width = (int)((float)min_width * screen_scale);
		min_height = (int)((float)min_height * screen_scale);
	}
	_apply_min_size();
	if (p_options.maximized && !p_options.fullscreen) {
		SDL_MaximizeWindow(window);
	}
	if (!p_options.hidden) {
		SDL_ShowWindow(window);
	}
	// Windows : le cadre est recalculé une fois la fenêtre montrée et le rendu en place, pour
	// que l'image couvre exactement la fenêtre sans décoration.
	WindowPlatform::refresh_frame(window);
	return OK;
}

// ---------------------------------------------------------------------------
// Taille et état de la fenêtre
// ---------------------------------------------------------------------------
Size2 DisplayServer::window_get_logical_size() const {
	int w = 0, h = 0;
	if (window) {
		SDL_GetWindowSize(window, &w, &h);
	}
	return Size2((real_t)w, (real_t)h);
}

Size2 DisplayServer::screen_get_usable_size() const {
	SDL_Rect usable;
	const int index = window ? std::max(SDL_GetWindowDisplayIndex(window), 0) : 0;
	if (SDL_GetDisplayUsableBounds(index, &usable) == 0 && usable.w > 0 && usable.h > 0) {
		return Size2((real_t)usable.w, (real_t)usable.h);
	}
	SDL_DisplayMode mode;
	if (SDL_GetDesktopDisplayMode(index, &mode) == 0) {
		return Size2((real_t)mode.w, (real_t)mode.h);
	}
	return Size2();
}

void DisplayServer::_apply_min_size() {
	if (!window) {
		return;
	}
	int w = std::max(min_width, 1), h = std::max(min_height, 1);
	// Jamais plus que la place disponible : sinon la fenêtre ne tiendrait pas sur un petit écran.
	const Size2 usable = screen_get_usable_size();
	if (usable.x > 0 && usable.y > 0) {
		// Une barre de titre du système prend sa part de la hauteur disponible.
		int top = 0;
		if (!borderless) {
			SDL_GetWindowBordersSize(window, &top, nullptr, nullptr, nullptr);
		}
		w = std::min(w, (int)usable.x);
		h = std::min(h, std::max((int)usable.y - top, 1));
	}
	SDL_SetWindowMinimumSize(window, w, h); // agrandit aussi la fenêtre si elle est plus petite
}

void DisplayServer::window_set_min_size(int p_width, int p_height) {
	if (p_width == min_width && p_height == min_height) {
		return;
	}
	min_width = p_width;
	min_height = p_height;
	_apply_min_size();
}

void DisplayServer::window_minimize() {
	if (window) {
		SDL_MinimizeWindow(window);
	}
}

void DisplayServer::window_set_maximized(bool p_maximized) {
	if (!window || fullscreen) {
		return;
	}
	if (p_maximized) {
		SDL_MaximizeWindow(window);
	} else {
		SDL_RestoreWindow(window);
	}
}

bool DisplayServer::window_is_maximized() const {
	return window && (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
}

bool DisplayServer::window_is_minimized() const {
	return window && (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0;
}

// ---------------------------------------------------------------------------
// Barre de titre
// ---------------------------------------------------------------------------
static SDL_HitTestResult SDLCALL _sdl_hit_test(SDL_Window *, const SDL_Point *p_point, void *p_data) {
	return (SDL_HitTestResult)((DisplayServer *)p_data)->window_hit_test(p_point->x, p_point->y, true);
}

int DisplayServer::window_hit_test(int p_x, int p_y, bool p_click) {
	if (!window || !borderless || fullscreen) {
		return SDL_HITTEST_NORMAL;
	}
	int w = 0, h = 0;
	SDL_GetWindowSize(window, &w, &h);
	// Bords et coins : redimensionnement (sauf fenêtre agrandie, qui occupe tout l'écran).
	if (resizable && resize_border > 0 && !window_is_maximized()) {
		const int edge = resize_border, corner = resize_border * 3;
		const bool left = p_x < edge, right = p_x >= w - edge, top = p_y < edge, bottom = p_y >= h - edge;
		if (left || right || top || bottom) {
			const bool near_left = p_x < corner, near_right = p_x >= w - corner;
			const bool near_top = p_y < corner, near_bottom = p_y >= h - corner;
			if (near_top && near_left) {
				return SDL_HITTEST_RESIZE_TOPLEFT;
			}
			if (near_top && near_right) {
				return SDL_HITTEST_RESIZE_TOPRIGHT;
			}
			if (near_bottom && near_left) {
				return SDL_HITTEST_RESIZE_BOTTOMLEFT;
			}
			if (near_bottom && near_right) {
				return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
			}
			return top ? SDL_HITTEST_RESIZE_TOP : bottom ? SDL_HITTEST_RESIZE_BOTTOM : left ? SDL_HITTEST_RESIZE_LEFT : SDL_HITTEST_RESIZE_RIGHT;
		}
	}
	for (const WindowRect &area : caption_areas) {
		if (p_x < area.x || p_y < area.y || p_x >= area.x + area.w || p_y >= area.y + area.h) {
			continue;
		}
#if defined(_WIN32)
		(void)p_click; // Windows agrandit lui-même la fenêtre au double-clic sur une zone de titre
#else
		// Ailleurs, SDL appelle cette fonction à chaque appui : deux appuis rapprochés font
		// un double-clic.
		if (!p_click) {
			return SDL_HITTEST_DRAGGABLE;
		}
		const uint32_t now = SDL_GetTicks();
		if (caption_click_ticks != 0 && now - caption_click_ticks < 400 && std::abs(p_x - caption_click_x) < 6 && std::abs(p_y - caption_click_y) < 6) {
			caption_click_ticks = 0;
			maximize_toggle_pending = true;
			return SDL_HITTEST_NORMAL;
		}
		caption_click_ticks = now == 0 ? 1 : now;
		caption_click_x = p_x;
		caption_click_y = p_y;
#endif
		return SDL_HITTEST_DRAGGABLE;
	}
	return SDL_HITTEST_NORMAL;
}

void DisplayServer::window_set_borderless(bool p_borderless) {
	if (!window || p_borderless == borderless) {
		return;
	}
	// Fenêtre agrandie : SDL garde la surface de dessin et fait grandir la fenêtre autour d'elle,
	// si bien que la barre de titre du système se retrouverait au-dessus de l'écran. On la
	// restaure le temps du changement, puis on l'agrandit de nouveau avec sa nouvelle décoration.
	const bool was_maximized = !fullscreen && window_is_maximized();
	if (was_maximized) {
		SDL_RestoreWindow(window);
	}
	borderless = p_borderless;
	SDL_SetWindowBordered(window, borderless ? SDL_FALSE : SDL_TRUE);
	SDL_SetWindowHitTest(window, borderless ? _sdl_hit_test : nullptr, this);
	_apply_title_bar_theme();
	_apply_min_size();
	WindowPlatform::refresh_frame(window);
	if (was_maximized) {
		SDL_MaximizeWindow(window);
	}
}

void DisplayServer::window_set_caption_areas(const std::vector<WindowRect> &p_areas, int p_resize_border) {
	caption_areas = p_areas;
	resize_border = p_resize_border;
}

void DisplayServer::_apply_title_bar_theme() {
	if (!window) {
		return;
	}
	const auto to_rgb = [](const Color &p_color) -> uint32_t {
		const auto channel = [](float p_value) -> uint32_t {
			return (uint32_t)(std::clamp(p_value, 0.0f, 1.0f) * 255.0f + 0.5f);
		};
		return (channel(p_color.r) << 16) | (channel(p_color.g) << 8) | channel(p_color.b);
	};
	WindowPlatform::set_title_bar_theme(window, dark_title_bar, title_bar_has_colors, to_rgb(title_bar_background), to_rgb(title_bar_text));
}

void DisplayServer::window_set_title_bar_theme(bool p_dark, const Color &p_background, const Color &p_text) {
	dark_title_bar = p_dark;
	title_bar_has_colors = true;
	title_bar_background = p_background;
	title_bar_text = p_text;
	_apply_title_bar_theme();
}

void DisplayServer::window_process_requests() {
	// Double-clic sur la barre intégrée : on attend que le bouton soit relâché. Changer la
	// taille de la fenêtre sous un bouton encore enfoncé serait pris pour un glisser.
	if (maximize_toggle_pending && (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) == 0) {
		maximize_toggle_pending = false;
		window_set_maximized(!window_is_maximized());
	}
}

void DisplayServer::set_icon(const uint8_t *p_rgba, int p_width, int p_height) {
	if (!window || !p_rgba || p_width <= 0 || p_height <= 0) {
		return;
	}
	// SDL copie les pixels : la surface peut être libérée aussitôt.
	SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom((void *)p_rgba, p_width, p_height, 32, p_width * 4, SDL_PIXELFORMAT_RGBA32);
	if (surface) {
		SDL_SetWindowIcon(window, surface);
		SDL_FreeSurface(surface);
	}
}

void DisplayServer::destroy() {
	if (window) {
		WindowPlatform::uninstall_maximize_fix(window);
	}
	if (renderer) {
		SDL_DestroyRenderer(renderer);
		renderer = nullptr;
	}
	if (window) {
		SDL_DestroyWindow(window);
		window = nullptr;
	}
}

Size2 DisplayServer::window_get_size() const {
	int w = 0, h = 0;
	if (renderer) {
		SDL_GetRendererOutputSize(renderer, &w, &h);
	}
	return Size2((real_t)w, (real_t)h);
}

void DisplayServer::window_set_title(const String &p_title) {
	if (window) {
		SDL_SetWindowTitle(window, p_title.c_str());
	}
}

void DisplayServer::window_set_fullscreen(bool p_fullscreen) {
	if (window) {
		SDL_SetWindowFullscreen(window, p_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
		fullscreen = p_fullscreen;
	}
}

void DisplayServer::set_logical_size(int p_width, int p_height) {
	if (renderer) {
		SDL_RenderSetLogicalSize(renderer, p_width, p_height);
	}
}

void DisplayServer::present() {
	if (renderer) {
		SDL_RenderPresent(renderer);
	}
}

Error DisplayServer::save_screenshot(const String &p_path) {
	ERR_FAIL_NULL_V(renderer, FAILED);
	SDL_SetRenderTarget(renderer, nullptr);
	int w = 0, h = 0;
	SDL_GetRendererOutputSize(renderer, &w, &h);
	SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	ERR_FAIL_NULL_V(surface, FAILED);
	// Lit en coordonnées réelles (ignore la résolution logique).
	int lw = 0, lh = 0;
	SDL_RenderGetLogicalSize(renderer, &lw, &lh);
	SDL_RenderSetLogicalSize(renderer, 0, 0);
	int res = SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch);
	if (lw > 0 && lh > 0) {
		SDL_RenderSetLogicalSize(renderer, lw, lh);
	}
	Error err = FAILED;
	if (res == 0 && SDL_SaveBMP(surface, p_path.c_str()) == 0) {
		err = OK;
	}
	SDL_FreeSurface(surface);
	return err;
}

// ---------------------------------------------------------------------------
// Manettes : SDL_GameController, qui donne à toutes les manettes la disposition d'une manette
// Xbox (c'est aussi la base de données de correspondances qu'utilise Godot).
// ---------------------------------------------------------------------------
namespace {
struct OpenJoypad {
	SDL_GameController *controller = nullptr;
	SDL_JoystickID instance = -1;
};
std::map<int, OpenJoypad> open_joypads; // numéro de manette (0, 1...) -> manette ouverte
SDL_JoystickID virtual_joypad_instance = -1; // manette virtuelle des essais

int _joypad_device(SDL_JoystickID p_instance) {
	for (const auto &kv : open_joypads) {
		if (kv.second.instance == p_instance) {
			return kv.first;
		}
	}
	return -1;
}

void _joypad_open(int p_sdl_index) {
	if (!SDL_IsGameController(p_sdl_index)) {
		return; // volants, manches à balai sans correspondance connue : ignorés
	}
	SDL_GameController *controller = SDL_GameControllerOpen(p_sdl_index);
	if (!controller) {
		return;
	}
	const SDL_JoystickID instance = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));
	if (_joypad_device(instance) >= 0) {
		return; // déjà ouverte
	}
	int device = 0;
	while (open_joypads.count(device)) {
		device++; // premier numéro libre, comme Godot
	}
	open_joypads[device] = { controller, instance };
	char guid[64] = "";
	SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(SDL_GameControllerGetJoystick(controller)), guid, sizeof(guid));
	const char *name = SDL_GameControllerName(controller);
	if (Input::get_singleton()) {
		Input::get_singleton()->joy_connection_changed(device, true, name ? name : "Manette", guid);
	}
}

void _joypad_close(SDL_JoystickID p_instance) {
	const int device = _joypad_device(p_instance);
	if (device < 0) {
		return;
	}
	SDL_GameControllerClose(open_joypads[device].controller);
	open_joypads.erase(device);
	if (Input::get_singleton()) {
		Input::get_singleton()->joy_connection_changed(device, false);
	}
}

void _joypads_init() {
	if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) != 0) {
		return;
	}
	if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
		print_warning(String("Manettes indisponibles : ") + SDL_GetError());
		return;
	}
	Input *input = Input::get_singleton();
	if (!input) {
		return;
	}
	input->set_joy_vibration_handler([](int p_device, float p_weak, float p_strong, float p_duration) {
		const auto it = open_joypads.find(p_device);
		if (it != open_joypads.end()) {
			// SDL : moteur basse fréquence (fort) puis haute fréquence (faible) ; 0 ms = arrêt.
			const Uint32 ms = (p_weak <= 0 && p_strong <= 0) ? 0 : (p_duration > 0 ? (Uint32)(p_duration * 1000.0f) : SDL_HAPTIC_INFINITY);
			SDL_GameControllerRumble(it->second.controller, (Uint16)(p_strong * 65535.0f), (Uint16)(p_weak * 65535.0f), ms);
		}
	});
	// Manette virtuelle pour les essais automatiques : MEX_VIRTUAL_JOYPAD=1.
	const char *wanted = SDL_getenv("MEX_VIRTUAL_JOYPAD");
	if (wanted && wanted[0] == '1') {
		const int index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
		if (index < 0) {
			print_warning(String("Manette virtuelle impossible : ") + SDL_GetError());
		} else {
			virtual_joypad_instance = SDL_JoystickGetDeviceInstanceID(index);
			input->set_virtual_joy_handler([](int p_kind, int p_index, float p_value) {
				const int device = _joypad_device(virtual_joypad_instance);
				if (device < 0) {
					return;
				}
				if (p_kind == 2) {
					// Débranchement : SDL envoie SDL_CONTROLLERDEVICEREMOVED comme pour une vraie manette.
					const int count = SDL_NumJoysticks();
					for (int i = 0; i < count; i++) {
						if (SDL_JoystickGetDeviceInstanceID(i) == virtual_joypad_instance) {
							SDL_JoystickDetachVirtual(i);
							break;
						}
					}
					return;
				}
				SDL_Joystick *joystick = SDL_GameControllerGetJoystick(open_joypads[device].controller);
				if (p_kind == 0) {
					SDL_JoystickSetVirtualButton(joystick, p_index, p_value > 0.5f ? 1 : 0);
				} else {
					SDL_JoystickSetVirtualAxis(joystick, p_index, (Sint16)(std::min(std::max(p_value, -1.0f), 1.0f) * 32767.0f));
				}
			});
		}
	}
}
} // namespace

static int _translate_keycode(SDL_Keycode p_sym) {
	switch (p_sym) {
		case SDLK_RCTRL: return KEY_CTRL;
		case SDLK_RSHIFT: return KEY_SHIFT;
		case SDLK_RALT: return KEY_ALT;
		case SDLK_KP_ENTER: return KEY_ENTER;
		default: return (int)p_sym;
	}
}

Ref<InputEvent> DisplayServer::translate_event(const SDL_Event &e) {
	switch (e.type) {
		case SDL_KEYDOWN:
		case SDL_KEYUP: {
			Ref<InputEventKey> ev;
			ev.instantiate();
			ev->set_keycode(_translate_keycode(e.key.keysym.sym));
			ev->set_pressed(e.type == SDL_KEYDOWN);
			ev->set_echo(e.key.repeat != 0);
			return ev;
		}
		case SDL_TEXTINPUT: {
			Ref<InputEventKey> ev;
			ev.instantiate();
			ev->set_keycode(KEY_NONE);
			ev->set_pressed(true);
			ev->set_text(e.text.text);
			return ev;
		}
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP: {
			Ref<InputEventMouseButton> ev;
			ev.instantiate();
			int button = MOUSE_BUTTON_NONE;
			switch (e.button.button) {
				case SDL_BUTTON_LEFT: button = MOUSE_BUTTON_LEFT; break;
				case SDL_BUTTON_RIGHT: button = MOUSE_BUTTON_RIGHT; break;
				case SDL_BUTTON_MIDDLE: button = MOUSE_BUTTON_MIDDLE; break;
				default: return Ref<InputEvent>();
			}
			ev->set_button_index(button);
			ev->set_pressed(e.type == SDL_MOUSEBUTTONDOWN);
			ev->set_double_click(e.button.clicks >= 2);
			ev->set_position(Vector2((real_t)e.button.x, (real_t)e.button.y));
			return ev;
		}
		case SDL_MOUSEWHEEL: {
			if (e.wheel.y == 0) {
				return Ref<InputEvent>();
			}
			Ref<InputEventMouseButton> ev;
			ev.instantiate();
			ev->set_button_index(e.wheel.y > 0 ? MOUSE_BUTTON_WHEEL_UP : MOUSE_BUTTON_WHEEL_DOWN);
			ev->set_pressed(true);
			int mx = 0, my = 0;
			SDL_GetMouseState(&mx, &my);
			ev->set_position(Vector2((real_t)mx, (real_t)my));
			return ev;
		}
		case SDL_FINGERDOWN:
		case SDL_FINGERUP:
		case SDL_FINGERMOTION: {
			if (e.tfinger.touchId == SDL_MOUSE_TOUCHID) {
				return Ref<InputEvent>(); // souris déguisée en doigt par SDL : MEX a sa propre émulation
			}
			// SDL donne des coordonnées de 0 à 1, rapportées à l'image du jeu quand une
			// résolution logique est fixée, sinon à la fenêtre.
			int w = 0, h = 0;
			if (singleton && singleton->renderer) {
				SDL_RenderGetLogicalSize(singleton->renderer, &w, &h);
			}
			if ((w <= 0 || h <= 0) && singleton && singleton->window) {
				SDL_GetWindowSize(singleton->window, &w, &h);
			}
			// Numéros de doigts 0, 1, 2... dans l'ordre où ils se posent, comme dans Godot.
			static std::map<SDL_FingerID, int> fingers;
			int index = 0;
			const auto known = fingers.find(e.tfinger.fingerId);
			if (known != fingers.end()) {
				index = known->second;
			} else {
				std::vector<bool> used(fingers.size() + 1, false);
				for (const auto &kv : fingers) {
					if (kv.second < (int)used.size()) {
						used[kv.second] = true;
					}
				}
				while (used[index]) {
					index++;
				}
				if (e.type != SDL_FINGERUP) {
					fingers[e.tfinger.fingerId] = index;
				}
			}
			if (Input::get_singleton()) {
				Input::get_singleton()->set_touchscreen_present(true);
			}
			const Vector2 position(e.tfinger.x * w, e.tfinger.y * h);
			if (e.type == SDL_FINGERMOTION) {
				Ref<InputEventScreenDrag> ev;
				ev.instantiate();
				ev->set_index(index);
				ev->set_position(position);
				ev->set_relative(Vector2(e.tfinger.dx * w, e.tfinger.dy * h));
				ev->set_pressure(e.tfinger.pressure);
				return ev;
			}
			if (e.type == SDL_FINGERUP) {
				fingers.erase(e.tfinger.fingerId);
			}
			Ref<InputEventScreenTouch> ev;
			ev.instantiate();
			ev->set_index(index);
			ev->set_position(position);
			ev->set_pressed(e.type == SDL_FINGERDOWN);
			return ev;
		}
		case SDL_CONTROLLERDEVICEADDED: {
			_joypad_open(e.cdevice.which);
			return Ref<InputEvent>();
		}
		case SDL_CONTROLLERDEVICEREMOVED: {
			_joypad_close(e.cdevice.which);
			return Ref<InputEvent>();
		}
		case SDL_CONTROLLERBUTTONDOWN:
		case SDL_CONTROLLERBUTTONUP: {
			const int device = _joypad_device(e.cbutton.which);
			if (device < 0) {
				return Ref<InputEvent>();
			}
			Ref<InputEventJoypadButton> ev;
			ev.instantiate();
			ev->set_device(device);
			ev->set_button_index(e.cbutton.button); // même numérotation que JoyButton
			ev->set_pressed(e.type == SDL_CONTROLLERBUTTONDOWN);
			ev->set_pressure(e.type == SDL_CONTROLLERBUTTONDOWN ? 1.0f : 0.0f);
			return ev;
		}
		case SDL_CONTROLLERAXISMOTION: {
			const int device = _joypad_device(e.caxis.which);
			if (device < 0) {
				return Ref<InputEvent>();
			}
			Ref<InputEventJoypadMotion> ev;
			ev.instantiate();
			ev->set_device(device);
			ev->set_axis(e.caxis.axis); // même numérotation que JoyAxis
			ev->set_axis_value(e.caxis.value < 0 ? e.caxis.value / 32768.0f : e.caxis.value / 32767.0f);
			return ev;
		}
		case SDL_MOUSEMOTION: {
			Ref<InputEventMouseMotion> ev;
			ev.instantiate();
			ev->set_position(Vector2((real_t)e.motion.x, (real_t)e.motion.y));
			ev->set_relative(Vector2((real_t)e.motion.xrel, (real_t)e.motion.yrel));
			return ev;
		}
		default:
			return Ref<InputEvent>();
	}
}
