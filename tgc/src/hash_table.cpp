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

#include <assert.h>
#include "tgc/hash_table.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

//
// Hash codes are unsigned ints which are 32 bit quantities on both ia32 and ia64.
// This naturally limits the size of hash tables to sizes that will fit in 32 bits.
//
unsigned primes [] = {2017,
                      5501,
                      10091,
                      20021,
                      40009,
                      80021,
                      160001,
                      320009,
                      640007,
                      1280023,
                      2560049,
                      5120051,
                      10240121,
                      20480249 };

const unsigned int NUMBER_OF_PRIMES = 14;

const double HASH_TABLE_THRESHOLD = 0.6;

#ifdef LINUX
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#endif

Hash_Table::Hash_Table() {
    _prime_index = 0;

    _size_in_entries = primes[_prime_index++];

    double threshold = HASH_TABLE_THRESHOLD;

    _resident_count       = 0;
    _size_in_bytes        = _size_in_entries * (sizeof(void *));
    _threshold_entries    = (unsigned int)(_size_in_entries * threshold);
    _save_pointer         = 0;
//    printf ("Making new hash table with size_in_entries %d and threashhold_entries %d \n", _size_in_entries, _threshold_entries);
    _table = (volatile void **)malloc(_size_in_bytes);
    if (_table == NULL) {
#ifdef _WINDOWS
        DWORD LastError = GetLastError();
#else
        int LastError = errno;
#endif
        fprintf(stderr,
                "Error: malloc failed when creating hash table "
                "%d (0x%x)\n",
                LastError, LastError);
        assert(LastError);
    }

    memset(_table, 0, _size_in_bytes);

//    orp_cout << "Created hash_table " << (void *)this << std::endl;
//OBSOLETE	InitializeCriticalSection(&_critical_section);
    return;
}

/* Discard this hash table. */
Hash_Table::~Hash_Table() {
    free(_table);
}



//
// Add an entry into this hash table, if it doesn't already exist.
// Returns true if an entry was added.
bool Hash_Table::add_entry_if_required(void *address) {
	//orp_cout << "Adding entry " << address << std::endl;

    // Before we add the entry, if we might possible overflow
    // extend. Once passed this extent point we know we have
    // enough room for this entry...
    if (_resident_count > _threshold_entries) {
        _extend();
    }
	//
	// Adding a null entry is illegal, since we can't distinguish
	// it from an empty slot.
	//
    assert(address != NULL);
	//
	// Obtain the hash associated with this entry.
	//
    unsigned int hash_code = _do_rs_hash((POINTER_SIZE_INT)address,
                                         _size_in_entries);

    volatile void *target = _table[hash_code];

    if (target == address) {  // already there
        return false;
    }
    //
    // Beyond this point, the table will get modified.
    //
    // The code that was not thread safe simple did  _table[hash_code] = address;
    if (target == NULL) { // empty: try to insert in a thread safe way.

#ifdef GC_THREAD_SAFE_REMSET
        if (InterlockedCompareExchangePointer((void **)&(_table[hash_code]), address, NULL) == NULL) {
            // This slot was not taken before we could get to it, great, return.
            _resident_count++;
            return true;
        }
#else
        // This is not thread safe but putting things in remsets is only
        // done while holding the gc_lock.
        _table[hash_code] = address;
        _resident_count++;
        return true;
#endif // GC_THREAD_SAFE_REMSET
    }

    while (TRUE) {
        // This loop is guaranteed to terminate since our residency
        // rate is guaranteed to be less than 90%
        hash_code = (hash_code + 1) % _size_in_entries;
        if (address == _table[hash_code]) { // hit
            return false;
        }

        if (_table[hash_code] == NULL) {// empty slot for now
#ifdef GC_THREAD_SAFE_REMSET
            // Thread unsafe code does _table[hash_code] = address;
            if (InterlockedCompareExchangePointer((void **)&(_table[hash_code]), address, NULL) == NULL) {
                // This slot was not taken before we could get to it, great, return.
                _resident_count++;
                return true;
            }
#else
            // This is not thread safe but putting things in remsets is only
            // done while holding the gc_lock.
            _table[hash_code] = address;
            _resident_count++;
            return true;
#endif
        }
    }
}



//
// Add an entry into this hash table, if it doesn't already exist.
//
unsigned Hash_Table::add_entry(void *address) {
	//orp_cout << "Adding entry " << address << std::endl;

    // Before we add the entry, if we might possible overflow
    // extend. Once passed this extent point we know we have
    // enough room for this entry...
    if (_resident_count > _threshold_entries) {
//        printf( "------------------------------------------------------- EXTENDING HASH TABLE ------------------------------");
        _extend();
    }
	//
	// Adding a null entry is illegal, since we can't distinguish
	// it from an empty slot.
	//
    assert(address != NULL);
	//
	// Obtain the hash associated with this entry.
	//
    int hash_code = _do_rs_hash((POINTER_SIZE_INT)address,
                                         _size_in_entries);

    volatile void *target = _table[hash_code];

    if (target == address) {  // already there
        return hash_code;
    }
    //
    // Beyond this point, the table will get modified.
    //
    // The code that was not thread safe simple did  _table[hash_code] = address;
    if (target == NULL) { // empty: try to insert in a thread safe way.

#ifdef GC_THREAD_SAFE_REMSET
        if (InterlockedCompareExchangePointer((void **)&(_table[hash_code]), address, NULL) == NULL) {
            // This slot was not taken before we could get to it, great, return.
            _resident_count++;
            return hash_code;
        }
#else
        // This is not thread safe but putting things in remsets is only
        // done while holding the gc_lock.
        _table[hash_code] = address;
        _resident_count++;
        return hash_code;
#endif // GC_THREAD_SAFE_REMSET
    }

    while (TRUE) {
        // This loop is guaranteed to terminate since our residency
        // rate is guaranteed to be less than 90%
        hash_code = (hash_code + 1);
        if (hash_code >= _size_in_entries) {
            hash_code = hash_code  % _size_in_entries;
        }

        if (address == _table[hash_code]) { // hit
            return hash_code;
        }

        if (_table[hash_code] == NULL) {// empty slot for now
#ifdef GC_THREAD_SAFE_REMSET
            // Thread unsafe code does _table[hash_code] = address;
            if (InterlockedCompareExchangePointer((void **)&(_table[hash_code]), address, NULL) == NULL) {
                // This slot was not taken before we could get to it, great, return.
                _resident_count++;
                return hash_code;
            }
#else
            // This is not thread safe but putting things in remsets is only
            // done while holding the gc_lock.
            _table[hash_code] = address;
            _resident_count++;
            return hash_code;
#endif
        }
    }
}

//
// An entry at location hash_code has just been deleted. We need
// to scan to the next zero entry and rehash every intervening
// entry so that this new zero entry doesn't confuse subsequent
// add_entries from creating duplicates. This needs to be done
// in a batch, (delete all, then rehash all) to minimize complexity.
//
void Hash_Table::_rehash(unsigned int hash_code) {
//	orp_cout << " Rehashing entry " << hash_code << std::endl;
    volatile void *address = _table[hash_code];
    //
    // Since we start scanning at the freshly deleted
    // slot, we have to accomodate an initial zero.
    //
    if (address!=0) {
//		orp_cout << " In rehash removing " << address << std::endl;
        _resident_count--;
        _table[hash_code] = 0;
    }
    //
    // Hitting a zero at the next entry indicates that
    // we have scanned far enough. This is guaranteed to
    // terminate since we rehash only , and immediately
    // after, a deletion. (Beyond that, our residency
    // rate is never near 100% anyway.)
    //
    unsigned int next_entry = (hash_code + 1) % _size_in_entries;
    if (_table[next_entry]!=0) {
        _rehash(next_entry);
    }
    //
    // On the unrecursion path, do the re-insertion of
    // the address that we saved.
    //
    if (address!=0) {
        add_entry((void *)address);
    }
}


#if 0
void
Hash_Table::dump()
{
	void *pp_obj;

	printf(">>>>>>>>>>>>>>>>>>\n");
	printf("%d entries>>>>>>>>>>\n", _resident_count);
	rewind();

	while ((pp_obj = next()) != NULL) {
		printf("\t==> [%x]\n", pp_obj);
	}
	printf("<<<<<<<<<<<<<<<<<<<\n");
	return;
}
#endif // _DEBUG

void Hash_Table::empty_all() {
    memset (_table, 0, _size_in_bytes);
    _resident_count = 0;
    return;
#if 0
    // Earlier slow code.
    for (int index = 0; index < _size_in_entries; index++) {
        _table[index] = 0;
    }
	_resident_count = 0;
#endif
}

/* Merge two remembered sets, and return
   the merged one. Typical use includes merging the
   remembered sets of all the cars of a train, or of
   merging the remembered set of a region with the
   relevant portion of the root set. */
Hash_Table * Hash_Table::merge(Hash_Table *p_Hash_Table) {
    void *p_entry;

    p_Hash_Table->rewind();

    while ((p_entry = p_Hash_Table->next()) != NULL) {
        this->add_entry(p_entry);
    }

    return this;
}

/* A dumb hashing function for insertion of a new entry
   into the remembered set. Need to improve. */
unsigned int Hash_Table::_do_rs_hash(POINTER_SIZE_INT address, unsigned int table_size) {
    POINTER_SIZE_INT result = address * 42283;
	assert((POINTER_SIZE_INT)(result % table_size) <= (POINTER_SIZE_INT)0xFFFFFFFF);
    return ((unsigned int)result % table_size);
}

/* The residency in our remembered set has exceeded a pre-defined
   threshold. Therefore we create a larger remembered set and re-
   hash.
   Always rehash after doing an extend. */
void Hash_Table::_extend() {
    volatile void **p_save_table       = _table;
	int saved_size_in_entries = _size_in_entries;
    //p_save_table = _table;

    if (_prime_index >= NUMBER_OF_PRIMES) {
        _size_in_entries = _size_in_entries * 2; // Not a prime but really big.
//        cerr << "Internal Error: prime table exceeded" << std::endl;
//        assert(0);
    } else {
        _size_in_entries   = primes[_prime_index++];
    }
    _size_in_bytes     = sizeof(void *) * _size_in_entries;
    _threshold_entries = (unsigned int)(_size_in_entries * HASH_TABLE_THRESHOLD);

	_resident_count     = 0;
    _table = (volatile void **)malloc(_size_in_bytes);

    if (_table == NULL) {
        fprintf(stderr,"Error: malloc failed when extending remembered set\n");
        assert(0);
    }

	memset(_table, 0, _size_in_bytes);

    for (int index = 0; index < saved_size_in_entries; index++) {
        if (p_save_table[index] != NULL) {
            this->add_entry((void *)(p_save_table[index]));
        }
    }

    free(p_save_table);

//    printf (" xxxxxxxxxxxx ROOT TABLE HAS BEEN EXTENDED xxxxxxxxxxxxxxxxxxxxxx" );
}

/* Add an entry into the remembered set. This represents an
   address of a slot of some object in a different space
   that is significant to the space associated with this
   remembered set. */
bool Hash_Table::is_present(void *address) {
    if (address == NULL)
        return false;
#if 0
    move add_entry to avoid readers causing writes if we extend....
    if (_resident_count > _threshold_entries) {
        _extend();
    }
#endif
    // Always rehash after doing an extend.
    unsigned int hash_code = _do_rs_hash((POINTER_SIZE_INT)address,
                                         _size_in_entries);

    volatile void *target = _table[hash_code];

    if (target == address) { // already there
        return true;
	}

    if (target == NULL) { // empty: absent
        return false;
    }
	//
	// Save our position before looping.
	//
	unsigned int saved_hash_code = hash_code;
	//
	// Loop through subsequent entries looking for match.
	//
    while (TRUE) {
        // This loop is guaranteed to terminate since our residency
        // rate is guaranteed to be less than 90%
        hash_code = (hash_code + 1) % _size_in_entries;

        if (_table[hash_code] == NULL)
            return false;

        if (address == _table[hash_code])  // hit
            return true;

		if (hash_code == saved_hash_code) {
			//
			// We have traversed a full circle and are back
			// where we started, so we are sure it isn't there.
			//
			return false;
		}
    }
}

/* Add an entry into the remembered set. This represents an
   address of a slot of some object in a different space
   that is significant to the space associated with this
   remembered set. */
int Hash_Table::_get_offset(void *address) {
    if (address == NULL)
        return -1;

#if 0
    // Count on add_entry to do the extend. Otherwise to_from_table logic
    // doesn't have the appropriate locks.
    if (_resident_count > _threshold_entries) {
        orp_cout << "Extending" << std::endl;
        _extend();
    }
#endif

    // Always rehash after doing an extend.
    unsigned int hash_code = _do_rs_hash((POINTER_SIZE_INT)address,
                                         _size_in_entries);

    volatile void *target = _table[hash_code];

    if (target == address) { // already there
        return hash_code;
	}

    if (target == NULL) { // empty: absent
        return -1;
    }
	//
	// Save our position before looping.
	//
	unsigned int saved_hash_code = hash_code;
	//
	// Loop through subsequent entries looking for match.
	//
    while (TRUE) {
        // This loop is guaranteed to terminate since our residency
        // rate is guaranteed to be less than 90%
        hash_code = (hash_code + 1) % _size_in_entries;

        if (_table[hash_code] == NULL)
            return -1;

        if (address == _table[hash_code])  // hit
            return hash_code;

		if (hash_code == saved_hash_code) {
			//
			// We have traversed a full circle and are back
			// where we started, so we are sure it isn't there.
			//
			return -1;
		}
    }
}


void * Hash_Table::next() {
    //
    // See if there are any entries in this hash table.
    //
    if (_resident_count == 0) {
        //
        // Nope - bail out.
        //
        return NULL;
    }


    if (_save_pointer >= _size_in_entries) {
        return NULL;
    }

    while (_table[_save_pointer] == NULL) {
        _save_pointer += 1;

        if (_save_pointer == _size_in_entries) {
            return NULL;
        }
    }

    void *p_return = (void *)_table[_save_pointer];
    _save_pointer++;
    return p_return;
}

//
// Start at the beginning for subsequent scans.
//
void Hash_Table::rewind() {
    _save_pointer = 0;
}

////////////////////////////////////////////////////////////////////////////////
//
//
// The key val hash table code which looks a lot like the code above.
//
//
///////////////////////////////////////////////////////////////////////////////
Count_Hash_Table::Count_Hash_Table() {
    _prime_index = 0;

    _size_in_entries = primes[_prime_index++];

    double threshold = HASH_TABLE_THRESHOLD;

    _resident_count       = 0;
    _size_in_bytes        = _size_in_entries * (sizeof(void *));
    _threshold_entries    = (unsigned int)(_size_in_entries * threshold);
    _save_pointer         = 0;

    _key_table = (volatile void **)malloc(_size_in_bytes);
    if (_key_table == NULL) {
#ifdef _WINDOWS
        DWORD LastError = GetLastError();
#else
        unsigned LastError = errno;
#endif
        fprintf(stderr,
                "Error: malloc failed when creating key val hash table "
                "%d (0x%x)\n",
                LastError, LastError);
        assert(LastError);
    }
    memset(_key_table, 0, _size_in_bytes);

    int val_size_in_bytes = _size_in_entries * (sizeof(long));
    _val_table = (long *)malloc(val_size_in_bytes);

    if (_val_table == NULL) {
#ifdef _WINDOWS
        DWORD LastError = GetLastError();
#else
        unsigned LastError = errno;
#endif
        fprintf(stderr,
                "Error: malloc failed when creating key val hash table "
                "%d (0x%x)\n",
                LastError, LastError);
        assert(LastError);
    }
    memset((void *)_val_table, 0, val_size_in_bytes);

    return;
}

/* Discard this hash table. */
Count_Hash_Table::~Count_Hash_Table() {
    free(_key_table);
    free((void *)_val_table);
}

//
// Add an entry into this hash table, if it doesn't already exist.
// If it does exist then just return the index to it.
//
unsigned Count_Hash_Table::add_entry(void *address) {
	//orp_cout << "Adding entry " << address << std::endl;

    // Before we add the entry, if we might possible overflow
    // extend. Once passed this extent point we know we have
    // enough room for this entry...
    if (_resident_count > _threshold_entries) {
//        printf( "------------------------------------------------------- EXTENDING HASH TABLE ------------------------------");
        _extend();
    }
	//
	// Adding a null entry is illegal, since we can't distinguish
	// it from an empty slot.
	//
    assert(address != NULL);
	//
	// Obtain the hash associated with this entry.
	//
    int hash_code = _do_rs_hash((POINTER_SIZE_INT)address,
                                         _size_in_entries);

    volatile void *target = _key_table[hash_code];

    if (target == address) {  // already there
        return hash_code;
    }
    //
    // Beyond this point, the table will get modified.
    //
    // The code that was not thread safe simple did  _table[hash_code] = address;
    if (target == NULL) { // empty: try to insert in a thread safe way.

#ifdef GC_THREAD_SAFE_REMSET
        if (InterlockedCompareExchangePointer((void **)&(_key_table[hash_code]), address, NULL) == NULL) {
            // This slot was not taken before we could get to it, great, return.
            _resident_count++;
            return hash_code;
        }
#else
        // This is not thread safe but putting things in remsets is only
        // done while holding the gc_lock.
        _key_table[hash_code] = address;
        _resident_count++;
        return hash_code;
#endif // GC_THREAD_SAFE_REMSET
    }

    while (TRUE) {
        // This loop is guaranteed to terminate since our residency
        // rate is guaranteed to be less than 90%
        hash_code = (hash_code + 1);
        if (hash_code >= _size_in_entries) {
            hash_code = hash_code  % _size_in_entries;
        }

        if (address == _key_table[hash_code]) { // hit
            return hash_code;
        }

        if (_key_table[hash_code] == NULL) {// empty slot for now
#ifdef GC_THREAD_SAFE_REMSET
            // Thread unsafe code does _table[hash_code] = address;
            if (InterlockedCompareExchangePointer((void **)&(_key_table[hash_code]), address, NULL) == NULL) {
                // This slot was not taken before we could get to it, great, return.
                _resident_count++;
                return hash_code;
            }
#else
            // This is not thread safe but putting things in remsets is only
            // done while holding the gc_lock.
            _key_table[hash_code] = address;
            _resident_count++;
            return hash_code;
#endif
        }
    }
}

long Count_Hash_Table::get_val(void* address) {
    int index = _get_offset(address);
    return _val_table[index];
}


void Count_Hash_Table::inc_val(void* address, int increment) {
    int index = _get_offset(address);

#ifdef ORP_POSIX
    _val_table[index] = _val_table[index] + increment;
#else
    InterlockedExchangeAdd(&(_val_table[index]), (long)increment);
#endif
}



#if 0
void
Count_Hash_Table::dump()
{
	void *pp_obj;

	printf(">>>>>>>>>>>>>>>>>>\n");
	printf("%d entries>>>>>>>>>>\n", _resident_count);
	rewind();

	while ((pp_obj = next()) != NULL) {
		printf("\t==> [%x]\n", pp_obj);
	}
	printf("<<<<<<<<<<<<<<<<<<<\n");
	return;
}
#endif // _DEBUG

void Count_Hash_Table::empty_all() {
    memset (_key_table, 0, _size_in_bytes);
    int val_size_in_bytes = sizeof(long) * _size_in_entries;
    memset ((void *)_val_table, 0, val_size_in_bytes);
    _resident_count = 0;
    return;
}

// Zero out the vals but keep the keys.
void Count_Hash_Table::zero_counts() {
    int val_size_in_bytes = sizeof(long) * _size_in_entries;
    memset ((void *)_val_table, 0, val_size_in_bytes);
}

/* A dumb hashing function for insertion of a new entry
   into the remembered set. Need to improve. */
unsigned int Count_Hash_Table::_do_rs_hash(POINTER_SIZE_INT address, unsigned int table_size) {
    POINTER_SIZE_INT result = address * 42283;
	assert((POINTER_SIZE_INT)(result % table_size) <= (POINTER_SIZE_INT)0xFFFFFFFF);
    return ((unsigned int)result % table_size);
}

/* The residency in our remembered set has exceeded a pre-defined
   threshold. Therefore we create a larger remembered set and re-
   hash.
   Always rehash after doing an extend. */
void Count_Hash_Table::_extend() {
    volatile void **p_save_key_table       = _key_table;
    long *p_save_val_table        = _val_table;
	int saved_size_in_entries = _size_in_entries;
    //p_save_table = _table;

    if (_prime_index >= NUMBER_OF_PRIMES) {
        _size_in_entries = _size_in_entries * 2; // Not a prime but really big.
//        cerr << "Internal Error: prime table exceeded" << std::endl;
//        assert(0);
    } else {
        _size_in_entries   = primes[_prime_index++];
    }
    _size_in_bytes     = sizeof(void *) * _size_in_entries;
    _threshold_entries = (unsigned int)(_size_in_entries * HASH_TABLE_THRESHOLD);

	_resident_count     = 0;
    _key_table = (volatile void **)malloc(_size_in_bytes);

    if (_key_table == NULL) {
        fprintf(stderr,"Error: malloc failed when extending key val table\n");
        assert(0);
    }
    memset(_key_table, 0, _size_in_bytes);

    int val_size_in_bytes = sizeof(long) * _size_in_entries;
    _val_table = (long *)malloc(val_size_in_bytes);

    if (_val_table == NULL) {
        fprintf(stderr,"Error: malloc failed when extending key val table\n");
        assert(0);
    }

	memset((void *)_val_table, 0, val_size_in_bytes);

    for (int index = 0; index < saved_size_in_entries; index++) {
        if (p_save_key_table[index] != NULL) {
            int new_index = this->add_entry((void *)(p_save_key_table[index]));
            _val_table[new_index] = p_save_val_table[index];
        } else {
            assert (p_save_val_table[index] == 0);
        }
    }

    free(p_save_key_table);
    free((void *)p_save_val_table);
}

/* Add an entry into the remembered set. This represents an
   address of a slot of some object in a different space
   that is significant to the space associated with this
   remembered set. */
bool Count_Hash_Table::is_present(void *address) {
    if (address == NULL) {
        return false;
    }

    // Always rehash after doing an extend.
    unsigned int hash_code = _do_rs_hash((POINTER_SIZE_INT)address,
                                         _size_in_entries);

    volatile void *target = _key_table[hash_code];

    if (target == address) { // already there
        return true;
	}

    if (target == NULL) { // empty: absent
        return false;
    }
	//
	// Save our position before looping.
	//
	unsigned int saved_hash_code = hash_code;
	//
	// Loop through subsequent entries looking for match.
	//
    while (TRUE) {
        // This loop is guaranteed to terminate since our residency
        // rate is guaranteed to be less than 90%
        hash_code = (hash_code + 1) % _size_in_entries;

        if (_key_table[hash_code] == NULL)
            return false;

        if (address == _key_table[hash_code])  // hit
            return true;

		if (hash_code == saved_hash_code) {
			//
			// We have traversed a full circle and are back
			// where we started, so we are sure it isn't there.
			//
			return false;
		}
    }
}

/* Add an entry into the remembered set. This represents an
   address of a slot of some object in a different space
   that is significant to the space associated with this
   remembered set. */
int Count_Hash_Table::_get_offset(void *address) {
    if (address == NULL) {
        return -1;
    }

    // Always rehash after doing an extend.
    unsigned int hash_code = _do_rs_hash((POINTER_SIZE_INT)address,
                                         _size_in_entries);

    volatile void *target = _key_table[hash_code];

    if (target == address) { // already there
        return hash_code;
	}

    if (target == NULL) { // empty: absent
        return -1;
    }
	//
	// Save our position before looping.
	//
	unsigned int saved_hash_code = hash_code;
	//
	// Loop through subsequent entries looking for match.
	//
    while (TRUE) {
        // This loop is guaranteed to terminate since our residency
        // rate is guaranteed to be less than 90%
        hash_code = (hash_code + 1) % _size_in_entries;

        if (_key_table[hash_code] == NULL)
            return -1;

        if (address == _key_table[hash_code])  // hit
            return hash_code;

		if (hash_code == saved_hash_code) {
			//
			// We have traversed a full circle and are back
			// where we started, so we are sure it isn't there.
			//
			return -1;
		}
    }
}

// Returns the next key, use get_val to get the value.
void * Count_Hash_Table::next() {
    //
    // See if there are any entries in this hash table.
    //
    if (_resident_count == 0) {
        //
        // Nope - bail out.
        //
        return NULL;
    }


    if (_save_pointer >= _size_in_entries) {
        return NULL;
    }

    while (_key_table[_save_pointer] == NULL) {
        _save_pointer += 1;

        if (_save_pointer == _size_in_entries) {
            return NULL;
        }
    }

    void *p_return = (void *)_key_table[_save_pointer];
    _save_pointer++;
    return p_return;
}

//
// Start at the beginning for subsequent scans.
//
void Count_Hash_Table::rewind() {
    _save_pointer = 0;
}

// end file gc\hash_table.cpp
