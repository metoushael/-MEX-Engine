// Effets du rendu 2D : matériaux de canevas (modes de mélange, shaders GLSL) et éclairage
// (teinte ambiante, lumières ponctuelles, ombres portées par des occulteurs).
//
// Les shaders s'écrivent comme ceux de Godot (shader_type canvas_item) et sont traduits en
// GLSL 1.20. Ils sont dessinés directement avec OpenGL, entre deux séries de commandes de
// SDL_Renderer : il faut donc le moteur de rendu « opengl » de SDL. Avec un autre moteur
// (rendu logiciel), l'item est dessiné sans son shader.

#include "rendering_server.h"

#include "servers/gl_api.h"
#include "servers/rendering_server_3d.h"

#include <SDL.h>

#include <regex>

using namespace gl;

// ---------------------------------------------------------------------------
// Matériaux
// ---------------------------------------------------------------------------
RID RenderingServer::canvas_material_create() {
	RID rid;
	rid.id = next_id++;
	materials[rid.id] = CanvasMaterial();
	return rid;
}

void RenderingServer::canvas_material_set_blend_mode(RID p_material, BlendMode p_mode) {
	auto it = materials.find(p_material.id);
	if (it != materials.end()) {
		it->second.blend_mode = p_mode;
	}
}

void RenderingServer::canvas_material_set_shader_code(RID p_material, const String &p_code) {
	auto it = materials.find(p_material.id);
	if (it != materials.end() && it->second.code != p_code) {
		it->second.code = p_code;
		it->second.dirty = true;
	}
}

void RenderingServer::canvas_material_set_param(RID p_material, const String &p_name, const Variant &p_value) {
	auto it = materials.find(p_material.id);
	if (it != materials.end()) {
		it->second.params[p_name] = p_value;
	}
}

void RenderingServer::canvas_material_set_texture_param(RID p_material, const String &p_name, RID p_texture) {
	auto it = materials.find(p_material.id);
	if (it != materials.end()) {
		it->second.texture_params[p_name] = p_texture;
	}
}

void RenderingServer::canvas_material_clear_params(RID p_material) {
	auto it = materials.find(p_material.id);
	if (it != materials.end()) {
		it->second.params.clear();
		it->second.texture_params.clear();
	}
}

String RenderingServer::canvas_material_get_shader_error(RID p_material) {
	auto it = materials.find(p_material.id);
	if (it == materials.end() || it->second.code.empty()) {
		return String();
	}
	if (!_shaders_available()) {
		return "Les shaders demandent le moteur de rendu OpenGL.";
	}
	_material_prepare(it->second);
	return it->second.error;
}

void RenderingServer::canvas_item_set_material(RID p_item, RID p_material) {
	if (Item *item = _get_item(p_item)) {
		item->material = p_material;
	}
}

void RenderingServer::canvas_item_set_clip(RID p_item, bool p_enabled, const Rect2 &p_rect) {
	if (Item *item = _get_item(p_item)) {
		item->clip = p_enabled;
		item->clip_rect = p_rect;
	}
}

bool RenderingServer::_shaders_available() const {
	RenderingServer3D *rs3d = RenderingServer3D::get_singleton();
	return renderer && rs3d && rs3d->is_available(); // même condition : SDL « opengl », OpenGL 2.1
}

static SDL_BlendMode _sdl_blend_mode(RenderingServer::BlendMode p_mode) {
	switch (p_mode) {
		case RenderingServer::BLEND_MODE_ADD:
			return SDL_BLENDMODE_ADD;
		case RenderingServer::BLEND_MODE_SUB:
			return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
		case RenderingServer::BLEND_MODE_MUL:
			return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_DST_COLOR, SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
		default:
			return SDL_BLENDMODE_BLEND;
	}
}

void RenderingServer::_apply_blend_mode(SDL_Texture *p_texture, BlendMode p_mode) {
	const SDL_BlendMode mode = _sdl_blend_mode(p_mode);
	int res = p_texture ? SDL_SetTextureBlendMode(p_texture, mode) : SDL_SetRenderDrawBlendMode(renderer, mode);
	if (res != 0 && p_mode == BLEND_MODE_SUB) {
		// Mode non géré par ce moteur de rendu : l'addition reste le plus proche.
		res = p_texture ? SDL_SetTextureBlendMode(p_texture, SDL_BLENDMODE_ADD) : SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
	}
}

// --- Traduction du shader ---------------------------------------------------
// Retire du code la fonction « void <nom>() { ... } » et retourne son corps.
static String _extract_function(String &r_code, const char *p_name) {
	const std::regex head(String("void\\s+") + p_name + "\\s*\\(\\s*\\)\\s*\\{");
	std::smatch match;
	if (!std::regex_search(r_code, match, head)) {
		return String();
	}
	const size_t start = (size_t)match.position(0);
	size_t pos = start + (size_t)match.length(0);
	const size_t body_start = pos;
	int depth = 1;
	while (pos < r_code.size() && depth > 0) {
		if (r_code[pos] == '{') {
			depth++;
		} else if (r_code[pos] == '}') {
			depth--;
		}
		pos++;
	}
	const String body = r_code.substr(body_start, pos - body_start - (depth == 0 ? 1 : 0));
	r_code.erase(start, pos - start);
	return body;
}

static const char *SHADER_COMMON =
		"#version 120\n"
		"#define texture texture2D\n"
		"#define textureLod texture2DLod\n"
		"uniform sampler2D TEXTURE;\n"
		"uniform vec2 TEXTURE_PIXEL_SIZE;\n"
		"uniform vec2 SCREEN_PIXEL_SIZE;\n"
		"uniform float TIME;\n"
		"varying vec2 mex_uv;\n"
		"varying vec4 mex_color;\n"
		"vec2 UV;\n"
		"vec4 COLOR;\n";

static bool _compile_stage(GLuint &r_shader, GLenum p_type, const String &p_source, String &r_error) {
	r_shader = CreateShader(p_type);
	const char *source = p_source.c_str();
	ShaderSource(r_shader, 1, &source, nullptr);
	CompileShader(r_shader);
	GLint ok = 0;
	GetShaderiv(r_shader, COMPILE_STATUS, &ok);
	if (!ok) {
		char log[2048] = "";
		GLsizei len = 0;
		GetShaderInfoLog(r_shader, sizeof(log) - 1, &len, log);
		r_error = String(p_type == VERTEX_SHADER ? "vertex() : " : "fragment() : ") + log;
		DeleteShader(r_shader);
		r_shader = 0;
		return false;
	}
	return true;
}

bool RenderingServer::_material_prepare(CanvasMaterial &p_material) {
	if (!_shaders_available()) {
		if (!shader_warning_shown) {
			shader_warning_shown = true;
			print_warning("Shaders 2D ignorés : ils demandent le moteur de rendu OpenGL.");
		}
		return false;
	}
	if (!p_material.dirty) {
		return p_material.program != 0;
	}
	p_material.dirty = false;
	p_material.error = String();
	p_material.uniform_types.clear();
	if (p_material.program) {
		DeleteProgram(p_material.program);
		p_material.program = 0;
	}

	// Ce qui n'existe pas en GLSL : la ligne shader_type, render_mode, les indications des
	// uniformes (« : source_color », « : hint_range(0, 1) »).
	String code = p_material.code;
	code = std::regex_replace(code, std::regex("(shader_type|render_mode)[^;]*;"), "");
	code = std::regex_replace(code, std::regex("(uniform\\s+\\w+\\s+\\w+)\\s*:[^=;]*"), "$1 ");
	const String vertex_body = _extract_function(code, "vertex");
	const String fragment_body = _extract_function(code, "fragment");

	const String vertex_source = String(SHADER_COMMON) +
			"attribute vec2 mex_a_pos;\nattribute vec2 mex_a_uv;\nattribute vec4 mex_a_color;\n"
			"uniform mat4 mex_matrix;\n"
			"vec2 VERTEX;\n"
			"#line 1\n" +
			code + "\nvoid vertex() {" + vertex_body + "}\n"
				   "void main() {\n"
				   "	VERTEX = mex_a_pos; UV = mex_a_uv; COLOR = mex_a_color;\n"
				   "	vertex();\n"
				   "	mex_uv = UV; mex_color = COLOR;\n"
				   "	gl_Position = mex_matrix * vec4(VERTEX, 0.0, 1.0);\n"
				   "}\n";
	const String fragment_source = String(SHADER_COMMON) +
			"#define FRAGCOORD gl_FragCoord\n"
			"#define SCREEN_UV (gl_FragCoord.xy * SCREEN_PIXEL_SIZE)\n"
			"#line 1\n" +
			code + "\nvoid fragment() {" + fragment_body + "}\n"
				   "void main() {\n"
				   "	UV = mex_uv;\n"
				   "	COLOR = mex_color * texture2D(TEXTURE, UV);\n"
				   "	fragment();\n"
				   "	gl_FragColor = COLOR;\n"
				   "}\n";

	SDL_RenderFlush(renderer);
	GLuint vs = 0, fs = 0;
	if (!_compile_stage(vs, VERTEX_SHADER, vertex_source, p_material.error) || !_compile_stage(fs, FRAGMENT_SHADER, fragment_source, p_material.error)) {
		if (vs) {
			DeleteShader(vs);
		}
		print_error("Shader 2D : " + p_material.error);
		return false;
	}
	const GLuint program = CreateProgram();
	AttachShader(program, vs);
	AttachShader(program, fs);
	BindAttribLocation(program, 0, "mex_a_pos");
	BindAttribLocation(program, 1, "mex_a_uv");
	BindAttribLocation(program, 2, "mex_a_color");
	LinkProgram(program);
	DeleteShader(vs);
	DeleteShader(fs);
	GLint ok = 0;
	GetProgramiv(program, LINK_STATUS, &ok);
	if (!ok) {
		char log[2048] = "";
		GLsizei len = 0;
		GetProgramInfoLog(program, sizeof(log) - 1, &len, log);
		p_material.error = String("édition des liens : ") + log;
		print_error("Shader 2D : " + p_material.error);
		DeleteProgram(program);
		return false;
	}
	if (GetActiveUniform) {
		GLint count = 0;
		GetProgramiv(program, ACTIVE_UNIFORMS, &count);
		for (GLint i = 0; i < count; i++) {
			char name[128] = "";
			GLsizei len = 0;
			GLint size = 0;
			GLenum type = 0;
			GetActiveUniform(program, (GLuint)i, sizeof(name) - 1, &len, &size, &type, name);
			p_material.uniform_types[name] = (int)type;
		}
	}
	while (GetError() != 0) {
	}
	p_material.program = program;
	return true;
}

// Nom OpenGL de la texture d'une SDL_Texture (0 si elle n'est pas une texture 2D ordinaire).
static GLuint _gl_texture_name(SDL_Texture *p_texture) {
	float tw = 0, th = 0;
	if (!p_texture || SDL_GL_BindTexture(p_texture, &tw, &th) != 0) {
		return 0;
	}
	GLint name = 0;
	GetIntegerv(TEXTURE_BINDING_2D, &name);
	SDL_GL_UnbindTexture(p_texture);
	return (tw > 1.0f || th > 1.0f) ? 0 : (GLuint)name; // textures « rectangle » : non gérées
}

// Géométrie de la cible de rendu courante, telle que SDL la calcule.
struct TargetGeometry {
	int out_w = 0, out_h = 0; // taille de la cible en pixels
	SDL_Rect viewport = { 0, 0, 0, 0 }; // en pixels, origine en haut à gauche
	float scale_x = 1, scale_y = 1;
	bool to_texture = false;
};

static TargetGeometry _target_geometry(SDL_Renderer *p_renderer) {
	TargetGeometry g;
	g.to_texture = SDL_GetRenderTarget(p_renderer) != nullptr;
	SDL_GetRendererOutputSize(p_renderer, &g.out_w, &g.out_h);
	SDL_RenderGetScale(p_renderer, &g.scale_x, &g.scale_y);
	g.viewport = { 0, 0, g.out_w, g.out_h };
	int lw = 0, lh = 0;
	SDL_RenderGetLogicalSize(p_renderer, &lw, &lh);
	if (!g.to_texture && lw > 0 && lh > 0 && g.out_w > 0 && g.out_h > 0) {
		// Résolution logique : image centrée entre des bandes (même calcul que SDL).
		const float want = (float)lw / (float)lh;
		const float real = (float)g.out_w / (float)g.out_h;
		if (std::fabs(want - real) < 0.0001f) {
			// plein écran, sans bandes
		} else if (want > real) {
			const float scale = (float)g.out_w / (float)lw;
			g.viewport.h = (int)std::floor(lh * scale);
			g.viewport.y = (g.out_h - g.viewport.h) / 2;
		} else {
			const float scale = (float)g.out_h / (float)lh;
			g.viewport.w = (int)std::floor(lw * scale);
			g.viewport.x = (g.out_w - g.viewport.w) / 2;
		}
	}
	return g;
}

bool RenderingServer::_draw_command_shader(const DrawEntry &p_entry, const Command &p_command, CanvasMaterial &p_material) {
	SDL_Texture *sdl_texture = p_command.texture.is_valid() ? texture_get_native(p_command.texture) : nullptr;
	if (p_command.texture.is_valid() && !sdl_texture) {
		return true; // texture libérée : rien à dessiner
	}
	SDL_RenderFlush(renderer);

	// Textures : la principale (blanche pour les formes sans texture) puis les paramètres.
	static RID white_texture;
	if (!white_texture.is_valid() || !textures.count(white_texture.id)) {
		const uint8_t white[4] = { 255, 255, 255, 255 };
		white_texture = texture_create(1, 1, white, TEXTURE_FILTER_NEAREST);
	}
	const GLuint main_name = _gl_texture_name(sdl_texture ? sdl_texture : texture_get_native(white_texture));
	if (!main_name) {
		return false;
	}
	std::vector<std::pair<String, GLuint>> samplers;
	for (const auto &kv : p_material.texture_params) {
		const GLuint name = _gl_texture_name(texture_get_native(kv.second));
		if (name) {
			samplers.push_back({ kv.first, name });
		}
	}
	const TargetGeometry g = _target_geometry(renderer);
	if (g.viewport.w <= 0 || g.viewport.h <= 0) {
		return false;
	}

	GLint old_program = 0, old_array_buffer = 0, old_element_buffer = 0;
	GetIntegerv(CURRENT_PROGRAM, &old_program);
	GetIntegerv(ARRAY_BUFFER_BINDING, &old_array_buffer);
	GetIntegerv(ELEMENT_ARRAY_BUFFER_BINDING, &old_element_buffer);
	PushAttrib(ALL_ATTRIB_BITS);
	PushClientAttrib(CLIENT_ALL_ATTRIB_BITS);

	Viewport(g.viewport.x, g.to_texture ? g.viewport.y : g.out_h - g.viewport.y - g.viewport.h, g.viewport.w, g.viewport.h);
	Disable(DEPTH_TEST);
	Disable(CULL_FACE);
	Disable(STENCIL_TEST);
	Disable(ALPHA_TEST);
	Disable(LIGHTING);
	if (p_entry.clip) {
		const int cx = g.viewport.x + (int)std::floor(p_entry.clip_rect.position.x * g.scale_x);
		const int cy = g.viewport.y + (int)std::floor(p_entry.clip_rect.position.y * g.scale_y);
		const int cw = (int)std::ceil(p_entry.clip_rect.size.x * g.scale_x);
		const int ch = (int)std::ceil(p_entry.clip_rect.size.y * g.scale_y);
		Enable(SCISSOR_TEST);
		Scissor(cx, g.to_texture ? cy : g.out_h - cy - ch, cw, ch);
	} else {
		Disable(SCISSOR_TEST);
	}
	Enable(BLEND);
	if (BlendEquation) {
		BlendEquation(p_material.blend_mode == BLEND_MODE_SUB ? FUNC_REVERSE_SUBTRACT : FUNC_ADD);
	}
	GLenum src = SRC_ALPHA, dst = ONE_MINUS_SRC_ALPHA;
	if (p_material.blend_mode == BLEND_MODE_ADD || p_material.blend_mode == BLEND_MODE_SUB) {
		dst = ONE;
	} else if (p_material.blend_mode == BLEND_MODE_MUL) {
		src = DST_COLOR;
		dst = ZERO;
	}
	if (BlendFuncSeparate) {
		// L'alpha de la cible n'est jamais diminué : l'image du jeu reste opaque.
		BlendFuncSeparate(src, dst, p_material.blend_mode == BLEND_MODE_MIX ? ONE : ZERO, p_material.blend_mode == BLEND_MODE_MIX ? ONE_MINUS_SRC_ALPHA : ONE);
	} else {
		BlendFunc(src, dst);
	}

	UseProgram(p_material.program);
	// Du repère de l'item vers les coordonnées de découpe d'OpenGL.
	const Transform2D &t = p_entry.xform;
	const float kx = 2.0f * g.scale_x / (float)g.viewport.w;
	const float ky = 2.0f * g.scale_y / (float)g.viewport.h;
	const float fy = g.to_texture ? 1.0f : -1.0f;
	const float matrix[16] = {
		t.x.x * kx, fy * t.x.y * ky, 0, 0,
		t.y.x * kx, fy * t.y.y * ky, 0, 0,
		0, 0, 1, 0,
		t.origin.x * kx - 1.0f, fy * (t.origin.y * ky - 1.0f), 0, 1
	};
	UniformMatrix4fv(GetUniformLocation(p_material.program, "mex_matrix"), 1, 0, matrix);
	Uniform1f(GetUniformLocation(p_material.program, "TIME"), (float)(SDL_GetTicks() / 1000.0));
	Uniform2f(GetUniformLocation(p_material.program, "SCREEN_PIXEL_SIZE"), 1.0f / (float)g.viewport.w, 1.0f / (float)g.viewport.h);
	const Size2 tex_size = sdl_texture ? texture_get_size(p_command.texture) : Size2(1, 1);
	Uniform2f(GetUniformLocation(p_material.program, "TEXTURE_PIXEL_SIZE"), 1.0f / std::max(tex_size.x, (real_t)1), 1.0f / std::max(tex_size.y, (real_t)1));

	ActiveTexture(TEXTURE0);
	Enable(TEXTURE_2D);
	BindTexture(TEXTURE_2D, main_name);
	Uniform1i(GetUniformLocation(p_material.program, "TEXTURE"), 0);
	for (size_t i = 0; i < samplers.size() && i < 7; i++) {
		ActiveTexture((GLenum)(TEXTURE0 + 1 + i));
		BindTexture(TEXTURE_2D, samplers[i].second);
		Uniform1i(GetUniformLocation(p_material.program, samplers[i].first.c_str()), (GLint)(1 + i));
	}
	ActiveTexture(TEXTURE0);

	for (const auto &kv : p_material.params) {
		const GLint location = GetUniformLocation(p_material.program, kv.first.c_str());
		if (location < 0) {
			continue;
		}
		const Variant &value = kv.second;
		auto type_it = p_material.uniform_types.find(kv.first);
		const int type = type_it != p_material.uniform_types.end() ? type_it->second : 0;
		switch (value.get_type()) {
			case Variant::BOOL:
			case Variant::INT:
			case Variant::FLOAT: {
				if (type == (int)gl::INT || type == (int)gl::BOOL || (type == 0 && value.get_type() != Variant::FLOAT)) {
					Uniform1i(location, (GLint)value.as_int());
				} else {
					Uniform1f(location, (float)value.as_float());
				}
			} break;
			case Variant::VECTOR2: {
				const Vector2 v = value.as_vector2();
				Uniform2f(location, v.x, v.y);
			} break;
			case Variant::VECTOR3: {
				const Vector3 v = value.as_vector3();
				Uniform3f(location, v.x, v.y, v.z);
			} break;
			case Variant::COLOR: {
				const Color c = value.as_color();
				if (type == (int)gl::FLOAT_VEC3) {
					Uniform3f(location, c.r, c.g, c.b);
				} else {
					Uniform4f(location, c.r, c.g, c.b, c.a);
				}
			} break;
			case Variant::RECT2: {
				const Rect2 r = value.as_rect2();
				Uniform4f(location, r.position.x, r.position.y, r.size.x, r.size.y);
			} break;
			default:
				break;
		}
	}

	static std::vector<float> data;
	data.resize(p_command.verts.size() * 8);
	for (size_t i = 0; i < p_command.verts.size(); i++) {
		const Vertex &v = p_command.verts[i];
		float *out = &data[i * 8];
		out[0] = v.pos.x;
		out[1] = v.pos.y;
		out[2] = v.uv.x;
		out[3] = v.uv.y;
		out[4] = v.color.r * p_entry.modulate.r;
		out[5] = v.color.g * p_entry.modulate.g;
		out[6] = v.color.b * p_entry.modulate.b;
		out[7] = v.color.a * p_entry.modulate.a;
	}
	BindBuffer(ARRAY_BUFFER, 0);
	BindBuffer(ELEMENT_ARRAY_BUFFER, 0);
	const GLsizei stride = (GLsizei)(8 * sizeof(float));
	EnableVertexAttribArray(0);
	EnableVertexAttribArray(1);
	EnableVertexAttribArray(2);
	VertexAttribPointer(0, 2, FLOAT, 0, stride, data.data());
	VertexAttribPointer(1, 2, FLOAT, 0, stride, data.data() + 2);
	VertexAttribPointer(2, 4, FLOAT, 0, stride, data.data() + 4);
	DrawElements(TRIANGLES, (GLsizei)p_command.indices.size(), UNSIGNED_INT, p_command.indices.data());
	DisableVertexAttribArray(0);
	DisableVertexAttribArray(1);
	DisableVertexAttribArray(2);

	PopClientAttrib();
	PopAttrib();
	UseProgram((GLuint)old_program);
	BindBuffer(ARRAY_BUFFER, (GLuint)old_array_buffer);
	BindBuffer(ELEMENT_ARRAY_BUFFER, (GLuint)old_element_buffer);
	while (GetError() != 0) {
	}
	return true;
}

// ---------------------------------------------------------------------------
// Éclairage 2D
// ---------------------------------------------------------------------------
void RenderingServer::canvas_set_ambient(RID p_canvas, bool p_enabled, const Color &p_color) {
	if (Item *canvas = _get_item(p_canvas)) {
		canvas->lit = p_enabled;
		canvas->ambient = p_color;
	}
}

RID RenderingServer::canvas_light_create() {
	RID rid;
	rid.id = next_id++;
	lights[rid.id] = CanvasLight();
	return rid;
}

#define MEX_LIGHT_SETTER(m_name, m_type, m_field) \
	void RenderingServer::m_name(RID p_light, m_type p_value) { \
		auto it = lights.find(p_light.id); \
		if (it != lights.end()) { \
			it->second.m_field = p_value; \
		} \
	}
MEX_LIGHT_SETTER(canvas_light_set_canvas, RID, canvas)
MEX_LIGHT_SETTER(canvas_light_set_enabled, bool, enabled)
MEX_LIGHT_SETTER(canvas_light_set_position, const Vector2 &, position)
MEX_LIGHT_SETTER(canvas_light_set_texture, RID, texture)
MEX_LIGHT_SETTER(canvas_light_set_scale, real_t, scale)
MEX_LIGHT_SETTER(canvas_light_set_color, const Color &, color)
MEX_LIGHT_SETTER(canvas_light_set_energy, real_t, energy)
MEX_LIGHT_SETTER(canvas_light_set_shadow_enabled, bool, shadow_enabled)
#undef MEX_LIGHT_SETTER

RID RenderingServer::canvas_occluder_create() {
	RID rid;
	rid.id = next_id++;
	occluders[rid.id] = CanvasOccluder();
	return rid;
}

void RenderingServer::canvas_occluder_set_canvas(RID p_occluder, RID p_canvas) {
	auto it = occluders.find(p_occluder.id);
	if (it != occluders.end()) {
		it->second.canvas = p_canvas;
	}
}

void RenderingServer::canvas_occluder_set_enabled(RID p_occluder, bool p_enabled) {
	auto it = occluders.find(p_occluder.id);
	if (it != occluders.end()) {
		it->second.enabled = p_enabled;
	}
}

void RenderingServer::canvas_occluder_set_transform(RID p_occluder, const Transform2D &p_xform) {
	auto it = occluders.find(p_occluder.id);
	if (it != occluders.end()) {
		it->second.xform = p_xform;
	}
}

void RenderingServer::canvas_occluder_set_polygon(RID p_occluder, const std::vector<Vector2> &p_points, bool p_closed) {
	auto it = occluders.find(p_occluder.id);
	if (it != occluders.end()) {
		it->second.polygon = p_points;
		it->second.closed = p_closed;
	}
}

void RenderingServer::_free_fx() {
	if (lightmap) {
		SDL_DestroyTexture(lightmap);
	}
	if (light_scratch) {
		SDL_DestroyTexture(light_scratch);
	}
	lightmap = light_scratch = nullptr;
	lightmap_w = lightmap_h = 0;
	light_texture = RID();
	materials.clear();
	lights.clear();
	occluders.clear();
}

void RenderingServer::_draw_lighting(Item *p_canvas, const Transform2D &p_xform) {
	// Taille logique de la cible courante : la carte de lumière la recouvre exactement.
	SDL_Texture *previous_target = SDL_GetRenderTarget(renderer);
	float scale_x = 1, scale_y = 1;
	SDL_RenderGetScale(renderer, &scale_x, &scale_y);
	SDL_Rect viewport;
	SDL_RenderGetViewport(renderer, &viewport);
	const int w = viewport.w, h = viewport.h;
	if (w <= 0 || h <= 0) {
		return;
	}
	if (!lightmap || lightmap_w != w || lightmap_h != h) {
		if (lightmap) {
			SDL_DestroyTexture(lightmap);
		}
		if (light_scratch) {
			SDL_DestroyTexture(light_scratch);
		}
		lightmap = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
		light_scratch = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
		lightmap_w = w;
		lightmap_h = h;
		if (!lightmap || !light_scratch) {
			return;
		}
		SDL_SetTextureScaleMode(lightmap, SDL_ScaleModeLinear);
		SDL_SetTextureScaleMode(light_scratch, SDL_ScaleModeLinear);
	}
	if (!light_texture.is_valid()) {
		// Dégradé radial : blanc, de plus en plus transparent vers le bord.
		const int size = 256;
		std::vector<uint8_t> pixels((size_t)size * size * 4, 255);
		for (int y = 0; y < size; y++) {
			for (int x = 0; x < size; x++) {
				const float dx = (x + 0.5f - size * 0.5f) / (size * 0.5f);
				const float dy = (y + 0.5f - size * 0.5f) / (size * 0.5f);
				const float d = std::min(std::sqrt(dx * dx + dy * dy), 1.0f);
				const float falloff = (1.0f - d) * (1.0f - d) * (3.0f - 2.0f * (1.0f - d)); // adouci
				pixels[((size_t)y * size + x) * 4 + 3] = (uint8_t)(falloff * 255.0f + 0.5f);
			}
		}
		light_texture = texture_create(size, size, pixels.data(), TEXTURE_FILTER_LINEAR);
	}

	// Les occulteurs du canevas, en coordonnées d'écran.
	std::vector<std::vector<Vector2>> shapes;
	std::vector<bool> shapes_closed;
	for (const auto &kv : occluders) {
		const CanvasOccluder &occluder = kv.second;
		if (!occluder.enabled || occluder.canvas != p_canvas->self || occluder.polygon.size() < 2) {
			continue;
		}
		std::vector<Vector2> points;
		for (const Vector2 &point : occluder.polygon) {
			points.push_back(p_xform.xform(occluder.xform.xform(point)));
		}
		shapes.push_back(points);
		shapes_closed.push_back(occluder.closed && points.size() > 2);
	}

	SDL_SetRenderTarget(renderer, lightmap);
	const Color &ambient = p_canvas->ambient;
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(renderer, (Uint8)(Math::clamp(ambient.r, 0.0f, 1.0f) * 255), (Uint8)(Math::clamp(ambient.g, 0.0f, 1.0f) * 255), (Uint8)(Math::clamp(ambient.b, 0.0f, 1.0f) * 255), 255);
	SDL_RenderClear(renderer);

	const real_t zoom = p_xform.get_scale().x;
	const float reach = (float)(w + h) * 4.0f; // longueur des ombres : au-delà de l'écran
	for (const auto &kv : lights) {
		const CanvasLight &light = kv.second;
		if (!light.enabled || light.canvas != p_canvas->self || light.energy <= 0) {
			continue;
		}
		const RID texture_rid = light.texture.is_valid() && textures.count(light.texture.id) ? light.texture : light_texture;
		SDL_Texture *texture = texture_get_native(texture_rid);
		if (!texture) {
			continue;
		}
		const Size2 size = texture_get_size(texture_rid) * (light.scale * zoom);
		const Vector2 center = p_xform.xform(light.position);
		const SDL_FRect dst = { center.x - size.x * 0.5f, center.y - size.y * 0.5f, size.x, size.y };
		if (dst.x > w || dst.y > h || dst.x + dst.w < 0 || dst.y + dst.h < 0) {
			continue; // hors de l'écran
		}
		const bool shadows = light.shadow_enabled && !shapes.empty();
		if (shadows) {
			SDL_SetRenderTarget(renderer, light_scratch);
			SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
			SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
			SDL_RenderClear(renderer);
		}
		// Une énergie supérieure à 1 se dessine en plusieurs passes additives.
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_ADD);
		real_t energy = std::min(light.energy, (real_t)8);
		while (energy > 0) {
			const real_t pass = std::min(energy, (real_t)1);
			SDL_SetTextureColorMod(texture, (Uint8)(Math::clamp(light.color.r * pass, 0.0f, 1.0f) * 255), (Uint8)(Math::clamp(light.color.g * pass, 0.0f, 1.0f) * 255), (Uint8)(Math::clamp(light.color.b * pass, 0.0f, 1.0f) * 255));
			SDL_SetTextureAlphaMod(texture, (Uint8)(Math::clamp(light.color.a, 0.0f, 1.0f) * 255));
			SDL_RenderCopyF(renderer, texture, nullptr, &dst);
			energy -= 1;
		}
		SDL_SetTextureColorMod(texture, 255, 255, 255);
		SDL_SetTextureAlphaMod(texture, 255);
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
		if (shadows) {
			// Chaque arête projette un quadrilatère noir à l'opposé de la lumière.
			std::vector<SDL_Vertex> verts;
			std::vector<int> indices;
			const SDL_Color black = { 0, 0, 0, 255 };
			for (size_t s = 0; s < shapes.size(); s++) {
				const std::vector<Vector2> &points = shapes[s];
				const size_t edges = shapes_closed[s] ? points.size() : points.size() - 1;
				for (size_t i = 0; i < edges; i++) {
					const Vector2 a = points[i];
					const Vector2 b = points[(i + 1) % points.size()];
					const Vector2 a2 = a + (a - center).normalized() * reach;
					const Vector2 b2 = b + (b - center).normalized() * reach;
					const int base = (int)verts.size();
					for (const Vector2 &p : { a, b, b2, a2 }) {
						SDL_Vertex vertex;
						vertex.position = { p.x, p.y };
						vertex.tex_coord = { 0, 0 };
						vertex.color = black;
						verts.push_back(vertex);
					}
					for (int index : { 0, 1, 2, 0, 2, 3 }) {
						indices.push_back(base + index);
					}
				}
			}
			SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
			SDL_RenderGeometry(renderer, nullptr, verts.data(), (int)verts.size(), indices.data(), (int)indices.size());
			SDL_SetRenderTarget(renderer, lightmap);
			SDL_SetTextureBlendMode(light_scratch, SDL_BLENDMODE_ADD);
			SDL_RenderCopy(renderer, light_scratch, nullptr, nullptr);
		}
		stat_draw_calls++;
	}

	// Retour à la cible d'origine : la scène est multipliée par la carte de lumière.
	SDL_SetRenderTarget(renderer, previous_target);
	if (previous_target) {
		SDL_RenderSetScale(renderer, scale_x, scale_y);
	}
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetTextureBlendMode(lightmap, _sdl_blend_mode(BLEND_MODE_MUL));
	const SDL_Rect dst = { 0, 0, w, h };
	SDL_RenderCopy(renderer, lightmap, nullptr, &dst);
	stat_draw_calls++;
}
