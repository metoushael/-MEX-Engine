#include "skeleton_3d.h"

#include "scene/main/scene_tree.h"

#include <cmath>

void Skeleton3D::_mark_dirty() {
	poses_dirty = true;
	pose_version++;
}

void Skeleton3D::set_source(const String &p_path) {
	source = p_path;
	stop();
	bones.clear();
	clips.clear();
	_mark_dirty();
	if (p_path.empty()) {
		return;
	}
	GLTFData data;
	String message;
	if (gltf_load(p_path, data, &message) != OK) {
		print_error("Skeleton3D : " + p_path + " illisible (" + message + ").");
		return;
	}
	for (const GLTFData::Bone &b : data.bones) {
		Bone bone;
		bone.name = b.name;
		bone.parent = b.parent;
		bone.rest_position = bone.position = b.position;
		bone.rest_rotation = bone.rotation = b.rotation;
		bone.rest_scale = bone.scale = b.scale;
		bones.push_back(bone);
	}
	clips = data.clips;
}

int Skeleton3D::add_bone(const String &p_name) {
	Bone bone;
	bone.name = p_name;
	bones.push_back(bone);
	_mark_dirty();
	return (int)bones.size() - 1;
}

void Skeleton3D::clear_bones() {
	bones.clear();
	_mark_dirty();
}

int Skeleton3D::find_bone(const String &p_name) const {
	for (size_t i = 0; i < bones.size(); i++) {
		if (bones[i].name == p_name) {
			return (int)i;
		}
	}
	return -1;
}

void Skeleton3D::set_bone_parent(int p_bone, int p_parent) {
	if (!_valid(p_bone) || p_parent == p_bone || p_parent >= (int)bones.size()) {
		return;
	}
	// Refuse une boucle : le parent ne doit pas descendre de l'os.
	for (int at = p_parent, guard = 0; at >= 0 && guard < (int)bones.size(); at = bones[at].parent, guard++) {
		if (at == p_bone) {
			print_error("Skeleton3D : un os ne peut pas descendre de lui-même.");
			return;
		}
	}
	bones[p_bone].parent = p_parent < 0 ? -1 : p_parent;
	_mark_dirty();
}

void Skeleton3D::set_bone_rest(int p_bone, const Vector3 &p_position, const Vector3 &p_rotation, const Vector3 &p_scale) {
	if (!_valid(p_bone)) {
		return;
	}
	Bone &bone = bones[p_bone];
	bone.rest_position = bone.position = p_position;
	bone.rest_rotation = bone.rotation = Quaternion::from_euler(p_rotation);
	bone.rest_scale = bone.scale = p_scale;
	_mark_dirty();
}

void Skeleton3D::set_bone_pose_position(int p_bone, const Vector3 &p_position) {
	if (_valid(p_bone)) {
		bones[p_bone].position = p_position;
		_mark_dirty();
	}
}

void Skeleton3D::set_bone_pose_rotation(int p_bone, const Vector3 &p_euler) {
	set_bone_pose_quaternion(p_bone, Quaternion::from_euler(p_euler));
}

void Skeleton3D::set_bone_pose_quaternion(int p_bone, const Quaternion &p_rotation) {
	if (_valid(p_bone)) {
		bones[p_bone].rotation = p_rotation;
		_mark_dirty();
	}
}

void Skeleton3D::set_bone_pose_scale(int p_bone, const Vector3 &p_scale) {
	if (_valid(p_bone)) {
		bones[p_bone].scale = p_scale;
		_mark_dirty();
	}
}

void Skeleton3D::reset_bone_pose(int p_bone) {
	if (_valid(p_bone)) {
		Bone &bone = bones[p_bone];
		bone.position = bone.rest_position;
		bone.rotation = bone.rest_rotation;
		bone.scale = bone.rest_scale;
		_mark_dirty();
	}
}

void Skeleton3D::reset_bone_poses() {
	for (int i = 0; i < (int)bones.size(); i++) {
		reset_bone_pose(i);
	}
}

const std::vector<Transform3D> &Skeleton3D::get_bone_global_poses() const {
	if (!poses_dirty && global_poses.size() == bones.size()) {
		return global_poses;
	}
	const size_t count = bones.size();
	global_poses.assign(count, Transform3D());
	// Un parent peut être rangé après son enfant : on résout dans l'ordre de la hiérarchie.
	std::vector<char> done(count, 0);
	for (size_t i = 0; i < count; i++) {
		int chain[65];
		int length = 0;
		for (int at = (int)i; at >= 0 && at < (int)count && !done[at] && length < 65; at = bones[at].parent) {
			chain[length++] = at;
		}
		while (length > 0) {
			const int at = chain[--length];
			const Bone &bone = bones[at];
			const Transform3D local(bone.rotation.to_basis().scaled_local(bone.scale), bone.position);
			const bool has_parent = bone.parent >= 0 && bone.parent < (int)count && done[bone.parent];
			global_poses[at] = has_parent ? global_poses[bone.parent] * local : local;
			done[at] = 1;
		}
	}
	poses_dirty = false;
	return global_poses;
}

Vector3 Skeleton3D::get_bone_global_position(int p_bone) const {
	return _valid(p_bone) ? get_bone_global_poses()[p_bone].origin : Vector3();
}

// --- Animations ---
const GLTFData::Clip *Skeleton3D::_find_clip(const String &p_name) const {
	for (const GLTFData::Clip &clip : clips) {
		if (clip.name == p_name) {
			return &clip;
		}
	}
	return nullptr;
}

Array Skeleton3D::get_animation_list() const {
	Array list;
	for (const GLTFData::Clip &clip : clips) {
		list.push_back(Variant(clip.name));
	}
	return list;
}

real_t Skeleton3D::get_animation_length(const String &p_name) const {
	const GLTFData::Clip *clip = _find_clip(p_name);
	return clip ? clip->length : 0;
}

void Skeleton3D::_apply_clip(const GLTFData::Clip &p_clip, double p_time) {
	const float t = (float)p_time;
	for (const GLTFData::Track &track : p_clip.tracks) {
		if (!_valid(track.bone) || track.times.empty()) {
			continue;
		}
		const int stride = track.path == 1 ? 4 : 3;
		// Clés qui encadrent l'instant demandé.
		size_t next = 0;
		while (next < track.times.size() && track.times[next] <= t) {
			next++;
		}
		const size_t previous = next == 0 ? 0 : next - 1;
		if (next >= track.times.size()) {
			next = track.times.size() - 1;
		}
		float weight = 0;
		if (next != previous && !track.step) {
			const float span = track.times[next] - track.times[previous];
			weight = span > 0 ? (t - track.times[previous]) / span : 0;
		}
		const float *a = &track.values[previous * stride];
		const float *b = &track.values[next * stride];
		Bone &bone = bones[track.bone];
		if (track.path == 1) {
			bone.rotation = Quaternion(a[0], a[1], a[2], a[3]).slerp(Quaternion(b[0], b[1], b[2], b[3]), weight).normalized();
		} else {
			const Vector3 value = Vector3(a[0], a[1], a[2]) * (1.0f - weight) + Vector3(b[0], b[1], b[2]) * weight;
			if (track.path == 0) {
				bone.position = value;
			} else {
				bone.scale = value;
			}
		}
	}
	_mark_dirty();
}

void Skeleton3D::play(const String &p_name) {
	const String name = p_name.empty() ? (current.empty() && !clips.empty() ? clips[0].name : current) : p_name;
	const GLTFData::Clip *clip = _find_clip(name);
	if (!clip) {
		print_error("Skeleton3D : animation « " + name + " » introuvable.");
		return;
	}
	current = name;
	time = 0.0;
	playing = true;
	_apply_clip(*clip, 0.0);
	set_process_internal(true);
}

void Skeleton3D::stop() {
	playing = false;
	set_process_internal(false);
}

void Skeleton3D::seek(double p_time) {
	const GLTFData::Clip *clip = _find_clip(current);
	if (clip) {
		time = std::min(std::max(p_time, 0.0), (double)clip->length);
		_apply_clip(*clip, time);
	}
}

void Skeleton3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (!autoplay.empty() && !get_tree()->is_edit_mode()) {
				play(autoplay);
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			const GLTFData::Clip *clip = playing ? _find_clip(current) : nullptr;
			if (!clip) {
				stop();
				break;
			}
			time += get_process_delta_time() * speed_scale;
			bool finished = false;
			if (clip->length <= 0) {
				time = 0;
			} else if (loop) {
				time = std::fmod(time, (double)clip->length);
				if (time < 0) {
					time += clip->length;
				}
			} else if (time >= clip->length || time < 0) {
				time = time < 0 ? 0.0 : (double)clip->length;
				finished = true;
			}
			_apply_clip(*clip, time);
			if (finished) {
				stop();
				emit_signal("animation_finished", current);
			}
		} break;
	}
}

void Skeleton3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_source", "path"), &Skeleton3D::set_source);
	ClassDB::bind_method(D_METHOD("get_source"), &Skeleton3D::get_source);
	ClassDB::bind_method(D_METHOD("add_bone", "name"), &Skeleton3D::add_bone);
	ClassDB::bind_method(D_METHOD("clear_bones"), &Skeleton3D::clear_bones);
	ClassDB::bind_method(D_METHOD("get_bone_count"), &Skeleton3D::get_bone_count);
	ClassDB::bind_method(D_METHOD("find_bone", "name"), &Skeleton3D::find_bone);
	ClassDB::bind_method(D_METHOD("get_bone_name", "bone"), &Skeleton3D::get_bone_name);
	ClassDB::bind_method(D_METHOD("get_bone_parent", "bone"), &Skeleton3D::get_bone_parent);
	ClassDB::bind_method(D_METHOD("set_bone_parent", "bone", "parent"), &Skeleton3D::set_bone_parent);
	ClassDB::bind_method(D_METHOD("set_bone_rest", "bone", "position", "rotation", "scale"), &Skeleton3D::set_bone_rest);
	ClassDB::bind_method(D_METHOD("set_bone_pose_position", "bone", "position"), &Skeleton3D::set_bone_pose_position);
	ClassDB::bind_method(D_METHOD("get_bone_pose_position", "bone"), &Skeleton3D::get_bone_pose_position);
	ClassDB::bind_method(D_METHOD("set_bone_pose_rotation", "bone", "euler"), &Skeleton3D::set_bone_pose_rotation);
	ClassDB::bind_method(D_METHOD("get_bone_pose_rotation", "bone"), &Skeleton3D::get_bone_pose_rotation);
	ClassDB::bind_method(D_METHOD("set_bone_pose_scale", "bone", "scale"), &Skeleton3D::set_bone_pose_scale);
	ClassDB::bind_method(D_METHOD("get_bone_pose_scale", "bone"), &Skeleton3D::get_bone_pose_scale);
	ClassDB::bind_method(D_METHOD("reset_bone_pose", "bone"), &Skeleton3D::reset_bone_pose);
	ClassDB::bind_method(D_METHOD("reset_bone_poses"), &Skeleton3D::reset_bone_poses);
	ClassDB::bind_method(D_METHOD("get_bone_global_position", "bone"), &Skeleton3D::get_bone_global_position);
	ClassDB::bind_method(D_METHOD("get_animation_list"), &Skeleton3D::get_animation_list);
	ClassDB::bind_method(D_METHOD("has_animation", "name"), &Skeleton3D::has_animation);
	ClassDB::bind_method(D_METHOD("get_animation_length", "name"), &Skeleton3D::get_animation_length);
	ClassDB::bind_method(D_METHOD("play", "name"), &Skeleton3D::play, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("stop"), &Skeleton3D::stop);
	ClassDB::bind_method(D_METHOD("is_playing"), &Skeleton3D::is_playing);
	ClassDB::bind_method(D_METHOD("get_current_animation"), &Skeleton3D::get_current_animation);
	ClassDB::bind_method(D_METHOD("seek", "time"), &Skeleton3D::seek);
	ClassDB::bind_method(D_METHOD("get_current_animation_position"), &Skeleton3D::get_current_animation_position);
	ClassDB::bind_method(D_METHOD("set_autoplay", "name"), &Skeleton3D::set_autoplay);
	ClassDB::bind_method(D_METHOD("get_autoplay"), &Skeleton3D::get_autoplay);
	ClassDB::bind_method(D_METHOD("set_loop", "loop"), &Skeleton3D::set_loop);
	ClassDB::bind_method(D_METHOD("has_loop"), &Skeleton3D::has_loop);
	ClassDB::bind_method(D_METHOD("set_speed_scale", "scale"), &Skeleton3D::set_speed_scale);
	ClassDB::bind_method(D_METHOD("get_speed_scale"), &Skeleton3D::get_speed_scale);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "source", PROPERTY_HINT_FILE, "glb,gltf"), "set_source", "get_source");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "autoplay"), "set_autoplay", "get_autoplay");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "loop"), "set_loop", "has_loop");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed_scale", PROPERTY_HINT_RANGE, "-4,4,0.01"), "set_speed_scale", "get_speed_scale");

	ADD_SIGNAL(MethodInfo("animation_finished", PropertyInfo(Variant::STRING, "name")));
}
