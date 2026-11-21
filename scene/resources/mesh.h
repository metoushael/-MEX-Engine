// Maillages 3D : primitives paramétrables (boîte, sphère, cylindre, capsule, plan, tore)
// et ArrayMesh pour la géométrie importée (fichiers Wavefront .obj).
#pragma once

#include "core/io/resource_loader.h"
#include "servers/rendering_server_3d.h"

class Mesh : public Resource {
	GDCLASS(Mesh, Resource)

	RID mesh;

protected:
	std::vector<RenderingServer3D::Vertex> vertices;
	std::vector<uint32_t> indices;
	AABB aabb;

	static void _bind_methods();
	// Envoie les tableaux au serveur de rendu. p_fix_winding : oriente chaque triangle
	// selon les normales des sommets (faces avant en sens antihoraire).
	void _commit(bool p_fix_winding);

public:
	RID get_rid() const { return mesh; }
	AABB get_aabb() const { return aabb; }
	Vector3 get_aabb_size() const { return aabb.size; }
	int get_vertex_count() const { return (int)vertices.size(); }
	int get_triangle_count() const { return (int)indices.size() / 3; }
	const std::vector<RenderingServer3D::Vertex> &get_vertices() const { return vertices; }
	const std::vector<uint32_t> &get_indices() const { return indices; }
	// Intersection avec un rayon exprimé dans le repère du maillage.
	bool intersect_ray(const Vector3 &p_from, const Vector3 &p_dir, real_t *r_distance = nullptr) const;

	Mesh();
	~Mesh();
};

class PrimitiveMesh : public Mesh {
	GDCLASS(PrimitiveMesh, Mesh)

	bool dirty = true;

protected:
	static void _bind_methods() {}
	virtual void _generate() = 0;
	void _request_update();
	void _add_quad(const Vector3 &a, const Vector3 &b, const Vector3 &c, const Vector3 &d, const Vector3 &p_normal);

public:
	void update_now(); // régénère si nécessaire
};

class BoxMesh : public PrimitiveMesh {
	GDCLASS(BoxMesh, PrimitiveMesh)

	Vector3 size = Vector3(1, 1, 1);

protected:
	static void _bind_methods();
	virtual void _generate() override;

public:
	void set_size(const Vector3 &p_size);
	Vector3 get_size() const { return size; }
	BoxMesh() { update_now(); }
};

class SphereMesh : public PrimitiveMesh {
	GDCLASS(SphereMesh, PrimitiveMesh)

	real_t radius = 0.5f;
	real_t height = 1.0f;
	int radial_segments = 32;
	int rings = 16;

protected:
	static void _bind_methods();
	virtual void _generate() override;

public:
	void set_radius(real_t p_radius);
	real_t get_radius() const { return radius; }
	void set_height(real_t p_height);
	real_t get_height() const { return height; }
	void set_radial_segments(int p_segments);
	int get_radial_segments() const { return radial_segments; }
	void set_rings(int p_rings);
	int get_rings() const { return rings; }
	SphereMesh() { update_now(); }
};

class CylinderMesh : public PrimitiveMesh {
	GDCLASS(CylinderMesh, PrimitiveMesh)

	real_t top_radius = 0.5f;
	real_t bottom_radius = 0.5f;
	real_t height = 2.0f;
	int radial_segments = 32;

protected:
	static void _bind_methods();
	virtual void _generate() override;

public:
	void set_top_radius(real_t p_radius);
	real_t get_top_radius() const { return top_radius; }
	void set_bottom_radius(real_t p_radius);
	real_t get_bottom_radius() const { return bottom_radius; }
	void set_height(real_t p_height);
	real_t get_height() const { return height; }
	void set_radial_segments(int p_segments);
	int get_radial_segments() const { return radial_segments; }
	CylinderMesh() { update_now(); }
};

class CapsuleMesh : public PrimitiveMesh {
	GDCLASS(CapsuleMesh, PrimitiveMesh)

	real_t radius = 0.5f;
	real_t height = 2.0f;
	int radial_segments = 32;
	int rings = 8;

protected:
	static void _bind_methods();
	virtual void _generate() override;

public:
	void set_radius(real_t p_radius);
	real_t get_radius() const { return radius; }
	void set_height(real_t p_height);
	real_t get_height() const { return height; }
	void set_radial_segments(int p_segments);
	int get_radial_segments() const { return radial_segments; }
	CapsuleMesh() { update_now(); }
};

class PlaneMesh : public PrimitiveMesh {
	GDCLASS(PlaneMesh, PrimitiveMesh)

	Vector2 size = Vector2(2, 2);

protected:
	static void _bind_methods();
	virtual void _generate() override;

public:
	void set_size(const Vector2 &p_size);
	Vector2 get_size() const { return size; }
	PlaneMesh() { update_now(); }
};

class TorusMesh : public PrimitiveMesh {
	GDCLASS(TorusMesh, PrimitiveMesh)

	real_t inner_radius = 0.5f;
	real_t outer_radius = 1.0f;
	int rings = 48;
	int ring_segments = 24;

protected:
	static void _bind_methods();
	virtual void _generate() override;

public:
	void set_inner_radius(real_t p_radius);
	real_t get_inner_radius() const { return inner_radius; }
	void set_outer_radius(real_t p_radius);
	real_t get_outer_radius() const { return outer_radius; }
	TorusMesh() { update_now(); }
};

// Maillage quelconque, chargé depuis un fichier .obj ou construit par script.
class ArrayMesh : public Mesh {
	GDCLASS(ArrayMesh, Mesh)

protected:
	static void _bind_methods();

public:
	void set_data(const std::vector<RenderingServer3D::Vertex> &p_vertices, const std::vector<uint32_t> &p_indices);
	// Construction depuis un script : tableaux de Vector3 (trois sommets par triangle),
	// normales et coordonnées UV facultatives.
	void set_triangles(const Array &p_positions, const Array &p_normals, const Array &p_uvs);
	Error load_obj(const String &p_text);
};

class ResourceFormatLoaderOBJ : public ResourceFormatLoader {
public:
	virtual void get_recognized_extensions(std::vector<String> *p_extensions) const override { p_extensions->push_back("obj"); }
	virtual String get_resource_type(const String &p_path) const override { return "ArrayMesh"; }
	virtual Ref<Resource> load(const String &p_path, Error *r_error) override;
};
