#include "a_star.h"

#include "core/object/class_db.h"

#include <algorithm>
#include <cmath>
#include <queue>

// ---------------------------------------------------------------------------
// AStar2D
// ---------------------------------------------------------------------------
int64_t AStar2D::get_available_point_id() const {
	int64_t id = 0;
	while (points.count(id)) {
		id++;
	}
	return id;
}

void AStar2D::add_point(int64_t p_id, const Vector2 &p_position, real_t p_weight) {
	ERR_FAIL_COND_MSG(p_id < 0, "AStar2D : l'identifiant d'un point doit être positif.");
	Point &point = points[p_id]; // un point existant est déplacé, ses liaisons sont conservées
	point.position = p_position;
	point.weight = std::max((real_t)0, p_weight);
}

void AStar2D::remove_point(int64_t p_id) {
	if (!points.erase(p_id)) {
		return;
	}
	for (auto &kv : points) {
		kv.second.neighbors.erase(p_id);
	}
}

Vector2 AStar2D::get_point_position(int64_t p_id) const {
	auto it = points.find(p_id);
	ERR_FAIL_COND_V_MSG(it == points.end(), Vector2(), "AStar2D : point inconnu.");
	return it->second.position;
}

void AStar2D::set_point_position(int64_t p_id, const Vector2 &p_position) {
	auto it = points.find(p_id);
	ERR_FAIL_COND_MSG(it == points.end(), "AStar2D : point inconnu.");
	it->second.position = p_position;
}

real_t AStar2D::get_point_weight_scale(int64_t p_id) const {
	auto it = points.find(p_id);
	return it == points.end() ? 1 : it->second.weight;
}

void AStar2D::set_point_weight_scale(int64_t p_id, real_t p_weight) {
	auto it = points.find(p_id);
	ERR_FAIL_COND_MSG(it == points.end(), "AStar2D : point inconnu.");
	it->second.weight = std::max((real_t)0, p_weight);
}

void AStar2D::set_point_disabled(int64_t p_id, bool p_disabled) {
	auto it = points.find(p_id);
	ERR_FAIL_COND_MSG(it == points.end(), "AStar2D : point inconnu.");
	it->second.disabled = p_disabled;
}

bool AStar2D::is_point_disabled(int64_t p_id) const {
	auto it = points.find(p_id);
	return it != points.end() && it->second.disabled;
}

Array AStar2D::get_point_ids() const {
	std::vector<int64_t> ids;
	for (const auto &kv : points) {
		ids.push_back(kv.first);
	}
	std::sort(ids.begin(), ids.end());
	Array out;
	for (int64_t id : ids) {
		out.push_back(Variant(id));
	}
	return out;
}

void AStar2D::connect_points(int64_t p_from, int64_t p_to, bool p_bidirectional) {
	ERR_FAIL_COND_MSG(!points.count(p_from) || !points.count(p_to) || p_from == p_to, "AStar2D : liaison entre des points inconnus ou identiques.");
	points[p_from].neighbors.insert(p_to);
	if (p_bidirectional) {
		points[p_to].neighbors.insert(p_from);
	}
}

void AStar2D::disconnect_points(int64_t p_from, int64_t p_to, bool p_bidirectional) {
	if (points.count(p_from)) {
		points[p_from].neighbors.erase(p_to);
	}
	if (p_bidirectional && points.count(p_to)) {
		points[p_to].neighbors.erase(p_from);
	}
}

bool AStar2D::are_points_connected(int64_t p_from, int64_t p_to) const {
	auto it = points.find(p_from);
	return it != points.end() && it->second.neighbors.count(p_to) > 0;
}

Array AStar2D::get_point_connections(int64_t p_id) const {
	Array out;
	auto it = points.find(p_id);
	if (it != points.end()) {
		std::vector<int64_t> ids(it->second.neighbors.begin(), it->second.neighbors.end());
		std::sort(ids.begin(), ids.end());
		for (int64_t id : ids) {
			out.push_back(Variant(id));
		}
	}
	return out;
}

int64_t AStar2D::get_closest_point(const Vector2 &p_position, bool p_include_disabled) const {
	int64_t best = -1;
	real_t best_distance = 0;
	for (const auto &kv : points) {
		if (kv.second.disabled && !p_include_disabled) {
			continue;
		}
		const real_t distance = kv.second.position.distance_squared_to(p_position);
		// À distance égale, le plus petit identifiant : résultat stable d'une exécution à l'autre.
		if (best < 0 || distance < best_distance || (distance == best_distance && kv.first < best)) {
			best = kv.first;
			best_distance = distance;
		}
	}
	return best;
}

bool AStar2D::_solve(int64_t p_from, int64_t p_to, std::vector<int64_t> &r_path) const {
	auto from = points.find(p_from);
	auto to = points.find(p_to);
	if (from == points.end() || to == points.end() || from->second.disabled || to->second.disabled) {
		return false;
	}
	struct Entry {
		real_t f;
		int64_t id;
		bool operator<(const Entry &p_other) const { return f > p_other.f; } // file à priorité : le plus petit d'abord
	};
	std::priority_queue<Entry> open;
	std::unordered_map<int64_t, real_t> cost;
	std::unordered_map<int64_t, int64_t> previous;
	std::unordered_set<int64_t> closed;
	cost[p_from] = 0;
	open.push({ from->second.position.distance_to(to->second.position), p_from });
	while (!open.empty()) {
		const int64_t current = open.top().id;
		open.pop();
		if (current == p_to) {
			for (int64_t id = p_to; id != p_from; id = previous[id]) {
				r_path.push_back(id);
			}
			r_path.push_back(p_from);
			std::reverse(r_path.begin(), r_path.end());
			return true;
		}
		if (!closed.insert(current).second) {
			continue;
		}
		const Point &point = points.at(current);
		for (int64_t next_id : point.neighbors) {
			auto next = points.find(next_id);
			if (next == points.end() || next->second.disabled || closed.count(next_id)) {
				continue;
			}
			const real_t g = cost[current] + point.position.distance_to(next->second.position) * next->second.weight;
			auto known = cost.find(next_id);
			if (known == cost.end() || g < known->second) {
				cost[next_id] = g;
				previous[next_id] = current;
				open.push({ g + next->second.position.distance_to(to->second.position), next_id });
			}
		}
	}
	return false;
}

Array AStar2D::get_id_path(int64_t p_from, int64_t p_to) const {
	Array out;
	std::vector<int64_t> path;
	if (_solve(p_from, p_to, path)) {
		for (int64_t id : path) {
			out.push_back(Variant(id));
		}
	}
	return out;
}

Array AStar2D::get_point_path(int64_t p_from, int64_t p_to) const {
	Array out;
	std::vector<int64_t> path;
	if (_solve(p_from, p_to, path)) {
		for (int64_t id : path) {
			out.push_back(Variant(points.at(id).position));
		}
	}
	return out;
}

void AStar2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_available_point_id"), &AStar2D::get_available_point_id);
	ClassDB::bind_method(D_METHOD("add_point", "id", "position", "weight_scale"), &AStar2D::add_point, DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("remove_point", "id"), &AStar2D::remove_point);
	ClassDB::bind_method(D_METHOD("has_point", "id"), &AStar2D::has_point);
	ClassDB::bind_method(D_METHOD("get_point_position", "id"), &AStar2D::get_point_position);
	ClassDB::bind_method(D_METHOD("set_point_position", "id", "position"), &AStar2D::set_point_position);
	ClassDB::bind_method(D_METHOD("get_point_weight_scale", "id"), &AStar2D::get_point_weight_scale);
	ClassDB::bind_method(D_METHOD("set_point_weight_scale", "id", "weight_scale"), &AStar2D::set_point_weight_scale);
	ClassDB::bind_method(D_METHOD("set_point_disabled", "id", "disabled"), &AStar2D::set_point_disabled, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("is_point_disabled", "id"), &AStar2D::is_point_disabled);
	ClassDB::bind_method(D_METHOD("get_point_count"), &AStar2D::get_point_count);
	ClassDB::bind_method(D_METHOD("get_point_ids"), &AStar2D::get_point_ids);
	ClassDB::bind_method(D_METHOD("connect_points", "id", "to_id", "bidirectional"), &AStar2D::connect_points, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("disconnect_points", "id", "to_id", "bidirectional"), &AStar2D::disconnect_points, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("are_points_connected", "id", "to_id"), &AStar2D::are_points_connected);
	ClassDB::bind_method(D_METHOD("get_point_connections", "id"), &AStar2D::get_point_connections);
	ClassDB::bind_method(D_METHOD("get_closest_point", "position", "include_disabled"), &AStar2D::get_closest_point, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_id_path", "from_id", "to_id"), &AStar2D::get_id_path);
	ClassDB::bind_method(D_METHOD("get_point_path", "from_id", "to_id"), &AStar2D::get_point_path);
	ClassDB::bind_method(D_METHOD("clear"), &AStar2D::clear);
}

// ---------------------------------------------------------------------------
// AStarGrid2D
// ---------------------------------------------------------------------------
void AStarGrid2D::set_size(const Vector2 &p_size) {
	const int new_width = std::max(0, (int)p_size.x);
	const int new_height = std::max(0, (int)p_size.y);
	// Les cellules déjà réglées sont conservées quand la grille change de taille.
	std::vector<uint8_t> new_solid((size_t)(new_width * new_height), 0);
	std::vector<real_t> new_weights((size_t)(new_width * new_height), 1);
	for (int y = 0; y < std::min(height, new_height); y++) {
		for (int x = 0; x < std::min(width, new_width); x++) {
			new_solid[(size_t)(y * new_width + x)] = solid[(size_t)(y * width + x)];
			new_weights[(size_t)(y * new_width + x)] = weights[(size_t)(y * width + x)];
		}
	}
	width = new_width;
	height = new_height;
	solid.swap(new_solid);
	weights.swap(new_weights);
}

void AStarGrid2D::clear() {
	std::fill(solid.begin(), solid.end(), 0);
	std::fill(weights.begin(), weights.end(), (real_t)1);
}

void AStarGrid2D::set_point_solid(const Vector2 &p_id, bool p_solid) {
	ERR_FAIL_COND_MSG(!is_in_bounds(p_id), "AStarGrid2D : cellule hors de la grille.");
	solid[(size_t)((int)p_id.y * width + (int)p_id.x)] = p_solid ? 1 : 0;
}

bool AStarGrid2D::is_point_solid(const Vector2 &p_id) const {
	return is_in_bounds(p_id) && solid[(size_t)((int)p_id.y * width + (int)p_id.x)];
}

void AStarGrid2D::set_point_weight_scale(const Vector2 &p_id, real_t p_weight) {
	ERR_FAIL_COND_MSG(!is_in_bounds(p_id), "AStarGrid2D : cellule hors de la grille.");
	weights[(size_t)((int)p_id.y * width + (int)p_id.x)] = std::max((real_t)0, p_weight);
}

real_t AStarGrid2D::get_point_weight_scale(const Vector2 &p_id) const {
	return is_in_bounds(p_id) ? weights[(size_t)((int)p_id.y * width + (int)p_id.x)] : 1;
}

void AStarGrid2D::fill_solid_region(const Rect2 &p_region, bool p_solid) {
	for (int y = (int)p_region.position.y; y < (int)(p_region.position.y + p_region.size.y); y++) {
		for (int x = (int)p_region.position.x; x < (int)(p_region.position.x + p_region.size.x); x++) {
			if (_in(x, y)) {
				solid[(size_t)(y * width + x)] = p_solid ? 1 : 0;
			}
		}
	}
}

bool AStarGrid2D::_solve(const Vector2 &p_from, const Vector2 &p_to, std::vector<int> &r_cells) const {
	const int fx = (int)p_from.x, fy = (int)p_from.y, tx = (int)p_to.x, ty = (int)p_to.y;
	if (!_free(fx, fy) || !_free(tx, ty)) {
		return false;
	}
	const bool diagonals = diagonal_mode != DIAGONAL_MODE_NEVER;
	auto heuristic = [&](int p_x, int p_y) -> real_t {
		const real_t dx = (real_t)std::abs(p_x - tx), dy = (real_t)std::abs(p_y - ty);
		// Distance octile avec diagonales, de Manhattan sans.
		return diagonals ? (dx + dy) + ((real_t)1.41421356 - 2) * std::min(dx, dy) : dx + dy;
	};
	struct Entry {
		real_t f;
		real_t g; // à estimation égale, le chemin le plus avancé d'abord
		int cell;
		bool operator<(const Entry &p_other) const { return f != p_other.f ? f > p_other.f : g < p_other.g; }
	};
	const size_t count = (size_t)(width * height);
	std::vector<real_t> cost(count, -1);
	std::vector<int> previous(count, -1);
	std::vector<uint8_t> closed(count, 0);
	std::priority_queue<Entry> open;
	const int start = fy * width + fx, goal = ty * width + tx;
	cost[(size_t)start] = 0;
	open.push({ heuristic(fx, fy), 0, start });
	static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
	static const int DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
	while (!open.empty()) {
		const int current = open.top().cell;
		open.pop();
		if (current == goal) {
			for (int cell = goal; cell != -1; cell = previous[(size_t)cell]) {
				r_cells.push_back(cell);
			}
			std::reverse(r_cells.begin(), r_cells.end());
			return true;
		}
		if (closed[(size_t)current]) {
			continue;
		}
		closed[(size_t)current] = 1;
		const int cx = current % width, cy = current / width;
		for (int d = 0; d < (diagonals ? 8 : 4); d++) {
			const int nx = cx + DX[d], ny = cy + DY[d];
			if (!_free(nx, ny)) {
				continue;
			}
			if (d >= 4) {
				// Passage en diagonale entre deux cellules : selon le mode, les coins bloquent.
				const bool side_a = _free(cx + DX[d], cy), side_b = _free(cx, cy + DY[d]);
				if (diagonal_mode == DIAGONAL_MODE_AT_LEAST_ONE_WALKABLE && !side_a && !side_b) {
					continue;
				}
				if (diagonal_mode == DIAGONAL_MODE_ONLY_IF_NO_OBSTACLES && (!side_a || !side_b)) {
					continue;
				}
			}
			const int next = ny * width + nx;
			if (closed[(size_t)next]) {
				continue;
			}
			const real_t g = cost[(size_t)current] + (d >= 4 ? (real_t)1.41421356 : (real_t)1) * weights[(size_t)next];
			if (cost[(size_t)next] < 0 || g < cost[(size_t)next]) {
				cost[(size_t)next] = g;
				previous[(size_t)next] = current;
				open.push({ g + heuristic(nx, ny), g, next });
			}
		}
	}
	return false;
}

Array AStarGrid2D::get_id_path(const Vector2 &p_from, const Vector2 &p_to) const {
	Array out;
	std::vector<int> cells;
	if (_solve(p_from, p_to, cells)) {
		for (int cell : cells) {
			out.push_back(Variant(Vector2((real_t)(cell % width), (real_t)(cell / width))));
		}
	}
	return out;
}

Array AStarGrid2D::get_point_path(const Vector2 &p_from, const Vector2 &p_to) const {
	Array out;
	std::vector<int> cells;
	if (_solve(p_from, p_to, cells)) {
		for (int cell : cells) {
			out.push_back(Variant(get_point_position(Vector2((real_t)(cell % width), (real_t)(cell / width)))));
		}
	}
	return out;
}

void AStarGrid2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_size", "size"), &AStarGrid2D::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &AStarGrid2D::get_size);
	ClassDB::bind_method(D_METHOD("set_cell_size", "cell_size"), &AStarGrid2D::set_cell_size);
	ClassDB::bind_method(D_METHOD("get_cell_size"), &AStarGrid2D::get_cell_size);
	ClassDB::bind_method(D_METHOD("set_offset", "offset"), &AStarGrid2D::set_offset);
	ClassDB::bind_method(D_METHOD("get_offset"), &AStarGrid2D::get_offset);
	ClassDB::bind_method(D_METHOD("set_diagonal_mode", "mode"), &AStarGrid2D::set_diagonal_mode);
	ClassDB::bind_method(D_METHOD("get_diagonal_mode"), &AStarGrid2D::get_diagonal_mode);
	ClassDB::bind_method(D_METHOD("update"), &AStarGrid2D::update);
	ClassDB::bind_method(D_METHOD("is_in_bounds", "id"), &AStarGrid2D::is_in_bounds);
	ClassDB::bind_method(D_METHOD("set_point_solid", "id", "solid"), &AStarGrid2D::set_point_solid, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("is_point_solid", "id"), &AStarGrid2D::is_point_solid);
	ClassDB::bind_method(D_METHOD("set_point_weight_scale", "id", "weight_scale"), &AStarGrid2D::set_point_weight_scale);
	ClassDB::bind_method(D_METHOD("get_point_weight_scale", "id"), &AStarGrid2D::get_point_weight_scale);
	ClassDB::bind_method(D_METHOD("fill_solid_region", "region", "solid"), &AStarGrid2D::fill_solid_region, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("get_point_position", "id"), &AStarGrid2D::get_point_position);
	ClassDB::bind_method(D_METHOD("clear"), &AStarGrid2D::clear);
	ClassDB::bind_method(D_METHOD("get_id_path", "from_id", "to_id"), &AStarGrid2D::get_id_path);
	ClassDB::bind_method(D_METHOD("get_point_path", "from_id", "to_id"), &AStarGrid2D::get_point_path);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "size"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "cell_size"), "set_cell_size", "get_cell_size");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "offset"), "set_offset", "get_offset");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "diagonal_mode"), "set_diagonal_mode", "get_diagonal_mode");
	BIND_ENUM_CONSTANT(DIAGONAL_MODE_ALWAYS);
	BIND_ENUM_CONSTANT(DIAGONAL_MODE_NEVER);
	BIND_ENUM_CONSTANT(DIAGONAL_MODE_AT_LEAST_ONE_WALKABLE);
	BIND_ENUM_CONSTANT(DIAGONAL_MODE_ONLY_IF_NO_OBSTACLES);
}
