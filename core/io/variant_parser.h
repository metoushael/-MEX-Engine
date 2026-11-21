// Lecture/écriture textuelle des Variants (scènes, projet, ressources).
#pragma once

#include "core/variant/variant.h"

class VariantParser {
public:
	struct Stream {
		const String &text;
		size_t pos = 0;
		int line = 1;
		Stream(const String &p_text) :
				text(p_text) {}
	};

	// Résout ExtResource("id") / SubResource("id") en un Variant objet.
	typedef std::function<Variant(const String &p_kind, const String &p_id)> ResourceResolver;

	struct Tag {
		String name;
		std::map<String, Variant> fields;
		bool has(const String &p_field) const { return fields.count(p_field) > 0; }
		Variant get(const String &p_field, const Variant &p_default = Variant()) const {
			auto it = fields.find(p_field);
			return it == fields.end() ? p_default : it->second;
		}
	};

	enum Item {
		ITEM_TAG, // [nom champ=valeur ...]
		ITEM_ASSIGN, // cle = valeur
		ITEM_EOF,
		ITEM_ERROR,
	};

	static Error parse_value(Stream &p_stream, Variant &r_value, String &r_err, const ResourceResolver *p_resolver = nullptr);
	static Item parse_next(Stream &p_stream, Tag &r_tag, String &r_key, Variant &r_value, String &r_err, const ResourceResolver *p_resolver = nullptr);
};

class VariantWriter {
public:
	// Encode un objet (ressource) ; doit retourner par ex. `ExtResource("1")`.
	typedef std::function<String(const Variant &p_object)> ObjectEncoder;
	static String write(const Variant &p_value, const ObjectEncoder *p_encoder = nullptr);
};
