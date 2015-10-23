/*
 * COPYRIGHT_NOTICE_1
 */

#include "pair_table.h"
#include "string.h"
#include "gc_header.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
Gray_Ssb::Gray_Ssb()
{
//    printf ("Making new hash table with size_in_entries %d and threashhold_entries %d \n", _size_in_entries, _threshold_entries);
    ssb_chain = (gray_ssb *)malloc(sizeof(gray_ssb));
    assert(ssb_chain);
    memset (ssb_chain, 0, sizeof(gray_ssb));
    return;
}

Gray_Ssb::~Gray_Ssb()
/* Discard this hash table. */
{
    gray_ssb *this_ssb = ssb_chain;
    while (this_ssb) {
        ssb_chain = ssb_chain->next;
        free (this_ssb);
        this_ssb = ssb_chain;
    }
    assert (ssb_chain == NULL);
}

void Gray_Ssb::add(Partial_Reveal_Object *obj)
{    
    if (current->free_index == SSB_SIZE) {
        current->next = (gray_ssb *)malloc(sizeof(gray_ssb));
        current->next = current;
        if (current->next == NULL) {
            printf ("(Internal - ORP out of malloc space generating a new Gray_Ssb\n");
            orp_exit(17031);
        }
        memset (current, 0, sizeof(gray_ssb));
    }
    current->buffer[current->free_index].obj = obj;
    current->free_index++;
}

bool Gray_Ssb::next(Partial_Reveal_Object **obj)
{
    assert (ssb_chain);
    assert (current_scan);
    if (current_scan->scan_index < current_scan->free_index) {
        *obj = current_scan->buffer[current_scan->scan_index].obj;
        current_scan->scan_index++;
        return true;
    }
    *obj = NULL;
    return false;
}

// Reset all the scan indexes in the ssb chain.
void Gray_Ssb::rewind()
{
    gray_ssb *this_ssb = ssb_chain;
    while (this_ssb) {
        this_ssb->scan_index = 0;
        this_ssb = this_ssb->next;
    }
    current_scan = ssb_chain;
}

void Gray_Ssb::clear()
{
    gray_ssb *this_ssb = ssb_chain;
    while (this_ssb) {
        this_ssb->scan_index = 0;
        this_ssb->free_index = 0;
        this_ssb = this_ssb->next;
    }
    current_scan = ssb_chain;
    current = ssb_chain;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
Pair_Table::Pair_Table()
{
//    printf ("Making new hash table with size_in_entries %d and threashhold_entries %d \n", _size_in_entries, _threshold_entries);
    ssb_chain = (ssb *)malloc(sizeof(ssb));
    assert(ssb_chain);
    memset (ssb_chain, 0, sizeof(ssb));
    return;
}

Pair_Table::~Pair_Table()
/* Discard this hash table. */
{
    ssb *this_ssb = ssb_chain;
    while (this_ssb) {
        ssb_chain = ssb_chain->next;
        free (this_ssb);
        this_ssb = ssb_chain;
    }
    assert (ssb_chain == NULL);
}

void Pair_Table::add(Partial_Reveal_Object *key, POINTER_SIZE_INT val, int32 val2)
{
    ssb *this_ssb = ssb_chain;
    // Grab the last ssb.
    while (this_ssb->next) {
        this_ssb = this_ssb->next;
    }
    if (this_ssb->free_index == SSB_SIZE) {
        this_ssb->next = (ssb *)malloc(sizeof(ssb));
        if (this_ssb->next == NULL) {
            printf ("(Internal - ORP out of malloc space generating a new Pair_Table\n");
            orp_exit(17032);
        }
        this_ssb = this_ssb->next;
        memset (this_ssb, 0, sizeof(ssb));
    }
    this_ssb->buffer[this_ssb->free_index].key = key;
    this_ssb->buffer[this_ssb->free_index].val = val;
    this_ssb->buffer[this_ssb->free_index].val2 = val2;
    
//    printf ("latency in is %d\n", val2);
    this_ssb->free_index++;
}

bool Pair_Table::next(Partial_Reveal_Object **key, POINTER_SIZE_INT *val, int32 *val2)
{
    assert (ssb_chain);
    ssb *this_ssb = ssb_chain;
    while ((this_ssb->scan_index == this_ssb->free_index) && this_ssb->next) {
        this_ssb = this_ssb->next;
    }
    if (this_ssb->scan_index < this_ssb->free_index) {
        *key = this_ssb->buffer[this_ssb->scan_index].key;
        *val = this_ssb->buffer[this_ssb->scan_index].val;
        *val2 = this_ssb->buffer[this_ssb->scan_index].val2;
//        printf ("latency out is %d\n", *val2);
        this_ssb->scan_index++;
        return true;
    }
    *key = NULL;
    *val = 0;
    *val2 = 0;
    return false;
}

// Reset all the scan indexes in the ssb chain.
void Pair_Table::rewind()
{
    ssb *this_ssb = ssb_chain;
    while (this_ssb) {
        this_ssb->scan_index = 0;
        this_ssb = this_ssb->next;
    }
}


/****************************** Triple_Table code *************************/
// This table has a key and three values.
  
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
Triple_Table::Triple_Table()
{
//    printf ("Making new hash table with size_in_entries %d and threashhold_entries %d \n", _size_in_entries, _threshold_entries);
    ssb_chain = (ssb_triple *)malloc(sizeof(ssb_triple));
    assert(ssb_chain);
    memset (ssb_chain, 0, sizeof(ssb_triple));
    return;
}

Triple_Table::~Triple_Table()
/* Discard this hash table. */
{
    ssb_triple *this_ssb = ssb_chain;
    while (this_ssb) {
        ssb_chain = ssb_chain->next;
        free (this_ssb);
        this_ssb = ssb_chain;
    }
    assert (ssb_chain == NULL);
}

void Triple_Table::add(Partial_Reveal_Object *key, POINTER_SIZE_INT val, int32 val2, int32 val3)
{
    ssb_triple *this_ssb = ssb_chain;
    // Grab the last ssb.
    while (this_ssb->next) {
        this_ssb = this_ssb->next;
    }
    if (this_ssb->free_index == SSB_SIZE) {
        this_ssb->next = (ssb_triple *)malloc(sizeof(ssb_triple));
        if (this_ssb->next == NULL) {
            printf ("(Internal - ORP out of malloc space generating a new Pair_Table\n");
            orp_exit(17033);
        }
        this_ssb = this_ssb->next;
        memset (this_ssb, 0, sizeof(ssb_triple));
    }
    this_ssb->buffer[this_ssb->free_index].key = key;
    this_ssb->buffer[this_ssb->free_index].val = val;
    this_ssb->buffer[this_ssb->free_index].val2 = val2;
    this_ssb->buffer[this_ssb->free_index].val3 = val3;
    
//    printf ("latency in is %d\n", val2);
    this_ssb->free_index++;
}
bool Triple_Table::next(Partial_Reveal_Object **key, POINTER_SIZE_INT *val, int32 *val2, int32 *val3)
{
    assert (ssb_chain);
    ssb_triple *this_ssb = ssb_chain;
    while ((this_ssb->scan_index == this_ssb->free_index) && this_ssb->next) {
        this_ssb = this_ssb->next;
    }
    if (this_ssb->scan_index < this_ssb->free_index) {
        *key = this_ssb->buffer[this_ssb->scan_index].key;
        *val = this_ssb->buffer[this_ssb->scan_index].val;
        *val2 = this_ssb->buffer[this_ssb->scan_index].val2;
        *val3 = this_ssb->buffer[this_ssb->scan_index].val3;
        this_ssb->scan_index++;
        return true;
    }
    *key = NULL;
    *val = 0;
    *val2 = 0;
    *val3 = 0;
    return false;
}

// Reset all the scan indexes in the ssb chain.
void Triple_Table::rewind()
{
    ssb_triple *this_ssb = ssb_chain;
    while (this_ssb) {
        this_ssb->scan_index = 0;
        this_ssb = this_ssb->next;
    }
}


/****************************** Sorted_Table code *************************/
//
// This table is populated, sorted and then probed to membership. If an object is added
// to the table after it has been sorted then an error is reported. Currently the 
// probe is log N where N is the number of entries.
//

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
const int SORTED_TABLE_SIZE = 256;

static int 
pair_table_intcompare( const void *arg1, const void *arg2 )
{
    POINTER_SIZE_INT a = *(POINTER_SIZE_INT *)arg1;
    POINTER_SIZE_INT b = *(POINTER_SIZE_INT *)arg2;
    if (a < b) {
        return -1;
    }
    if (a == b) {
        return 0;
    }
    return 1;
}

Sorted_Table::Sorted_Table()
{
    init_sorted_table(SORTED_TABLE_SIZE);
    free_index = 0;
    iter = 0;
}

Sorted_Table::Sorted_Table(unsigned int size)
{
    init_sorted_table(size);
    free_index = 0;
    iter = 0;
}

Sorted_Table::~Sorted_Table()
/* Discard this hash table. */
{
    free (sorted_table);
}

void Sorted_Table::init_sorted_table(unsigned int size)
{
    unsigned int size_in_bytes = sizeof(void *) * size;
    sorted_table = (POINTER_SIZE_INT *)malloc(size_in_bytes);
    memset (sorted_table, 0, size_in_bytes);
    sorted_table_size = size;
    table_sorted = false;
    members_found = 0;
}

//
// Add val to the table. If the table is full then double the size.
//

void Sorted_Table::add(void *val)
{
    assert (free_index < sorted_table_size);
    sorted_table[free_index] = (POINTER_SIZE_INT)val;
    free_index++;
    if (free_index == sorted_table_size) {
        void *old_sorted_table = sorted_table;
        init_sorted_table (sorted_table_size * 2);
        memmove(sorted_table, old_sorted_table, free_index * sizeof (void *)); 
        free(old_sorted_table);
    }
    table_sorted = false;
}

// Sort the table;
void Sorted_Table::sort()
{
    if (table_sorted) {
        return;
    }
    sorted_table_size = free_index;
    qsort(sorted_table, sorted_table_size, sizeof(void *), pair_table_intcompare);
    table_sorted = true;
    /*
    unsigned int i = 0;
    for (i = 0; i < sorted_table_size; i++) {
        printf ("sorted_table[%d] = %p\n", i, sorted_table[i]);
        if (i > 0) {
            assert (sorted_table[i] >= sorted_table[i-1]);
        }
    } 
    */
}
#if 0
bool Sorted_Table::member (POINTER_SIZE_INT val)
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
#endif
