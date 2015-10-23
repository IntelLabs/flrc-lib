/*
 * COPYRIGHT_NOTICE_1
 */

// $Header: /nfs/sc/proj/ctg/psl002/CVS/pillar_pthread/src/base/prtmain.cpp,v 1.14 2013/02/04 19:12:23 taanders Exp $

/*
 * Pillar "management" functions: command line and property access, DLL enquery,
 * and compiler registration.
 */

#include <assert.h>

#include "prtcodegenerator.h"

#include "prtvse.h"
#include "prttls.h"
#include "prtcims.h"
#include "prtcodeinfo.h"
#include "prtglobals.h"
#include "prtmisc.h"
#include "prtconcurrency.h"

#ifdef _WINDOWS
#include "VtuneApi.h"
#endif // _WINDOWS
#ifdef _WIN32
#include <windows.h>
#endif
#include <pthread.h>
#include <string.h>

#ifdef __GNUC__
#include <unistd.h>
#define exit(x) _exit(x)
#include <stdlib.h>
#endif // __GNUC__

// BTL 20071127 Debug
// #define BTL_DEBUGGING 1

// BTL 20071203 Uncomment to enable malloc heap debugging
#if 0 
#if defined(BTL_DEBUGGING) && defined(_DEBUG)
// BTL 20071130 Used to enable malloc heap checking. You must use the debug version of the CRT.
#include <crtdbg.h>
#endif //defined(BTL_DEBUGGING) && defined(_DEBUG)
#endif //0

// Holds the global variables for the (currently) single Pillar instance.
extern "C" Prt_Globals *prt_Globals = NULL;
static bool exiting = false;
extern "C" unsigned prtMinFreeStackSpace = 0; // initialized once at startup and thereafter constant
static bool prt_vtune_initialized = false;
#ifdef _WINDOWS
static LARGE_INTEGER perfCtrFrequency;
static LARGE_INTEGER perfEventsStartTime;
static FILE         *perfEventsLogFile = NULL;
#endif

// Forward declarations
static void initEventLogging();
static void shutdownEventLogging();

extern "C" void prt_testStackSize(void *function_to_call, void *stack_top);
extern "C" void prt_testExtendStack(void);


Prt_Globals *prt_GetGlobals(void)
{
    return prt_Globals;
} // prt_GetGlobals

////////////////////////////////////////////////////////////////////////////////
// Pillar startup.
////////////////////////////////////////////////////////////////////////////////

unsigned findLastModifiedDword(unsigned stack[],unsigned pattern,unsigned size)
{
    unsigned int i;
    for(i=0;i<size;i++) {
        if(stack[i] != pattern) return (size-i)*4;
    }
    assert(0);
    return 0; // get rid of compiler warning
} // findLastModifiedDword

#ifndef MAX
#define MAX(a,b) (((a) < (b)) ? (b) : (a))
#endif


void prt_initStealZero(void *data, void *clientData)
{
    *((PRT_POINTER_SIZE_INT*)data) = 0;
} // prt_initStealZero


// Pillar's logical entry point.
void prtMain(PrtInitialProc proc, int argc, char *argv[])
{
    // If they haven't previously called prtInit then do so now.
    if(!prt_Globals) {
        prtInit();
    }

    prt_TlsRunOnce();

    const unsigned numBootstrapArgs = 2;
    void *bootstrapArgs[numBootstrapArgs];
    bootstrapArgs[0] = (void *) argc;
    bootstrapArgs[1] = (void *) argv;
    void *newTask  = malloc(sizeof(Prt_Task));
    memset(newTask,0,sizeof(Prt_Task));
    Prt_WrappedTaskData wrappedData((PrtCodeAddress)proc, (void *)bootstrapArgs, numBootstrapArgs, NULL, newTask, NULL);
#if 0
#ifdef _WIN32
    __try {
#endif // _WIN32
#endif
        prt_BootstrapTask(&wrappedData, /*deleteWrappedData*/ false);
#if 0
#ifdef _WIN32
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        printf("Uncaught exception in prtMain.\n");
//        __throw_exception_again;
        exit(-1);
    }
#endif // _WIN32
#endif
} //prtMain


static bool validateStubConstant(const char *name, PrtRegister trueConstant, PrtRegister stubConstant)
{
    if (trueConstant != stubConstant) {
        printf("validateStubConstant: discrepancy in %s; true value is %p, stub value is %p\n",
            name, trueConstant, stubConstant);
        return false;
    }
    return true;
} //validateStubConstant

static void validateStubConstants(void)
{
    PrtRegister taskLastVse          = (PrtRegister)-1;
    PrtRegister taskUserTls          = (PrtRegister)-1;
    PrtRegister taskThreadHandle     = (PrtRegister)-1;
    PrtRegister vseEntryTypeCode     = (PrtRegister)-1;
    PrtRegister vseNextFrame         = (PrtRegister)-1;
    PrtRegister vseContinuation      = (PrtRegister)-1;
    PrtRegister continuationEip      = (PrtRegister)-1;

    bool result = true;
    prt_getStubConstants(&taskLastVse,
                         &taskUserTls,
                         &taskThreadHandle,
                         &vseEntryTypeCode,
                         &vseNextFrame,
                         &vseContinuation,
                         &continuationEip
                        );
    result = validateStubConstant("Prt_Task.mLastVse",        (PrtRegister)(&(((Prt_Task *)0)->mLastVse)), taskLastVse) && result;
    result = validateStubConstant("Prt_Task.mUserTls",        (PrtRegister)(&(((Prt_Task *)0)->mUserTls)), taskUserTls) && result;
    result = validateStubConstant("Prt_Task.mThread",         (PrtRegister)(&(((Prt_Task *)0)->mThread)), taskThreadHandle) && result;
    result = validateStubConstant("Prt_Vse.entryTypeCode",      (PrtRegister)(&(((Prt_Vse *)0)->entryTypeCode)), vseEntryTypeCode) && result;
    result = validateStubConstant("Prt_Vse.nextFrame",          (PrtRegister)(&(((Prt_Vse *)0)->nextFrame)), vseNextFrame) && result;
    result = validateStubConstant("Prt_Vse.targetContinuation", (PrtRegister)(&(((Prt_Vse *)0)->targetContinuation)), vseContinuation) && result;
    result = validateStubConstant("PrtContinuation.eip",        (PrtRegister)(&(((PrtContinuation *)0)->eip)), continuationEip) && result;

    if (!result) {
        printf("validateStubConstants: failure, exiting.\n");
        exit(0);
    }
} //validateStubConstants


PILLAR_EXPORT void prtInit(void)
{
    if(!prt_Globals) {
// BTL 20071203 Uncomment to enable malloc heap debugging
#if 0 
#if defined(BTL_DEBUGGING) && defined(_DEBUG)
        // BTL 20071130 Turn on malloc heap checking on every allocation. You must use the debug version of the CRT.
        int tmpDbgFlag;
        tmpDbgFlag = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
        tmpDbgFlag |= _CRTDBG_CHECK_ALWAYS_DF;
        _CrtSetDbgFlag(tmpDbgFlag);

        // Set to the number of the allocation that you want to break at, or -1 to not break.
        int break_at_alloc = -1;
        _CrtSetBreakAlloc(break_at_alloc);
#endif // defined(BTL_DEBUGGING) && defined(_DEBUG)
#endif //0

        // We don't have the ability in asm code to get the offset of a field
        // in a struct so we hardcode them and then here at runtime take their
        // real values in C code, pass them to the validateOffsets asm function
        // which then validates that the offsets are correct.
        validateStubConstants();

        // Create and initialize the global data structure with this Pillar instance.
        prt_Globals = new Prt_Globals;

        prt_registerBuiltinCodeInfoManagers();

#if !defined _WIN32 && !defined _WIN64
        pthread_key_create(&(prt_Globals->tls_key),NULL);
#endif
    }
} //prtInit


extern unsigned cimCreated;
#ifndef __GNUC__
extern "C" int ptw32_processInitialize();
#endif // __GNUC__

// The actual "main" entry point.
PILLAR_EXPORT void prtStart(PrtInitialProc proc, int argc, char *argv[])
{
    cimCreated = 0;

#ifndef __GNUC__
    ptw32_processInitialize();
#endif // __GNUC__

    // Save the proc to be invoked after Pillar initialization.  We can't do
    // this in the normal spot for Pillar globals since we haven't initialized that yet.
    prtMain(proc, argc, argv);
} //prtStart


typedef void (*Prt_SetOptionCallback)(const char *fullString,  // something like "key=value"
                                      const char *valueString, // the "value" substring in "key=value"
                                      void *token              // some constant token, like a prt_Globals offset
                                      );


static void defaultOptionCallback(const char *fullString, const char *valueString, void *token)
{
    *(int *)((char *)prt_GetGlobals() + (size_t) token) = atoi(valueString);
} //defaultOptionCallback


static void enableLoggingCallback(const char *fullString, const char *valueString, void *token)
{
    int value = atoi(valueString);
    prt_GetGlobals()->logEvents = value;
    if (value) {
        // Open the log file and set the event start time
        initEventLogging();
    }
} //enableLoggingCallback


// For now, assume all parameters are 32-bit integers.
struct Prt_Options {
    const char *key;
    //size_t valueOffset;
    Prt_SetOptionCallback callback;
    void *callbackToken;
    const char *description;
};

static struct Prt_Options optionTable[] = {
    { "verbosestackextension", 0, (void *) (&(((Prt_Globals *)0)->verboseStackExtension)),
       "Print messages when the stack is extended (default=0)" },
    { "suspendunmanaged", 0, (void *) (&(((Prt_Globals *)0)->suspendunmanaged)),
       "0 = suspend anytime in unmanaged, 1 = suspend when back to managed (default=0)" },
    { "logevents", enableLoggingCallback, 0,
       "Enable logging of events (default=0)" }
};
static const int optionTableSize = sizeof(optionTable)/sizeof(*optionTable);

#ifndef _WINDOWS
#define _strdup strdup
#endif // _WINDOWS

PILLAR_EXPORT void prtSetOption(PRT_IN const char *optionString)
{
    int i;
    if (!strcmp(optionString, "help")) {
        // Print help message
        printf("Pillar Runtime options:\n");
        for (i=0; i<optionTableSize; i++) {
            printf("  %s: %s\n", optionTable[i].key, optionTable[i].description);
        }
        return;
    }
    const char *equals = strchr(optionString, '=');
    if (!equals) {
        printf("Malformed Pillar Runtime option: %s\n", optionString);
        return;
    }
    prtInit(); // make sure prt_Globals is initialized

    char *copy = _strdup(optionString);
    copy[equals - optionString] = 0;
    bool found = false;
    for (i=0; !found && i<optionTableSize; i++) {
        if (!strcmp(copy, optionTable[i].key)) {
            Prt_SetOptionCallback callback = optionTable[i].callback;
            if (!callback) {
                callback = defaultOptionCallback;
            }
            callback(optionString, equals + 1, optionTable[i].callbackToken);
            found = true;
        }
    }
    if (!found) {
        printf("prtSetOption: Unknown option: %s\n", optionString);
    }
    free(copy);
} //prtSetOption


PILLAR_EXPORT void prtExit(int status)
{
    if (exiting) {
        prt_ExitThread();
    }
    exiting = true;

    shutdownEventLogging();

    // Do any shutdown, cleanup, stats dumping, etc.
    Prt_Globals *globals = prt_GetGlobals();
    exit(status);
} //prtExit


// To avoid making Pillar programs depend on having VTuneAPI.DLL on the local system, we get pointers to the 
// VTune API functions dynamically. If you link against a .dll, then on each machine you run the program, 
// you either have to specify its location in the PATH environment variable, or you have to put the .dll 
// in one of a few standard directories such as the current directory or the program's directory.
static void CDECL_FUNC_OUT (CDECL_FUNC_IN *prt_vtResume)(void) = NULL; 
static void CDECL_FUNC_OUT (CDECL_FUNC_IN *prt_vtPause) (void) = NULL; 


static void initVTuneFunctions() 
{
#ifdef _WINDOWS
    HINSTANCE prt_vtuneDll = LoadLibrary("vtuneapi.dll");
    if (prt_vtuneDll != NULL) {
        prt_vtResume = (void (__cdecl *)())GetProcAddress(prt_vtuneDll, "VTResume"); 
        prt_vtPause  = (void (__cdecl *)())GetProcAddress(prt_vtuneDll, "VTPause"); 
    }
    prt_vtune_initialized = true;
#endif
} //initVTuneFunctions


/* Pause VTune data collection for all VTune data collectors (sampling, counter monitor, and call graph). */
PILLAR_EXPORT void PRT_CDECL prtVtunePause(void)
{
#ifdef _WINDOWS
    if (!prt_vtune_initialized) {
        initVTuneFunctions();
    }
    if (prt_vtPause != NULL) {
        prt_vtPause();
    }
#endif
} //prtVtunePause


/* Resume VTune data collection for all VTune data collectors. */
PILLAR_EXPORT void PRT_CDECL prtVtuneResume(void)
{
#ifdef _WINDOWS
    if (!prt_vtune_initialized) {
        initVTuneFunctions();
    }
    if (prt_vtResume != NULL) {
        prt_vtResume();
    }
#endif
} //prtVtuneResume

static void initEventLogging()
{
#ifdef _WINDOWS
    if (!QueryPerformanceFrequency(&perfCtrFrequency)) {
        printf("initEventLogging: no high-resolution performance counter is supported, exiting\n");
        fflush(stdout);
        exit(0);
    }
    QueryPerformanceCounter(&perfEventsStartTime);
    perfEventsLogFile = fopen("events.log", "wt");
#endif
} //initEventLogging

// Log the fact that we have just made the "transition" (usually "ENTER" or "EXIT") from "state".
PILLAR_EXPORT PRT_CALL_FROM_ANYWHERE void PRT_CDECL prtLogEvent(PRT_IN void *clientData, PRT_IN char *transition, PRT_IN char *state)
{
#ifdef _WINDOWS
    if (perfEventsLogFile != NULL) {
        LARGE_INTEGER currTime;
        QueryPerformanceCounter(&currTime);
        double elapsedTicks = (double)(currTime.QuadPart - perfEventsStartTime.QuadPart);
        float elapsedTimeInMilliseconds = (float)((elapsedTicks/(double)perfCtrFrequency.QuadPart) * 1000.0);
        fprintf(perfEventsLogFile, "%.3f,%p,%p,%s,%s\n", 
                elapsedTimeInMilliseconds, prtGetTaskHandle(),
                clientData, transition, state);
        fflush(perfEventsLogFile);
    }
#endif
} //prtLogEvent


static void shutdownEventLogging()
{
#ifdef _WINDOWS
    if (perfEventsLogFile != NULL) {
        fclose(perfEventsLogFile);
    }
#endif
}


PILLAR_EXPORT unsigned PRT_CDECL prtGetNumProcessors(void) {
  long nprocs = -1;
#ifdef _WIN32
#ifndef _SC_NPROCESSORS_ONLN
SYSTEM_INFO info;
GetSystemInfo(&info);
#define sysconf(a) info.dwNumberOfProcessors
#define _SC_NPROCESSORS_ONLN
#endif
#endif
#ifdef _SC_NPROCESSORS_ONLN
    nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    assert(nprocs);
    return nprocs;
#else
    assert(0);
#endif
}

