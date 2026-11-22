// TranslationServer : textes du jeu dans plusieurs langues.
//
// Les traductions viennent de fichiers CSV (première colonne : la clé ; les suivantes : une
// langue par colonne, nommée dans la première ligne) ou sont ajoutées par script.
//   keys,fr,en
//   BONJOUR,Bonjour,Hello
// tr("BONJOUR") renvoie le texte dans la langue courante ; une clé inconnue est renvoyée telle quelle.
#pragma once

#include "core/object/object.h"

#include <unordered_map>

class TranslationServer : public Object {
	GDCLASS(TranslationServer, Object)

	static TranslationServer *singleton;

	std::unordered_map<String, std::unordered_map<String, String>> messages; // langue -> clé -> texte
	std::vector<String> locales; // dans l'ordre d'ajout
	String locale = "en";
	String fallback = "en";
	uint64_t generation = 1; // change à chaque modification : l'interface se redessine

	const String *_find(const String &p_locale, const String &p_key) const;

protected:
	static void _bind_methods();

public:
	static TranslationServer *get_singleton() { return singleton; }

	void set_locale(const String &p_locale);
	String get_locale() const { return locale; }
	void set_fallback_locale(const String &p_locale);
	String get_fallback_locale() const { return fallback; }
	Array get_loaded_locales() const;
	// Langue chargée la plus proche de celle demandée (« fr_CA » -> « fr ») ; vide si aucune.
	String find_closest_locale(const String &p_locale) const;

	void add_message(const String &p_locale, const String &p_key, const String &p_text);
	void add_translation(const String &p_locale, const Dictionary &p_messages);
	Error load_csv(const String &p_path);
	Error parse_csv(const String &p_text);
	void clear();

	String translate(const String &p_key) const;
	bool has_message(const String &p_key) const;
	uint64_t get_generation() const { return generation; }

	static String standardize_locale(const String &p_locale);
	// Découpe un texte CSV en lignes de champs (guillemets, virgules et retours à la ligne dans les champs).
	static std::vector<std::vector<String>> split_csv(const String &p_text, char p_delimiter = ',');

	TranslationServer();
	~TranslationServer();
};

// Raccourci utilisable partout dans le moteur.
String TTR_GAME(const String &p_key);
