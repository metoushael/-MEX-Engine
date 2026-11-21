#include "font.h"

#include <cstring>

#include "core/io/file_access.h"
#include "core/string/ustring.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "thirdparty/stb/stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "thirdparty/fonts/roboto_medium.gen.h"

Ref<Font> Font::default_font;

const unsigned char *Font::get_embedded_data(unsigned int &r_size) {
	r_size = roboto_medium_ttf_size;
	return roboto_medium_ttf;
}

Ref<Font> Font::get_default() {
	if (default_font.is_null()) {
		default_font.instantiate();
		default_font->set_data(roboto_medium_ttf, roboto_medium_ttf_size);
	}
	return default_font;
}

void Font::free_default() {
	default_font.unref();
}

void Font::set_data(const uint8_t *p_data, size_t p_size) {
	data.assign(p_data, p_data + p_size);
	cache.clear();
	emit_changed();
}

Error Font::load_from_file(const String &p_path) {
	Error err = OK;
	std::vector<uint8_t> bytes = FileAccess::get_file_as_bytes(p_path, &err);
	if (err != OK) {
		return err;
	}
	stbtt_fontinfo info;
	if (bytes.empty() || !stbtt_InitFont(&info, bytes.data(), stbtt_GetFontOffsetForIndex(bytes.data(), 0))) {
		print_error("Police invalide : " + p_path);
		return ERR_INVALID_DATA;
	}
	data.swap(bytes);
	cache.clear();
	return OK;
}

const Font::SizeCache &Font::_get_cache(int p_size) const {
	p_size = Math::clamp(p_size, 4, 256);
	auto it = cache.find(p_size);
	if (it != cache.end()) {
		return it->second;
	}
	SizeCache &sc = cache[p_size];
	if (data.empty()) {
		return sc;
	}
	stbtt_fontinfo info;
	if (!stbtt_InitFont(&info, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0))) {
		return sc;
	}
	real_t scale = stbtt_ScaleForPixelHeight(&info, (float)p_size);
	int ascent, descent, line_gap;
	stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
	sc.ascent = ascent * scale;
	sc.descent = -descent * scale;
	sc.line_height = (ascent - descent + line_gap) * scale;

	// Latin de base + Latin-1 + Latin étendu A (œ, Œ...) + ponctuation typographique.
	static const int extra_codepoints[] = { 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x20AC, 0x2190, 0x2191, 0x2192, 0x2193 };
	const int extra_count = (int)(sizeof(extra_codepoints) / sizeof(int));
	std::vector<stbtt_packedchar> chars_a(0x180 - 32);
	std::vector<stbtt_packedchar> chars_b(extra_count);
	stbtt_pack_range ranges[2];
	memset(ranges, 0, sizeof(ranges));
	ranges[0].font_size = (float)p_size;
	ranges[0].first_unicode_codepoint_in_range = 32;
	ranges[0].num_chars = 0x180 - 32;
	ranges[0].chardata_for_range = chars_a.data();
	ranges[1].font_size = (float)p_size;
	ranges[1].array_of_unicode_codepoints = const_cast<int *>(extra_codepoints);
	ranges[1].num_chars = extra_count;
	ranges[1].chardata_for_range = chars_b.data();

	int atlas_size = 256;
	std::vector<uint8_t> bitmap;
	bool packed = false;
	while (atlas_size <= 4096 && !packed) {
		bitmap.assign((size_t)atlas_size * atlas_size, 0);
		stbtt_pack_context ctx;
		if (stbtt_PackBegin(&ctx, bitmap.data(), atlas_size, atlas_size, 0, 1, nullptr)) {
			packed = stbtt_PackFontRanges(&ctx, data.data(), 0, ranges, 2) != 0;
			stbtt_PackEnd(&ctx);
		}
		if (!packed) {
			atlas_size *= 2;
		}
	}
	if (!packed) {
		print_error("Impossible de générer l'atlas de police (taille " + itos(p_size) + ").");
		return sc;
	}

	std::vector<uint8_t> rgba((size_t)atlas_size * atlas_size * 4);
	for (size_t i = 0; i < bitmap.size(); i++) {
		rgba[i * 4 + 0] = 255;
		rgba[i * 4 + 1] = 255;
		rgba[i * 4 + 2] = 255;
		rgba[i * 4 + 3] = bitmap[i];
	}
	sc.atlas = Texture2D::create_from_rgba(atlas_size, atlas_size, rgba.data(), RenderingServer::TEXTURE_FILTER_LINEAR);

	auto add_glyph = [&](uint32_t p_codepoint, const stbtt_packedchar &pc) {
		Glyph g;
		g.region = Rect2((real_t)pc.x0, (real_t)pc.y0, (real_t)(pc.x1 - pc.x0), (real_t)(pc.y1 - pc.y0));
		g.offset = Vector2(pc.xoff, pc.yoff);
		g.advance = pc.xadvance;
		sc.glyphs[p_codepoint] = g;
	};
	for (int i = 0; i < 0x180 - 32; i++) {
		if (stbtt_FindGlyphIndex(&info, 32 + i) != 0 || i == 0) {
			add_glyph((uint32_t)(32 + i), chars_a[i]);
		}
	}
	for (int i = 0; i < extra_count; i++) {
		if (stbtt_FindGlyphIndex(&info, extra_codepoints[i]) != 0) {
			add_glyph((uint32_t)extra_codepoints[i], chars_b[i]);
		}
	}
	return sc;
}

real_t Font::get_height(int p_size) const {
	return _get_cache(p_size).line_height;
}

real_t Font::get_ascent(int p_size) const {
	return _get_cache(p_size).ascent;
}

Size2 Font::get_string_size(const String &p_text, int p_size) const {
	const SizeCache &sc = _get_cache(p_size);
	real_t width = 0, max_width = 0;
	int lines = 1;
	for (uint32_t cp : utf8_decode(p_text)) {
		if (cp == '\n') {
			max_width = std::max(max_width, width);
			width = 0;
			lines++;
			continue;
		}
		if (cp == '\t') {
			auto space = sc.glyphs.find(' ');
			width += space != sc.glyphs.end() ? space->second.advance * 4 : 0;
			continue;
		}
		auto it = sc.glyphs.find(cp);
		if (it == sc.glyphs.end()) {
			it = sc.glyphs.find('?');
		}
		if (it != sc.glyphs.end()) {
			width += it->second.advance;
		}
	}
	max_width = std::max(max_width, width);
	return Size2(max_width, sc.line_height * lines);
}

void Font::draw_string(RID p_canvas_item, const Vector2 &p_pos, const String &p_text, int p_size, const Color &p_color) const {
	const SizeCache &sc = _get_cache(p_size);
	if (sc.atlas.is_null()) {
		return;
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	RID atlas = sc.atlas->get_rid();
	Vector2 pen(p_pos.x, p_pos.y + sc.ascent);
	for (uint32_t cp : utf8_decode(p_text)) {
		if (cp == '\n') {
			pen.x = p_pos.x;
			pen.y += sc.line_height;
			continue;
		}
		if (cp == '\t') {
			auto space = sc.glyphs.find(' ');
			pen.x += space != sc.glyphs.end() ? space->second.advance * 4 : 0;
			continue;
		}
		auto it = sc.glyphs.find(cp);
		if (it == sc.glyphs.end()) {
			it = sc.glyphs.find('?');
			if (it == sc.glyphs.end()) {
				continue;
			}
		}
		const Glyph &g = it->second;
		if (g.region.size.x > 0 && g.region.size.y > 0) {
			// Positions arrondies : texte net.
			Rect2 dst(std::round(pen.x + g.offset.x), std::round(pen.y + g.offset.y), g.region.size.x, g.region.size.y);
			rs->canvas_item_add_texture_rect_region(p_canvas_item, dst, atlas, g.region, p_color);
		}
		pen.x += g.advance;
	}
}

void Font::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_height", "size"), &Font::get_height);
	ClassDB::bind_method(D_METHOD("get_string_size", "text", "size"), &Font::get_string_size);
}

void ResourceFormatLoaderFont::get_recognized_extensions(std::vector<String> *p_extensions) const {
	p_extensions->push_back("ttf");
	p_extensions->push_back("otf");
}

Ref<Resource> ResourceFormatLoaderFont::load(const String &p_path, Error *r_error) {
	Ref<Font> font;
	font.instantiate();
	Error err = font->load_from_file(p_path);
	if (r_error) {
		*r_error = err;
	}
	if (err != OK) {
		return Ref<Resource>();
	}
	return font;
}
