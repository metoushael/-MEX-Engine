// Node : brique de base de l'arbre de scène.
#pragma once

#include "core/input/input_event.h"
#include "core/object/object.h"

class SceneTree;

class Node : public Object {
	GDCLASS(Node, Object)

public:
	enum {
		NOTIFICATION_ENTER_TREE = 10,
		NOTIFICATION_EXIT_TREE = 11,
		NOTIFICATION_MOVED_IN_PARENT = 12,
		NOTIFICATION_READY = 13,
		NOTIFICATION_PAUSED = 14,
		NOTIFICATION_UNPAUSED = 15,
		NOTIFICATION_PHYSICS_PROCESS = 16,
		NOTIFICATION_PROCESS = 17,
		NOTIFICATION_PARENTED = 18,
		NOTIFICATION_UNPARENTED = 19,
		NOTIFICATION_CHILD_ORDER_CHANGED = 24,
		NOTIFICATION_INTERNAL_PROCESS = 25,
		NOTIFICATION_INTERNAL_PHYSICS_PROCESS = 26,
	};

	enum ProcessMode {
		PROCESS_MODE_INHERIT,
		PROCESS_MODE_PAUSABLE,
		PROCESS_MODE_WHEN_PAUSED,
		PROCESS_MODE_ALWAYS,
		PROCESS_MODE_DISABLED,
	};

private:
	friend class SceneTree;

	String name;
	Node *parent = nullptr;
	Node *owner = nullptr;
	std::vector<Node *> children;
	SceneTree *tree = nullptr;
	std::set<String> groups;
	String scene_file_path;
	ProcessMode process_mode = PROCESS_MODE_INHERIT;

	bool inside_tree = false;
	bool ready_notified = false;
	bool process = false;
	bool physics_process = false;
	bool process_internal = false;
	bool physics_process_internal = false;
	bool process_input = false;
	bool process_unhandled_input = false;

	void _propagate_enter_tree();
	void _propagate_ready();
	void _propagate_exit_tree();
	void _validate_child_name(Node *p_child);
	Node *_duplicate(std::map<const Node *, Node *> &r_map) const;
	void _call_input(const Ref<InputEvent> &p_event);
	void _call_unhandled_input(const Ref<InputEvent> &p_event);

protected:
	void _notification(int p_what);
	static void _bind_methods();
	Variant _call_script(const String &p_method, const Variant **p_args = nullptr, int p_argc = 0);

	// Crochet C++ pour les nœuds qui traitent les entrées (Button...).
	virtual void input(const Ref<InputEvent> &p_event) {}

public:
	void set_name(const String &p_name);
	String get_name() const { return name; }
	virtual String to_string() const override;

	void add_child(Node *p_child);
	void remove_child(Node *p_child);
	void move_child(Node *p_child, int p_index);
	int get_child_count() const { return (int)children.size(); }
	Node *get_child(int p_index) const;
	Array get_children() const;
	const std::vector<Node *> &get_children_vector() const { return children; }
	Node *get_parent() const { return parent; }
	int get_index() const;
	void reparent(Node *p_new_parent);

	bool has_node(const String &p_path) const;
	Node *get_node(const String &p_path) const; // erreur si introuvable
	Node *get_node_or_null(const String &p_path) const;
	Node *find_child(const String &p_name, bool p_recursive = true) const;
	String get_path() const;
	String get_path_to(const Node *p_node) const;
	bool is_ancestor_of(const Node *p_node) const;

	void set_owner(Node *p_owner) { owner = p_owner; }
	Node *get_owner() const { return owner; }
	void set_scene_file_path(const String &p_path) { scene_file_path = p_path; }
	String get_scene_file_path() const { return scene_file_path; }

	void add_to_group(const String &p_group);
	void remove_from_group(const String &p_group);
	bool is_in_group(const String &p_group) const { return groups.count(p_group) > 0; }
	Array get_groups() const;
	const std::set<String> &get_groups_set() const { return groups; }

	SceneTree *get_tree() const { return tree; }
	bool is_inside_tree() const { return inside_tree; }
	bool is_node_ready() const { return ready_notified; }

	void set_process(bool p_enable) { process = p_enable; }
	bool is_processing() const { return process; }
	void set_physics_process(bool p_enable) { physics_process = p_enable; }
	bool is_physics_processing() const { return physics_process; }
	void set_process_internal(bool p_enable) { process_internal = p_enable; }
	void set_physics_process_internal(bool p_enable) { physics_process_internal = p_enable; }
	void set_process_input(bool p_enable) { process_input = p_enable; }
	bool is_processing_input() const { return process_input; }
	void set_process_unhandled_input(bool p_enable) { process_unhandled_input = p_enable; }
	void set_process_mode(ProcessMode p_mode) { process_mode = p_mode; }
	ProcessMode get_process_mode() const { return process_mode; }
	bool can_process() const;
	double get_process_delta_time() const;
	double get_physics_process_delta_time() const;

	void propagate_notification(int p_what);
	void queue_free();
	Node *duplicate() const;

	Node();
	~Node();
};
