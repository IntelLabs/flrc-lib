/*
 * COPYRIGHT_NOTICE_1
 */

// System header files
#include <iostream>

// GC header files
#include "tgc/gc_cout.h"
#include "tgc/gc_header.h"
#include "tgc/gc_v4.h"
#include "tgc/gcv4_synch.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline bool IS_DESIRED_BIT_VALUE(bool IS_ZERO_STR, uint8 *BYTE_ADDR, unsigned BIT_INDEX) {
	return ( (*BYTE_ADDR & (1 << BIT_INDEX)) == (IS_ZERO_STR ? 0 : (1 << BIT_INDEX)));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const uint8 right_most_bit [256] =
{
    0, 0, 1, 0, 2, 0, 1, 0,
    3, 0, 1, 0, 2, 0, 1, 0,
    4, 0, 1, 0, 2, 0, 1, 0,
    3, 0, 1, 0, 2, 0, 1, 0,
    5, 0, 1, 0, 2, 0, 1, 0,
    3, 0, 1, 0, 2, 0, 1, 0,
    4, 0, 1, 0, 2, 0, 1, 0,
    3, 0, 1, 0, 2, 0, 1, 0,
    6, 0, 1, 0, 2, 0, 1, 0,
    3, 0, 1, 0, 2, 0, 1, 0,
    4, 0, 1, 0, 2, 0, 1, 0,
    3, 0, 1, 0, 2, 0, 1, 0,
    5, 0, 1, 0, 2, 0, 1, 0,
    3, 0, 1, 0, 2, 0, 1, 0,
    4, 0, 1, 0, 2, 0, 1, 0,
    3, 0, 1, 0, 2, 0, 1, 0,
    7, 0, 1, 0, 2, 0, 1, 0,
    3, 0, 1, 0, 2, 0, 1, 0,
    4, 0, 1, 0, 2, 0, 1, 0,
    3, 0, 1, 0, 2, 0, 1, 0,
    5, 0, 1, 0, 2, 0, 1, 0,
    3, 0, 1, 0, 2, 0, 1, 0,
    4, 0, 1, 0, 2, 0, 1, 0,
    3, 0, 1, 0, 2, 0, 1, 0,
    6, 0, 1, 0, 2, 0, 1, 0,
    3, 0, 1, 0, 2, 0, 1, 0,
    4, 0, 1, 0, 2, 0, 1, 0,
    3, 0, 1, 0, 2, 0, 1, 0,
    5, 0, 1, 0, 2, 0, 1, 0,
    3, 0, 1, 0, 2, 0, 1, 0,
    4, 0, 1, 0, 2, 0, 1, 0,
    3, 0, 1, 0, 2, 0, 1, 0
};
#ifdef USE_ORP_STW

//
// 3 if & branch return compares for every call.
// Split the top 4 bits from the bottom then split the bottom 2 and then figur out which on is set.
inline unsigned int get_right_most_bit (uint8 x) {
    assert (x);
    if (x & 0xf) {
        if (x & 3) {
            if (x & 1) {
                return 0;
            } else {
                return 1;
            }
        } else if (x & 4) {
            return 2;
        } else {
            return 3;
        }
    } else {
        if (x & 0x30) {
            if (x & 0x10) {
                return 4;
            } else {
                return 5;
            }
        } else {
            if (x & 0x40) {
                return 6;
            } else {
                return 7;
            }
        }
    }
}

#else // USE_ORP_STW

inline unsigned int get_right_most_bit (uint8 x) {
    assert (x);
    return ((unsigned int)right_most_bit[x]);
}

#endif // USE_ORP_STW
/** The loop through the zero words is where all the time is spent.
    Some prefetching might be worth considering.
**/
void get_next_set_bit(set_bit_search_info *info) {
	uint8 *p_byte_start = info->p_start_byte;
	unsigned int start_bit_index = info->start_bit_index;
	uint8 *p_ceil = info->p_ceil_byte;

	uint8 *p_byte = p_byte_start;

#ifdef _IA64_
	POINTER_SIZE_INT alignment_mask = 0x0000000000000007;
#else
	POINTER_SIZE_INT alignment_mask = 0x00000003;
#endif // _IA64_

    uint8 a_byte = *p_byte;
    // Clear up to bit index.
    uint8 mask = ~((1<<start_bit_index) - 1);
    a_byte = a_byte & mask;

    if (a_byte) {
        info->bit_set_index =  get_right_most_bit (a_byte);
		info->p_non_zero_byte = p_byte;
        return;
    }

	// Move to next byte
	p_byte++;

	// Skip "0" bytes till we get to a word boundary
	while ((p_byte < p_ceil) && (*p_byte == 0) && ((uintptr_t) p_byte & alignment_mask)) {
		p_byte++;
	}

	if (p_byte >= p_ceil) {
		// Reached the end....there is no "1" after (p_byte_start, start_bit_index)
		info->bit_set_index = 0;
		info->p_non_zero_byte = NULL;
		return;
	}

    if (*p_byte != 0) {
        info->bit_set_index = get_right_most_bit (*p_byte);
		info->p_non_zero_byte = p_byte;
        return;
    }

    // We are definitely at a word boundary
	POINTER_SIZE_INT *p_word = (POINTER_SIZE_INT *) p_byte;
	assert(((uintptr_t) p_word & alignment_mask) == 0);
    // This loop is where we spend all of out time. So lets do some optimization.
    // Originally we had the conditional (((uint8 *)((POINTER_SIZE_INT) p_word + sizeof(POINTER_SIZE_INT)) < p_ceil) && (*p_word == 0))
    // Lets adjust p_ceil to subtract sizeof(POINTER_SIZE_INT) and hoist it. This will speed up the loop by about 10%.
    uint8 *p_ceil_last_to_check = p_ceil - sizeof(POINTER_SIZE_INT);

	while (((uint8 *)(uintptr_t) p_word < p_ceil_last_to_check) && (*p_word == 0)) {
		p_word++;	// skip a zero word each time
	}

	p_byte = (uint8 *) p_word;

	// Skip past "zero" bytes....
	while ((p_byte < p_ceil) && (*p_byte == 0)) {
		p_byte++;
	}

	if (p_byte >= p_ceil) {
		// Reached the end....there is no "1" after (p_byte_start, start_bit_index)
		info->bit_set_index = 0;
		info->p_non_zero_byte = NULL;
	} else {
		info->bit_set_index = get_right_most_bit (*p_byte);
		info->p_non_zero_byte = p_byte;
	}
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool get_num_consecutive_similar_bits(uint8 *p_byte_start, unsigned int bit_index_to_search_from, unsigned int *num_consec_bits, uint8 *p_ceil) {
	if (p_ceil <= p_byte_start) {
		assert(0);
		orp_exit(17013);
	}

	bool is_zero_str = ((*p_byte_start) & (1 << bit_index_to_search_from)) ? false : true;
	uint8 byte_search_value = is_zero_str ? 0x00 : 0xFF;

#ifdef _IA64
	POINTER_SIZE_INT word_search_value = is_zero_str ? 0x0000000000000000 : 0xFFFFffffFFFFffff;
	POINTER_SIZE_INT alignment_mask = 0x0000000000000007;
#else
	POINTER_SIZE_INT word_search_value = is_zero_str ? 0x00000000 : 0xFFFFffff;
	POINTER_SIZE_INT alignment_mask = 0x00000003;
#endif // _IA64_

	assert(((uintptr_t)p_ceil & alignment_mask) == 0);	//????

	uint8 *p_byte = p_byte_start;
	unsigned int bit_index = bit_index_to_search_from + 1;
	unsigned int num_all_same_bits = 1;		// Need to start from the beginning

	while ((bit_index < GC_NUM_BITS_PER_BYTE) && IS_DESIRED_BIT_VALUE(is_zero_str, p_byte, bit_index)) {
		bit_index++;
		num_all_same_bits++;
	}

	if (bit_index != GC_NUM_BITS_PER_BYTE) {
		*num_consec_bits = num_all_same_bits;	// failed because some bit in the same byte was dissimilar
		return is_zero_str ? false : true;
	}

	// move to next byte
	p_byte++;

	while ((p_byte < p_ceil) && (*p_byte == byte_search_value) && ((uintptr_t) p_byte & alignment_mask)) {	// skip bytes till we get to a word boundary
		num_all_same_bits += GC_NUM_BITS_PER_BYTE;
		p_byte++;
	}

	if (p_byte >= p_ceil) { // reached the end
		*num_consec_bits = num_all_same_bits;
		return is_zero_str ? false : true;
	}

	if (*p_byte != byte_search_value) {
		// there might be more bits in this of interest..
		bit_index = 0;
		while ((bit_index < GC_NUM_BITS_PER_BYTE) && IS_DESIRED_BIT_VALUE(is_zero_str, p_byte, bit_index)){
			bit_index++;
			num_all_same_bits++;
		}
		*num_consec_bits = num_all_same_bits;
		return is_zero_str ? false : true;
	}

	// We are definitely at a word boundary
	POINTER_SIZE_INT *p_word = (POINTER_SIZE_INT *) p_byte;
	assert(((uintptr_t) p_word & alignment_mask) == 0);

	while (((uint8 *)((uintptr_t) p_word + sizeof(POINTER_SIZE_INT)) < p_ceil) && (*p_word == word_search_value)) {
		num_all_same_bits += (sizeof(POINTER_SIZE_INT) * GC_NUM_BITS_PER_BYTE);	// jump ahead 32 bits or 64 bits
		p_word++;	// skip a word each time
		p_byte = (uint8 *) p_word;
	}

	while ((p_byte < p_ceil) && (*p_byte == byte_search_value)) {
		num_all_same_bits += GC_NUM_BITS_PER_BYTE;
		p_byte++;
	}

	bit_index = 0;
	while ((p_byte < p_ceil) && (bit_index < GC_NUM_BITS_PER_BYTE) && IS_DESIRED_BIT_VALUE(is_zero_str, p_byte, bit_index)){
		bit_index++;
		num_all_same_bits++;
	}

	*num_consec_bits = num_all_same_bits;
	return is_zero_str ? false : true;
}
