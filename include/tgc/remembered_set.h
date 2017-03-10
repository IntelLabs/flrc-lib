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

#ifndef _Remembered_Set_H_
#define _Remembered_Set_H_

//
// A remembered set is maintained per car and per generation
// (except the highest numbered car and the oldest generation).
// and is a non-duplicate list of pointers to pointers from
// outside that region. Typically only pointers from older to
// younger generations, and higher to lower trains are recorded.
//

#include <stdio.h>
#include "tgc/compressed_references.h"

class Train_Generation;

//
// Remembered Sets inherit from Hash Table and enhance it
// by typing the entries of the Hash Table to be pointers
// to Java_lang_Object.
//
#ifdef REMEMBERED_SET_ARRAY

#include "tgc/slot_offset_list.h"

class Remembered_Set : public ExpandInPlaceArray<void *> {
public:

    // For now remove the inline so we can check for valid references.
    bool add_entry(Slot address) {
        return ExpandInPlaceArray<void *>::add_entry(address.get_value());
    }

	void dump() {
		Slot pp_obj(NULL);

		printf(">>>>>>>>>>>>>>>>>>\n");
		printf("%d entries>>>>>>>>>>\n", size());

        iterator iter;

        for(iter  = begin();
            iter != end();
            ++iter) {
            pp_obj.set(iter.get_current());
			printf("\t==> [%p](%p)\n", pp_obj.get_value(), pp_obj.dereference());
        }

		printf("<<<<<<<<<<<<<<<<<<<\n");
	}
};

#else

#include "tgc/hash_table.h"

class Remembered_Set : public Hash_Table {
public:

    // For now remove the inline so we can check for valid references.

    unsigned add_entry(Slot address) {
        return Hash_Table::add_entry(address.get_value());
    }

	void dump() {
		Slot pp_obj(NULL);

		printf(">>>>>>>>>>>>>>>>>>\n");
		printf("%d entries>>>>>>>>>>\n", _resident_count);
		rewind();

		while (pp_obj.set(next().get_value()) != NULL) {
			printf("\t==> [%p](%p)\n", pp_obj.get_value(), pp_obj.dereference());
		}
		printf("<<<<<<<<<<<<<<<<<<<\n");
		return;
	}

#if 0
    Remembered_Set *merge(Remembered_Set *rs) {
		return (Remembered_Set *)Hash_Table::merge((Hash_Table *)rs);
	}
#endif

    inline bool is_present(Slot address) {
		return Hash_Table::is_present(address.get_value());
	}

    inline bool is_not_present(Slot address) {
		return Hash_Table::is_not_present(address.get_value());
	}

	inline Slot next(void) {
		return Slot(Hash_Table::next());
	}

};

#endif // REMEMBERED_SET_ARRAY

#endif // _Remembered_Set_H_
