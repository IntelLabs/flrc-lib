/*
 * COPYRIGHT_NOTICE_1
 */

#ifndef _MARK_H
#define _MARK_H

#ifdef _IA64_

extern "C" inline uint8 LockedCompareExchangeUint8(uint8 *Destination,uint8 Exchange,uint8 Comperand);

extern "C" inline uint16 LockedCompareExchangeUint16(uint16 *Destination,uint16 Exchange,uint16 Comperand);

#else // !_IA64_

static inline uint8 LockedCompareExchangeUint8(uint8 *Destination,uint8 Exchange,uint8 Comperand) {
#ifdef _IA64_
    assert(0);
    orp_exit(17060);
    return 0;

#else // _IA64_

#ifdef ORP_POSIX
    uint8 OrigValue=0;
#if 1
    __asm__(
	LOCK_PREFIX "\tcmpxchgb %1, (%2)"
        :"=a"(Comperand)
        :"d"(Exchange), "r"(Destination), "a"(Comperand)
    );
#else
    __asm__(
        LOCK_PREFIX "\tcmpxchgb %2, %3\t\n"
        :"=a"(OrigValue), "=m"(*Destination)
        :"r"(Exchange), "m"(*Destination), "al"(Comperand)
        :"memory"
        );
#endif

#else
    __asm {
        mov al,  Comperand
            mov dl,  Exchange
            mov ecx, Destination
            lock cmpxchg [ecx], dl
            mov Comperand, al
    }
#endif //#ifdef ORP_POSIX

    return Comperand;

#endif // _IA64_
}

static inline uint16 LockedCompareExchangeUint16(uint16 *Destination, uint16 Exchange, uint16 Comperand) {
#ifdef ORP_POSIX
	__asm__(
		LOCK_PREFIX "\tcmpxchgw %1, (%2)"
        :"=a"(Comperand)
        :"d"(Exchange), "r"(Destination), "a"(Comperand)
	);
#else  // !ORP_POSIX
	__asm {
		mov ax,  Comperand
		mov dx,  Exchange
		mov ecx, Destination
		lock cmpxchg [ecx], dx
		mov Comperand, ax
	}
#endif // !ORP_POSIX
	return Comperand;
} //LockedCompareExchangeUint16

static inline uint32 LockedCompareExchangeUint32(uint32 *Destination, uint32 Exchange, uint32 Comperand) {
#ifdef ORP_POSIX
	__asm__(
		LOCK_PREFIX "\tcmpxchgl %1, (%2)"
        :"=a"(Comperand)
        :"d"(Exchange), "r"(Destination), "a"(Comperand)
	);
#else  // !ORP_POSIX
	__asm {
		mov eax,  Comperand
		mov edx,  Exchange
		mov ecx, Destination
		lock cmpxchg [ecx], edx
		mov Comperand, eax
	}
#endif // !ORP_POSIX
	return Comperand;
} //LockedCompareExchangeUint16

#ifdef __x86_64__
static inline uint64 LockedCompareExchangeUint64(
						uint64 *Destination,
						uint64 Exchange,
						uint64 Comperand)
{
#ifdef ORP_POSIX
	__asm__(
		LOCK_PREFIX "\tcmpxchgq %1, (%2)"
        :"=a"(Comperand)
        :"d"(Exchange), "r"(Destination), "a"(Comperand)
	);
#else  // !ORP_POSIX
	__asm {
		mov eax,  Comperand
		mov edx,  Exchange
		mov ecx, Destination
		lock cmpxchg [ecx], edx
		mov Comperand, eax
	}
#endif // !ORP_POSIX
	return Comperand;
} //LockedCompareExchangeUint16
#endif // __x86_64__

#endif // !_IA64_



inline void * GC_BLOCK_ADDR_FROM_MARK_BIT_INDEX(block_info *BLOCK, unsigned MARK_BIT_INDEX) {
	return (void *)((uintptr_t)BLOCK + GC_BLOCK_INFO_SIZE_BYTES + (MARK_BIT_INDEX * GC_LIVE_OBJECT_CARD_SIZE_IN_BYTES));
}

inline void GC_CLEAR_BLOCK_MARK_BIT_VECTOR(block_info *BLOCK) {
	memset(BLOCK->mark_bit_vector, 0, GC_SIZEOF_MARK_BIT_VECTOR_IN_BYTES);
}

inline bool is_mark_bit_vector_clear(block_info *block) {
    for(unsigned i=0;i<GC_SIZEOF_MARK_BIT_VECTOR_IN_BYTES;++i) {
        if(block->mark_bit_vector[i]) return true;
    }
    return false;
}

inline unsigned int GC_LIVE_OBJECT_CARD_INDEX_INTO_GC_BLOCK_NEW(Partial_Reveal_Object *P_OBJ) {
	return (((((uintptr_t)P_OBJ) & GC_BLOCK_LOW_MASK) >> GC_LIVE_OBJECT_CARD_SIZE_BITS) - GC_NUM_LIVE_OBJECT_CARDS_IN_GC_BLOCK_INFO);
}

extern "C" unsigned global_is_in_heap(Partial_Reveal_Object *p_obj);

static inline void get_mark_byte_and_mark_bit_index_for_object(Partial_Reveal_Object *p_obj, unsigned int *p_byte_index, unsigned int *p_bit_index) {
#ifdef _DEBUG
    if(!global_is_in_heap(p_obj)) {
        printf("Attempt to get the mark bit for an object not in the heap.\n");
        exit(-1);
    }
#endif // _DEBUG

    unsigned int object_index_into_block = (unsigned int) (GC_LIVE_OBJECT_CARD_INDEX_INTO_GC_BLOCK_NEW(p_obj));
    // assert(object_index_into_block >= 0); // pointless comparison : is unsigned
    assert(object_index_into_block < (GC_NUM_LIVE_OBJECT_CARDS_PER_GC_BLOCK - GC_NUM_LIVE_OBJECT_CARDS_IN_GC_BLOCK_INFO));

#ifdef _DEBUG // Check that the bit conversion STUFF has two-way integrity.....ZZZZZ
    Partial_Reveal_Object *p_test_obj = (Partial_Reveal_Object *)
        ((POINTER_SIZE_INT)GC_BLOCK_INFO(p_obj) + GC_BLOCK_INFO_SIZE_BYTES + (object_index_into_block * GC_LIVE_OBJECT_CARD_SIZE_IN_BYTES));
    assert(p_test_obj == p_obj);
#endif // _DEBUG

    *p_byte_index = object_index_into_block / GC_NUM_BITS_PER_BYTE;
    *p_bit_index = object_index_into_block & (GC_NUM_BITS_PER_BYTE - 1);

    assert(*p_bit_index == (object_index_into_block % GC_NUM_BITS_PER_BYTE));
    assert(object_index_into_block == ((*p_byte_index * GC_NUM_BITS_PER_BYTE) + *p_bit_index));
    return;
}


inline bool mark_object_in_block(Partial_Reveal_Object *p_obj) {
    unsigned int object_index_byte = 0;
    unsigned int bit_index_into_byte = 0;
    assert (!p_obj->isForwarded());
    // If it does break we need to either
    // 1. Adjust the bits in the header so the GC has a bit that is not used by other parts of the VM or by an address.
    // 2. Adjust the algorithm so that there is an additional pass of the objects that have non-zero values in the obj_info field so
    //    that these values are saved prior to the colocation work being done.
    // 3. Use Rick and Sree's "bits are bits*" hack.  *Direct quote from Prof. Robert Graham (OS/360 architect)
    //
    get_mark_byte_and_mark_bit_index_for_object(p_obj, &object_index_byte, &bit_index_into_byte);

    volatile uint8 *p_byte = &(GC_BLOCK_INFO(p_obj)->mark_bit_vector[object_index_byte]);

    uint8 mask = (1 << bit_index_into_byte);

    if ((*p_byte & mask) == mask) {
        // Object already marked
        return false;
    }

    uint8 old_val, final_val, val;

    while (true) {
        old_val = *p_byte;
        final_val = old_val | mask;

        if (old_val != final_val) {
            if (LockedCompareExchangeUint8((uint8 *)p_byte, final_val, old_val) == old_val) {
                // SUCCESS
                return true;	// ALL DONE....
            }
        }
        val = *p_byte;
        if (val == (val | mask)) {
            return false;	// Somebody beat me to it...;-(
        }
    } // while
    assert(0);
}


static inline bool is_object_marked(Partial_Reveal_Object *p_obj) {
    unsigned int object_index_byte = 0;
    unsigned int bit_index_into_byte = 0;

    get_mark_byte_and_mark_bit_index_for_object(p_obj, &object_index_byte, &bit_index_into_byte);

    volatile uint8 *p_byte = &(GC_BLOCK_INFO(p_obj)->mark_bit_vector[object_index_byte]);

    if ((*p_byte & (1 << bit_index_into_byte)) == (1 << bit_index_into_byte)) {
        // Object is marked
        return true;
    } else {
        return false;
    }
}

#endif // _MARK_H
