#include "editor_project_icon.h"

#include "scene/resources/font.h"

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

#include <cmath>
#include <fstream>
#include <filesystem>

namespace EditorProjectIcon {

namespace {
// Teintes de la charte de MEX et voisines ; le nom du projet en choisit une.
const uint8_t PALETTE[][3] = {
	{ 0x2f, 0x6f, 0xed }, { 0x16, 0xa3, 0x8a }, { 0xe2, 0x5a, 0x3c }, { 0x8b, 0x5c, 0xf6 },
	{ 0xd9, 0x7a, 0x06 }, { 0xdb, 0x3f, 0x7a }, { 0x1f, 0x9d, 0xd6 }, { 0x4f, 0xa1, 0x2f },
	{ 0xc0, 0x45, 0xc9 }, { 0x54, 0x62, 0xd8 }, { 0xcf, 0x4b, 0x4b }, { 0x0f, 0x8f, 0x6f },
};

uint32_t _hash(const String &p_text) {
	uint32_t h = 2166136261u; // FNV-1a
	for (unsigned char c : p_text) {
		h = (h ^ c) * 16777619u;
	}
	return h;
}

// Première lettre ou premier chiffre du nom, en majuscule (point de code Unicode).
int _initial(const String &p_name) {
	size_t i = 0;
	while (i < p_name.size()) {
		const unsigned char c = (unsigned char)p_name[i];
		int code = c, extra = 0;
		if (c >= 0xf0) {
			code = c & 0x07;
			extra = 3;
		} else if (c >= 0xe0) {
			code = c & 0x0f;
			extra = 2;
		} else if (c >= 0xc0) {
			code = c & 0x1f;
			extra = 1;
		}
		for (int k = 1; k <= extra && i + k < p_name.size(); k++) {
			code = (code << 6) | ((unsigned char)p_name[i + k] & 0x3f);
		}
		i += 1 + extra;
		if (code >= 'a' && code <= 'z') {
			return code - 32;
		}
		if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')) {
			return code;
		}
		if (code >= 0xe0 && code <= 0xfe && code != 0xf7) {
			return code - 32; // minuscules accentuées du Latin-1 : é -> É
		}
		if (code >= 0xc0) {
			return code;
		}
	}
	return 'M';
}

uint32_t _crc32(const uint8_t *p_data, size_t p_size, uint32_t p_crc = 0) {
	static uint32_t table[256];
	if (!table[1]) {
		for (uint32_t n = 0; n < 256; n++) {
			uint32_t c = n;
			for (int k = 0; k < 8; k++) {
				c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
			}
			table[n] = c;
		}
	}
	uint32_t c = p_crc ^ 0xffffffffu;
	for (size_t i = 0; i < p_size; i++) {
		c = table[(c ^ p_data[i]) & 0xff] ^ (c >> 8);
	}
	return c ^ 0xffffffffu;
}

void _be32(std::vector<uint8_t> &r_out, uint32_t p_value) {
	for (int shift = 24; shift >= 0; shift -= 8) {
		r_out.push_back((uint8_t)(p_value >> shift));
	}
}

void _chunk(std::vector<uint8_t> &r_out, const char *p_type, const std::vector<uint8_t> &p_data) {
	_be32(r_out, (uint32_t)p_data.size());
	std::vector<uint8_t> body(p_type, p_type + 4);
	body.insert(body.end(), p_data.begin(), p_data.end());
	r_out.insert(r_out.end(), body.begin(), body.end());
	_be32(r_out, _crc32(body.data(), body.size()));
}
} // namespace

std::vector<uint8_t> generate(const String &p_name, int p_size) {
	const int size = p_size < 16 ? 16 : p_size;
	std::vector<uint8_t> rgba((size_t)size * size * 4, 0);
	const uint32_t hash = _hash(p_name);
	const uint8_t *base = PALETTE[hash % (sizeof(PALETTE) / sizeof(PALETTE[0]))];

	// Pavé arrondi, dégradé du clair (haut) au foncé (bas), bord lissé.
	const float radius = size * 0.22f;
	for (int y = 0; y < size; y++) {
		const float t = (float)y / (size - 1);
		const float light = 1.14f - 0.34f * t;
		for (int x = 0; x < size; x++) {
			const float cx = std::fmax(std::fmax(radius - (x + 0.5f), (x + 0.5f) - (size - radius)), 0.0f);
			const float cy = std::fmax(std::fmax(radius - (y + 0.5f), (y + 0.5f) - (size - radius)), 0.0f);
			const float alpha = std::fmin(std::fmax(radius - std::sqrt(cx * cx + cy * cy) + 0.5f, 0.0f), 1.0f);
			uint8_t *px = &rgba[((size_t)y * size + x) * 4];
			for (int c = 0; c < 3; c++) {
				px[c] = (uint8_t)std::fmin(base[c] * light, 255.0f);
			}
			px[3] = (uint8_t)(alpha * 255.0f + 0.5f);
		}
	}

	// Initiale en blanc, centrée (police embarquée du moteur).
	unsigned int font_size = 0;
	const unsigned char *font_data = Font::get_embedded_data(font_size);
	stbtt_fontinfo font;
	if (font_data && stbtt_InitFont(&font, font_data, 0)) {
		int glyph = stbtt_FindGlyphIndex(&font, _initial(p_name));
		if (glyph == 0) {
			glyph = stbtt_FindGlyphIndex(&font, 'M');
		}
		const float scale = stbtt_ScaleForPixelHeight(&font, size * 0.74f);
		int w = 0, h = 0, ox = 0, oy = 0;
		unsigned char *bitmap = stbtt_GetGlyphBitmap(&font, scale, scale, glyph, &w, &h, &ox, &oy);
		if (bitmap) {
			const int left = (size - w) / 2, top = (size - h) / 2;
			for (int y = 0; y < h; y++) {
				for (int x = 0; x < w; x++) {
					const int dx = left + x, dy = top + y;
					if (dx < 0 || dy < 0 || dx >= size || dy >= size) {
						continue;
					}
					const int a = bitmap[y * w + x];
					uint8_t *px = &rgba[((size_t)dy * size + dx) * 4];
					for (int c = 0; c < 3; c++) {
						px[c] = (uint8_t)((px[c] * (255 - a) + 255 * a) / 255);
					}
				}
			}
			stbtt_FreeBitmap(bitmap, nullptr);
		}
	}
	return rgba;
}

bool write_png(const String &p_path, int p_width, int p_height, const uint8_t *p_rgba) {
	// Lignes précédées de l'octet de filtre 0.
	std::vector<uint8_t> raw;
	raw.reserve((size_t)p_height * (p_width * 4 + 1));
	for (int y = 0; y < p_height; y++) {
		raw.push_back(0);
		raw.insert(raw.end(), p_rgba + (size_t)y * p_width * 4, p_rgba + (size_t)(y + 1) * p_width * 4);
	}
	// Flux zlib fait de blocs non compressés de 65535 octets au plus.
	std::vector<uint8_t> zlib = { 0x78, 0x01 };
	uint32_t s1 = 1, s2 = 0;
	for (size_t pos = 0; pos < raw.size();) {
		const size_t len = std::min<size_t>(65535, raw.size() - pos);
		zlib.push_back(pos + len >= raw.size() ? 1 : 0);
		zlib.push_back((uint8_t)(len & 0xff));
		zlib.push_back((uint8_t)(len >> 8));
		zlib.push_back((uint8_t)(~len & 0xff));
		zlib.push_back((uint8_t)((~len >> 8) & 0xff));
		for (size_t i = 0; i < len; i++) {
			s1 = (s1 + raw[pos + i]) % 65521;
			s2 = (s2 + s1) % 65521;
		}
		zlib.insert(zlib.end(), raw.begin() + pos, raw.begin() + pos + len);
		pos += len;
	}
	_be32(zlib, (s2 << 16) | s1);

	std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
	std::vector<uint8_t> header;
	_be32(header, (uint32_t)p_width);
	_be32(header, (uint32_t)p_height);
	header.insert(header.end(), { 8, 6, 0, 0, 0 }); // 8 bits, RGBA
	_chunk(png, "IHDR", header);
	_chunk(png, "IDAT", zlib);
	_chunk(png, "IEND", {});
	std::ofstream out(std::filesystem::u8path(p_path), std::ios::binary | std::ios::trunc);
	out.write((const char *)png.data(), (std::streamsize)png.size());
	out.close();
	return (bool)out;
}

} // namespace EditorProjectIcon
