// Texture2D : image chargée sur le GPU (PNG, JPG, BMP, TGA via stb_image).
#pragma once

#include "core/io/resource_loader.h"
#include "servers/rendering_server.h"

class Texture2D : public Resource {
	GDCLASS(Texture2D, Resource)

	RID texture;
	mutable RID texture_3d; // créée à la demande pour les matériaux 3D
	std::vector<uint8_t> pixels; // copie RGBA (textures 3D, lecture de pixels)
	int width = 0;
	int height = 0;

protected:
	static void _bind_methods();

public:
	static Ref<Texture2D> create_from_rgba(int p_width, int p_height, const uint8_t *p_rgba, int p_filter = -1);
	Error load_from_file(const String &p_path);
	Error load_from_memory(const uint8_t *p_data, size_t p_size); // image encodée (PNG, JPEG...)
	const std::vector<uint8_t> &get_rgba() const { return pixels; }
	void set_rgba(int p_width, int p_height, const uint8_t *p_rgba, int p_filter = -1);

	RID get_rid() const { return texture; }
	RID get_rid_3d() const; // texture avec mipmaps pour le rendu 3D (invalide si la 3D est indisponible)
	const std::vector<uint8_t> &get_pixels() const { return pixels; }
	int get_width() const { return width; }
	int get_height() const { return height; }
	Size2 get_size() const { return Size2((real_t)width, (real_t)height); }

	~Texture2D();
};

class ResourceFormatLoaderTexture : public ResourceFormatLoader {
public:
	virtual void get_recognized_extensions(std::vector<String> *p_extensions) const override;
	virtual String get_resource_type(const String &p_path) const override { return "Texture2D"; }
	virtual Ref<Resource> load(const String &p_path, Error *r_error) override;
};
