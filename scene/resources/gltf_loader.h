// Import des modèles glTF 2.0 (.glb et .gltf) sous forme de maillage (ArrayMesh).
//
// Écrit pour MEX d'après la spécification glTF 2.0 de Khronos ; Godot fait ce travail dans
// modules/gltf (GLTFDocument), bien plus complet. Ici : géométrie en triangles de toutes les
// mailles de la scène, transformations des nœuds appliquées, normales, coordonnées UV, couleurs
// de sommet multipliées par la couleur de base du matériau, peaux (os et poids par sommet) et
// animations des nœuds (position, rotation, échelle). Non repris : textures, formes de mélange,
// caméras, lumières, accesseurs « sparse », extensions (Draco, etc.).
#pragma once

#include "core/io/resource_loader.h"
#include "core/math/quaternion.h"
#include "scene/resources/mesh.h"

// Contenu d'un fichier glTF : le maillage dans sa pose de repos, sa peau, et de quoi construire
// un Skeleton3D (un os par nœud du fichier) avec ses animations.
struct GLTFData {
	struct Bone {
		String name;
		int parent = -1;
		Vector3 position;
		Quaternion rotation;
		Vector3 scale = Vector3(1, 1, 1);
	};
	struct Track {
		int bone = -1;
		int path = 0; // 0 position, 1 rotation, 2 échelle
		bool step = false; // sans interpolation
		std::vector<float> times;
		std::vector<float> values; // 3 nombres par clé (4 pour une rotation)
	};
	struct Clip {
		String name;
		float length = 0;
		std::vector<Track> tracks;
	};
	std::vector<RenderingServer3D::Vertex> vertices;
	std::vector<uint32_t> indices;
	std::vector<SkinVertex> skin;
	std::vector<Bone> bones;
	std::vector<Clip> clips;
	bool has_skins = false;
};

Error gltf_load(const String &p_path, GLTFData &r_data, String *r_error = nullptr);

class ResourceFormatLoaderGLTF : public ResourceFormatLoader {
public:
	virtual void get_recognized_extensions(std::vector<String> *p_extensions) const override {
		p_extensions->push_back("glb");
		p_extensions->push_back("gltf");
	}
	virtual String get_resource_type(const String &p_path) const override { return "ArrayMesh"; }
	virtual Ref<Resource> load(const String &p_path, Error *r_error) override;
};
