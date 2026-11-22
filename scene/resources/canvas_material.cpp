#include "canvas_material.h"

#include "core/io/file_access.h"

// --- Shader ---
void Shader::set_code(const String &p_code) {
	if (code == p_code) {
		return;
	}
	code = p_code;
	emit_changed();
}

void Shader::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_code", "code"), &Shader::set_code);
	ClassDB::bind_method(D_METHOD("get_code"), &Shader::get_code);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "code", PROPERTY_HINT_MULTILINE_TEXT), "set_code", "get_code");
}

Ref<Resource> ResourceFormatLoaderShader::load(const String &p_path, Error *r_error) {
	Error err = OK;
	const String code = FileAccess::get_file_as_string(p_path, &err);
	if (r_error) {
		*r_error = err;
	}
	if (err != OK) {
		return Ref<Resource>();
	}
	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(code);
	return shader;
}

// --- CanvasItemMaterial ---
CanvasItemMaterial::CanvasItemMaterial() {
	if (RenderingServer::get_singleton()) {
		material = RenderingServer::get_singleton()->canvas_material_create();
	}
}

CanvasItemMaterial::~CanvasItemMaterial() {
	if (RenderingServer::get_singleton()) {
		RenderingServer::get_singleton()->free_rid(material);
	}
}

void CanvasItemMaterial::set_blend_mode(int p_mode) {
	blend_mode = p_mode;
	if (RenderingServer::get_singleton()) {
		RenderingServer::get_singleton()->canvas_material_set_blend_mode(material, (RenderingServer::BlendMode)p_mode);
	}
	emit_changed();
}

void CanvasItemMaterial::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_blend_mode", "mode"), &CanvasItemMaterial::set_blend_mode);
	ClassDB::bind_method(D_METHOD("get_blend_mode"), &CanvasItemMaterial::get_blend_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "blend_mode", PROPERTY_HINT_ENUM, "Mix,Add,Sub,Mul"), "set_blend_mode", "get_blend_mode");
	BIND_ENUM_CONSTANT(BLEND_MODE_MIX);
	BIND_ENUM_CONSTANT(BLEND_MODE_ADD);
	BIND_ENUM_CONSTANT(BLEND_MODE_SUB);
	BIND_ENUM_CONSTANT(BLEND_MODE_MUL);
}

// --- ShaderMaterial ---
void ShaderMaterial::_shader_changed() {
	if (RenderingServer::get_singleton()) {
		RenderingServer::get_singleton()->canvas_material_set_shader_code(material, shader.is_valid() ? shader->get_code() : String());
	}
	emit_changed();
}

void ShaderMaterial::_update_parameters() {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!rs) {
		return;
	}
	rs->canvas_material_clear_params(material);
	for (const auto &kv : parameters.map()) {
		const String name = kv.first.as_string();
		if (kv.second.get_type() == Variant::OBJECT) {
			// Une texture se transmet au shader comme un « uniform sampler2D ».
			if (Texture2D *texture = Object::cast_to<Texture2D>(kv.second.as_object())) {
				rs->canvas_material_set_texture_param(material, name, texture->get_rid());
			}
		} else {
			rs->canvas_material_set_param(material, name, kv.second);
		}
	}
}

void ShaderMaterial::set_shader(const Ref<Shader> &p_shader) {
	if (shader == p_shader) {
		return;
	}
	if (shader.is_valid()) {
		shader->disconnect("changed", Callable(this, "_shader_changed"));
	}
	shader = p_shader;
	if (shader.is_valid()) {
		shader->connect("changed", Callable(this, "_shader_changed"));
	}
	_shader_changed();
}

void ShaderMaterial::set_shader_parameter(const String &p_name, const Variant &p_value) {
	parameters[p_name] = p_value;
	_update_parameters();
}

Variant ShaderMaterial::get_shader_parameter(const String &p_name) const {
	return parameters.get(p_name, Variant());
}

void ShaderMaterial::set_shader_parameters(const Dictionary &p_parameters) {
	parameters = p_parameters.duplicate();
	_update_parameters();
}

String ShaderMaterial::get_shader_error() const {
	return RenderingServer::get_singleton() ? RenderingServer::get_singleton()->canvas_material_get_shader_error(material) : String();
}

void ShaderMaterial::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_shader", "shader"), &ShaderMaterial::set_shader);
	ClassDB::bind_method(D_METHOD("get_shader"), &ShaderMaterial::get_shader);
	ClassDB::bind_method(D_METHOD("set_shader_parameter", "name", "value"), &ShaderMaterial::set_shader_parameter);
	ClassDB::bind_method(D_METHOD("get_shader_parameter", "name"), &ShaderMaterial::get_shader_parameter);
	ClassDB::bind_method(D_METHOD("set_shader_parameters", "parameters"), &ShaderMaterial::set_shader_parameters);
	ClassDB::bind_method(D_METHOD("get_shader_parameters"), &ShaderMaterial::get_shader_parameters);
	ClassDB::bind_method(D_METHOD("get_shader_error"), &ShaderMaterial::get_shader_error);
	ClassDB::bind_method(D_METHOD("_shader_changed"), &ShaderMaterial::_shader_changed);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shader", PROPERTY_HINT_RESOURCE_TYPE, "Shader"), "set_shader", "get_shader");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "shader_parameters"), "set_shader_parameters", "get_shader_parameters");
}
