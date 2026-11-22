#include "visual_3d.h"

#include "scene/3d/skeleton_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/shape_3d.h"

#define RS3D RenderingServer3D::get_singleton()

typedef RenderingServer3D::Line Line;

// ---------------------------------------------------------------------------
// MeshInstance3D
// ---------------------------------------------------------------------------
void MeshInstance3D::_update_instance() {
	if (!instance.is_valid()) {
		return;
	}
	RS3D->instance_set_mesh(instance, mesh.is_valid() ? mesh->get_rid() : RID());
	_update_skin(true);
	RS3D->instance_set_material(instance, material_override.is_valid() ? material_override->get_rid() : RID());
	RS3D->instance_set_cast_shadows(instance, cast_shadow);
	RS3D->instance_set_transform(instance, get_global_transform());
	RS3D->instance_set_visible(instance, is_visible_in_tree());
}

// Maillage avec peau placé sous un Skeleton3D : on recalcule les sommets quand la pose change
// (calcul sur le processeur ; Godot le fait sur la carte graphique).
void MeshInstance3D::_update_skin(bool p_force) {
	ArrayMesh *array_mesh = Object::cast_to<ArrayMesh>(mesh.ptr());
	Skeleton3D *skeleton = is_inside_tree() ? Object::cast_to<Skeleton3D>(get_parent()) : nullptr;
	const bool skinned = RS3D && instance.is_valid() && array_mesh && array_mesh->has_skin() && skeleton && skeleton->get_bone_count() > 0;
	set_process_internal(skinned);
	if (!skinned) {
		skin_skeleton = 0;
		return;
	}
	if (!p_force && skin_skeleton == skeleton->get_instance_id() && skin_version == skeleton->get_pose_version()) {
		return;
	}
	skin_skeleton = skeleton->get_instance_id();
	skin_version = skeleton->get_pose_version();
	if (!skinned_mesh.is_valid()) {
		skinned_mesh = RS3D->mesh_create();
	}
	std::vector<RenderingServer3D::Vertex> deformed = array_mesh->get_vertices();
	ArrayMesh::apply_skin(array_mesh->get_skin(), skeleton->get_bone_global_poses(), deformed);
	RS3D->mesh_set_data(skinned_mesh, deformed, array_mesh->get_indices());
	RS3D->instance_set_mesh(instance, skinned_mesh);
}

void MeshInstance3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_INTERNAL_PROCESS: {
			_update_skin(false);
		} break;
		case NOTIFICATION_ENTER_TREE: {
			if (RS3D && !instance.is_valid()) {
				instance = RS3D->instance_create(get_tree()->get_scenario());
			}
			_update_instance();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (RS3D && instance.is_valid()) {
				RS3D->free_rid(instance);
			}
			instance = RID();
		} break;
		case NOTIFICATION_TRANSFORM_CHANGED: {
			if (instance.is_valid()) {
				RS3D->instance_set_transform(instance, get_global_transform());
			}
		} break;
		case NOTIFICATION_VISIBILITY_CHANGED_3D: {
			if (instance.is_valid()) {
				RS3D->instance_set_visible(instance, is_visible_in_tree());
			}
		} break;
	}
}

void MeshInstance3D::set_mesh(const Ref<Mesh> &p_mesh) {
	mesh = p_mesh;
	_update_instance();
}

void MeshInstance3D::set_material_override(const Ref<Material> &p_material) {
	material_override = p_material;
	_update_instance();
}

void MeshInstance3D::set_cast_shadow(bool p_enabled) {
	cast_shadow = p_enabled;
	_update_instance();
}

bool MeshInstance3D::_edit_get_aabb(AABB &r_aabb) const {
	if (mesh.is_null()) {
		return false;
	}
	r_aabb = mesh->get_aabb();
	return true;
}

MeshInstance3D::~MeshInstance3D() {
	if (RS3D && instance.is_valid()) {
		RS3D->free_rid(instance);
	}
	if (RS3D && skinned_mesh.is_valid()) {
		RS3D->free_rid(skinned_mesh);
	}
}

void MeshInstance3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_mesh", "mesh"), &MeshInstance3D::set_mesh);
	ClassDB::bind_method(D_METHOD("get_mesh"), &MeshInstance3D::get_mesh);
	ClassDB::bind_method(D_METHOD("set_material_override", "material"), &MeshInstance3D::set_material_override);
	ClassDB::bind_method(D_METHOD("get_material_override"), &MeshInstance3D::get_material_override);
	ClassDB::bind_method(D_METHOD("set_cast_shadow", "enabled"), &MeshInstance3D::set_cast_shadow);
	ClassDB::bind_method(D_METHOD("get_cast_shadow"), &MeshInstance3D::get_cast_shadow);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mesh", PROPERTY_HINT_RESOURCE_TYPE, "Mesh"), "set_mesh", "get_mesh");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material_override", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_material_override", "get_material_override");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "cast_shadow"), "set_cast_shadow", "get_cast_shadow");
}

// ---------------------------------------------------------------------------
// Camera3D
// ---------------------------------------------------------------------------
void Camera3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			// La première caméra de la scène devient la caméra active.
			if (current || !ObjectDB::get_instance(get_tree()->get_current_camera_3d())) {
				make_current();
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (is_current()) {
				get_tree()->set_current_camera_3d(0);
			}
		} break;
	}
}

void Camera3D::set_current(bool p_current) {
	current = p_current;
	if (!is_inside_tree()) {
		return;
	}
	if (p_current) {
		make_current();
	} else {
		clear_current();
	}
}

void Camera3D::make_current() {
	current = true;
	if (is_inside_tree()) {
		get_tree()->set_current_camera_3d(get_instance_id());
	}
}

void Camera3D::clear_current() {
	current = false;
	if (is_current()) {
		get_tree()->set_current_camera_3d(0);
	}
}

bool Camera3D::is_current() const {
	return is_inside_tree() && get_tree()->get_current_camera_3d() == get_instance_id();
}

real_t Camera3D::_get_aspect() const {
	if (!is_inside_tree()) {
		return 16.0f / 9.0f;
	}
	const Size2 s = get_tree()->get_viewport_size();
	return s.y > 0 ? s.x / s.y : 1.0f;
}

RenderingServer3D::CameraData Camera3D::get_camera_data() const {
	RenderingServer3D::CameraData cd;
	// La caméra ignore l'échelle : seules la position et l'orientation comptent.
	cd.transform = get_global_transform().orthonormalized();
	cd.orthogonal = projection == PROJECTION_ORTHOGONAL;
	cd.fov = fov;
	cd.size = size;
	cd.z_near = z_near;
	cd.z_far = std::max(z_far, z_near + 0.01f);
	return cd;
}

Vector2 Camera3D::unproject_position(const Vector3 &p_world) const {
	const RenderingServer3D::CameraData cd = get_camera_data();
	const Size2 s = is_inside_tree() ? get_tree()->get_viewport_size() : Size2(1, 1);
	const Vector3 view = cd.transform.affine_inverse().xform(p_world);
	const Vector3 ndc = cd.get_projection(_get_aspect()).xform(view);
	return Vector2((ndc.x * 0.5f + 0.5f) * s.x, (0.5f - ndc.y * 0.5f) * s.y);
}

bool Camera3D::is_position_behind(const Vector3 &p_world) const {
	const Transform3D t = get_global_transform().orthonormalized();
	return (p_world - t.origin).dot(-t.basis.get_column(2)) < z_near;
}

// Coordonnées normalisées (-1..1, Y vers le haut) d'un point de l'écran.
static Vector2 _screen_to_ndc(const Vector2 &p_screen, const Size2 &p_size) {
	return Vector2(p_screen.x / std::max(p_size.x, 1.0f) * 2.0f - 1.0f, 1.0f - p_screen.y / std::max(p_size.y, 1.0f) * 2.0f);
}

Vector3 Camera3D::project_ray_origin(const Vector2 &p_screen) const {
	const Transform3D t = get_global_transform().orthonormalized();
	if (projection == PROJECTION_PERSPECTIVE) {
		return t.origin;
	}
	const Size2 s = is_inside_tree() ? get_tree()->get_viewport_size() : Size2(1, 1);
	const Vector2 ndc = _screen_to_ndc(p_screen, s);
	return t.xform(Vector3(ndc.x * size * 0.5f * _get_aspect(), ndc.y * size * 0.5f, -z_near));
}

Vector3 Camera3D::project_ray_normal(const Vector2 &p_screen) const {
	const Transform3D t = get_global_transform().orthonormalized();
	if (projection == PROJECTION_ORTHOGONAL) {
		return -t.basis.get_column(2);
	}
	const Size2 s = is_inside_tree() ? get_tree()->get_viewport_size() : Size2(1, 1);
	const Vector2 ndc = _screen_to_ndc(p_screen, s);
	const real_t half = std::tan(Math::deg_to_rad(fov) * 0.5f);
	return t.basis.xform(Vector3(ndc.x * half * _get_aspect(), ndc.y * half, -1)).normalized();
}

Vector3 Camera3D::project_position(const Vector2 &p_screen, real_t p_depth) const {
	const Vector3 origin = project_ray_origin(p_screen);
	const Vector3 dir = project_ray_normal(p_screen);
	// p_depth est mesurée le long de l'axe de visée, pas le long du rayon.
	const Vector3 forward = -get_global_transform().orthonormalized().basis.get_column(2);
	const real_t along = dir.dot(forward);
	return origin + dir * (along > 1e-6f ? p_depth / along : p_depth);
}

void Camera3D::_edit_get_gizmo_lines(std::vector<Line> *r_lines) const {
	const Transform3D t = get_global_transform().orthonormalized();
	const Color color(0.85f, 0.45f, 0.9f, 1);
	const real_t aspect = _get_aspect();
	Vector3 corners_near[4], corners_far[4];
	const real_t depth = 1.0f;
	for (int i = 0; i < 4; i++) {
		const real_t sx = (i == 0 || i == 3) ? -1.0f : 1.0f;
		const real_t sy = i < 2 ? -1.0f : 1.0f;
		if (projection == PROJECTION_PERSPECTIVE) {
			const real_t half = std::tan(Math::deg_to_rad(fov) * 0.5f) * depth;
			corners_near[i] = t.origin;
			corners_far[i] = t.xform(Vector3(sx * half * aspect, sy * half, -depth));
		} else {
			const real_t half = size * 0.5f;
			corners_near[i] = t.xform(Vector3(sx * half * aspect, sy * half, 0));
			corners_far[i] = t.xform(Vector3(sx * half * aspect, sy * half, -depth));
		}
	}
	for (int i = 0; i < 4; i++) {
		r_lines->push_back({ corners_near[i], corners_far[i], color });
		r_lines->push_back({ corners_far[i], corners_far[(i + 1) % 4], color });
		if (projection == PROJECTION_ORTHOGONAL) {
			r_lines->push_back({ corners_near[i], corners_near[(i + 1) % 4], color });
		}
	}
	// Petit triangle indiquant le haut de l'image.
	const Vector3 top = (corners_far[2] + corners_far[3]) * 0.5f;
	const Vector3 up = t.basis.get_column(1) * (corners_far[2] - corners_far[1]).length() * 0.3f;
	const Vector3 side = (corners_far[2] - corners_far[3]) * 0.15f;
	r_lines->push_back({ top - side, top + up, color });
	r_lines->push_back({ top + side, top + up, color });
}

void Camera3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_projection", "projection"), &Camera3D::set_projection);
	ClassDB::bind_method(D_METHOD("get_projection"), &Camera3D::get_projection);
	ClassDB::bind_method(D_METHOD("set_fov", "fov"), &Camera3D::set_fov);
	ClassDB::bind_method(D_METHOD("get_fov"), &Camera3D::get_fov);
	ClassDB::bind_method(D_METHOD("set_size", "size"), &Camera3D::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &Camera3D::get_size);
	ClassDB::bind_method(D_METHOD("set_near", "near"), &Camera3D::set_near);
	ClassDB::bind_method(D_METHOD("get_near"), &Camera3D::get_near);
	ClassDB::bind_method(D_METHOD("set_far", "far"), &Camera3D::set_far);
	ClassDB::bind_method(D_METHOD("get_far"), &Camera3D::get_far);
	ClassDB::bind_method(D_METHOD("set_current", "current"), &Camera3D::set_current);
	ClassDB::bind_method(D_METHOD("get_current_flag"), &Camera3D::get_current_flag);
	ClassDB::bind_method(D_METHOD("make_current"), &Camera3D::make_current);
	ClassDB::bind_method(D_METHOD("clear_current"), &Camera3D::clear_current);
	ClassDB::bind_method(D_METHOD("is_current"), &Camera3D::is_current);
	ClassDB::bind_method(D_METHOD("unproject_position", "world_position"), &Camera3D::unproject_position);
	ClassDB::bind_method(D_METHOD("is_position_behind", "world_position"), &Camera3D::is_position_behind);
	ClassDB::bind_method(D_METHOD("project_ray_origin", "screen_position"), &Camera3D::project_ray_origin);
	ClassDB::bind_method(D_METHOD("project_ray_normal", "screen_position"), &Camera3D::project_ray_normal);
	ClassDB::bind_method(D_METHOD("project_position", "screen_position", "depth"), &Camera3D::project_position);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "projection", PROPERTY_HINT_ENUM, "Perspective,Orthogonal"), "set_projection", "get_projection");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fov", PROPERTY_HINT_RANGE, "1,179,0.1"), "set_fov", "get_fov");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "size", PROPERTY_HINT_RANGE, "0.01,1000,0.01"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "near", PROPERTY_HINT_RANGE, "0.001,10,0.001"), "set_near", "get_near");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "far", PROPERTY_HINT_RANGE, "0.1,10000,0.1"), "set_far", "get_far");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "current"), "set_current", "get_current_flag");
}

// ---------------------------------------------------------------------------
// Light3D
// ---------------------------------------------------------------------------
void Light3D::_update_light() {
	if (!light.is_valid()) {
		return;
	}
	RS3D->light_set_data(light, data);
	RS3D->light_set_transform(light, get_global_transform().orthonormalized());
	RS3D->light_set_visible(light, is_visible_in_tree());
}

void Light3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (RS3D && !light.is_valid()) {
				light = RS3D->light_create(get_tree()->get_scenario());
			}
			_update_light();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (RS3D && light.is_valid()) {
				RS3D->free_rid(light);
			}
			light = RID();
		} break;
		case NOTIFICATION_TRANSFORM_CHANGED:
		case NOTIFICATION_VISIBILITY_CHANGED_3D: {
			_update_light();
		} break;
	}
}

void Light3D::set_color(const Color &p_color) {
	data.color = p_color;
	_update_light();
}

void Light3D::set_energy(real_t p_energy) {
	data.energy = std::max(p_energy, (real_t)0);
	_update_light();
}

Light3D::~Light3D() {
	if (RS3D && light.is_valid()) {
		RS3D->free_rid(light);
	}
}

void Light3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_color", "color"), &Light3D::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &Light3D::get_color);
	ClassDB::bind_method(D_METHOD("set_energy", "energy"), &Light3D::set_energy);
	ClassDB::bind_method(D_METHOD("get_energy"), &Light3D::get_energy);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color"), "set_color", "get_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "energy", PROPERTY_HINT_RANGE, "0,16,0.01"), "set_energy", "get_energy");
}

static const Color LIGHT_GIZMO_COLOR(1.0f, 0.85f, 0.3f, 1);

// --- DirectionalLight3D ---
void DirectionalLight3D::set_shadow(bool p_enabled) {
	data.shadow = p_enabled;
	_update_light();
}

void DirectionalLight3D::_edit_get_gizmo_lines(std::vector<Line> *r_lines) const {
	const Transform3D t = get_global_transform().orthonormalized();
	const Vector3 x(1, 0, 0), y(0, 1, 0);
	DebugLines3D::add_arc(r_lines, t, Vector3(), x, y, 0.3f, 0, (real_t)Math_TAU, LIGHT_GIZMO_COLOR, 16);
	for (int i = 0; i < 4; i++) {
		const real_t a = (real_t)i * (real_t)Math_PI * 0.5f;
		const Vector3 p(std::cos(a) * 0.3f, std::sin(a) * 0.3f, 0);
		r_lines->push_back({ t.xform(p), t.xform(p + Vector3(0, 0, -1.0f)), LIGHT_GIZMO_COLOR });
	}
	r_lines->push_back({ t.origin, t.xform(Vector3(0, 0, -1.5f)), LIGHT_GIZMO_COLOR });
}

void DirectionalLight3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_shadow", "enabled"), &DirectionalLight3D::set_shadow);
	ClassDB::bind_method(D_METHOD("has_shadow"), &DirectionalLight3D::has_shadow);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shadow_enabled"), "set_shadow", "has_shadow");
}

// --- OmniLight3D ---
void OmniLight3D::set_range(real_t p_range) {
	data.range = std::max(p_range, (real_t)0.01);
	_update_light();
}

void OmniLight3D::set_attenuation(real_t p_attenuation) {
	data.attenuation = std::max(p_attenuation, (real_t)0.01);
	_update_light();
}

void OmniLight3D::_edit_get_gizmo_lines(std::vector<Line> *r_lines) const {
	Transform3D t;
	t.origin = get_global_transform().origin;
	const Vector3 x(1, 0, 0), y(0, 1, 0), z(0, 0, 1);
	const real_t tau = (real_t)Math_TAU;
	DebugLines3D::add_arc(r_lines, t, Vector3(), x, y, data.range, 0, tau, LIGHT_GIZMO_COLOR);
	DebugLines3D::add_arc(r_lines, t, Vector3(), x, z, data.range, 0, tau, LIGHT_GIZMO_COLOR);
	DebugLines3D::add_arc(r_lines, t, Vector3(), y, z, data.range, 0, tau, LIGHT_GIZMO_COLOR);
}

void OmniLight3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_range", "range"), &OmniLight3D::set_range);
	ClassDB::bind_method(D_METHOD("get_range"), &OmniLight3D::get_range);
	ClassDB::bind_method(D_METHOD("set_attenuation", "attenuation"), &OmniLight3D::set_attenuation);
	ClassDB::bind_method(D_METHOD("get_attenuation"), &OmniLight3D::get_attenuation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "range", PROPERTY_HINT_RANGE, "0.01,500,0.01"), "set_range", "get_range");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "attenuation", PROPERTY_HINT_RANGE, "0.01,8,0.01"), "set_attenuation", "get_attenuation");
}

// --- SpotLight3D ---
void SpotLight3D::set_range(real_t p_range) {
	data.range = std::max(p_range, (real_t)0.01);
	_update_light();
}

void SpotLight3D::set_attenuation(real_t p_attenuation) {
	data.attenuation = std::max(p_attenuation, (real_t)0.01);
	_update_light();
}

void SpotLight3D::set_spot_angle(real_t p_degrees) {
	data.spot_angle = Math::clamp(p_degrees, (real_t)1, (real_t)89);
	_update_light();
}

void SpotLight3D::_edit_get_gizmo_lines(std::vector<Line> *r_lines) const {
	const Transform3D t = get_global_transform().orthonormalized();
	const real_t angle = Math::deg_to_rad(data.spot_angle);
	const real_t depth = std::cos(angle) * data.range;
	const real_t radius = std::sin(angle) * data.range;
	DebugLines3D::add_arc(r_lines, t, Vector3(0, 0, -depth), Vector3(1, 0, 0), Vector3(0, 1, 0), radius, 0, (real_t)Math_TAU, LIGHT_GIZMO_COLOR);
	for (int i = 0; i < 4; i++) {
		const real_t a = (real_t)i * (real_t)Math_PI * 0.5f;
		r_lines->push_back({ t.origin, t.xform(Vector3(std::cos(a) * radius, std::sin(a) * radius, -depth)), LIGHT_GIZMO_COLOR });
	}
}

void SpotLight3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_range", "range"), &SpotLight3D::set_range);
	ClassDB::bind_method(D_METHOD("get_range"), &SpotLight3D::get_range);
	ClassDB::bind_method(D_METHOD("set_attenuation", "attenuation"), &SpotLight3D::set_attenuation);
	ClassDB::bind_method(D_METHOD("get_attenuation"), &SpotLight3D::get_attenuation);
	ClassDB::bind_method(D_METHOD("set_spot_angle", "degrees"), &SpotLight3D::set_spot_angle);
	ClassDB::bind_method(D_METHOD("get_spot_angle"), &SpotLight3D::get_spot_angle);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "range", PROPERTY_HINT_RANGE, "0.01,500,0.01"), "set_range", "get_range");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "attenuation", PROPERTY_HINT_RANGE, "0.01,8,0.01"), "set_attenuation", "get_attenuation");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spot_angle", PROPERTY_HINT_RANGE, "1,89,0.1"), "set_spot_angle", "get_spot_angle");
}

// ---------------------------------------------------------------------------
// WorldEnvironment
// ---------------------------------------------------------------------------
void WorldEnvironment::_update() {
	if (!is_inside_tree() || !RS3D) {
		return;
	}
	if (environment.is_valid()) {
		RS3D->scenario_set_environment(get_tree()->get_scenario(), environment->get_data());
	} else {
		RS3D->scenario_clear_environment(get_tree()->get_scenario());
	}
}

void WorldEnvironment::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			_update();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (RS3D) {
				RS3D->scenario_clear_environment(get_tree()->get_scenario());
			}
		} break;
	}
}

void WorldEnvironment::set_environment(const Ref<Environment> &p_environment) {
	if (environment.is_valid()) {
		environment->disconnect("changed", Callable(this, "_environment_changed"));
	}
	environment = p_environment;
	if (environment.is_valid()) {
		environment->connect("changed", Callable(this, "_environment_changed"));
	}
	_update();
}

void WorldEnvironment::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_environment", "environment"), &WorldEnvironment::set_environment);
	ClassDB::bind_method(D_METHOD("get_environment"), &WorldEnvironment::get_environment);
	ClassDB::bind_method(D_METHOD("_environment_changed"), &WorldEnvironment::_environment_changed);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "environment", PROPERTY_HINT_RESOURCE_TYPE, "Environment"), "set_environment", "get_environment");
}
