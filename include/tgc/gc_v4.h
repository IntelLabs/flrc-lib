/*
 * COPYRIGHT_NOTICE_1
 */

#ifndef _gc_v4_H_
#define _gc_v4_H_

#include "tgc/gc_header.h"
#include "tgc/gc_cout.h"
#if _MSC_VER >= 1400 || defined __GNUC__ // vs2005+
#include <stack>
#include <vector>
#include <map>
#else
#include <..\stlport\stack>
#include <..\stlport\vector>
#include <..\stlport\map>
#endif
#ifdef LINUX
#include <string.h>
#endif // LINUX

// Check gc_for_orp.cpp for the intention for these definitions
#if defined(ORP_POSIX) && !defined(USE_GC_STATIC)

// #error "If this isn't regressed toss it out of the interface. RLH - 6/16/03"
#define GCEXPORT(a, b) extern "C" a internal_##b
#define INTERNAL(a) internal_##a
#define INTERNAL_FUNC_NAME(a) "internal_"##a

#else //!(defined(ORP_POSIX) && !defined(USE_GC_STATIC))

#define GCEXPORT(a, b) extern "C" a b
#define INTERNAL(a) a
#define INTERNAL_FUNC_NAME(a) a

#endif //defined(ORP_POSIX) && !defined(USE_GC_STATIC)



const int GC_MAX_CHUNKS = 32 * 1024;			// 512K per chunk, so this gives us 16GB heap max if needed


//
// These are the phases the GC can be in. The global current_gc_phase
// can be referenced by the write barriers to see what they should do.
//

enum gc_phase {
    normal,               // Not in a phase, threads can go up and come down.
    mark_phase,           // Threads are stopping enumeration is done, threads start, marking is done
                          // Write barriers are in place to prevent white to black pointers from being installed
    copy_phase,           // Copy objects and install forwarding ptrs. Read barriers are preventing old version from being used.
    flip_phase,           // Flipping refs to new copies of the objects in heap, globals and stacks, threads being stopped and stack being flipped.
}; // gc_phases

extern volatile gc_phase current_gc_phase;

extern unsigned g_remove_indirections;
extern bool g_cheney;

inline unsigned int GC_OBJECT_LOW_MARK_INDEX(Partial_Reveal_Object *P_OBJ, unsigned SIZE) {
    return ((((uintptr_t)P_OBJ & GC_BLOCK_LOW_MASK) >> GC_MARK_SHIFT_COUNT) - GC_NUM_MARK_BYTES_FOR_BLOCK_INFO_SIZE_BYTES);
}
inline unsigned int GC_OBJECT_HIGH_MARK_INDEX(Partial_Reveal_Object *P_OBJ, unsigned SIZE) {
	return (((((uintptr_t)P_OBJ + SIZE - 1) & GC_BLOCK_LOW_MASK) >> GC_MARK_SHIFT_COUNT) - GC_NUM_MARK_BYTES_FOR_BLOCK_INFO_SIZE_BYTES);
}
inline unsigned int GC_BLOCK_ADDR_FROM_MARK_INDEX(block_info *BLOCK_INFO, unsigned INDEX) {
	return (((INDEX + GC_NUM_MARK_BYTES_FOR_BLOCK_INFO_SIZE_BYTES) << GC_MARK_SHIFT_COUNT) + (uintptr_t) BLOCK_INFO);
}


// What should a GC thread do???
enum gc_thread_action {
	GC_MARK_SCAN_TASK = 0,
	GC_SWEEP_TASK,
	GC_OBJECT_HEADERS_CLEAR_TASK,
////////////////////////////// new tasks ////////////////////
	GC_INSERT_COMPACTION_LIVE_OBJECTS_INTO_COMPACTION_BLOCKS_TASK,
	GC_ALLOCATE_FORWARDING_POINTERS_FOR_COMPACTION_LIVE_OBJECTS_TASK,
	GC_FIX_SLOTS_TO_COMPACTION_LIVE_OBJECTS_TASK,
	GC_SLIDE_COMPACT_LIVE_OBJECTS_IN_COMPACTION_BLOCKS,
//	GC_RESTORE_HIJACKED_HEADERS,
	GC_CLEAR_MARK_BIT_VECTORS,
////////////////////////////// //////////////////////////////
	GC_BOGUS_TASK
};

typedef struct {
	uint8 *p_start_byte;
	unsigned int start_bit_index;
	uint8 *p_non_zero_byte;
	unsigned int bit_set_index;
	uint8 *p_ceil_byte;
} set_bit_search_info;



typedef struct {
	block_info *free_chunk;
	block_info *chunk;
} chunk_info;

#ifdef GC_BREADTH_FIRST_MARK_SCAN

// Make sort of large for specjbb because of the size of the stack needed (SPECJBB seems to create broad rather than deep structures)
#if 0
const int GC_MARK_STACK_SIZE = 1024 * 256;

typedef struct {
	Partial_Reveal_Object *mark_stack[GC_MARK_STACK_SIZE];
	unsigned int push_index;
	unsigned int pop_index;;
	unsigned int num_grey_objects;
} MARK_STACK;
#endif

#else

#ifdef ONE_MARK_STACK
#else
#ifdef STL_MARK_STACK
#else
#endif
#endif

#ifdef ONE_MARK_STACK
const int GC_MARK_STACK_SIZE = 1024 * 256;


typedef struct {
	Partial_Reveal_Object *mark_stack[GC_MARK_STACK_SIZE];
	unsigned int bottom;
	unsigned int top;
} MARK_STACK;

#else

#ifdef STL_MARK_STACK
typedef std::stack<Partial_Reveal_Object*,std::vector<Partial_Reveal_Object*> > MARK_STACK;
#else

const int GC_MARK_STACK_SIZE = 1024 * 256;
class PART_MARK_STACK {
protected:
	Partial_Reveal_Object *mark_stack[GC_MARK_STACK_SIZE];
	unsigned int m_top;
public:
	PART_MARK_STACK(void) : m_top(0) {}
	bool empty(void) const { return m_top == 0; }
	bool full(void)  const { return m_top == GC_MARK_STACK_SIZE; }
	Partial_Reveal_Object * top(void) {
		if(!empty()) return mark_stack[m_top - 1];
		orp_cout << "GC: request for top of empty mark stack" << std::endl;
		orp_exit(18003);
	}
	void pop(void) {
		if(!empty()) {
			--m_top;
			return;
		}
		orp_cout << "GC: request for pop of empty mark stack" << std::endl;
		orp_exit(18003);
	}
	bool push(Partial_Reveal_Object *p_obj) {
		if(!full()) {
			mark_stack[m_top++] = p_obj;
			return full();
		}
		orp_cout << "GC: request for push on full mark stack" << std::endl;
		orp_exit(18003);
	}
};

class MARK_STACK {
protected:
	std::vector<PART_MARK_STACK *> m_data;
	unsigned m_in_use;
	PART_MARK_STACK *m_cur_stack;
public:
	MARK_STACK(void) {
		m_in_use = 0;
		m_data.push_back(m_cur_stack = new PART_MARK_STACK());
	}
   ~MARK_STACK(void) {
	   unsigned i;
	   for(i = 0; i < m_data.size(); ++i) {
		   delete m_data[i];
	   }
    }
	bool empty(void) const {
		if(m_in_use == 0) return m_cur_stack->empty();
		return false;
	}
	Partial_Reveal_Object * top(void) {
		if(m_cur_stack->empty()) {
			if(m_in_use) {
				return m_data[m_in_use-1]->top();
			} else {
				orp_cout << "GC: request for top of empty mark stack" << std::endl;
				orp_exit(18004);
			}
		}
		return m_cur_stack->top();
	}
	void pop(void) {
		if(m_cur_stack->empty()) {
			if(m_in_use) {
				--m_in_use;
				m_cur_stack = m_data[m_in_use];
			} else {
				orp_cout << "GC: request for pop of empty mark stack" << std::endl;
				orp_exit(18004);
			}
		}
		m_cur_stack->pop();
	}
	void push(Partial_Reveal_Object *p_obj) {
		bool now_full = m_cur_stack->push(p_obj);
		if(now_full) {
			++m_in_use;
			if(m_in_use >= m_data.size()) {
				m_data.push_back(new PART_MARK_STACK());
			}
			m_cur_stack = m_data[m_in_use];
		}
	}
};
#endif
#endif

#endif // GC_BREADTH_FIRST_MARK_SCAN


typedef struct {
	unsigned int average_number_of_free_areas;
	unsigned int average_size_per_free_area;
} chunk_sweep_stats;






//////////////////////////////////////////////////////////////////////////////////////////////
class GC_Thread;
//////////////////////////////////////////////////////////////////////////////////////////////


#ifndef ORP_POSIX
extern void clear_block_free_areas(block_info *);
#endif // ORP_POSIX
block_info *p_get_new_block(bool );
Partial_Reveal_Object  *gc_pinned_malloc(unsigned size, Allocation_Handle, bool return_null_on_fail, bool double_align, GC_Thread_Info *tls_for_gc);

void verify_object (Partial_Reveal_Object *p_object, POINTER_SIZE_INT );
void verify_block (block_info *);
void verify_marks_for_all_live_objects();

bool mark_object_in_block(Partial_Reveal_Object *);
inline bool mark_header_and_block_non_atomic(Partial_Reveal_Object *obj) {
    if(! obj->isMarked()) {
    	obj->mark();
	    mark_object_in_block(obj);
        return true;
    }
    return false;
}

inline bool mark_header_and_block_atomic(Partial_Reveal_Object *obj) {
    if(! obj->isMarked()) {
    	if(obj->atomic_mark()) {
    	    mark_object_in_block(obj);
            return true;
        }
    }
    return false;
}

void mark_scan_heap(GC_Thread *);

void verify_marks_for_live_object(Partial_Reveal_Object *);

extern unsigned int num_roots_added;



//////////////////////////// V A R I A B L E S /////////////////////////////////////////////

#ifdef _WINDOWS
extern LARGE_INTEGER performance_frequency;
#endif // _WINDOWS



/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////


static inline void
clear_block_free_areas(block_info *block) {
	for (unsigned i = 0; i < block->size_block_free_areas; i++) {
		memset(&(block->block_free_areas[i]), 0, sizeof(free_area));
	}
}

static inline void
zero_out_sweep_stats(chunk_sweep_stats *stats) {
	assert(stats);
	memset(stats, 0, sizeof(chunk_sweep_stats) * GC_MAX_CHUNKS);
}


static inline void
zero_out_mark_stack(MARK_STACK *ms) {
#if 0
	assert(ms);
	memset(ms, 0, sizeof(MARK_STACK));
#endif
}


#ifdef _DEBUG
static inline void
check_mark_stack_is_all_null(MARK_STACK *ms) {
#if 0
	for (int i = 0; i < GC_MARK_STACK_SIZE; i++) {
		assert(ms->mark_stack[i] == NULL);
	}
#endif
}
#endif // _DEBUG

#ifdef ORP_POSIX
#define LOCK_PREFIX "lock"
#endif // ORP_POSIX


static inline
POINTER_SIZE_INT LockedCompareExchangePOINTER_SIZE_INT(
						POINTER_SIZE_INT *Destination,
						POINTER_SIZE_INT Exchange,
						POINTER_SIZE_INT Comperand
						) {
#ifdef _IA64_
	return (POINTER_SIZE_INT) InterlockedCompareExchangePointer((PVOID *)Destination, (PVOID)Exchange, (PVOID)Comperand);
	assert(0);
	orp_exit(17057);
#else // !_IA64_

#ifdef ORP_POSIX
        __asm__(
                LOCK_PREFIX "\tcmpxchg %1, (%2)"
                :"=a"(Comperand)
                :"d"(Exchange), "r"(Destination), "a"(Comperand)
                );
#else // ORP_POSIX

	__asm {
		mov eax,  Comperand
		mov edx,  Exchange
		mov ecx, Destination
		lock cmpxchg [ecx], edx
		mov Comperand, eax
	}
#endif

	return Comperand;

#endif // !_IA64_
}




///////////////////////////////// M A R K  S T A C K  (begin)  /////////////////////////


#ifdef GC_BREADTH_FIRST_MARK_SCAN

static inline unsigned int
number_of_objects_in_mark_stack(MARK_STACK *ms) {
	// do a bunch of asserts to check integrity of mark stack state variables...
	assert(ms->pop_index < GC_MARK_STACK_SIZE);
	assert(ms->push_index < GC_MARK_STACK_SIZE);

	if (ms->pop_index == ms->push_index) {
		assert((ms->num_grey_objects == 0) || (ms->num_grey_objects == GC_MARK_STACK_SIZE));
	} else {
		// something in the stack...
		if (ms->pop_index < ms->push_index) {
			assert(ms->num_grey_objects == (ms->push_index - ms->pop_index));
		} else {
			assert(ms->num_grey_objects == (GC_MARK_STACK_SIZE - (ms->pop_index - ms->push_index)));
		}
	}

	// cant have more than what it can hold!!
	assert(ms->num_grey_objects <= GC_MARK_STACK_SIZE);

	return ms->num_grey_objects;
}


static inline bool
mark_stack_is_empty(MARK_STACK *ms) {
	assert(ms->num_grey_objects == number_of_objects_in_mark_stack(ms));

	if (ms->num_grey_objects == 0) {
		assert(ms->pop_index == ms->push_index);
		// no elements in the stack...then this is true
		assert(ms->mark_stack[ms->pop_index] == NULL);
		// this is always true....
		assert(ms->mark_stack[ms->push_index] == NULL);
		// of course this one...
		return true;
	} else {
		// there is at least one element in the stack..if push and pop are equal...mark stack is full...
		assert((ms->pop_index != ms->push_index) || (ms->num_grey_objects == GC_MARK_STACK_SIZE));
		assert(ms->mark_stack[ms->pop_index] != NULL);
		return false;
	}
}

static inline bool
mark_stack_is_full(MARK_STACK *ms) {
	assert(ms->num_grey_objects == number_of_objects_in_mark_stack(ms));
	return (ms->num_grey_objects == GC_MARK_STACK_SIZE);
}


static inline void
adjust_pop_index(MARK_STACK *ms) {
	(ms->pop_index)++;
	if (ms->pop_index == GC_MARK_STACK_SIZE) {
		// pop_index falls off the right edge of mark stack
		ms->pop_index = 0;
	}
}


static inline void
adjust_push_index(MARK_STACK *ms) {
	(ms->push_index)++;
	if (ms->push_index == GC_MARK_STACK_SIZE) {
		// pop_index falls off the right edge of mark stack
		ms->push_index = 0;
	}
}


static inline Partial_Reveal_Object *
pop_bottom_object_from_local_mark_stack(MARK_STACK *ms) {
	if (mark_stack_is_empty(ms)) {
		return NULL;
	}

	Partial_Reveal_Object *p_ret = ms->mark_stack[ms->pop_index];
	assert(p_ret);
	ms->mark_stack[ms->pop_index] = NULL;

	adjust_pop_index(ms);

	// decrement the stack size...
	(ms->num_grey_objects)--;

	// we just removed something....the mark stack cant be full now...
	assert(!mark_stack_is_full(ms));

	return p_ret;
}



static inline bool
push_bottom_on_local_mark_stack(Partial_Reveal_Object *p_obj, MARK_STACK *ms) {
    Partial_Reveal_Object *usable_object = (Partial_Reveal_Object*)((POINTER_SIZE_INT)p_obj & ~0x3);
#ifdef _DEBUG
	verify_object (usable_object, (POINTER_SIZE_INT) get_object_size_bytes(usable_object));
#endif

	if (mark_stack_is_full(ms)) {
		printf("MARK STACK OVERFLOWED.....\n");
		orp_exit(17058);
		return false;
	}

	// need empty slot...
	assert(ms->mark_stack[ms->push_index] == NULL);
	ms->mark_stack[ms->push_index] = p_obj;

	adjust_push_index(ms);

	// increment the stack size...
	(ms->num_grey_objects)++;

	// we just pushed something...mark stack cant be empty
	assert(!mark_stack_is_empty(ms));

	// all done...
	return true;
}


#else

// DEFAULT depth first traversal...

static inline void
debug_check_mark_stack_is_empty(MARK_STACK *ms) {
#if 0
	assert(ms->bottom == ms->top);
	assert(ms->top == 0);
#else
	assert(ms->empty());
#endif
}



static inline Partial_Reveal_Object *
pop_bottom_object_from_local_mark_stack(MARK_STACK *ms) {
#if 0
	// assert(ms->bottom >= 0); // pointless comparison: is unsigned
	assert(ms->bottom <= GC_MARK_STACK_SIZE);

	if (ms->bottom == ms->top) {
		// Mark stack is empty
		assert(ms->bottom == 0);
		assert(ms->mark_stack[ms->bottom] == NULL);
		return NULL;
	}

	assert(ms->bottom > 0);
	// We know the mark stack is not empty;
	Partial_Reveal_Object *p_ret = ms->mark_stack[ms->bottom - 1];
	// Means better not have a null reference
	assert(p_ret != NULL);
	// Null that entry
	ms->mark_stack[ms->bottom - 1] = NULL;
	// Adjust TOS
	(ms->bottom)--;

	return p_ret;
#else
	if(ms->empty()) {
		return NULL;
	} else {
		Partial_Reveal_Object *ret = ms->top();
		ms->pop();
		return ret;
	}
#endif
}


static inline bool
push_bottom_on_local_mark_stack(Partial_Reveal_Object *p_obj, MARK_STACK *ms) {
    Partial_Reveal_Object *usable_object = (Partial_Reveal_Object*)((uintptr_t)p_obj & ~0x3);
#ifdef _DEBUG
	verify_object (usable_object, (POINTER_SIZE_INT) get_object_size_bytes(usable_object));
#endif
#if 0
	// assert(ms->bottom >= 0); // pointless comparison
	assert(ms->bottom <= GC_MARK_STACK_SIZE);

	if (ms->bottom >= GC_MARK_STACK_SIZE) {
		// I cannot push/add any more.
		assert(0);
		printf("Mark stack overflowed\n");
		orp_exit(17059);
		return false;
	}
	// TOS is NULL always
	assert(ms->mark_stack[ms->bottom] == NULL);
	// Copy object to TOS
	ms->mark_stack[ms->bottom] = p_obj;
	// Increment TOS ptr
	(ms->bottom)++;

	// New TOS should have NULL slot
	assert(ms->mark_stack[ms->bottom] == NULL);

	return true;
#else
	ms->push(p_obj);
	return true;
#endif
}

#if 0
static inline unsigned int
number_of_objects_in_mark_stack(MARK_STACK *ms) {
	if ((ms->bottom == ms->top) && (ms->bottom == 0)) {
		assert(ms->mark_stack[ms->bottom] == NULL);
		return 0;
	}
	// At least one object in mark stack
	return (ms->bottom - ms->top);
}
#endif

static inline bool
mark_stack_is_empty(MARK_STACK *ms) {
#if 0
	if ((ms->bottom == ms->top) && (ms->bottom == 0)) {
		assert(ms->mark_stack[ms->bottom] == NULL);
		return true;
	} else {
		assert(ms->mark_stack[ms->bottom - 1] != NULL);
		return false;
	}
#else
	return ms->empty();
#endif
}

#endif // GC_BREADTH_FIRST_MARK_SCAN


///////////////////////////////// M A R K  S T A C K  (end)  /////////////////////////////////////////////////////////////////



////////////////////////////////// M E A S U R E M E N T /////////////////////////////////////////////////////////////////////



#ifdef _WINDOWS
static inline __int64 get_microseconds(LARGE_INTEGER time) {
    return time.QuadPart;
}

static inline unsigned long
get_time_in_milliseconds(LARGE_INTEGER StartCounter, LARGE_INTEGER EndCounter) {
	return (unsigned long)
			((double)(EndCounter.QuadPart - StartCounter.QuadPart) / (double)performance_frequency.QuadPart * 1000.0);
}

static inline unsigned long
get_time_in_microseconds(LARGE_INTEGER StartCounter, LARGE_INTEGER EndCounter) {
	return (unsigned long)
			((double)(EndCounter.QuadPart - StartCounter.QuadPart) / (double)performance_frequency.QuadPart * 1000000);
}

static inline void
gc_time_start_hook(LARGE_INTEGER *start_time) {
	QueryPerformanceCounter(start_time);
}

static inline unsigned int
gc_time_end_hook(const char *event, LARGE_INTEGER *start_time, LARGE_INTEGER *end_time, bool print) {
    QueryPerformanceCounter(end_time);
    unsigned int time = get_time_in_milliseconds(*start_time, *end_time);
	if (print) {
        printf ("%s: %ums\n", event, time);
		//orp_cout << event << ":" << time << "ms\n";
	}
	return time;
}

static inline unsigned int
gc_time_end_hook_micros(const char *event, LARGE_INTEGER *start_time, LARGE_INTEGER *end_time, bool print) {
    QueryPerformanceCounter(end_time);
    unsigned int time = get_time_in_microseconds(*start_time, *end_time);
	if (print) {
        printf ("%s: %uus\n", event, time);
		//orp_cout << event << ":" << time << "ms\n";
	}
	return time;
}

#elif defined LINUX
#include <sys/time.h>

static inline long long get_microseconds(struct timeval &time) {
    return (time.tv_sec * 1000000) + time.tv_usec;
}

static inline void add_microseconds(struct timeval &time, unsigned microseconds) {
    time.tv_usec += microseconds;
    if(time.tv_usec > 1000000) {
        unsigned secs_to_add = time.tv_usec / 1000000;
        time.tv_sec += secs_to_add;
        time.tv_usec -= (secs_to_add * 1000000);
    }
}

static inline unsigned long
get_time_in_milliseconds(struct timeval StartCounter, struct timeval EndCounter) {
	return ((EndCounter.tv_sec - StartCounter.tv_sec) * 1000) + ((EndCounter.tv_usec - StartCounter.tv_usec) / 1000);
}

static inline unsigned long
get_time_in_microseconds(struct timeval StartCounter, struct timeval EndCounter) {
	return ((EndCounter.tv_sec - StartCounter.tv_sec) * 1000000) + ((EndCounter.tv_usec - StartCounter.tv_usec));
}

static inline void
gc_time_start_hook(struct timeval *start_time) {
    gettimeofday(start_time,NULL);
}

static inline unsigned int
gc_time_end_hook(const char *event, struct timeval *start_time, struct timeval *end_time, bool print) {
    gettimeofday(end_time,NULL);
    unsigned int time = get_time_in_milliseconds(*start_time, *end_time);
	if (print) {
        printf ("%s: %ums\n", event, time);
		//orp_cout << event << ":" << time << "ms\n";
	}
	return time;
}

static inline unsigned int
gc_time_end_hook_micros(const char *event, struct timeval *start_time, struct timeval *end_time, bool print) {
    gettimeofday(end_time,NULL);
    unsigned int time = get_time_in_microseconds(*start_time, *end_time);
	if (print) {
        printf ("%s: %uus\n", event, time);
		//orp_cout << event << ":" << time << "ms\n";
	}
	return time;
}

#endif


///////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Centralize all mallocs done in the GC to this function.
//

void *malloc_or_die(unsigned int size);

///////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef _DEBUG

void trace_object (void *obj_to_trace);

void gc_trace (void *object, const char *string_x);

void gc_trace_block (void *, const char *);

void gc_trace_slot (void **, void *, const char *);

void gc_trace_allocation (void *, const char *);

#else

inline void trace_object (void *obj_to_trace) {
    return;
}

inline void gc_trace_slot (void **object_slot, void *object, const char *string_x)
{
    return;
}

inline void gc_trace(void *object, const char *string_x)
{
    return;
    // set up a latency free routine when not debugging....
}

// gc_trace is defined in gc_for_orp.h for some reason.
static inline void gc_trace_block (void *foo, const char *moo)
{
    return;
}

inline void gc_trace_allocation (void *object, const char *string_x)
{
    return;
}

#endif // _DEBUG

#ifdef CONCURRENT

template <class T,class U>
class bimap {
protected:
    typedef std::map<T,U> from_type;
    typedef std::map<U,T> to_type;

	from_type from_map;
	to_type   to_map;
public:
	unsigned insert(const std::pair<T,U> &value) {
		if(from_map.find(value.first) != from_map.end()) {
			return 1;
		}
		if(to_map.find(value.second) != to_map.end()) {
			return 2;
		}
		from_map.insert(std::pair<T,U>(value.first,value.second));
		to_map.insert(std::pair<U,T>(value.second,value.first));
		return 0;
	}

	void clear(void) {
		from_map.clear();
		to_map.clear();
	}

	unsigned size(void) {
		assert(from_map.size() == to_map.size());
		return from_map.size();
	}

	unsigned erase(const T& t) {
		typename from_type::iterator iter = from_map.find(t);
		if(iter == from_map.end()) return 1;

		to_map.erase(iter->second);
		from_map.erase(t);

		return 0;
	}

	unsigned erase(typename from_type::iterator iter) {
		if(iter == from_map.end()) return 1;

		to_map.erase(iter->second);
		from_map.erase(iter);

		return 0;
	}

	typename from_type::iterator find(const T &t) {
		return from_map.find(t);
	}
	typename from_type::iterator begin(void) {
		return from_map.begin();
	}
	typename from_type::iterator end(void) {
		return from_map.end();
	}

	typename to_type::iterator inverse_find(const U &u) {
		return to_map.find(u);
	}
	typename to_type::iterator inverse_begin(void) {
		return to_map.begin();
	}
	typename to_type::iterator inverse_end(void) {
		return to_map.end();
	}
};

enum CONCURRENT_SCAN_MODE {
	NORMAL = 0,
	UPDATE_MOVED_SLOTS = 1
};

class ImmutableMoveInfo {
public:
	Partial_Reveal_Object *m_new_location;
	unsigned m_gc_move_number; // number of GC during which this move was issued

	ImmutableMoveInfo(Partial_Reveal_Object *new_location,unsigned gc_move_number=0) :
	    m_new_location(new_location),
		m_gc_move_number(gc_move_number) {}

	bool operator<(const ImmutableMoveInfo &rhs) const {
		return m_new_location < rhs.m_new_location;
	}
};

typedef std::map<Partial_Reveal_Object*,ImmutableMoveInfo>::iterator MovedObjectIterator;
typedef std::map<ImmutableMoveInfo,Partial_Reveal_Object*>::iterator MovedObjectInverseIterator;
extern bimap<Partial_Reveal_Object*,ImmutableMoveInfo> g_moved_objects;
#endif // CONCURRENT

class EscapeIpInfo {
public:
    unsigned m_count;
	char     m_description[1000];

	EscapeIpInfo(unsigned count) : m_count(count) {}
};

extern bool g_gen;
extern bool g_gen_all;
bool is_young_gen(Partial_Reveal_Object *p_obj);
inline bool is_young_gen_collection(void) {
    return g_gen && !g_gen_all;
}

inline bool is_tagged_pointer(Partial_Reveal_Object *p_obj) {
    if((uintptr_t)p_obj & 0x3) return false;
    return true;
}

inline bool is_object_pointer(Partial_Reveal_Object *p_obj) {
    if(!p_obj) return false;
    if((uintptr_t)p_obj & 0x3) return false;
    return true;
}

extern "C" void CDECL_FUNC_OUT (CDECL_FUNC_IN *gc_mark_profiler)(unsigned gc_num, unsigned gc_thread_id, void * live_object);

#endif // _gc_v4_H_
