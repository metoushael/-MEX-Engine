// Archive de données d'un jeu exporté (fichier .pck).
// Quand une archive est chargée, les chemins « res:// » sont lus dedans.
//
// Format (entiers en petit-boutiste) :
//   8 octets  "MEXPACK1"
//   uint32    nombre de fichiers
//   par fichier : uint16 longueur du chemin, chemin UTF-8 (relatif à res://),
//                 uint64 position des données dans l'archive, uint64 taille
//   données des fichiers, à la suite
#pragma once

#include "core/typedefs.h"

class PackedData {
public:
	static Error load(const String &p_pack_path);
	static void unload();
	static bool is_loaded();
	static String get_pack_path();

	// Chemins au format « res://dossier/fichier ».
	static bool has_file(const String &p_path);
	static bool has_dir(const String &p_path);
	static bool get_file(const String &p_path, std::vector<uint8_t> *r_data);
	static void list_dir(const String &p_path, std::vector<String> *r_files, std::vector<String> *r_dirs);

	// Crée une archive à partir de fichiers du disque (chemin relatif dans l'archive, chemin absolu).
	static Error create(const String &p_pack_path, const std::vector<std::pair<String, String>> &p_files);
};
