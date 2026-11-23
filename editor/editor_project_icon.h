// Logo des projets : chaque projet a le sien sur la page d'accueil. Un nouveau projet reçoit
// un fichier icon.png (comme l'icon.svg de Godot) ; un projet sans icône en reçoit un à
// l'affichage, sans que ses fichiers soient modifiés.
#pragma once

#include "core/typedefs.h"

#include <cstdint>
#include <vector>

namespace EditorProjectIcon {
// Image RGBA carrée : pavé arrondi dont la couleur dépend du nom, avec l'initiale du projet.
std::vector<uint8_t> generate(const String &p_name, int p_size);
// PNG non compressé (blocs « stored » de deflate) : pas de bibliothèque de compression requise.
bool write_png(const String &p_path, int p_width, int p_height, const uint8_t *p_rgba);
} // namespace EditorProjectIcon
