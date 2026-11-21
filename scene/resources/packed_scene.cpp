#include "packed_scene.h"

#include "core/io/file_access.h"
#include "core/io/variant_parser.h"
#include "core/object/script_language.h"
#include "core/string/ustring.h"

// ---------------------------------------------------------------------------
// Lecture commune (.scn et .res)
// ---------------------------------------------------------------------------
namespace {

struct TextLoadState {
	String path;
	std::map<String, Ref<Resource>> ext_resources;
	std::map<String, Ref<Resource>> sub_resources;
	Ref<Resource> main_resource; // .res
	String main_type;
	std::vector<PackedScene::NodeData> nodes;
	std::vector<PackedScene::ConnectionData> connections;
};

std::set<String> &_loading_paths() {
	static std::set<String> paths;
	return paths;
}

Error _load_text(const String &p_text, TextLoadState &r_state) {
	VariantParser::Stream stream(p_text);
	VariantParser::ResourceResolver resolver = [&](const String &p_kind, const String &p_id) -> Variant {
		if (p_kind == "ExtResource") {
			auto it = r_state.ext_resources.find(p_id);
			if (it != r_state.ext_resources.end()) {
				return Variant(it->second);
			}
		} else {
			auto it = r_state.sub_resources.find(p_id);
			if (it != r_state.sub_resources.end()) {
				return Variant(it->second);
			}
		}
		print_error(r_state.path + " : ressource " + p_kind + "(\"" + p_id + "\") introuvable.");
		return Variant();
	};

	enum Section {
		SECTION_NONE,
		SECTION_SUB_RESOURCE,
		SECTION_RESOURCE,
		SECTION_NODE,
	};
	Section section = SECTION_NONE;
	Ref<Resource> current_resource;

	while (true) {
		VariantParser::Tag tag;
		String key, err;
		Variant value;
		VariantParser::Item item = VariantParser::parse_next(stream, tag, key, value, err, &resolver);
		if (item == VariantParser::ITEM_EOF) {
			break;
		}
		if (item == VariantParser::ITEM_ERROR) {
			print_error(r_state.path + ":" + itos(stream.line) + " - " + err);
			return ERR_PARSE_ERROR;
		}
		if (item == VariantParser::ITEM_TAG) {
			section = SECTION_NONE;
			current_resource.unref();
			if (tag.name == "mex_scene") {
				// en-tête
			} else if (tag.name == "mex_resource") {
				r_state.main_type = tag.get("type").as_string();
			} else if (tag.name == "ext_resource") {
				String id = tag.get("id").as_string();
				String path = tag.get("path").as_string();
				if (_loading_paths().count(path)) {
					print_error(r_state.path + " : dépendance cyclique vers " + path);
					continue;
				}
				Ref<Resource> res = ResourceLoader::load(path);
				if (res.is_null()) {
					print_error(r_state.path + " : dépendance introuvable : " + path);
				}
				r_state.ext_resources[id] = res;
			} else if (tag.name == "sub_resource" || tag.name == "resource") {
				String type = tag.name == "resource" ? r_state.main_type : tag.get("type").as_string();
				Object *obj = ClassDB::instantiate(type);
				Resource *res = Object::cast_to<Resource>(obj);
				if (!res) {
					if (obj) {
						memdelete(obj);
					}
					print_error(r_state.path + ":" + itos(stream.line) + " - type de ressource inconnu : " + type);
					continue;
				}
				current_resource = Ref<Resource>(res);
				if (tag.name == "resource") {
					r_state.main_resource = current_resource;
					section = SECTION_RESOURCE;
				} else {
					r_state.sub_resources[tag.get("id").as_string()] = current_resource;
					section = SECTION_SUB_RESOURCE;
				}
			} else if (tag.name == "node") {
				PackedScene::NodeData nd;
				nd.name = tag.get("name").as_string();
				nd.type = tag.get("type").as_string();
				nd.parent = tag.get("parent").as_string();
				if (tag.has("instance")) {
					nd.instance = Ref<PackedScene>(Object::cast_to<PackedScene>(tag.get("instance").as_object()));
					if (nd.instance.is_null()) {
						print_error(r_state.path + " : la sous-scène du nœud '" + nd.name + "' est introuvable.");
					}
				}
				if (tag.has("groups")) {
					for (const Variant &g : tag.get("groups").as_array().vec()) {
						nd.groups.push_back(g.as_string());
					}
				}
				r_state.nodes.push_back(nd);
				section = SECTION_NODE;
			} else if (tag.name == "connection") {
				PackedScene::ConnectionData cd;
				cd.signal = tag.get("signal").as_string();
				cd.from = tag.get("from").as_string();
				cd.to = tag.get("to").as_string();
				cd.method = tag.get("method").as_string();
				r_state.connections.push_back(cd);
			} else {
				print_warning(r_state.path + ":" + itos(stream.line) + " - section inconnue : [" + tag.name + "]");
			}
			continue;
		}
		// ITEM_ASSIGN
		switch (section) {
			case SECTION_SUB_RESOURCE:
			case SECTION_RESOURCE: {
				if (current_resource.is_valid()) {
					bool valid = false;
					current_resource->set(key, value, &valid);
					if (!valid) {
						print_warning(r_state.path + " : propriété inconnue '" + key + "' pour " + current_resource->get_class() + ".");
					}
				}
			} break;
			case SECTION_NODE: {
				r_state.nodes.back().properties.push_back({ key, value });
			} break;
			default:
				break;
		}
	}
	return OK;
}

// ---------------------------------------------------------------------------
// Écriture commune
// ---------------------------------------------------------------------------
struct TextSaveState {
	String self_path;
	std::vector<Ref<Resource>> ext;
	std::vector<Ref<Resource>> sub;
	std::map<const Resource *, String> ids; // -> ExtResource("1") / SubResource("1")
};

void _get_storable_properties(const Object *p_object, std::vector<std::pair<String, Variant>> &r_props) {
	std::vector<PropertyInfo> props;
	p_object->get_property_list(&props);
	Ref<Script> script = Ref<Script>(Object::cast_to<Script>(p_object->get_script().as_object()));
	std::set<String> seen;
	for (const PropertyInfo &pi : props) {
		if (!(pi.usage & PROPERTY_USAGE_STORAGE) || (pi.usage & PROPERTY_USAGE_GROUP) || seen.count(pi.name)) {
			continue;
		}
		seen.insert(pi.name);
		Variant value = p_object->get(pi.name);
		Variant def;
		if (pi.usage & PROPERTY_USAGE_SCRIPT_VARIABLE) {
			if (script.is_valid() && script->get_property_default_value(pi.name, def) && def == value) {
				continue;
			}
		} else {
			bool valid = false;
			def = ClassDB::class_get_default_property_value(p_object->get_class(), pi.name, &valid);
			if (valid && def == value) {
				continue;
			}
		}
		r_props.push_back({ pi.name, value });
	}
}

void _collect_resources(const Variant &p_value, TextSaveState &r_state) {
	switch (p_value.get_type()) {
		case Variant::OBJECT: {
			Resource *res = Object::cast_to<Resource>(p_value.as_object());
			if (!res || r_state.ids.count(res)) {
				return;
			}
			if (!res->get_path().empty() && res->get_path() != r_state.self_path) {
				r_state.ext.push_back(Ref<Resource>(res));
				r_state.ids[res] = "ExtResource(\"" + itos((int64_t)r_state.ext.size()) + "\")";
			} else {
				r_state.ids[res] = ""; // réservé (évite les cycles)
				std::vector<std::pair<String, Variant>> props;
				_get_storable_properties(res, props);
				for (auto &kv : props) {
					_collect_resources(kv.second, r_state);
				}
				r_state.sub.push_back(Ref<Resource>(res));
				r_state.ids[res] = "SubResource(\"" + itos((int64_t)r_state.sub.size()) + "\")";
			}
		} break;
		case Variant::ARRAY: {
			for (const Variant &v : p_value.as_array().vec()) {
				_collect_resources(v, r_state);
			}
		} break;
		case Variant::DICTIONARY: {
			for (auto &kv : p_value.as_dictionary().map()) {
				_collect_resources(kv.first, r_state);
				_collect_resources(kv.second, r_state);
			}
		} break;
		default:
			break;
	}
}

String _write_resource_sections(TextSaveState &r_state, const VariantWriter::ObjectEncoder &p_encoder) {
	String out;
	for (size_t i = 0; i < r_state.ext.size(); i++) {
		const Ref<Resource> &res = r_state.ext[i];
		out += "[ext_resource type=\"" + res->get_class() + "\" path=\"" + str_c_escape(res->get_path()) + "\" id=\"" + itos((int64_t)i + 1) + "\"]\n";
	}
	if (!r_state.ext.empty()) {
		out += "\n";
	}
	for (size_t i = 0; i < r_state.sub.size(); i++) {
		const Ref<Resource> &res = r_state.sub[i];
		out += "[sub_resource type=\"" + res->get_class() + "\" id=\"" + itos((int64_t)i + 1) + "\"]\n";
		std::vector<std::pair<String, Variant>> props;
		_get_storable_properties(res.ptr(), props);
		for (auto &kv : props) {
			out += kv.first + " = " + VariantWriter::write(kv.second, &p_encoder) + "\n";
		}
		out += "\n";
	}
	return out;
}

} // namespace

// ---------------------------------------------------------------------------
// PackedScene
// ---------------------------------------------------------------------------
Error PackedScene::load_from_text(const String &p_text, const String &p_path) {
	TextLoadState state;
	state.path = p_path;
	Error err = _load_text(p_text, state);
	if (err != OK) {
		return err;
	}
	nodes = state.nodes;
	connections = state.connections;
	if (nodes.empty()) {
		print_error(p_path + " : la scène ne contient aucun nœud.");
		return ERR_INVALID_DATA;
	}
	return OK;
}

String PackedScene::get_root_type() const {
	if (nodes.empty()) {
		return String();
	}
	if (nodes[0].instance.is_valid()) {
		return nodes[0].instance->get_root_type();
	}
	return nodes[0].type;
}

Node *PackedScene::instantiate() const {
	ERR_FAIL_COND_V_MSG(nodes.empty(), nullptr, "PackedScene::instantiate() : scène vide.");
	Node *root = nullptr;
	for (size_t i = 0; i < nodes.size(); i++) {
		const NodeData &nd = nodes[i];
		Node *node = nullptr;
		if (nd.instance.is_valid()) {
			node = nd.instance->instantiate();
		} else if (!nd.type.empty()) {
			Object *obj = ClassDB::instantiate(nd.type);
			node = Object::cast_to<Node>(obj);
			if (!node) {
				if (obj) {
					memdelete(obj);
				}
				print_error(get_path() + " : type de nœud inconnu '" + nd.type + "' (nœud '" + nd.name + "'), remplacé par Node.");
			}
		}
		if (!node) {
			node = memnew(Node);
		}
		node->set_name(nd.name);
		for (auto &kv : nd.properties) {
			bool valid = false;
			node->set(kv.first, kv.second, &valid);
			if (!valid) {
				print_warning(get_path() + " : propriété inconnue '" + kv.first + "' pour le nœud '" + nd.name + "' (" + node->get_class() + ").");
			}
		}
		for (const String &g : nd.groups) {
			node->add_to_group(g);
		}
		if (i == 0) {
			root = node;
		} else {
			Node *parent = root->get_node_or_null(nd.parent.empty() ? "." : nd.parent);
			if (!parent) {
				print_error(get_path() + " : parent introuvable '" + nd.parent + "' pour le nœud '" + nd.name + "'.");
				memdelete(node);
				continue;
			}
			parent->add_child(node);
			node->set_owner(root);
		}
	}
	root->set_scene_file_path(get_path());

	for (const ConnectionData &cd : connections) {
		Node *from = root->get_node_or_null(cd.from);
		Node *to = root->get_node_or_null(cd.to);
		if (!from || !to) {
			print_warning(get_path() + " : connexion invalide du signal '" + cd.signal + "' (" + cd.from + " -> " + cd.to + ").");
			continue;
		}
		if (!from->has_signal(cd.signal)) {
			from->add_user_signal(cd.signal); // signal de script non disponible (éditeur)
		}
		from->connect(cd.signal, Callable(to, cd.method), Object::CONNECT_PERSIST);
	}
	return root;
}

void PackedScene::_pack_node(Node *p_root, Node *p_node) {
	NodeData nd;
	nd.name = p_node->get_name();
	if (p_node != p_root) {
		nd.parent = p_root->get_path_to(p_node->get_parent());
	}

	std::vector<std::pair<String, Variant>> props;
	_get_storable_properties(p_node, props);
	std::set<String> groups = p_node->get_groups_set();

	std::set<String> inherited_connections; // connexions déjà définies dans la sous-scène instanciée
	const bool is_instance = p_node != p_root && !p_node->get_scene_file_path().empty();
	if (is_instance) {
		nd.instance = ResourceLoader::load(p_node->get_scene_file_path());
	}
	if (nd.instance.is_valid()) {
		// On ne sauvegarde que ce qui diffère de la sous-scène d'origine.
		ScriptingDisabledScope no_scripts;
		Node *baseline = nd.instance->instantiate();
		if (baseline) {
			for (auto &kv : props) {
				bool valid = false;
				Variant base_value = baseline->get(kv.first, &valid);
				if (!valid || base_value != kv.second) {
					nd.properties.push_back(kv);
				}
			}
			for (const String &g : baseline->get_groups_set()) {
				groups.erase(g);
			}
			std::vector<MethodInfo> base_signals;
			ClassDB::get_signal_list(baseline->get_class(), &base_signals);
			Ref<Script> base_script = Ref<Script>(Object::cast_to<Script>(baseline->get_script().as_object()));
			if (base_script.is_valid()) {
				base_script->get_script_signal_list(&base_signals);
			}
			for (const MethodInfo &mi : base_signals) {
				std::vector<Object::Connection> conns;
				baseline->get_signal_connection_list(mi.name, &conns);
				for (const Object::Connection &c : conns) {
					Node *target = Object::cast_to<Node>(c.callable.get_object());
					if (target && (target == baseline || baseline->is_ancestor_of(target))) {
						inherited_connections.insert(mi.name + "|" + baseline->get_path_to(target) + "|" + c.callable.method);
					}
				}
			}
			memdelete(baseline);
		} else {
			nd.properties = props;
		}
	} else {
		nd.type = p_node->get_class();
		nd.properties = props;
	}
	nd.groups.assign(groups.begin(), groups.end());
	nodes.push_back(nd);

	// Connexions persistantes vers des nœuds de la scène.
	std::vector<MethodInfo> signal_list;
	ClassDB::get_signal_list(p_node->get_class(), &signal_list);
	Ref<Script> script = Ref<Script>(Object::cast_to<Script>(p_node->get_script().as_object()));
	if (script.is_valid()) {
		script->get_script_signal_list(&signal_list);
	}
	std::set<String> seen_signals;
	for (const MethodInfo &mi : signal_list) {
		if (!seen_signals.insert(mi.name).second) {
			continue;
		}
		std::vector<Object::Connection> conns;
		p_node->get_signal_connection_list(mi.name, &conns);
		for (const Object::Connection &c : conns) {
			if (!(c.flags & Object::CONNECT_PERSIST)) {
				continue;
			}
			Node *target = Object::cast_to<Node>(c.callable.get_object());
			if (!target || (target != p_root && !p_root->is_ancestor_of(target))) {
				continue;
			}
			if (!inherited_connections.empty() && (target == p_node || p_node->is_ancestor_of(target)) && inherited_connections.count(mi.name + "|" + p_node->get_path_to(target) + "|" + c.callable.method)) {
				continue;
			}
			ConnectionData cd;
			cd.signal = mi.name;
			cd.from = p_root->get_path_to(p_node);
			cd.to = p_root->get_path_to(target);
			cd.method = c.callable.method;
			connections.push_back(cd);
		}
	}

	for (Node *child : p_node->get_children_vector()) {
		if (child->get_owner() == p_root) {
			_pack_node(p_root, child);
		}
	}
}

Error PackedScene::pack(Node *p_root) {
	ERR_FAIL_NULL_V(p_root, ERR_INVALID_PARAMETER);
	nodes.clear();
	connections.clear();
	_pack_node(p_root, p_root);
	return OK;
}

String PackedScene::save_to_text() const {
	TextSaveState state;
	state.self_path = get_path();
	for (const NodeData &nd : nodes) {
		if (nd.instance.is_valid()) {
			_collect_resources(Variant(nd.instance), state);
		}
		for (auto &kv : nd.properties) {
			_collect_resources(kv.second, state);
		}
	}
	VariantWriter::ObjectEncoder encoder = [&](const Variant &p_object) -> String {
		Resource *res = Object::cast_to<Resource>(p_object.as_object());
		auto it = state.ids.find(res);
		return (res && it != state.ids.end()) ? it->second : String("null");
	};

	String out = "[mex_scene format=1]\n\n";
	out += _write_resource_sections(state, encoder);
	for (size_t i = 0; i < nodes.size(); i++) {
		const NodeData &nd = nodes[i];
		out += "[node name=\"" + str_c_escape(nd.name) + "\"";
		if (nd.instance.is_null()) {
			out += " type=\"" + nd.type + "\"";
		}
		if (i > 0) {
			out += " parent=\"" + str_c_escape(nd.parent.empty() ? String(".") : nd.parent) + "\"";
		}
		if (nd.instance.is_valid()) {
			out += " instance=" + encoder(Variant(nd.instance));
		}
		if (!nd.groups.empty()) {
			Array groups;
			for (const String &g : nd.groups) {
				groups.push_back(Variant(g));
			}
			out += " groups=" + VariantWriter::write(Variant(groups));
		}
		out += "]\n";
		for (auto &kv : nd.properties) {
			out += kv.first + " = " + VariantWriter::write(kv.second, &encoder) + "\n";
		}
		out += "\n";
	}
	for (const ConnectionData &cd : connections) {
		out += "[connection signal=\"" + cd.signal + "\" from=\"" + str_c_escape(cd.from) + "\" to=\"" + str_c_escape(cd.to) + "\" method=\"" + cd.method + "\"]\n";
	}
	return out;
}

void PackedScene::_bind_methods() {
	ClassDB::bind_method(D_METHOD("instantiate"), &PackedScene::instantiate);
	ClassDB::bind_method(D_METHOD("can_instantiate"), &PackedScene::can_instantiate);
	ClassDB::bind_method(D_METHOD("pack", "root"), &PackedScene::pack);
}

// ---------------------------------------------------------------------------
// ResourceSaver / chargeurs
// ---------------------------------------------------------------------------
String ResourceSaver::resource_to_text(const Ref<Resource> &p_resource) {
	if (const PackedScene *scene = Object::cast_to<PackedScene>(p_resource.ptr())) {
		return scene->save_to_text();
	}
	TextSaveState state;
	state.self_path = p_resource->get_path();
	state.ids[p_resource.ptr()] = "null"; // pas d'auto-référence
	std::vector<std::pair<String, Variant>> props;
	_get_storable_properties(p_resource.ptr(), props);
	for (auto &kv : props) {
		_collect_resources(kv.second, state);
	}
	VariantWriter::ObjectEncoder encoder = [&](const Variant &p_object) -> String {
		Resource *res = Object::cast_to<Resource>(p_object.as_object());
		auto it = state.ids.find(res);
		return (res && it != state.ids.end()) ? it->second : String("null");
	};
	String out = "[mex_resource type=\"" + p_resource->get_class() + "\" format=1]\n\n";
	out += _write_resource_sections(state, encoder);
	out += "[resource]\n";
	for (auto &kv : props) {
		out += kv.first + " = " + VariantWriter::write(kv.second, &encoder) + "\n";
	}
	return out;
}

Error ResourceSaver::save(const String &p_path, const Ref<Resource> &p_resource) {
	ERR_FAIL_COND_V(p_resource.is_null(), ERR_INVALID_PARAMETER);
	String ext = path_get_extension(p_path);
	const bool is_scene = Object::cast_to<PackedScene>(p_resource.ptr()) != nullptr;
	ERR_FAIL_COND_V_MSG(is_scene && ext != "scn", ERR_INVALID_PARAMETER, "Une scène doit être enregistrée avec l'extension .scn : " + p_path);
	ERR_FAIL_COND_V_MSG(!is_scene && ext != "res", ERR_INVALID_PARAMETER, "Une ressource doit être enregistrée avec l'extension .res : " + p_path);
	Error err = FileAccess::write_string(p_path, resource_to_text(p_resource));
	if (err != OK) {
		print_error("Impossible d'écrire le fichier : " + p_path);
	}
	return err;
}

Ref<Resource> ResourceFormatLoaderScene::load(const String &p_path, Error *r_error) {
	Error err = OK;
	String text = FileAccess::get_file_as_string(p_path, &err);
	Ref<PackedScene> scene;
	if (err == OK) {
		scene.instantiate();
		_loading_paths().insert(p_path);
		err = scene->load_from_text(text, p_path);
		_loading_paths().erase(p_path);
	}
	if (r_error) {
		*r_error = err;
	}
	if (err != OK) {
		return Ref<Resource>();
	}
	return scene;
}

String ResourceFormatLoaderText::get_resource_type(const String &p_path) const {
	// Lit uniquement l'en-tête : [mex_resource type="..."]
	String text = FileAccess::get_file_as_string(p_path);
	VariantParser::Stream stream(text);
	VariantParser::Tag tag;
	String key, err;
	Variant value;
	if (VariantParser::parse_next(stream, tag, key, value, err) == VariantParser::ITEM_TAG && tag.name == "mex_resource") {
		return tag.get("type").as_string();
	}
	return "Resource";
}

Ref<Resource> ResourceFormatLoaderText::load(const String &p_path, Error *r_error) {
	Error err = OK;
	String text = FileAccess::get_file_as_string(p_path, &err);
	TextLoadState state;
	state.path = p_path;
	if (err == OK) {
		_loading_paths().insert(p_path);
		err = _load_text(text, state);
		_loading_paths().erase(p_path);
		if (err == OK && state.main_resource.is_null()) {
			print_error(p_path + " : section [resource] manquante.");
			err = ERR_INVALID_DATA;
		}
	}
	if (r_error) {
		*r_error = err;
	}
	if (err != OK) {
		return Ref<Resource>();
	}
	return state.main_resource;
}
