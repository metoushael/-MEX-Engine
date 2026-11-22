// Recherche de chemin A* : sur un graphe de points (AStar2D) ou sur une grille (AStarGrid2D).
#pragma once

#include "core/object/ref_counted.h"

#include <unordered_map>
#include <unordered_set>

class AStar2D : public RefCounted {
	GDCLASS(AStar2D, RefCounted)

	struct Point {
		Vector2 position;
		real_t weight = 1;
		bool disabled = false;
		std::unordered_set<int64_t> neighbors; // arêtes sortantes
	};
	std::unordered_map<int64_t, Point> points;

	bool _solve(int64_t p_from, int64_t p_to, std::vector<int64_t> &r_path) const;

protected:
	static void _bind_methods();

public:
	int64_t get_available_point_id() const;
	void add_point(int64_t p_id, const Vector2 &p_position, real_t p_weight = 1);
	void remove_point(int64_t p_id);
	bool has_point(int64_t p_id) const { return points.count(p_id) > 0; }
	Vector2 get_point_position(int64_t p_id) const;
	void set_point_position(int64_t p_id, const Vector2 &p_position);
	real_t get_point_weight_scale(int64_t p_id) const;
	void set_point_weight_scale(int64_t p_id, real_t p_weight);
	void set_point_disabled(int64_t p_id, bool p_disabled = true);
	bool is_point_disabled(int64_t p_id) const;
	int get_point_count() const { return (int)points.size(); }
	Array get_point_ids() const;
	void connect_points(int64_t p_from, int64_t p_to, bool p_bidirectional = true);
	void disconnect_points(int64_t p_from, int64_t p_to, bool p_bidirectional = true);
	bool are_points_connected(int64_t p_from, int64_t p_to) const;
	Array get_point_connections(int64_t p_id) const;
	int64_t get_closest_point(const Vector2 &p_position, bool p_include_disabled = false) const;
	Array get_id_path(int64_t p_from, int64_t p_to) const;
	Array get_point_path(int64_t p_from, int64_t p_to) const;
	void clear() { points.clear(); }
};

class AStarGrid2D : public RefCounted {
	GDCLASS(AStarGrid2D, RefCounted)

public:
	enum DiagonalMode {
		DIAGONAL_MODE_ALWAYS,
		DIAGONAL_MODE_NEVER,
		DIAGONAL_MODE_AT_LEAST_ONE_WALKABLE,
		DIAGONAL_MODE_ONLY_IF_NO_OBSTACLES,
	};

private:
	int width = 0;
	int height = 0;
	Vector2 cell_size = Vector2(1, 1);
	Vector2 offset;
	int diagonal_mode = DIAGONAL_MODE_ALWAYS;
	std::vector<uint8_t> solid;
	std::vector<real_t> weights;

	bool _in(int p_x, int p_y) const { return p_x >= 0 && p_y >= 0 && p_x < width && p_y < height; }
	bool _free(int p_x, int p_y) const { return _in(p_x, p_y) && !solid[(size_t)(p_y * width + p_x)]; }
	bool _solve(const Vector2 &p_from, const Vector2 &p_to, std::vector<int> &r_cells) const;

protected:
	static void _bind_methods();

public:
	void set_size(const Vector2 &p_size);
	Vector2 get_size() const { return Vector2((real_t)width, (real_t)height); }
	void set_cell_size(const Vector2 &p_size) { cell_size = p_size; }
	Vector2 get_cell_size() const { return cell_size; }
	void set_offset(const Vector2 &p_offset) { offset = p_offset; }
	Vector2 get_offset() const { return offset; }
	void set_diagonal_mode(int p_mode) { diagonal_mode = Math::clamp(p_mode, 0, 3); }
	int get_diagonal_mode() const { return diagonal_mode; }
	void update() {} // compatibilité : la grille est toujours à jour

	bool is_in_bounds(const Vector2 &p_id) const { return _in((int)p_id.x, (int)p_id.y); }
	void set_point_solid(const Vector2 &p_id, bool p_solid = true);
	bool is_point_solid(const Vector2 &p_id) const;
	void set_point_weight_scale(const Vector2 &p_id, real_t p_weight);
	real_t get_point_weight_scale(const Vector2 &p_id) const;
	void fill_solid_region(const Rect2 &p_region, bool p_solid = true);
	Vector2 get_point_position(const Vector2 &p_id) const { return offset + Vector2(p_id.x * cell_size.x, p_id.y * cell_size.y); }
	void clear();

	// Chemin en cellules (Vector2 à coordonnées entières) ou en positions ; vide si la cible est inaccessible.
	Array get_id_path(const Vector2 &p_from, const Vector2 &p_to) const;
	Array get_point_path(const Vector2 &p_from, const Vector2 &p_to) const;
};
