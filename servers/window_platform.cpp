#include "window_platform.h"

#include <SDL.h>
#include <SDL_syswm.h>

namespace WindowPlatform {

#if defined(_WIN32)

// ---------------------------------------------------------------------------
// Windows
// ---------------------------------------------------------------------------
static SDL_Window *hooked_window = nullptr;
static WNDPROC previous_proc = nullptr;

static HWND _get_hwnd(SDL_Window *p_window) {
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (!p_window || !SDL_GetWindowWMInfo(p_window, &info) || info.subsystem != SDL_SYSWM_WINDOWS) {
		return nullptr;
	}
	return info.info.win.window;
}

void set_title_bar_theme(SDL_Window *p_window, bool p_dark, bool p_has_colors, uint32_t p_background, uint32_t p_text) {
	HWND hwnd = _get_hwnd(p_window);
	if (!hwnd) {
		return;
	}
	// dwmapi.dll est chargée à l'exécution : aucune dépendance de plus à l'édition de liens,
	// et les versions de Windows qui ignorent ces attributs renvoient simplement une erreur.
	void *library = SDL_LoadObject("dwmapi.dll");
	if (!library) {
		SDL_ClearError();
		return;
	}
	typedef HRESULT(WINAPI * SetAttribute)(HWND, DWORD, LPCVOID, DWORD);
	SetAttribute set_attribute = (SetAttribute)SDL_LoadFunction(library, "DwmSetWindowAttribute");
	if (set_attribute) {
		// DWMWA_USE_IMMERSIVE_DARK_MODE : 20 depuis Windows 10 2004, 19 sur les versions 1809 à 1909.
		const BOOL dark = p_dark ? TRUE : FALSE;
		if (FAILED(set_attribute(hwnd, 20, &dark, sizeof(dark)))) {
			set_attribute(hwnd, 19, &dark, sizeof(dark));
		}
		// Windows 11 : DWMWA_BORDER_COLOR (34), DWMWA_CAPTION_COLOR (35), DWMWA_TEXT_COLOR (36).
		// Une COLORREF se lit 0x00BBGGRR ; 0xFFFFFFFF rend la couleur par défaut.
		const auto to_colorref = [](uint32_t p_rgb) -> COLORREF {
			return RGB((p_rgb >> 16) & 0xFF, (p_rgb >> 8) & 0xFF, p_rgb & 0xFF);
		};
		const COLORREF background = p_has_colors ? to_colorref(p_background) : 0xFFFFFFFF;
		const COLORREF text = p_has_colors ? to_colorref(p_text) : 0xFFFFFFFF;
		set_attribute(hwnd, 35, &background, sizeof(background));
		set_attribute(hwnd, 34, &background, sizeof(background));
		set_attribute(hwnd, 36, &text, sizeof(text));
		// Windows 10 ne redessine la barre qu'au prochain changement de cadre.
		SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	} else {
		SDL_ClearError();
	}
	SDL_UnloadObject(library);
}

// La fenêtre porte-t-elle la barre de titre intégrée (sans décoration, hors plein écran) ?
static bool _is_borderless() {
	if (!hooked_window) {
		return false;
	}
	const Uint32 flags = SDL_GetWindowFlags(hooked_window);
	return (flags & SDL_WINDOW_BORDERLESS) && !(flags & SDL_WINDOW_FULLSCREEN);
}

static LRESULT CALLBACK _window_proc(HWND p_hwnd, UINT p_message, WPARAM p_wparam, LPARAM p_lparam) {
	const bool custom_frame = _is_borderless();
	if (custom_frame && p_message == WM_NCACTIVATE) {
		// -1 : Windows ne redessine pas son cadre quand la fenêtre gagne ou perd le focus. Sans
		// cela, il peut peindre des morceaux de sa barre de titre par-dessus celle de l'éditeur.
		p_lparam = -1;
	}
	if (custom_frame && (p_message == 0x00AE || p_message == 0x00AF)) {
		// WM_NCUAHDRAWCAPTION et WM_NCUAHDRAWFRAME (non documentés) : Windows y dessine son cadre
		// à thème. La fenêtre n'en a pas : les ignorer évite qu'il soit peint sur l'image.
		return 0;
	}
	const LRESULT result = CallWindowProcW(previous_proc, p_hwnd, p_message, p_wparam, p_lparam);
	if (custom_frame && p_message == WM_NCCALCSIZE && p_wparam == TRUE && IsZoomed(p_hwnd)) {
		// Fenêtre agrandie : Windows la fait déborder de l'écran de l'épaisseur de son cadre
		// (8 pixels de chaque côté à 100 %). Toute la fenêtre étant ici zone de dessin, l'image
		// serait rognée sur les quatre bords : on la ramène à la zone de travail de l'écran.
		MONITORINFO monitor;
		monitor.cbSize = sizeof(monitor);
		if (GetMonitorInfoW(MonitorFromWindow(p_hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
			RECT *client = &((NCCALCSIZE_PARAMS *)p_lparam)->rgrc[0];
			client->left = SDL_max(client->left, monitor.rcWork.left);
			client->top = SDL_max(client->top, monitor.rcWork.top);
			client->right = SDL_min(client->right, monitor.rcWork.right);
			client->bottom = SDL_min(client->bottom, monitor.rcWork.bottom);
		}
		return 0;
	}
	if (p_message == WM_GETMINMAXINFO && hooked_window) {
		const Uint32 flags = SDL_GetWindowFlags(hooked_window);
		if ((flags & SDL_WINDOW_BORDERLESS) && !(flags & SDL_WINDOW_FULLSCREEN)) {
			// SDL donne à une fenêtre sans décoration agrandie la taille de l'écran entier.
			// On la ramène à la zone de travail : la barre des tâches reste visible. Les
			// coordonnées sont relatives à l'écran qui porte la fenêtre.
			MONITORINFO monitor;
			monitor.cbSize = sizeof(monitor);
			if (GetMonitorInfoW(MonitorFromWindow(p_hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
				MINMAXINFO *info = (MINMAXINFO *)p_lparam;
				info->ptMaxPosition.x = monitor.rcWork.left - monitor.rcMonitor.left;
				info->ptMaxPosition.y = monitor.rcWork.top - monitor.rcMonitor.top;
				info->ptMaxSize.x = monitor.rcWork.right - monitor.rcWork.left;
				info->ptMaxSize.y = monitor.rcWork.bottom - monitor.rcWork.top;
				if (EqualRect(&monitor.rcWork, &monitor.rcMonitor)) {
					// Barre des tâches masquée automatiquement : une fenêtre qui couvre l'écran au
					// pixel près passe pour du plein écran (la barre des tâches ne revient plus, et
					// certains pilotes OpenGL changent de mode d'affichage). Un pixel de moins suffit.
					info->ptMaxSize.y -= 1;
				}
			}
		}
	}
	return result;
}

void install_maximize_fix(SDL_Window *p_window) {
	HWND hwnd = _get_hwnd(p_window);
	if (!hwnd || hooked_window) {
		return;
	}
	previous_proc = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
	if (!previous_proc) {
		return;
	}
	hooked_window = p_window;
	SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)_window_proc);
}

void uninstall_maximize_fix(SDL_Window *p_window) {
	if (!hooked_window || hooked_window != p_window) {
		return;
	}
	if (HWND hwnd = _get_hwnd(p_window)) {
		SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)previous_proc);
	}
	hooked_window = nullptr;
	previous_proc = nullptr;
}

void refresh_frame(SDL_Window *p_window) {
	if (HWND hwnd = _get_hwnd(p_window)) {
		SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	}
}

#elif defined(SDL_VIDEO_DRIVER_X11) && !defined(__APPLE__)

// ---------------------------------------------------------------------------
// Linux (X11)
// ---------------------------------------------------------------------------
void set_title_bar_theme(SDL_Window *p_window, bool p_dark, bool, uint32_t, uint32_t) {
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (!p_window || !SDL_GetWindowWMInfo(p_window, &info) || info.subsystem != SDL_SYSWM_X11) {
		return; // Wayland : la décoration est celle du compositeur ou de libdecor
	}
	// Propriété _GTK_THEME_VARIANT : les gestionnaires de fenêtres qui la connaissent dessinent
	// la barre avec la variante sombre de leur thème. Xlib est chargée à l'exécution, comme le
	// fait SDL : le moteur ne dépend pas de libX11 à l'édition de liens.
	void *library = SDL_LoadObject("libX11.so.6");
	if (!library) {
		SDL_ClearError();
		return;
	}
	typedef Atom (*InternAtom)(Display *, const char *, Bool);
	typedef int (*ChangeProperty)(Display *, Window, Atom, Atom, int, int, const unsigned char *, int);
	typedef int (*DeleteProperty)(Display *, Window, Atom);
	typedef int (*Flush)(Display *);
	InternAtom intern_atom = (InternAtom)SDL_LoadFunction(library, "XInternAtom");
	ChangeProperty change_property = (ChangeProperty)SDL_LoadFunction(library, "XChangeProperty");
	DeleteProperty delete_property = (DeleteProperty)SDL_LoadFunction(library, "XDeleteProperty");
	Flush flush = (Flush)SDL_LoadFunction(library, "XFlush");
	if (intern_atom && change_property && delete_property && flush) {
		Display *display = info.info.x11.display;
		const Window window = info.info.x11.window;
		const Atom property = intern_atom(display, "_GTK_THEME_VARIANT", False);
		const Atom utf8 = intern_atom(display, "UTF8_STRING", False);
		if (p_dark) {
			change_property(display, window, property, utf8, 8, PropModeReplace, (const unsigned char *)"dark", 4);
		} else {
			delete_property(display, window, property);
		}
		flush(display);
	} else {
		SDL_ClearError();
	}
	SDL_UnloadObject(library);
}

void install_maximize_fix(SDL_Window *) {
}

void uninstall_maximize_fix(SDL_Window *) {
}

void refresh_frame(SDL_Window *) {
}

#else

// ---------------------------------------------------------------------------
// macOS et autres : la barre de titre suit l'apparence du système.
// ---------------------------------------------------------------------------
void set_title_bar_theme(SDL_Window *, bool, bool, uint32_t, uint32_t) {
}

void install_maximize_fix(SDL_Window *) {
}

void uninstall_maximize_fix(SDL_Window *) {
}

void refresh_frame(SDL_Window *) {
}

#endif

} // namespace WindowPlatform
