#include "node.h"

#include "core/object/script_language.h"
#include "core/string/ustring.h"
#include "scene/main/scene_tree.h"

Node::Node() {
}

Node::~Node() {
}

String Node::to_string() const {
	return name + ":<" + get_class() + "#" + itos((int64_t)get_instance_id()) + ">";
}

// ---------------------------------------------------------------------------
// Appels vers le script
// ---------------------------------------------------------------------------
Variant Node::_call_script(const String &p_method, const Variant **p_args, int p_argc) {
	ScriptInstance *si = get_script_instance();
	if (!si || !si->has_method(p_method)) {
		return Variant();
	}
	CallError ce;
	return si->callp(p_method, p_args, p_argc, ce);
}

void Node::_call_input(const Ref<InputEvent> &p_event) {
	input(p_event);
	if (!ObjectDB::get_instance(get_instance_id())) {
		return;
	}
	Variant ev(p_event);
	const Variant *args[1] = { &ev };
	_call_script("_input", args, 1);
}

void Node::_call_unhandled_input(const Ref<InputEvent> &p_event) {
	Variant ev(p_event);
	const Variant *args[1] = { &ev };
	_call_script("_unhandled_input", args, 1);
}

void Node::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			// Active automatiquement les callbacks définis par le script.
			if (ScriptInstance *si = get_script_instance()) {
				if (si->has_method("_process")) {
					set_process(true);
				}
				if (si->has_method("_physics_process")) {
					set_physics_process(true);
				}
				if (si->has_method("_input")) {
					set_process_input(true);
				}
				if (si->has_method("_unhandled_input")) {
					set_process_unhandled_input(true);
				}
				_call_script("_ready");
			}
		} break;
		case NOTIFICATION_PROCESS: {
			Variant delta(get_process_delta_time());
			const Variant *args[1] = { &delta };
			_call_script("_process", args, 1);
		} break;
		case NOTIFICATION_PHYSICS_PROCESS: {
			Variant delta(get_physics_process_delta_time());
			const Variant *args[1] = { &delta };
			_call_script("_physics_process", args, 1);
		} break;
		case NOTIFICATION_PREDELETE: {
			if (parent) {
				parent->remove_child(this);
			}
			while (!children.empty()) {
				memdelete(children.back());
			}
		} break;
	}
}

// ---------------------------------------------------------------------------
// Arbre
// ---------------------------------------------------------------------------
void Node::set_name(const String &p_name) {
	String new_name = p_name;
	// Caractères réservés aux chemins.
	for (char &c : new_name) {
		if (c == '/' || c == ':' || c == '.' || c == '"' || c == '%' || c == '@') {
			c = '_';
		}
	}
	if (new_name.empty()) {
		new_name = get_class();
	}
	name = new_name;
	if (parent) {
		parent->_validate_child_name(this);
	}
}

void Node::_validate_child_name(Node *p_child) {
	// Garantit un nom unique parmi les frères : "Nom", "Nom2", "Nom3"...
	auto exists = [&](const String &n) {
		for (Node *c : children) {
			if (c != p_child && c->name == n) {
				return true;
			}
		}
		return false;
	};
	if (p_child->name.empty()) {
		p_child->name = p_child->get_class();
	}
	if (!exists(p_child->name)) {
		return;
	}
	String base = p_child->name;
	while (!base.empty() && base.back() >= '0' && base.back() <= '9') {
		base.pop_back();
	}
	if (base.empty()) {
		base = p_child->name;
	}
	for (int i = 2;; i++) {
		String candidate = base + itos(i);
		if (!exists(candidate)) {
			p_child->name = candidate;
			return;
		}
	}
}

void Node::add_child(Node *p_child) {
	ERR_FAIL_NULL(p_child);
	ERR_FAIL_COND_MSG(p_child == this, "add_child(): un nœud ne peut pas être son propre enfant.");
	ERR_FAIL_COND_MSG(p_child->parent, "add_child(): le nœud '" + p_child->name + "' a déjà un parent ('" + p_child->parent->name + "').");
	ERR_FAIL_COND_MSG(p_child->is_ancestor_of(this), "add_child(): cycle dans l'arbre.");

	_validate_child_name(p_child);
	p_child->parent = this;
	children.push_back(p_child);
	p_child->notification(NOTIFICATION_PARENTED);
	if (inside_tree) {
		p_child->_propagate_enter_tree();
		// Si le parent n'est pas encore prêt, _ready sera propagé par lui.
		if (ready_notified) {
			p_child->_propagate_ready();
		}
	}
	notification(NOTIFICATION_CHILD_ORDER_CHANGED);
}

void Node::remove_child(Node *p_child) {
	ERR_FAIL_NULL(p_child);
	auto it = std::find(children.begin(), children.end(), p_child);
	ERR_FAIL_COND_MSG(it == children.end(), "remove_child(): '" + p_child->name + "' n'est pas un enfant de '" + name + "'.");
	if (p_child->inside_tree) {
		p_child->_propagate_exit_tree();
	}
	it = std::find(children.begin(), children.end(), p_child);
	size_t index = it - children.begin();
	children.erase(it);
	p_child->parent = nullptr;
	p_child->notification(NOTIFICATION_UNPARENTED);
	for (size_t i = index; i < children.size(); i++) {
		children[i]->notification(NOTIFICATION_MOVED_IN_PARENT);
	}
	notification(NOTIFICATION_CHILD_ORDER_CHANGED);
}

void Node::move_child(Node *p_child, int p_index) {
	auto it = std::find(children.begin(), children.end(), p_child);
	ERR_FAIL_COND(it == children.end());
	if (p_index < 0) {
		p_index += (int)children.size();
	}
	p_index = Math::clamp(p_index, 0, (int)children.size() - 1);
	int from = (int)(it - children.begin());
	if (from == p_index) {
		return;
	}
	children.erase(it);
	children.insert(children.begin() + p_index, p_child);
	for (int i = std::min(from, p_index); i <= std::max(from, p_index); i++) {
		children[i]->notification(NOTIFICATION_MOVED_IN_PARENT);
	}
	notification(NOTIFICATION_CHILD_ORDER_CHANGED);
}

Node *Node::get_child(int p_index) const {
	if (p_index < 0) {
		p_index += (int)children.size();
	}
	ERR_FAIL_INDEX_V(p_index, children.size(), nullptr);
	return children[p_index];
}

Array Node::get_children() const {
	Array out;
	for (Node *c : children) {
		out.push_back(Variant(c));
	}
	return out;
}

int Node::get_index() const {
	if (!parent) {
		return 0;
	}
	auto it = std::find(parent->children.begin(), parent->children.end(), this);
	return (int)(it - parent->children.begin());
}

void Node::reparent(Node *p_new_parent) {
	ERR_FAIL_NULL(p_new_parent);
	if (parent == p_new_parent) {
		return;
	}
	if (parent) {
		parent->remove_child(this);
	}
	p_new_parent->add_child(this);
}

Node *Node::get_node_or_null(const String &p_path) const {
	if (p_path.empty()) {
		return nullptr;
	}
	const Node *current = this;
	String path = p_path;
	if (path[0] == '/') {
		if (!tree) {
			return nullptr;
		}
		current = tree->get_root();
		path = path.substr(1);
		// Le premier composant est le nom de la racine ("root").
		size_t slash = path.find('/');
		String first = path.substr(0, slash);
		if (first != current->name) {
			return nullptr;
		}
		path = slash == String::npos ? String() : path.substr(slash + 1);
	}
	for (const String &part : str_split(path, "/", false)) {
		if (part == ".") {
			continue;
		}
		if (part == "..") {
			current = current->parent;
		} else {
			const Node *next = nullptr;
			for (Node *c : current->children) {
				if (c->name == part) {
					next = c;
					break;
				}
			}
			current = next;
		}
		if (!current) {
			return nullptr;
		}
	}
	return const_cast<Node *>(current);
}

Node *Node::get_node(const String &p_path) const {
	Node *node = get_node_or_null(p_path);
	if (!node) {
		print_error("get_node(): nœud introuvable : \"" + p_path + "\" (relatif à \"" + get_path() + "\").");
	}
	return node;
}

bool Node::has_node(const String &p_path) const {
	return get_node_or_null(p_path) != nullptr;
}

Node *Node::find_child(const String &p_name, bool p_recursive) const {
	for (Node *c : children) {
		if (c->name == p_name) {
			return c;
		}
	}
	if (p_recursive) {
		for (Node *c : children) {
			if (Node *found = c->find_child(p_name, true)) {
				return found;
			}
		}
	}
	return nullptr;
}

String Node::get_path() const {
	if (!parent) {
		return inside_tree ? "/" + name : name;
	}
	String parent_path = parent->get_path();
	return parent_path + "/" + name;
}

String Node::get_path_to(const Node *p_node) const {
	if (p_node == this) {
		return ".";
	}
	// Ancêtre commun.
	std::vector<const Node *> a_chain, b_chain;
	for (const Node *n = this; n; n = n->parent) {
		a_chain.push_back(n);
	}
	for (const Node *n = p_node; n; n = n->parent) {
		b_chain.push_back(n);
	}
	std::reverse(a_chain.begin(), a_chain.end());
	std::reverse(b_chain.begin(), b_chain.end());
	if (a_chain.empty() || b_chain.empty() || a_chain[0] != b_chain[0]) {
		return String();
	}
	size_t common = 0;
	while (common < a_chain.size() && common < b_chain.size() && a_chain[common] == b_chain[common]) {
		common++;
	}
	std::vector<String> parts;
	for (size_t i = common; i < a_chain.size(); i++) {
		parts.push_back("..");
	}
	for (size_t i = common; i < b_chain.size(); i++) {
		parts.push_back(b_chain[i]->name);
	}
	return str_join(parts, "/");
}

bool Node::is_ancestor_of(const Node *p_node) const {
	for (const Node *n = p_node ? p_node->parent : nullptr; n; n = n->parent) {
		if (n == this) {
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
// Groupes
// ---------------------------------------------------------------------------
void Node::add_to_group(const String &p_group) {
	if (p_group.empty() || groups.count(p_group)) {
		return;
	}
	groups.insert(p_group);
	if (tree) {
		tree->_add_to_group(p_group, this);
	}
}

void Node::remove_from_group(const String &p_group) {
	if (!groups.count(p_group)) {
		return;
	}
	groups.erase(p_group);
	if (tree) {
		tree->_remove_from_group(p_group, this);
	}
}

Array Node::get_groups() const {
	Array out;
	for (const String &g : groups) {
		out.push_back(Variant(g));
	}
	return out;
}

// ---------------------------------------------------------------------------
// Entrée / sortie de l'arbre
// ---------------------------------------------------------------------------
void Node::_propagate_enter_tree() {
	tree = parent ? parent->tree : tree;
	inside_tree = true;
	for (const String &g : groups) {
		tree->_add_to_group(g, this);
	}
	notification(NOTIFICATION_ENTER_TREE);
	_call_script("_enter_tree");
	emit_signal("tree_entered");
	// Copie : un script peut modifier les enfants pendant l'entrée.
	std::vector<Node *> copy = children;
	for (Node *c : copy) {
		if (c->parent == this && !c->inside_tree) {
			c->_propagate_enter_tree();
		}
	}
}

void Node::_propagate_ready() {
	std::vector<Node *> copy = children;
	for (Node *c : copy) {
		if (c->parent == this && c->inside_tree) {
			c->_propagate_ready();
		}
	}
	if (!ready_notified && inside_tree) {
		ready_notified = true;
		notification(NOTIFICATION_READY);
		emit_signal("ready");
	}
}

void Node::_propagate_exit_tree() {
	std::vector<Node *> copy = children;
	for (auto it = copy.rbegin(); it != copy.rend(); ++it) {
		if ((*it)->parent == this && (*it)->inside_tree) {
			(*it)->_propagate_exit_tree();
		}
	}
	_call_script("_exit_tree");
	emit_signal("tree_exiting");
	notification(NOTIFICATION_EXIT_TREE, true);
	if (tree) {
		for (const String &g : groups) {
			tree->_remove_from_group(g, this);
		}
	}
	inside_tree = false;
	ready_notified = false;
	tree = nullptr;
}

bool Node::can_process() const {
	if (!tree) {
		return false;
	}
	ProcessMode mode = process_mode;
	const Node *n = this;
	while (mode == PROCESS_MODE_INHERIT && n->parent) {
		n = n->parent;
		mode = n->process_mode;
	}
	if (mode == PROCESS_MODE_INHERIT) {
		mode = PROCESS_MODE_PAUSABLE;
	}
	switch (mode) {
		case PROCESS_MODE_DISABLED: return false;
		case PROCESS_MODE_ALWAYS: return true;
		case PROCESS_MODE_WHEN_PAUSED: return tree->is_paused();
		default: return !tree->is_paused();
	}
}

double Node::get_process_delta_time() const {
	return tree ? tree->get_process_time() : 0.0;
}

double Node::get_physics_process_delta_time() const {
	return tree ? tree->get_physics_process_time() : 0.0;
}

void Node::propagate_notification(int p_what) {
	notification(p_what);
	std::vector<Node *> copy = children;
	for (Node *c : copy) {
		c->propagate_notification(p_what);
	}
}

void Node::queue_free() {
	SceneTree::queue_delete(this);
}

Node *Node::_duplicate(std::map<const Node *, Node *> &r_map) const {
	Node *copy = Object::cast_to<Node>(ClassDB::instantiate(get_class()));
	ERR_FAIL_NULL_V(copy, nullptr);
	copy->name = name;
	copy->scene_file_path = scene_file_path;
	copy->process_mode = process_mode;
	std::vector<PropertyInfo> props;
	get_property_list(&props);
	for (const PropertyInfo &pi : props) {
		if (pi.usage & PROPERTY_USAGE_STORAGE) {
			copy->set(pi.name, get(pi.name));
		}
	}
	for (const String &g : groups) {
		copy->add_to_group(g);
	}
	r_map[this] = copy;
	for (Node *c : children) {
		Node *cc = c->_duplicate(r_map);
		if (cc) {
			copy->add_child(cc);
		}
	}
	return copy;
}

Node *Node::duplicate() const {
	std::map<const Node *, Node *> map;
	Node *copy = _duplicate(map);
	// Reporte les propriétaires (owner) : internes -> copie correspondante, externes -> identiques.
	for (auto &kv : map) {
		if (kv.first == this) {
			kv.second->owner = owner;
			continue;
		}
		Node *orig_owner = kv.first->owner;
		auto it = map.find(orig_owner);
		kv.second->owner = it != map.end() ? it->second : orig_owner;
	}
	return copy;
}

void Node::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_name", "name"), &Node::set_name);
	ClassDB::bind_method(D_METHOD("get_name"), &Node::get_name);
	ClassDB::bind_method(D_METHOD("add_child", "node"), &Node::add_child);
	ClassDB::bind_method(D_METHOD("remove_child", "node"), &Node::remove_child);
	ClassDB::bind_method(D_METHOD("move_child", "node", "index"), &Node::move_child);
	ClassDB::bind_method(D_METHOD("get_child_count"), &Node::get_child_count);
	ClassDB::bind_method(D_METHOD("get_child", "index"), &Node::get_child);
	ClassDB::bind_method(D_METHOD("get_children"), &Node::get_children);
	ClassDB::bind_method(D_METHOD("get_parent"), &Node::get_parent);
	ClassDB::bind_method(D_METHOD("get_index"), &Node::get_index);
	ClassDB::bind_method(D_METHOD("reparent", "new_parent"), &Node::reparent);
	ClassDB::bind_method(D_METHOD("has_node", "path"), &Node::has_node);
	ClassDB::bind_method(D_METHOD("get_node", "path"), &Node::get_node);
	ClassDB::bind_method(D_METHOD("get_node_or_null", "path"), &Node::get_node_or_null);
	ClassDB::bind_method(D_METHOD("find_child", "name", "recursive"), &Node::find_child, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("get_path"), &Node::get_path);
	ClassDB::bind_method(D_METHOD("get_path_to", "node"), &Node::get_path_to);
	ClassDB::bind_method(D_METHOD("is_ancestor_of", "node"), &Node::is_ancestor_of);
	ClassDB::bind_method(D_METHOD("set_owner", "owner"), &Node::set_owner);
	ClassDB::bind_method(D_METHOD("get_owner"), &Node::get_owner);
	ClassDB::bind_method(D_METHOD("get_scene_file_path"), &Node::get_scene_file_path);
	ClassDB::bind_method(D_METHOD("add_to_group", "group"), &Node::add_to_group);
	ClassDB::bind_method(D_METHOD("remove_from_group", "group"), &Node::remove_from_group);
	ClassDB::bind_method(D_METHOD("is_in_group", "group"), &Node::is_in_group);
	ClassDB::bind_method(D_METHOD("get_groups"), &Node::get_groups);
	ClassDB::bind_method(D_METHOD("get_tree"), &Node::get_tree);
	ClassDB::bind_method(D_METHOD("is_inside_tree"), &Node::is_inside_tree);
	ClassDB::bind_method(D_METHOD("is_node_ready"), &Node::is_node_ready);
	ClassDB::bind_method(D_METHOD("set_process", "enable"), &Node::set_process);
	ClassDB::bind_method(D_METHOD("is_processing"), &Node::is_processing);
	ClassDB::bind_method(D_METHOD("set_physics_process", "enable"), &Node::set_physics_process);
	ClassDB::bind_method(D_METHOD("is_physics_processing"), &Node::is_physics_processing);
	ClassDB::bind_method(D_METHOD("set_process_input", "enable"), &Node::set_process_input);
	ClassDB::bind_method(D_METHOD("set_process_unhandled_input", "enable"), &Node::set_process_unhandled_input);
	ClassDB::bind_method(D_METHOD("set_process_mode", "mode"), &Node::set_process_mode);
	ClassDB::bind_method(D_METHOD("get_process_mode"), &Node::get_process_mode);
	ClassDB::bind_method(D_METHOD("can_process"), &Node::can_process);
	ClassDB::bind_method(D_METHOD("get_process_delta_time"), &Node::get_process_delta_time);
	ClassDB::bind_method(D_METHOD("get_physics_process_delta_time"), &Node::get_physics_process_delta_time);
	ClassDB::bind_method(D_METHOD("propagate_notification", "what"), &Node::propagate_notification);
	ClassDB::bind_method(D_METHOD("queue_free"), &Node::queue_free);
	ClassDB::bind_method(D_METHOD("duplicate"), &Node::duplicate);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "name", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_name", "get_name");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "owner", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_owner", "get_owner");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "process_mode", PROPERTY_HINT_ENUM, "Inherit,Pausable,When Paused,Always,Disabled"), "set_process_mode", "get_process_mode");

	ADD_SIGNAL(MethodInfo("ready"));
	ADD_SIGNAL(MethodInfo("tree_entered"));
	ADD_SIGNAL(MethodInfo("tree_exiting"));

	BIND_CONSTANT(NOTIFICATION_ENTER_TREE);
	BIND_CONSTANT(NOTIFICATION_EXIT_TREE);
	BIND_CONSTANT(NOTIFICATION_READY);
	BIND_CONSTANT(NOTIFICATION_PROCESS);
	BIND_CONSTANT(NOTIFICATION_PHYSICS_PROCESS);
	BIND_ENUM_CONSTANT(PROCESS_MODE_INHERIT);
	BIND_ENUM_CONSTANT(PROCESS_MODE_PAUSABLE);
	BIND_ENUM_CONSTANT(PROCESS_MODE_WHEN_PAUSED);
	BIND_ENUM_CONSTANT(PROCESS_MODE_ALWAYS);
	BIND_ENUM_CONSTANT(PROCESS_MODE_DISABLED);
}
