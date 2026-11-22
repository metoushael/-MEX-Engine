#include "tile_map.h"

#define PS PhysicsServer2D::get_singleton()

TileMap::TileMap() {
	body = PS->body_create();
	PS->body_set_mode(body, PhysicsServer2D::BODY_MODE_STATIC);
	PS->collision_object_attach_instance_id(body, get_instance_id());
}

TileMap::~TileMap() {
	if (PS) {
		PS->free_rid(body);
	}
}

void TileMap::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (get_tree()->is_edit_mode()) {
				return;
			}
			PS->collision_object_set_space(body, get_tree()->get_space());
			get_tree()->add_physics_listener(this);
			in_space = true;
			has_pushed = false;
			collision_dirty = true;
		} break;
		case NOTIFICATION_READY: {
			if (in_space) {
				_physics_pre_step();
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (in_space) {
				PS->collision_object_set_space(body, RID());
				get_tree()->remove_physics_listener(this);
				in_space = false;
			}
		} break;
		case NOTIFICATION_DRAW: {
			const bool has_texture = texture.is_valid();
			for (auto &kv : cells) {
				Rect2 dst(Vector2(kv.first.x * tile_size.x, kv.first.y * tile_size.y), tile_size);
				if (has_texture) {
					draw_texture_rect_region(texture, dst, get_tile_region(kv.second));
				} else {
					// Sans texture : tuiles colorées, pratiques pour prototyper.
					draw_rect(dst, get_placeholder_color(kv.second), true);
					draw_rect(Rect2(dst.position, Size2(dst.size.x, 2)), Color(1, 1, 1, 0.18f), true);
				}
			}
		} break;
	}
}

Color TileMap::get_placeholder_color(int p_tile) {
	static const Color palette[] = {
		Color(0.36f, 0.62f, 0.33f), Color(0.55f, 0.40f, 0.26f), Color(0.50f, 0.52f, 0.56f), Color(0.80f, 0.69f, 0.42f),
		Color(0.30f, 0.50f, 0.75f), Color(0.72f, 0.33f, 0.31f), Color(0.58f, 0.42f, 0.70f), Color(0.85f, 0.85f, 0.88f),
	};
	return palette[((p_tile % 8) + 8) % 8];
}

int TileMap::get_atlas_columns() const {
	if (texture.is_null() || tile_size.x <= 0) {
		return 8;
	}
	return std::max(1, (int)(texture->get_width() / tile_size.x));
}

int TileMap::get_atlas_tile_count() const {
	if (texture.is_null() || tile_size.y <= 0) {
		return 8;
	}
	return get_atlas_columns() * std::max(1, (int)(texture->get_height() / tile_size.y));
}

Rect2 TileMap::get_tile_region(int p_tile) const {
	const int columns = get_atlas_columns();
	return Rect2(Vector2((p_tile % columns) * tile_size.x, (p_tile / columns) * tile_size.y), tile_size);
}

void TileMap::_update_collision() {
	collision_dirty = false;
	PS->collision_object_clear_shapes(body);
	if (!collision_enabled) {
		return;
	}
	const Transform2D global = get_global_transform();
	const Transform2D rigid(global.get_rotation(), global.origin);
	const Transform2D to_body = rigid.affine_inverse() * global;
	// Fusionne les suites horizontales de tuiles pleines en un seul rectangle :
	// moins de formes, et pas d'accrochage sur les arêtes internes.
	auto it = cells.begin();
	while (it != cells.end()) {
		if (!_is_solid(it->second)) {
			++it;
			continue;
		}
		const int y = it->first.y;
		const int x0 = it->first.x;
		int x1 = x0;
		++it;
		while (it != cells.end() && it->first.y == y && it->first.x == x1 + 1 && _is_solid(it->second)) {
			x1 = it->first.x;
			++it;
		}
		PhysicsServer2D::ShapeData sd;
		sd.type = PhysicsServer2D::SHAPE_POLYGON;
		const real_t left = x0 * tile_size.x, right = (x1 + 1) * tile_size.x;
		const real_t top = y * tile_size.y, bottom = (y + 1) * tile_size.y;
		sd.points = { Vector2(left, top), Vector2(right, top), Vector2(right, bottom), Vector2(left, bottom) };
		PS->collision_object_add_shape(body, sd, to_body);
	}
}

void TileMap::_physics_pre_step() {
	const Transform2D global = get_global_transform();
	const Transform2D rigid(global.get_rotation(), global.origin);
	if (!has_pushed || rigid != last_pushed) {
		last_pushed = rigid;
		has_pushed = true;
		PS->collision_object_set_transform(body, rigid);
	}
	// L'échelle est cuite dans les formes : on les reconstruit si elle change.
	const Vector2 global_scale = global.get_scale();
	if (global_scale != last_scale) {
		last_scale = global_scale;
		collision_dirty = true;
	}
	if (collision_dirty) {
		_update_collision();
	}
}

void TileMap::set_texture(const Ref<Texture2D> &p_texture) {
	texture = p_texture;
	queue_redraw();
}

void TileMap::set_tile_size(const Vector2 &p_size) {
	ERR_FAIL_COND_MSG(p_size.x < 1 || p_size.y < 1, "TileMap : la taille des tuiles doit être d'au moins 1 pixel.");
	tile_size = p_size;
	collision_dirty = true;
	queue_redraw();
}

void TileMap::set_collision_enabled(bool p_enabled) {
	collision_enabled = p_enabled;
	collision_dirty = true;
}

void TileMap::set_collision_layer(int p_layer) {
	collision_layer = (uint32_t)p_layer;
	PS->collision_object_set_collision_layer(body, collision_layer);
}

void TileMap::set_non_solid_tiles(const Array &p_tiles) {
	non_solid_tiles.clear();
	for (const Variant &v : p_tiles.vec()) {
		non_solid_tiles.insert((int)v.as_int());
	}
	collision_dirty = true;
}

Array TileMap::get_non_solid_tiles() const {
	Array out;
	for (int t : non_solid_tiles) {
		out.push_back(Variant(t));
	}
	return out;
}

void TileMap::set_tile_data(const Array &p_data) {
	cells.clear();
	const std::vector<Variant> &v = p_data.vec();
	for (size_t i = 0; i + 2 < v.size(); i += 3) {
		Cell c;
		c.x = (int)v[i].as_int();
		c.y = (int)v[i + 1].as_int();
		cells[c] = (int)v[i + 2].as_int();
	}
	collision_dirty = true;
	queue_redraw();
}

Array TileMap::get_tile_data() const {
	Array out;
	for (auto &kv : cells) {
		out.push_back(Variant(kv.first.x));
		out.push_back(Variant(kv.first.y));
		out.push_back(Variant(kv.second));
	}
	return out;
}

void TileMap::set_cell(int p_x, int p_y, int p_tile) {
	if (p_tile < 0) {
		erase_cell(p_x, p_y);
		return;
	}
	Cell c;
	c.x = p_x;
	c.y = p_y;
	auto it = cells.find(c);
	if (it != cells.end() && it->second == p_tile) {
		return;
	}
	cells[c] = p_tile;
	collision_dirty = true;
	queue_redraw();
}

int TileMap::get_cell(int p_x, int p_y) const {
	Cell c;
	c.x = p_x;
	c.y = p_y;
	auto it = cells.find(c);
	return it == cells.end() ? -1 : it->second;
}

void TileMap::erase_cell(int p_x, int p_y) {
	Cell c;
	c.x = p_x;
	c.y = p_y;
	if (cells.erase(c)) {
		collision_dirty = true;
		queue_redraw();
	}
}

void TileMap::clear() {
	cells.clear();
	collision_dirty = true;
	queue_redraw();
}

Array TileMap::get_used_cells() const {
	Array out;
	for (auto &kv : cells) {
		out.push_back(Variant(Vector2((real_t)kv.first.x, (real_t)kv.first.y)));
	}
	return out;
}

Rect2 TileMap::get_used_rect() const {
	if (cells.empty()) {
		return Rect2();
	}
	int min_x = INT32_MAX, min_y = INT32_MAX, max_x = INT32_MIN, max_y = INT32_MIN;
	for (auto &kv : cells) {
		min_x = std::min(min_x, kv.first.x);
		min_y = std::min(min_y, kv.first.y);
		max_x = std::max(max_x, kv.first.x);
		max_y = std::max(max_y, kv.first.y);
	}
	return Rect2((real_t)min_x, (real_t)min_y, (real_t)(max_x - min_x + 1), (real_t)(max_y - min_y + 1));
}

Rect2 TileMap::_edit_get_rect() const {
	Rect2 used = get_used_rect();
	return Rect2(used.position * tile_size, used.size * tile_size);
}

Vector2 TileMap::local_to_map(const Vector2 &p_local) const {
	return Vector2(std::floor(p_local.x / tile_size.x), std::floor(p_local.y / tile_size.y));
}

Vector2 TileMap::map_to_local(const Vector2 &p_cell) const {
	return Vector2((p_cell.x + 0.5f) * tile_size.x, (p_cell.y + 0.5f) * tile_size.y);
}

void TileMap::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_texture", "texture"), &TileMap::set_texture);
	ClassDB::bind_method(D_METHOD("get_texture"), &TileMap::get_texture);
	ClassDB::bind_method(D_METHOD("set_tile_size", "size"), &TileMap::set_tile_size);
	ClassDB::bind_method(D_METHOD("get_tile_size"), &TileMap::get_tile_size);
	ClassDB::bind_method(D_METHOD("set_collision_enabled", "enabled"), &TileMap::set_collision_enabled);
	ClassDB::bind_method(D_METHOD("is_collision_enabled"), &TileMap::is_collision_enabled);
	ClassDB::bind_method(D_METHOD("set_collision_layer", "layer"), &TileMap::set_collision_layer);
	ClassDB::bind_method(D_METHOD("get_collision_layer"), &TileMap::get_collision_layer);
	ClassDB::bind_method(D_METHOD("set_non_solid_tiles", "tiles"), &TileMap::set_non_solid_tiles);
	ClassDB::bind_method(D_METHOD("get_non_solid_tiles"), &TileMap::get_non_solid_tiles);
	ClassDB::bind_method(D_METHOD("set_tile_data", "data"), &TileMap::set_tile_data);
	ClassDB::bind_method(D_METHOD("get_tile_data"), &TileMap::get_tile_data);
	ClassDB::bind_method(D_METHOD("set_cell", "x", "y", "tile"), &TileMap::set_cell);
	ClassDB::bind_method(D_METHOD("get_cell", "x", "y"), &TileMap::get_cell);
	ClassDB::bind_method(D_METHOD("erase_cell", "x", "y"), &TileMap::erase_cell);
	ClassDB::bind_method(D_METHOD("clear"), &TileMap::clear);
	ClassDB::bind_method(D_METHOD("get_used_cells"), &TileMap::get_used_cells);
	ClassDB::bind_method(D_METHOD("get_used_rect"), &TileMap::get_used_rect);
	ClassDB::bind_method(D_METHOD("local_to_map", "local_position"), &TileMap::local_to_map);
	ClassDB::bind_method(D_METHOD("map_to_local", "cell"), &TileMap::map_to_local);

	ADD_GROUP("Tile Map");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_texture", "get_texture");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "tile_size"), "set_tile_size", "get_tile_size");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "collision_enabled"), "set_collision_enabled", "is_collision_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_layer", PROPERTY_HINT_RANGE, "0,65535,1"), "set_collision_layer", "get_collision_layer");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "non_solid_tiles"), "set_non_solid_tiles", "get_non_solid_tiles");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "tile_data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_tile_data", "get_tile_data");
}
