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

// $Header: /nfs/sc/proj/ctg/psl002/CVS/pillar_pthread/src/include/prtmisc.h,v 1.2 2011/03/09 19:09:53 taanders Exp $

#ifndef _PRTMISC_H
#define _PRTMISC_H

extern "C" void prt_ExitThread(void);
extern "C" void *prt_getCurrentEsp(void);
extern "C" void prt_getStubConstants(PRT_OUT PrtRegister *last_vse_offset, 
                                     PRT_OUT PrtRegister *prt_task_user_tls,
                                     PRT_OUT PrtRegister *task_thread_if_offset,
                                     PRT_OUT PrtRegister *vse_entry_type_offset,
                                     PRT_OUT PrtRegister *vse_vsh_offset,
                                     PRT_OUT PrtRegister *vse_cont_offset,
                                     PRT_OUT PrtRegister *continuation_eip_offset
                                    );

#endif // !_PRTMISC_H
