// PackedScene : scène sérialisée (format texte .scn) + lecture/écriture des ressources (.res).
#pragma once

#include "core/io/resource_loader.h"
#include "scene/main/node.h"

class PackedScene : public Resource {
	GDCLASS(PackedScene, Resource)

public:
	struct NodeData {
		String name;
		String type;
		String parent; // "" pour la racine, "." pour ses enfants directs
		Ref<PackedScene> instance; // sous-scène instanciée
		std::vector<String> groups;
		std::vector<std::pair<String, Variant>> properties;
	};

	struct ConnectionData {
		String signal;
		String from;
		String to;
		String method;
	};

private:
	std::vector<NodeData> nodes;
	std::vector<ConnectionData> connections;

	void _pack_node(Node *p_root, Node *p_node);

protected:
	static void _bind_methods();

public:
	Error pack(Node *p_root);
	Node *instantiate() const;
	bool can_instantiate() const { return !nodes.empty(); }
	String get_root_type() const;

	Error load_from_text(const String &p_text, const String &p_path);
	String save_to_text() const;

	const std::vector<NodeData> &get_node_data() const { return nodes; }
};

class ResourceSaver {
public:
	// Scènes (.scn) et ressources génériques (.res).
	static Error save(const String &p_path, const Ref<Resource> &p_resource);
	static String resource_to_text(const Ref<Resource> &p_resource);
};

class ResourceFormatLoaderScene : public ResourceFormatLoader {
public:
	virtual void get_recognized_extensions(std::vector<String> *p_extensions) const override { p_extensions->push_back("scn"); }
	virtual String get_resource_type(const String &p_path) const override { return "PackedScene"; }
	virtual Ref<Resource> load(const String &p_path, Error *r_error) override;
};

class ResourceFormatLoaderText : public ResourceFormatLoader {
public:
	virtual void get_recognized_extensions(std::vector<String> *p_extensions) const override { p_extensions->push_back("res"); }
	virtual String get_resource_type(const String &p_path) const override;
	virtual Ref<Resource> load(const String &p_path, Error *r_error) override;
};
