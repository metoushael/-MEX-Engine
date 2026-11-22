// RefCounted + Ref<T> : gestion mémoire par comptage de références (ressources, événements...).
#pragma once

#include "core/object/object.h"

class RefCounted : public Object {
	GDCLASS(RefCounted, Object)

	int refcount = 0;

protected:
	static void _bind_methods();

public:
	void reference() { refcount++; }
	bool unreference() { return --refcount <= 0; } // vrai => l'objet doit être détruit
	int get_reference_count() const { return refcount; }

	RefCounted() { _is_ref_counted = true; }
};

template <class T>
class Ref {
	T *reference = nullptr;

	void _ref(T *p_ptr) {
		if (p_ptr == reference) {
			return;
		}
		if (p_ptr) {
			p_ptr->reference();
		}
		T *old = reference;
		reference = p_ptr;
		if (old && old->unreference()) {
			memdelete(old);
		}
	}

public:
	Ref() {}
	Ref(T *p_ptr) { _ref(p_ptr); }
	Ref(const Ref &p_from) { _ref(p_from.reference); }
	template <class U>
	Ref(const Ref<U> &p_from) { _ref(Object::cast_to<T>(static_cast<Object *>(p_from.ptr()))); }
	~Ref() { unref(); }

	Ref &operator=(const Ref &p_from) {
		_ref(p_from.reference);
		return *this;
	}
	template <class U>
	Ref &operator=(const Ref<U> &p_from) {
		_ref(Object::cast_to<T>(static_cast<Object *>(p_from.ptr())));
		return *this;
	}

	T *operator->() const { return reference; }
	T *operator*() const { return reference; }
	T *ptr() const { return reference; }
	bool is_valid() const { return reference != nullptr; }
	bool is_null() const { return reference == nullptr; }
	bool operator==(const Ref &p_other) const { return reference == p_other.reference; }
	bool operator!=(const Ref &p_other) const { return reference != p_other.reference; }
	bool operator<(const Ref &p_other) const { return reference < p_other.reference; }

	void unref() { _ref(nullptr); }
	void instantiate() { _ref(memnew(T)); }
};
