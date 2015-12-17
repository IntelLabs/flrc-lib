/*
 * COPYRIGHT_NOTICE_1
 */

#ifndef _PGC_H_
#define _PGC_H_

#include "flrclibconfig.h"

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif // HAVE_STDINT_H
#include "prt/prt.h"

struct VTable;
struct clss;
struct Object;

#ifdef __cplusplus
#define EXTERN(rt) extern "C" rt
#else
#define EXTERN(rt) rt
#endif


// BTL 20080320 Enable REF_ZEROING to zero ref fields/array items at allocation
//#define REF_ZEROING 1

// BTL 20080320 Enable both ZERO_AT_ALLOCATION and REF_ZEROING to zero whole objects at allocation
//#define ZERO_AT_ALLOCATION 1

enum PGC_MUTABILITY {
	PGC_ALWAYS_MUTABLE = 0,
	PGC_CREATED_MUTABLE = 1,
	PGC_ALWAYS_IMMUTABLE = 2
};

typedef enum {
    PGC_DATA_TYPE_INT8    = 'B',
    PGC_DATA_TYPE_UINT8   = 'b',
    PGC_DATA_TYPE_INT16   = 'S',
    PGC_DATA_TYPE_UINT16  = 's',
    PGC_DATA_TYPE_INT32   = 'I',
    PGC_DATA_TYPE_UINT32  = 'i',
    PGC_DATA_TYPE_INT64   = 'J',
    PGC_DATA_TYPE_UINT64  = 'j',
    PGC_DATA_TYPE_INTPTR  = 'N',
    PGC_DATA_TYPE_UINTPTR = 'n',
    PGC_DATA_TYPE_F8      = 'D',
    PGC_DATA_TYPE_F4      = 'F',
    PGC_DATA_TYPE_BOOLEAN = 'Z',
    PGC_DATA_TYPE_CHAR    = 'C',
    PGC_DATA_TYPE_CLASS   = 'L',
    PGC_DATA_TYPE_ARRAY   = '[',
    PGC_DATA_TYPE_VOID    = 'V',
    PGC_DATA_TYPE_MP      = 'P',        // managed pointers
    PGC_DATA_TYPE_UP      = 'p',        // unmanaged pointers
    PGC_DATA_TYPE_VALUE   = 'K',
    PGC_DATA_TYPE_STRING  = '$',        // deprecated
    PGC_DATA_TYPE_STRUCT  = '^',
    PGC_DATA_TYPE_INVALID = '?',
    PGC_DATA_TYPE_END     = ')'         // For the iterator
} PGC_Data_Type; // PGC_Data_Type

typedef unsigned PgcBool;

struct VTable {
    void *_gc_private_information;

	struct clss *clss;
    unsigned indirection_offset;

    // Offset from the top by CLASS_ALLOCATED_SIZE_OFFSET
    // The number of bytes allocated for this object. It is the same as
    // instance_data_size with the constraint bit cleared. This includes
    // the OBJECT_HEADER_SIZE as well as the OBJECT_VTABLE_POINTER_SIZE
    unsigned int   allocated_size;
    unsigned short array_element_size;
    unsigned short array_length_offset;
	enum PGC_MUTABILITY mutability;
    unsigned short pre_tenure;
#ifdef __cplusplus
    int vector_get_length(void *o);
#endif
};

#define PGC_VTABLE_RESERVE sizeof(struct VTable)

// ------------------------------------------------------------------------------
// Remove this section once we eliminate pgclib and only retain pgc_pthreads.
#ifndef PRT_CDECL
#ifdef _MSC_VER
#define PRT_CDECL __cdecl
#elif defined __GNUC__
#define PRT_CDECL __attribute__((cdecl))
#else
#error
#endif
#endif // PRT_CDECL

#ifndef CDECL_FUNC_OUT
#ifdef _MSC_VER
#define CDECL_FUNC_OUT
#elif defined __GNUC__
#define CDECL_FUNC_OUT __attribute__((cdecl))
#else
#error
#endif
#endif // CDECL_FUNC_OUT

#ifndef CDECL_FUNC_IN
#ifdef _MSC_VER
#define CDECL_FUNC_IN __cdecl
#elif defined __GNUC__
#define CDECL_FUNC_IN
#else
#error
#endif
#endif // CDECL_FUNC_IN

#ifndef STDCALL_FUNC_OUT
#ifdef _MSC_VER
#define STDCALL_FUNC_OUT
#elif defined __GNUC__
#define STDCALL_FUNC_OUT __attribute__((stdcall))
#else
#error
#endif
#endif // STDCALL_FUNC_OUT

#ifndef STDCALL_FUNC_IN
#ifdef _MSC_VER
#define STDCALL_FUNC_IN __stdcall
#elif defined __GNUC__
#define STDCALL_FUNC_IN
#else
#error
#endif
#endif // STDCALL_FUNC_IN
// End of section.
// ------------------------------------------------------------------------------

typedef void CDECL_FUNC_OUT (CDECL_FUNC_IN *PgcRuntimeCallback)(PrtRseCallback callbackIntoPgc,void *env);
typedef unsigned char PgcIsRef;


// Call this routine before any other routine.
// runtimeCallback is a function pointer that is invoked by pgc to
// enumerate global refs in the P runtime.  For each such ref, the
// P runtime is expected to invoke the incoming PgcRseCallback once
// passing the second param (env) to the first param of PgcRseCallback.
//
// update_threads_on_lock - should be true for GCv4 and false for gc_mf.
// it controls whether the thread list is updated on each time the
EXTERN(void) PRT_CDECL pgc_init(PgcRuntimeCallback runtimeCallback, int update_threads_on_lock);

// Call this right before exiting to do clean-up.
EXTERN(void) PRT_CDECL pgc_kill(void);

EXTERN(void) PRT_CDECL pgc_force_gc(void);
EXTERN(void) PRT_CDECL pgc_register_global_objects(void *globals[], unsigned numGlobals);
// The refs array contains the addresses of global refs.
EXTERN(void) PRT_CDECL pgc_register_global_refs(void *refs[], unsigned array_size);

enum PGC_ALIGNMENT {
    PGC_ALIGN_4 = 0,
    PGC_ALIGN_8 = 1,
	PGC_ALIGN_16 = 2,
	PGC_ALIGN_32 = 3,
	PGC_ALIGN_64 = 4,
	PGC_ALIGN_128 = 5,
	PGC_ALIGN_256 = 6,
	PGC_ALIGN_512 = 7,
	PGC_BASE_ALIGN_4 = 8,
	PGC_BASE_ALIGN_8 = 9
};

struct AlignmentInfo {
	PgcBool  alignArray; // If true then the array portion is aligned as specified.  If false the start is aligned.
	unsigned powerOfTwoBaseFour; // alignment requirement is 2^(2+powerOfTwoBaseFour).
};

EXTERN(struct AlignmentInfo) PRT_CDECL pgc_get_vtable_alignment(struct VTable *vt);

// Returns the offset into the object where the array porition begins if vt corresponds to an array type, else 0.
EXTERN(int) PRT_CDECL pgc_get_array_offset(struct VTable *vt);

EXTERN(void) PRT_CDECL pgc_next_command_line_argument(const char *name, const char *arg);

typedef void *Managed_Object_Handle;

// new_vt = pointer to a new vtable structure at least the size of struct VTable but can be larger if client wants to store other things in the vtable.
// size_of_fixed is the size of the fixed portion of the object
// is_ref is an array whose length is equal to size_of_fixed / sizeof(void*) and whose entries indicate whether the corresponding fields in the fixed portion of the object is a ref or not
// item_size = if greater than zero, this indicates that the object has a variable length array at its end.  The first POINTER_SIZE_INT after size_of_fixed will be a length
//          field and the array will continue afterwards.  The value of item_size is the size of each array item in terms of POINTER_SIZE_INTs.
// item_field_ref is an array whose length is equal to item_size / sizeof(void*) and which indicates whether the particular field for each item is a ref or not.
// array_length_offset is the offset from the start of the object to the array length field.  only used if array_item_size != 0.
EXTERN(void) PRT_CDECL pgc_new_object_format(
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
                                      void (*finalizer)(Managed_Object_Handle));

EXTERN(void) PRT_CDECL pgc_new_indirection_object(
                                      struct VTable *new_vt,
                                      unsigned size,
                                      unsigned indirection_ref_offset); // in bytes

// DEPRECATED.  To be removed.  DO NOT USE.
EXTERN(void) PRT_CDECL pgc_new_object_format_2(
									  struct VTable *new_vt,
                                      unsigned size_of_fixed,   // in units of bytes
                                      PgcIsRef is_ref[],
                                      unsigned array_item_size, // in units of bytes
                                      unsigned array_length_offset,
                                      PgcIsRef array_item_field_ref[],
                                      struct AlignmentInfo alignment,
									  enum PGC_MUTABILITY mutability,
                                      unsigned is_pinned,
                                      PgcIsRef weak_refs[]);

// deprecated... do not use
EXTERN(void) PRT_CDECL pin_vtable(struct VTable *);

// Allocation routines
// Memory returned is zero-filled.
EXTERN(void *) __pcdecl  pgc_allocate_or_null(unsigned size, void *vtable_id);
EXTERN(void *) PRT_CDECL pgc_allocate(unsigned size, void *vtable_id);
EXTERN(void *) PRT_CDECL pgc_allocate_escaping(unsigned size, void *vtable_id);

// Ask if the GC can definitely allocate size number of bytes without needing a collection.
EXTERN(PgcBool) __pcdecl pgc_can_allocate_without_collection(unsigned size);
// version that provides the task handle to avoid a tls lookup
EXTERN(PgcBool) __pcdecl pgc_can_allocate_without_collection_th(unsigned size, PrtTaskHandle task);
// Do whatever is necessary to make sure that size bytes can be allocated after this call without a collection.
// This call itself may or may not cause a collection.
EXTERN(void) PRT_CDECL   pgc_require_allocate_without_collection(unsigned size);
EXTERN(void) PRT_CDECL   pgc_require_allocate_without_collection_th(unsigned size, PrtTaskHandle task);

// Updates an object's vtable to a new_vt.
// The size of objects for the new vtable must be either 0 or at least
// sizeof(struct VTable*) less than the size of the original object.
// Returns 1 on success, 0 on failure (due to a violation of the previously stated rule).
#define pgc_modify_object_vtable(object,new_vt) *((struct VTable **)(object)) = (new_vt)
#define pgc_modify_object_vtable_same_size(object,new_vt) pgc_modify_object_vtable(object,new_vt)

EXTERN(int) PRT_CDECL pgc_modify_object_vtable_shorter(struct Object *object, struct VTable  *new_vt);

struct Object;
#ifndef BUILDING_GC
EXTERN(void) (__pdecl *gc_heap_slot_write_barrier_indirect)(struct Object *,struct Object **,struct Object *);
EXTERN(struct Object *) (__pdecl *gc_cas_write_barrier_indirect)(struct Object *,struct Object **,struct Object *,struct Object *);
EXTERN(void) CDECL_FUNC_OUT (CDECL_FUNC_IN *gc_mark_profiler)(unsigned gc_num, unsigned gc_thread_id, void * live_object);
#endif
#define pgc_write_ref_slot(slot,ref)	                gc_heap_slot_write_barrier_indirect(NULL, slot, ref)
#define pgc_write_ref_slot_with_base(base,slot,ref)	    gc_heap_slot_write_barrier_indirect(base, slot, ref)
#define pgc_write_ref_cas(slot,ref,cmp)	                gc_cas_write_barrier_indirect(NULL, slot, ref, cmp)
#define pgc_write_ref_cas_with_base(base,slot,ref,cmp)	gc_cas_write_barrier_indirect(base, slot, ref, cmp)

#define pgc_set_live_object_profiler(x) (gc_mark_profiler = (x))

// Called at program termination to, e.g., print gc statistics.
EXTERN(void) PRT_CDECL pgc_wrapup();

#ifdef REF_ZEROING
// BTL 20080313 Explicitly zero out ref fields in newly-allocated objects.
// If "what_to_zero"=1, zero ref fields in the variable portion of the newly-allocated "object".
// If "what_to_zero"=2, zero all ref fields of "object".
EXTERN(void) __pdecl pgc_zero_ref_fields(struct Object *object, unsigned size, unsigned what_to_zero);
#endif // REF_ZEROING

EXTERN(unsigned short) PRT_CDECL pgc_is_vtable_immutable(struct VTable *vt);

#define pgc_is_pretenured(vt) ((struct VTable*)vt)->pre_tenure
#define pgc_pretenure(vt)     ((struct VTable*)vt)->pre_tenure = 1;
#define pgc_is_indirection(vt) (((struct VTable*)vt)->indirection_offset != 0)
#define pgc_indirection_offset(vt) (((struct VTable*)vt)->indirection_offset)

#define pgc_array_length_offset(vt) ((struct VTable*)vt)->array_length_offset

// Called by the GC to indicate the start and finish of a private nursery collection.
EXTERN(void) PRT_CDECL pgc_local_nursery_collection_start();
EXTERN(void) PRT_CDECL pgc_local_nursery_collection_finish();

#ifndef HAVE_STDINT_H

typedef signed char int8_t;
typedef short int16_t;
typedef int int32_t;
#ifdef __x86_64__
typedef long int int64_t;
#else
typedef long long int64_t;
#endif

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
#ifdef __x86_64__
typedef unsigned long int uint64_t;
#else
typedef unsigned long long uint64_t;
#endif

#endif // HAVE_STDINT_H

#if defined POINTER64 || defined __x86_64__
typedef uint64_t POINTER_SIZE_INT;
typedef int64_t  POINTER_SIZE_SINT;
#else
typedef uint32_t POINTER_SIZE_INT;
typedef int32_t  POINTER_SIZE_SINT;
#endif // POINTER64

struct ManagedObject;

typedef void *SynchCriticalSectionHandle;
typedef void *Class_Handle;
typedef void *VTable_Handle;
typedef void *Field_Handle;
typedef void *ThreadThreadHandle;
typedef void *SynchEventHandle;
typedef void *SynchLockHandle;
typedef void *Vector_Handle;
typedef void *Type_Info_Handle;
typedef unsigned Boolean;
typedef unsigned char Byte;

#define EVENT_WAIT_TIMEOUT 0x102
#define EVENT_WAIT_OBJECT_0 0
#define EVENT_WAIT_FAILED (unsigned int)0xFFFFFFFF

typedef POINTER_SIZE_INT Allocation_Handle;

EXTERN(SynchCriticalSectionHandle) orp_synch_create_critical_section(void);
EXTERN(unsigned) int orp_synch_enter_critical_section(SynchCriticalSectionHandle cs);
EXTERN(void) orp_synch_leave_critical_section(SynchCriticalSectionHandle cs);
EXTERN(Class_Handle) allocation_handle_get_class(Allocation_Handle ah);
EXTERN(unsigned) int class_is_finalizable(Class_Handle ch);
EXTERN(void) orp_gc_lock_enum(void);
EXTERN(void) orp_gc_unlock_enum(void);
EXTERN(void) orp_exit(int exit_code);
EXTERN(unsigned) int orp_vtable_pointers_are_compressed(void);
EXTERN(const) char * orp_get_property_value(const char *property_name);
EXTERN(void) orp_thread_sleep(unsigned int msec);
EXTERN(unsigned) int orp_references_are_compressed(void);
EXTERN(Type_Info_Handle) class_get_element_type_info(Class_Handle ch);
EXTERN(Class_Handle) type_info_get_class(Type_Info_Handle tih);
EXTERN(unsigned) int type_info_is_primitive(Type_Info_Handle tih);
EXTERN(unsigned) int type_info_is_unboxed(Type_Info_Handle tih);
EXTERN(int) vector_first_element_offset_unboxed(Class_Handle element_type);
EXTERN(unsigned) int class_get_unboxed_data_offset(Class_Handle ch);
EXTERN(unsigned) int verify_object_header(void *ptr);
EXTERN(void) * class_get_vtable(Class_Handle ch);
EXTERN(unsigned) int class_element_size(Class_Handle ch);
EXTERN(const) char * class_get_name(Class_Handle cl);
EXTERN(Class_Handle) type_info_get_class(Type_Info_Handle tih);
EXTERN(unsigned) int orp_vector_size(Class_Handle vector_class, int length);
EXTERN(unsigned) int type_info_is_reference(Type_Info_Handle tih);
EXTERN(unsigned) int type_info_is_vector(Type_Info_Handle tih);
EXTERN(unsigned) int type_info_is_general_array(Type_Info_Handle tih);
EXTERN(unsigned) int orp_number_of_gc_bytes_in_thread_local(void);
EXTERN(unsigned) int orp_number_of_gc_bytes_in_vtable(void);
EXTERN(POINTER_SIZE_INT) orp_get_vtable_base(void);
EXTERN(void) * orp_get_gc_thread_local(void);
EXTERN(unsigned) int class_get_alignment(Class_Handle ch);
EXTERN(unsigned) int class_is_pinned(Class_Handle ch);
EXTERN(unsigned) int class_num_instance_fields_recursive(Class_Handle ch);
EXTERN(unsigned) int class_num_instance_fields(Class_Handle ch);
EXTERN(Field_Handle) class_get_instance_field_recursive(Class_Handle ch, unsigned int index);
EXTERN(unsigned) int field_is_reference(Field_Handle fh);
EXTERN(unsigned) int field_is_weak_reference(Field_Handle fh);
EXTERN(unsigned) int field_get_offset(Field_Handle fh);
EXTERN(unsigned) int class_is_array(Class_Handle ch);
EXTERN(Class_Handle) class_get_array_element_class(Class_Handle ch);
EXTERN(struct VTable *) class_get_array_element_vtable(Class_Handle ch);
EXTERN(unsigned) int class_is_non_ref_array(Class_Handle ch);
EXTERN(unsigned) int class_get_boxed_data_size(Class_Handle ch);
EXTERN(void) orp_synch_delete_critical_section(SynchCriticalSectionHandle cs);
EXTERN(void) orp_enumerate_thread(PrtTaskHandle prt_task);
EXTERN(void) orp_enumerate_global_refs(void);
EXTERN(unsigned) int orp_suspend_thread_for_enumeration(PrtTaskHandle prt_task);
EXTERN(void) orp_resume_thread_after_enumeration(PrtTaskHandle prt_task);
EXTERN(void) orp_gc_cycle_end_notification(void);
EXTERN(void) orp_finalize_object(Managed_Object_Handle pobj);
EXTERN(unsigned) int orp_synch_wait_for_event(SynchEventHandle hHandle, unsigned int dwMillisec);
EXTERN(unsigned) int orp_synch_wait_for_multiple_events(unsigned int numobj, SynchEventHandle *hHandle, unsigned int dwMillisec);
EXTERN(unsigned) int orp_synch_set_event(SynchEventHandle handle);
EXTERN(void) orp_post_gc_mcrt_cleanup(void);
EXTERN(SynchEventHandle) orp_synch_create_event(unsigned int man_reset_flag);
EXTERN(unsigned) int orp_synch_reset_event(SynchEventHandle handle);
typedef unsigned int STDCALL_FUNC_OUT (STDCALL_FUNC_IN *pgc_thread_func)(void *);
EXTERN(ThreadThreadHandle) orp_thread_create(pgc_thread_func start_address, void *arglist, unsigned *thrdaddr );
EXTERN(void) orp_set_affinity(void);
EXTERN(void) orp_get_stm_properties(void *properties);
EXTERN(void) pgc_set_wpo_vtable(struct VTable *vt);
EXTERN(void) pgc_set_wpo_finalizer(void (*finalizer)(void *));
EXTERN(void) pgcSetOption(const char *optionString);

#endif  // _PGC_H_
