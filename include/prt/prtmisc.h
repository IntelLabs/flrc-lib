/*
 * COPYRIGHT_NOTICE_1
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
