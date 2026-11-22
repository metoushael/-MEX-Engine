// Matériaux 2D : CanvasItemMaterial (mode de mélange), Shader (code) et ShaderMaterial
// (shader + paramètres). S'affectent à la propriété « material » d'un CanvasItem.
#pragma once

#include "core/io/resource_loader.h"
#include "scene/resources/texture.h"
#include "servers/rendering_server.h"

// Code d'un shader, écrit comme dans Godot :
//   shader_type canvas_item;
//   uniform float force = 1.0;
//   void fragment() { COLOR.rgb = mix(COLOR.rgb, vec3(1.0), force); }
// Variables : VERTEX (dans vertex), UV, COLOR, TEXTURE, TEXTURE_PIXEL_SIZE, SCREEN_UV, TIME.
class Shader : public Resource {
	GDCLASS(Shader, Resource)

	String code;

protected:
	static void _bind_methods();

public:
	void set_code(const String &p_code);
	String get_code() const { return code; }
};

class ResourceFormatLoaderShader : public ResourceFormatLoader {
public:
	virtual void get_recognized_extensions(std::vector<String> *p_extensions) const override { p_extensions->push_back("shader"); }
	virtual String get_resource_type(const String &p_path) const override { return "Shader"; }
	virtual Ref<Resource> load(const String &p_path, Error *r_error) override;
};

class CanvasItemMaterial : public Resource {
	GDCLASS(CanvasItemMaterial, Resource)

public:
	enum BlendMode {
		BLEND_MODE_MIX,
		BLEND_MODE_ADD,
		BLEND_MODE_SUB,
		BLEND_MODE_MUL,
	};

private:
	int blend_mode = BLEND_MODE_MIX;

protected:
	RID material;
	static void _bind_methods();

public:
	RID get_rid() const { return material; }
	void set_blend_mode(int p_mode);
	int get_blend_mode() const { return blend_mode; }

	CanvasItemMaterial();
	~CanvasItemMaterial();
};

class ShaderMaterial : public CanvasItemMaterial {
	GDCLASS(ShaderMaterial, CanvasItemMaterial)

	Ref<Shader> shader;
	Dictionary parameters;

	void _shader_changed();
	void _update_parameters();

protected:
	static void _bind_methods();

public:
	void set_shader(const Ref<Shader> &p_shader);
	Ref<Shader> get_shader() const { return shader; }
	void set_shader_parameter(const String &p_name, const Variant &p_value);
	Variant get_shader_parameter(const String &p_name) const;
	void set_shader_parameters(const Dictionary &p_parameters);
	Dictionary get_shader_parameters() const { return parameters; }
	// Message d'erreur de compilation ("" si le shader est valide).
	String get_shader_error() const;
};
