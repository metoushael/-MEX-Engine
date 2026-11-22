// Accès fichiers. Tous les chemins acceptent le préfixe "res://" (racine du projet).
// Dans un jeu exporté, les lectures « res:// » se font dans l'archive .pck.
#pragma once

#include "core/typedefs.h"

class FileAccess {
public:
	static bool exists(const String &p_path);
	static bool dir_exists(const String &p_path);
	static String get_file_as_string(const String &p_path, Error *r_error = nullptr);
	static std::vector<uint8_t> get_file_as_bytes(const String &p_path, Error *r_error = nullptr);
	static Error write_string(const String &p_path, const String &p_text);
	static Error write_bytes(const String &p_path, const std::vector<uint8_t> &p_data);
	// Copie un fichier (les droits d'exécution sont conservés).
	static Error copy(const String &p_from, const String &p_to);
	static Error make_dir_recursive(const String &p_path);
	static Error remove(const String &p_path);
	static Error rename(const String &p_from, const String &p_to);
	// Liste triée du contenu direct d'un dossier (noms seuls).
	static void list_dir(const String &p_path, std::vector<String> *r_files, std::vector<String> *r_dirs);
	static uint64_t get_modified_time(const String &p_path);
};
