#include "display_server.h"

#include <SDL.h>

DisplayServer *DisplayServer::singleton = nullptr;

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
	screen_scale = _detect_screen_scale(0);
	int width = p_options.width, height = p_options.height;
	if (p_options.scale_with_screen && screen_scale > 1.0f) {
		width = (int)(width * screen_scale);
		height = (int)(height * screen_scale);
		SDL_Rect usable;
		if (SDL_GetDisplayUsableBounds(0, &usable) == 0 && usable.w > 0 && usable.h > 0) {
			// La fenêtre ne dépasse pas de l'écran ; ses proportions sont conservées.
			const float fit = SDL_min((float)usable.w * 0.95f / (float)width, (float)usable.h * 0.9f / (float)height);
			if (fit < 1.0f) {
				width = (int)(width * fit);
				height = (int)(height * fit);
			}
		}
	}
	Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI;
	if (p_options.resizable) {
		flags |= SDL_WINDOW_RESIZABLE;
	}
	if (p_options.maximized) {
		flags |= SDL_WINDOW_MAXIMIZED;
	}
	if (p_options.fullscreen) {
		flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}
	if (p_options.hidden) {
		flags |= SDL_WINDOW_HIDDEN;
	}
	window = SDL_CreateWindow(p_options.title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, flags);
	if (!window) {
		print_error(String("SDL_CreateWindow: ") + SDL_GetError());
		return ERR_CANT_CREATE;
	}
	fullscreen = p_options.fullscreen;

	Uint32 rflags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE;
	if (p_options.vsync) {
		rflags |= SDL_RENDERER_PRESENTVSYNC;
	}
	// Le rendu 3D partage le contexte OpenGL du moteur de rendu 2D de SDL : on demande donc le
	// pilote « opengl » partout (Direct3D et Metal sont sinon choisis par défaut sous Windows
	// et macOS). MEX_RENDER_DRIVER permet de forcer un autre pilote (« software », « direct3d »...)
	// au prix de la 3D. Si le pilote demandé échoue, SDL essaie les autres.
	const char *driver = SDL_getenv("MEX_RENDER_DRIVER");
	SDL_SetHint(SDL_HINT_RENDER_DRIVER, driver && *driver ? driver : "opengl");
	renderer = SDL_CreateRenderer(window, -1, rflags);
	if (!renderer) {
		// Repli sur le rendu logiciel (machines sans GPU, mode sans affichage).
		renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
	}
	if (!renderer) {
		print_error(String("SDL_CreateRenderer: ") + SDL_GetError());
		return ERR_CANT_CREATE;
	}
	return OK;
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
