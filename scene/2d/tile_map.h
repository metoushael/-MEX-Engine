// TileMap : grille de tuiles tirées d'une texture d'atlas, avec collisions optionnelles.
#pragma once

#include "scene/2d/node_2d.h"
#include "scene/main/scene_tree.h"
#include "servers/physics_server_2d.h"

class TileMap : public Node2D, public PhysicsSyncListener {
	GDCLASS(TileMap, Node2D)

	struct Cell {
		int x = 0, y = 0;
		bool operator<(const Cell &c) const { return y == c.y ? x < c.x : y < c.y; }
	};

	Ref<Texture2D> texture;
	Vector2 tile_size = Vector2(32, 32);
	std::map<Cell, int> cells; // (x, y) -> identifiant de tuile dans l'atlas
	bool collision_enabled = true;
	uint32_t collision_layer = 1;
	std::set<int> non_solid_tiles;

	RID body;
	bool collision_dirty = true;
	bool in_space = false;
	Transform2D last_pushed;
	Vector2 last_scale = Vector2(1, 1);
	bool has_pushed = false;

	void _update_collision();
	bool _is_solid(int p_tile) const { return non_solid_tiles.count(p_tile) == 0; }

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_texture() const { return texture; }
	void set_tile_size(const Vector2 &p_size);
	Vector2 get_tile_size() const { return tile_size; }
	void set_collision_enabled(bool p_enabled);
	bool is_collision_enabled() const { return collision_enabled; }
	void set_collision_layer(int p_layer);
	int get_collision_layer() const { return (int)collision_layer; }
	void set_non_solid_tiles(const Array &p_tiles);
	Array get_non_solid_tiles() const;

	// Données sérialisées : [x, y, tuile, x, y, tuile, ...]
	void set_tile_data(const Array &p_data);
	Array get_tile_data() const;

	void set_cell(int p_x, int p_y, int p_tile);
	int get_cell(int p_x, int p_y) const; // -1 si vide
	void erase_cell(int p_x, int p_y);
	void clear();
	Array get_used_cells() const;
	Rect2 get_used_rect() const; // en cellules
	int get_atlas_columns() const;
	int get_atlas_tile_count() const;
	Rect2 get_tile_region(int p_tile) const;
	static Color get_placeholder_color(int p_tile);

	Vector2 local_to_map(const Vector2 &p_local) const;
	Vector2 map_to_local(const Vector2 &p_cell) const; // centre de la cellule

	virtual void _physics_pre_step() override;
	virtual void _physics_post_step() override {}

	virtual bool _edit_use_rect() const override { return !cells.empty(); }
	virtual Rect2 _edit_get_rect() const override;

	TileMap();
	~TileMap();
};
