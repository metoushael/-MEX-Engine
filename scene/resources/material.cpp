#include "material.h"

#define RS3D RenderingServer3D::get_singleton()

Material::Material() {
	if (RS3D) {
		material = RS3D->material_create();
	}
}

Material::~Material() {
	if (RS3D && material.is_valid()) {
		RS3D->free_rid(material);
	}
}

// ---------------------------------------------------------------------------
// StandardMaterial3D
// ---------------------------------------------------------------------------
StandardMaterial3D::StandardMaterial3D() {
	_update();
}

void StandardMaterial3D::_update() {
	if (!RS3D || !material.is_valid()) {
		return;
	}
	RenderingServer3D::MaterialData d;
	d.albedo = albedo_color;
	if (albedo_texture.is_valid()) {
		d.albedo_texture = albedo_texture->get_rid_3d();
	}
	d.metallic = metallic;
	d.roughness = roughness;
	d.emission = emission;
	d.emission_energy = emission_energy;
	d.uv_scale = uv_scale;
	d.unshaded = unshaded;
	d.transparent = transparency;
	d.cull = (RenderingServer3D::CullMode)cull_mode;
	RS3D->material_set_data(material, d);
}

#define MATERIAL_SETTER(m_name, m_type, m_member, m_value) \
	void StandardMaterial3D::m_name(m_type p_value) { \
		m_member = m_value; \
		_update(); \
		emit_changed(); \
	}

MATERIAL_SETTER(set_albedo_color, const Color &, albedo_color, p_value)
MATERIAL_SETTER(set_albedo_texture, const Ref<Texture2D> &, albedo_texture, p_value)
MATERIAL_SETTER(set_metallic, real_t, metallic, Math::clamp(p_value, (real_t)0, (real_t)1))
MATERIAL_SETTER(set_roughness, real_t, roughness, Math::clamp(p_value, (real_t)0, (real_t)1))
MATERIAL_SETTER(set_emission, const Color &, emission, p_value)
MATERIAL_SETTER(set_emission_energy, real_t, emission_energy, std::max(p_value, (real_t)0))
MATERIAL_SETTER(set_uv_scale, const Vector2 &, uv_scale, p_value)
MATERIAL_SETTER(set_unshaded, bool, unshaded, p_value)
MATERIAL_SETTER(set_transparency, bool, transparency, p_value)
MATERIAL_SETTER(set_cull_mode, int, cull_mode, Math::clamp(p_value, 0, 2))

void StandardMaterial3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_albedo_color", "color"), &StandardMaterial3D::set_albedo_color);
	ClassDB::bind_method(D_METHOD("get_albedo_color"), &StandardMaterial3D::get_albedo_color);
	ClassDB::bind_method(D_METHOD("set_albedo_texture", "texture"), &StandardMaterial3D::set_albedo_texture);
	ClassDB::bind_method(D_METHOD("get_albedo_texture"), &StandardMaterial3D::get_albedo_texture);
	ClassDB::bind_method(D_METHOD("set_metallic", "metallic"), &StandardMaterial3D::set_metallic);
	ClassDB::bind_method(D_METHOD("get_metallic"), &StandardMaterial3D::get_metallic);
	ClassDB::bind_method(D_METHOD("set_roughness", "roughness"), &StandardMaterial3D::set_roughness);
	ClassDB::bind_method(D_METHOD("get_roughness"), &StandardMaterial3D::get_roughness);
	ClassDB::bind_method(D_METHOD("set_emission", "color"), &StandardMaterial3D::set_emission);
	ClassDB::bind_method(D_METHOD("get_emission"), &StandardMaterial3D::get_emission);
	ClassDB::bind_method(D_METHOD("set_emission_energy", "energy"), &StandardMaterial3D::set_emission_energy);
	ClassDB::bind_method(D_METHOD("get_emission_energy"), &StandardMaterial3D::get_emission_energy);
	ClassDB::bind_method(D_METHOD("set_uv_scale", "scale"), &StandardMaterial3D::set_uv_scale);
	ClassDB::bind_method(D_METHOD("get_uv_scale"), &StandardMaterial3D::get_uv_scale);
	ClassDB::bind_method(D_METHOD("set_unshaded", "unshaded"), &StandardMaterial3D::set_unshaded);
	ClassDB::bind_method(D_METHOD("is_unshaded"), &StandardMaterial3D::is_unshaded);
	ClassDB::bind_method(D_METHOD("set_transparency", "enabled"), &StandardMaterial3D::set_transparency);
	ClassDB::bind_method(D_METHOD("has_transparency"), &StandardMaterial3D::has_transparency);
	ClassDB::bind_method(D_METHOD("set_cull_mode", "mode"), &StandardMaterial3D::set_cull_mode);
	ClassDB::bind_method(D_METHOD("get_cull_mode"), &StandardMaterial3D::get_cull_mode);

	ADD_GROUP("Albedo");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "albedo_color"), "set_albedo_color", "get_albedo_color");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "albedo_texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_albedo_texture", "get_albedo_texture");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "uv_scale"), "set_uv_scale", "get_uv_scale");
	ADD_GROUP("Surface");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "metallic", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_metallic", "get_metallic");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "roughness", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_roughness", "get_roughness");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "emission"), "set_emission", "get_emission");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "emission_energy", PROPERTY_HINT_RANGE, "0,16,0.01"), "set_emission_energy", "get_emission_energy");
	ADD_GROUP("Rendering");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "unshaded"), "set_unshaded", "is_unshaded");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "transparency"), "set_transparency", "has_transparency");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cull_mode", PROPERTY_HINT_ENUM, "Back,Front,Disabled"), "set_cull_mode", "get_cull_mode");
}

// ---------------------------------------------------------------------------
// Environment
// ---------------------------------------------------------------------------
#define ENVIRONMENT_SETTER(m_name, m_type, m_member, m_value) \
	void Environment::m_name(m_type p_value) { \
		data.m_member = m_value; \
		emit_changed(); \
	}

ENVIRONMENT_SETTER(set_background_mode, int, background, (RenderingServer3D::BackgroundMode)Math::clamp(p_value, 0, 1))
ENVIRONMENT_SETTER(set_background_color, const Color &, background_color, p_value)
ENVIRONMENT_SETTER(set_sky_top_color, const Color &, sky_top_color, p_value)
ENVIRONMENT_SETTER(set_sky_horizon_color, const Color &, sky_horizon_color, p_value)
ENVIRONMENT_SETTER(set_ground_color, const Color &, ground_color, p_value)
ENVIRONMENT_SETTER(set_ambient_light_color, const Color &, ambient_color, p_value)
ENVIRONMENT_SETTER(set_ambient_light_energy, real_t, ambient_energy, std::max(p_value, (real_t)0))
ENVIRONMENT_SETTER(set_fog_enabled, bool, fog_enabled, p_value)
ENVIRONMENT_SETTER(set_fog_color, const Color &, fog_color, p_value)
ENVIRONMENT_SETTER(set_fog_density, real_t, fog_density, std::max(p_value, (real_t)0))

void Environment::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_background_mode", "mode"), &Environment::set_background_mode);
	ClassDB::bind_method(D_METHOD("get_background_mode"), &Environment::get_background_mode);
	ClassDB::bind_method(D_METHOD("set_background_color", "color"), &Environment::set_background_color);
	ClassDB::bind_method(D_METHOD("get_background_color"), &Environment::get_background_color);
	ClassDB::bind_method(D_METHOD("set_sky_top_color", "color"), &Environment::set_sky_top_color);
	ClassDB::bind_method(D_METHOD("get_sky_top_color"), &Environment::get_sky_top_color);
	ClassDB::bind_method(D_METHOD("set_sky_horizon_color", "color"), &Environment::set_sky_horizon_color);
	ClassDB::bind_method(D_METHOD("get_sky_horizon_color"), &Environment::get_sky_horizon_color);
	ClassDB::bind_method(D_METHOD("set_ground_color", "color"), &Environment::set_ground_color);
	ClassDB::bind_method(D_METHOD("get_ground_color"), &Environment::get_ground_color);
	ClassDB::bind_method(D_METHOD("set_ambient_light_color", "color"), &Environment::set_ambient_light_color);
	ClassDB::bind_method(D_METHOD("get_ambient_light_color"), &Environment::get_ambient_light_color);
	ClassDB::bind_method(D_METHOD("set_ambient_light_energy", "energy"), &Environment::set_ambient_light_energy);
	ClassDB::bind_method(D_METHOD("get_ambient_light_energy"), &Environment::get_ambient_light_energy);
	ClassDB::bind_method(D_METHOD("set_fog_enabled", "enabled"), &Environment::set_fog_enabled);
	ClassDB::bind_method(D_METHOD("is_fog_enabled"), &Environment::is_fog_enabled);
	ClassDB::bind_method(D_METHOD("set_fog_color", "color"), &Environment::set_fog_color);
	ClassDB::bind_method(D_METHOD("get_fog_color"), &Environment::get_fog_color);
	ClassDB::bind_method(D_METHOD("set_fog_density", "density"), &Environment::set_fog_density);
	ClassDB::bind_method(D_METHOD("get_fog_density"), &Environment::get_fog_density);

	ADD_GROUP("Background");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "background_mode", PROPERTY_HINT_ENUM, "Color,Sky"), "set_background_mode", "get_background_mode");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "background_color"), "set_background_color", "get_background_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "sky_top_color"), "set_sky_top_color", "get_sky_top_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "sky_horizon_color"), "set_sky_horizon_color", "get_sky_horizon_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "ground_color"), "set_ground_color", "get_ground_color");
	ADD_GROUP("Ambient Light");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "ambient_light_color"), "set_ambient_light_color", "get_ambient_light_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ambient_light_energy", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_ambient_light_energy", "get_ambient_light_energy");
	ADD_GROUP("Fog");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "fog_enabled"), "set_fog_enabled", "is_fog_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "fog_color"), "set_fog_color", "get_fog_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fog_density", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_fog_density", "get_fog_density");
}
