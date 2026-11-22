#include "container.h"

std::vector<Control *> Container::_get_layout_children() const {
	std::vector<Control *> out;
	for (Node *child : get_children_vector()) {
		Control *c = Object::cast_to<Control>(child);
		if (c && c->is_visible()) {
			out.push_back(c);
		}
	}
	return out;
}

void Container::_fit_child_in_rect(Control *p_child, const Rect2 &p_rect) const {
	const Size2 min = p_child->get_combined_minimum_size();
	Rect2 r = p_rect;
	auto fit_axis = [](int p_flags, real_t p_min, real_t &r_pos, real_t &r_size) {
		const real_t available = r_size;
		switch (p_flags) {
			case SIZE_SHRINK_BEGIN:
				r_size = std::min(p_min, available);
				break;
			case SIZE_SHRINK_CENTER:
				r_size = std::min(p_min, available);
				r_pos += std::floor((available - r_size) * 0.5f);
				break;
			case SIZE_SHRINK_END:
				r_size = std::min(p_min, available);
				r_pos += available - r_size;
				break;
			default:
				break; // remplit la zone
		}
	};
	fit_axis(p_child->get_h_size_flags(), min.x, r.position.x, r.size.x);
	fit_axis(p_child->get_v_size_flags(), min.y, r.position.y, r.size.y);
	p_child->set_rect_in_container(r);
}

void Container::queue_sort() {
	if (sorting) {
		// Demande reçue pendant un tri (un enfant a changé de taille minimale) : on recommencera.
		sort_again = true;
		return;
	}
	sorting = true;
	int passes = 0;
	do {
		sort_again = false;
		// Le conteneur ne peut pas être plus petit que son contenu : hors d'un conteneur parent,
		// il recalcule son rectangle (ses marges d'ancrage ne changent pas).
		Control *parent_control = Object::cast_to<Control>(get_parent());
		if (!parent_control || !parent_control->is_container()) {
			update_rect_from_anchors();
		}
		_sort_children();
	} while (sort_again && ++passes < 8);
	sorting = false;
	// Si la taille minimale a changé, le conteneur parent doit se réorganiser à son tour.
	const Size2 min = get_combined_minimum_size();
	if (min != last_minimum_size) {
		last_minimum_size = min;
		update_minimum_size();
	}
}

void Container::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE:
		case NOTIFICATION_READY:
		case NOTIFICATION_RESIZED:
		case NOTIFICATION_CHILD_ORDER_CHANGED:
		case NOTIFICATION_SORT_CHILDREN:
			queue_sort();
			break;
	}
}

// --- BoxContainer -------------------------------------------------------------

Size2 BoxContainer::get_minimum_size() const {
	Size2 total;
	int count = 0;
	for (Control *c : _get_layout_children()) {
		const Size2 min = c->get_combined_minimum_size();
		if (vertical) {
			total.x = std::max(total.x, min.x);
			total.y += min.y;
		} else {
			total.x += min.x;
			total.y = std::max(total.y, min.y);
		}
		count++;
	}
	if (count > 1) {
		(vertical ? total.y : total.x) += (real_t)(separation * (count - 1));
	}
	return total;
}

void BoxContainer::_sort_children() {
	const std::vector<Control *> children = _get_layout_children();
	if (children.empty()) {
		return;
	}
	const Size2 size = get_size();
	const real_t main_size = vertical ? size.y : size.x;
	const real_t cross_size = vertical ? size.x : size.y;

	real_t total_min = (real_t)(separation * ((int)children.size() - 1));
	int expand_count = 0;
	for (Control *c : children) {
		const Size2 min = c->get_combined_minimum_size();
		total_min += vertical ? min.y : min.x;
		if ((vertical ? c->get_v_size_flags() : c->get_h_size_flags()) == SIZE_EXPAND_FILL) {
			expand_count++;
		}
	}
	const real_t extra = std::max((real_t)0, main_size - total_min);
	real_t cursor = 0;
	if (expand_count == 0) {
		if (alignment == ALIGNMENT_CENTER) {
			cursor = std::floor(extra * 0.5f);
		} else if (alignment == ALIGNMENT_END) {
			cursor = extra;
		}
	}
	const real_t share = expand_count > 0 ? std::floor(extra / (real_t)expand_count) : 0;
	for (Control *c : children) {
		const Size2 min = c->get_combined_minimum_size();
		real_t length = vertical ? min.y : min.x;
		if ((vertical ? c->get_v_size_flags() : c->get_h_size_flags()) == SIZE_EXPAND_FILL) {
			length += share;
		}
		// Sur l'axe principal, l'enfant occupe exactement sa part ; l'axe transversal suit ses drapeaux.
		Rect2 slot = vertical ? Rect2(0, cursor, cross_size, length) : Rect2(cursor, 0, length, cross_size);
		const Size2 child_min = min;
		Rect2 r = slot;
		const int cross_flags = vertical ? c->get_h_size_flags() : c->get_v_size_flags();
		real_t &cross_pos = vertical ? r.position.x : r.position.y;
		real_t &cross_len = vertical ? r.size.x : r.size.y;
		const real_t cross_min = vertical ? child_min.x : child_min.y;
		if (cross_flags == SIZE_SHRINK_BEGIN) {
			cross_len = std::min(cross_min, cross_size);
		} else if (cross_flags == SIZE_SHRINK_CENTER) {
			cross_len = std::min(cross_min, cross_size);
			cross_pos = std::floor((cross_size - cross_len) * 0.5f);
		} else if (cross_flags == SIZE_SHRINK_END) {
			cross_len = std::min(cross_min, cross_size);
			cross_pos = cross_size - cross_len;
		}
		c->set_rect_in_container(r);
		cursor += length + (real_t)separation;
	}
}

void BoxContainer::set_separation(int p_separation) {
	separation = p_separation;
	queue_sort();
}

void BoxContainer::set_alignment(int p_alignment) {
	alignment = p_alignment;
	queue_sort();
}

void BoxContainer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_separation", "separation"), &BoxContainer::set_separation);
	ClassDB::bind_method(D_METHOD("get_separation"), &BoxContainer::get_separation);
	ClassDB::bind_method(D_METHOD("set_alignment", "alignment"), &BoxContainer::set_alignment);
	ClassDB::bind_method(D_METHOD("get_alignment"), &BoxContainer::get_alignment);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "separation", PROPERTY_HINT_RANGE, "0,200,1"), "set_separation", "get_separation");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "alignment", PROPERTY_HINT_ENUM, "Begin,Center,End"), "set_alignment", "get_alignment");
	BIND_ENUM_CONSTANT(ALIGNMENT_BEGIN);
	BIND_ENUM_CONSTANT(ALIGNMENT_CENTER);
	BIND_ENUM_CONSTANT(ALIGNMENT_END);
}

// --- MarginContainer ----------------------------------------------------------

Size2 MarginContainer::get_minimum_size() const {
	Size2 max;
	for (Control *c : _get_layout_children()) {
		const Size2 min = c->get_combined_minimum_size();
		max.x = std::max(max.x, min.x);
		max.y = std::max(max.y, min.y);
	}
	return max + Size2((real_t)(margins[SIDE_LEFT] + margins[SIDE_RIGHT]), (real_t)(margins[SIDE_TOP] + margins[SIDE_BOTTOM]));
}

void MarginContainer::_sort_children() {
	const Size2 size = get_size();
	const Rect2 inner((real_t)margins[SIDE_LEFT], (real_t)margins[SIDE_TOP],
			std::max((real_t)0, size.x - (real_t)(margins[SIDE_LEFT] + margins[SIDE_RIGHT])),
			std::max((real_t)0, size.y - (real_t)(margins[SIDE_TOP] + margins[SIDE_BOTTOM])));
	for (Control *c : _get_layout_children()) {
		_fit_child_in_rect(c, inner);
	}
}

void MarginContainer::set_margin(int p_side, int p_margin) {
	margins[p_side] = p_margin;
	queue_sort();
}

void MarginContainer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_margin_left", "margin"), &MarginContainer::set_margin_left);
	ClassDB::bind_method(D_METHOD("get_margin_left"), &MarginContainer::get_margin_left);
	ClassDB::bind_method(D_METHOD("set_margin_top", "margin"), &MarginContainer::set_margin_top);
	ClassDB::bind_method(D_METHOD("get_margin_top"), &MarginContainer::get_margin_top);
	ClassDB::bind_method(D_METHOD("set_margin_right", "margin"), &MarginContainer::set_margin_right);
	ClassDB::bind_method(D_METHOD("get_margin_right"), &MarginContainer::get_margin_right);
	ClassDB::bind_method(D_METHOD("set_margin_bottom", "margin"), &MarginContainer::set_margin_bottom);
	ClassDB::bind_method(D_METHOD("get_margin_bottom"), &MarginContainer::get_margin_bottom);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "margin_left"), "set_margin_left", "get_margin_left");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "margin_top"), "set_margin_top", "get_margin_top");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "margin_right"), "set_margin_right", "get_margin_right");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "margin_bottom"), "set_margin_bottom", "get_margin_bottom");
}

// --- CenterContainer ----------------------------------------------------------

Size2 CenterContainer::get_minimum_size() const {
	Size2 max;
	for (Control *c : _get_layout_children()) {
		const Size2 min = c->get_combined_minimum_size();
		max.x = std::max(max.x, min.x);
		max.y = std::max(max.y, min.y);
	}
	return max;
}

void CenterContainer::_sort_children() {
	const Size2 size = get_size();
	for (Control *c : _get_layout_children()) {
		const Size2 min = c->get_combined_minimum_size();
		c->set_rect_in_container(Rect2(((size - min) * 0.5f).floor(), min));
	}
}
