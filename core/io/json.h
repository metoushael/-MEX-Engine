#pragma once

#include "core/object/object.h"
#include "core/variant/variant.h"

// Lecture et écriture de JSON. Correspondance avec Variant : null <-> NIL, true/false <-> BOOL,
// nombre <-> INT ou FLOAT, chaîne <-> STRING, tableau <-> ARRAY, objet <-> DICTIONARY (clés
// en chaînes ; l'ordre des clés d'un objet n'est pas conservé).
class JSON {
public:
	// p_indent vide : une seule ligne. Sinon, l'indentation d'un niveau (par exemple "\t").
	static String stringify(const Variant &p_value, const String &p_indent = "");
	// Renvoie false et remplit r_error (avec la position) si le texte n'est pas du JSON valide.
	static bool parse(const String &p_text, Variant &r_value, String *r_error = nullptr);
};

// Accès depuis les scripts, sous le nom « JSON » (sauvegardes, fichiers de données) :
//   var texte = JSON.stringify({"score": 12, "noms": ["a", "b"]}, "\t")
//   var donnees = JSON.parse_string(texte)      # null si le texte n'est pas du JSON valide
class JSONSingleton : public Object {
	GDCLASS(JSONSingleton, Object)

protected:
	static void _bind_methods();

public:
	String stringify(const Variant &p_value, const String &p_indent);
	Variant parse_string(const String &p_text);
};
