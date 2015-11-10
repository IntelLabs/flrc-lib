/*
 * COPYRIGHT_NOTICE_1
 */

// Debug routine headers and constants.
//

#ifndef _gc_debug_H_
#define _gc_debug_H_

extern bool verify_live_heap;
void add_repointed_info_for_thread(Partial_Reveal_Object *p_old, Partial_Reveal_Object *p_new, unsigned int thread_id);

// Replace the header with this distinct pattern during debugging
// of the heap.
// ww -- I set this to zero to get hash to work properly //#define OBJECT_DEBUG_HEADER 0xCDAB0000
#define OBJECT_DEBUG_HEADER 0

// Add this trailer to an extra word in the object during debugging
#define OBJECT_DEBUG_TRAILER 0xBEBAFECA


//
// Memory patterns.
//
#if 0
//
// Original nursery free bytes.
//
#define DEBUG_NURSERY_ORIGINAL_FREE 0xF0

//
// Replaced nursery free bytes.
//
#define DEBUG_NURSERY_REPLACED_FREE 0xF1

//
// Nurseries that are freed and shouldn't be used.f
//
#define DEBUG_NURSERY_DISCARDED 0xF2

//
// These nursery blocks haven't been used yet:
//
#define DEBUG_STEP_ORIGINAL_FREE 0xF5

//
// These nursery blocks were discarded.
//
#define DEBUG_STEP_DISCARDED 0xF6

//
// This is a debugging pattern placed in all free blocks.
//
#define DEBUG_FREE_FIXED_BLOCK_PATTERN 0xBB

#endif // 0

#endif // _gc_debug_H_
