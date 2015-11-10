/*
 * COPYRIGHT_NOTICE_1
 */

#ifndef __PILLAR_TLS_ALLOCATOR__
#define __PILLAR_TLS_ALLOCATOR__

class PillarTlsAllocator {
protected:
    static unsigned next_offset;
public:
    static unsigned get_offset(unsigned space_needed) {
        unsigned ret = next_offset;
        next_offset += space_needed;
        return ret;
    }
};

#endif 
