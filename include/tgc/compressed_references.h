/*
 * COPYRIGHT_NOTICE_1
 */

#ifndef _compressed_references_h
#define _compressed_references_h

#include "tgc/gc_v4.h"

#ifdef ALLOW_COMPRESSED_REFS

// Set the DISALLOW_RUNTIME_SELECTION_OF_COMPRESSED_REFERENCES define to force a build that supports either
// compressed references or raw references, but not both.
// Also set the gc_references_are_compressed define to either true or false.
//#define DISALLOW_RUNTIME_SELECTION_OF_COMPRESSED_REFERENCES

#ifdef DISALLOW_RUNTIME_SELECTION_OF_COMPRESSED_REFERENCES
#define gc_references_are_compressed false
#else // !DISALLOW_RUNTIME_SELECTION_OF_COMPRESSED_REFERENCES
extern bool gc_references_are_compressed;
#endif // !DISALLOW_RUNTIME_SELECTION_OF_COMPRESSED_REFERENCES

#endif // ALLOW_COMPRESSED_REFS

//
// The Slot data structure represents a pointer to a heap location that contains
// a reference field.  It is packaged this way because the heap location may
// contain either a raw pointer or a compressed pointer, depending on command line
// options.
//
// The "check_within_heap" parameter to the constructor and the set() routine
// should be set to false when doing the full heap verification, when creating
// a slot that points to an object that is a copy of something that was originally
// in the heap.
//
// Code originally of the form:
//     Partial_Reveal_Object **p_slot = foo ;
//     ... *p_slot ...
// should be changed to this:
//     Slot p_slot(foo);
//     ... p_slot.dereference() ...
//
// The dereference() method still returns a raw Partial_Reveal_Object* pointer,
// which can have vt() and other operations performed on it as before.
//
class Slot {
private:
    union {
        Partial_Reveal_Object **raw;
        uint32 *cmpr;
        void *value;
    } content;
    static void *cached_heap_base;
    static void *cached_heap_ceiling;

public:
	Partial_Reveal_Object *base;

    unsigned get_offset(void) const {
        assert(base);
        return (uintptr_t)content.value - (uintptr_t)base;
    }

    Slot(void *v, bool check_within_heap=true) {
        set(v, check_within_heap);
    }

	void * get_address(void) {
		return (void*)&content.value;
	}

    // Sets the raw value of the slot.
    void *set(void *v, bool check_within_heap=true) {
        content.value = v;
        if (v != NULL) {
#ifdef FILTER_NON_HEAP
            check_within_heap=false;
#endif // FILTER_NON_HEAP
            // Make sure the value points somewhere within the heap.
            if (check_within_heap) {
                assert(v >= cached_heap_base);
                assert(v < cached_heap_ceiling);
                // Make sure the value pointed to points somewhere else within the heap.
                assert(dereference() == managed_null() ||
                    (dereference() >= cached_heap_base && dereference() < cached_heap_ceiling));
            }
        }
        return v;
    }


    // Dereferences the slot and converts it to a raw object pointer.
    Partial_Reveal_Object *dereference() {
		Partial_Reveal_Object *ret = NULL;
#ifdef ALLOW_COMPRESSED_REFS
        if (gc_references_are_compressed) {
            assert(content.cmpr != NULL);
			ret = (Partial_Reveal_Object *) (*content.cmpr + (Byte*)cached_heap_base);
        } else {
            assert(content.raw != NULL);
            ret = *content.raw;
        }
#else
        assert(content.raw != NULL);
        ret = *content.raw;
#endif
#ifndef DISALLOW_TAGGED_POINTERS
		// If either of the lower two bits are set then this is not a valid pointer but
		// must be being used by the language for some alternative representation.
		// Used for P tagged integer rational representation.
		if(!is_tagged_pointer(ret)) {
			// Returning NULL will treat this slot as if it were a NULL pointer which
			// is accurate since it doesn't pointer to anything.
			ret = NULL;
		}
#endif // DISALLOW_TAGGED_POINTERS
		return ret;
    }


    // Returns the raw pointer value.
    void *get_value() { return content.value; }


    // Writes a new object reference into the slot.
    void update(Partial_Reveal_Object *obj) {
        //assert(obj != NULL);
        //assert(obj != managed_null());
		if(obj < cached_heap_base) {
			assert(0);
		}
        if(obj > cached_heap_ceiling) {
			assert(0);
		}

#ifdef ALLOW_COMPRESSED_REFS
        if (gc_references_are_compressed) {
            *content.cmpr = (uint32) ((Byte *)obj - (Byte*)cached_heap_base);
        } else {
            *content.raw = obj;
        }
#else
        *content.raw = obj;
#endif
    }

    void unchecked_update(Partial_Reveal_Object *obj) {
#ifdef ALLOW_COMPRESSED_REFS
        if (gc_references_are_compressed) {
            *content.cmpr = (uint32) ((Byte *)obj - (Byte*)cached_heap_base);
        } else {
            *content.raw = obj;
        }
#else
            *content.raw = obj;
#endif
    }


    // Returns true if the slot points to a null reference.
    bool is_null() {
#ifdef ALLOW_COMPRESSED_REFS
        if (gc_references_are_compressed) {
            assert(content.cmpr != NULL);
            return (content.cmpr == NULL || *content.cmpr == 0
#ifndef DISALLOW_TAGGED_POINTERS
				// If lower two bits are set then this is a tagged representation and not really a pointer and thus is NULL.
				|| !is_object_pointer((Partial_Reveal_Object*)*content.cmpr)
//				|| (POINTER_SIZE_INT)(*content.raw) & 0x3
#endif // DISALLOW_TAGGED_POINTERS
				);
        } else {
            assert(content.raw != NULL);
            return (content.raw == NULL || *content.raw == NULL
#ifndef DISALLOW_TAGGED_POINTERS
				// If lower two bits are set then this is a tagged representation and not really a pointer and thus is NULL.
				|| !is_object_pointer((Partial_Reveal_Object*)*content.raw)
//				|| (POINTER_SIZE_INT)(*content.raw) & 0x3
#endif // DISALLOW_TAGGED_POINTERS
				);
        }
#else
        assert(content.raw != NULL);
        return (content.raw == NULL
#ifdef DISALLOW_TAGGED_POINTERS
                || *content.raw == NULL
#else
				// If lower two bits are set then this is a tagged representation and not really a pointer and thus is NULL.
				|| !is_object_pointer((Partial_Reveal_Object*)*content.raw)
//				|| (POINTER_SIZE_INT)(*content.raw) & 0x3
#endif // DISALLOW_TAGGED_POINTERS
				);
#endif
    }


    // Returns the raw value of a managed null, which may be different
    // depending on whether compressed references are used.
    static void *managed_null() {
#ifdef ALLOW_COMPRESSED_REFS
        return (gc_references_are_compressed ? cached_heap_base : NULL);
#else
        return NULL;
#endif
    }


    // Returns the maximum heap size supported, which depends on the
    // configuration of compressed references.
    // If not using compressed references, the heap size is basically unlimited.
    // If using compressed references, normally the heap size is limited
    // to 4GB, unless other tricks are used to extend it to 8/16/32GB.
    static uint64 max_supported_heap_size() {
#ifdef ALLOW_COMPRESSED_REFS
        if (gc_references_are_compressed) {
            return __INT64_C(0x100000000); // 4GB
        } else {
            return ~((uint64)0); // unlimited
        }
#else
        return ~((uint64)0); // unlimited
#endif
    }


    // Returns the additional amount of memory that should be requested from
    // VirtualAlloc so that the heap can be aligned at a 4GB boundary.
    // The advantage of such alignment is tbat tbe JIT-generated code doesn't
    // need to subtract the heap base when compressing a reference; instead,
    // it can just store the lower 32 bits.
    static uint64 additional_padding_for_heap_alignment() {
#ifdef ALLOW_COMPRESSED_REFS
        if (gc_references_are_compressed) {
            return __INT64_C(0x100000000); // 4GB
        } else {
            return 0;
        }
#else
        return 0;
#endif
    }


    // Initializes the cached heap base and ceiling to the specified values.
    // Other required initialization may be done here as well.
    static void init(void *base, void *ceiling) {
        cached_heap_base = base;
        cached_heap_ceiling = ceiling;
    }
};


#endif // _compressed_references_h
