/*
 * COPYRIGHT_NOTICE_1
 */

#ifndef _pair_table_H_
#define _pair_table_H_

#include "tgc/gc_header.h"
#include <assert.h>

struct Partial_Reveal_Object;

/* Roughly set to produce a (slightly less than) 4096 bytes malloc call. */

const int SSB_SIZE = (4096 - 5*sizeof(POINTER_SIZE_INT));

struct gray_list {
    Partial_Reveal_Object *obj;
};

struct key_val {
    Partial_Reveal_Object *key;
    POINTER_SIZE_INT val;
    int32 val2;
};

struct key_val_val {
    Partial_Reveal_Object *key;
    POINTER_SIZE_INT val;
    int32 val2;
    int32 val3;
};

/* This is not thread safe so must only be used in a thread local fashion
   or when the thread is stopped. */
class Gray_Ssb {
    struct gray_ssb {
        gray_ssb *next;
        unsigned int free_index;
        unsigned int scan_index;
        gray_list buffer[SSB_SIZE];
    };

public:
    Gray_Ssb();
    virtual ~Gray_Ssb();
    void add (Partial_Reveal_Object *obj);
    void rewind();
    // True if obj is valid otherwise false.
    bool next(Partial_Reveal_Object **obj);
    void clear();
private:
    gray_ssb *ssb_chain;
    gray_ssb *current;
    gray_ssb *current_scan;
};

class Pair_Table {
public:
    // This is a simple SSB style implementation that holds pairs of object.
    // It is currently *not* thread safe.

    struct ssb {
        ssb *next;
        unsigned int free_index;
        unsigned int scan_index;
        key_val buffer[SSB_SIZE];
    };


    Pair_Table ();
    virtual ~Pair_Table();

    void add (Partial_Reveal_Object *key, POINTER_SIZE_INT val, int32 val2);

    void rewind();

    // Return: true if key and val hold valid data
    // Otherwise returns false.
    bool next(Partial_Reveal_Object **key, POINTER_SIZE_INT *val, int32 *val2);

private:
    ssb *ssb_chain;
};

class Triple_Table {
public:
    // This is a simple SSB style implementation that holds pairs of object.
    // It is currently *not* thread safe.

    struct ssb_triple {
        ssb_triple *next;
        unsigned int free_index;
        unsigned int scan_index;
        key_val_val buffer[SSB_SIZE];
    };


    Triple_Table ();
    virtual ~Triple_Table();

    void add (Partial_Reveal_Object *key, POINTER_SIZE_INT val, int32 val2, int32 val3);

    void rewind();

    // Return: true if key and val hold valid data
    // Otherwise returns false.
    bool next(Partial_Reveal_Object **key, POINTER_SIZE_INT *val, int32 *val2, int32 *val3);

private:
    ssb_triple *ssb_chain;
};

class Sorted_Table {
public:
    Sorted_Table();
    Sorted_Table(unsigned int size);
    ~Sorted_Table();

    void init_sorted_table(unsigned int size);
    //
    // Add val to the table. If the table is full then double the size.
    //

    void add(void *val);


    // Checks to see if the table contains val.
    // Returns true if it is a member
    //         false otherwise
    //

    inline
    bool member (POINTER_SIZE_INT val)
    {
        if (!table_sorted) {
            assert(0); // We can't sort now unless we make this code thread safe.
        }
        // Do binary search to find val
        assert(val);
        // Binary search.
        int low = 0, high = sorted_table_size - 1;
        while (low <= high) {
            // printf ("high sorted_table[%d]= %p, low sorted_table[%d]= %p \n", high, (void *)sorted_table[high], low, (void *)sorted_table[low]);
            unsigned int mid = (high + low) / 2;
            if (sorted_table[mid] == val) {
                // This object was moved during GC
                members_found++;
                // printf ("Found val = %d, mid = %d\n", val, mid);
                return true;
            } else if ((POINTER_SIZE_INT) sorted_table[mid] < (POINTER_SIZE_INT) val) {
                low = mid + 1;
            } else {
                high = mid - 1;
            }
        }
        // printf ("Not found val = %d\n", val);
        // couldnt find val ....return false
        return false;
    }
#if 0
    bool member (POINTER_SIZE_INT val);
#endif
/*
    void rewind () {
        iter = 0;
    }

    inline bool next (POINTER_SIZE_INT **slot) {
        if (iter >= free_index) {
            // At end of the sorted_table
            *slot = NULL;
            return false;
        }
        *slot = &sorted_table[iter];
        iter++;
        while (!(*slot)) {
            // This slot is empty for some reason.
            if (iter >= free_index) {
                // At end of the sorted_table
                *slot = NULL;
                return false;
            }
            *slot = &sorted_table[iter];
            iter++;
        }
        return true;
    }
*/
    // Sort the table; This assumes that race condition are dealt with above.
    void sort();

private:

    unsigned int sorted_table_size;
    unsigned int free_index;
    unsigned int iter;
    POINTER_SIZE_INT *sorted_table;
    Boolean table_sorted;
    unsigned int members_found;
};

#endif // _pair_table_H_
