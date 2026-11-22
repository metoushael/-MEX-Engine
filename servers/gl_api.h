// Accès minimal à OpenGL 2.1 (+ framebuffer objects), sans en-têtes système ni bibliothèque
// à lier : les fonctions sont résolues à l'exécution par SDL_GL_GetProcAddress, dans le
// contexte créé par le moteur de rendu « opengl » de SDL. Le même code tourne ainsi sous
// Windows (WGL), Linux (GLX/EGL) et macOS (profil 2.1 « legacy »).
#pragma once

#include <cstddef>

#if defined(_WIN32) && !defined(_WIN64)
#define MEX_GL_CALL __stdcall
#else
#define MEX_GL_CALL
#endif

namespace gl {

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef double GLdouble;
typedef char GLchar;
typedef unsigned int GLbitfield;
typedef unsigned char GLboolean;
typedef unsigned char GLubyte;
typedef ptrdiff_t GLsizeiptr;

enum : GLenum {
	// Primitives
	LINES = 0x0001,
	TRIANGLES = 0x0004,
	// Tampons à effacer
	DEPTH_BUFFER_BIT = 0x0100,
	COLOR_BUFFER_BIT = 0x4000,
	ALL_ATTRIB_BITS = 0xFFFFFFFF,
	CLIENT_ALL_ATTRIB_BITS = 0xFFFFFFFF,
	// Capacités
	CULL_FACE = 0x0B44,
	DEPTH_TEST = 0x0B71,
	STENCIL_TEST = 0x0B90,
	ALPHA_TEST = 0x0BC0,
	BLEND = 0x0BE2,
	SCISSOR_TEST = 0x0C11,
	LIGHTING = 0x0B50,
	TEXTURE_2D = 0x0DE1,
	POLYGON_OFFSET_FILL = 0x8037,
	MULTISAMPLE = 0x809D,
	LINE_SMOOTH = 0x0B20,
	// Faces et comparaisons
	FRONT = 0x0404,
	BACK = 0x0405,
	CW = 0x0900,
	CCW = 0x0901,
	LESS = 0x0201,
	LEQUAL = 0x0203,
	ALWAYS = 0x0207,
	NONE = 0,
	// Mélange
	ZERO = 0,
	ONE = 1,
	SRC_ALPHA = 0x0302,
	ONE_MINUS_SRC_ALPHA = 0x0303,
	DST_COLOR = 0x0306,
	FUNC_ADD = 0x8006,
	FUNC_REVERSE_SUBTRACT = 0x800B,
	// Types d'uniformes
	INT = 0x1404,
	FLOAT_VEC2 = 0x8B50,
	FLOAT_VEC3 = 0x8B51,
	FLOAT_VEC4 = 0x8B52,
	BOOL = 0x8B56,
	SAMPLER_2D = 0x8B5E,
	ACTIVE_UNIFORMS = 0x8B86,
	// Types
	UNSIGNED_BYTE = 0x1401,
	UNSIGNED_INT = 0x1405,
	FLOAT = 0x1406,
	// Chaînes et requêtes
	VENDOR = 0x1F00,
	RENDERER = 0x1F01,
	VERSION = 0x1F02,
	EXTENSIONS = 0x1F03,
	SHADING_LANGUAGE_VERSION = 0x8B8C,
	MAX_SAMPLES = 0x8D57,
	MAX_TEXTURE_SIZE = 0x0D33,
	TEXTURE_BINDING_2D = 0x8069,
	CURRENT_PROGRAM = 0x8B8D,
	ARRAY_BUFFER_BINDING = 0x8894,
	ELEMENT_ARRAY_BUFFER_BINDING = 0x8895,
	FRAMEBUFFER_BINDING = 0x8CA6,
	RENDERBUFFER_BINDING = 0x8CA7,
	// Textures
	RGBA = 0x1908,
	RGBA8 = 0x8058,
	DEPTH_COMPONENT = 0x1902,
	DEPTH_COMPONENT24 = 0x81A6,
	TEXTURE_MAG_FILTER = 0x2800,
	TEXTURE_MIN_FILTER = 0x2801,
	TEXTURE_WRAP_S = 0x2802,
	TEXTURE_WRAP_T = 0x2803,
	NEAREST = 0x2600,
	LINEAR = 0x2601,
	NEAREST_MIPMAP_NEAREST = 0x2700,
	LINEAR_MIPMAP_LINEAR = 0x2703,
	REPEAT = 0x2901,
	CLAMP_TO_EDGE = 0x812F,
	GENERATE_MIPMAP = 0x8191,
	TEXTURE_COMPARE_MODE = 0x884C,
	TEXTURE_COMPARE_FUNC = 0x884D,
	COMPARE_R_TO_TEXTURE = 0x884E,
	DEPTH_TEXTURE_MODE = 0x884B,
	INTENSITY = 0x8049,
	TEXTURE0 = 0x84C0,
	UNPACK_ALIGNMENT = 0x0CF5,
	// Tampons de sommets
	ARRAY_BUFFER = 0x8892,
	ELEMENT_ARRAY_BUFFER = 0x8893,
	STATIC_DRAW = 0x88E4,
	STREAM_DRAW = 0x88E0,
	// Shaders
	FRAGMENT_SHADER = 0x8B30,
	VERTEX_SHADER = 0x8B31,
	COMPILE_STATUS = 0x8B81,
	LINK_STATUS = 0x8B82,
	// Framebuffers
	FRAMEBUFFER = 0x8D40,
	READ_FRAMEBUFFER = 0x8CA8,
	DRAW_FRAMEBUFFER = 0x8CA9,
	RENDERBUFFER = 0x8D41,
	COLOR_ATTACHMENT0 = 0x8CE0,
	DEPTH_ATTACHMENT = 0x8D00,
	FRAMEBUFFER_COMPLETE = 0x8CD5,
};

// Liste des fonctions : X(retour, nom, paramètres). Les fonctions « F » existent aussi avec
// le suffixe EXT (extensions framebuffer), « O » sont facultatives.
#define MEX_GL_FUNCTIONS(X, F, O) \
	X(void, GetIntegerv, (GLenum, GLint *)) \
	X(const GLubyte *, GetString, (GLenum)) \
	X(GLenum, GetError, (void)) \
	X(void, Enable, (GLenum)) \
	X(void, Disable, (GLenum)) \
	X(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) \
	X(void, Scissor, (GLint, GLint, GLsizei, GLsizei)) \
	X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
	X(void, ClearDepth, (GLdouble)) \
	X(void, Clear, (GLbitfield)) \
	X(void, DepthFunc, (GLenum)) \
	X(void, DepthMask, (GLboolean)) \
	X(void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) \
	X(void, CullFace, (GLenum)) \
	X(void, FrontFace, (GLenum)) \
	X(void, BlendFunc, (GLenum, GLenum)) \
	X(void, PolygonOffset, (GLfloat, GLfloat)) \
	X(void, LineWidth, (GLfloat)) \
	X(void, DrawArrays, (GLenum, GLint, GLsizei)) \
	X(void, DrawElements, (GLenum, GLsizei, GLenum, const void *)) \
	X(void, DrawBuffer, (GLenum)) \
	X(void, ReadBuffer, (GLenum)) \
	X(void, PushAttrib, (GLbitfield)) \
	X(void, PopAttrib, (void)) \
	X(void, PushClientAttrib, (GLbitfield)) \
	X(void, PopClientAttrib, (void)) \
	X(void, PixelStorei, (GLenum, GLint)) \
	X(void, GenTextures, (GLsizei, GLuint *)) \
	X(void, DeleteTextures, (GLsizei, const GLuint *)) \
	X(void, BindTexture, (GLenum, GLuint)) \
	X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
	X(void, TexParameteri, (GLenum, GLenum, GLint)) \
	X(void, ActiveTexture, (GLenum)) \
	X(void, GenBuffers, (GLsizei, GLuint *)) \
	X(void, DeleteBuffers, (GLsizei, const GLuint *)) \
	X(void, BindBuffer, (GLenum, GLuint)) \
	X(void, BufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
	X(GLuint, CreateShader, (GLenum)) \
	X(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
	X(void, CompileShader, (GLuint)) \
	X(void, GetShaderiv, (GLuint, GLenum, GLint *)) \
	X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
	X(void, DeleteShader, (GLuint)) \
	X(GLuint, CreateProgram, (void)) \
	X(void, AttachShader, (GLuint, GLuint)) \
	X(void, BindAttribLocation, (GLuint, GLuint, const GLchar *)) \
	X(void, LinkProgram, (GLuint)) \
	X(void, GetProgramiv, (GLuint, GLenum, GLint *)) \
	X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
	X(void, UseProgram, (GLuint)) \
	X(void, DeleteProgram, (GLuint)) \
	X(GLint, GetUniformLocation, (GLuint, const GLchar *)) \
	X(void, Uniform1i, (GLint, GLint)) \
	X(void, Uniform1f, (GLint, GLfloat)) \
	X(void, Uniform2f, (GLint, GLfloat, GLfloat)) \
	X(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat)) \
	X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
	X(void, Uniform4fv, (GLint, GLsizei, const GLfloat *)) \
	X(void, UniformMatrix3fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
	X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
	X(void, EnableVertexAttribArray, (GLuint)) \
	X(void, DisableVertexAttribArray, (GLuint)) \
	X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
	X(void, VertexAttrib4f, (GLuint, GLfloat, GLfloat, GLfloat, GLfloat)) \
	F(void, GenFramebuffers, (GLsizei, GLuint *)) \
	F(void, DeleteFramebuffers, (GLsizei, const GLuint *)) \
	F(void, BindFramebuffer, (GLenum, GLuint)) \
	F(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
	F(void, FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint)) \
	F(GLenum, CheckFramebufferStatus, (GLenum)) \
	F(void, GenRenderbuffers, (GLsizei, GLuint *)) \
	F(void, DeleteRenderbuffers, (GLsizei, const GLuint *)) \
	F(void, BindRenderbuffer, (GLenum, GLuint)) \
	F(void, RenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)) \
	O(void, GenerateMipmap, (GLenum)) \
	O(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum)) \
	O(void, BlendEquation, (GLenum)) \
	O(void, GetActiveUniform, (GLuint, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLchar *)) \
	O(void, RenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei)) \
	O(void, BlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum))

#define MEX_GL_DECLARE(ret, name, args) \
	typedef ret(MEX_GL_CALL *Fn##name) args; \
	extern Fn##name name;
MEX_GL_FUNCTIONS(MEX_GL_DECLARE, MEX_GL_DECLARE, MEX_GL_DECLARE)
#undef MEX_GL_DECLARE

// Résout toutes les fonctions dans le contexte courant. Retourne le nom de la première
// fonction obligatoire manquante, ou nullptr si tout est disponible.
const char *load();

} // namespace gl
