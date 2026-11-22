// Réglages de fenêtre propres à chaque système (interne à DisplayServer).
// Isolé dans son propre fichier : les en-têtes de Windows et de X11 définissent des macros
// envahissantes.
#pragma once

#include <cstdint>

struct SDL_Window;

namespace WindowPlatform {

// Barre de titre du système : sombre ou claire. Les couleurs exactes (0xRRGGBB) ne sont
// appliquées que si p_has_colors est vrai et que le système les accepte.
//  - Windows 10 (1809 et suivants) et 11 : barre sombre ; Windows 11 : couleurs exactes.
//  - Linux (X11) : indication « thème sombre » suivie par GNOME, Cinnamon, Xfce et quelques autres.
//  - macOS : la barre suit l'apparence du système.
void set_title_bar_theme(SDL_Window *p_window, bool p_dark, bool p_has_colors, uint32_t p_background, uint32_t p_text);

// Windows : une fenêtre sans décoration agrandie doit couvrir la zone de travail de son
// écran, et non l'écran entier (elle masquerait la barre des tâches). Sans effet ailleurs.
void install_maximize_fix(SDL_Window *p_window);
void uninstall_maximize_fix(SDL_Window *p_window);

// Windows : fait recalculer le cadre de la fenêtre (zone cliente, ombre). À appeler quand la
// fenêtre est montrée ou change de décoration. Sans effet ailleurs.
void refresh_frame(SDL_Window *p_window);

} // namespace WindowPlatform
