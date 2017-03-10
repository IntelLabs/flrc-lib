/*
 * Redistribution and use in source and binary forms, with or without modification, are permitted 
 * provided that the following conditions are met:
 * 1.   Redistributions of source code must retain the above copyright notice, this list of 
 * conditions and the following disclaimer.
 * 2.   Redistributions in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "flrclibconfig.h"
#if _MSC_VER >= 1400 || defined __GNUC__ // vs2005+
#include <map>
#else
#include <..\stlport\map>
#endif
#include "tgc/gc_header.h"
#include "tgc/garbage_collector.h"
#if _MSC_VER >= 1400 || defined __GNUC__  // vs2005+
#include <algorithm>
#include <deque>
#else
#include <..\stlport\algorithm>
#include <..\stlport\deque>
#endif
#ifdef __GNUC__
//#include <strings.h>
#else
#include <intrin.h>
#pragma intrinsic(_BitScanForward)
//#pragma intrinsic(_BitScanForward) std::bitset bs;
#endif
#include <stdint.h>

Managed_Object_Handle gc_malloc_slow_no_constraints (unsigned size, Allocation_Handle ah, GC_Thread_Info *tp
#ifdef PUB_PRIV
, bool private_heap_block
#endif // PUB_PRIV
);
Managed_Object_Handle gc_malloc_slow_no_constraints_with_nursery (
	unsigned size,
	Allocation_Handle ah,
	GC_Thread_Info *tp,
	GC_Nursery_Info *nursery
#ifdef PUB_PRIV
    , bool private_heap_block
#endif // PUB_PRIV
);

class obj_collection_info {
public:
	Partial_Reveal_Object *old_location;
	Partial_Reveal_Object *new_location;

	obj_collection_info(Partial_Reveal_Object *old) : old_location(old) {}
};

enum LOCAL_MARK_STATE {
	LOCAL_MARK_IDLE = 0,     // nobody is marking or doing anything else
	LOCAL_MARK_ACTIVE = 1,   // typical local collection driven by thread exhausting allocation area
	LOCAL_MARK_GC = 2,       // marking and inter-slots computed by a larger GC
	LOCAL_MARK_LIVE_IDENTIFIED = 3, // local collection after all roots and live objects have been identified
	LOCAL_PAUSED_DURING_MOVE = 4 // local collection couldn't get a new block to allocate in and so is paused.(CONCURRENT ONLY)
#ifdef PUB_PRIV
	, LOCAL_MARK_PRIVATE_HEAP_GC = 5
#endif // PUB_PRIV
};

class pn_info;
extern bool g_two_space_pn;
extern bool g_pure_two_space;

extern unsigned ARENA_SIZE;

template <class T>
class PrivateNurseryAlloc {
protected:
	GC_Small_Nursery_Info *m_thread;
public:
    typedef size_t     size_type;
    typedef ptrdiff_t  difference_type;
    typedef T         *pointer;
    typedef const T   *const_pointer;
    typedef T&         reference;
    typedef const T&   const_reference;
    typedef T          value_type;

    template<typename Other> struct rebind
    {
      typedef PrivateNurseryAlloc<Other> other;
    };

    pointer address(reference x)const{return &x;}
    const_pointer address(const_reference x)const{return &x;}

//	PrivateNurseryAlloc() throw() {
//	}

    PrivateNurseryAlloc(const PrivateNurseryAlloc &other) throw() {
		m_thread = other.m_thread;
	}

//    template <class U> PrivateNurseryAlloc(const PrivateNurseryAlloc<U>&) throw() {
//	}

    ~PrivateNurseryAlloc() throw() {
	}

    PrivateNurseryAlloc(GC_Small_Nursery_Info *thread) throw() : m_thread(thread) {
	}

#ifdef USE_STL_PN_ALLOCATOR
	pointer allocate(size_type size, const void * = 0) {
		char *ret = (char*)arena_alloc_space(m_thread->tls_arena,size);
		if(!ret) {
			m_thread->tls_arena = alloc_arena(m_thread->tls_arena,ARENA_SIZE);
			ret = (char*)arena_alloc_space(m_thread->tls_arena,size);
		}
		return (pointer)ret;
	}

	void deallocate(void *p,size_type size) {
		// Intentionally empty.
	}

	pointer _Charalloc(size_type size) {
		return allocate(size);
	}
#endif

    void construct(pointer p,const T& val) {
        new ((void *)p) T(val);
    }

    void destroy(pointer p) {
        p->T::~T();
    }
};

template <class T1, class T2>
bool operator== (const PrivateNurseryAlloc<T1>&,
                 const PrivateNurseryAlloc<T2>&) throw() {
    return true;
}
template <class T1, class T2>
bool operator!= (const PrivateNurseryAlloc<T1>&,
                 const PrivateNurseryAlloc<T2>&) throw() {
    return false;
}

//#define USE_STL_PN_ALLOCATOR

#ifdef USE_STL_PN_ALLOCATOR
typedef PrivateNurseryAlloc<Partial_Reveal_Object**> LSAllocator;
#else // USE_STL_PN_ALLOCATOR
typedef std::allocator<Partial_Reveal_Object**> LSAllocator;
#endif // USE_STL_PN_ALLOCATOR

enum INTER_SLOT_MODE {
	NO_INTER_SLOT = 0,
	YES_INTER_SLOT = 1,
	ONLY_INTER_SLOT = 2
#ifdef PUB_PRIV
	, PRIVATE_HEAP_SLOT = 3
    , PRIVATE_HEAP_SLOT_NON_ESCAPING = 4
#endif // PUB_PRIV
};

typedef enum {
    MUST_LEAVE = 0,
    CAN_STAY = 1,
    POINTS_TO_MUTABLE = 2
} NEW_LOCATION_CLASSIFIER;

#ifndef NEW_APPROACH
class PncLiveObject {
public:
	Partial_Reveal_Object *old_location;
    Partial_Reveal_Object *new_location;
    NEW_LOCATION_CLASSIFIER new_loc_class;
	Partial_Reveal_VTable *vt;
	PncLiveObject *previous_object;
#ifdef PUB_PRIV
	bool m_points_to_mutable;
#endif // PUB_PRIV

	PncLiveObject(Partial_Reveal_Object *old,
                  NEW_LOCATION_CLASSIFIER new_location_class,
				  Partial_Reveal_VTable *obj_vt,
				  PncLiveObject *prev) :
	    old_location(old),
        new_location(NULL),
        new_loc_class(new_location_class),
		vt(obj_vt),
		previous_object(prev)
#ifdef PUB_PRIV
        , m_points_to_mutable(false)
#endif // PUB_PRIV
        {}

	PncLiveObject(void) {}

	bool operator<(const PncLiveObject &rhs) const {
		return old_location < rhs.old_location;
	}

#ifdef PUB_PRIV
    void set_points_to_mutable(void) {
        if(!m_points_to_mutable) {
            m_points_to_mutable = true;
            previous_object->set_points_to_mutable();
        }
    }
#endif // PUB_PRIV
};

class ObjectPath {
public:
	PncLiveObject *from;
	Partial_Reveal_Object *to;

	ObjectPath(void) : from(NULL), to(NULL) {}

	ObjectPath(PncLiveObject *f, Partial_Reveal_Object *t) : from(f), to(t) {}
};

class MARK_STACK_PATH : public std::stack<ObjectPath,std::vector<ObjectPath> > {
public:
	unsigned capacity(void) const {
		return c.capacity();
	}

	void reserve(unsigned size) {
		c.reserve(size);
	}
};

//===========================================================================================================

static inline ObjectPath pop_bottom_object_from_local_mark_stack(MARK_STACK_PATH *ms) {
	if(ms->empty()) {
		return ObjectPath();
	} else {
		ObjectPath ret = ms->top();
		ms->pop();
		return ret;
	}
}


static inline bool
push_bottom_on_local_mark_stack(PncLiveObject *from,
								Partial_Reveal_Object *p_obj,
								MARK_STACK_PATH *ms) {
    Partial_Reveal_Object *usable_object = (Partial_Reveal_Object*)((uintptr_t)p_obj & ~0x3);
#ifdef _DEBUG
	verify_object (usable_object, (POINTER_SIZE_INT) get_object_size_bytes(usable_object));
#endif

	ms->push(ObjectPath(from,p_obj));
	return true;
}


#else

class MARK_STACK_PATH : public std::stack<Partial_Reveal_Object *,std::vector<Partial_Reveal_Object *> > {
public:
	unsigned capacity(void) const {
		return c.capacity();
	}

	void reserve(unsigned size) {
		c.reserve(size);
	}
};

static inline Partial_Reveal_Object *pop_bottom_object_from_local_mark_stack(MARK_STACK_PATH *ms) {
	if(ms->empty()) {
		return NULL;
	} else {
		Partial_Reveal_Object *ret = ms->top();
		ms->pop();
		return ret;
	}
}


static inline bool
push_bottom_on_local_mark_stack(Partial_Reveal_Object *p_obj,
								MARK_STACK_PATH *ms) {
    Partial_Reveal_Object *usable_object = (Partial_Reveal_Object*)((POINTER_SIZE_INT)p_obj & ~0x3);
#ifdef _DEBUG
	verify_object (usable_object, (POINTER_SIZE_INT) get_object_size_bytes(usable_object));
#endif

	ms->push(p_obj);
	return true;
}


#endif

static inline bool mark_stack_is_empty(MARK_STACK_PATH *ms) {
	return ms->empty();
}

//===========================================================================================================
enum STAT_ENTRY_TYPE {
	FULL_PRIVATE_NURSERY_COLLECTION = 0,
	CONCURRENT_GC_MARK_CROSS_SUSPEND = 1,
	CONCURRENT_GC_SWEEP_IDLE_CROSS_SUSPEND = 2
};

class pn_collection_stats {
public:
#ifdef _WINDOWS
	LARGE_INTEGER start_time;
	LARGE_INTEGER end_time;
#elif defined LINUX
	struct timeval start_time;
	struct timeval end_time;
#endif
	unsigned size_at_collection;
	unsigned num_lives;
	unsigned size_lives;
	STAT_ENTRY_TYPE set;

#ifdef _WINDOWS
	pn_collection_stats(LARGE_INTEGER &start,
						LARGE_INTEGER &end,
#elif defined LINUX
	pn_collection_stats(struct timeval &start,
						struct timeval &end,
#endif
						unsigned sac,
						unsigned num,
						unsigned size,
						STAT_ENTRY_TYPE type = FULL_PRIVATE_NURSERY_COLLECTION) :
		start_time(start),
		end_time(end),
		size_at_collection(sac),
		num_lives(num),
		size_lives(size),
		set(type) {}
};

class intra_slot {
public:
	Partial_Reveal_Object ** slot;
	Partial_Reveal_Object *  base;

	intra_slot(Partial_Reveal_Object **s, Partial_Reveal_Object *b) : slot(s), base(b) {}

    unsigned get_offset(void) {
        return (unsigned)((char*)slot - (char*)base);
    }
};
class inter_slot {
public:
	Partial_Reveal_Object ** slot;
	Partial_Reveal_Object *  base;

	inter_slot(Partial_Reveal_Object **s, Partial_Reveal_Object *b) : slot(s), base(b) {}

    unsigned get_offset(void) {
        return (unsigned)((char*)slot - (char*)base);
    }
};

// Here we assume that the two lower bits are 0 so that we can indicate non-presence with NULL
// and "DELETED" with 1.
template <class T>
class ClosedPointerHash {
public:
	class CPH_entry {
	public:
		union {
		void *key;
		void *first;
		};
		union {
		T     data;
		T     second;
		};
	};

protected:
	CPH_entry *m_entries;
	unsigned   m_size;

public:
	class iterator {
	protected:
		ClosedPointerHash<T> *m_back;
	public:
		unsigned m_index;

		iterator(void) {
			m_back  = NULL;
			m_index = 0;
		}

		iterator(ClosedPointerHash<T> *back, unsigned index) : m_back(back), m_index(index) {}

		bool operator!=(const iterator &rho) {
			if(m_back == NULL) return true;
			if(m_back != rho.m_back) return true;
			return m_index != rho.m_index;
		}

		CPH_entry * operator->(void) {
			if(m_back != NULL && m_index < m_back->m_size) {
				return &m_back->m_entries[m_index];
			} else {
				assert(0);
				printf("CPH::iterator error\n");
				exit(-1);
				return NULL;
			}
		}
	};

	friend class iterator;

	ClosedPointerHash(unsigned size=20) : m_size(size) {
		m_entries = new CPH_entry[m_size];
		for(unsigned i=0;i<m_size;++i) {
			m_entries[i].key = NULL;
		}
	}

	~ClosedPointerHash(void) {
		delete [] m_entries;
	}

	void clear(void) {
		for(unsigned i=0;i<m_size;++i) {
			m_entries[i].key = NULL;
		}
	}

	void swap(ClosedPointerHash<T> &other) {
		CPH_entry *entry_save = other.m_entries;
		unsigned   size_save  = other.m_size;
		other.m_entries = m_entries;
		other.m_size    = m_size;
		m_entries       = entry_save;
        m_size          = size_save;
	}

	iterator end(void) {
		return iterator(this,m_size);
	}

	std::pair<iterator, bool> insert(void *key, const T &data) {
		iterator res = find(key);
		if(res != end()) {
			return std::pair<iterator,bool>(res,false);
		}

		unsigned index = ((POINTER_SIZE_INT)key>>2) % m_size;
		for(unsigned int i=0;i<m_size;++i) {
			if(m_entries[(index+i)%m_size].key <= (void*)0x1) {
				m_entries[(index+i)%m_size].key  = key;
				m_entries[(index+i)%m_size].data = data;
                return std::pair<iterator,bool>(iterator(this,(index+i)%m_size),true);
			}
		}

		return std::pair<iterator,bool>(end(),false);
	}

	std::pair<iterator, bool> insert(const std::pair<void*,T> &the_pair) {
		return insert(the_pair.first,the_pair.second);
	}

	bool erase(void *key) {
		unsigned index = ((POINTER_SIZE_INT)key>>2) % m_size;
		for(unsigned int i=0;i<m_size;++i) {
			if(m_entries[(index+i)%m_size].key == key) {
				m_entries[(index+i)%m_size].key = (void*)0x1;
				return true;
			}
			if(m_entries[(index+i)%m_size].key != (void*)0x1) {
				return false;
			}
		}
		return false;
	}

	iterator find(void *key) {
		unsigned index = ((POINTER_SIZE_INT)key>>2) % m_size;
		for(unsigned int i=0;i<m_size;++i) {
			if(m_entries[(index+i)%m_size].key == key) {
				return iterator(this,(index+i)%m_size);
			}
			if(m_entries[(index+i)%m_size].key == NULL) {
				return end();
			}
		}
		return end();
	}

	bool present(void *key) {
		return find(key) != end();
	}
};

/*
 * We start adaptive nursery size by starting at the maximum nursery size and halfing it
 * until we find a size that is worse.  At that point, we have top, middle, and bottom
 * numbers where both top and bottom are worse than middle.  We then enter the searching
 * phase where we try (top+middle)/2 and (middle+bottom)/2 and try to narrow down the
 * best nursery size parameter.  When we have found something close enough to optimal
 * then we enter phase detect mode where we try to detect a phase change that effects
 * the average collection cost and if so we start the process over by going back to
 * 'decreasing' mode.
 */
enum ADAPTIVE_MODE {
	ADAPTIVE_DECREASING=0,
	ADAPTIVE_SEARCHING=1,
	ADAPTIVE_PHASE_DETECT=2,
	ADAPTIVE_HIGH_LOW=3
};

extern bool adaptive_nursery_size;

#ifdef TYPE_SURVIVAL
class TypeSurvivalInfo {
public:
	uint64_t m_num_objects;
	uint64_t m_num_bytes;

	TypeSurvivalInfo(unsigned num_objects, unsigned size) : m_num_objects(num_objects), m_num_bytes(size) {}
};
#endif // TYPE_SURVIVAL

#ifdef SURVIVE_WHERE
class SurviveStat {
public:
	unsigned num_instance;
	double   sum_survive_percent;

	SurviveStat(unsigned num, double sum) : num_instance(num), sum_survive_percent(sum) {}
};
#endif

extern FILE *cdump;

class cheney_space_pair;
class cheney_spaces;

class PrtSuspendSelf {
protected:
    bool m_is_suspended;
public:
    PrtSuspendSelf(void) {
        prtSuspendTask(prtGetTaskHandle());
        m_is_suspended = true;
    }
   ~PrtSuspendSelf(void) {
        if(m_is_suspended) {
            prtResumeTask(prtGetTaskHandle());
        }
    }

    void resume(void) {
        assert(m_is_suspended);
        m_is_suspended = false;
        prtResumeTask(prtGetTaskHandle());
    }

    void suspend(void) {
        assert(!m_is_suspended);
        m_is_suspended = true;
        prtSuspendTask(prtGetTaskHandle());
    }
};

class PN_cheney_info {
public:
    pn_info *collector;
    GC_Thread_Info *tls_for_gc;
    GC_Nursery_Info ** public_nursery;
    GC_Nursery_Info ** immutable_nursery;
    PrtSuspendSelf *lock_myself;
    cheney_space_pair **cur_cheney_pair;
    cheney_spaces *cspaces;
	bool two_space;
#ifdef _DEBUG
    unsigned num_surviving , size_surviving;
    unsigned mutable_copied , immutable_copied;
	unsigned num_rs, rs_created, two_space_copy;
#endif

    PN_cheney_info(void) :
        collector(NULL),
        tls_for_gc(NULL),
        public_nursery(NULL),
        immutable_nursery(NULL),
        lock_myself(NULL),
        cur_cheney_pair(NULL),
        cspaces(NULL),
		two_space(false)
#ifdef _DEBUG
        , num_surviving(0)
        , size_surviving(0)
        , mutable_copied(0)
        , immutable_copied(0)
		, num_rs(0)
		, rs_created(0)
		, two_space_copy(0)
#endif
        {}
};

enum CPS_RESULT {
	CPS_NOT_IN_PN = 0,
    CPS_ALREADY_TARGET_TWO_SPACE = 1,
	CPS_AFTER_INDIRECTION_NON_OBJ = 2,
	CPS_AFTER_INDIRECTION_NOT_IN_PN = 3,
	CPS_FORWARDED = 4,
	CPS_COPY = 5,
	CPS_TWO_SPACE_COPY = 6,
	CPS_END = 7 // just marks the end, not used
};

CPS_RESULT cheney_process_slot(Partial_Reveal_Object *base, Partial_Reveal_Object **slot, PN_cheney_info *info);
unsigned adjust_frontier_to_alignment(Partial_Reveal_Object * &frontier, Partial_Reveal_VTable *vt);

class pn_space {
protected:
	// start and end don't change
	// free is the next spot to allocate in the forward direction
	// ceiling is the next spot to reverse allocate
	// fill is at the end of the region which objects who have survived exactly
	// one collection have been copied
	void *start, *end, *fill, *free, *ceiling, *cheney_ptr;
public:
	pn_space(void *s = NULL, void *e = NULL) {
		if((uintptr_t)s % sizeof(POINTER_SIZE_INT)) {
			printf("pn_space start must be a multiple of POINTER_SIZE_INT\n");
			exit(-1);
		}
		start = free = cheney_ptr = s;
		end   = ceiling = e;
		fill  = start;
	}
	void * get_start(void)   const { return start; }
	void * get_end(void)     const { return end; }
	void * get_fill(void)    const { return fill; }
	void * get_free(void)    const { return free; }
	void * get_ceiling(void) const { return ceiling; }
	void   set_start_end(void *s, void *e) {
		if((uintptr_t)s % sizeof(POINTER_SIZE_INT)) {
			printf("pn_space start must be a multiple of POINTER_SIZE_INT\n");
			exit(-1);
		}
		start = free = cheney_ptr = s;
		end   = ceiling = e;
		fill  = start;
	}
	void set_fill_to_free(void) {
		fill = free;
	}
	void reset(void) {
		free    = cheney_ptr = start;
		ceiling = end;
	}

	bool contains(void* addr) const {
		if(addr >= start && addr < end) return true;
		else                            return false;
	}

	Partial_Reveal_Object * allocate(Partial_Reveal_VTable *obj_vtable, unsigned obj_size) {
		Partial_Reveal_Object *frontier = (Partial_Reveal_Object *)free;
		unsigned skip_size = adjust_frontier_to_alignment(frontier, obj_vtable);

		POINTER_SIZE_INT new_free = (obj_size + (uintptr_t)frontier);
		assert(new_free <= (uintptr_t)ceiling);
		if(skip_size) {
			*((POINTER_SIZE_INT*)((char*)frontier - skip_size)) = skip_size;
		}
		frontier->set_vtable((uintptr_t)obj_vtable);
		// increment free ptr and return object
		free = (void *) new_free;
		return frontier;
	}

	bool process(void *env);

	void copy_rs_entry(external_pointer *entry);
	void add_rs_entry(Partial_Reveal_Object *base, Partial_Reveal_Object **slot);

	void zero(void) {
		POINTER_SIZE_INT size = (uintptr_t)end - (uintptr_t)start;
		memset(start,0,size);
	}
	//void mark_phase_process(GC_Thread *gc_thread);
	void global_gc_get_pn_lives(void *env);
};

class pn_info {
public:
	std::vector<Partial_Reveal_Object**> m_roots;
	ExpandInPlaceArray<slot_offset_entry> interior_pointer_table;
	ExpandInPlaceArray<slot_offset_entry> interior_pointer_table_public;

	MARK_STACK_PATH mark_stack;
	void *local_nursery_start;         // nursery start
	void *local_nursery_end;           // nursery end
	void *currently_used_nursery_end;  // the nursery end in use, may be less than the max nursery end
	pn_space two_spaces[2];
	unsigned two_space_in_use;

	typedef std::vector<intra_slot>::iterator intra_iterator;
	std::vector<intra_slot> *intra_slots;
	typedef std::vector<Partial_Reveal_Object**>::iterator intra_weak_iterator;
    std::vector<Partial_Reveal_Object **> *intra_weak_slots;
	typedef std::vector<inter_slot>::iterator inter_iterator;
	std::vector<inter_slot> *inter_slots;

	LOCAL_MARK_STATE gc_state;
	unsigned num_micro_collections;
#ifdef PUB_PRIV
	unsigned num_private_heap_collections;
	LARGE_INTEGER sum_private_heap_time;
    unsigned num_private_collection_escaping;
    unsigned num_private_collection_objects;
    bool ph_to_pn_escape;
#ifdef _DEBUG
    std::ofstream *ph_ofstream;
#endif // _DEBUG
#endif // PUB_PRIV
#ifdef _WINDOWS
	LARGE_INTEGER sum_micro_time;
#elif defined LINUX
	struct timeval sum_micro_time;
#endif
	unsigned max_collection_time;
#ifdef DETAILED_PN_TIMES
#ifdef _WINDOWS
	LARGE_INTEGER memset_time;
	LARGE_INTEGER clear_time;
	LARGE_INTEGER roots_and_mark_time;
	LARGE_INTEGER allocate_time;
	LARGE_INTEGER move_time;
	LARGE_INTEGER update_root_time;

	LARGE_INTEGER mark_prepare_time;
	LARGE_INTEGER mark_stackwalk_time;
	LARGE_INTEGER mark_push_roots_escaping_time;
	LARGE_INTEGER mark_all_escaping_time;
	LARGE_INTEGER mark_push_roots_time;
	LARGE_INTEGER mark_all_time;
	std::list<pn_collection_stats> *pn_stats;

#ifdef CLEAR_STATS
	LARGE_INTEGER root_clear;
	LARGE_INTEGER ipt_private_clear;
	LARGE_INTEGER ipt_public_clear;
	LARGE_INTEGER pi_clear;
	LARGE_INTEGER intra_slots_clear;
	LARGE_INTEGER inter_slots_clear;
	LARGE_INTEGER live_objects_clear;
#endif
#elif defined LINUX
	struct timeval memset_time;
	struct timeval clear_time;
	struct timeval roots_and_mark_time;
	struct timeval allocate_time;
	struct timeval move_time;
	struct timeval update_root_time;

	struct timeval mark_prepare_time;
	struct timeval mark_stackwalk_time;
	struct timeval mark_push_roots_escaping_time;
	struct timeval mark_all_escaping_time;
	struct timeval mark_push_roots_time;
	struct timeval mark_all_time;
	std::list<pn_collection_stats> *pn_stats;

#ifdef CLEAR_STATS
	struct timeval root_clear;
	struct timeval ipt_private_clear;
	struct timeval ipt_public_clear;
	struct timeval pi_clear;
	struct timeval intra_slots_clear;
	struct timeval inter_slots_clear;
	struct timeval live_objects_clear;
#endif
#endif
#endif

//    typedef std::vector<PncLiveObject>::iterator live_obj_iterator;
//	std::vector<PncLiveObject> *live_objects;
#ifdef NEW_APPROACH
	ExpandInPlaceArray<ForwardedObject> m_forwards;
    unsigned cur_pn_live_obj_num;
#else
	ExpandInPlaceArray<PncLiveObject> *live_objects;
#endif

#ifdef RECORD_IMMUTABLE_COPIES
	std::map<Partial_Reveal_Object*,Partial_Reveal_Object*> promoted_immutables;
#endif // RECORD_IMMUTABLE_COPIES
	GC_Small_Nursery_Info *m_info;
#ifndef HAVE_PTHREAD_H
	PrtTaskHandle m_original_task;
	PrtTaskHandle m_new_task;
#endif // !HAVE_PTHREAD_H

#ifdef CONCURRENT
	std::vector<Partial_Reveal_Object*> new_moved_grays;
	void *sweep_ptr_copy;
	std::deque<Partial_Reveal_Object*> m_concurrent_gray_objects;
	SynchCriticalSectionHandle m_concurrent_gray_lock_cs;
#endif // CONCURRENT

#ifdef _DEBUG
	uint64_t num_marks, num_slots;
	uint64_t num_mutable_ref, num_mutable_refless, num_immutable_ref, num_immutable_refless;
    uint64_t num_escape_live, num_stack_live, num_rs_live;
#endif

	ADAPTIVE_MODE adaptive_mode;
	unsigned adaptive_top, adaptive_middle, adaptive_bottom, adaptive_sample;
	// These are measured as collection_time / size at collection.
	double   adaptive_top_res, adaptive_middle_res, adaptive_bottom_res, adaptive_high_middle_res;
	unsigned adaptive_searching_current_size;
	uint64   adaptive_cumul_time, adaptive_cumul_size;
	bool     adaptive_main_thread;
	unsigned adaptive_num_successive_excessive;

#ifdef TYPE_SURVIVAL
	std::map<struct Partial_Reveal_VTable*, TypeSurvivalInfo> m_type_survival;
#endif // TYPE_SURVIVAL

//	std::map<void*,uint64_t> m_why_extra_live;
#ifdef SURVIVE_WHERE
	std::map<struct Partial_Reveal_VTable*,SurviveStat> m_sw_value;
	std::map<struct Partial_Reveal_VTable*,SurviveStat> m_sw_base;
	std::map<PrtCodeAddress,SurviveStat> m_sw_eip;
#endif // SURVIVE_WHERE

#ifdef PUB_PRIV
    unsigned *staying_mark_bits, smb_size, smb_iter, non_zero_save;

    void stay_mark(void *obj) {
        unsigned offset    = (POINTER_SIZE_INT)obj - (POINTER_SIZE_INT)local_nursery_start;
        unsigned objoff    = offset / sizeof(void*);
        assert(offset % sizeof(void*) == 0);
        unsigned compress  = sizeof(unsigned) * 8; // number of bits in an unsigned
        unsigned mark_word = objoff / compress;
        unsigned mark_bit  = objoff % compress;

        unsigned mask = 1 << mark_bit;
        staying_mark_bits[mark_word] |= mask;
    }

#ifdef NEW_APPROACH
    class mb_iter {
    protected:
        pn_info *m_ls;
        unsigned cur_non_zero;
        unsigned smb_iter;
        unsigned index;
    public:
        mb_iter(void) : m_ls(NULL) {}

        mb_iter(pn_info *ls) {
            m_ls = ls;
            smb_iter = 0;
            cur_non_zero = m_ls->staying_mark_bits[0];
            operator++();
        }

        bool valid(void) { return m_ls != NULL; }

        Partial_Reveal_Object * operator*(void) {
            if(m_ls) {
                return (Partial_Reveal_Object*)((char*)m_ls->local_nursery_start + ((smb_iter * (sizeof(void*) * sizeof(unsigned) * 8)) + (sizeof(void*) * index)));
            } else {
                return NULL;
            }
        }

        mb_iter & operator++(void) {
            if(!cur_non_zero) {
                ++smb_iter;
                while(smb_iter < m_ls->smb_size && m_ls->staying_mark_bits[smb_iter] == 0) {
                    ++smb_iter;
                }
                if(smb_iter < m_ls->smb_size) {
                    cur_non_zero = m_ls->staying_mark_bits[smb_iter];
                } else {
                    m_ls = NULL;
                }
            }

#ifdef __GNUC__
            index = ffs(cur_non_zero) - 1;
#else
            _BitScanForward((DWORD*)&index, cur_non_zero);
#endif
            cur_non_zero &= (~(1 << index));
            return *this;
        }
    };

    friend class mb_iter;
#endif // NEW_APPROACH

#ifdef NEW_APPROACH
    mb_iter start_iterator(void) {
        if(staying_mark_bits) {
            return mb_iter(this);
        } else {
            return mb_iter(NULL);
        }
    }
#else
    void * start_iterator(void) {
        if(staying_mark_bits) {
            non_zero_save = staying_mark_bits[0];
            smb_iter      = 0;
            return next_iterator();
        } else {
            return NULL;
        }
    }
#endif

    void * start_zero_iterator(void) {
        if(staying_mark_bits) {
            smb_iter = 0;
            return next_zero_iterator();
        } else {
            return NULL;
        }
    }

#ifndef NEW_APPROACH
    void * next_iterator(void) {
        unsigned index;
        while(smb_iter < smb_size) {
            if(staying_mark_bits[smb_iter]) {
#ifdef __GNUC__
                index = ffs(staying_mark_bits[smb_iter]) - 1;
#else
                _BitScanForward((DWORD*)&index, staying_mark_bits[smb_iter]);
#endif
                staying_mark_bits[smb_iter] &= (~(1 << index));
                return (char*)local_nursery_start + ((smb_iter * (sizeof(void*) * sizeof(unsigned) * 8)) + (sizeof(void*) * index));
            }
            if(non_zero_save) {
                staying_mark_bits[smb_iter] = non_zero_save;
            }
            ++smb_iter;
            if(smb_iter < smb_size) {
                non_zero_save = staying_mark_bits[smb_iter];
            }
        }
        return NULL;
    }
#endif

    void * next_zero_iterator(void) {
        unsigned index;
        while(smb_iter < smb_size) {
            if(staying_mark_bits[smb_iter]) {
#ifdef __GNUC__
                index = ffs(staying_mark_bits[smb_iter]) - 1;
#else
                _BitScanForward((DWORD*)&index, staying_mark_bits[smb_iter]);
#endif
                staying_mark_bits[smb_iter] &= (~(1 << index));
                return (char*)local_nursery_start + ((smb_iter * (sizeof(void*) * sizeof(unsigned) * 8)) + (sizeof(void*) * index));
            }
            ++smb_iter;
        }
        return NULL;
    }
#endif // PUB_PRIV

	float last_survive_percent;

    PrtStackIterator *current_si;

	std::vector<weak_pointer_object*> m_wpos;
	std::vector<Partial_Reveal_Object*> m_finalize;
	std::vector<Partial_Reveal_Object*> m_to_be_finalize;
    bool new_pn;
    cheney_spaces cspaces;

	// =================================================================================================

	pn_info(GC_Small_Nursery_Info *info) :
		num_micro_collections(0),
#ifdef PUB_PRIV
		num_private_heap_collections(0),
        num_private_collection_escaping(0),
        num_private_collection_objects(0),
#ifdef _DEBUG
        ph_ofstream(NULL),
#endif // _DEBUG
#endif // PUB_PRIV
		max_collection_time(0),
		gc_state(LOCAL_MARK_IDLE),
		m_info(info),
#ifndef HAVE_PTHREAD_H
		m_original_task(NULL),
		m_new_task(NULL),
#endif // !HAVE_PTHREAD_H
        intra_slots(NULL),
        intra_weak_slots(NULL),
        inter_slots(NULL)
#ifndef NEW_APPROACH
        ,live_objects(NULL)
#endif
#ifdef _DEBUG
		,num_marks(0)
		,num_slots(0)
        ,num_mutable_ref(0)
		,num_mutable_refless(0)
		,num_immutable_ref(0)
		,num_immutable_refless(0)
        ,num_escape_live(0)
        ,num_stack_live(0)
        ,num_rs_live(0)
#endif
#if 0
#ifdef USE_STL_PN_ALLOCATOR
		, intra_slots(std::less<Partial_Reveal_Object**>(), PrivateNurseryAlloc<Partial_Reveal_Object**>(info))
		, intra_weak_slots(std::less<Partial_Reveal_Object**>(), PrivateNurseryAlloc<Partial_Reveal_Object**>(info))
		, inter_slots(std::less<Partial_Reveal_Object**>(), PrivateNurseryAlloc<Partial_Reveal_Object**>(info))
		, live_objects(std::less<Partial_Reveal_Object*>(), PrivateNurseryAlloc<Partial_Reveal_Object**>(info))
#endif
#endif
        , current_si(NULL)
        , new_pn(false)
#ifdef PUB_PRIV
        , staying_mark_bits(NULL)
#endif
	{
#ifdef _WINDOWS
		sum_micro_time.QuadPart = 0;
#elif defined LINUX
		sum_micro_time.tv_sec = 0;
		sum_micro_time.tv_usec = 0;
#endif
#ifdef PUB_PRIV
		sum_private_heap_time.QuadPart = 0;
#endif // PUB_PRIV
#ifdef DETAILED_PN_TIMES
		memset_time.QuadPart = 0;
		clear_time.QuadPart = 0;
		roots_and_mark_time.QuadPart = 0;
		allocate_time.QuadPart = 0;
		move_time.QuadPart = 0;
		update_root_time.QuadPart = 0;

		mark_prepare_time.QuadPart = 0;
	    mark_stackwalk_time.QuadPart = 0;
	    mark_push_roots_time.QuadPart = 0;
	    mark_all_time.QuadPart = 0;
	    mark_push_roots_escaping_time.QuadPart = 0;
	    mark_all_escaping_time.QuadPart = 0;
#ifdef CLEAR_STATS
		root_clear.QuadPart = 0;
		ipt_private_clear.QuadPart = 0;
		ipt_public_clear.QuadPart = 0;
		pi_clear.QuadPart = 0;
		intra_slots_clear.QuadPart = 0;
    	inter_slots_clear.QuadPart = 0;
    	live_objects_clear.QuadPart = 0;
#endif
#endif // DETAILED_PN_TIMES

#ifdef USE_STL_PN_ALLOCATOR
		intra_slots = new std::set<Partial_Reveal_Object **,std::less<Partial_Reveal_Object**>,LSAllocator>(std::less<Partial_Reveal_Object**>(), PrivateNurseryAlloc<Partial_Reveal_Object**>(m_info));
		intra_weak_slots = new std::set<Partial_Reveal_Object **,std::less<Partial_Reveal_Object**>,LSAllocator>(std::less<Partial_Reveal_Object**>(), PrivateNurseryAlloc<Partial_Reveal_Object**>(m_info));
		inter_slots = new std::set<Partial_Reveal_Object **,std::less<Partial_Reveal_Object**>,LSAllocator>(std::less<Partial_Reveal_Object**>(), PrivateNurseryAlloc<Partial_Reveal_Object**>(m_info));
		live_objects = new std::map<Partial_Reveal_Object*,Partial_Reveal_Object*,std::less<Partial_Reveal_Object*>,LSAllocator>(std::less<Partial_Reveal_Object*>(), PrivateNurseryAlloc<Partial_Reveal_Object**>(m_info));
#else // USE_STL_PN_ALLOCATOR
        intra_slots = new std::vector<intra_slot>;
		intra_weak_slots = new std::vector<Partial_Reveal_Object **>;
		inter_slots = new std::vector<inter_slot>;
#ifndef NEW_APPROACH
		live_objects = new ExpandInPlaceArray<PncLiveObject>;
#endif
#endif // USE_STL_PN_ALLOCATOR
		intra_slots->reserve(40);
		intra_weak_slots->reserve(40);
		inter_slots->reserve(40);
		mark_stack.reserve(40);

#ifdef CONCURRENT
		m_concurrent_gray_lock_cs = orp_synch_create_critical_section();
#endif // CONCURRENT

		if(info) {
			if(g_two_space_pn) {
				POINTER_SIZE_INT midpoint = (uintptr_t)info->start + (local_nursery_size / 2);
				midpoint &= ~(sizeof(void*)-1); // alignment

				char *the_end = (char*)info->start + local_nursery_size;

				two_spaces[0].set_start_end(info->start,(void*)midpoint);
				two_spaces[1].set_start_end((void*)midpoint,the_end);

				info->tls_current_free    = two_spaces[0].get_start();
				info->tls_current_ceiling = two_spaces[0].get_end();

				local_nursery_start = info->start;
				local_nursery_end   = the_end;
				currently_used_nursery_end = info->tls_current_ceiling;
				two_space_in_use = 0;
			} else {
				info->tls_current_free    = info->start;
				info->tls_current_ceiling = (char*)info->tls_current_free + local_nursery_size;

				local_nursery_start = info->start;
				local_nursery_end   = info->tls_current_ceiling;
				currently_used_nursery_end = info->tls_current_ceiling;
			}
		}

		if(adaptive_nursery_size) {
			static bool first_thread = true;

            adaptive_mode = ADAPTIVE_DECREASING;
			adaptive_middle = local_nursery_size * 2;
			adaptive_top = adaptive_middle * 2;
            adaptive_bottom = local_nursery_size;
			adaptive_sample = 0;
			adaptive_cumul_time = 0;
			adaptive_cumul_size = 0;
			adaptive_top_res = 10;    // kick-off the algorithm.  normal value will be less than 1.
			adaptive_middle_res = 5;
			if(first_thread) {
				adaptive_main_thread = true;
				first_thread = false;
			} else {
				adaptive_main_thread = false;
			}
		}

#ifdef CONCURRENT_DEBUG_2
#ifdef CONCURRENT
        if(cdump == NULL) {
            cdump = fopen("cmoves.txt","w");
        }
#endif
#endif
	}

    ~pn_info(void) {
        // deleting "NULL" is ok.
		delete intra_slots;
		delete intra_weak_slots;
		delete inter_slots;
#ifndef NEW_APPROACH
		delete live_objects;
#endif
    }

	void clear(void) {
#ifdef CLEAR_STATS
	    LARGE_INTEGER _detail_start_time, _detail_end_time;
	    gc_time_start_hook(&_detail_start_time);
		unsigned latest_time;
#endif
		m_roots.clear();
#ifdef CLEAR_STATS
	    QueryPerformanceCounter(&_detail_end_time);
		latest_time = get_time_in_microseconds(_detail_start_time, _detail_end_time);
		root_clear.QuadPart += latest_time;
#endif
        interior_pointer_table.reset();
#ifdef CLEAR_STATS
	    QueryPerformanceCounter(&_detail_end_time);
		latest_time = get_time_in_microseconds(_detail_start_time, _detail_end_time);
		ipt_private_clear.QuadPart += latest_time;
#endif
        interior_pointer_table_public.reset();
#ifdef CLEAR_STATS
	    QueryPerformanceCounter(&_detail_end_time);
		latest_time = get_time_in_microseconds(_detail_start_time, _detail_end_time);
		ipt_public_clear.QuadPart += latest_time;
#endif
		assert(mark_stack_is_empty(&mark_stack));
#ifdef RECORD_IMMUTABLE_COPIES
		promoted_immutables.clear();
#endif // RECORD_IMMUTABLE_COPIES
#ifdef CLEAR_STATS
	    QueryPerformanceCounter(&_detail_end_time);
		latest_time = get_time_in_microseconds(_detail_start_time, _detail_end_time);
		pi_clear.QuadPart += latest_time;
#endif
		intra_slots->clear();
		intra_weak_slots->clear();
#ifdef CLEAR_STATS
	    QueryPerformanceCounter(&_detail_end_time);
		latest_time = get_time_in_microseconds(_detail_start_time, _detail_end_time);
		intra_slots_clear.QuadPart += latest_time;
#endif
		inter_slots->clear();
#ifdef CLEAR_STATS
	    QueryPerformanceCounter(&_detail_end_time);
		latest_time = get_time_in_microseconds(_detail_start_time, _detail_end_time);
		inter_slots_clear.QuadPart += latest_time;
#endif
#ifndef NEW_APPROACH
		live_objects->clear();
#endif
#ifdef CLEAR_STATS
	    QueryPerformanceCounter(&_detail_end_time);
		latest_time = get_time_in_microseconds(_detail_start_time, _detail_end_time);
		live_objects_clear.QuadPart += latest_time;
#endif

#ifdef USE_STL_PN_ALLOCATOR
		m_info->tls_arena = free_all_but_one_arena(m_info->tls_arena);
#endif // USE_STL_PN_ALLOCATOR
#ifdef CONCURRENT
		new_moved_grays.clear();
#endif // CONCURRENT
        cspaces.clear();
    }

	inline void add_inter_slot(Partial_Reveal_Object **p_obj, Partial_Reveal_Object *base) {
		inter_slots->push_back(inter_slot(p_obj,base));
	}

	inline void add_intra_slot(Partial_Reveal_Object **p_obj, Partial_Reveal_Object *base) {
		intra_slots->push_back(intra_slot(p_obj,base));
	}

    inline void add_intra_weak_slot(Partial_Reveal_Object **p_obj) {
		intra_weak_slots->push_back(p_obj);
	}

    bool address_in_pn(void *addr) {
        if(addr >= local_nursery_start && addr < local_nursery_end) return true;
        else return false;
    }
};

extern unsigned g_use_pub_priv;

inline OBJECT_LOCATION get_object_location(void *p_obj, pn_info *cur_private_nursery) {
    if((uintptr_t)p_obj - (uintptr_t)cur_private_nursery->local_nursery_start < local_nursery_size) return PRIVATE_NURSERY;
    if(p_obj < p_global_gc->get_gc_heap_base_address() || p_obj > p_global_gc->get_gc_heap_ceiling_address()) return GLOBAL;
#ifdef PUB_PRIV
	if(g_use_pub_priv && GC_BLOCK_INFO(p_obj)->thread_owner != NULL)
		return PRIVATE_HEAP;
#endif // PUB_PRIV
    else return PUBLIC_HEAP;
} // get_object_location

//extern enum CONCURRENT_GC_STATE g_concurrent_gc_state;

//#define USE_GLOBAL_CONCURRENT_STATE_AS_LOCAL

#ifdef CONCURRENT
inline CONCURRENT_GC_STATE GetLocalConcurrentGcState(GC_Small_Nursery_Info *private_nursery) {
#ifdef USE_GLOBAL_CONCURRENT_STATE_AS_LOCAL
	return g_concurrent_gc_state;
#else
	return private_nursery->concurrent_state_copy;
#endif
}
#endif // CONCURRENT

class external_pointer {
public:
    Partial_Reveal_Object *base;
    Slot slot;
};

#ifdef _MSC_VER
__forceinline
#else
inline
#endif
REMOVE_INDIR_RES remove_one_indirection_not_pn(Partial_Reveal_Object *&p_obj, Slot p_slot,pn_info *local_collector, unsigned rmindir_mask) {
#ifdef _DEBUG
#if 0
    if(!(g_remove_indirections & (1 << rmindir_mask))) {
		return RI_NOTHING;
	}
#else
	if(g_remove_indirections) {
		--g_remove_indirections;
	}
#endif
#endif
    REMOVE_INDIR_RES ret = RI_NOTHING;
    if(g_remove_indirections && is_object_pointer(p_obj)) {
        // A => B1 => ... => Bn => C.  A is a regular object.  B1...Bn are indirection objects.  C is a regular object.
        // Get the vtable of a possible B.
        struct Partial_Reveal_VTable *indir_vtable = p_obj->vt();
        // See if that vtable is an indirection vtable.
        if(pgc_is_indirection(indir_vtable)) {
            // Compute the address of the indirection slot within the indirection object.
            // B + indirection_offset
            Partial_Reveal_Object ** new_value = (Partial_Reveal_Object**)((char*)p_obj + pgc_indirection_offset(indir_vtable));
            if(!local_collector->address_in_pn(*new_value)) {
                // Scan C as a live object instead of B.
                p_obj = *new_value;
                // Update the slot in A to point to C.
                p_slot.unchecked_update(p_obj);

#ifdef _DEBUG
                ++indirections_removed;
#endif
                // If the new slot value isn't a pointer then return true.
                if(!is_object_pointer(p_obj)) {
                    ret = RI_REPLACE_NON_OBJ;
                    return ret;
                }
                ret = RI_REPLACE_OBJ;
            }
        } else {
            return ret;
        }
    }
    return ret;
}
