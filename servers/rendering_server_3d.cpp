#include "rendering_server_3d.h"

#include "core/string/ustring.h"
#include "servers/gl_api.h"

#include <SDL.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>

// ---------------------------------------------------------------------------
// Chargement des fonctions OpenGL
// ---------------------------------------------------------------------------
namespace gl {
#define MEX_GL_DEFINE(ret, name, args) Fn##name name = nullptr;
MEX_GL_FUNCTIONS(MEX_GL_DEFINE, MEX_GL_DEFINE, MEX_GL_DEFINE)
#undef MEX_GL_DEFINE

const char *load() {
	const char *missing = nullptr;
#define MEX_GL_LOAD(ret, name, args) \
	name = (Fn##name)SDL_GL_GetProcAddress("gl" #name); \
	if (!name && !missing) { \
		missing = "gl" #name; \
	}
#define MEX_GL_LOAD_FBO(ret, name, args) \
	name = (Fn##name)SDL_GL_GetProcAddress("gl" #name); \
	if (!name) { \
		name = (Fn##name)SDL_GL_GetProcAddress("gl" #name "EXT"); \
	} \
	if (!name && !missing) { \
		missing = "gl" #name; \
	}
#define MEX_GL_LOAD_OPTIONAL(ret, name, args) \
	name = (Fn##name)SDL_GL_GetProcAddress("gl" #name); \
	if (!name) { \
		name = (Fn##name)SDL_GL_GetProcAddress("gl" #name "EXT"); \
	}
	MEX_GL_FUNCTIONS(MEX_GL_LOAD, MEX_GL_LOAD_FBO, MEX_GL_LOAD_OPTIONAL)
#undef MEX_GL_LOAD
#undef MEX_GL_LOAD_FBO
#undef MEX_GL_LOAD_OPTIONAL
	return missing;
}
} // namespace gl

using namespace gl;

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------
namespace {

static_assert(sizeof(RenderingServer3D::Vertex) == 12 * sizeof(float), "Vertex doit être compact (12 flottants).");

const int MAX_LIGHTS = 8;

const char *SCENE_VERTEX = R"(#version 120
attribute vec3 a_position;
attribute vec3 a_normal;
attribute vec2 a_uv;
attribute vec4 a_color;
uniform mat4 u_mvp;
uniform mat4 u_model;
uniform mat3 u_normal_matrix;
uniform mat4 u_shadow_matrix;
uniform float u_shadow_normal_bias;
uniform vec2 u_uv_scale;
varying vec3 v_world;
varying vec3 v_normal;
varying vec2 v_uv;
varying vec4 v_color;
varying vec4 v_shadow;
void main() {
	vec4 world = u_model * vec4(a_position, 1.0);
	v_world = world.xyz;
	v_normal = u_normal_matrix * a_normal;
	v_uv = a_uv * u_uv_scale;
	v_color = a_color;
	v_shadow = u_shadow_matrix * vec4(world.xyz + normalize(v_normal) * u_shadow_normal_bias, 1.0);
	gl_Position = u_mvp * vec4(a_position, 1.0);
}
)";

const char *SCENE_FRAGMENT = R"(#version 120
#define MAX_LIGHTS 8
uniform vec4 u_albedo;
uniform sampler2D u_texture;
uniform float u_use_texture;
uniform float u_metallic;
uniform float u_roughness;
uniform vec3 u_emission;
uniform float u_unshaded;
uniform vec3 u_camera_position;
uniform vec3 u_ambient_sky;
uniform vec3 u_ambient_ground;
uniform int u_light_count;
uniform vec4 u_light_position[MAX_LIGHTS]; // xyz : position (ou direction vers la lumiere), w : type
uniform vec4 u_light_color[MAX_LIGHTS]; // rgb : couleur * energie, a : portee
uniform vec4 u_light_direction[MAX_LIGHTS]; // xyz : axe du cone, w : cos du demi-angle
uniform vec4 u_light_params[MAX_LIGHTS]; // x : attenuation, y : projette une ombre
uniform sampler2DShadow u_shadow_map;
uniform float u_shadow_texel;
uniform float u_shadow_distance;
uniform vec4 u_fog; // rgb : couleur (lineaire), a : densite
varying vec3 v_world;
varying vec3 v_normal;
varying vec2 v_uv;
varying vec4 v_color;
varying vec4 v_shadow;

float shadow_factor(float p_distance) {
	vec3 p = v_shadow.xyz;
	if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) {
		return 1.0;
	}
	float t = u_shadow_texel;
	float s = shadow2D(u_shadow_map, vec3(p.xy + vec2(-0.6, -0.3) * t, p.z)).r;
	s += shadow2D(u_shadow_map, vec3(p.xy + vec2(0.3, -0.6) * t, p.z)).r;
	s += shadow2D(u_shadow_map, vec3(p.xy + vec2(0.6, 0.3) * t, p.z)).r;
	s += shadow2D(u_shadow_map, vec3(p.xy + vec2(-0.3, 0.6) * t, p.z)).r;
	s *= 0.25;
	// Fondu en limite de distance d'ombre.
	float fade = clamp((u_shadow_distance - p_distance) / (0.15 * u_shadow_distance), 0.0, 1.0);
	return mix(1.0, s, fade);
}

void main() {
	vec4 base = u_albedo * v_color;
	if (u_use_texture > 0.5) {
		base *= texture2D(u_texture, v_uv);
	}
	vec3 albedo = pow(base.rgb, vec3(2.2));
	vec3 to_camera = u_camera_position - v_world;
	float distance_to_camera = length(to_camera);
	vec3 color;
	if (u_unshaded > 0.5) {
		color = albedo;
	} else {
		vec3 N = normalize(v_normal);
		if (!gl_FrontFacing) {
			N = -N;
		}
		vec3 V = to_camera / max(distance_to_camera, 0.0001);
		vec3 diffuse_color = albedo * (1.0 - u_metallic);
		vec3 specular_color = mix(vec3(0.04), albedo, u_metallic);
		float a = max(u_roughness * u_roughness, 0.02);
		float shininess = clamp(2.0 / (a * a) - 2.0, 1.0, 2048.0);
		float specular_norm = (shininess + 8.0) / 8.0;
		vec3 ambient = mix(u_ambient_ground, u_ambient_sky, N.y * 0.5 + 0.5);
		color = ambient * (diffuse_color + specular_color * 0.5);
		for (int i = 0; i < MAX_LIGHTS; i++) {
			if (i >= u_light_count) {
				break;
			}
			vec3 L;
			float attenuation = 1.0;
			float type = u_light_position[i].w;
			if (type < 0.5) {
				L = u_light_position[i].xyz;
				if (u_light_params[i].y > 0.5) {
					attenuation = shadow_factor(distance_to_camera);
				}
			} else {
				vec3 delta = u_light_position[i].xyz - v_world;
				float d = length(delta);
				L = delta / max(d, 0.0001);
				float f = clamp(1.0 - d / u_light_color[i].a, 0.0, 1.0);
				attenuation = pow(f, 2.0 * u_light_params[i].x);
				if (type > 1.5) {
					float c = dot(-L, u_light_direction[i].xyz);
					float outer = u_light_direction[i].w;
					attenuation *= smoothstep(outer, mix(outer, 1.0, 0.25), c);
				}
			}
			float ndl = max(dot(N, L), 0.0);
			if (ndl > 0.0 && attenuation > 0.0) {
				vec3 H = normalize(L + V);
				float spec = pow(max(dot(N, H), 0.0), shininess) * specular_norm;
				color += (diffuse_color + specular_color * spec) * u_light_color[i].rgb * (ndl * attenuation);
			}
		}
	}
	color += u_emission;
	float fog = 1.0 - exp(-u_fog.a * distance_to_camera);
	color = mix(color, u_fog.rgb, fog);
	gl_FragColor = vec4(pow(max(color, vec3(0.0)), vec3(1.0 / 2.2)), base.a);
}
)";

const char *DEPTH_VERTEX = R"(#version 120
attribute vec3 a_position;
uniform mat4 u_mvp;
void main() {
	gl_Position = u_mvp * vec4(a_position, 1.0);
}
)";

const char *DEPTH_FRAGMENT = R"(#version 120
void main() {
	gl_FragColor = vec4(1.0);
}
)";

const char *COLOR_VERTEX = R"(#version 120
attribute vec3 a_position;
attribute vec4 a_color;
uniform mat4 u_mvp;
uniform vec4 u_modulate;
varying vec4 v_color;
void main() {
	v_color = a_color * u_modulate;
	gl_Position = u_mvp * vec4(a_position, 1.0);
}
)";

const char *COLOR_FRAGMENT = R"(#version 120
varying vec4 v_color;
void main() {
	gl_FragColor = v_color;
}
)";

const char *SKY_VERTEX = R"(#version 120
attribute vec3 a_position; // xy : coin de l'ecran
attribute vec3 a_normal; // direction du regard dans le monde
varying vec3 v_direction;
void main() {
	v_direction = a_normal;
	gl_Position = vec4(a_position.xy, 1.0, 1.0);
}
)";

const char *SKY_FRAGMENT = R"(#version 120
uniform vec3 u_top;
uniform vec3 u_horizon;
uniform vec3 u_ground;
uniform vec3 u_sun_direction;
uniform vec3 u_sun_color;
uniform vec4 u_fog; // rgb : couleur, a : opacite a l'horizon
varying vec3 v_direction;
void main() {
	vec3 d = normalize(v_direction);
	vec3 color;
	if (d.y >= 0.0) {
		color = mix(u_horizon, u_top, pow(d.y, 0.55));
	} else {
		color = mix(u_horizon, u_ground, pow(min(-d.y * 6.0, 1.0), 0.6));
	}
	float sun = max(dot(d, u_sun_direction), 0.0);
	color += u_sun_color * (pow(sun, 900.0) * 1.5 + pow(sun, 12.0) * 0.12);
	color = mix(color, u_fog.rgb, u_fog.a * (1.0 - smoothstep(0.0, 0.35, abs(d.y))));
	gl_FragColor = vec4(color, 1.0);
}
)";

struct Mat4 {
	float m[16];
};

inline Vector3 _linear(const Color &c, float p_scale = 1.0f) {
	return Vector3(std::pow(std::max(c.r, 0.0f), 2.2f), std::pow(std::max(c.g, 0.0f), 2.2f), std::pow(std::max(c.b, 0.0f), 2.2f)) * p_scale;
}

} // namespace

Projection RenderingServer3D::CameraData::get_projection(float p_aspect) const {
	const float n = std::max(z_near, 0.0001f);
	const float f = std::max(z_far, n + 0.01f);
	if (orthogonal) {
		const float h = std::max(size, 0.001f) * 0.5f;
		return Projection::orthographic(-h * p_aspect, h * p_aspect, -h, h, n, f);
	}
	return Projection::perspective(Math::clamp(fov, 1.0f, 179.0f), p_aspect, n, f);
}

// ---------------------------------------------------------------------------
// Données internes
// ---------------------------------------------------------------------------
struct RenderingServer3D::Impl {
	struct Program {
		GLuint id = 0;
		std::unordered_map<std::string, GLint> locations;
		GLint loc(const char *p_name) {
			auto it = locations.find(p_name);
			if (it != locations.end()) {
				return it->second;
			}
			const GLint l = GetUniformLocation(id, p_name);
			locations[p_name] = l;
			return l;
		}
	};
	struct Texture {
		GLuint id = 0;
	};
	struct Mesh {
		GLuint vbo = 0, ibo = 0;
		int vertex_count = 0, index_count = 0;
		Primitive primitive = PRIMITIVE_TRIANGLES;
		AABB aabb;
	};
	struct Instance {
		uint64_t scenario = 0;
		RID mesh, material;
		Transform3D transform;
		bool visible = true;
		bool cast_shadows = true;
	};
	struct Light {
		uint64_t scenario = 0;
		LightData data;
		Transform3D transform;
		bool visible = true;
	};
	struct Scenario {
		EnvironmentData environment;
		bool has_environment = false;
	};
	struct Viewport {
		RID texture;
		GLuint texture_id = 0;
		GLuint fbo = 0, depth_rb = 0;
		GLuint ms_fbo = 0, ms_color_rb = 0, ms_depth_rb = 0;
		int width = 0, height = 0;
	};

	SDL_Renderer *renderer = nullptr;
	bool available = false;
	int max_anisotropy = 0; // 0 : extension absente
	String info;
	int msaa = 0;
	int shadow_size = 2048;
	float shadow_distance = 30.0f;
	uint64_t next_id = 1;
	int stat_draw_calls = 0, stat_triangles = 0;

	std::unordered_map<uint64_t, Texture> textures;
	std::unordered_map<uint64_t, Mesh> meshes;
	std::unordered_map<uint64_t, MaterialData> materials;
	std::unordered_map<uint64_t, Instance> instances;
	std::unordered_map<uint64_t, Light> lights;
	std::unordered_map<uint64_t, Scenario> scenarios;
	std::unordered_map<uint64_t, Viewport> viewports;

	Program scene_program, depth_program, color_program, sky_program;
	GLuint shadow_fbo = 0, shadow_texture = 0;
	GLuint white_texture = 0;
	MaterialData default_material;

	// Sauvegarde et restaure l'état OpenGL attendu par SDL_Renderer.
	struct StateScope {
		GLint fbo = 0, program = 0, array_buffer = 0, element_buffer = 0;
		StateScope(SDL_Renderer *p_renderer) {
			SDL_RenderFlush(p_renderer);
			GetIntegerv(FRAMEBUFFER_BINDING, &fbo);
			GetIntegerv(CURRENT_PROGRAM, &program);
			GetIntegerv(ARRAY_BUFFER_BINDING, &array_buffer);
			GetIntegerv(ELEMENT_ARRAY_BUFFER_BINDING, &element_buffer);
			PushAttrib(ALL_ATTRIB_BITS);
			PushClientAttrib(CLIENT_ALL_ATTRIB_BITS);
		}
		~StateScope() {
			PopClientAttrib();
			PopAttrib();
			UseProgram((GLuint)program);
			BindFramebuffer(FRAMEBUFFER, (GLuint)fbo);
			BindBuffer(ARRAY_BUFFER, (GLuint)array_buffer);
			BindBuffer(ELEMENT_ARRAY_BUFFER, (GLuint)element_buffer);
			while (GetError() != 0) {
			}
		}
	};

	RID make_rid() {
		RID rid;
		rid.id = next_id++;
		return rid;
	}

	bool compile(Program &r_program, const char *p_name, const char *p_vertex, const char *p_fragment) {
		GLuint shaders[2];
		const char *sources[2] = { p_vertex, p_fragment };
		const GLenum types[2] = { VERTEX_SHADER, FRAGMENT_SHADER };
		for (int i = 0; i < 2; i++) {
			shaders[i] = CreateShader(types[i]);
			ShaderSource(shaders[i], 1, &sources[i], nullptr);
			CompileShader(shaders[i]);
			GLint ok = 0;
			GetShaderiv(shaders[i], COMPILE_STATUS, &ok);
			if (!ok) {
				char log[2048] = "";
				GLsizei len = 0;
				GetShaderInfoLog(shaders[i], sizeof(log) - 1, &len, log);
				print_error(vformat("Rendu 3D : erreur de compilation du shader '%s' (%s) :\n%s", p_name, i == 0 ? "sommets" : "fragments", log));
				return false;
			}
		}
		r_program.id = CreateProgram();
		AttachShader(r_program.id, shaders[0]);
		AttachShader(r_program.id, shaders[1]);
		BindAttribLocation(r_program.id, 0, "a_position");
		BindAttribLocation(r_program.id, 1, "a_normal");
		BindAttribLocation(r_program.id, 2, "a_uv");
		BindAttribLocation(r_program.id, 3, "a_color");
		LinkProgram(r_program.id);
		GLint ok = 0;
		GetProgramiv(r_program.id, LINK_STATUS, &ok);
		DeleteShader(shaders[0]);
		DeleteShader(shaders[1]);
		if (!ok) {
			char log[2048] = "";
			GLsizei len = 0;
			GetProgramInfoLog(r_program.id, sizeof(log) - 1, &len, log);
			print_error(vformat("Rendu 3D : erreur d'édition de liens du shader '%s' :\n%s", p_name, log));
			return false;
		}
		return true;
	}

	void destroy_viewport_buffers(Viewport &vp) {
		if (vp.fbo) {
			DeleteFramebuffers(1, &vp.fbo);
		}
		if (vp.depth_rb) {
			DeleteRenderbuffers(1, &vp.depth_rb);
		}
		if (vp.ms_fbo) {
			DeleteFramebuffers(1, &vp.ms_fbo);
		}
		if (vp.ms_color_rb) {
			DeleteRenderbuffers(1, &vp.ms_color_rb);
		}
		if (vp.ms_depth_rb) {
			DeleteRenderbuffers(1, &vp.ms_depth_rb);
		}
		vp.fbo = vp.depth_rb = vp.ms_fbo = vp.ms_color_rb = vp.ms_depth_rb = 0;
		if (vp.texture.is_valid() && RenderingServer::get_singleton()) {
			RenderingServer::get_singleton()->free_rid(vp.texture);
		}
		vp.texture = RID();
		vp.texture_id = 0;
	}

	void create_viewport_buffers(Viewport &vp, int p_width, int p_height) {
		vp.width = std::max(p_width, 1);
		vp.height = std::max(p_height, 1);
		RenderingServer *rs = RenderingServer::get_singleton();
		vp.texture = rs->texture_create_target(vp.width, vp.height);
		if (!available || !vp.texture.is_valid()) {
			return;
		}
		// Nom OpenGL de la texture SDL : elle devient l'attache couleur de notre framebuffer.
		SDL_Texture *native = rs->texture_get_native(vp.texture);
		SDL_RenderFlush(renderer);
		GLint bound = 0;
		if (SDL_GL_BindTexture(native, nullptr, nullptr) == 0) {
			GetIntegerv(TEXTURE_BINDING_2D, &bound);
			SDL_GL_UnbindTexture(native);
		}
		if (bound == 0) {
			print_error("Rendu 3D : texture cible inutilisable (SDL_GL_BindTexture).");
			return;
		}
		vp.texture_id = (GLuint)bound;

		StateScope scope(renderer);
		GenFramebuffers(1, &vp.fbo);
		BindFramebuffer(FRAMEBUFFER, vp.fbo);
		FramebufferTexture2D(FRAMEBUFFER, COLOR_ATTACHMENT0, TEXTURE_2D, vp.texture_id, 0);
		const bool multisample = msaa > 1 && RenderbufferStorageMultisample && BlitFramebuffer;
		if (!multisample) {
			GenRenderbuffers(1, &vp.depth_rb);
			BindRenderbuffer(RENDERBUFFER, vp.depth_rb);
			RenderbufferStorage(RENDERBUFFER, DEPTH_COMPONENT24, vp.width, vp.height);
			FramebufferRenderbuffer(FRAMEBUFFER, DEPTH_ATTACHMENT, RENDERBUFFER, vp.depth_rb);
		}
		if (CheckFramebufferStatus(FRAMEBUFFER) != FRAMEBUFFER_COMPLETE) {
			print_error("Rendu 3D : framebuffer incomplet.");
			vp.texture_id = 0;
			return;
		}
		if (multisample) {
			GenFramebuffers(1, &vp.ms_fbo);
			BindFramebuffer(FRAMEBUFFER, vp.ms_fbo);
			GenRenderbuffers(1, &vp.ms_color_rb);
			BindRenderbuffer(RENDERBUFFER, vp.ms_color_rb);
			RenderbufferStorageMultisample(RENDERBUFFER, msaa, RGBA8, vp.width, vp.height);
			FramebufferRenderbuffer(FRAMEBUFFER, COLOR_ATTACHMENT0, RENDERBUFFER, vp.ms_color_rb);
			GenRenderbuffers(1, &vp.ms_depth_rb);
			BindRenderbuffer(RENDERBUFFER, vp.ms_depth_rb);
			RenderbufferStorageMultisample(RENDERBUFFER, msaa, DEPTH_COMPONENT24, vp.width, vp.height);
			FramebufferRenderbuffer(FRAMEBUFFER, DEPTH_ATTACHMENT, RENDERBUFFER, vp.ms_depth_rb);
			if (CheckFramebufferStatus(FRAMEBUFFER) != FRAMEBUFFER_COMPLETE) {
				// Anticrénelage refusé par le pilote : repli sur un rendu simple.
				print_warning("Rendu 3D : anticrénelage (MSAA) indisponible, désactivé.");
				DeleteFramebuffers(1, &vp.ms_fbo);
				DeleteRenderbuffers(1, &vp.ms_color_rb);
				DeleteRenderbuffers(1, &vp.ms_depth_rb);
				vp.ms_fbo = vp.ms_color_rb = vp.ms_depth_rb = 0;
				msaa = 0;
				BindFramebuffer(FRAMEBUFFER, vp.fbo);
				GenRenderbuffers(1, &vp.depth_rb);
				BindRenderbuffer(RENDERBUFFER, vp.depth_rb);
				RenderbufferStorage(RENDERBUFFER, DEPTH_COMPONENT24, vp.width, vp.height);
				FramebufferRenderbuffer(FRAMEBUFFER, DEPTH_ATTACHMENT, RENDERBUFFER, vp.depth_rb);
			}
		}
	}

	void bind_mesh(const Mesh &mesh, bool p_full) {
		BindBuffer(ARRAY_BUFFER, mesh.vbo);
		BindBuffer(ELEMENT_ARRAY_BUFFER, mesh.ibo);
		const GLsizei stride = (GLsizei)sizeof(Vertex);
		EnableVertexAttribArray(0);
		VertexAttribPointer(0, 3, FLOAT, 0, stride, (const void *)offsetof(Vertex, position));
		if (p_full) {
			EnableVertexAttribArray(1);
			VertexAttribPointer(1, 3, FLOAT, 0, stride, (const void *)offsetof(Vertex, normal));
			EnableVertexAttribArray(2);
			VertexAttribPointer(2, 2, FLOAT, 0, stride, (const void *)offsetof(Vertex, uv));
		} else {
			DisableVertexAttribArray(1);
			DisableVertexAttribArray(2);
		}
		EnableVertexAttribArray(3);
		VertexAttribPointer(3, 4, FLOAT, 0, stride, (const void *)offsetof(Vertex, color));
	}

	void draw_mesh(const Mesh &mesh) {
		const GLenum mode = mesh.primitive == PRIMITIVE_LINES ? LINES : TRIANGLES;
		if (mesh.index_count > 0) {
			DrawElements(mode, mesh.index_count, UNSIGNED_INT, nullptr);
			stat_triangles += mesh.index_count / 3;
		} else {
			DrawArrays(mode, 0, mesh.vertex_count);
			stat_triangles += mesh.vertex_count / 3;
		}
		stat_draw_calls++;
	}

	void draw_lines(const std::vector<Line> &p_lines, const Projection &p_view_projection) {
		if (p_lines.empty()) {
			return;
		}
		static std::vector<float> buffer;
		buffer.clear();
		buffer.reserve(p_lines.size() * 14);
		for (const Line &l : p_lines) {
			const Vector3 pts[2] = { l.from, l.to };
			for (const Vector3 &p : pts) {
				buffer.push_back(p.x);
				buffer.push_back(p.y);
				buffer.push_back(p.z);
				buffer.push_back(l.color.r);
				buffer.push_back(l.color.g);
				buffer.push_back(l.color.b);
				buffer.push_back(l.color.a);
			}
		}
		BindBuffer(ARRAY_BUFFER, 0);
		BindBuffer(ELEMENT_ARRAY_BUFFER, 0);
		UseProgram(color_program.id);
		UniformMatrix4fv(color_program.loc("u_mvp"), 1, 0, p_view_projection.m);
		Uniform4f(color_program.loc("u_modulate"), 1, 1, 1, 1);
		EnableVertexAttribArray(0);
		VertexAttribPointer(0, 3, FLOAT, 0, 7 * sizeof(float), buffer.data());
		DisableVertexAttribArray(1);
		DisableVertexAttribArray(2);
		EnableVertexAttribArray(3);
		VertexAttribPointer(3, 4, FLOAT, 0, 7 * sizeof(float), buffer.data() + 3);
		DrawArrays(LINES, 0, (GLsizei)(p_lines.size() * 2));
		stat_draw_calls++;
	}
};

RenderingServer3D *RenderingServer3D::singleton = nullptr;

RenderingServer3D::RenderingServer3D() {
	singleton = this;
	impl = new Impl;
}

RenderingServer3D::~RenderingServer3D() {
	delete impl;
	if (singleton == this) {
		singleton = nullptr;
	}
}

bool RenderingServer3D::is_available() const {
	return impl->available;
}

String RenderingServer3D::get_driver_info() const {
	return impl->info;
}

void RenderingServer3D::set_shadow_distance(float p_distance) {
	impl->shadow_distance = std::max(p_distance, 1.0f);
}

void RenderingServer3D::reset_stats() {
	impl->stat_draw_calls = impl->stat_triangles = 0;
}

int RenderingServer3D::get_stat_draw_calls() const {
	return impl->stat_draw_calls;
}

int RenderingServer3D::get_stat_triangles() const {
	return impl->stat_triangles;
}

void RenderingServer3D::init(SDL_Renderer *p_renderer, int p_msaa, int p_shadow_size) {
	impl->renderer = p_renderer;
	impl->available = false;
	if (!p_renderer) {
		return;
	}
	SDL_RendererInfo renderer_info;
	if (SDL_GetRendererInfo(p_renderer, &renderer_info) != 0 || String(renderer_info.name) != "opengl") {
		impl->info = String("rendu 2D « ") + (renderer_info.name ? renderer_info.name : "?") + " » sans OpenGL";
		return;
	}
	SDL_RenderFlush(p_renderer);
	if (const char *missing = gl::load()) {
		impl->info = String("fonction OpenGL manquante : ") + missing;
		print_warning("Rendu 3D désactivé : le pilote OpenGL ne fournit pas " + String(missing) + " (OpenGL 2.1 avec framebuffer objects requis).");
		return;
	}
	const char *version = (const char *)GetString(VERSION);
	const char *gpu = (const char *)GetString(RENDERER);
	impl->info = String("OpenGL ") + (version ? version : "?") + " - " + (gpu ? gpu : "?");
	// Filtrage anisotrope : les textures vues en biais (sols, murs) restent nettes au loin.
	const char *extensions = (const char *)GetString((GLenum)0x1F03); // GL_EXTENSIONS
	if (extensions && strstr(extensions, "GL_EXT_texture_filter_anisotropic")) {
		GLint max_level = 0;
		GetIntegerv((GLenum)0x84FF, &max_level); // GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
		impl->max_anisotropy = std::min((int)max_level, 8);
	}

	Impl::StateScope scope(p_renderer);
	if (!impl->compile(impl->scene_program, "scène", SCENE_VERTEX, SCENE_FRAGMENT) ||
			!impl->compile(impl->depth_program, "profondeur", DEPTH_VERTEX, DEPTH_FRAGMENT) ||
			!impl->compile(impl->color_program, "couleur", COLOR_VERTEX, COLOR_FRAGMENT) ||
			!impl->compile(impl->sky_program, "ciel", SKY_VERTEX, SKY_FRAGMENT)) {
		print_warning("Rendu 3D désactivé : shaders GLSL 1.20 refusés par le pilote.");
		return;
	}

	GLint max_samples = 0;
	if (RenderbufferStorageMultisample && BlitFramebuffer) {
		GetIntegerv(MAX_SAMPLES, &max_samples);
	}
	impl->msaa = std::min(std::max(p_msaa, 0), (int)max_samples);
	if (impl->msaa < 2) {
		impl->msaa = 0;
	}

	// Texture blanche (matériaux sans texture).
	const unsigned char white[4] = { 255, 255, 255, 255 };
	GenTextures(1, &impl->white_texture);
	BindTexture(TEXTURE_2D, impl->white_texture);
	TexParameteri(TEXTURE_2D, TEXTURE_MIN_FILTER, NEAREST);
	TexParameteri(TEXTURE_2D, TEXTURE_MAG_FILTER, NEAREST);
	TexImage2D(TEXTURE_2D, 0, RGBA8, 1, 1, 0, RGBA, UNSIGNED_BYTE, white);

	// Carte d'ombres de la lumière directionnelle.
	GLint max_texture = 2048;
	GetIntegerv(MAX_TEXTURE_SIZE, &max_texture);
	impl->shadow_size = std::min(std::max(p_shadow_size, 256), (int)max_texture);
	GenTextures(1, &impl->shadow_texture);
	BindTexture(TEXTURE_2D, impl->shadow_texture);
	TexParameteri(TEXTURE_2D, TEXTURE_MIN_FILTER, LINEAR);
	TexParameteri(TEXTURE_2D, TEXTURE_MAG_FILTER, LINEAR);
	TexParameteri(TEXTURE_2D, TEXTURE_WRAP_S, CLAMP_TO_EDGE);
	TexParameteri(TEXTURE_2D, TEXTURE_WRAP_T, CLAMP_TO_EDGE);
	TexParameteri(TEXTURE_2D, TEXTURE_COMPARE_MODE, COMPARE_R_TO_TEXTURE);
	TexParameteri(TEXTURE_2D, TEXTURE_COMPARE_FUNC, LEQUAL);
	TexParameteri(TEXTURE_2D, DEPTH_TEXTURE_MODE, INTENSITY);
	TexImage2D(TEXTURE_2D, 0, DEPTH_COMPONENT24, impl->shadow_size, impl->shadow_size, 0, DEPTH_COMPONENT, UNSIGNED_INT, nullptr);
	GenFramebuffers(1, &impl->shadow_fbo);
	BindFramebuffer(FRAMEBUFFER, impl->shadow_fbo);
	FramebufferTexture2D(FRAMEBUFFER, DEPTH_ATTACHMENT, TEXTURE_2D, impl->shadow_texture, 0);
	DrawBuffer(NONE);
	ReadBuffer(NONE);
	if (CheckFramebufferStatus(FRAMEBUFFER) != FRAMEBUFFER_COMPLETE) {
		print_warning("Rendu 3D : carte d'ombres indisponible, ombres désactivées.");
		DeleteFramebuffers(1, &impl->shadow_fbo);
		impl->shadow_fbo = 0;
	}
	impl->available = true;
}

void RenderingServer3D::finish() {
	if (impl->available) {
		for (auto &kv : impl->viewports) {
			impl->destroy_viewport_buffers(kv.second);
		}
	}
	impl->viewports.clear();
	impl->available = false;
	impl->renderer = nullptr;
}

// ---------------------------------------------------------------------------
// Ressources
// ---------------------------------------------------------------------------
RID RenderingServer3D::texture_create(int p_width, int p_height, const uint8_t *p_rgba, bool p_linear, bool p_repeat) {
	if (!impl->available || p_width <= 0 || p_height <= 0 || !p_rgba) {
		return RID();
	}
	Impl::StateScope scope(impl->renderer);
	Impl::Texture tex;
	GenTextures(1, &tex.id);
	BindTexture(TEXTURE_2D, tex.id);
	PixelStorei(UNPACK_ALIGNMENT, 1);
	TexParameteri(TEXTURE_2D, TEXTURE_WRAP_S, p_repeat ? REPEAT : CLAMP_TO_EDGE);
	TexParameteri(TEXTURE_2D, TEXTURE_WRAP_T, p_repeat ? REPEAT : CLAMP_TO_EDGE);
	TexParameteri(TEXTURE_2D, TEXTURE_MAG_FILTER, p_linear ? LINEAR : NEAREST);
	TexParameteri(TEXTURE_2D, TEXTURE_MIN_FILTER, p_linear ? LINEAR_MIPMAP_LINEAR : NEAREST_MIPMAP_NEAREST);
	if (p_linear && impl->max_anisotropy > 1) {
		TexParameteri(TEXTURE_2D, (GLenum)0x84FE, impl->max_anisotropy); // GL_TEXTURE_MAX_ANISOTROPY_EXT
	}
	if (!GenerateMipmap) {
		TexParameteri(TEXTURE_2D, GENERATE_MIPMAP, 1);
	}
	TexImage2D(TEXTURE_2D, 0, RGBA8, p_width, p_height, 0, RGBA, UNSIGNED_BYTE, p_rgba);
	if (GenerateMipmap) {
		GenerateMipmap(TEXTURE_2D);
	}
	const RID rid = impl->make_rid();
	impl->textures[rid.id] = tex;
	return rid;
}

RID RenderingServer3D::mesh_create() {
	const RID rid = impl->make_rid();
	impl->meshes[rid.id] = Impl::Mesh();
	return rid;
}

void RenderingServer3D::mesh_set_data(RID p_mesh, const std::vector<Vertex> &p_vertices, const std::vector<uint32_t> &p_indices, Primitive p_primitive) {
	auto it = impl->meshes.find(p_mesh.id);
	if (it == impl->meshes.end()) {
		return;
	}
	Impl::Mesh &mesh = it->second;
	mesh.primitive = p_primitive;
	mesh.vertex_count = (int)p_vertices.size();
	mesh.index_count = (int)p_indices.size();
	mesh.aabb = AABB();
	if (!p_vertices.empty()) {
		mesh.aabb = AABB(p_vertices[0].position, Vector3());
		for (const Vertex &v : p_vertices) {
			mesh.aabb.expand_to(v.position);
		}
	}
	if (!impl->available) {
		return;
	}
	Impl::StateScope scope(impl->renderer);
	if (!mesh.vbo) {
		GenBuffers(1, &mesh.vbo);
		GenBuffers(1, &mesh.ibo);
	}
	BindBuffer(ARRAY_BUFFER, mesh.vbo);
	BufferData(ARRAY_BUFFER, (GLsizeiptr)(p_vertices.size() * sizeof(Vertex)), p_vertices.empty() ? nullptr : p_vertices.data(), STATIC_DRAW);
	BindBuffer(ELEMENT_ARRAY_BUFFER, mesh.ibo);
	BufferData(ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(p_indices.size() * sizeof(uint32_t)), p_indices.empty() ? nullptr : p_indices.data(), STATIC_DRAW);
}

AABB RenderingServer3D::mesh_get_aabb(RID p_mesh) const {
	auto it = impl->meshes.find(p_mesh.id);
	return it == impl->meshes.end() ? AABB() : it->second.aabb;
}

RID RenderingServer3D::material_create() {
	const RID rid = impl->make_rid();
	impl->materials[rid.id] = MaterialData();
	return rid;
}

void RenderingServer3D::material_set_data(RID p_material, const MaterialData &p_data) {
	auto it = impl->materials.find(p_material.id);
	if (it != impl->materials.end()) {
		it->second = p_data;
	}
}

RID RenderingServer3D::scenario_create() {
	const RID rid = impl->make_rid();
	impl->scenarios[rid.id] = Impl::Scenario();
	return rid;
}

void RenderingServer3D::scenario_set_environment(RID p_scenario, const EnvironmentData &p_environment) {
	auto it = impl->scenarios.find(p_scenario.id);
	if (it != impl->scenarios.end()) {
		it->second.environment = p_environment;
		it->second.has_environment = true;
	}
}

void RenderingServer3D::scenario_clear_environment(RID p_scenario) {
	auto it = impl->scenarios.find(p_scenario.id);
	if (it != impl->scenarios.end()) {
		it->second.has_environment = false;
	}
}

RID RenderingServer3D::instance_create(RID p_scenario) {
	const RID rid = impl->make_rid();
	Impl::Instance instance;
	instance.scenario = p_scenario.id;
	impl->instances[rid.id] = instance;
	return rid;
}

void RenderingServer3D::instance_set_mesh(RID p_instance, RID p_mesh) {
	auto it = impl->instances.find(p_instance.id);
	if (it != impl->instances.end()) {
		it->second.mesh = p_mesh;
	}
}

void RenderingServer3D::instance_set_material(RID p_instance, RID p_material) {
	auto it = impl->instances.find(p_instance.id);
	if (it != impl->instances.end()) {
		it->second.material = p_material;
	}
}

void RenderingServer3D::instance_set_transform(RID p_instance, const Transform3D &p_transform) {
	auto it = impl->instances.find(p_instance.id);
	if (it != impl->instances.end()) {
		it->second.transform = p_transform;
	}
}

void RenderingServer3D::instance_set_visible(RID p_instance, bool p_visible) {
	auto it = impl->instances.find(p_instance.id);
	if (it != impl->instances.end()) {
		it->second.visible = p_visible;
	}
}

void RenderingServer3D::instance_set_cast_shadows(RID p_instance, bool p_enabled) {
	auto it = impl->instances.find(p_instance.id);
	if (it != impl->instances.end()) {
		it->second.cast_shadows = p_enabled;
	}
}

RID RenderingServer3D::light_create(RID p_scenario) {
	const RID rid = impl->make_rid();
	Impl::Light light;
	light.scenario = p_scenario.id;
	impl->lights[rid.id] = light;
	return rid;
}

void RenderingServer3D::light_set_data(RID p_light, const LightData &p_data) {
	auto it = impl->lights.find(p_light.id);
	if (it != impl->lights.end()) {
		it->second.data = p_data;
	}
}

void RenderingServer3D::light_set_transform(RID p_light, const Transform3D &p_transform) {
	auto it = impl->lights.find(p_light.id);
	if (it != impl->lights.end()) {
		it->second.transform = p_transform;
	}
}

void RenderingServer3D::light_set_visible(RID p_light, bool p_visible) {
	auto it = impl->lights.find(p_light.id);
	if (it != impl->lights.end()) {
		it->second.visible = p_visible;
	}
}

RID RenderingServer3D::viewport_create(int p_width, int p_height) {
	const RID rid = impl->make_rid();
	Impl::Viewport vp;
	if (RenderingServer::get_singleton() && impl->renderer) {
		impl->create_viewport_buffers(vp, p_width, p_height);
	}
	impl->viewports[rid.id] = vp;
	return rid;
}

void RenderingServer3D::viewport_set_size(RID p_viewport, int p_width, int p_height) {
	auto it = impl->viewports.find(p_viewport.id);
	if (it == impl->viewports.end() || !impl->renderer) {
		return;
	}
	p_width = std::max(p_width, 1);
	p_height = std::max(p_height, 1);
	if (it->second.width == p_width && it->second.height == p_height) {
		return;
	}
	if (impl->available) {
		Impl::StateScope scope(impl->renderer);
		impl->destroy_viewport_buffers(it->second);
	} else {
		impl->destroy_viewport_buffers(it->second);
	}
	impl->create_viewport_buffers(it->second, p_width, p_height);
}

Size2 RenderingServer3D::viewport_get_size(RID p_viewport) const {
	auto it = impl->viewports.find(p_viewport.id);
	return it == impl->viewports.end() ? Size2() : Size2((real_t)it->second.width, (real_t)it->second.height);
}

RID RenderingServer3D::viewport_get_texture(RID p_viewport) const {
	auto it = impl->viewports.find(p_viewport.id);
	return it == impl->viewports.end() ? RID() : it->second.texture;
}

void RenderingServer3D::free_rid(RID p_rid) {
	if (!p_rid.is_valid()) {
		return;
	}
	const uint64_t id = p_rid.id;
	if (impl->instances.erase(id) || impl->lights.erase(id) || impl->materials.erase(id)) {
		return;
	}
	auto mesh = impl->meshes.find(id);
	if (mesh != impl->meshes.end()) {
		if (impl->available && mesh->second.vbo) {
			Impl::StateScope scope(impl->renderer);
			DeleteBuffers(1, &mesh->second.vbo);
			DeleteBuffers(1, &mesh->second.ibo);
		}
		impl->meshes.erase(mesh);
		return;
	}
	auto tex = impl->textures.find(id);
	if (tex != impl->textures.end()) {
		if (impl->available) {
			Impl::StateScope scope(impl->renderer);
			DeleteTextures(1, &tex->second.id);
		}
		impl->textures.erase(tex);
		return;
	}
	auto vp = impl->viewports.find(id);
	if (vp != impl->viewports.end()) {
		if (impl->available) {
			Impl::StateScope scope(impl->renderer);
			impl->destroy_viewport_buffers(vp->second);
		} else if (impl->renderer) {
			impl->destroy_viewport_buffers(vp->second);
		}
		impl->viewports.erase(vp);
		return;
	}
	if (impl->scenarios.erase(id)) {
		for (auto it = impl->instances.begin(); it != impl->instances.end();) {
			it = it->second.scenario == id ? impl->instances.erase(it) : std::next(it);
		}
		for (auto it = impl->lights.begin(); it != impl->lights.end();) {
			it = it->second.scenario == id ? impl->lights.erase(it) : std::next(it);
		}
	}
}

// ---------------------------------------------------------------------------
// Rendu
// ---------------------------------------------------------------------------
void RenderingServer3D::render(RID p_viewport, RID p_scenario, const CameraData &p_camera, const RenderExtras &p_extras) {
	impl->stat_draw_calls = impl->stat_triangles = 0;
	auto vp_it = impl->viewports.find(p_viewport.id);
	if (!impl->available || vp_it == impl->viewports.end() || vp_it->second.texture_id == 0) {
		return;
	}
	Impl::Viewport &vp = vp_it->second;
	auto sc_it = impl->scenarios.find(p_scenario.id);
	const Impl::Scenario *scenario = sc_it == impl->scenarios.end() ? nullptr : &sc_it->second;
	EnvironmentData env;
	if (scenario && scenario->has_environment && p_extras.use_environment) {
		env = scenario->environment;
	} else {
		env.background = BG_COLOR;
		env.background_color = p_extras.fallback_background;
		env.ambient_energy = 0.45f;
	}

	Impl::StateScope scope(impl->renderer);

	const float aspect = (float)vp.width / (float)vp.height;
	const Transform3D camera_xform = p_camera.transform.orthonormalized();
	const Vector3 camera_position = camera_xform.origin;
	const Vector3 camera_forward = -camera_xform.basis.get_column(2);
	// La ligne 0 d'une texture SDL est le haut de l'image : on inverse l'axe Y de la projection.
	Projection flip;
	flip.m[5] = -1;
	const Projection view(camera_xform.affine_inverse());
	const Projection view_projection = flip * p_camera.get_projection(aspect) * view;

	// --- Instances visibles ---
	struct DrawItem {
		const Impl::Instance *instance;
		const Impl::Mesh *mesh;
		const MaterialData *material;
		float distance;
	};
	std::vector<DrawItem> opaque, transparent, line_items, casters;
	for (auto &kv : impl->instances) {
		const Impl::Instance &instance = kv.second;
		if (instance.scenario != p_scenario.id || !instance.visible) {
			continue;
		}
		auto mesh_it = impl->meshes.find(instance.mesh.id);
		if (mesh_it == impl->meshes.end() || mesh_it->second.vertex_count == 0 || !mesh_it->second.vbo) {
			continue;
		}
		const Impl::Mesh &mesh = mesh_it->second;
		auto mat_it = impl->materials.find(instance.material.id);
		const MaterialData *material = mat_it == impl->materials.end() ? &impl->default_material : &mat_it->second;
		DrawItem item = { &instance, &mesh, material, 0.0f };
		if (mesh.primitive == PRIMITIVE_TRIANGLES && instance.cast_shadows && !material->transparent) {
			casters.push_back(item);
		}
		// Élimination des objets hors du champ de vision (boîte englobante en espace de découpe).
		const Projection mvp = view_projection * Projection(instance.transform);
		int outside[6] = { 0, 0, 0, 0, 0, 0 };
		for (int i = 0; i < 8; i++) {
			float w = 1;
			const Vector3 c = mvp.xform4(mesh.aabb.get_corner(i), &w);
			outside[0] += c.x < -w;
			outside[1] += c.x > w;
			outside[2] += c.y < -w;
			outside[3] += c.y > w;
			outside[4] += c.z < -w;
			outside[5] += c.z > w;
		}
		bool culled = false;
		for (int i = 0; i < 6; i++) {
			culled = culled || outside[i] == 8;
		}
		if (culled) {
			continue;
		}
		item.distance = (instance.transform.xform(mesh.aabb.get_center()) - camera_position).length_squared();
		if (mesh.primitive == PRIMITIVE_LINES) {
			line_items.push_back(item);
		} else if (material->transparent) {
			transparent.push_back(item);
		} else {
			opaque.push_back(item);
		}
	}
	std::sort(opaque.begin(), opaque.end(), [](const DrawItem &a, const DrawItem &b) { return a.distance < b.distance; });
	std::sort(transparent.begin(), transparent.end(), [](const DrawItem &a, const DrawItem &b) { return a.distance > b.distance; });

	// --- Lumières : directionnelles d'abord, puis les plus proches de la caméra ---
	struct ActiveLight {
		const Impl::Light *light;
		float priority;
	};
	std::vector<ActiveLight> active_lights;
	for (auto &kv : impl->lights) {
		const Impl::Light &light = kv.second;
		if (light.scenario != p_scenario.id || !light.visible || light.data.energy <= 0) {
			continue;
		}
		const float priority = light.data.type == LIGHT_DIRECTIONAL ? -1.0f : std::max((light.transform.origin - camera_position).length() - light.data.range, 0.0f);
		active_lights.push_back({ &light, priority });
	}
	std::stable_sort(active_lights.begin(), active_lights.end(), [](const ActiveLight &a, const ActiveLight &b) { return a.priority < b.priority; });
	if ((int)active_lights.size() > MAX_LIGHTS) {
		active_lights.resize(MAX_LIGHTS);
	}

	// --- Carte d'ombres ---
	const Impl::Light *shadow_light = nullptr;
	for (const ActiveLight &al : active_lights) {
		if (al.light->data.type == LIGHT_DIRECTIONAL && al.light->data.shadow) {
			shadow_light = al.light;
			break;
		}
	}
	Projection shadow_matrix;
	float shadow_world_texel = 0;
	if (shadow_light && impl->shadow_fbo && !casters.empty()) {
		const float d = std::min(impl->shadow_distance, p_camera.z_far);
		Vector3 center;
		float radius;
		if (p_camera.orthogonal) {
			const float h = p_camera.size * 0.5f * std::sqrt(1 + aspect * aspect);
			center = camera_position + camera_forward * (d * 0.5f);
			radius = std::sqrt(h * h + d * d * 0.25f);
		} else {
			const float h = d * std::tan(Math::clamp(p_camera.fov, 1.0f, 179.0f) * 0.5f * (float)Math_PI / 180.0f) * std::sqrt(1 + aspect * aspect);
			const float c = std::min(d, (d + h * h / d) * 0.5f);
			center = camera_position + camera_forward * c;
			radius = c < d ? c : h;
		}
		const Basis light_basis = shadow_light->transform.basis.orthonormalized();
		// Centre aligné sur la grille de texels : les ombres ne scintillent pas quand la caméra bouge.
		const float texel = 2.0f * radius / (float)impl->shadow_size;
		shadow_world_texel = texel;
		Vector3 local = light_basis.xform_inv(center);
		local.x = std::floor(local.x / texel) * texel;
		local.y = std::floor(local.y / texel) * texel;
		const Transform3D light_xform(light_basis, light_basis.xform(local));
		const float depth_range = radius + 200.0f;
		const Projection light_vp = Projection::orthographic(-radius, radius, -radius, radius, -depth_range, depth_range) * Projection(light_xform.affine_inverse());
		Projection bias;
		bias.m[0] = bias.m[5] = bias.m[10] = 0.5f;
		bias.m[12] = bias.m[13] = bias.m[14] = 0.5f;
		shadow_matrix = bias * light_vp;

		BindFramebuffer(FRAMEBUFFER, impl->shadow_fbo);
		Viewport(0, 0, impl->shadow_size, impl->shadow_size);
		Disable(SCISSOR_TEST);
		Disable(BLEND);
		Disable(ALPHA_TEST);
		Disable(CULL_FACE);
		Enable(DEPTH_TEST);
		DepthFunc(LEQUAL);
		DepthMask(1);
		ColorMask(0, 0, 0, 0);
		ClearDepth(1.0);
		Clear(DEPTH_BUFFER_BIT);
		Enable(POLYGON_OFFSET_FILL);
		PolygonOffset(2.0f, 4.0f);
		UseProgram(impl->depth_program.id);
		for (const DrawItem &item : casters) {
			const Projection mvp = light_vp * Projection(item.instance->transform);
			UniformMatrix4fv(impl->depth_program.loc("u_mvp"), 1, 0, mvp.m);
			impl->bind_mesh(*item.mesh, false);
			DisableVertexAttribArray(3);
			impl->draw_mesh(*item.mesh);
		}
		Disable(POLYGON_OFFSET_FILL);
		ColorMask(1, 1, 1, 1);
	} else {
		shadow_light = nullptr;
	}

	// --- Passe principale ---
	BindFramebuffer(FRAMEBUFFER, vp.ms_fbo ? vp.ms_fbo : vp.fbo);
	Viewport(0, 0, vp.width, vp.height);
	Disable(SCISSOR_TEST);
	Disable(STENCIL_TEST);
	Disable(ALPHA_TEST);
	Disable(BLEND);
	Disable(LIGHTING);
	if (vp.ms_fbo) {
		Enable(MULTISAMPLE);
	}
	Enable(DEPTH_TEST);
	DepthFunc(LEQUAL);
	DepthMask(1);
	ColorMask(1, 1, 1, 1);
	FrontFace(CW); // projection inversée en Y : le sens des faces l'est aussi
	const Color bg = env.background == BG_SKY ? env.sky_horizon_color : env.background_color;
	ClearColor(bg.r, bg.g, bg.b, 1.0f);
	ClearDepth(1.0);
	Clear(COLOR_BUFFER_BIT | DEPTH_BUFFER_BIT);

	Impl::Program &sp = impl->scene_program;
	UseProgram(sp.id);
	Uniform1i(sp.loc("u_texture"), 0);
	Uniform1i(sp.loc("u_shadow_map"), 1);
	ActiveTexture(TEXTURE0 + 1);
	BindTexture(TEXTURE_2D, impl->shadow_texture);
	ActiveTexture(TEXTURE0);
	Uniform3f(sp.loc("u_camera_position"), camera_position.x, camera_position.y, camera_position.z);
	const Vector3 ambient = _linear(env.ambient_color, env.ambient_energy);
	Vector3 ambient_ground = ambient * 0.45f;
	if (env.background == BG_SKY) {
		const Vector3 g = _linear(env.ground_color);
		ambient_ground = Vector3(ambient.x * (0.3f + g.x), ambient.y * (0.3f + g.y), ambient.z * (0.3f + g.z));
	}
	Uniform3f(sp.loc("u_ambient_sky"), ambient.x, ambient.y, ambient.z);
	Uniform3f(sp.loc("u_ambient_ground"), ambient_ground.x, ambient_ground.y, ambient_ground.z);
	const Vector3 fog_color = _linear(env.fog_color);
	Uniform4f(sp.loc("u_fog"), fog_color.x, fog_color.y, fog_color.z, env.fog_enabled ? std::max(env.fog_density, 0.0f) : 0.0f);
	UniformMatrix4fv(sp.loc("u_shadow_matrix"), 1, 0, shadow_matrix.m);
	Uniform1f(sp.loc("u_shadow_texel"), 1.0f / (float)impl->shadow_size);
	Uniform1f(sp.loc("u_shadow_distance"), std::min(impl->shadow_distance, p_camera.z_far));
	Uniform1f(sp.loc("u_shadow_normal_bias"), shadow_world_texel * 1.5f);

	float light_position[MAX_LIGHTS * 4] = {}, light_color[MAX_LIGHTS * 4] = {}, light_direction[MAX_LIGHTS * 4] = {}, light_params[MAX_LIGHTS * 4] = {};
	Vector3 sun_direction(0, 1, 0);
	Vector3 sun_color;
	bool has_sun = false;
	for (size_t i = 0; i < active_lights.size(); i++) {
		const Impl::Light &light = *active_lights[i].light;
		const Vector3 axis = -light.transform.basis.get_column(2).normalized(); // la lumière éclaire vers -Z
		const Vector3 color = _linear(light.data.color, light.data.energy);
		Vector3 position = light.transform.origin;
		if (light.data.type == LIGHT_DIRECTIONAL) {
			position = -axis;
			if (!has_sun) {
				has_sun = true;
				sun_direction = -axis;
				sun_color = Vector3(light.data.color.r, light.data.color.g, light.data.color.b) * std::min(light.data.energy, 1.5f);
			}
		}
		light_position[i * 4 + 0] = position.x;
		light_position[i * 4 + 1] = position.y;
		light_position[i * 4 + 2] = position.z;
		light_position[i * 4 + 3] = (float)light.data.type;
		light_color[i * 4 + 0] = color.x;
		light_color[i * 4 + 1] = color.y;
		light_color[i * 4 + 2] = color.z;
		light_color[i * 4 + 3] = std::max(light.data.range, 0.001f);
		light_direction[i * 4 + 0] = axis.x;
		light_direction[i * 4 + 1] = axis.y;
		light_direction[i * 4 + 2] = axis.z;
		light_direction[i * 4 + 3] = std::cos(Math::clamp(light.data.spot_angle, 1.0f, 89.0f) * (float)Math_PI / 180.0f);
		light_params[i * 4 + 0] = std::max(light.data.attenuation, 0.01f);
		light_params[i * 4 + 1] = (&light == shadow_light) ? 1.0f : 0.0f;
	}
	Uniform1i(sp.loc("u_light_count"), (GLint)active_lights.size());
	Uniform4fv(sp.loc("u_light_position[0]"), MAX_LIGHTS, light_position);
	Uniform4fv(sp.loc("u_light_color[0]"), MAX_LIGHTS, light_color);
	Uniform4fv(sp.loc("u_light_direction[0]"), MAX_LIGHTS, light_direction);
	Uniform4fv(sp.loc("u_light_params[0]"), MAX_LIGHTS, light_params);

	auto draw_item = [&](const DrawItem &item) {
		const MaterialData &m = *item.material;
		const Projection model(item.instance->transform);
		const Projection mvp = view_projection * model;
		UniformMatrix4fv(sp.loc("u_mvp"), 1, 0, mvp.m);
		UniformMatrix4fv(sp.loc("u_model"), 1, 0, model.m);
		const Basis normal_basis = item.instance->transform.basis.inverse().transposed();
		float nm[9];
		for (int c = 0; c < 3; c++) {
			for (int r = 0; r < 3; r++) {
				nm[c * 3 + r] = normal_basis.rows[r][c];
			}
		}
		UniformMatrix3fv(sp.loc("u_normal_matrix"), 1, 0, nm);
		Uniform4f(sp.loc("u_albedo"), m.albedo.r, m.albedo.g, m.albedo.b, m.albedo.a);
		Uniform1f(sp.loc("u_metallic"), Math::clamp(m.metallic, 0.0f, 1.0f));
		Uniform1f(sp.loc("u_roughness"), Math::clamp(m.roughness, 0.0f, 1.0f));
		const Vector3 emission = _linear(m.emission, m.emission_energy);
		Uniform3f(sp.loc("u_emission"), emission.x, emission.y, emission.z);
		Uniform1f(sp.loc("u_unshaded"), m.unshaded ? 1.0f : 0.0f);
		Uniform2f(sp.loc("u_uv_scale"), m.uv_scale.x, m.uv_scale.y);
		auto tex_it = impl->textures.find(m.albedo_texture.id);
		if (tex_it != impl->textures.end()) {
			BindTexture(TEXTURE_2D, tex_it->second.id);
			Uniform1f(sp.loc("u_use_texture"), 1.0f);
		} else {
			BindTexture(TEXTURE_2D, impl->white_texture);
			Uniform1f(sp.loc("u_use_texture"), 0.0f);
		}
		// Une échelle négative inverse le sens des faces.
		const bool mirrored = item.instance->transform.basis.determinant() < 0;
		if (m.cull == CULL_DISABLED) {
			Disable(CULL_FACE);
		} else {
			Enable(CULL_FACE);
			CullFace((m.cull == CULL_BACK) != mirrored ? BACK : FRONT);
		}
		impl->bind_mesh(*item.mesh, true);
		impl->draw_mesh(*item.mesh);
	};

	for (const DrawItem &item : opaque) {
		draw_item(item);
	}

	// --- Ciel ---
	if (env.background == BG_SKY) {
		const Projection inverse_vp = view_projection.inverse();
		float sky_vertices[4 * 6];
		const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };
		for (int i = 0; i < 4; i++) {
			const Vector3 near_point = inverse_vp.xform(Vector3(corners[i][0], corners[i][1], -1));
			const Vector3 far_point = inverse_vp.xform(Vector3(corners[i][0], corners[i][1], 1));
			const Vector3 dir = p_camera.orthogonal ? camera_forward : (far_point - near_point).normalized();
			sky_vertices[i * 6 + 0] = corners[i][0];
			sky_vertices[i * 6 + 1] = corners[i][1];
			sky_vertices[i * 6 + 2] = 0;
			sky_vertices[i * 6 + 3] = dir.x;
			sky_vertices[i * 6 + 4] = dir.y;
			sky_vertices[i * 6 + 5] = dir.z;
		}
		Impl::Program &kp = impl->sky_program;
		UseProgram(kp.id);
		Uniform3f(kp.loc("u_top"), env.sky_top_color.r, env.sky_top_color.g, env.sky_top_color.b);
		Uniform3f(kp.loc("u_horizon"), env.sky_horizon_color.r, env.sky_horizon_color.g, env.sky_horizon_color.b);
		Uniform3f(kp.loc("u_ground"), env.ground_color.r, env.ground_color.g, env.ground_color.b);
		Uniform3f(kp.loc("u_sun_direction"), sun_direction.x, sun_direction.y, sun_direction.z);
		Uniform3f(kp.loc("u_sun_color"), sun_color.x, sun_color.y, sun_color.z);
		Uniform4f(kp.loc("u_fog"), env.fog_color.r, env.fog_color.g, env.fog_color.b, env.fog_enabled ? Math::clamp(env.fog_density * 40.0f, 0.0f, 1.0f) : 0.0f);
		BindBuffer(ARRAY_BUFFER, 0);
		BindBuffer(ELEMENT_ARRAY_BUFFER, 0);
		Disable(CULL_FACE);
		DepthMask(0);
		EnableVertexAttribArray(0);
		VertexAttribPointer(0, 3, FLOAT, 0, 6 * sizeof(float), sky_vertices);
		EnableVertexAttribArray(1);
		VertexAttribPointer(1, 3, FLOAT, 0, 6 * sizeof(float), sky_vertices + 3);
		DisableVertexAttribArray(2);
		DisableVertexAttribArray(3);
		const unsigned int sky_indices[6] = { 0, 1, 2, 2, 1, 3 };
		DrawElements(TRIANGLES, 6, UNSIGNED_INT, sky_indices);
		impl->stat_draw_calls++;
		DepthMask(1);
		UseProgram(sp.id);
	}

	// --- Objets transparents, du plus loin au plus proche ---
	if (!transparent.empty()) {
		Enable(BLEND);
		BlendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
		DepthMask(0);
		ColorMask(1, 1, 1, 0);
		for (const DrawItem &item : transparent) {
			draw_item(item);
		}
		DepthMask(1);
	}

	// --- Lignes (maillages filaires, repères de l'éditeur) ---
	Enable(BLEND);
	BlendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
	ColorMask(1, 1, 1, 0);
	Disable(CULL_FACE);
	LineWidth(1.0f);
	if (!line_items.empty()) {
		Impl::Program &cp = impl->color_program;
		UseProgram(cp.id);
		for (const DrawItem &item : line_items) {
			const Projection mvp = view_projection * Projection(item.instance->transform);
			UniformMatrix4fv(cp.loc("u_mvp"), 1, 0, mvp.m);
			Uniform4f(cp.loc("u_modulate"), item.material->albedo.r, item.material->albedo.g, item.material->albedo.b, item.material->albedo.a);
			impl->bind_mesh(*item.mesh, false);
			impl->draw_mesh(*item.mesh);
		}
	}
	if (p_extras.lines) {
		impl->draw_lines(*p_extras.lines, view_projection);
	}
	if (p_extras.overlay_lines && !p_extras.overlay_lines->empty()) {
		Disable(DEPTH_TEST);
		impl->draw_lines(*p_extras.overlay_lines, view_projection);
	}
	ColorMask(1, 1, 1, 1);
	FrontFace(CCW);

	// --- Résolution de l'anticrénelage vers la texture cible ---
	if (vp.ms_fbo) {
		BindFramebuffer(READ_FRAMEBUFFER, vp.ms_fbo);
		BindFramebuffer(DRAW_FRAMEBUFFER, vp.fbo);
		BlitFramebuffer(0, 0, vp.width, vp.height, 0, 0, vp.width, vp.height, COLOR_BUFFER_BIT, NEAREST);
	}
}
