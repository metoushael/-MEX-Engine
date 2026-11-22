#include "gltf_loader.h"

#include "core/io/file_access.h"
#include "core/string/ustring.h"
#include "core/math/quaternion.h"
#include "core/math/transform_3d.h"
#include "scene/resources/mesh.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>

namespace {
// --- Lecteur JSON minimal ---
struct JSONValue {
	enum Type { NIL, BOOL, NUMBER, STRING, ARRAY, OBJECT };
	Type type = NIL;
	double number = 0;
	std::string string;
	std::vector<JSONValue> array;
	std::map<std::string, JSONValue> object;

	const JSONValue &operator[](const char *p_key) const {
		static const JSONValue nil;
		const auto it = object.find(p_key);
		return it == object.end() ? nil : it->second;
	}
	const JSONValue &operator[](size_t p_index) const {
		static const JSONValue nil;
		return p_index < array.size() ? array[p_index] : nil;
	}
	const JSONValue &operator[](int p_index) const { return (*this)[(size_t)std::max(p_index, 0)]; }
	bool has(const char *p_key) const { return object.count(p_key) > 0; }
	int as_int(int p_default = 0) const { return type == NUMBER ? (int)number : p_default; }
	double as_double(double p_default = 0) const { return type == NUMBER ? number : p_default; }
};

struct JSONParser {
	const std::string &text;
	size_t pos = 0;
	bool failed = false;

	explicit JSONParser(const std::string &p_text) :
			text(p_text) {}

	void skip() {
		while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r')) {
			pos++;
		}
	}
	std::string parse_string() {
		std::string out;
		pos++; // guillemet ouvrant
		while (pos < text.size() && text[pos] != '"') {
			char c = text[pos++];
			if (c == '\\' && pos < text.size()) {
				c = text[pos++];
				switch (c) {
					case 'n': out += '\n'; break;
					case 't': out += '\t'; break;
					case 'r': out += '\r'; break;
					case 'b': out += '\b'; break;
					case 'f': out += '\f'; break;
					case 'u': {
						const unsigned code = (unsigned)strtoul(text.substr(pos, 4).c_str(), nullptr, 16);
						pos = std::min(pos + 4, text.size());
						if (code < 0x80) {
							out += (char)code;
						} else if (code < 0x800) {
							out += (char)(0xC0 | (code >> 6));
							out += (char)(0x80 | (code & 0x3F));
						} else {
							out += (char)(0xE0 | (code >> 12));
							out += (char)(0x80 | ((code >> 6) & 0x3F));
							out += (char)(0x80 | (code & 0x3F));
						}
					} break;
					default: out += c; break;
				}
			} else {
				out += c;
			}
		}
		if (pos >= text.size()) {
			failed = true;
		}
		pos++; // guillemet fermant
		return out;
	}
	JSONValue parse(int p_depth = 0) {
		JSONValue value;
		skip();
		if (pos >= text.size() || p_depth > 64) {
			failed = true;
			return value;
		}
		const char c = text[pos];
		if (c == '{') {
			value.type = JSONValue::OBJECT;
			pos++;
			skip();
			while (!failed && pos < text.size() && text[pos] != '}') {
				skip();
				if (text[pos] != '"') {
					failed = true;
					break;
				}
				const std::string key = parse_string();
				skip();
				if (pos >= text.size() || text[pos] != ':') {
					failed = true;
					break;
				}
				pos++;
				value.object[key] = parse(p_depth + 1);
				skip();
				if (pos < text.size() && text[pos] == ',') {
					pos++;
					skip();
				}
			}
			pos++;
		} else if (c == '[') {
			value.type = JSONValue::ARRAY;
			pos++;
			skip();
			while (!failed && pos < text.size() && text[pos] != ']') {
				value.array.push_back(parse(p_depth + 1));
				skip();
				if (pos < text.size() && text[pos] == ',') {
					pos++;
					skip();
				}
			}
			pos++;
		} else if (c == '"') {
			value.type = JSONValue::STRING;
			value.string = parse_string();
		} else if (text.compare(pos, 4, "true") == 0) {
			value.type = JSONValue::BOOL;
			value.number = 1;
			pos += 4;
		} else if (text.compare(pos, 5, "false") == 0) {
			value.type = JSONValue::BOOL;
			pos += 5;
		} else if (text.compare(pos, 4, "null") == 0) {
			pos += 4;
		} else {
			char *end = nullptr;
			value.number = strtod(text.c_str() + pos, &end);
			if (end == text.c_str() + pos) {
				failed = true;
			} else {
				value.type = JSONValue::NUMBER;
				pos = (size_t)(end - text.c_str());
			}
		}
		return value;
	}
};

std::vector<uint8_t> _base64_decode(const std::string &p_text) {
	std::vector<uint8_t> out;
	unsigned buffer = 0;
	int bits = 0;
	for (char c : p_text) {
		int v;
		if (c >= 'A' && c <= 'Z') {
			v = c - 'A';
		} else if (c >= 'a' && c <= 'z') {
			v = c - 'a' + 26;
		} else if (c >= '0' && c <= '9') {
			v = c - '0' + 52;
		} else if (c == '+' || c == '-') {
			v = 62;
		} else if (c == '/' || c == '_') {
			v = 63;
		} else {
			continue;
		}
		buffer = (buffer << 6) | (unsigned)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back((uint8_t)((buffer >> bits) & 0xFF));
		}
	}
	return out;
}

struct GLTFState {
	JSONValue json;
	std::vector<std::vector<uint8_t>> buffers;
	std::vector<Transform3D> node_globals; // pose de repos de chaque nœud, repère de la scène
	GLTFData *data = nullptr;
	String error;
};

// Lit un accesseur comme une suite de nombres réels (p_components par élément).
bool _read_accessor(const GLTFState &p_state, int p_accessor, int &r_components, std::vector<float> &r_values) {
	const JSONValue &accessor = p_state.json["accessors"][(size_t)p_accessor];
	if (accessor.type != JSONValue::OBJECT || !accessor.has("bufferView") || accessor.has("sparse")) {
		return false;
	}
	const JSONValue &view = p_state.json["bufferViews"][(size_t)accessor["bufferView"].as_int()];
	const int buffer_index = view["buffer"].as_int(-1);
	if (view.type != JSONValue::OBJECT || buffer_index < 0 || buffer_index >= (int)p_state.buffers.size()) {
		return false;
	}
	const std::vector<uint8_t> &buffer = p_state.buffers[buffer_index];
	const std::string &type = accessor["type"].string;
	r_components = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : type == "MAT4" ? 16 : 0;
	const int component_type = accessor["componentType"].as_int();
	int component_size = 0;
	switch (component_type) {
		case 5120: case 5121: component_size = 1; break;
		case 5122: case 5123: component_size = 2; break;
		case 5125: case 5126: component_size = 4; break;
	}
	if (r_components == 0 || component_size == 0) {
		return false;
	}
	const size_t count = (size_t)std::max(accessor["count"].as_int(), 0);
	const size_t element_size = (size_t)r_components * component_size;
	const size_t stride = view.has("byteStride") ? (size_t)view["byteStride"].as_int() : element_size;
	const size_t start = (size_t)view["byteOffset"].as_int() + (size_t)accessor["byteOffset"].as_int();
	if (count == 0 || stride < element_size || start + (count - 1) * stride + element_size > buffer.size()) {
		return false;
	}
	const bool normalized = accessor["normalized"].type == JSONValue::BOOL && accessor["normalized"].number != 0;
	r_values.resize(count * r_components);
	for (size_t i = 0; i < count; i++) {
		const uint8_t *p = buffer.data() + start + i * stride;
		for (int c = 0; c < r_components; c++, p += component_size) {
			float v = 0;
			switch (component_type) {
				case 5120: v = (float)(int8_t)p[0]; if (normalized) { v = std::max(v / 127.0f, -1.0f); } break;
				case 5121: v = (float)p[0]; if (normalized) { v /= 255.0f; } break;
				case 5122: v = (float)(int16_t)(p[0] | (p[1] << 8)); if (normalized) { v = std::max(v / 32767.0f, -1.0f); } break;
				case 5123: v = (float)(uint16_t)(p[0] | (p[1] << 8)); if (normalized) { v /= 65535.0f; } break;
				case 5125: v = (float)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)); break;
				case 5126: memcpy(&v, p, 4); break;
			}
			r_values[i * r_components + c] = v;
		}
	}
	return true;
}

Transform3D _matrix_to_transform(const std::vector<float> &m, size_t p_at) {
	const auto f = [&](int i) { return (real_t)m[p_at + (size_t)i]; };
	return Transform3D(Basis(Vector3(f(0), f(1), f(2)), Vector3(f(4), f(5), f(6)), Vector3(f(8), f(9), f(10))), Vector3(f(12), f(13), f(14)));
}

// Position, rotation et échelle d'un nœud (champs séparés, ou matrice 4x4 décomposée).
void _node_trs(const JSONValue &p_node, GLTFData::Bone &r_bone) {
	const JSONValue &m = p_node["matrix"];
	if (m.array.size() == 16) {
		std::vector<float> values(16);
		for (int i = 0; i < 16; i++) {
			values[i] = (float)m[i].as_double();
		}
		const Transform3D t = _matrix_to_transform(values, 0);
		r_bone.position = t.origin;
		r_bone.scale = t.basis.get_scale();
		r_bone.rotation = Quaternion(t.basis.orthonormalized());
		return;
	}
	const auto number = [](const JSONValue &p_array, int p_index, double p_default) { return (real_t)p_array[p_index].as_double(p_default); };
	if (p_node["translation"].array.size() == 3) {
		r_bone.position = Vector3(number(p_node["translation"], 0, 0), number(p_node["translation"], 1, 0), number(p_node["translation"], 2, 0));
	}
	if (p_node["scale"].array.size() == 3) {
		r_bone.scale = Vector3(number(p_node["scale"], 0, 1), number(p_node["scale"], 1, 1), number(p_node["scale"], 2, 1));
	}
	if (p_node["rotation"].array.size() == 4) {
		r_bone.rotation = Quaternion(number(p_node["rotation"], 0, 0), number(p_node["rotation"], 1, 0), number(p_node["rotation"], 2, 0), number(p_node["rotation"], 3, 1)).normalized();
	}
}

void _add_primitive(GLTFState &p_state, const JSONValue &p_primitive, int p_node, int p_skin) {
	if (p_primitive["mode"].as_int(4) != 4) {
		return; // points, lignes, bandes : ignorés
	}
	GLTFData &data = *p_state.data;
	const JSONValue &attributes = p_primitive["attributes"];
	std::vector<float> positions, normals, uvs, colors, index_values, joints, weights, inverse_binds;
	int components = 0, color_components = 0;
	if (!attributes.has("POSITION") || !_read_accessor(p_state, attributes["POSITION"].as_int(), components, positions) || components != 3) {
		return;
	}
	const size_t count = positions.size() / 3;
	if (attributes.has("NORMAL") && (!_read_accessor(p_state, attributes["NORMAL"].as_int(), components, normals) || components != 3 || normals.size() != count * 3)) {
		normals.clear();
	}
	if (attributes.has("TEXCOORD_0") && (!_read_accessor(p_state, attributes["TEXCOORD_0"].as_int(), components, uvs) || components != 2 || uvs.size() != count * 2)) {
		uvs.clear();
	}
	if (attributes.has("COLOR_0") && (!_read_accessor(p_state, attributes["COLOR_0"].as_int(), color_components, colors) || color_components < 3 || colors.size() != count * color_components)) {
		colors.clear();
	}
	std::vector<uint32_t> local_indices;
	if (p_primitive.has("indices")) {
		if (!_read_accessor(p_state, p_primitive["indices"].as_int(), components, index_values) || components != 1) {
			return;
		}
		for (float v : index_values) {
			if (v < 0 || (size_t)v >= count) {
				return;
			}
			local_indices.push_back((uint32_t)v);
		}
	} else {
		for (size_t i = 0; i < count; i++) {
			local_indices.push_back((uint32_t)i);
		}
	}
	local_indices.resize(local_indices.size() / 3 * 3);

	// Peau : os et poids par sommet, matrices inverses de liaison par os.
	const JSONValue &skin = p_state.json["skins"][p_skin];
	const std::vector<JSONValue> &skin_joints = skin["joints"].array;
	bool skinned = p_skin >= 0 && !skin_joints.empty() && attributes.has("JOINTS_0") && attributes.has("WEIGHTS_0");
	if (skinned) {
		skinned = _read_accessor(p_state, attributes["JOINTS_0"].as_int(), components, joints) && components == 4 && joints.size() == count * 4;
		skinned = skinned && _read_accessor(p_state, attributes["WEIGHTS_0"].as_int(), components, weights) && components == 4 && weights.size() == count * 4;
		if (skinned && skin.has("inverseBindMatrices")) {
			if (!_read_accessor(p_state, skin["inverseBindMatrices"].as_int(), components, inverse_binds) || components != 16 || inverse_binds.size() < skin_joints.size() * 16) {
				inverse_binds.clear();
			}
		}
	}
	std::vector<Transform3D> binds(skinned ? skin_joints.size() : 0);
	for (size_t j = 0; j < binds.size(); j++) {
		if (!inverse_binds.empty()) {
			binds[j] = _matrix_to_transform(inverse_binds, j * 16);
		}
	}

	std::vector<Vector3> local_positions(count), local_normals(count);
	for (size_t i = 0; i < count; i++) {
		local_positions[i] = Vector3(positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]);
		if (!normals.empty()) {
			local_normals[i] = Vector3(normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]);
		}
	}
	if (normals.empty()) {
		// Pas de normales dans le fichier : normales de face, cumulées sur les sommets partagés.
		for (size_t i = 0; i + 2 < local_indices.size(); i += 3) {
			const uint32_t a = local_indices[i], b = local_indices[i + 1], c = local_indices[i + 2];
			const Vector3 n = (local_positions[b] - local_positions[a]).cross(local_positions[c] - local_positions[a]);
			local_normals[a] += n;
			local_normals[b] += n;
			local_normals[c] += n;
		}
		for (Vector3 &n : local_normals) {
			n = n.length_squared() > 0 ? n.normalized() : Vector3(0, 1, 0);
		}
	}

	Color base(1, 1, 1, 1);
	if (p_primitive.has("material")) {
		const JSONValue &factor = p_state.json["materials"][p_primitive["material"].as_int()]["pbrMetallicRoughness"]["baseColorFactor"];
		if (factor.array.size() == 4) {
			base = Color((float)factor[0].as_double(1), (float)factor[1].as_double(1), (float)factor[2].as_double(1), (float)factor[3].as_double(1));
		}
	}
	const uint32_t first = (uint32_t)data.vertices.size();
	for (size_t i = 0; i < count; i++) {
		RenderingServer3D::Vertex vertex;
		SkinVertex sv;
		if (skinned) {
			int used = 0;
			for (int k = 0; k < 4; k++) {
				const int joint = (int)joints[i * 4 + k];
				const float weight = weights[i * 4 + k];
				if (weight <= 0 || joint < 0 || joint >= (int)skin_joints.size()) {
					continue;
				}
				sv.bones[used] = skin_joints[joint].as_int(-1);
				sv.weights[used] = weight;
				sv.positions[used] = binds[joint].xform(local_positions[i]);
				sv.normals[used] = binds[joint].basis.xform(local_normals[i]);
				used++;
			}
			if (used == 0) {
				skinned = false; // sommet sans os : toute la primitive suit son nœud
			}
		}
		if (!skinned) {
			sv = SkinVertex();
			sv.bones[0] = p_node;
			sv.weights[0] = 1;
			sv.positions[0] = local_positions[i];
			sv.normals[0] = local_normals[i];
		}
		if (!uvs.empty()) {
			vertex.uv = Vector2(uvs[i * 2], uvs[i * 2 + 1]);
		}
		vertex.color = base;
		if (!colors.empty()) {
			const float *c = &colors[i * color_components];
			vertex.color = Color(base.r * c[0], base.g * c[1], base.b * c[2], base.a * (color_components == 4 ? c[3] : 1.0f));
		}
		data.vertices.push_back(vertex);
		data.skin.push_back(sv);
	}
	data.has_skins = data.has_skins || skinned;
	bool flip = false;
	if (!skinned && p_node >= 0 && p_node < (int)p_state.node_globals.size()) {
		const Basis &basis = p_state.node_globals[p_node].basis;
		flip = basis.get_column(0).cross(basis.get_column(1)).dot(basis.get_column(2)) < 0; // échelle négative
	}
	for (size_t i = 0; i + 2 < local_indices.size(); i += 3) {
		uint32_t a = first + local_indices[i], b = first + local_indices[i + 1], c = first + local_indices[i + 2];
		if (flip) {
			std::swap(b, c);
		}
		data.indices.push_back(a);
		data.indices.push_back(b);
		data.indices.push_back(c);
	}
}

void _add_node(GLTFState &p_state, int p_node, int p_depth) {
	const JSONValue &node = p_state.json["nodes"][p_node];
	if (p_node < 0 || node.type != JSONValue::OBJECT || p_depth > 64) {
		return;
	}
	if (node.has("mesh")) {
		for (const JSONValue &primitive : p_state.json["meshes"][node["mesh"].as_int()]["primitives"].array) {
			_add_primitive(p_state, primitive, p_node, node.has("skin") ? node["skin"].as_int(-1) : -1);
		}
	}
	for (const JSONValue &child : node["children"].array) {
		_add_node(p_state, child.as_int(-1), p_depth + 1);
	}
}

Transform3D _bone_global(const GLTFData &p_data, int p_bone, int p_depth = 0) {
	const GLTFData::Bone &bone = p_data.bones[p_bone];
	const Transform3D local(bone.rotation.to_basis().scaled_local(bone.scale), bone.position);
	if (bone.parent < 0 || bone.parent >= (int)p_data.bones.size() || p_depth > 64) {
		return local;
	}
	return _bone_global(p_data, bone.parent, p_depth + 1) * local;
}

void _read_animations(GLTFState &p_state) {
	GLTFData &data = *p_state.data;
	for (const JSONValue &animation : p_state.json["animations"].array) {
		GLTFData::Clip clip;
		clip.name = animation["name"].string.empty() ? "animation_" + itos((int64_t)data.clips.size()) : animation["name"].string;
		for (const JSONValue &channel : animation["channels"].array) {
			const JSONValue &sampler = animation["samplers"][channel["sampler"].as_int(-1)];
			const std::string &path = channel["target"]["path"].string;
			GLTFData::Track track;
			track.bone = channel["target"]["node"].as_int(-1);
			track.path = path == "translation" ? 0 : path == "rotation" ? 1 : path == "scale" ? 2 : -1;
			if (sampler.type != JSONValue::OBJECT || track.path < 0 || track.bone < 0 || track.bone >= (int)data.bones.size()) {
				continue; // « weights » (formes de mélange) : non repris
			}
			int time_components = 0, value_components = 0;
			std::vector<float> values;
			if (!_read_accessor(p_state, sampler["input"].as_int(-1), time_components, track.times) || time_components != 1) {
				continue;
			}
			if (!_read_accessor(p_state, sampler["output"].as_int(-1), value_components, values) || value_components != (track.path == 1 ? 4 : 3)) {
				continue;
			}
			const std::string &interpolation = sampler["interpolation"].string;
			track.step = interpolation == "STEP";
			const size_t keys = track.times.size();
			if (interpolation == "CUBICSPLINE") {
				// Trois valeurs par clé (tangente, valeur, tangente) : on garde la valeur, en linéaire.
				if (values.size() < keys * 3 * (size_t)value_components) {
					continue;
				}
				track.values.resize(keys * value_components);
				for (size_t k = 0; k < keys; k++) {
					for (int c = 0; c < value_components; c++) {
						track.values[k * value_components + c] = values[(k * 3 + 1) * value_components + c];
					}
				}
			} else {
				if (values.size() < keys * (size_t)value_components) {
					continue;
				}
				values.resize(keys * value_components);
				track.values = values;
			}
			if (keys == 0) {
				continue;
			}
			clip.length = std::max(clip.length, track.times.back());
			clip.tracks.push_back(track);
		}
		if (!clip.tracks.empty()) {
			data.clips.push_back(clip);
		}
	}
}

uint32_t _read_u32(const std::vector<uint8_t> &p_data, size_t p_at) {
	return (uint32_t)p_data[p_at] | ((uint32_t)p_data[p_at + 1] << 8) | ((uint32_t)p_data[p_at + 2] << 16) | ((uint32_t)p_data[p_at + 3] << 24);
}

bool _load(const String &p_path, GLTFState &p_state) {
	GLTFData &data = *p_state.data;
	const std::vector<uint8_t> file = FileAccess::get_file_as_bytes(p_path);
	std::string json_text;
	std::vector<uint8_t> binary_chunk;
	if (file.size() >= 12 && _read_u32(file, 0) == 0x46546C67) { // « glTF » : fichier binaire .glb
		if (_read_u32(file, 4) != 2) {
			p_state.error = "seule la version 2 de glTF est lue";
			return false;
		}
		size_t at = 12;
		while (at + 8 <= file.size()) {
			const size_t length = _read_u32(file, at);
			const uint32_t type = _read_u32(file, at + 4);
			at += 8;
			if (length > file.size() - at) {
				break;
			}
			if (type == 0x4E4F534A) { // JSON
				json_text.assign((const char *)file.data() + at, length);
			} else if (type == 0x004E4942 && binary_chunk.empty()) { // BIN
				binary_chunk.assign(file.begin() + at, file.begin() + at + length);
			}
			at += length;
		}
	} else {
		json_text.assign(file.begin(), file.end());
	}
	JSONParser parser(json_text);
	p_state.json = parser.parse();
	if (parser.failed || p_state.json.type != JSONValue::OBJECT) {
		p_state.error = "JSON illisible";
		return false;
	}
	const size_t slash = p_path.find_last_of('/');
	const String folder = slash == String::npos ? String() : p_path.substr(0, slash + 1);
	for (const JSONValue &buffer : p_state.json["buffers"].array) {
		const std::string &uri = buffer["uri"].string;
		if (uri.empty()) {
			p_state.buffers.push_back(binary_chunk);
		} else if (uri.compare(0, 5, "data:") == 0) {
			const size_t comma = uri.find(',');
			p_state.buffers.push_back(comma == std::string::npos ? std::vector<uint8_t>() : _base64_decode(uri.substr(comma + 1)));
		} else {
			p_state.buffers.push_back(FileAccess::get_file_as_bytes(folder + uri));
			if (p_state.buffers.back().empty()) {
				p_state.error = "fichier de données introuvable : " + folder + uri;
				return false;
			}
		}
	}
	// Chaque nœud du fichier devient un os : nom, parent, pose de repos.
	const std::vector<JSONValue> &nodes = p_state.json["nodes"].array;
	data.bones.resize(nodes.size());
	for (size_t i = 0; i < nodes.size(); i++) {
		data.bones[i].name = nodes[i]["name"].string.empty() ? "noeud_" + itos((int64_t)i) : nodes[i]["name"].string;
		_node_trs(nodes[i], data.bones[i]);
	}
	for (size_t i = 0; i < nodes.size(); i++) {
		for (const JSONValue &child : nodes[i]["children"].array) {
			const int c = child.as_int(-1);
			if (c >= 0 && c < (int)nodes.size() && c != (int)i) {
				data.bones[c].parent = (int)i;
			}
		}
	}
	p_state.node_globals.resize(nodes.size());
	for (size_t i = 0; i < nodes.size(); i++) {
		p_state.node_globals[i] = _bone_global(data, (int)i);
	}
	const JSONValue &scenes = p_state.json["scenes"];
	if (!scenes.array.empty()) {
		const JSONValue &scene = scenes[std::max(p_state.json["scene"].as_int(0), 0)];
		for (const JSONValue &root : scene["nodes"].array) {
			_add_node(p_state, root.as_int(-1), 0);
		}
	} else {
		// Pas de scène : toutes les mailles, sans transformation.
		for (const JSONValue &mesh : p_state.json["meshes"].array) {
			for (const JSONValue &primitive : mesh["primitives"].array) {
				_add_primitive(p_state, primitive, -1, -1);
			}
		}
	}
	if (data.indices.empty()) {
		p_state.error = "aucun triangle";
		return false;
	}
	// Pose de repos : c'est elle qu'affiche le maillage quand il n'est pas sous un Skeleton3D.
	ArrayMesh::apply_skin(data.skin, p_state.node_globals, data.vertices);
	_read_animations(p_state);
	return true;
}
} // namespace

Error gltf_load(const String &p_path, GLTFData &r_data, String *r_error) {
	if (!FileAccess::exists(p_path)) {
		if (r_error) {
			*r_error = "fichier introuvable";
		}
		return ERR_FILE_NOT_FOUND;
	}
	GLTFState state;
	state.data = &r_data;
	if (!_load(p_path, state)) {
		if (r_error) {
			*r_error = state.error;
		}
		return ERR_PARSE_ERROR;
	}
	return OK;
}

Ref<Resource> ResourceFormatLoaderGLTF::load(const String &p_path, Error *r_error) {
	GLTFData data;
	String message;
	const Error err = gltf_load(p_path, data, &message);
	if (r_error) {
		*r_error = err;
	}
	if (err != OK) {
		if (err != ERR_FILE_NOT_FOUND) {
			print_error("Fichier glTF illisible (" + message + ") : " + p_path);
		}
		return Ref<Resource>();
	}
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->set_data(data.vertices, data.indices);
	mesh->set_skin(data.skin);
	return mesh;
}
