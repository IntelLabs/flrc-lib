/*
 * COPYRIGHT_NOTICE_1
 */

#ifndef _gc_header_H_
#define _gc_header_H_

#include "iflclibconfig.h"
#include "pgc/pgc.h"
#ifdef WIN32
#include <Windows.h>
#endif // WIN32
#include <stdio.h>

typedef int8_t   int8;
typedef uint8_t  uint8;
typedef uint8_t  g1;
typedef int16_t  int16;
typedef uint16_t uint16;
typedef uint16_t g2;
typedef int32_t  int32;
typedef uint32_t uint32;
typedef uint32_t g4;
typedef int64_t  int64;
typedef uint64_t uint64;
typedef uint64_t g8;
typedef uint64_t __uint64;
typedef float    f4;
typedef double   f8;

#ifndef LONG
#define LONG long
#endif
#ifndef PVOID
#define PVOID void *
#endif
#ifndef IN
#define IN
#endif
#ifndef OUT
#define OUT
#endif

#ifdef POINTER64
#define GC_OBJECT_ALIGNMENT 8
#else
#define GC_OBJECT_ALIGNMENT 4
#endif // POINTER64

#if 0
#define dprintf printf
#else
inline void ignore_printf(...) { return;}
#define dprintf ignore_printf
#endif

# if __WORDSIZE == 64 || defined(WIN32)
#  define __INT64_C(c)  c ## L
#  define __UINT64_C(c) c ## UL
# else
#  define __INT64_C(c)  c ## LL
#  define __UINT64_C(c) c ## ULL
# endif

#define CL_PROP_ALIGNMENT_MASK      0x00FFF
#define CL_PROP_NON_REF_ARRAY_MASK  0x01000
#define CL_PROP_ARRAY_MASK          0x02000
#define CL_PROP_PINNED_MASK         0x04000
#define CL_PROP_FINALIZABLE_MASK    0x08000

#if 0
enum safepoint_state {
    nill = 0,
    enumerate_the_universe
};
#endif

#ifdef CONCURRENT
enum CONCURRENT_GC_STATE {
	CONCURRENT_IDLE     = 0,
	CONCURRENT_MARKING  = 1,
	CONCURRENT_SWEEPING = 2
};
#endif // CONCURRENT


typedef struct ManagedObject HeapObject;
extern PrtTaskHandle active_gc_thread;
typedef int Loader_Exception;

#include <assert.h>
#include "tgc/gcv4_synch.h"

#include "prt/prtcodegenerator.h"
#ifdef HAVE_PTHREAD_H
#include <pthread.h>
#else  // HAVE_PTHREAD_H
#include <mcrt_for_orp.h>
#endif // HAVE_PTHREAD_H
#ifdef CONCURRENT
#include <set>
#endif // CONCURRENT

#ifdef __GNUC__
#include <unistd.h>
#include <stdlib.h>
#endif // __GNUC__

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#ifndef ORP_POSIX
#pragma warning( disable : 4786 )
#endif

class Hash_Table;
extern "C" unsigned g_use_pub_priv;

//
// This Hash_Table has an entry per loaded class.
// It is used for determining valid vtable pointers
// when examining candidate objects.
//
extern Hash_Table *p_loaded_vtable_directory;

// Define USE_COMPRESSED_VTABLE_POINTERS here to enable compressed vtable
// pointers within objects.
#ifdef _IA64_
#define USE_COMPRESSED_VTABLE_POINTERS
#endif // _IA64_

//
// Use the contention bit of object header to lock stuff for now...Is this OKAY???
//
// This bit is also used by the Mississippi Delta code to distinguish object that
// need to be monitored during the heap traversal.
//
const int GC_OBJECT_MARK_BIT_MASK = 0x2;
const int FORWARDING_BIT_MASK = 0x1;


//
// This is thread local per Java/VM thread and is used by the GC to stuff in whatever it needs.
// Current free is the free point in the curr_alloc_block. current_ceiling is the ceiling of that area.
// This will be the first two works in this structure for IA32 but might be moved into reserved registers
// for IPF.
//

typedef void *Thread_Handle; // Used to communicate with VM about threads.

#ifdef USE_COMPRESSED_VTABLE_POINTERS
typedef uint32 Obj_Info_Type;
#else // !USE_COMPRESSED_VTABLE_POINTERS
typedef POINTER_SIZE_INT Obj_Info_Type;
#endif // !USE_COMPRESSED_VTABLE_POINTERS

struct Partial_Reveal_VTable;
struct Partial_Reveal_Object;

class ForwardedObject {
public:
	Partial_Reveal_VTable *vt;
	Partial_Reveal_Object *new_location;

	ForwardedObject(Partial_Reveal_VTable *v,Partial_Reveal_Object *n) : vt(v), new_location(n) {}
};

#ifdef CONCURRENT_DEBUG_2
extern FILE *cdump;
extern FILE *cgcdump;
#endif

typedef struct Partial_Reveal_Object {
#ifdef USE_COMPRESSED_VTABLE_POINTERS
    uint32 vt_offset;
private:
    Obj_Info_Type obj_info;
public:

    Obj_Info_Type get_obj_info() { return obj_info; }
    void set_obj_info(Obj_Info_Type new_obj_info) { obj_info = new_obj_info; }
    Obj_Info_Type * obj_info_addr() { return &obj_info; }

    struct Partial_Reveal_VTable *vt() { assert(vt_offset); return (struct Partial_Reveal_VTable *) (vt_offset + vtable_base); }
    void set_vtable(Allocation_Handle ah) { assert(ah < 1000000); vt_offset = (uint32)ah; }

private:
    // This function returns the number of bits that an address may be
    // right-shifted after subtracting the heap base, to compress it to
    // 32 bits.  In the current implementation, objects are aligned at
    // 8-bit(??RLH you mean 8 byte right??) boundaries, leaving 3 lower bits of the address that are
    // guaranteed to be 0.  However, one bit must be reserved to mark
    // the value as a forwarding pointer.
    static unsigned forwarding_pointer_compression_shift() {
        return 2;
    }
public:

    struct Partial_Reveal_Object *get_forwarding_pointer() {
        assert(obj_info & FORWARDING_BIT_MASK);
        POINTER_SIZE_INT offset = ((POINTER_SIZE_INT)(obj_info & ~FORWARDING_BIT_MASK)) << forwarding_pointer_compression_shift();
        return (struct Partial_Reveal_Object *) (heap_base + offset);
    }

    void set_forwarding_pointer(void *dest) {
//        assert(!(obj_info & FORWARDING_BIT_MASK)); // Delta Force uses the FORWARDING_BIT_MASK to reserve the right to set_forwarding_pointer
        assert((POINTER_SIZE_INT)dest % (1<<forwarding_pointer_compression_shift()) == 0);
        obj_info = (Obj_Info_Type)(((POINTER_SIZE_INT)dest - heap_base) >> forwarding_pointer_compression_shift()) | FORWARDING_BIT_MASK;
    }

    Obj_Info_Type compare_exchange(Obj_Info_Type new_value, Obj_Info_Type old_value) {
        return (Obj_Info_Type) InterlockedCompareExchange((long *) &obj_info, (long) new_value, (long) old_value);
    }

    bool set_forwarding_pointer_atomic(void *dest) {
        Obj_Info_Type old_val = obj_info;
        Obj_Info_Type new_val = (Obj_Info_Type)(((POINTER_SIZE_INT)dest - heap_base) >> forwarding_pointer_compression_shift()) | FORWARDING_BIT_MASK;
        if ((old_val & FORWARDING_BIT_MASK) == FORWARDING_BIT_MASK) {
            return false; // Somebody else grabbed the forwarding pointer.
        }
        Obj_Info_Type returned_val = compare_exchange(new_val, old_val);
        return (returned_val == old_val); // if compare exchange fails return false otherwise true.
    }
    static bool use_compressed_vtable_pointers() { return true; }
    static unsigned object_overhead_bytes() { return sizeof(uint32) + sizeof(Obj_Info_Type); }
    static unsigned vtable_bytes() { return sizeof(uint32); }
    static struct Partial_Reveal_VTable *allocation_handle_to_vtable(Allocation_Handle ah) {
        return (Partial_Reveal_VTable *) ((POINTER_SIZE_INT)ah + vtable_base);
    }
    static uint64 max_supported_heap_size() { return (0x100000000) << forwarding_pointer_compression_shift(); }
#else // !USE_COMPRESSED_VTABLE_POINTERS
    struct Partial_Reveal_VTable *vt_raw;
#ifdef USE_OBJ_INFO
    Obj_Info_Type obj_info;

    Obj_Info_Type get_obj_info() { return obj_info; }
    void set_obj_info(Obj_Info_Type new_obj_info) { obj_info = new_obj_info; }
    Obj_Info_Type * obj_info_addr() { return &obj_info; }
#endif // USE_OBJ_INFO

    void set_vtable(Allocation_Handle ah) { vt_raw = (struct Partial_Reveal_VTable *)ah; }

    static bool use_compressed_vtable_pointers() { return false; }

    struct Partial_Reveal_VTable *vt() {
        if(!vt_raw) {
    	    assert(vt_raw);
        }
    	if((uintptr_t)vt_raw & FORWARDING_BIT_MASK) {
		    ForwardedObject *fo = (ForwardedObject*)((uintptr_t)vt_raw & (~(FORWARDING_BIT_MASK | GC_OBJECT_MARK_BIT_MASK)));
			return fo->vt;
    	} else {
    		return (Partial_Reveal_VTable*)((uintptr_t)vt_raw & (~GC_OBJECT_MARK_BIT_MASK));
    	}
    }

    struct Partial_Reveal_Object *get_forwarding_pointer() {
        //assert(!((POINTER_SIZE_INT)vt_raw & GC_OBJECT_MARK_BIT_MASK));
        assert((uintptr_t)vt_raw & FORWARDING_BIT_MASK);
	    ForwardedObject *fo = (ForwardedObject*)((uintptr_t)vt_raw & (~(FORWARDING_BIT_MASK | GC_OBJECT_MARK_BIT_MASK)));
		return fo->new_location;
    }

    void *get_raw_forwarding_pointer(void) {
        assert(!((uintptr_t)vt_raw & GC_OBJECT_MARK_BIT_MASK));
        assert((uintptr_t)vt_raw & FORWARDING_BIT_MASK);
	    return (void *)((uintptr_t)vt_raw & (~FORWARDING_BIT_MASK));
    }

    void *get_vt_no_low_bits(void) {
		return (Partial_Reveal_VTable*)((uintptr_t)vt_raw & (~(FORWARDING_BIT_MASK | GC_OBJECT_MARK_BIT_MASK)));
    }

    void set_forwarding_pointer(void *dest) {
        // This assert will catch most cases where the forwarding pointer is being
        // set when the object has not been copied to its new location.
#ifdef CONCURRENT
        assert(0);
#endif
        vt_raw = (Partial_Reveal_VTable*)((uintptr_t)dest | FORWARDING_BIT_MASK);
    }

    static unsigned object_overhead_bytes() { return sizeof(struct Partial_Reveal_VTable *); }

    bool isForwarded(void) {
        return (uintptr_t)vt_raw & FORWARDING_BIT_MASK;
    }

	void setLowFlag(void) {
        vt_raw = (Partial_Reveal_VTable*)((uintptr_t)vt_raw | FORWARDING_BIT_MASK);
	}

	void clearLowFlag(void) {
        vt_raw = (Partial_Reveal_VTable*)((uintptr_t)vt_raw & ~FORWARDING_BIT_MASK);
	}

	void clearLowBits(void) {
        vt_raw = (Partial_Reveal_VTable*)((uintptr_t)vt_raw & ~(FORWARDING_BIT_MASK | GC_OBJECT_MARK_BIT_MASK));
	}

	bool isLowFlagSet(void) {
        return (uintptr_t)vt_raw & FORWARDING_BIT_MASK;
	}

    bool isMarked(void) {
        return ((uintptr_t)vt_raw & GC_OBJECT_MARK_BIT_MASK) != 0;
    }

    void mark(void) {
        vt_raw = (Partial_Reveal_VTable*)((uintptr_t)vt_raw | GC_OBJECT_MARK_BIT_MASK);
    }

	bool atomic_mark(void) {
        assert(!isForwarded());
		Partial_Reveal_VTable *old_vt = vt_raw;
#ifdef CONCURRENT_DEBUG_2
        if(old_vt == NULL) {
            printf("vt_raw is NULL in atomic mark %p.\n",this);
            fclose(cdump);
            fclose(cgcdump);
            return true;
        }
#endif
		Partial_Reveal_VTable *new_vt = (Partial_Reveal_VTable*)((uintptr_t)old_vt | GC_OBJECT_MARK_BIT_MASK);
        if ((LONG)InterlockedCompareExchange((LONG *)&vt_raw, (LONG)new_vt, (LONG)old_vt) == (LONG)old_vt) {
		    return true;
		} else {
			return false;
		}
	}

    void unmark(void) {
        vt_raw = (Partial_Reveal_VTable*)((uintptr_t)vt_raw & ~GC_OBJECT_MARK_BIT_MASK);
    }

    static unsigned vtable_bytes() { return sizeof(struct Partial_Reveal_VTable *); }
    static struct Partial_Reveal_VTable *allocation_handle_to_vtable(Allocation_Handle ah) {
        return (Partial_Reveal_VTable *) ah;
    }
    static uint64 max_supported_heap_size() { return ~((uint64)0); }
#endif // !USE_COMPRESSED_VTABLE_POINTERS

    static POINTER_SIZE_INT vtable_base;
    static POINTER_SIZE_INT heap_base;
} Partial_Reveal_Object;


#ifdef USE_COMPRESSED_VTABLE_POINTERS
inline Allocation_Handle object_get_allocation_handle (Partial_Reveal_Object *obj) {
    return (Allocation_Handle)obj->vt_offset;
}
#else // !USE_COMPRESSED_VTABLE_POINTERS
inline Allocation_Handle object_get_allocation_handle (Partial_Reveal_Object *obj) {
    return (uintptr_t)obj->vt_raw;
}
#endif // !USE_COMPRESSED_VTABLE_POINTERS


enum gc_enumeration_state {
	gc_enumeration_state_bogus,
	gc_enumeration_state_stopped,
	gc_enumeration_state_running,
};

typedef struct Arena {
	struct Arena	*next_arena;// next arena
	char			*next_byte;	// next byte available in arena
	char			*last_byte;	// end of arena	space
	char			bytes[1];	// start of arena space
} Arena;

static const unsigned int default_arena_size = 1024;
//
// given an empty space of memory, make it into an arena of given size.
//
Arena *init_arena(void *space,Arena *next_arena,/*unsigned*/ size_t size);
//
// malloc (free) an arena from the global (thread-safe) heap
//
Arena *alloc_arena(Arena *next, /*unsigned*/ size_t size);
Arena *free_arena(Arena *a);
void free_all_arenas(Arena *a);
Arena *free_all_but_one_arena(Arena *a);
//
// allocate and return space from Arena
//
void *arena_alloc_space(Arena *arena, /*unsigned*/ size_t size);

typedef struct {
    void *tls_current_free;
    void *tls_current_ceiling;
    void *chunk;
    void *curr_alloc_block;
} GC_Nursery_Info;

template <class T>
class ExpandInPlaceArray;

class pn_info;
class Slot;
class external_pointer;

typedef struct {
    void *tls_current_free;
    void *tls_current_ceiling;
    void *start; // the start of the allocation area, never changes for this thread.
#ifdef CONCURRENT
	enum CONCURRENT_GC_STATE concurrent_state_copy;
	enum CONCURRENT_GC_STATE current_state;
#endif // CONCURRENT
	pn_info *local_gc_info;
#ifdef USE_STL_PN_ALLOCATOR
	Arena *tls_arena;
#endif // USE_STL_PN_ALLOCATOR
#if defined _DEBUG
	unsigned num_write_barriers;
	unsigned slot_outside_nursery;
	unsigned value_inside_nursery;
	unsigned useful;
	unsigned num_survivors;
	unsigned size_survivors;
	__uint64 space_used;
	__uint64 size_objects_escaping;
	unsigned num_promote_on_escape;
	unsigned num_promote_on_escape_interior;
	unsigned frame_count;
#endif // _DEBUG

	external_pointer * allocate_external_pointer(void);
    bool is_duplicate_last_external_pointer(Partial_Reveal_Object **slot);
} GC_Small_Nursery_Info;

extern "C" unsigned local_nursery_size;

void reset_nursery(GC_Nursery_Info *nursery);
void reset_one_thread_nursery(GC_Nursery_Info *nursery);

struct BothNurseries {
	GC_Small_Nursery_Info local_nursery;
    GC_Nursery_Info nursery;
	GC_Nursery_Info immutable_nursery;
#ifdef PUB_PRIV
	GC_Nursery_Info level_1_mutable_nursery;
	GC_Nursery_Info level_1_immutable_nursery;
#endif // PUB_PRIV

    void reset(void) {
        reset_one_thread_nursery(&nursery);
        reset_one_thread_nursery(&immutable_nursery);
#ifdef PUB_PRIV
        reset_one_thread_nursery(&level_1_mutable_nursery);
	    reset_one_thread_nursery(&level_1_immutable_nursery);
#endif // PUB_PRIV
    }
};

union CombinedNurseries {
	struct BothNurseries both;
	GC_Nursery_Info regular_nursery;
};

// This is thread local per Java/VM thread and is used by the GC to stuff in whatever it needs.
typedef struct GC_Thread_Info {
protected:
	CombinedNurseries cn;
public:
    Thread_Handle thread_handle;               // This thread;
    //Thread_Handle p_active_thread;           // The next active thread. same as this->p_active_gc_thread_info->thread_handle
    GC_Thread_Info *p_active_gc_thread_info;   // The gc info area associated with the next active thread.
    gc_enumeration_state enumeration_state;
//	bool m_entering_gc_lock;
    ExpandInPlaceArray<Managed_Object_Handle *> *elder_roots_to_younger;

    void reset_nurseries(void) {
        if(local_nursery_size) {
            cn.both.reset();
        } else {
            reset_one_thread_nursery(&cn.regular_nursery);
        }
    }

	GC_Small_Nursery_Info * get_private_nursery(void) {
		if(local_nursery_size) {
			return &(cn.both.local_nursery);
		} else {
			printf("Tried to get a private nursery when private nurseries not enabled.\n");
			exit(17054);
		}
	}

    GC_Nursery_Info * get_primary_nursery(void) {
        return &(cn.regular_nursery);
    }

#ifdef PUB_PRIV
	GC_Nursery_Info * get_level_1_mutable_nursery(void) {
		if(local_nursery_size) {
			return &(cn.both.level_1_mutable_nursery);
		} else {
			printf("Tried to get an immutable nursery when private nurseries not enabled.\n");
			exit(17054);
		}
	}

	GC_Nursery_Info * get_level_1_immutable_nursery(void) {
		if(local_nursery_size) {
			return &(cn.both.level_1_immutable_nursery);
		} else {
			printf("Tried to get an immutable nursery when private nurseries not enabled.\n");
			exit(17054);
		}
	}

	GC_Nursery_Info * get_nursery(void) {
        if(g_use_pub_priv) {
            return get_level_1_mutable_nursery();
        } else {
            return get_public_nursery();
        }
	}

	GC_Nursery_Info * get_immutable_nursery(void) {
        if(g_use_pub_priv) {
            return get_level_1_immutable_nursery();
        } else {
            return get_public_nursery();
        }
	}

	GC_Nursery_Info * get_public_nursery(void) {
		if(local_nursery_size) {
			return &(cn.both.nursery);
		} else {
			return &(cn.regular_nursery);
		}
	}

	GC_Nursery_Info * get_public_immutable_nursery(void) {
		if(local_nursery_size) {
			return &(cn.both.immutable_nursery);
		} else {
			printf("Tried to get an immutable nursery when private nurseries not enabled.\n");
			exit(17054);
		}
	}
#else  // PUB_PRIV
	GC_Nursery_Info * get_nursery(void) {
		if(local_nursery_size) {
			return &(cn.both.nursery);
		} else {
			return &(cn.regular_nursery);
		}
	}

	GC_Nursery_Info * get_public_nursery(void) {
        return get_nursery();
    }

	GC_Nursery_Info * get_immutable_nursery(void) {
		if(local_nursery_size) {
			return &(cn.both.immutable_nursery);
		} else {
			printf("Tried to get an immutable nursery when private nurseries not enabled.\n");
			exit(17054);
		}
	}

    GC_Nursery_Info * get_public_immutable_nursery(void) {
        return get_immutable_nursery();
    }
#endif // PUB_PRIV
} GC_Thread_Info;

extern "C" GC_Thread_Info * get_gc_thread_local(void);
extern "C" GC_Thread_Info * orp_local_to_gc_local(void *tp);

struct GC_VTable_Info {
    unsigned int gc_object_has_slots;
    unsigned int gc_number_of_slots;

    // If this object type demonstrates temporal locality (is fused somehow) with some
    // other object type then this is non-zero and is the offset in this object of the offset
    // that that should be used to reach the fused object.

    // if gc_fuse_info == NULL: don't fuse
    // else
    //    it stores fused field offset array, end with 0
	unsigned int *gc_fuse_info;
	unsigned int delinquent_type_id;

    uint32 gc_class_properties;    // This is the same as class_properties in ORP's VTable.

    // Offset from the top by CLASS_ALLOCATED_SIZE_OFFSET
    // The number of bytes allocated for this object. It is the same as
    // instance_data_size with the constraint bit cleared. This includes
    // the OBJECT_HEADER_SIZE as well as the OBJECT_VTABLE_POINTER_SIZE
    unsigned int gc_allocated_size;

    unsigned int gc_array_element_size;

    // This is the offset from the start of the object to the first element in the
    // array. It isn't a constant since we pad double words.
    unsigned int gc_array_first_element_offset;

    // The GC needs access to the class name for debugging and for collecting information
    // about the allocation behavior of certain classes. Store the name of the class here.
    const char *gc_class_name;
    Class_Handle gc_clss;

    // This typically holds an array offsets to the pointers in
    // an instance of this class. What is here is totally up to the
    // garbage collector side of the interface. It would be nice if this
    // was located immediately prior to the vtable, since that would
    // eliminate a dereference.
    unsigned int *gc_ref_offset_array;
    unsigned int *gc_weak_ref_offset_array;

    inline bool has_slots(void) const {
        return gc_object_has_slots != 0;
    }
    inline bool is_array(void) const {
        if (gc_class_properties & CL_PROP_ARRAY_MASK) {
            return true;
        } else {
            return false;
        }
    }
    inline bool is_array_of_primitives(void) const {
        if (gc_class_properties & CL_PROP_NON_REF_ARRAY_MASK) {
            return true;
        } else {
            return false;
        }
    }
};

typedef struct Partial_Reveal_VTable {
private:
    struct GC_VTable_Info *gcvt;
public:
    struct GC_VTable_Info *get_gcvt() { return gcvt; }
    void set_gcvt(struct GC_VTable_Info *new_gcvt) { gcvt = new_gcvt; }
    struct GC_VTable_Info **gcvt_address(void) { return &gcvt; }
} Partial_Reveal_VTable;


// This is what is held in the mark field for fixed objects and card table for moveable objects.
typedef bool MARK;

// The GC heap is divided into blocks that are sized based on several constraints.
// They must be small enough to allow unused blocks not to be a big problem.
// They must be large enough so that we are avoid spending a lot of time requesting additional
// blocks.
// They must be small enough to allow them to be scanned without a lot of problem.
// They must be aligned so that clearing lower bits in any address in the block will get
// the start of the block.
// There are several tables that must be held in the first page (hardware specific, for IA32 4096 bytes)
// These table have a single entry for every page in the block. This limits the size of the block.
//


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// TUNABLE (heres for lazy people!)
// 16 for 64K
// 18 for 256K
// 19 for 512K
// 20 for 1MB
// 22 for 4MB
const int GC_BLOCK_SHIFT_COUNT = 16;


// TUNABLE...each thread gets this many blocks per chunk during each request....512K per chunk
#ifdef CONCURRENT
const int GC_NUM_BLOCKS_PER_CHUNK = 1;
#else
const int GC_NUM_BLOCKS_PER_CHUNK = 8;
#endif


// FIXED
// The information about the block is held in the first (4096 byte) card (page) of the block.
const int GC_BYTES_PER_MARK_BYTE = 16;
// FIXED
const int GC_MARK_SHIFT_COUNT = 4;



const int GC_BLOCK_SIZE_BYTES = (1 << GC_BLOCK_SHIFT_COUNT);
// Make this 4K if block is 64K
const int GC_BLOCK_INFO_SIZE_BYTES = (GC_BLOCK_SIZE_BYTES / GC_BYTES_PER_MARK_BYTE);


// 2K is the smallest allocation size we will identify after a collection if block is 64K

extern int GC_MIN_FREE_AREA_SIZE;

//const int GC_MAX_FREE_AREAS_PER_BLOCK = ((GC_BLOCK_SIZE_BYTES - GC_BLOCK_INFO_SIZE_BYTES) / GC_MIN_FREE_AREA_SIZE);
inline unsigned GC_MAX_FREE_AREAS_PER_BLOCK(unsigned min_free_area_size) {
    return ((GC_BLOCK_SIZE_BYTES - GC_BLOCK_INFO_SIZE_BYTES) / min_free_area_size);
}


// Number of marks needed for the block_info bytes...we dont need this!
const int GC_NUM_MARK_BYTES_FOR_BLOCK_INFO_SIZE_BYTES = (GC_BLOCK_INFO_SIZE_BYTES / GC_BYTES_PER_MARK_BYTE);


//////////////////////////////////////////////////////////////////////////////

const int GC_NUM_BITS_PER_BYTE  = 8;
const int GC_NUM_BITS_PER_DWORD = 32;

#ifdef POINTER64
// On IA32 ORP, objects are at least 8 bytes (vtable + header)....but an object can start at any 4-byte aligned address
const int GC_LIVE_OBJECT_CARD_SIZE_IN_BYTES = 8; //sizeof(void *)
// This just needs to be 60K/4....now it is 64K/4....we can fix this easily later...(16K cards per block)
// for now i will just throw in asserts against use of first 1/16th of bit string
const int GC_LIVE_OBJECT_CARD_SIZE_BITS = 3; ///XXXXXXXXX
#else // POINTER64
// On IA32 ORP, objects are at least 8 bytes (vtable + header)....but an object can start at any 4-byte aligned address
const int GC_LIVE_OBJECT_CARD_SIZE_IN_BYTES = 4; //sizeof(void *)
// This just needs to be 60K/4....now it is 64K/4....we can fix this easily later...(16K cards per block)
// for now i will just throw in asserts against use of first 1/16th of bit string
const int GC_LIVE_OBJECT_CARD_SIZE_BITS = 2; ///XXXXXXXXX
#endif // POINTER64

const int GC_NUM_LIVE_OBJECT_CARDS_PER_GC_BLOCK = (GC_BLOCK_SIZE_BYTES / GC_LIVE_OBJECT_CARD_SIZE_IN_BYTES);
unsigned int GC_LIVE_OBJECT_CARD_INDEX_INTO_GC_BLOCK(Partial_Reveal_Object *P_OBJ);

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////

const int GC_NUM_LIVE_OBJECT_CARDS_IN_GC_BLOCK_INFO = (GC_BLOCK_INFO_SIZE_BYTES / GC_LIVE_OBJECT_CARD_SIZE_IN_BYTES);
const int GC_SIZEOF_MARK_BIT_VECTOR_IN_BYTES = (GC_NUM_LIVE_OBJECT_CARDS_PER_GC_BLOCK - GC_NUM_LIVE_OBJECT_CARDS_IN_GC_BLOCK_INFO) / GC_NUM_BITS_PER_BYTE;
const int GC_SIZEOF_MARK_BIT_VECTOR_IN_DWORDS = (GC_NUM_LIVE_OBJECT_CARDS_PER_GC_BLOCK - GC_NUM_LIVE_OBJECT_CARDS_IN_GC_BLOCK_INFO) / GC_NUM_BITS_PER_DWORD;

//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////
// really 1/16 was 1/8 but upped Aug. 1, 2003 for better default speeds. RLH. !!
const int GC_FRACTION_OF_HEAP_INCREMENTAL_COMPACTED_DURING_EACH_GC = 39;

extern unsigned int gc_heap_compaction_ratio;
extern unsigned ephemeral_collection;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

enum gc_compaction_type {
	gc_no_compaction,
	gc_full_heap_sliding_compaction,
	gc_incremental_sliding_compaction,
	gc_bogus_compaction
};


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined _IA64_ || defined __x86_64__
// Assuming 64K blocks, make room for up to 32 Gig heaps which is about the max real memory available on PSL machines in 2004.
const int GC_MAX_BLOCKS = 512 * 1024;
#else
// Even at 64K per block, for 2GB heap we will need max of (2^31/2^16 = 2^15 = 32K blocks in all)
const int GC_MAX_BLOCKS = 32 * 1024;
#endif // _IA64_

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef POINTER64
const int GC_CARD_SHIFT_COUNT = 13;
#else
const int GC_CARD_SHIFT_COUNT = 12;
#endif


// Mask of 1's that when ANDed in will give the offset into the block
const int GC_BLOCK_LOW_MASK = ((POINTER_SIZE_INT)(GC_BLOCK_SIZE_BYTES - 1));
// Mask that gives the base of the block.
const int GC_BLOCK_HIGH_MASK = (~GC_BLOCK_LOW_MASK);
// The card index of an object ref. (not used today)
// #define GC_CARD_INDEX(ADDR) (( (POINTER_SIZE_INT)(ADDR) & GC_BLOCK_LOW_MASK ) >> GC_CARD_SHIFT_COUNT)
// The maximum size of an object that can be allocated in this block.
const int GC_BLOCK_ALLOC_SIZE = (GC_BLOCK_SIZE_BYTES - GC_BLOCK_INFO_SIZE_BYTES);
// If an objects spans at least half the size of the block, I will move it to a separate block for itself
const int GC_MAX_CHUNK_BLOCK_OBJECT_SIZE = (GC_BLOCK_ALLOC_SIZE / 2);


inline unsigned int GC_LIVE_OBJECT_CARD_INDEX_INTO_GC_BLOCK(Partial_Reveal_Object *P_OBJ) {
	return ((((uintptr_t)P_OBJ) & GC_BLOCK_LOW_MASK) >> GC_LIVE_OBJECT_CARD_SIZE_BITS);
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct block_info;  // Forward ref

enum nursery_state {
    free_uncleared_nursery = 0,
    thread_clearing_nursery,
    free_nursery,
    active_nursery,
    spent_nursery,
	concurrent_sweeper_nursery,
    bogus_nursery,
};


typedef struct {
	void *area_base;
	void *area_ceiling;
	unsigned int area_size;
	bool has_been_zeroed;
} free_area;


unsigned int get_object_size_bytes_with_vt(Partial_Reveal_Object *,Partial_Reveal_VTable *);
inline unsigned int get_object_size_bytes(Partial_Reveal_Object *p_obj) {
	Partial_Reveal_VTable *vt = p_obj->vt();
    return get_object_size_bytes_with_vt(p_obj,vt);
}

//
// block_info - This is what resides at the start of each block holding the start of an object.
//              This is all blocks except for sequential blocks holding objects that will not
//              fit into a single block.
//

// The start of the allocation area for this block, page 0 gets block info page 1 gets objects
inline void * GC_BLOCK_ALLOC_START(block_info *BLOCK_ADDR) {
	return ( (void *)((uintptr_t)BLOCK_ADDR + GC_BLOCK_INFO_SIZE_BYTES) );
}
// Used to go from an object reference to the block information.
inline block_info * GC_BLOCK_INFO(void *ADDR) {
	return ((block_info *)((uintptr_t)ADDR & GC_BLOCK_HIGH_MASK));
}
inline char * GC_BLOCK_CEILING(void *P_OBJ) {
    return ( ((char *)(GC_BLOCK_INFO(P_OBJ)) + GC_BLOCK_SIZE_BYTES) - 1 );
}

class block_store_info;

struct block_info {
    unsigned generation;

	// The thread that owns this block/chunk if any.
	void *thread_owner;

	// true if the block is in a nursery.
    bool in_nursery_p;

	// true if the block is in large object (fixed) space.
    bool in_los_p;

	// true if block contains only one object (large)
	bool is_single_object_block;

	// Performance optimization --> to have fast access to block store info table. store it here when block is handed out
	unsigned int block_store_info_index;
    block_store_info *bsi;

	// At the end of each GC calculate the number of free bytes in this block and store it here.
    // This is only accurate at the end of each GC.
    unsigned int block_free_bytes;
    unsigned int block_used_bytes;

    // LOS and the free list need to know how many blocks are represented by this block.
    unsigned int number_of_blocks;

protected:
    nursery_state nursery_status;

public:
	nursery_state get_nursery_status(void) {
		return nursery_status;
	}

	bool set_nursery_status(const nursery_state &expected_state, const nursery_state &new_state);

	// Age is 0 if the block is an active nursery
	// if a block of age A is compacted into another block, the next block has age A+1
	unsigned int age;

	// If this is a SOB, what is the size of the resident object?
    POINTER_SIZE_INT los_object_size;

	// Has it been swept ...could happen during GC or otherwise
	bool block_has_been_swept;

	// Will be turned on by the GC if this block needs to be sliding compacted.
	bool is_compaction_block;

	// If the block is completely free.
    bool is_empty(void) const {
        if(block_free_areas == NULL) return true;
        return block_free_areas[0].area_size == GC_BLOCK_ALLOC_SIZE;
    }
//	bool is_free_block;

    // Set to true once this block has been evacuated of all of its "from" objects.
    bool from_block_has_been_evacuated;

    // This block has objects slid into it so it should not be swept.
    bool is_to_block;

	// The free lists in this block...GC_MAX_FREE_AREAS_PER_BLOCK
    unsigned size_block_free_areas;
	free_area *block_free_areas;

	// Number of allocation areas in this block. This can be 0 if there are not free areas in this block.
	int num_free_areas_in_block;

	unsigned amount_free_space(void) const {
		unsigned sum = 0;
		for(int i = 0; i<num_free_areas_in_block; ++i) {
			sum += block_free_areas[i].area_size;
		}
		return sum;
	}

	// The current allocation area. If this is -1 then area 0 will be the next area to use...
	int current_alloc_area;

	// The next free block that will be used by the thread once all areas in this block are exhausted.
	block_info *next_free_block;

    //
    // Not used delete with new tls allocation routines.
    //
	// The frontier allocation pointer in the current allocation area within this block
	void *curr_free;

	// The ceiling/limit pointer in the current allocation area within this block
	void *curr_ceiling;	// for allocation is faster when we use this

    // Used to mark and keep track of live objects resident in this block for heap tracing activity
	uint8 mark_bit_vector[GC_SIZEOF_MARK_BIT_VECTOR_IN_BYTES];

///////////////////////////////////////////////////////////////////////////////

    POINTER_SIZE_INT current_object_offset;
    char *current_block_base;

	bool block_contains_only_immutables;
	bool block_immutable_copied_to_this_gc;
	unsigned last_sweep_num_holes;

    char *use_to_get_last_used_word_in_header;
	char *unused;

    void init_object_iterator() {
        current_object_offset = 0;
        current_block_base = (char *)GC_BLOCK_INFO(&use_to_get_last_used_word_in_header);
    }
}; // struct block_info

static inline bool
is_array(Partial_Reveal_Object *p_obj) {
    if (p_obj->vt()->get_gcvt()->gc_class_properties & CL_PROP_ARRAY_MASK) {
        return true;
    } else {
        return false;
    }
}


static inline bool
is_vt_array(Partial_Reveal_VTable *vt) {
    if (vt->get_gcvt()->gc_class_properties & CL_PROP_ARRAY_MASK) {
        return true;
    } else {
        return false;
    }
}


static inline bool
is_array_of_primitives(Partial_Reveal_Object *p_obj) {
    if (p_obj->vt()->get_gcvt()->gc_class_properties & CL_PROP_NON_REF_ARRAY_MASK) {
        return true;
    } else {
        return false;
    }
}


static inline bool
is_array_of_references(Partial_Reveal_VTable *vt) {
    if (vt->get_gcvt()->gc_class_properties & CL_PROP_ARRAY_MASK) {
	    if (vt->get_gcvt()->gc_class_properties & CL_PROP_NON_REF_ARRAY_MASK) {
			return false;
		} else {
			return true;
		}
	}
	return false;
}

#ifdef MANGLEPOINTERS
/* uses these routines in barrier.cpp to mangle and unmangle pointers seen by ORP. */
HeapObject *mangleBits(HeapObject *mangledObject);
HeapObject *unmangleBits(HeapObject *mangledObject);
#else
#define mangleBits(_X_) (_X_)
#define unmangleBits(_X_) (_X_)
#endif


enum OBJECT_LOCATION {
    PRIVATE_NURSERY = 0,  // object is in a private nursery
    PUBLIC_HEAP = 1,      // object is in the heap and potentially accessible from any thread
    GLOBAL = 2           // object is not in a private nursery or in the heap
#ifdef PUB_PRIV
    ,PRIVATE_HEAP = 3      // object is in the heap but is only accessible from one thread
#endif // PUB_PRIV
};

extern "C" void heapSetG4(HeapObject *object, uint32 offset, g4 value);
extern "C" void gc_add_root_set_entry(Managed_Object_Handle *ref1, Boolean is_pinned);
extern "C" void gc_add_root_set_entry_mid_pn_collect(Managed_Object_Handle *ref1);
extern "C" void *gc_heap_base_address();
extern "C" void *gc_heap_ceiling_address();

// A list of the STM implementations supported by McRT.
typedef enum _TgcStmImplementation {
    STMDisabled = 0,                     /* no STM */
    STMReaderVersionAndWriterLock = 1,   /* with read versioning */
    // reader writer lock with reader priority
    STMReaderLockAndWriterLockWithReaderPriority = 2,
    // reader writer lock with writer priority
    STMReaderLockAndWriterLockWithWriterPriority = 3,
    STMReaderVersionAndWriterLogging = 4,
    STMInvalidType = 5
} TgcStmImplementation;


// The STM properties important for JIT compilation.
typedef struct _TgcStmProperties {
    TgcStmImplementation implementation;
    uint32                               initial_lock_value;
    uint32                               initial_lock_value_mask;
    uint32                               private_object_indicator;
    uint32                               memento_size;
    uint32               descriptor_alignment;
    uint32               transaction_info_offset;
    uint32               buffer_overflow_check_mask;
    uint32               read_lock_index_offset;
    uint32               write_lock_index_offset;
    uint32               version_mask;
    uint32               version_increment;
    uint32               strong_atomicity_read_mask;
    uint32               lock_table_base_address;
    uint32               lock_table_size;
    uint32               lock_table_element_size;
    uint32               desc_offset_in_tls;
    uint32               filter_base_offset;
    uint32               sequence_number_offset;
        uint32                           mode_offset;
        uint32                           aggressive_mode;
        uint32                           cautious_mode;
        uint32                           hybrid_mode;
} TgcStmProperties;

enum FREE_BLOCK_POLICY {
	WATERMARK = 0,
	RETURN_ALL_IF_LOS = 1,
	MAINTAIN_RATIO = 2
};

extern FREE_BLOCK_POLICY g_return_free_block_policy;

#endif // _gc_header_H_
