// Squelette : une hiérarchie d'os qui déforme les MeshInstance3D enfants dont le maillage a une
// peau (fichiers .glb / .gltf), avec la lecture des animations du fichier.
//
// Inspiré de scene/3d/skeleton_3d.{h,cpp} du moteur Godot 4.3 (licence MIT, voir
// thirdparty/godot/LICENSE.txt) : os avec parent, pose de repos et pose courante (position,
// rotation, échelle), poses globales calculées à la demande. Différences avec Godot : les
// animations du fichier sont jouées par le squelette lui-même (Godot crée un AnimationPlayer),
// les rotations se règlent par script en angles d'Euler (MexScript n'a pas de type quaternion),
// la peau est calculée sur le processeur, et il n'y a ni cinématique inverse, ni
// BoneAttachment3D, ni mélange d'animations (AnimationTree).
#pragma once

#include "core/math/quaternion.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/gltf_loader.h"

class Skeleton3D : public Node3D {
	GDCLASS(Skeleton3D, Node3D)

	struct Bone {
		String name;
		int parent = -1;
		Vector3 rest_position, rest_scale = Vector3(1, 1, 1);
		Quaternion rest_rotation;
		Vector3 position, scale = Vector3(1, 1, 1);
		Quaternion rotation;
	};

	std::vector<Bone> bones;
	std::vector<GLTFData::Clip> clips;
	String source;
	String autoplay;
	String current;
	bool playing = false;
	bool loop = true;
	real_t speed_scale = 1.0f;
	double time = 0.0;

	mutable std::vector<Transform3D> global_poses;
	mutable bool poses_dirty = true;
	uint64_t pose_version = 1;

	void _mark_dirty();
	void _apply_clip(const GLTFData::Clip &p_clip, double p_time);
	const GLTFData::Clip *_find_clip(const String &p_name) const;
	bool _valid(int p_bone) const { return p_bone >= 0 && p_bone < (int)bones.size(); }

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	// Fichier .glb / .gltf d'où viennent les os et les animations.
	void set_source(const String &p_path);
	String get_source() const { return source; }

	// Os
	int add_bone(const String &p_name);
	void clear_bones();
	int get_bone_count() const { return (int)bones.size(); }
	int find_bone(const String &p_name) const;
	String get_bone_name(int p_bone) const { return _valid(p_bone) ? bones[p_bone].name : String(); }
	int get_bone_parent(int p_bone) const { return _valid(p_bone) ? bones[p_bone].parent : -1; }
	void set_bone_parent(int p_bone, int p_parent);
	void set_bone_rest(int p_bone, const Vector3 &p_position, const Vector3 &p_rotation, const Vector3 &p_scale);
	void set_bone_pose_position(int p_bone, const Vector3 &p_position);
	Vector3 get_bone_pose_position(int p_bone) const { return _valid(p_bone) ? bones[p_bone].position : Vector3(); }
	void set_bone_pose_rotation(int p_bone, const Vector3 &p_euler); // radians, ordre YXZ
	Vector3 get_bone_pose_rotation(int p_bone) const { return _valid(p_bone) ? bones[p_bone].rotation.get_euler() : Vector3(); }
	void set_bone_pose_scale(int p_bone, const Vector3 &p_scale);
	Vector3 get_bone_pose_scale(int p_bone) const { return _valid(p_bone) ? bones[p_bone].scale : Vector3(1, 1, 1); }
	void set_bone_pose_quaternion(int p_bone, const Quaternion &p_rotation);
	void reset_bone_pose(int p_bone);
	void reset_bone_poses();
	// Poses dans le repère du squelette.
	const std::vector<Transform3D> &get_bone_global_poses() const;
	Vector3 get_bone_global_position(int p_bone) const;
	uint64_t get_pose_version() const { return pose_version; }

	// Animations
	Array get_animation_list() const;
	bool has_animation(const String &p_name) const { return _find_clip(p_name) != nullptr; }
	real_t get_animation_length(const String &p_name) const;
	void play(const String &p_name = String());
	void stop();
	bool is_playing() const { return playing; }
	String get_current_animation() const { return current; }
	void seek(double p_time);
	double get_current_animation_position() const { return time; }
	void set_autoplay(const String &p_name) { autoplay = p_name; }
	String get_autoplay() const { return autoplay; }
	void set_loop(bool p_loop) { loop = p_loop; }
	bool has_loop() const { return loop; }
	void set_speed_scale(real_t p_scale) { speed_scale = p_scale; }
	real_t get_speed_scale() const { return speed_scale; }
};
