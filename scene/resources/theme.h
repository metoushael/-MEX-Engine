// Theme : apparence commune à toute une interface. Un thème posé sur un Control s'applique
// à ce contrôle et à tous ses descendants.
//
// Chaque entrée porte le nom d'un type de contrôle et celui d'une de ses propriétés
// d'apparence : set_color("font_color", "Button", ...). Une entrée du thème remplace la
// propriété de même nom des contrôles concernés ; les classes dérivées héritent des entrées
// de leur classe de base (un CheckBox utilise celles de Button).
#pragma once

#include "scene/resources/font.h"

class Theme : public Resource {
	GDCLASS(Theme, Resource)

	Dictionary items; // "Type/nom" -> valeur
	Ref<Font> default_font;
	int default_font_size = 0; // 0 : chaque contrôle garde sa taille

protected:
	static void _bind_methods();

public:
	void set_item(const String &p_name, const String &p_type, const Variant &p_value);
	Variant get_item(const String &p_name, const String &p_type) const;
	bool has_item(const String &p_name, const String &p_type) const;
	void clear_item(const String &p_name, const String &p_type);

	void set_color(const String &p_name, const String &p_type, const Color &p_color) { set_item(p_name, p_type, p_color); }
	Color get_color(const String &p_name, const String &p_type) const { return get_item(p_name, p_type).as_color(); }
	bool has_color(const String &p_name, const String &p_type) const { return has_item(p_name, p_type); }
	void set_constant(const String &p_name, const String &p_type, int p_value) { set_item(p_name, p_type, p_value); }
	int get_constant(const String &p_name, const String &p_type) const { return (int)get_item(p_name, p_type).as_int(); }
	void set_font(const String &p_name, const String &p_type, const Ref<Font> &p_font) { set_item(p_name, p_type, p_font); }
	void set_font_size(const String &p_name, const String &p_type, int p_size) { set_item(p_name, p_type, p_size); }

	void set_items(const Dictionary &p_items);
	Dictionary get_items() const { return items; }
	void set_default_font(const Ref<Font> &p_font);
	Ref<Font> get_default_font() const { return default_font; }
	void set_default_font_size(int p_size);
	int get_default_font_size() const { return default_font_size; }
};
