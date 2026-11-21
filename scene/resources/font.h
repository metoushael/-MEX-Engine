// Font : police TrueType rendue dans un atlas par taille (stb_truetype).
#pragma once

#include "scene/resources/texture.h"

class Font : public Resource {
	GDCLASS(Font, Resource)

	struct Glyph {
		Rect2 region; // dans l'atlas, en pixels
		Vector2 offset; // par rapport à la ligne de base
		real_t advance = 0;
	};

	struct SizeCache {
		Ref<Texture2D> atlas;
		std::unordered_map<uint32_t, Glyph> glyphs;
		real_t ascent = 0;
		real_t descent = 0;
		real_t line_height = 0;
	};

	std::vector<uint8_t> data;
	mutable std::map<int, SizeCache> cache;
	static Ref<Font> default_font;

	const SizeCache &_get_cache(int p_size) const;

protected:
	static void _bind_methods();

public:
	static Ref<Font> get_default();
	static void free_default();
	// Données TTF de la police embarquée (Roboto Medium), réutilisées par l'éditeur.
	static const unsigned char *get_embedded_data(unsigned int &r_size);

	Error load_from_file(const String &p_path);
	void set_data(const uint8_t *p_data, size_t p_size);

	real_t get_height(int p_size) const;
	real_t get_ascent(int p_size) const;
	Size2 get_string_size(const String &p_text, int p_size) const;
	// p_pos : coin supérieur gauche de la première ligne.
	void draw_string(RID p_canvas_item, const Vector2 &p_pos, const String &p_text, int p_size, const Color &p_color) const;
};

class ResourceFormatLoaderFont : public ResourceFormatLoader {
public:
	virtual void get_recognized_extensions(std::vector<String> *p_extensions) const override;
	virtual String get_resource_type(const String &p_path) const override { return "Font"; }
	virtual Ref<Resource> load(const String &p_path, Error *r_error) override;
};
