// RenderingServer3D : rendu 3D accéléré (OpenGL 2.1 + shaders GLSL 1.20).
//
// Le serveur partage le contexte OpenGL du SDL_Renderer : il dessine la scène 3D dans une
// texture cible (un « viewport »), que le rendu 2D compose ensuite comme n'importe quelle
// image. Le canvas 2D, l'interface du jeu et l'éditeur ImGui restent donc inchangés et se
// dessinent par-dessus la 3D, comme dans Godot.
//
// Si OpenGL n'est pas disponible (rendu logiciel, mode sans affichage), is_available()
// retourne false : toutes les fonctions restent appelables mais rien n'est dessiné.
#pragma once

#include "core/math/aabb.h"
#include "core/math/projection.h"
#include "servers/rendering_server.h"

class RenderingServer3D {
public:
	struct Vertex {
		Vector3 position;
		Vector3 normal;
		Vector2 uv;
		Color color = Color(1, 1, 1, 1);
	};
	enum Primitive {
		PRIMITIVE_TRIANGLES,
		PRIMITIVE_LINES,
	};
	enum CullMode {
		CULL_BACK,
		CULL_FRONT,
		CULL_DISABLED,
	};
	enum LightType {
		LIGHT_DIRECTIONAL,
		LIGHT_OMNI,
		LIGHT_SPOT,
	};
	enum BackgroundMode {
		BG_COLOR,
		BG_SKY,
	};
	struct MaterialData {
		Color albedo = Color(1, 1, 1, 1);
		RID albedo_texture; // texture 3D (texture_create)
		float metallic = 0.0f;
		float roughness = 0.7f;
		Color emission = Color(0, 0, 0, 1);
		float emission_energy = 1.0f;
		Vector2 uv_scale = Vector2(1, 1);
		bool unshaded = false;
		bool transparent = false;
		CullMode cull = CULL_BACK;
	};
	struct LightData {
		LightType type = LIGHT_DIRECTIONAL;
		Color color = Color(1, 1, 1, 1);
		float energy = 1.0f;
		float range = 10.0f; // omni et spot
		float attenuation = 1.0f;
		float spot_angle = 45.0f; // demi-angle, en degrés
		bool shadow = false; // lumière directionnelle uniquement
	};
	struct EnvironmentData {
		BackgroundMode background = BG_COLOR;
		Color background_color = Color(0.3f, 0.3f, 0.3f, 1);
		Color sky_top_color = Color(0.25f, 0.45f, 0.85f, 1);
		Color sky_horizon_color = Color(0.70f, 0.82f, 0.95f, 1);
		Color ground_color = Color(0.32f, 0.30f, 0.28f, 1);
		Color ambient_color = Color(1, 1, 1, 1);
		float ambient_energy = 0.35f;
		bool fog_enabled = false;
		Color fog_color = Color(0.7f, 0.8f, 0.9f, 1);
		float fog_density = 0.02f;
	};
	struct CameraData {
		Transform3D transform;
		bool orthogonal = false;
		float fov = 70.0f; // vertical, en degrés
		float size = 10.0f; // hauteur visible en projection orthogonale
		float z_near = 0.05f;
		float z_far = 500.0f;

		Projection get_projection(float p_aspect) const;
	};
	struct Line {
		Vector3 from, to;
		Color color;
	};
	struct RenderExtras {
		const std::vector<Line> *lines = nullptr; // avec test de profondeur
		const std::vector<Line> *overlay_lines = nullptr; // toujours visibles
		bool use_environment = true; // faux : fond uni (aperçus de l'éditeur)
		Color fallback_background = Color(0.19f, 0.2f, 0.22f, 1);
	};

private:
	struct Impl;
	Impl *impl = nullptr;
	static RenderingServer3D *singleton;

public:
	static RenderingServer3D *get_singleton() { return singleton; }

	// p_msaa : 0 (désactivé), 2, 4 ou 8 échantillons.
	void init(SDL_Renderer *p_renderer, int p_msaa, int p_shadow_size);
	void finish();
	bool is_available() const;
	String get_driver_info() const;

	// --- Textures 3D (mipmaps, répétition) ---
	RID texture_create(int p_width, int p_height, const uint8_t *p_rgba, bool p_linear = true, bool p_repeat = true);

	// --- Maillages ---
	RID mesh_create();
	void mesh_set_data(RID p_mesh, const std::vector<Vertex> &p_vertices, const std::vector<uint32_t> &p_indices, Primitive p_primitive = PRIMITIVE_TRIANGLES);
	AABB mesh_get_aabb(RID p_mesh) const;

	// --- Matériaux ---
	RID material_create();
	void material_set_data(RID p_material, const MaterialData &p_data);

	// --- Scénario : ensemble d'instances, de lumières et un environnement ---
	RID scenario_create();
	void scenario_set_environment(RID p_scenario, const EnvironmentData &p_environment);
	void scenario_clear_environment(RID p_scenario);

	RID instance_create(RID p_scenario);
	void instance_set_mesh(RID p_instance, RID p_mesh);
	void instance_set_material(RID p_instance, RID p_material);
	void instance_set_transform(RID p_instance, const Transform3D &p_transform);
	void instance_set_visible(RID p_instance, bool p_visible);
	void instance_set_cast_shadows(RID p_instance, bool p_enabled);

	RID light_create(RID p_scenario);
	void light_set_data(RID p_light, const LightData &p_data);
	void light_set_transform(RID p_light, const Transform3D &p_transform);
	void light_set_visible(RID p_light, bool p_visible);

	// --- Viewports : cibles de rendu 3D ---
	RID viewport_create(int p_width, int p_height);
	void viewport_set_size(RID p_viewport, int p_width, int p_height);
	Size2 viewport_get_size(RID p_viewport) const;
	RID viewport_get_texture(RID p_viewport) const; // texture 2D (RenderingServer)
	void render(RID p_viewport, RID p_scenario, const CameraData &p_camera, const RenderExtras &p_extras);
	void render(RID p_viewport, RID p_scenario, const CameraData &p_camera) { render(p_viewport, p_scenario, p_camera, RenderExtras()); }

	void free_rid(RID p_rid);

	void set_shadow_distance(float p_distance);
	void reset_stats(); // les compteurs sont aussi remis à zéro au début de chaque rendu
	int get_stat_draw_calls() const;
	int get_stat_triangles() const;

	RenderingServer3D();
	~RenderingServer3D();
};
