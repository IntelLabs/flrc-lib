/*
 * COPYRIGHT_NOTICE_1
 */

#ifdef ORP_POSIX

#ifndef LONG
#define LONG long
#endif
#ifndef PVOID
#define PVOID void *
#endif
#ifndef IN
#define IN
#endif
#ifndef OUT
#define OUT
#endif

LONG InterlockedExchangeAddPosix(
				IN OUT PVOID Addend,
				IN LONG Value
				);

#define InterlockedExchangeAdd(a, b) InterlockedExchangeAddPosix(a, b);

PVOID InterlockedCompareExchangePosix(IN OUT PVOID *Destination,
				 IN PVOID Exchange,
				 IN PVOID Comperand
				 );

// #ifndef _IA64_
#define InterlockedCompareExchange(a, b, c) InterlockedCompareExchangePosix((PVOID *)(a), (PVOID)(b), (PVOID)(c))
#define InterlockedCompareExchangePointer InterlockedCompareExchange
// #endif // !_IA64_

#else //ORP_POSIX

#ifndef _IA64_
#ifdef _VC80_UPGRADE
//#define InterlockedCompareExchange(a, b, c) InterlockedCompareExchange((volatile LONG *)(a), (PVOID)(b), (PVOID)(c))
#else
#define InterlockedCompareExchange(a, b, c) InterlockedCompareExchange((PVOID *)(a), (PVOID)(b), (PVOID)(c))
#ifndef InterlockedCompareExchangePointer
#define InterlockedCompareExchangePointer InterlockedCompareExchange
#endif
#endif
#endif // !_IA64_

#endif //ORP_POSIX