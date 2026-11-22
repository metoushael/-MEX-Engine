// Archive de données d'un jeu exporté (fichier .pck).
// Quand une archive est chargée, les chemins « res:// » sont lus dedans.
//
// Format (entiers en petit-boutiste) :
//   8 octets  "MEXPACK1"
//   uint32    nombre de fichiers
//   par fichier : uint16 longueur du chemin, chemin UTF-8 (relatif à res://),
//                 uint64 position des données dans l'archive, uint64 taille
//   données des fichiers, à la suite
<<<<<<< HEAD
//
// Archive incorporée à l'exécutable (même procédé que Godot, « embed_pck ») : l'archive est
// ajoutée à la fin de l'exécutable, suivie de sa taille (uint64) et de 4 octets "MEXP". Au
// lancement, le jeu lit ces 12 derniers octets, remonte de la taille indiquée et y trouve
// l'archive. Les positions des fichiers sont relatives au début de l'archive.
=======
>>>>>>> ad542b7994018528bfadabd060baf4823c0564c5
#pragma once

#include "core/typedefs.h"

class PackedData {
public:
<<<<<<< HEAD
	// Archive seule, ou exécutable qui porte une archive incorporée.
	static Error load(const String &p_pack_path);
	// Ce fichier (un exécutable) se termine-t-il par une archive incorporée ?
	static bool has_embedded_pack(const String &p_path);
=======
	static Error load(const String &p_pack_path);
>>>>>>> ad542b7994018528bfadabd060baf4823c0564c5
	static void unload();
	static bool is_loaded();
	static String get_pack_path();

	// Chemins au format « res://dossier/fichier ».
	static bool has_file(const String &p_path);
	static bool has_dir(const String &p_path);
	static bool get_file(const String &p_path, std::vector<uint8_t> *r_data);
	static void list_dir(const String &p_path, std::vector<String> *r_files, std::vector<String> *r_dirs);

	// Crée une archive à partir de fichiers du disque (chemin relatif dans l'archive, chemin absolu).
<<<<<<< HEAD
	// p_embed : l'archive est ajoutée à la fin du fichier existant (un exécutable) au lieu de
	// le remplacer.
	static Error create(const String &p_pack_path, const std::vector<std::pair<String, String>> &p_files, bool p_embed = false);
=======
	static Error create(const String &p_pack_path, const std::vector<std::pair<String, String>> &p_files);
>>>>>>> ad542b7994018528bfadabd060baf4823c0564c5
};
