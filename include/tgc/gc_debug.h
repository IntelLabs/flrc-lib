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
