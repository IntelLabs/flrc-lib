/*
 * COPYRIGHT_NOTICE_1
 */

#include "flrclibconfig.h"

#ifndef __GNUC__
#include <windows.h>
#endif
#include <stdio.h>
#include <assert.h>

#include "pgc/pgc.h"
#include <stdlib.h>

#ifdef HAVE_PTHREAD_H
#include <pthread.h>
#else // HAVE_PTHREAD_H
#include <mcrt.h>
#endif // HAVE_PTHREAD_H
#include "prt/prtcodegenerator.h"

#ifdef WIN32
#include <Imagehlp.h>
#endif // WIN32

#include <list>
#ifdef __GNUC__
#include <sys/time.h>
#include <string.h>
#endif // __GNUC__

// Uncomment to trace prscall stealing state transitions
//#define TRACE_THREAD_SUSPENSION 1

#define INFINITE_TIME -1

unsigned GC_TLS_SIZE_SLOTS = 300;  // the number of register sized slots needed by GC in TLS; // JMS: Add 128 bytes to avoid cache line ping-pong effects.

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

struct Object {
protected:
    VTable *vt;
public:
    // TODO: update for 64-bit
    VTable * get_vtable();
	void set_vtable(VTable *new_vt);
};

typedef struct {
    bool is_for_field;  // we only use type_info for fields of a class or for types of array elements
    int  field_index;   // only used if is_for_field is true
    int  field_offset;
    struct clss *clss;
} FieldTypeInfo;


typedef struct clss {
    VTable array_vt;                    // if an array, the VTable for the class of the elements

    const char *name;

    unsigned int allocated_size;        // size in bytes of the fixed portion
    PgcIsRef *is_ref;                   // points to an array describing which of the fixed fields are refs
    PgcIsRef *is_weak_ref;              // points to an array describing which of the fixed fields are refs
    unsigned fixed_part_has_ref;        // 0 if the fixed portion has no ref fields; otherwise the count of those fields

    unsigned array_item_size;           // if an array, the element size in bytes; otherwise 0
    unsigned adjusted_item_size;        // if an array, the array element size in bytes rounded up to support alignment
    PgcIsRef *array_item_field_ref;     // points to an array describing which pointer-sized-words of the element type are refs
    struct AlignmentInfo alignment;     // whether array elements are aligned on 4- or 8-byte boundaries
    unsigned array_item_has_ref;        // 0 if no ref elements; otherwise the count of those ref elements

    FieldTypeInfo *field_ft_info;       // points to an array of FieldTypeInfo structs describing each fixed field
    FieldTypeInfo array_element_ft_info;// if an array, a FieldTypeInfo struct that describes the array elements
    bool   is_array_elem_vt;            // true if this class is the element type of an array
    struct clss *containing_clss_type;  // if an array, the type (clss*) for the enclosing type
    bool is_pinned;
    void (*finalizer)(Managed_Object_Handle obj);

    VTable *vt;
} Clss;

//typedef POINTER_SIZE_INT Allocation_Handle;

extern "C" {
void gc_init();
void gc_wrapup();
void gc_force_gc();
void gc_next_command_line_argument(const char *name, const char *arg);
void gc_class_prepared(Class_Handle ch, VTable_Handle vth);
void gc_thread_init(void *gc_information, ThreadThreadHandle thread_handle);
void gc_thread_kill(void *gc_information);
void gc_add_root_set_entry(Managed_Object_Handle *mref, Boolean is_pinned);
void gc_add_root_set_entry_interior_pointer (void **slot, int offset, Boolean is_pinned);
void gc_add_root_set_entry_nonheap(Managed_Object_Handle *mref);
Managed_Object_Handle gc_malloc_or_null_with_thread_pointer(unsigned size, Allocation_Handle type, void *thread_pointer);
Managed_Object_Handle gc_malloc_with_thread_pointer(unsigned size, Allocation_Handle type, void *thread_pointer);
Boolean gc_update_vtable(Managed_Object_Handle object, Allocation_Handle new_vtable);
void gc_set_wpo_vtable(void *vt);
void gc_set_wpo_finalizer(void (*finalizer)(void *));
Boolean gc_can_allocate_without_collection(unsigned size, void *thread_pointer);
void gc_require_allocate_without_collection(unsigned size, void *thread_pointer);
}

static SynchCriticalSectionHandle g_gc_lock = NULL;
static PgcRuntimeCallback pgc_g_runtime_callback=NULL;
static int g_update_threads_on_lock=0;
//static char *g_data_start;
//static char *g_data_end;
//static VTable reduced_space_vt;
void TaskNotification(PrtTaskHandle pth, PrtBool is_new_task);
static bool use_char = false;

#define pgc_started() (g_gc_lock != NULL)

static void **globalRoots = NULL;
static unsigned numGlobalRoots = 0;
static void *globalRootAddressLow = NULL;
static void *globalRootAddressHigh = NULL;
static void **globalRefs = NULL;
static unsigned numGlobalRefs = 0;

typedef struct {
public:
#ifdef HAVE_PTHREAD_H
    int auto_reset_flag;
    int state;
    pthread_mutex_t *event_mutex;
    pthread_cond_t  *event_condition;
#else // HAVE_PTHREAD_H
    McrtBool auto_reset_flag;
#ifdef USE_MCRT_CLH_LOCK
    McrtClhLock *mcrt_clhlock;
#else // clh lock
    McrtEvent *mcrt_event;
#endif // clh lock
#endif // HAVE_PTHREAD_H
} threadEvent;

extern "C" uint32_t heapGetG4(ManagedObject *object, uint32_t offset);
extern "C" void     heapSetG4(ManagedObject *object, uint32_t offset, uint32_t value);
extern "C" uint64_t heapGetG8(ManagedObject *object, uint32_t offset);
extern "C" void     heapSetG8(ManagedObject *object, uint32_t offset, uint64_t value);

uint32_t orp_heap_get_uint32_t(ManagedObject *object, uint32_t offset) {
    return heapGetG4(object, offset);
}

void orp_heap_set_uint32_t(ManagedObject *object, uint32_t offset, uint32_t value) {
    heapSetG4(object, offset, value);
}

uint64_t orp_heap_get_uint64_t(ManagedObject *object, uint32_t offset) {
    return heapGetG8(object, offset);
}

void orp_heap_set_uint64_t(ManagedObject *object, uint32_t offset, uint64_t value) {
    heapSetG8(object, offset, value);
}


VTable * Object::get_vtable() {
#ifdef __x86_64__
    // mask off the two low-order bits that may be used for flags by the GC
    return (VTable*)(heapGetG8((ManagedObject*)&vt,0) & ~0x3);
#else
    // mask off the two low-order bits that may be used for flags by the GC
    return (VTable*)(heapGetG4((ManagedObject*)&vt,0) & ~0x3);
#endif
}

void Object::set_vtable(VTable *new_vt) {
#ifdef __x86_64__
	heapSetG8((ManagedObject*)&vt,0,(uint64_t)new_vt);
#else
	heapSetG4((ManagedObject*)&vt,0,(uintptr_t)new_vt);
#endif
}

extern "C" unsigned short pgc_is_vtable_immutable(struct VTable *vt)
{
	return vt->mutability == PGC_ALWAYS_IMMUTABLE;
}

unsigned num_vtables_allocated = 0;

#ifdef HAVE_PTHREAD_H
pthread_mutex_t pgc_vtable_lock;
#endif
std::list<struct VTable*> pgc_vtable_list;

extern "C" void pgc_new_indirection_object(struct VTable *new_vt,
                                           unsigned size,
                                           unsigned indirection_ref_offset) {
    ++num_vtables_allocated;

#ifdef __GNUC__
    Clss *new_clss = NULL;
    if(posix_memalign((void**)&new_clss,16,sizeof(Clss)) != 0) {
        printf("posix_memalign failed!\n");
        exit(-1);
    }
#else // __GNUC__
    Clss *new_clss = (Clss *)_aligned_malloc(sizeof(Clss),16);
#endif // __GNUC__
	new_clss->name = "pgc indirection class";

	new_vt->pre_tenure = 0; // everything starts off not being pre-tenured

    new_vt->clss = new_clss;
    if((uintptr_t)new_vt % 16 != 0) {
        printf("Incoming vtable is not 16-byte aligned %p!!!\n",new_vt);
    }
    if((uintptr_t)&(new_clss->array_vt) % 16 != 0) {
        printf("New class array_vt field is not 16-byte aligned!!!\n");
    }

    new_vt->indirection_offset = indirection_ref_offset;
    if(!indirection_ref_offset) {
        printf("0 indirection offset in call to pgc_new_indirection_object\n");
        exit(-1);
    }
    new_clss->vt = new_vt;
    new_vt->allocated_size   = size;
    new_clss->allocated_size = size;
    new_vt->array_length_offset = 0;
    new_clss->alignment.alignArray = false;
    new_clss->alignment.powerOfTwoBaseFour = 0;
    new_clss->finalizer = NULL;
    new_clss->is_pinned = false;
    new_clss->array_item_size = 0;
    new_clss->adjusted_item_size = 0;
    new_vt->array_element_size = 0;

    unsigned int max_refs = (size / (sizeof(void*)));
    new_clss->is_ref = (PgcIsRef *)malloc(sizeof(PgcIsRef) * max_refs);
    memset(new_clss->is_ref, 0, (sizeof(PgcIsRef) * max_refs));
    new_clss->is_ref[indirection_ref_offset / sizeof(void*)] = 1;
    new_clss->is_weak_ref = (PgcIsRef *)malloc(sizeof(PgcIsRef) * max_refs);
    memset(new_clss->is_weak_ref, 0, (sizeof(PgcIsRef) * max_refs));

    new_clss->fixed_part_has_ref = 1;
    unsigned int i;

    new_vt->array_element_size = 0;
    new_clss->array_item_field_ref = NULL;
    new_clss->array_item_has_ref = 0;
	new_vt->mutability = PGC_ALWAYS_IMMUTABLE;

    new_clss->field_ft_info = (FieldTypeInfo*)malloc(max_refs * sizeof(FieldTypeInfo));
    for(i=0;i<max_refs;++i) {
        new_clss->field_ft_info[i].is_for_field = true;
        new_clss->field_ft_info[i].clss = new_clss;
        new_clss->field_ft_info[i].field_index = i;
        new_clss->field_ft_info[i].field_offset = i*sizeof(POINTER_SIZE_INT);
    }

    gc_class_prepared(new_clss,new_vt);

#ifdef HAVE_PTHREAD_H
    pthread_mutex_lock(&pgc_vtable_lock);
#endif
    pgc_vtable_list.push_back(new_vt);
#ifdef HAVE_PTHREAD_H
    pthread_mutex_unlock(&pgc_vtable_lock);
#endif
}

extern "C" void pgc_new_object_format(
									  struct VTable *new_vt,
                                      unsigned size_of_fixed,   // in units of bytes
                                      PgcIsRef is_ref[],
                                      unsigned array_item_size, // in units of bytes
                                      unsigned array_length_offset,
                                      PgcIsRef array_item_field_ref[],
                                      struct AlignmentInfo alignment,
									  enum PGC_MUTABILITY mutability,
                                      unsigned is_pinned,
                                      PgcIsRef weak_refs[],
                                      void (*finalizer)(Managed_Object_Handle))
{
    ++num_vtables_allocated;

//    Clss *new_clss = (Clss *)malloc(sizeof(Clss));
#ifdef __GNUC__
    Clss *new_clss = NULL;
    if(posix_memalign((void**)&new_clss,16,sizeof(Clss)) != 0) {
        printf("posix_memalign failed!\n");
        exit(-1);
    }
#else // __GNUC__
    Clss *new_clss = (Clss *)_aligned_malloc(sizeof(Clss),16);
#endif // __GNUC__
	new_clss->name = "pgc class";

	new_vt->pre_tenure = 0; // everything starts off not being pre-tenured

    new_vt->clss = new_clss;
    if((uintptr_t)new_vt % 16 != 0) {
        printf("Incoming vtable is not 16-byte aligned %p!!!\n",new_vt);
    }
    if((uintptr_t)&(new_clss->array_vt) % 16 != 0) {
        printf("New class array_vt field is not 16-byte aligned!!!\n");
    }

    if(alignment.alignArray && array_item_size == 0) {
//		printf("Vtable specification desires array alignment but type is not an array.  Reverting to 4-byte base alignment.\n");
        alignment.alignArray = false;
        alignment.powerOfTwoBaseFour = 0;
    }

    new_vt->indirection_offset = 0;
    new_clss->vt = new_vt;
    new_vt->allocated_size   = size_of_fixed;
    new_clss->allocated_size = size_of_fixed;
    new_vt->array_length_offset = array_length_offset;
    new_clss->alignment = alignment;
    new_clss->finalizer = finalizer;

    if(is_pinned) {
        new_clss->is_pinned = true;
    } else {
        new_clss->is_pinned = false;
    }

    unsigned int max_refs = (size_of_fixed / (sizeof(void*)));
    new_clss->is_ref = (PgcIsRef *)malloc(sizeof(PgcIsRef) * max_refs);
    memcpy(new_clss->is_ref, is_ref, (sizeof(PgcIsRef) * max_refs));
    new_clss->is_weak_ref = (PgcIsRef *)malloc(sizeof(PgcIsRef) * max_refs);
    memcpy(new_clss->is_weak_ref, weak_refs, (sizeof(PgcIsRef) * max_refs));

    new_clss->fixed_part_has_ref = 0;
    unsigned int i;
    for (i = 0;  i < max_refs;  ++i) {
        if (new_clss->is_ref[i] || new_clss->is_weak_ref[i]) new_clss->fixed_part_has_ref++; // found a ref field so increment their count
        if (new_clss->is_ref[i] && new_clss->is_weak_ref[i]) {
            printf("pgc_add_weak_refs encountered both ref and weak ref in the same slot.\n");
            exit(-1);
        }
    }

    new_clss->array_item_size = array_item_size;
    switch(array_item_size) {
        case 0:
        case 1:
        case 2:
            new_clss->adjusted_item_size = array_item_size;
            break;
        // TODO: need to fix for 64-bit
        default:
            // if it is already a multiple of the register size then leave it alone
            if((array_item_size & 0xFFffFFfc) == array_item_size) {
                new_clss->adjusted_item_size = array_item_size;
            } else {
                new_clss->adjusted_item_size = (array_item_size + sizeof(POINTER_SIZE_INT)) & 0xFFffFFfc;
            }
    }

    new_vt->array_element_size = new_clss->adjusted_item_size;

    new_clss->array_item_field_ref = (PgcIsRef *)malloc(sizeof(PgcIsRef) * (new_clss->adjusted_item_size / sizeof(void*)));
    memcpy(new_clss->array_item_field_ref, array_item_field_ref, sizeof(PgcIsRef) * (new_clss->adjusted_item_size / sizeof(void*)));

    new_clss->array_item_has_ref = 0;
    for(i=0; i<new_clss->adjusted_item_size / sizeof(POINTER_SIZE_INT); ++i) {
        if(new_clss->array_item_field_ref[i]) new_clss->array_item_has_ref++; // found a ref entry so increment ref count
    }

	new_vt->mutability = mutability;

    if(new_clss->adjusted_item_size) {
        new_clss->array_element_ft_info.is_for_field = false;
        new_clss->array_element_ft_info.clss = new_clss;

        unsigned new_vtable_fixed_size = sizeof(void*) + new_clss->adjusted_item_size;
        unsigned new_vtable_max_refs   = new_vtable_fixed_size / sizeof(void*);

        PgcIsRef *norefs = (PgcIsRef *)malloc(sizeof(PgcIsRef) * new_vtable_max_refs);
        memset(norefs,0,sizeof(PgcIsRef) * new_vtable_max_refs);
        PgcIsRef *arefs = (PgcIsRef *)malloc(sizeof(PgcIsRef) * new_vtable_max_refs);

        arefs[0] = 0;
        memcpy(&arefs[1], new_clss->array_item_field_ref, new_vtable_max_refs - 1);
		struct AlignmentInfo array_type_alignment;
		array_type_alignment.alignArray = true;
		array_type_alignment.powerOfTwoBaseFour = 0;
        pgc_new_object_format(
                          /*new_vt*/              &(new_clss->array_vt),
                          /*size_of_fixed*/       new_vtable_fixed_size,   // in units of bytes
                          /*is_ref*/              arefs,
                          /*array_item_size*/     0,
                          /*array_length_offset*/ 0,
                          /*array_item_field_ref*/norefs,
                          /*array_item_alignment*/array_type_alignment,
												  mutability,
                          /*is_pinned*/           0,
                          /*weak_refs*/           norefs,
                                                  NULL);
        free(arefs);
        free(norefs);

        new_clss->array_vt.clss->is_array_elem_vt = true;
        new_clss->array_vt.clss->containing_clss_type = new_clss;
    }

    new_clss->field_ft_info = (FieldTypeInfo*)malloc(max_refs * sizeof(FieldTypeInfo));
    for(i=0;i<max_refs;++i) {
        new_clss->field_ft_info[i].is_for_field = true;
        new_clss->field_ft_info[i].clss = new_clss;
        new_clss->field_ft_info[i].field_index = i;
        new_clss->field_ft_info[i].field_offset = i*sizeof(POINTER_SIZE_INT);
    }

    gc_class_prepared(new_clss,new_vt);

#ifdef HAVE_PTHREAD_H
    pthread_mutex_lock(&pgc_vtable_lock);
#endif
    pgc_vtable_list.push_back(new_vt);
#ifdef HAVE_PTHREAD_H
    pthread_mutex_unlock(&pgc_vtable_lock);
#endif
} // pgc_new_object_format

extern "C" void pgc_new_object_format_2(
									  struct VTable *new_vt,
                                      unsigned size_of_fixed,   // in units of bytes
                                      PgcIsRef is_ref[],
                                      unsigned array_item_size, // in units of bytes
                                      unsigned array_length_offset,
                                      PgcIsRef array_item_field_ref[],
                                      struct AlignmentInfo alignment,
									  enum PGC_MUTABILITY mutability,
                                      unsigned is_pinned,
                                      PgcIsRef weak_refs[]) {
    pgc_new_object_format(new_vt,size_of_fixed,is_ref,array_item_size,array_length_offset,array_item_field_ref,alignment,mutability,
        is_pinned, weak_refs, NULL);
}

extern "C" void * class_get_super_class(Class_Handle ch) {
    return NULL;
}

extern "C" void * class_get_vtable(Class_Handle ch) {
    Clss *clss = (Clss*)ch;
    return (void*)clss->vt;
}

extern "C" unsigned int field_get_offset(Field_Handle fh) {
    FieldTypeInfo *fti = (FieldTypeInfo*)fh;
    assert(fti->is_for_field);
    return fti->field_offset;
}

extern "C" unsigned int field_is_reference(Field_Handle fh) {
    FieldTypeInfo *fti = (FieldTypeInfo*)fh;
    assert(fti->is_for_field);
    return fti->clss->is_ref[fti->field_index];
}

extern "C" unsigned int field_is_weak_reference(Field_Handle fh) {
    FieldTypeInfo *fti = (FieldTypeInfo*)fh;
    assert(fti->is_for_field);
    return fti->clss->is_weak_ref[fti->field_index];
}

extern "C" unsigned int class_num_instance_fields_recursive(Class_Handle ch) {
    Clss *clss = (Clss*)ch;
    // the number of fields is the size of the object minus the vtable pointer
    return (clss->allocated_size - (1 * sizeof(void *))) / sizeof(void*);
}

extern "C" unsigned int class_num_instance_fields(Class_Handle ch) {
    return class_num_instance_fields_recursive(ch);
}

extern "C" Field_Handle class_get_instance_field_recursive(Class_Handle ch, unsigned int index) {
    Clss *clss = (Clss*)ch;
    return (Field_Handle)&(clss->field_ft_info[index+/*2*/1]);
}

extern "C" Field_Handle class_get_instance_field(Class_Handle ch, unsigned int index) {
    return class_get_instance_field_recursive(ch,index);
}

extern "C" unsigned int class_is_array(Class_Handle ch) {
    return ((Clss*)ch)->array_item_size > 0;
}

extern "C" void orp_exit(int exit_code) {
    printf("PGC code forcing exit(%d)\n", exit_code);
    fflush(stdout);
    exit(exit_code);
}

extern "C" unsigned int orp_vtable_pointers_are_compressed(void) {
    return 0; // false == pointers are not compressed
}

extern "C" const char * orp_get_property_value(const char *property_name) {
    if(strcmp(property_name,"delta") == 0) return "off";                         // don't use MS-Delta
    else if (strcmp(property_name,"heap_compaction_ratio") == 0) return "";      // use default heap compaction ratio
    else if (strcmp(property_name,"characterize_heap") == 0) return use_char?"on":"off"; // characterize the heap if "gc characterize" option used
    else if (strcmp(property_name,"delta_cutoff_percent") == 0) return "";       // doesn't matter since not using MS-Delta
    else if (strcmp(property_name,"delta_write") == 0) return "";                // doesn't matter since not using MS-Delta
    else if (strcmp(property_name,"delta_read") == 0) return "";                 // doesn't matter since not using MS-Delta
    else {
        assert(0);
        printf("GC inquired about undefined property %s\n", property_name);
        exit(-1);
    }
}

extern "C" unsigned int orp_get_boolean_property_value_with_default(const char *property_name) {
    if(strcmp(property_name,"intel.orp.pls_bump_pointer") == 0) return 0;
    else {
        assert(0);
        printf("GC inquired about undefined property %s\n", property_name);
        exit(-1);
    }
}

extern "C" unsigned int orp_references_are_compressed(void) {
    return 0; // false == references are not compressed
}

// Only need to do something different here if you want to support compressed vtables.
extern "C" POINTER_SIZE_INT orp_get_vtable_base(void) {
    return 0;
}

extern "C" void orp_get_stm_properties(void *properties) {
    assert(0); // should never be called
}

// They are asking for a pointer to the GC's thread local storage but in Pillar we'll do them
// one better and return a pointer to processor local storage.
extern "C" void * orp_get_gc_thread_local(void) {
#if 1
    void *res = prtGetTls();
    if(res == NULL) {
        PrtTaskHandle task = prtGetTaskHandle();
        if (task != NULL) {
            TaskNotification(task, /*is_new_task*/ PrtTrue);
            res = prtGetTls();
        }
    }
    return res;
#else
    PrtTaskHandle pth = prtGetTaskHandle();
    std::map<PrtTaskHandle,void *>::iterator iter = g_gc_information_map.find(pth);
    // the main thread and the future threads are created before we can register
    // our callback so we catch those threads here.
    if(iter == g_gc_information_map.end()) {
        TaskNotification(pth,PrtTrue);
        return orp_get_gc_thread_local();
    } else {
        return iter->second;
    }
#endif
}

extern "C" unsigned int orp_number_of_gc_bytes_in_thread_local(void) {
    return GC_TLS_SIZE_SLOTS * sizeof(POINTER_SIZE_INT);
}

extern "C" unsigned int orp_number_of_gc_bytes_in_vtable(void) {
    return sizeof(POINTER_SIZE_INT);
}

extern "C" unsigned int class_is_finalizable(Class_Handle ch) {
    struct clss *cl = (struct clss*)ch;
    return cl->finalizer != NULL;
}

extern "C" SynchCriticalSectionHandle orp_synch_create_critical_section(void) {
#ifdef HAVE_PTHREAD_H
    pthread_mutex_t *new_mux = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(new_mux,NULL);
    return (SynchCriticalSectionHandle)new_mux;
#else
    return (SynchCriticalSectionHandle)mcrtMonitorNew();
#endif
}

extern "C" unsigned int orp_synch_enter_critical_section(SynchCriticalSectionHandle cs) {
#ifdef HAVE_PTHREAD_H
    // FIX FIX FIX...is this right or do we need to yield somehow?
    int res = pthread_mutex_lock((pthread_mutex_t*)cs);
    assert(res == 0);
#else
    McrtSyncResult res;
    while((res = mcrtMonitorTryEnter((McrtMonitor*)cs)) == Unavailable) {
        prtYieldUnmanaged();
        mcrtThreadYield();
    }
    assert(res == Success);
//    mcrtMonitorEnter((McrtMonitor*)cs);
#endif
    return 1;
}

extern "C" void orp_synch_leave_critical_section(SynchCriticalSectionHandle cs) {
#ifdef HAVE_PTHREAD_H
    // FIX FIX FIX...is this right or do we need to yield somehow?
    int res = pthread_mutex_unlock((pthread_mutex_t*)cs);
    assert(res == 0);
#else
    mcrtMonitorExit((McrtMonitor*)cs);
#endif
}

extern "C" void orp_synch_delete_critical_section(SynchCriticalSectionHandle cs) {
#ifdef HAVE_PTHREAD_H
    pthread_mutex_unlock((pthread_mutex_t*)cs);
    free(cs);
#else
    mcrtMonitorDelete((McrtMonitor*)cs);
#endif
}

extern "C" Class_Handle allocation_handle_get_class(Allocation_Handle ah) {
    return (Class_Handle)(((VTable*)ah)->clss);
}

extern "C" unsigned int class_is_pinned(Class_Handle ch) {
    Clss *clss = (Clss*)ch;
    if(clss->is_pinned) return 1;
    else                return 0;
}

extern "C" unsigned int class_get_alignment(Class_Handle ch) {
    return 0;
}

struct tse_node {
    struct PrtTaskSetEnumerator *ptse;
    struct tse_node *next;
};

struct tse_node *g_tse_list = NULL;

void add_tse_node(struct PrtTaskSetEnumerator *ptse) {
    struct tse_node *new_node = (struct tse_node *)malloc(sizeof(struct tse_node));
    new_node->ptse = ptse;
    new_node->next = g_tse_list;
    g_tse_list     = new_node;
}

typedef struct threadNodeS {
    char gc_space[40];
    struct threadNodeS *next;
    PrtTaskHandle pth;
} threadNode;

threadNode *g_thread_list = NULL;

unsigned gc_lock_count = 0;

bool is_in_thread_list(const PrtTaskHandle &pth) {
    threadNode *cur_thread = g_thread_list;

    while(cur_thread) {
        if(cur_thread->pth == pth) return true;
        cur_thread = cur_thread->next;
    }
    return false;
}

unsigned num_gc_locks = 0;

extern "C" void orp_gc_lock_enum(void) {
    orp_synch_enter_critical_section(g_gc_lock);

    ++gc_lock_count;
    ++num_gc_locks;
    if(num_gc_locks % 20 == 0) {
//        printf("%d orp_gc_lock_enum calls.\n",num_gc_locks);
    }

    assert(gc_lock_count < 2);

//    printf("orp_gc_lock_enum %d\n",gc_lock_count);
}

extern "C" void orp_gc_unlock_enum(void) {
//    printf("orp_gc_unlock_enum %d\n",gc_lock_count);

    --gc_lock_count;

    orp_synch_leave_critical_section(g_gc_lock);
}

#ifndef __GNUC__
#ifdef _WIN32
#if defined(_MSC_VER) || defined(_MSC_EXTENSIONS)
  #define DELTA_EPOCH_IN_MICROSECS  11644473600000000Ui64
#else
  #define DELTA_EPOCH_IN_MICROSECS  11644473600000000ULL
#endif

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
    FILETIME ft;
    unsigned __int64 tmpres = 0;
    static int tzflag;

    if (NULL != tv) {
        GetSystemTimeAsFileTime(&ft);

        tmpres |= ft.dwHighDateTime;
        tmpres <<= 32;
        tmpres |= ft.dwLowDateTime;

        /*converting file time to unix epoch*/
        tmpres -= DELTA_EPOCH_IN_MICROSECS;
        tmpres /= 10;  /*convert into microseconds*/
        tv->tv_sec = (long)(tmpres / 1000000UL);
        tv->tv_usec = (long)(tmpres % 1000000UL);
    }

    if(tz != NULL) {
        assert(0);
      }

    return 0;
}
#endif
#endif

extern "C" void orp_thread_sleep(unsigned int msec) {
#ifdef HAVE_PTHREAD_H
    struct timeval cur_time;
    struct timeval timeout;

    gettimeofday(&timeout,NULL);
    timeout.tv_usec += (msec * 1000);
    if(timeout.tv_usec > 1000000) {
        timeout.tv_sec++;
        timeout.tv_usec -= 1000000;
    }

    while(1) {
        sched_yield();
        gettimeofday(&cur_time,NULL);
        if(cur_time.tv_sec  > timeout.tv_sec)  return;
        if(cur_time.tv_sec  < timeout.tv_sec)  continue;
        if(cur_time.tv_usec > timeout.tv_usec) return;
    }
#else
    mcrtThreadSleep(mcrtMsecs32toTime(msec));
#endif
}

extern "C" SynchEventHandle orp_synch_create_event(unsigned int man_reset_flag) {
    threadEvent *retval = NULL;
    retval = (threadEvent *)malloc (sizeof(threadEvent));
#ifdef HAVE_PTHREAD_H
    retval->auto_reset_flag = !man_reset_flag;
    retval->state = 0;
    retval->event_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    retval->event_condition = (pthread_cond_t*)malloc(sizeof(pthread_cond_t));
    pthread_mutex_init(retval->event_mutex,NULL);
    pthread_cond_init(retval->event_condition,NULL);
#else // HAVE_PTHREAD_H
    retval->auto_reset_flag = !man_reset_flag;
#ifdef USE_MCRT_CLH_LOCK
    retval->mcrt_clhlock = mcrtClhLockNew();
    {
        McrtBool res = mcrtClhLockTryAcquire(retval->mcrt_clhlock);
    }
#else // USE_MCRT_CLH_LOCK
    retval->mcrt_event = mcrtEventNew();
#endif // USE_MCRT_CLH_LOCK
#endif // pthreads

    return (SynchEventHandle)retval;
}

/* The event is not signaled. */
extern "C" unsigned int orp_synch_reset_event(SynchEventHandle handle) {
    threadEvent *the_event;
    the_event = (threadEvent *)handle;

#ifdef HAVE_PTHREAD_H
    pthread_mutex_lock(the_event->event_mutex);
    the_event->state = 0;
    pthread_cond_broadcast(the_event->event_condition);
    pthread_mutex_unlock(the_event->event_mutex);
#else // HAVE_PTHREAD_H
#ifdef USE_MCRT_CLH_LOCK
    mcrtClhLockTryAcquire(the_event->mcrt_clhlock);
#else // USE_MCRT_CLH_LOCK
    mcrtEventReset(the_event->mcrt_event);
#endif // USE_MCRT_CLH_LOCK
#endif // pthreads

    return 1;
}

/* Signal the event */
extern "C" unsigned int orp_synch_set_event(SynchEventHandle handle) {
    threadEvent *the_event = (threadEvent *)handle;
#ifdef HAVE_PTHREAD_H
    pthread_mutex_lock(the_event->event_mutex);
    the_event->state = 1;
    pthread_cond_broadcast(the_event->event_condition);
    pthread_mutex_unlock(the_event->event_mutex);
#else // HAVE_PTHREAD_H
#ifdef USE_MCRT_CLH_LOCK
    mcrtClhLockTryRelease(the_event->mcrt_clhlock);
#else  // USE_MCRT_CLH_LOCK
    mcrtEventNotify(the_event->mcrt_event);
#endif // USE_MCRT_CLH_LOCK
#endif // pthreads

    return 1;
}

extern "C" unsigned int orp_synch_wait_for_event(SynchEventHandle hHandle, unsigned int dwMillisec) {
    threadEvent *the_threadevent = (threadEvent *)hHandle;
#ifdef HAVE_PTHREAD_H
    struct timeval cur_time;
    struct timespec timeout;

    if(dwMillisec != INFINITE_TIME) {
        gettimeofday(&cur_time,NULL);
        timeout.tv_sec = cur_time.tv_sec;
        timeout.tv_nsec = (cur_time.tv_usec * 1000) + (dwMillisec * 1000000);
        if(timeout.tv_nsec > 1000000000) {
            timeout.tv_sec++;
            timeout.tv_nsec -= 1000000000;
        }
    }

    pthread_mutex_lock(the_threadevent->event_mutex);

    while(!the_threadevent->state) {
        if(dwMillisec == INFINITE_TIME) {
            pthread_cond_wait(the_threadevent->event_condition, the_threadevent->event_mutex);
        } else {
            int res = pthread_cond_timedwait(the_threadevent->event_condition, the_threadevent->event_mutex, &timeout);
#ifdef __GNUC__
            if(res) {
#else // __GNUC__
            if(res == ETIMEDOUT) {
#endif // __GNUC__
                return EVENT_WAIT_TIMEOUT;
            }
        }
    }

    if(the_threadevent->auto_reset_flag) {
        the_threadevent->state = 0;
    }

    pthread_mutex_unlock(the_threadevent->event_mutex);

    return EVENT_WAIT_OBJECT_0;
#else  // HAVE_PTHREAD_H
    McrtSyncResult result;
#ifdef USE_MCRT_CLH_LOCK
    result = mcrtClhLockAcquire (the_threadevent->mcrt_clhlock,
                                 ((dwMillisec == MCRT_INFINITE)
                                  ? InfiniteWaitCycles64
                                  : mcrtConvertMsecsToCycles(mcrtMsecs64toTime((uint64)dwMillisec))));
    if (result == Success) {
        if (!the_threadevent->auto_reset_flag) {
            result = mcrtClhLockTryRelease(the_threadevent->mcrt_clhlock);
            assert(result == Success);
        }
    }
#else
    if(dwMillisec == INFINITE_TIME)
        result = mcrtEventWait (the_threadevent->mcrt_event,
                                the_threadevent->auto_reset_flag);
    else
        result = mcrtEventTimedWait (the_threadevent->mcrt_event,
                                     the_threadevent->auto_reset_flag,
                				     mcrtConvertMsecsToCycles(dwMillisec));
#endif // USE_MCRT_CLH_LOCK
    if (result == Success) {
        return EVENT_WAIT_OBJECT_0;
    }
    if (result == Timeout) {
        return EVENT_WAIT_TIMEOUT;
    }
    if (result == Interrupt) {
        return EVENT_WAIT_OBJECT_0;
    }
    if (result == IllegalState) {
        printf ("thread %d - threadwait_for_single_object call to mcrtEventTimedWait returns IllegalState.\n", mcrtThreadGetId() );
        assert (0);
    }
    printf ("thread %d - threadwait_for_single_object call to mcrtEventTimedWait returns unknown result.\n", mcrtThreadGetId() );
    assert (0);
    return EVENT_WAIT_OBJECT_0;
#endif
} //synch_wait_for_event



extern "C" unsigned int orp_synch_wait_for_multiple_events(unsigned int numobj, SynchEventHandle *hHandle, unsigned int dwMillisec) {
  unsigned int ret;
  unsigned int i;

#ifdef HAVE_PTHREAD_H
  if (dwMillisec != INFINITE_TIME) {
      assert(0);
  }
#else
  if (dwMillisec != INFINITE_TIME) {
      assert(0);
  }
#endif

  for (i = 0; i < numobj; i++) {
      ret = orp_synch_wait_for_event (hHandle[i], dwMillisec);
      if (ret != EVENT_WAIT_OBJECT_0)
          return ret;
  }
  return ret; // should always return WAIT_OBJECT_0
}

extern "C" void orp_set_affinity(void) {
    // Intentionally do nothing.
}

struct funarg_wrapS {
    unsigned int STDCALL_FUNC_OUT (STDCALL_FUNC_IN *start_address)(void *);
    void *arglist;
};

typedef struct funarg_wrapS funarg_wrap;

static void
#ifdef HAVE_PTHREAD_H
*
#endif
cdeclThreadWrapper(void *funarg_wrap_args)
{
    int start_result = 0;
    funarg_wrap *wrap_args = (funarg_wrap *)funarg_wrap_args;
    start_result = wrap_args->start_address(wrap_args->arglist);
    free(funarg_wrap_args);
#ifdef HAVE_PTHREAD_H
    return NULL;
#endif
}

extern "C" ThreadThreadHandle orp_thread_create(unsigned int STDCALL_FUNC_OUT (STDCALL_FUNC_IN *start_address)(void *), void *arglist, unsigned *thrdaddr )
{
    ThreadThreadHandle thread;
#ifdef HAVE_PTHREAD_H
    pthread_t *new_thread;
#endif
    funarg_wrap *wrap_args;

    wrap_args = (funarg_wrap *)malloc (sizeof(funarg_wrap));
    wrap_args->arglist = arglist;
    wrap_args->start_address = start_address;

#ifdef HAVE_PTHREAD_H
    // FIX FIX FIX ....this allocaiton will leak for the moment.
    new_thread = (pthread_t*)malloc(sizeof(pthread_t));
    pthread_create(new_thread, NULL, cdeclThreadWrapper, wrap_args);
    thread = (ThreadThreadHandle)new_thread;
    if(thrdaddr) {
        *thrdaddr =
#ifdef WIN32
            (unsigned)pthread_self().p;
#else // WIN32
            (unsigned)pthread_self();
#endif // WIN32
    }
#else // HAVE_PTHREAD_H
    /* mcrtThreadCreate returns a mcrtThread*. */
    thread = (ThreadThreadHandle)mcrtThreadCreate(cdeclThreadWrapper, wrap_args);
    if (thread != NULL) {
        /* All threads created for ORP are detached. */
        mcrtThreadDetach((McrtThread *)thread);
	    if(thrdaddr) {
		    *thrdaddr = *(unsigned *)thread;
	    }
    }
#endif // HAVE_PTHREAD_H
    return thread; /* return 0 on error to match beginthreadex. */
}

enum safepoint_state {
    nill = 0,
    enumerate_the_universe, // for sapphire just means sapphire is running.
    java_suspend_one_thread,
    java_debugger,
};

volatile safepoint_state global_safepoint_status = nill;

extern "C" enum safepoint_state get_global_safepoint_status() {
    return global_safepoint_status;
} //get_global_safepoint_status

extern "C" void orp_post_gc_mcrt_cleanup(void) {
    // intentially nothing here
}

extern "C" void orp_gc_cycle_end_notification(void) {
    // TODO: verify that this is accurate
    // Don't think we need to do anything here.
}

void *orp_heap_get_pointer(ManagedObject *object, uint32_t offset)
{
#if defined POINTER64 || defined __x86_64__
    return (void *)heapGetG8(object, offset);
#else
    return (void *)heapGetG4(object, offset);
#endif
}

typedef struct ManagedObjectUncompressedVtablePtr {
    VTable *vt_raw;
    POINTER_SIZE_INT obj_info;

    VTable *vt_unsafe() {
        return (VTable *)orp_heap_get_pointer((ManagedObject *)this, 0);
    }
    VTable *vt() { return vt_unsafe(); }

    //  VTable * is used for the size of the vt_raw. using orp_heap_get_uint32_t instead of heapGetG4 due to
    // the fact that these types are used by synch.dll which is not part of orp.
    POINTER_SIZE_INT get_obj_info() {
        if(sizeof(POINTER_SIZE_INT) == 4)
            return (uint32_t)orp_heap_get_uint32_t((ManagedObject *)this, sizeof(VTable *));
        if(sizeof(POINTER_SIZE_INT) == 8)
            return (uint32_t)orp_heap_get_uint64_t((ManagedObject *)this, sizeof(VTable *));
    }

    void set_obj_info(POINTER_SIZE_INT value) {
        if(sizeof(POINTER_SIZE_INT) == 4)
            orp_heap_set_uint32_t((ManagedObject *)this, sizeof(VTable *), (uint32_t) value);
        if(sizeof(POINTER_SIZE_INT) == 8)
            orp_heap_set_uint64_t((ManagedObject *)this, sizeof(VTable *), (uint64_t) value);
    }

    static VTable *allocation_handle_to_vtable(Allocation_Handle ah) {
        return (VTable *) ah;

    }
    static unsigned header_offset() { return sizeof(VTable *); }
    static size_t get_size() { return sizeof(ManagedObjectUncompressedVtablePtr); }
    static bool are_vtable_pointers_compressed() { return false; }
} ManagedObjectUncompressedVtablePtr;

typedef struct ManagedObject {
    VTable *vt_unsafe() {
        return ((ManagedObjectUncompressedVtablePtr *)this)->vt_unsafe();
    }
    VTable *vt() {
        return ((ManagedObjectUncompressedVtablePtr *)this)->vt();
    }
    POINTER_SIZE_INT get_obj_info() {
        return ((ManagedObjectUncompressedVtablePtr *)this)->get_obj_info();
    }
    // if
    POINTER_SIZE_INT *get_obj_info_addr() {
        return (POINTER_SIZE_INT *)( ((char *)(this)) + header_offset() );
    }
    POINTER_SIZE_INT get_obj_info_offset() { return (uint32_t)header_offset(); }
    void set_obj_info(POINTER_SIZE_INT value) {
        ((ManagedObjectUncompressedVtablePtr *)(this))->set_obj_info(value);
    }
    static VTable *allocation_handle_to_vtable(Allocation_Handle ah) {
        return ManagedObjectUncompressedVtablePtr::allocation_handle_to_vtable(ah);
    }
    static unsigned header_offset() {
        return ManagedObjectUncompressedVtablePtr::header_offset();
    }

    bool is_forwarded() {
        return false;
    }

    struct ManagedObject *get_forwarding_pointer() {
        assert(0); // This should never be called unless we are in the McRT based SUPPORT_TRANSACTIONS version of the world.
        return NULL;
    }

    static size_t get_size() {
        return ManagedObjectUncompressedVtablePtr::get_size();
    }

    static bool are_vtable_pointers_compressed() {
        return false;
    }
} ManagedObject;

int VTable::vector_get_length(void *o) {
    assert(sizeof(int) == 4);
    assert(array_element_size != 0);
    return heapGetG4((ManagedObject*)o,array_length_offset);
}

extern "C" int32_t vector_get_length(Vector_Handle vector) {
    Object *o = (Object*)vector;
    int32_t r = o->get_vtable()->vector_get_length(o);
	return r;
} //get_vector_length

extern "C" int32_t vector_get_length_with_vt(Vector_Handle vector, VTable *vt) {
    Object *o = (Object*)vector;
    int32_t r = vt->vector_get_length(o);
	return r;
} //get_vector_length

extern "C" void orp_finalize_object(Managed_Object_Handle pobj) {
    Object *fo = (Object*)pobj;
    struct clss *fo_clss = fo->get_vtable()->clss;
    assert(fo_clss->finalizer);
    fo_clss->finalizer(pobj);
}

extern "C" unsigned int verify_object_header(void *ptr) {
    return 0;
}

extern "C" const char * class_get_name(Class_Handle cl) {
    return ((Clss *)cl)->name;
}

// field names only need for informational printing with MS-Delta and for fusing so we can return NULL here.
extern "C" const char * field_get_name(Field_Handle f) {
    return NULL;
}

// This method only used during fusing and heap characterization which we don't plan to do so safe to return NULL.
extern "C" Class_Handle field_get_class_of_field_value(Field_Handle fh) {
    return (Class_Handle)((FieldTypeInfo*)fh)->clss;
}

extern "C" unsigned int class_get_boxed_data_size(Class_Handle ch) {
    Clss *clss = (Clss*)ch;
    unsigned res = clss->allocated_size;
    if((res & 0xFFffFFfc) != res) {
        res = (res + sizeof(POINTER_SIZE_INT)) & 0xFFffFFfc;
    }
    return res;
}

extern "C" int pgc_get_array_offset(struct VTable *vt) {
	if(vt->clss->array_item_size) {
		return vt->allocated_size;
	} else {
		return 0;
	}
} // pgc_get_array_offset

extern "C" unsigned int class_is_non_ref_array(Class_Handle ch) {
    Clss *clss = (Clss*)ch;
    if(clss->array_item_size == 0) return 0; // class is not an array so return false

    return !clss->array_item_has_ref;
}

extern "C" unsigned int class_element_size(Class_Handle ch) {
    Clss *clss = (Clss*)ch;
    return clss->adjusted_item_size;
}

// The only place this is used in the GC is to determine the offset of the first element of an array so return that value here.
extern "C" struct VTable * class_get_array_element_vtable(Class_Handle ch) {
    Clss *clss = (Clss*)ch;
    // make sure this is actually an array
    assert(clss->array_item_size != 0);
    return (struct VTable *)&(clss->array_vt);
}

// The only place this is used in the GC is to determine the offset of the first element of an array so return that value here.
extern "C" Class_Handle class_get_array_element_class(Class_Handle ch) {
    Clss *clss = (Clss*)ch;
    // make sure this is actually an array
    assert(clss->array_item_size != 0);
    return (Class_Handle)clss->array_vt.clss;
}

// element type is constructed from class_get_array_element_class above to be the offset of the start of the array
extern "C" int vector_first_element_offset_unboxed(Class_Handle element_type) {
    Clss *clss = (Clss*)element_type;
    assert(clss->is_array_elem_vt);
    int ret = clss->containing_clss_type->allocated_size;
    return ret;
}

extern "C" struct AlignmentInfo pgc_get_vtable_alignment(struct VTable *vt) {
	return vt->clss->alignment;
} // pgc_get_vtable_alignment

extern "C" void *vector_get_element_address_ref(Vector_Handle vector, int32_t idx) {
    char *ret = (char*)vector;

    Object *o = (Object*)vector;
    VTable *vt = o->get_vtable();
    Clss *vector_class = vt->clss;
    // go to the first element
    ret += vector_first_element_offset_unboxed(class_get_array_element_class(vector_class));
    // go to the element
    ret += idx * vector_class->adjusted_item_size;
    return (void*)ret;
}

extern "C" void *vector_get_element_address_ref_with_vt(Vector_Handle vector, int32_t idx, VTable *vt) {
    char *ret = (char*)vector;

    Object *o = (Object*)vector;
    Clss *vector_class = vt->clss;
    // go to the first element
    ret += vector_first_element_offset_unboxed(class_get_array_element_class(vector_class));
    // go to the element
    ret += idx * vector_class->adjusted_item_size;
    return (void*)ret;
}

extern "C" unsigned int orp_vector_size(Class_Handle vector_class, int length) {
    Clss *vc = (Clss*)vector_class;
    unsigned int res = vector_first_element_offset_unboxed(class_get_array_element_class(vector_class)) +
        (length * vc->adjusted_item_size);
    // round up to nearest aligned word
    if((res & 0xFFffFFfc) != res) {
        res = (res + sizeof(POINTER_SIZE_INT)) & 0xFFffFFfc;
    }
    return res;
}

// This function is only used by characterize heap so we don't really need it.  Just a dummy implementation.
extern "C" PGC_Data_Type class_get_primitive_type_of_class(Class_Handle ch) {
    Clss *clss = (Clss*)ch;
    if(clss->is_array_elem_vt) {
        return (PGC_Data_Type)PGC_DATA_TYPE_STRUCT;
    } else {
        assert(0);
        return (PGC_Data_Type)'?';
    }
}

// we only use primitive and unboxed
extern "C" unsigned int type_info_is_reference(Type_Info_Handle tih) {
    FieldTypeInfo *fti = (FieldTypeInfo*)tih;
    if(fti->is_for_field) return field_is_reference((Field_Handle)fti);
    else                  return false; // array element is never ref
}

// This function is only used by characterize heap so we don't really need it.  Just a dummy implementation.
extern "C" PGC_Data_Type type_info_get_type(Type_Info_Handle ch) {
    if(type_info_is_reference(ch)) {
        return PGC_DATA_TYPE_INT32;
        assert(0);
        return (PGC_Data_Type)'?';
    } else {
        return PGC_DATA_TYPE_INT32;
    }
}

// This function is only used by characterize heap so we don't really need it.  Just a dummy implementation.
extern "C" int vector_first_element_offset_class_handle(Class_Handle ch) {
    assert(0);
    return 0;
}

extern "C" Type_Info_Handle field_get_type_info_of_field_value(Field_Handle fh) {
    return (Type_Info_Handle)fh;
}

extern "C" Type_Info_Handle class_get_element_type_info(Class_Handle ch) {
    Clss *clss = (Clss*)ch;
    return (Type_Info_Handle)&clss->array_element_ft_info;
}

extern "C" Class_Handle type_info_get_class(Type_Info_Handle tih) {
    FieldTypeInfo *fti = (FieldTypeInfo*)tih;
    if(fti->is_for_field) return (Class_Handle)fti->clss;
    else                  return (Class_Handle)fti->clss->array_vt.clss;
}

// we only use primitive and unboxed
extern "C" unsigned int type_info_is_vector(Type_Info_Handle tih) {
    return false;
}
// we only use primitive and unboxed
extern "C" unsigned int type_info_is_general_array(Type_Info_Handle tih) {
    return false;
}
extern "C" unsigned int type_info_is_unmanaged_pointer(Type_Info_Handle tih) {
    return false;
}

// we only use primitive and unboxed
// primitive means no references
// unboxed contains references
extern "C" unsigned int type_info_is_primitive(Type_Info_Handle tih) {
    FieldTypeInfo *fti = (FieldTypeInfo*)tih;
    if(fti->is_for_field) return !field_is_reference((Field_Handle)fti);
    else                  return !fti->clss->array_item_has_ref;
}

// we only use primitive and unboxed
extern "C" unsigned int type_info_is_unboxed(Type_Info_Handle tih) {
    FieldTypeInfo *fti = (FieldTypeInfo*)tih;
    if(fti->is_for_field) return false;
    else                  return fti->clss->array_item_has_ref;
}

extern "C" unsigned int class_get_unboxed_data_offset(Class_Handle ch) {
    return 4;
}

extern "C" unsigned int orp_suspend_thread_for_enumeration(PrtTaskHandle prt_task) {
#ifdef TRACE_THREAD_SUSPENSION
    printf("orp_suspend_thread_for_enumeration suspending task\n");  fflush(stdout);
#endif // TRACE_THREAD_SUSPENSION

    PrtBool success = prtSuspendTask(prt_task);

#ifdef TRACE_THREAD_SUSPENSION
    printf("orp_suspend_thread_for_enumeration done suspending task\n");  fflush(stdout);
#endif // TRACE_THREAD_SUSPENSION
    return success ? 1 : 0;
}

extern "C" void orp_resume_thread_after_enumeration(PrtTaskHandle prt_task) {
#ifdef TRACE_THREAD_SUSPENSION
    printf("orp_resume_thread_after_enumeration resuming task\n");  fflush(stdout);
#endif // TRACE_THREAD_SUSPENSION

    prtResumeTask(prt_task);

#ifdef TRACE_THREAD_SUSPENSION
    printf("orp_resume_thread_after_enumeration done resuming task\n");  fflush(stdout);
#endif // TRACE_THREAD_SUSPENSION
}

extern "C" void orp_resume_threads_after(void) {
#ifdef TRACE_THREAD_SUSPENSION
    printf("orp_resume_threads_after: start\n");  fflush(stdout);
#endif // TRACE_THREAD_SUSPENSION

    if(!g_update_threads_on_lock) {
		PrtTaskHandle our_pth = prtGetTaskHandle();
        while(g_tse_list) {
            struct tse_node *old_node = g_tse_list;
            g_tse_list = g_tse_list->next;

            PrtTaskHandle pth = prtStartIterator(old_node->ptse);
            while(pth) {
				// Don't try to resume ourselves.
//				if(pth != our_pth) {
#ifdef TRACE_THREAD_SUSPENSION
                    printf("orp_resume_threads_after resuming task\n");  fflush(stdout);
#endif // TRACE_THREAD_SUSPENSION
					prtResumeTask(pth);
//				}
                pth = prtNextIterator(old_node->ptse);
            }

            prtReleaseTaskSet(old_node->ptse);

            free(old_node);
        }

        while(g_thread_list) {
            threadNode *to_delete = g_thread_list;
            g_thread_list = g_thread_list->next;
            free(to_delete);
        }
    }

#ifdef TRACE_THREAD_SUSPENSION
    printf("orp_resume_threads_after: done\n");  fflush(stdout);
#endif // TRACE_THREAD_SUSPENSION
}

#if 0
extern "C" void pgc_root_callback(void *env, void **rootAddr, PrtBool isMP) {
    if(isMP) {
        gc_add_root_set_entry_managed_pointer((void **)rootAddr, FALSE);
    } else {
        gc_add_root_set_entry((void **)rootAddr, FALSE);
    }
}
#else
extern "C" void pgc_root_callback(void *env, void **rootAddr, PrtGcTag tag, void *parameter) {
    int offset;
    assert(rootAddr);

    switch(tag) {
    case PrtGcTagDefault:
        gc_add_root_set_entry((void **)rootAddr, FALSE);
        break;
    case PrtGcTagOffset:
        gc_add_root_set_entry_interior_pointer(rootAddr, (intptr_t)parameter, FALSE);
        break;
    case PrtGcTagBase:
        offset = (int)(POINTER_SIZE_INT)(*((Byte**)rootAddr) - (Byte*)parameter);
        gc_add_root_set_entry_interior_pointer(rootAddr, offset, FALSE);
        break;
    default:
        if(tag == ((PrtGcTag)(PrtGcTagOffset | (1 << 31)))) {
//             gc_add_weak_root_set_entry_interior_pointer(rootAddr, (int)parameter);
            gc_add_root_set_entry_interior_pointer(rootAddr, (intptr_t)parameter, FALSE);
        } else {
            assert(0);
        }
        break;
    }
}
#endif

extern "C" void orp_enumerate_thread(PrtTaskHandle prt_task) {
    PrtRseInfo rse;
    rse.env = NULL;
    rse.callback = &pgc_root_callback;
    prtEnumerateTaskRootSet(prt_task,&rse);
}

void gc_add_root_set_entry_nonheap(Managed_Object_Handle *mref);

extern "C" void orp_enumerate_global_refs(void) {
    PrtRseInfo pri;
    pri.callback = &pgc_root_callback;
    pri.env = NULL;
    prtEnumerateGlobalRootSet(&pri);
	// tell the P runtime to enumerate its global refs via the callback function that we pass in.
    // JMS: Don't call this; instead, scan and enumerate the array of global roots ourselves.
	pgc_g_runtime_callback(&pgc_root_callback,NULL);
    for (unsigned i=0; i<numGlobalRoots; i++) {
        gc_add_root_set_entry_nonheap(&globalRoots[i]);
    }
    for (unsigned i=0; i<numGlobalRefs; i++) {
        gc_add_root_set_entry((Managed_Object_Handle*)globalRefs[i],FALSE);
    }
}

extern "C" void orp_enumerate_root_set_all_threads(void) {
#if 1
#ifdef TRACE_THREAD_SUSPENSION
    printf("orp_enumerate_root_set_all_threads: start\n");  fflush(stdout);
#endif // TRACE_THREAD_SUSPENSION


    if(!g_update_threads_on_lock) {
        struct PrtTaskSetEnumerator *ptse = NULL;

        while(1) {
            ptse = prtGetTaskSet(ptse);
            add_tse_node(ptse);

            PrtTaskHandle pth = prtStartIterator(ptse);
            if(!pth) break;
            while(pth) {
#ifdef TRACE_THREAD_SUSPENSION
                printf("orp_enumerate_root_set_all_threads suspending task\n");  fflush(stdout);
#endif // TRACE_THREAD_SUSPENSION

                prtSuspendTask(pth);
                threadNode *new_tn = (threadNode*)malloc(sizeof(threadNode));
                new_tn->next = g_thread_list;
                new_tn->pth = pth;
                g_thread_list = new_tn;
                pth = prtNextIterator(ptse);
            }
        }
    }

    threadNode *temp = g_thread_list;
    while(temp) {
        orp_enumerate_thread(temp->pth);
        temp = temp->next;
    }
#else
    std::map<PrtTaskHandle,void*>::iterator iter;
    PrtTaskHandle pth = prtGetTaskHandle();
    for(iter  = g_gc_information_map.begin();
        iter != g_gc_information_map.end();
        ++iter) {
        if(pth == iter->first) {
            orp_enumerate_thread(iter->first);
        } else {
            prtSuspendTask(iter->first);
            orp_enumerate_thread(iter->first);
            prtResumeTask(iter->first);
        }
    }
#endif

    //printf("Enumerating globals\n");
    orp_enumerate_global_refs();
	//printf("Enumerating done\n");

#ifdef TRACE_THREAD_SUSPENSION
    printf("orp_enumerate_root_set_all_threads: done\n");  fflush(stdout);
#endif // TRACE_THREAD_SUSPENSION
}

struct update_data {
    void *data;
    int size_of_data;
};

void TaskNotification(PrtTaskHandle pth, PrtBool is_new_task) {
#if 1
    if(is_new_task) {
        void *gc_info = malloc(GC_TLS_SIZE_SLOTS * sizeof(void*));
        memset(gc_info, 0, GC_TLS_SIZE_SLOTS * sizeof(void*));
        prtSetTlsForTask(pth, (PrtProvidedTlsStruct *)gc_info);
        gc_thread_init(gc_info, pth);
    } else {
        void *tls = prtGetTlsForTask(pth);
        if(tls) {
            gc_thread_kill(tls);
            free(tls);
        }
    }
#else // 0
    if(is_new_task) {
        std::map<PrtTaskHandle,void *>::iterator iter = g_gc_information_map.find(pth);
        assert(iter == g_gc_information_map.end());
        void *gc_info = malloc(GC_TLS_SIZE_SLOTS * sizeof(void*));
		memset(gc_info, 0, GC_TLS_SIZE_SLOTS * sizeof(void*));
        g_gc_information_map.insert(std::pair<PrtTaskHandle,void*>(pth,gc_info));
        gc_thread_init(gc_info,pth);
    } else {
        std::map<PrtTaskHandle,void *>::iterator iter = g_gc_information_map.find(pth);
        assert(iter != g_gc_information_map.end());
        gc_thread_kill(iter->second);
        free(iter->second);
        g_gc_information_map.erase(pth);
    }
#endif //0
} // TaskNotification

extern "C" void pgc_kill(void) {
    gc_wrapup();

#ifdef HAVE_PTHREAD_H
    pthread_mutex_lock(&pgc_vtable_lock);
#endif
    while(!pgc_vtable_list.empty()) {
        struct VTable *vtable_iter = pgc_vtable_list.front();

        free(vtable_iter->clss->is_ref);
        free(vtable_iter->clss->array_item_field_ref);
        free(vtable_iter->clss->field_ft_info);
#ifdef __GNUC__
        free(vtable_iter->clss);
#else // __GNUC__
        _aligned_free(vtable_iter->clss);
#endif // __GNUC__

        pgc_vtable_list.pop_front();
    }
#ifdef HAVE_PTHREAD_H
    pthread_mutex_unlock(&pgc_vtable_lock);
#endif
} // pgc_kill

extern "C" void pgc_force_gc(void) {
    gc_force_gc();
}

extern "C" void pgc_init(PgcRuntimeCallback runtimeCallback, int update_threads_on_lock) {
#ifdef HAVE_PTHREAD_H
    pthread_mutex_init(&pgc_vtable_lock, NULL);
#endif

#if 0
    HINSTANCE handle = LoadLibrary(gc_name);
    if (handle == NULL) {
        printf("Unable to load gc.dll.\n");
        exit(0);
    }
#endif

#ifdef _DEBUGxxx
    if(update_threads_on_lock) {
        printf("Running pgc_init for GCv4\n");
    } else {
        printf("Running pgc_init for GCmf\n");
    }
#endif
	pgc_g_runtime_callback = runtimeCallback;
    g_update_threads_on_lock = update_threads_on_lock;

    g_gc_lock = orp_synch_create_critical_section();
    gc_init();

    prtRegisterTaskExistenceCallback((PrtTaskExistenceCallback)TaskNotification);

    struct PrtTaskSetEnumerator *ptse = NULL;

    ptse = prtGetTaskSet(ptse);
    PrtTaskHandle pth = prtStartIterator(ptse);
    while(pth) {
        if(!prtGetTlsForTask(pth)) {
            void *gc_info = malloc(GC_TLS_SIZE_SLOTS * sizeof(void*));
            memset(gc_info, 0, GC_TLS_SIZE_SLOTS * sizeof(void*));
            prtSetTlsForTask(pth,(PrtProvidedTlsStruct*)gc_info);
            gc_thread_init(gc_info,pth);
        }
        pth = prtNextIterator(ptse);
    }
    prtReleaseTaskSet(ptse);

//    gc_orp_initialized();

#if 0
#ifdef WIN32
    char filename[2000];
    if(GetModuleFileName(NULL,filename,2000) == 2000) {
        printf("Current executable name too long\n");
        exit(0);
    }
//    printf("Exe name = %s\n",filename);

    LOADED_IMAGE image;
    if(MapAndLoad(filename,NULL,(PLOADED_IMAGE)&image,FALSE,TRUE) == FALSE) {
        printf("MapAndUnload problem = %d",GetLastError());
        exit(0);
    }

//    printf("Base: %x\n",image.FileHeader->OptionalHeader.ImageBase);
//    printf("Number of sections = %d\n",image.NumberOfSections);
    for(unsigned i=0;i<image.NumberOfSections;++i) {
//        printf("Section %i: Name = %s, Addr = %x, Chars = %x, Length = %d\n",i,image.Sections[i].Name,image.Sections[i].VirtualAddress,image.Sections[i].Characteristics,
//            image.Sections[i].SizeOfRawData);
        if(strncmp((const char*)image.Sections[i].Name,".data",8)==0) {
            g_data_start = (char*)image.FileHeader->OptionalHeader.ImageBase + image.Sections[i].VirtualAddress;
            g_data_end   = g_data_start + image.Sections[i].SizeOfRawData;
        }
    }

    UnMapAndLoad(&image);
#endif // WIN32
#endif

    unsigned isrefs[1000];
    memset(&isrefs,0,sizeof(isrefs));

#if 0
    pgc_new_object_format(&reduced_space_vt,
                          12,   // in units of bytes
                          isrefs,
                          4, // in units of bytes
                          8,
                          isrefs,
                          PGC_ALIGN_4);
#endif
}

PrtBool PRT_CDECL pgcPredicateEqualUint32(volatile void *location, void *data)
{
//#if defined POINTER64 || defined __x86_64__
#if 0
    return (*(volatile uint64_t*)location) == ((uint64_t)data) ? PrtTrue : PrtFalse;
#else
    return (*(volatile uint32_t*)location) == ((uintptr_t)data) ? PrtTrue : PrtFalse;
#endif
}

// Allocation routines
extern "C" void * pgc_allocate_or_null(unsigned size, void *vtable_id) {
    // Don't try to do prtYieldUntil here like for pgc_allocate because this is
    // managed code and we can't do a GC here.

    if((size & 0xFFffFFfc) != size) {
        size = (size + sizeof(POINTER_SIZE_INT)) & 0xFFffFFfc;
    }
    return gc_malloc_or_null_with_thread_pointer(size,(uintptr_t)vtable_id,orp_get_gc_thread_local());
}

extern "C" void * pgc_allocate(unsigned size, void *vtable_id) {
    // While there is a GC in progress, call prtYield to give this
    // thread a chance to be stopped.  More importantly, we cannot
    // enter the gc_malloc_with_thread_pointer call because
    // if a thread on this processor was the one to trigger the GC
    // then this processor's TLS nursery entry will be NULL and may
    // really confuse the GC.
    while(gc_lock_count) {
#ifdef HAVE_PTHREAD_H
        prtYieldUntil(pgcPredicateEqualUint32, &gc_lock_count, 0, PrtInfiniteWait64);
#else
        prtYieldUntil((PrtPredicate)mcrtPredicateEqualUint32, &gc_lock_count, 0, InfiniteWaitCycles64);
#endif
    }

    if((size & 0xFFffFFfc) != size) {
        size = (size + sizeof(POINTER_SIZE_INT)) & 0xFFffFFfc;
    }
    return gc_malloc_with_thread_pointer(size,(uintptr_t)vtable_id,orp_get_gc_thread_local());
} // pgc_allocate

#if 0
extern "C" Managed_Object_Handle gc_malloc_with_thread_pointer_escaping(unsigned size, Allocation_Handle ah, void *tp);

extern "C" void * pgc_allocate_escaping(unsigned size, void *vtable_id) {
    // While there is a GC in progress, call prtYield to give this
    // thread a chance to be stopped.  More importantly, we cannot
    // enter the gc_malloc_with_thread_pointer call because
    // if a thread on this processor was the one to trigger the GC
    // then this processor's TLS nursery entry will be NULL and may
    // really confuse the GC.
    while(gc_lock_count) {
        prtYieldUntil((PrtPredicate)mcrtPredicateEqualUint32, &gc_lock_count, 0, InfiniteWaitCycles64);
    }

	//printf("pgc: alloc: %8x(%d)\n", vtable_id, size);
    if((size & 0xFFffFFfc) != size) {
        size = (size + sizeof(POINTER_SIZE_INT)) & 0xFFffFFfc;
    }
    return gc_malloc_with_thread_pointer_escaping(size,(Allocation_Handle)vtable_id,orp_get_gc_thread_local());
}
#endif // 0

extern "C" PgcBool pgc_can_allocate_without_collection_th(unsigned size, PrtTaskHandle task) {
    return gc_can_allocate_without_collection(size,prtGetTlsForTask(task));
}

extern "C" void pgc_require_allocate_without_collection_th(unsigned size, PrtTaskHandle task) {
    gc_require_allocate_without_collection(size,prtGetTlsForTask(task));
}

extern "C" PgcBool pgc_can_allocate_without_collection(unsigned size) {
    return gc_can_allocate_without_collection(size,orp_get_gc_thread_local());
}

extern "C" void pgc_require_allocate_without_collection(unsigned size) {
    gc_require_allocate_without_collection(size,orp_get_gc_thread_local());
}

static int get_object_size(struct VTable *vt,Object *object) {
	if(vt->array_element_size == 0) {
		return vt->allocated_size;
	} else {
		return orp_vector_size(vt->clss,vt->vector_get_length(object));
	}
}

extern "C" void pgc_next_command_line_argument(const char *name, const char *arg) {
    if(strcmp(name,"-gc")==0 && strcmp(arg,"characterize")==0) {
        use_char = true;
        return;
    }
    static bool first_time = true;
    if(first_time) {
        char buf[50];
#ifdef HAVE_PTHREAD_H
        sprintf(buf,"num_procs=%d",prtGetNumProcessors());
#else
        sprintf(buf,"num_procs=%d",mcrtGetNumProcessors());
#endif
        gc_next_command_line_argument("-gc",buf);
        first_time = false;
    }
    gc_next_command_line_argument(name,arg);
}

#define FILLER_VTABLE_SIZE 100

static VTable * g_filler_vtables[FILLER_VTABLE_SIZE];

// Updates an object's vtable to a new_vt.
// The size of objects for the new vtable must be at least sizeof(struct VTable*) less than the size of the original object.
// Returns 1 on success, 0 on failure (due to a violation of the previously stated rule).
extern "C" int pgc_modify_object_vtable_shorter(Object *object,struct VTable  *new_vt) {
    return gc_update_vtable((Managed_Object_Handle)object,(uintptr_t)new_vt);
}


extern "C" void pgc_register_global_refs(void *refs[], unsigned array_size)
{
    unsigned oldNumGlobalRefs = numGlobalRefs;
    // Make a copy of the global root array, in case the caller deallocates the
    // array, and also so that the same root doesn't get enumerated twice.
    numGlobalRefs += array_size;
    globalRefs = (void **) realloc(globalRefs, numGlobalRefs * sizeof(*globalRefs));
    memcpy(globalRefs + oldNumGlobalRefs, refs, array_size * sizeof(*globalRefs));

    // For this implementation, just compute a lower and upper memory address
    // across the set of all roots.
    for (unsigned i=oldNumGlobalRefs; i<numGlobalRefs; i++) {
        void *startAddress = globalRefs[i];
        Object *obj = (Object *) startAddress;
        if (globalRootAddressLow == NULL || startAddress < globalRootAddressLow)
            globalRootAddressLow = startAddress;
        if (startAddress > globalRootAddressHigh)
            globalRootAddressHigh = startAddress;
    }
} //pgc_register_global_roots

extern "C" void pgc_register_global_objects(void *globals[], unsigned numGlobals)
{
    unsigned oldNumGlobalRoots = numGlobalRoots;
    // Make a copy of the global root array, in case the caller deallocates the
    // array, and also so that the same root doesn't get enumerated twice.
    numGlobalRoots += numGlobals;
    globalRoots = (void **) realloc(globalRoots, numGlobalRoots * sizeof(*globalRoots));
    memcpy(globalRoots + oldNumGlobalRoots, globals, numGlobals * sizeof(*globalRoots));

    // For this implementation, just compute a lower and upper memory address
    // across the set of all roots.
    for (unsigned i=oldNumGlobalRoots; i<numGlobalRoots; i++) {
        void *startAddress = globalRoots[i];
        Object *obj = (Object *) startAddress;
        void *endAddress = (void *) ((char *)globalRoots[i] + get_object_size(obj->get_vtable(), obj));
        if (globalRootAddressLow == NULL || startAddress < globalRootAddressLow)
            globalRootAddressLow = startAddress;
        if (endAddress > globalRootAddressHigh)
            globalRootAddressHigh = endAddress;
    }
    //gc_orp_initialized();
} //pgc_register_global_roots

extern "C" Boolean orp_is_global_object(Managed_Object_Handle p_obj)
{
    if (globalRootAddressLow <= p_obj && p_obj < globalRootAddressHigh)
        return TRUE;
    return FALSE;
}


#if 0
extern "C" void gc_heap_slot_write_ref(Object *,Object **,Object *);
extern "C" void __pdecl pgc_write_ref_slot(struct Object **slot, struct Object *ref)
{
	gc_heap_slot_write_ref(NULL, slot, ref);
} // pgc_write_ref_slot
#endif

// BTL 20080314 Called at program termination to, e.g., print gc statistics.
extern "C" void pgc_wrapup()
{
    gc_wrapup();
} // pgc_wrapup


#ifdef REF_ZEROING
// BTL 20080313 Explicitly zero out ref fields in newly-allocated objects.
// If "what_to_zero"=1, zero ref fields in the variable portion of the newly-allocated "object".
// If "what_to_zero"=2, zero all ref fields of "object".
extern "C" void __pdecl pgc_zero_ref_fields(struct Object *object, unsigned size, unsigned what_to_zero)
{
    if (what_to_zero == 0) {
        return;         // doing standard memset when getting a gc block from the global pool
    }

#ifdef ZERO_AT_ALLOCATION
    memset(object, 0, size);
    return;
#endif // ZERO_AT_ALLOCATION

    assert(object);
    VTable *vt = object->get_vtable();
    assert(vt);
    Clss *obj_class = vt->clss;
    assert(obj_class);
    assert(sizeof(unsigned) == sizeof(void *));

    // Always clear the 2nd field in the object, which GC_V4 (but not TGC) assumes will be initialized to zero.
    ((unsigned *)object)[1] = 0;

    if ((what_to_zero == 2) && (obj_class->fixed_part_has_ref > 0)) {
        // Zero refs among the fixed fields too
        PgcIsRef *field_is_ref = obj_class->is_ref;  // NB: this array also describes the VTable* and obj_info words
        assert(field_is_ref);

        unsigned fixed_words = (obj_class->allocated_size / sizeof(void *)); // NB: includes the vtable pointer and object info field
        unsigned *fixed_ptr = (unsigned *)object;
        for (unsigned j = 0;  j < fixed_words;  j++) {
            if (field_is_ref[j]) {
                // zero the j-th word among the object's fixed fields
                assert(j >= 2);     // else trying to clear the VTable* or obj_info word
                *(fixed_ptr + j) = 0;
            }
        }
    }

    // Always zero refs among the fields in the "variable" part of the object: the elements if an array
    if (obj_class->array_item_has_ref && (obj_class->array_item_size > 0)) {
        // An array whose elements contain one or more ref fields
        Clss *elem_class = obj_class->array_vt.clss;
        assert(elem_class);
        assert(elem_class->is_array_elem_vt);
        assert(elem_class->containing_clss_type == obj_class);

        unsigned adj_elem_bytes = obj_class->adjusted_item_size;
        assert(adj_elem_bytes);
        unsigned adj_elem_words = (adj_elem_bytes / sizeof(void *));

        // elem_is_ref points to an array of bytes describing which fields of the array element type are refs
        PgcIsRef *elem_is_ref = obj_class->array_item_field_ref;
        assert(elem_is_ref);

        int first_elem_offset = vector_first_element_offset_unboxed(elem_class);
        unsigned *elem_ptr = (unsigned *)(((char *)object) + first_elem_offset);

        unsigned total_elem_bytes = (size - first_elem_offset);
        unsigned num_elems = (total_elem_bytes / adj_elem_bytes);
        for (unsigned i = 0;  i < num_elems;  i++) {
            // Zero the refs among the i-th element's fields.
            for (unsigned j = 0;  j < adj_elem_words;  j++) {
                if (elem_is_ref[j]) {
                    // zero the j-th word of the i-th element
                    *(elem_ptr + j) = 0;
                }
            }
            elem_ptr += adj_elem_words;
        }
    }
} // pgc_zero_ref_fields
#endif // REF_ZEROING


// Called by the GC at the beginning of a private nursery collection.
extern "C" void pgc_local_nursery_collection_start()
{
#ifndef HAVE_PTHREAD_H
    PrtTaskHandle cur_task = prtGetTaskHandle();
	if(cur_task) {
	    prtDisablePrscallStealing(cur_task);
	}
#endif
} // pgc_local_nursery_collection_start


// Called by the GC at the end of a private nursery collection.
extern "C" void pgc_local_nursery_collection_finish()
{
#ifndef HAVE_PTHREAD_H
    PrtTaskHandle cur_task = prtGetTaskHandle();
    if(cur_task) {
		prtEnablePrscallStealing(cur_task);
	}
#endif
} // pgc_local_nursery_collection_finish

extern "C" void pgc_set_wpo_vtable(VTable *vt) {
    gc_set_wpo_vtable(vt);
}

extern "C" void pgc_set_wpo_finalizer(void (*finalizer)(void *)) {
    gc_set_wpo_finalizer(finalizer);
}

#define NUM_TLS_SLOT_OPTION "num_tls_slots="

extern "C" void pgcSetOption(const char *optionString) {
    if(strncmp(optionString,NUM_TLS_SLOT_OPTION,sizeof(NUM_TLS_SLOT_OPTION)) == 0) {
        if(pgc_started()) {
            printf("You cannot change the number of TLS slots after PGC has started.\n");
            exit(-1);
        } else {
    		const char *value_ptr = optionString + strlen(NUM_TLS_SLOT_OPTION);
	    	GC_TLS_SIZE_SLOTS = atoi(value_ptr);
        }
    } else {
        printf("Unrecognized pgc option %s\n",optionString);
    }
}

extern "C" void pin_vtable(struct VTable *) {
}
