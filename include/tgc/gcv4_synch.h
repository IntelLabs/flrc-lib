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
