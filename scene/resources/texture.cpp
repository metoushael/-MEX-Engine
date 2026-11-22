#include "texture.h"

#include "core/io/file_access.h"
#include "servers/rendering_server_3d.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_STDIO
#include "thirdparty/stb/stb_image.h"

Texture2D::~Texture2D() {
	if (texture.is_valid() && RenderingServer::get_singleton()) {
		RenderingServer::get_singleton()->free_rid(texture);
	}
	if (texture_3d.is_valid() && RenderingServer3D::get_singleton()) {
		RenderingServer3D::get_singleton()->free_rid(texture_3d);
	}
}

RID Texture2D::get_rid_3d() const {
	RenderingServer3D *rs = RenderingServer3D::get_singleton();
	if (!texture_3d.is_valid() && rs && rs->is_available() && !pixels.empty()) {
		texture_3d = rs->texture_create(width, height, pixels.data());
	}
	return texture_3d;
}

void Texture2D::set_rgba(int p_width, int p_height, const uint8_t *p_rgba, int p_filter) {
	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL(rs);
	if (texture.is_valid()) {
		rs->free_rid(texture);
	}
	texture = rs->texture_create(p_width, p_height, p_rgba, p_filter);
	width = p_width;
	height = p_height;
	if (texture_3d.is_valid() && RenderingServer3D::get_singleton()) {
		RenderingServer3D::get_singleton()->free_rid(texture_3d);
		texture_3d = RID();
	}
	if (p_rgba && p_width > 0 && p_height > 0) {
		pixels.assign(p_rgba, p_rgba + (size_t)p_width * (size_t)p_height * 4);
	} else {
		pixels.clear();
	}
	emit_changed();
}

Ref<Texture2D> Texture2D::create_from_rgba(int p_width, int p_height, const uint8_t *p_rgba, int p_filter) {
	Ref<Texture2D> tex;
	tex.instantiate();
	tex->set_rgba(p_width, p_height, p_rgba, p_filter);
	return tex;
}

Error Texture2D::load_from_file(const String &p_path) {
	Error err = OK;
	std::vector<uint8_t> bytes = FileAccess::get_file_as_bytes(p_path, &err);
	if (err != OK) {
		return err;
	}
	err = load_from_memory(bytes.data(), bytes.size());
	if (err != OK) {
		print_error("Image illisible : " + p_path + " (" + String(stbi_failure_reason()) + ")");
	}
	return err;
}

Error Texture2D::load_from_memory(const uint8_t *p_data, size_t p_size) {
	int w = 0, h = 0, comp = 0;
	stbi_uc *decoded = stbi_load_from_memory(p_data, (int)p_size, &w, &h, &comp, 4);
	if (!decoded) {
		return ERR_INVALID_DATA;
	}
	set_rgba(w, h, decoded);
	stbi_image_free(decoded);
	return OK;
}

void Texture2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_width"), &Texture2D::get_width);
	ClassDB::bind_method(D_METHOD("get_height"), &Texture2D::get_height);
	ClassDB::bind_method(D_METHOD("get_size"), &Texture2D::get_size);
}

void ResourceFormatLoaderTexture::get_recognized_extensions(std::vector<String> *p_extensions) const {
	for (const char *ext : { "png", "jpg", "jpeg", "bmp", "tga", "gif" }) {
		p_extensions->push_back(ext);
	}
}

Ref<Resource> ResourceFormatLoaderTexture::load(const String &p_path, Error *r_error) {
	Ref<Texture2D> tex;
	tex.instantiate();
	Error err = tex->load_from_file(p_path);
	if (r_error) {
		*r_error = err;
	}
	if (err != OK) {
		return Ref<Resource>();
	}
	return tex;
}
