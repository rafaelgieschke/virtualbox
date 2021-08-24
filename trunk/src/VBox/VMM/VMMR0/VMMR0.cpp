/* $Id$ */
/** @file
 * VMM - Host Context Ring 0.
 */

/*
 * Copyright (C) 2006-2020 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_VMM
#include <VBox/vmm/vmm.h>
#include <VBox/sup.h>
#include <VBox/vmm/iom.h>
#include <VBox/vmm/trpm.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/pgm.h>
#ifdef VBOX_WITH_NEM_R0
# include <VBox/vmm/nem.h>
#endif
#include <VBox/vmm/em.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/tm.h>
#include "VMMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/gvm.h>
#ifdef VBOX_WITH_PCI_PASSTHROUGH
# include <VBox/vmm/pdmpci.h>
#endif
#include <VBox/vmm/apic.h>

#include <VBox/vmm/gvmm.h>
#include <VBox/vmm/gmm.h>
#include <VBox/vmm/gim.h>
#include <VBox/intnet.h>
#include <VBox/vmm/hm.h>
#include <VBox/param.h>
#include <VBox/err.h>
#include <VBox/version.h>
#include <VBox/log.h>

#include <iprt/asm-amd64-x86.h>
#include <iprt/assert.h>
#include <iprt/crc.h>
#include <iprt/mem.h>
#include <iprt/memobj.h>
#include <iprt/mp.h>
#include <iprt/once.h>
#include <iprt/stdarg.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/timer.h>
#include <iprt/time.h>

#include "dtrace/VBoxVMM.h"


#if defined(_MSC_VER) && defined(RT_ARCH_AMD64) /** @todo check this with with VC7! */
#  pragma intrinsic(_AddressOfReturnAddress)
#endif

#if defined(RT_OS_DARWIN) && ARCH_BITS == 32
# error "32-bit darwin is no longer supported. Go back to 4.3 or earlier!"
#endif



/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def VMM_CHECK_SMAP_SETUP
 * SMAP check setup. */
/** @def VMM_CHECK_SMAP_CHECK
 * Checks that the AC flag is set if SMAP is enabled. If AC is not set,
 * it will be logged and @a a_BadExpr is executed. */
/** @def VMM_CHECK_SMAP_CHECK2
 * Checks that the AC flag is set if SMAP is enabled.  If AC is not set, it will
 * be logged, written to the VMs assertion text buffer, and @a a_BadExpr is
 * executed. */
#if (defined(VBOX_STRICT) || 1) && !defined(VBOX_WITH_RAM_IN_KERNEL)
# define VMM_CHECK_SMAP_SETUP() uint32_t const fKernelFeatures = SUPR0GetKernelFeatures()
# define VMM_CHECK_SMAP_CHECK(a_BadExpr) \
    do { \
        if (fKernelFeatures & SUPKERNELFEATURES_SMAP) \
        { \
            RTCCUINTREG fEflCheck = ASMGetFlags(); \
            if (RT_LIKELY(fEflCheck & X86_EFL_AC)) \
            { /* likely */ } \
            else \
            { \
                SUPR0Printf("%s, line %d: EFLAGS.AC is clear! (%#x)\n", __FUNCTION__, __LINE__, (uint32_t)fEflCheck); \
                a_BadExpr; \
            } \
        } \
    } while (0)
# define VMM_CHECK_SMAP_CHECK2(a_pGVM, a_BadExpr) \
    do { \
        if (fKernelFeatures & SUPKERNELFEATURES_SMAP) \
        { \
            RTCCUINTREG fEflCheck = ASMGetFlags(); \
            if (RT_LIKELY(fEflCheck & X86_EFL_AC)) \
            { /* likely */ } \
            else if (a_pGVM) \
            { \
                SUPR0BadContext((a_pGVM)->pSession, __FILE__, __LINE__, "EFLAGS.AC is zero!"); \
                RTStrPrintf((a_pGVM)->vmm.s.szRing0AssertMsg1, sizeof((a_pGVM)->vmm.s.szRing0AssertMsg1), \
                            "%s, line %d: EFLAGS.AC is clear! (%#x)\n", __FUNCTION__, __LINE__, (uint32_t)fEflCheck); \
                a_BadExpr; \
            } \
            else \
            { \
                SUPR0Printf("%s, line %d: EFLAGS.AC is clear! (%#x)\n", __FUNCTION__, __LINE__, (uint32_t)fEflCheck); \
                a_BadExpr; \
            } \
        } \
    } while (0)
#else
# define VMM_CHECK_SMAP_SETUP()                         uint32_t const fKernelFeatures = 0
# define VMM_CHECK_SMAP_CHECK(a_BadExpr)                NOREF(fKernelFeatures)
# define VMM_CHECK_SMAP_CHECK2(a_pGVM, a_BadExpr)       NOREF(fKernelFeatures)
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
RT_C_DECLS_BEGIN
#if defined(RT_ARCH_X86) && (defined(RT_OS_SOLARIS) || defined(RT_OS_FREEBSD))
extern uint64_t __udivdi3(uint64_t, uint64_t);
extern uint64_t __umoddi3(uint64_t, uint64_t);
#endif
RT_C_DECLS_END
static int vmmR0UpdateLoggers(PGVM pGVM, VMCPUID idCpu, PVMMR0UPDATELOGGERSREQ pReq, bool fRelease);
static FNRTLOGFLUSH vmmR0LogFlush;
static FNRTLOGFLUSH vmmR0LogRelFlush;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Drag in necessary library bits.
 * The runtime lives here (in VMMR0.r0) and VBoxDD*R0.r0 links against us. */
struct CLANG11WEIRDNOTHROW { PFNRT pfn; } g_VMMR0Deps[] =
{
    { (PFNRT)RTCrc32 },
    { (PFNRT)RTOnce },
#if defined(RT_ARCH_X86) && (defined(RT_OS_SOLARIS) || defined(RT_OS_FREEBSD))
    { (PFNRT)__udivdi3 },
    { (PFNRT)__umoddi3 },
#endif
    { NULL }
};

#ifdef RT_OS_SOLARIS
/* Dependency information for the native solaris loader. */
extern "C" { char _depends_on[] = "vboxdrv"; }
#endif


/**
 * Initialize the module.
 * This is called when we're first loaded.
 *
 * @returns 0 on success.
 * @returns VBox status on failure.
 * @param   hMod        Image handle for use in APIs.
 */
DECLEXPORT(int) ModuleInit(void *hMod)
{
    VMM_CHECK_SMAP_SETUP();
    VMM_CHECK_SMAP_CHECK(RT_NOTHING);

#ifdef VBOX_WITH_DTRACE_R0
    /*
     * The first thing to do is register the static tracepoints.
     * (Deregistration is automatic.)
     */
    int rc2 = SUPR0TracerRegisterModule(hMod, &g_VTGObjHeader);
    if (RT_FAILURE(rc2))
        return rc2;
#endif
    LogFlow(("ModuleInit:\n"));

#ifdef VBOX_WITH_64ON32_CMOS_DEBUG
    /*
     * Display the CMOS debug code.
     */
    ASMOutU8(0x72, 0x03);
    uint8_t bDebugCode = ASMInU8(0x73);
    LogRel(("CMOS Debug Code: %#x (%d)\n", bDebugCode, bDebugCode));
    RTLogComPrintf("CMOS Debug Code: %#x (%d)\n", bDebugCode, bDebugCode);
#endif

    /*
     * Initialize the VMM, GVMM, GMM, HM, PGM (Darwin) and INTNET.
     */
    int rc = vmmInitFormatTypes();
    if (RT_SUCCESS(rc))
    {
        VMM_CHECK_SMAP_CHECK(RT_NOTHING);
        rc = GVMMR0Init();
        if (RT_SUCCESS(rc))
        {
            VMM_CHECK_SMAP_CHECK(RT_NOTHING);
            rc = GMMR0Init();
            if (RT_SUCCESS(rc))
            {
                VMM_CHECK_SMAP_CHECK(RT_NOTHING);
                rc = HMR0Init();
                if (RT_SUCCESS(rc))
                {
                    VMM_CHECK_SMAP_CHECK(RT_NOTHING);

                    PDMR0Init(hMod);
                    VMM_CHECK_SMAP_CHECK(RT_NOTHING);

                    rc = PGMRegisterStringFormatTypes();
                    if (RT_SUCCESS(rc))
                    {
                        VMM_CHECK_SMAP_CHECK(RT_NOTHING);
#ifdef VBOX_WITH_2X_4GB_ADDR_SPACE
                        rc = PGMR0DynMapInit();
#endif
                        if (RT_SUCCESS(rc))
                        {
                            VMM_CHECK_SMAP_CHECK(RT_NOTHING);
                            rc = IntNetR0Init();
                            if (RT_SUCCESS(rc))
                            {
#ifdef VBOX_WITH_PCI_PASSTHROUGH
                                VMM_CHECK_SMAP_CHECK(RT_NOTHING);
                                rc = PciRawR0Init();
#endif
                                if (RT_SUCCESS(rc))
                                {
                                    VMM_CHECK_SMAP_CHECK(RT_NOTHING);
                                    rc = CPUMR0ModuleInit();
                                    if (RT_SUCCESS(rc))
                                    {
#ifdef VBOX_WITH_TRIPLE_FAULT_HACK
                                        VMM_CHECK_SMAP_CHECK(RT_NOTHING);
                                        rc = vmmR0TripleFaultHackInit();
                                        if (RT_SUCCESS(rc))
#endif
                                        {
                                            VMM_CHECK_SMAP_CHECK(rc = VERR_VMM_SMAP_BUT_AC_CLEAR);
                                            if (RT_SUCCESS(rc))
                                            {
                                                LogFlow(("ModuleInit: returns success\n"));
                                                return VINF_SUCCESS;
                                            }
                                        }

                                        /*
                                         * Bail out.
                                         */
#ifdef VBOX_WITH_TRIPLE_FAULT_HACK
                                        vmmR0TripleFaultHackTerm();
#endif
                                    }
                                    else
                                        LogRel(("ModuleInit: CPUMR0ModuleInit -> %Rrc\n", rc));
#ifdef VBOX_WITH_PCI_PASSTHROUGH
                                    PciRawR0Term();
#endif
                                }
                                else
                                    LogRel(("ModuleInit: PciRawR0Init -> %Rrc\n", rc));
                                IntNetR0Term();
                            }
                            else
                                LogRel(("ModuleInit: IntNetR0Init -> %Rrc\n", rc));
#ifdef VBOX_WITH_2X_4GB_ADDR_SPACE
                            PGMR0DynMapTerm();
#endif
                        }
                        else
                            LogRel(("ModuleInit: PGMR0DynMapInit -> %Rrc\n", rc));
                        PGMDeregisterStringFormatTypes();
                    }
                    else
                        LogRel(("ModuleInit: PGMRegisterStringFormatTypes -> %Rrc\n", rc));
                    HMR0Term();
                }
                else
                    LogRel(("ModuleInit: HMR0Init -> %Rrc\n", rc));
                GMMR0Term();
            }
            else
                LogRel(("ModuleInit: GMMR0Init -> %Rrc\n", rc));
            GVMMR0Term();
        }
        else
            LogRel(("ModuleInit: GVMMR0Init -> %Rrc\n", rc));
        vmmTermFormatTypes();
    }
    else
        LogRel(("ModuleInit: vmmInitFormatTypes -> %Rrc\n", rc));

    LogFlow(("ModuleInit: failed %Rrc\n", rc));
    return rc;
}


/**
 * Terminate the module.
 * This is called when we're finally unloaded.
 *
 * @param   hMod        Image handle for use in APIs.
 */
DECLEXPORT(void) ModuleTerm(void *hMod)
{
    NOREF(hMod);
    LogFlow(("ModuleTerm:\n"));

    /*
     * Terminate the CPUM module (Local APIC cleanup).
     */
    CPUMR0ModuleTerm();

    /*
     * Terminate the internal network service.
     */
    IntNetR0Term();

    /*
     * PGM (Darwin), HM and PciRaw global cleanup.
     */
#ifdef VBOX_WITH_2X_4GB_ADDR_SPACE
    PGMR0DynMapTerm();
#endif
#ifdef VBOX_WITH_PCI_PASSTHROUGH
    PciRawR0Term();
#endif
    PGMDeregisterStringFormatTypes();
    HMR0Term();
#ifdef VBOX_WITH_TRIPLE_FAULT_HACK
    vmmR0TripleFaultHackTerm();
#endif

    /*
     * Destroy the GMM and GVMM instances.
     */
    GMMR0Term();
    GVMMR0Term();

    vmmTermFormatTypes();

    LogFlow(("ModuleTerm: returns\n"));
}


/**
 * Initializes VMM specific members when the GVM structure is created.
 *
 * @param   pGVM        The global (ring-0) VM structure.
 */
VMMR0_INT_DECL(void) VMMR0InitPerVMData(PGVM pGVM)
{
    pGVM->vmmr0.s.hMemObjLogger         = NIL_RTR0MEMOBJ;
    pGVM->vmmr0.s.hMapObjLogger         = NIL_RTR0MEMOBJ;
    pGVM->vmmr0.s.hMemObjReleaseLogger  = NIL_RTR0MEMOBJ;
    pGVM->vmmr0.s.hMapObjReleaseLogger  = NIL_RTR0MEMOBJ;
    pGVM->vmmr0.s.fCalledInitVm         = false;

    for (VMCPUID idCpu = 0; idCpu < pGVM->cCpus; idCpu++)
    {
        PGVMCPU pGVCpu = &pGVM->aCpus[idCpu];
        Assert(pGVCpu->idHostCpu == NIL_RTCPUID);
        Assert(pGVCpu->iHostCpuSet == UINT32_MAX);
        pGVCpu->vmmr0.s.fInHmContext    = false;
        pGVCpu->vmmr0.s.pPreemptState   = NULL;
        pGVCpu->vmmr0.s.hCtxHook        = NIL_RTTHREADCTXHOOK;
    }
}


/**
 * Helper for vmmR0InitLoggers
 */
static int vmmR0InitLoggerOne(PGVMCPU pGVCpu, bool fRelease, PVMMR0PERVCPULOGGER pR0Log, PVMMR3CPULOGGER pShared,
                              uint32_t cbBuf, char *pchBuf, RTR3PTR pchBufR3)
{
    pR0Log->BufDesc.u32Magic    = RTLOGBUFFERDESC_MAGIC;
    pR0Log->BufDesc.uReserved   = 0;
    pR0Log->BufDesc.cbBuf       = cbBuf;
    pR0Log->BufDesc.offBuf      = 0;
    pR0Log->BufDesc.pchBuf      = pchBuf;
    pR0Log->BufDesc.pAux        = &pShared->AuxDesc;

    pShared->AuxDesc.fFlushedIndicator   = false;
    pShared->AuxDesc.afPadding[0]        = 0;
    pShared->AuxDesc.afPadding[1]        = 0;
    pShared->AuxDesc.afPadding[2]        = 0;
    pShared->AuxDesc.offBuf              = 0;
    pShared->pchBufR3                    = pchBufR3;
    pShared->cbBuf                       = cbBuf;

    static const char * const s_apszGroups[] = VBOX_LOGGROUP_NAMES;
    int rc = RTLogCreateEx(&pR0Log->pLogger, fRelease ? "VBOX_RELEASE_LOG" : "VBOX_LOG", RTLOG_F_NO_LOCKING | RTLOGFLAGS_BUFFERED,
                           "all", RT_ELEMENTS(s_apszGroups), s_apszGroups, UINT32_MAX,
                           fRelease ? vmmR0LogRelFlush : vmmR0LogFlush, 1 /*cBufDescs*/, &pR0Log->BufDesc,
                           RTLOGDEST_DUMMY,
                           NULL /*pfnPhase*/, 0 /*cHistory*/, 0 /*cbHistoryFileMax*/, 0 /*cSecsHistoryTimeSlot*/,
                           NULL /*pErrInfo*/, NULL /*pszFilenameFmt*/);
    if (RT_SUCCESS(rc))
    {
        PRTLOGGER pLogger = pR0Log->pLogger;
        pLogger->u32UserValue1 = VMMR0_LOGGER_FLAGS_MAGIC_VALUE;
        pLogger->u64UserValue2 = (uintptr_t)pGVCpu;
        pLogger->u64UserValue3 = (uintptr_t)pGVCpu;

        RTLogSetR0ThreadNameF(pLogger, "EMT-%u-R0", pGVCpu->idCpu);
    }
    else
        pR0Log->pLogger = NULL;
    return rc;
}


/**
 * Initializes one type of loggers for each EMT.
 */
static int vmmR0InitLoggers(PGVM pGVM, bool fRelease, uint32_t cbBuf, PRTR0MEMOBJ phMemObj, PRTR0MEMOBJ phMapObj)
{
    /* Allocate buffers first. */
    int rc = RTR0MemObjAllocPage(phMemObj, cbBuf * pGVM->cCpus, false /*fExecutable*/);
    if (RT_SUCCESS(rc))
    {
        rc = RTR0MemObjMapUser(phMapObj, *phMemObj, (RTR3PTR)-1, 0 /*uAlignment*/, RTMEM_PROT_READ, NIL_RTR0PROCESS);
        if (RT_SUCCESS(rc))
        {
            char  * const pchBuf   = (char *)RTR0MemObjAddress(*phMemObj);
            AssertPtrReturn(pchBuf, VERR_INTERNAL_ERROR_2);

            RTR3PTR const pchBufR3 = RTR0MemObjAddressR3(*phMapObj);
            AssertReturn(pchBufR3 != NIL_RTR3PTR, VERR_INTERNAL_ERROR_3);

            /* Initialize the per-CPU loggers. */
            for (uint32_t i = 0; i < pGVM->cCpus; i++)
            {
                PGVMCPU             pGVCpu  = &pGVM->aCpus[i];
                PVMMR0PERVCPULOGGER pR0Log  = fRelease ? &pGVCpu->vmmr0.s.RelLogger : &pGVCpu->vmmr0.s.Logger;
                PVMMR3CPULOGGER     pShared = fRelease ? &pGVCpu->vmm.s.RelLogger   : &pGVCpu->vmm.s.Logger;
                rc = vmmR0InitLoggerOne(pGVCpu, fRelease, pR0Log, pShared, cbBuf, pchBuf + i * cbBuf, pchBufR3 + i * cbBuf);
                if (RT_FAILURE(rc))
                {
                    pR0Log->pLogger   = NULL;
                    pShared->pchBufR3 = NIL_RTR3PTR;
                    while (i-- > 0)
                    {
                        pGVCpu  = &pGVM->aCpus[i];
                        pR0Log  = fRelease ? &pGVCpu->vmmr0.s.RelLogger : &pGVCpu->vmmr0.s.Logger;
                        pShared = fRelease ? &pGVCpu->vmm.s.RelLogger   : &pGVCpu->vmm.s.Logger;
                        RTLogDestroy(pR0Log->pLogger);
                        pR0Log->pLogger   = NULL;
                        pShared->pchBufR3 = NIL_RTR3PTR;
                    }
                    break;
                }
            }
            if (RT_SUCCESS(rc))
                return VINF_SUCCESS;

            /* Bail out. */
            RTR0MemObjFree(*phMapObj, false /*fFreeMappings*/);
            *phMapObj = NIL_RTR0MEMOBJ;
        }
        RTR0MemObjFree(*phMemObj, true /*fFreeMappings*/);
        *phMemObj = NIL_RTR0MEMOBJ;
    }
    return rc;
}


/**
 * Initiates the R0 driver for a particular VM instance.
 *
 * @returns VBox status code.
 *
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   uSvnRev     The SVN revision of the ring-3 part.
 * @param   uBuildType  Build type indicator.
 * @thread  EMT(0)
 */
static int vmmR0InitVM(PGVM pGVM, uint32_t uSvnRev, uint32_t uBuildType)
{
    VMM_CHECK_SMAP_SETUP();
    VMM_CHECK_SMAP_CHECK(return VERR_VMM_SMAP_BUT_AC_CLEAR);

    /*
     * Match the SVN revisions and build type.
     */
    if (uSvnRev != VMMGetSvnRev())
    {
        LogRel(("VMMR0InitVM: Revision mismatch, r3=%d r0=%d\n", uSvnRev, VMMGetSvnRev()));
        SUPR0Printf("VMMR0InitVM: Revision mismatch, r3=%d r0=%d\n", uSvnRev, VMMGetSvnRev());
        return VERR_VMM_R0_VERSION_MISMATCH;
    }
    if (uBuildType != vmmGetBuildType())
    {
        LogRel(("VMMR0InitVM: Build type mismatch, r3=%#x r0=%#x\n", uBuildType, vmmGetBuildType()));
        SUPR0Printf("VMMR0InitVM: Build type mismatch, r3=%#x r0=%#x\n", uBuildType, vmmGetBuildType());
        return VERR_VMM_R0_VERSION_MISMATCH;
    }

    int rc = GVMMR0ValidateGVMandEMT(pGVM, 0 /*idCpu*/);
    if (RT_FAILURE(rc))
        return rc;

    /* Don't allow this to be called more than once. */
    if (!pGVM->vmmr0.s.fCalledInitVm)
        pGVM->vmmr0.s.fCalledInitVm = true;
    else
        return VERR_ALREADY_INITIALIZED;

    /*
     * Create the ring-0 release loggers.
     */
    rc = vmmR0InitLoggers(pGVM, true /*fRelease*/, _8K, &pGVM->vmmr0.s.hMemObjReleaseLogger, &pGVM->vmmr0.s.hMapObjReleaseLogger);
    if (RT_FAILURE(rc))
        return rc;

#ifdef LOG_ENABLED
    /*
     * Create debug loggers.
     */
    rc = vmmR0InitLoggers(pGVM, false /*fRelease*/, _64K, &pGVM->vmmr0.s.hMemObjLogger, &pGVM->vmmr0.s.hMapObjLogger);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Register the EMT R0 logger instance for VCPU 0.
     */
    PVMCPUCC pVCpu = VMCC_GET_CPU_0(pGVM);
    if (pVCpu->vmmr0.s.Logger.pLogger)
    {
# if 0 /* testing of the logger. */
        LogCom(("vmmR0InitVM: before %p\n", RTLogDefaultInstance()));
        LogCom(("vmmR0InitVM: pfnFlush=%p actual=%p\n", pR0Logger->Logger.pfnFlush, vmmR0LoggerFlush));
        LogCom(("vmmR0InitVM: pfnLogger=%p actual=%p\n", pR0Logger->Logger.pfnLogger, vmmR0LoggerWrapper));
        LogCom(("vmmR0InitVM: offScratch=%d fFlags=%#x fDestFlags=%#x\n", pR0Logger->Logger.offScratch, pR0Logger->Logger.fFlags, pR0Logger->Logger.fDestFlags));

        RTLogSetDefaultInstanceThread(&pR0Logger->Logger, (uintptr_t)pGVM->pSession);
        LogCom(("vmmR0InitVM: after %p reg\n", RTLogDefaultInstance()));
        RTLogSetDefaultInstanceThread(NULL, pGVM->pSession);
        LogCom(("vmmR0InitVM: after %p dereg\n", RTLogDefaultInstance()));

        pR0Logger->Logger.pfnLogger("hello ring-0 logger\n");
        LogCom(("vmmR0InitVM: returned successfully from direct logger call.\n"));
        pR0Logger->Logger.pfnFlush(&pR0Logger->Logger);
        LogCom(("vmmR0InitVM: returned successfully from direct flush call.\n"));

        RTLogSetDefaultInstanceThread(&pR0Logger->Logger, (uintptr_t)pGVM->pSession);
        LogCom(("vmmR0InitVM: after %p reg2\n", RTLogDefaultInstance()));
        pR0Logger->Logger.pfnLogger("hello ring-0 logger\n");
        LogCom(("vmmR0InitVM: returned successfully from direct logger call (2). offScratch=%d\n", pR0Logger->Logger.offScratch));
        RTLogSetDefaultInstanceThread(NULL, pGVM->pSession);
        LogCom(("vmmR0InitVM: after %p dereg2\n", RTLogDefaultInstance()));

        RTLogLoggerEx(&pR0Logger->Logger, 0, ~0U, "hello ring-0 logger (RTLogLoggerEx)\n");
        LogCom(("vmmR0InitVM: RTLogLoggerEx returned fine offScratch=%d\n", pR0Logger->Logger.offScratch));

        RTLogSetDefaultInstanceThread(&pR0Logger->Logger, (uintptr_t)pGVM->pSession);
        RTLogPrintf("hello ring-0 logger (RTLogPrintf)\n");
        LogCom(("vmmR0InitVM: RTLogPrintf returned fine offScratch=%d\n", pR0Logger->Logger.offScratch));
# endif
# ifdef VBOX_WITH_R0_LOGGING
        Log(("Switching to per-thread logging instance %p (key=%p)\n", pVCpu->vmmr0.s.Logger.pLogger, pGVM->pSession));
        RTLogSetDefaultInstanceThread(pVCpu->vmmr0.s.Logger.pLogger, (uintptr_t)pGVM->pSession);
        pVCpu->vmmr0.s.Logger.fRegistered = true;
# endif
    }
#endif /* LOG_ENABLED */

    /*
     * Check if the host supports high resolution timers or not.
     */
    if (   pGVM->vmm.s.fUsePeriodicPreemptionTimers
        && !RTTimerCanDoHighResolution())
        pGVM->vmm.s.fUsePeriodicPreemptionTimers = false;

    /*
     * Initialize the per VM data for GVMM and GMM.
     */
    VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
    rc = GVMMR0InitVM(pGVM);
    if (RT_SUCCESS(rc))
    {
        /*
         * Init HM, CPUM and PGM (Darwin only).
         */
        VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
        rc = HMR0InitVM(pGVM);
        if (RT_SUCCESS(rc))
            VMM_CHECK_SMAP_CHECK2(pGVM, rc = VERR_VMM_RING0_ASSERTION); /* CPUR0InitVM will otherwise panic the host */
        if (RT_SUCCESS(rc))
        {
            rc = CPUMR0InitVM(pGVM);
            if (RT_SUCCESS(rc))
            {
                VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
                rc = PGMR0InitVM(pGVM);
                if (RT_SUCCESS(rc))
                {
                    VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
                    rc = EMR0InitVM(pGVM);
                    if (RT_SUCCESS(rc))
                    {
                        VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
#ifdef VBOX_WITH_PCI_PASSTHROUGH
                        rc = PciRawR0InitVM(pGVM);
#endif
                        if (RT_SUCCESS(rc))
                        {
                            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
                            rc = GIMR0InitVM(pGVM);
                            if (RT_SUCCESS(rc))
                            {
                                VMM_CHECK_SMAP_CHECK2(pGVM, rc = VERR_VMM_RING0_ASSERTION);
                                if (RT_SUCCESS(rc))
                                {
                                    GVMMR0DoneInitVM(pGVM);

                                    /*
                                     * Collect a bit of info for the VM release log.
                                     */
                                    pGVM->vmm.s.fIsPreemptPendingApiTrusty = RTThreadPreemptIsPendingTrusty();
                                    pGVM->vmm.s.fIsPreemptPossible         = RTThreadPreemptIsPossible();;

                                    VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
                                    return rc;
                                }

                                /* bail out*/
                                GIMR0TermVM(pGVM);
                            }
#ifdef VBOX_WITH_PCI_PASSTHROUGH
                            PciRawR0TermVM(pGVM);
#endif
                        }
                    }
                }
            }
            HMR0TermVM(pGVM);
        }
    }

    RTLogSetDefaultInstanceThread(NULL, (uintptr_t)pGVM->pSession);
    return rc;
}


/**
 * Does EMT specific VM initialization.
 *
 * @returns VBox status code.
 * @param   pGVM        The ring-0 VM structure.
 * @param   idCpu       The EMT that's calling.
 */
static int vmmR0InitVMEmt(PGVM pGVM, VMCPUID idCpu)
{
    /* Paranoia (caller checked these already). */
    AssertReturn(idCpu < pGVM->cCpus, VERR_INVALID_CPU_ID);
    AssertReturn(pGVM->aCpus[idCpu].hEMT == RTThreadNativeSelf(), VERR_INVALID_CPU_ID);

#if defined(LOG_ENABLED) && defined(VBOX_WITH_R0_LOGGING)
    /*
     * Registration of ring 0 loggers.
     */
    PVMCPUCC pVCpu = &pGVM->aCpus[idCpu];
    if (   pVCpu->vmmr0.s.Logger.pLogger
        && !pVCpu->vmmr0.s.Logger.fRegistered)
    {
        RTLogSetDefaultInstanceThread(pVCpu->vmmr0.s.Logger.pLogger, (uintptr_t)pGVM->pSession);
        pVCpu->vmmr0.s.Logger.fRegistered = true;
    }
#endif

    return VINF_SUCCESS;
}



/**
 * Terminates the R0 bits for a particular VM instance.
 *
 * This is normally called by ring-3 as part of the VM termination process, but
 * may alternatively be called during the support driver session cleanup when
 * the VM object is destroyed (see GVMM).
 *
 * @returns VBox status code.
 *
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   idCpu       Set to 0 if EMT(0) or NIL_VMCPUID if session cleanup
 *                      thread.
 * @thread  EMT(0) or session clean up thread.
 */
VMMR0_INT_DECL(int) VMMR0TermVM(PGVM pGVM, VMCPUID idCpu)
{
    /*
     * Check EMT(0) claim if we're called from userland.
     */
    if (idCpu != NIL_VMCPUID)
    {
        AssertReturn(idCpu == 0, VERR_INVALID_CPU_ID);
        int rc = GVMMR0ValidateGVMandEMT(pGVM, idCpu);
        if (RT_FAILURE(rc))
            return rc;
    }

#ifdef VBOX_WITH_PCI_PASSTHROUGH
    PciRawR0TermVM(pGVM);
#endif

    /*
     * Tell GVMM what we're up to and check that we only do this once.
     */
    if (GVMMR0DoingTermVM(pGVM))
    {
        GIMR0TermVM(pGVM);

        /** @todo I wish to call PGMR0PhysFlushHandyPages(pGVM, &pGVM->aCpus[idCpu])
         *        here to make sure we don't leak any shared pages if we crash... */
#ifdef VBOX_WITH_2X_4GB_ADDR_SPACE
        PGMR0DynMapTermVM(pGVM);
#endif
        HMR0TermVM(pGVM);
    }

    /*
     * Deregister the logger for this EMT.
     */
    RTLogSetDefaultInstanceThread(NULL, (uintptr_t)pGVM->pSession);
    return VINF_SUCCESS;
}


/**
 * This is called at the end of gvmmR0CleanupVM().
 *
 * @param   pGVM        The global (ring-0) VM structure.
 */
VMMR0_INT_DECL(void) VMMR0CleanupVM(PGVM pGVM)
{
    AssertCompile(NIL_RTTHREADCTXHOOK == (RTTHREADCTXHOOK)0); /* Depends on zero initialized memory working for NIL at the moment. */
    for (VMCPUID idCpu = 0; idCpu < pGVM->cCpus; idCpu++)
    {
        PGVMCPU pGVCpu = &pGVM->aCpus[idCpu];

        /** @todo Can we busy wait here for all thread-context hooks to be
         *        deregistered before releasing (destroying) it? Only until we find a
         *        solution for not deregistering hooks everytime we're leaving HMR0
         *        context. */
        VMMR0ThreadCtxHookDestroyForEmt(pGVCpu);

        /* Destroy the release logger. */
        RTLogDestroy(pGVCpu->vmmr0.s.RelLogger.pLogger);
        pGVCpu->vmmr0.s.RelLogger.pLogger = NULL;
        pGVCpu->vmm.s.RelLogger.pchBufR3  = NIL_RTR3PTR;

        /* Destroy the regular logger. */
        RTLogDestroy(pGVCpu->vmmr0.s.Logger.pLogger);
        pGVCpu->vmmr0.s.Logger.pLogger = NULL;
        pGVCpu->vmm.s.Logger.pchBufR3  = NIL_RTR3PTR;
    }

    /*
     * Free logger buffer memory.
     */
    RTR0MemObjFree(pGVM->vmmr0.s.hMapObjReleaseLogger, false /*fFreeMappings*/);
    pGVM->vmmr0.s.hMapObjReleaseLogger = NIL_RTR0MEMOBJ;
    RTR0MemObjFree(pGVM->vmmr0.s.hMemObjReleaseLogger, true /*fFreeMappings*/);
    pGVM->vmmr0.s.hMemObjReleaseLogger = NIL_RTR0MEMOBJ;

    RTR0MemObjFree(pGVM->vmmr0.s.hMapObjLogger, false /*fFreeMappings*/);
    pGVM->vmmr0.s.hMapObjLogger = NIL_RTR0MEMOBJ;
    RTR0MemObjFree(pGVM->vmmr0.s.hMemObjLogger, true /*fFreeMappings*/);
    pGVM->vmmr0.s.hMemObjLogger = NIL_RTR0MEMOBJ;
}


/**
 * An interrupt or unhalt force flag is set, deal with it.
 *
 * @returns VINF_SUCCESS (or VINF_EM_HALT).
 * @param   pVCpu                   The cross context virtual CPU structure.
 * @param   uMWait                  Result from EMMonitorWaitIsActive().
 * @param   enmInterruptibility     Guest CPU interruptbility level.
 */
static int vmmR0DoHaltInterrupt(PVMCPUCC pVCpu, unsigned uMWait, CPUMINTERRUPTIBILITY enmInterruptibility)
{
    Assert(!TRPMHasTrap(pVCpu));
    Assert(   enmInterruptibility > CPUMINTERRUPTIBILITY_INVALID
           && enmInterruptibility < CPUMINTERRUPTIBILITY_END);

    /*
     * Pending interrupts w/o any SMIs or NMIs?  That the usual case.
     */
    if (    VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_INTERRUPT_PIC)
        && !VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_INTERRUPT_SMI  | VMCPU_FF_INTERRUPT_NMI))
    {
        if (enmInterruptibility <= CPUMINTERRUPTIBILITY_UNRESTRAINED)
        {
            uint8_t u8Interrupt = 0;
            int rc = PDMGetInterrupt(pVCpu, &u8Interrupt);
            Log(("vmmR0DoHaltInterrupt: CPU%d u8Interrupt=%d (%#x) rc=%Rrc\n", pVCpu->idCpu, u8Interrupt, u8Interrupt, rc));
            if (RT_SUCCESS(rc))
            {
                VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_UNHALT);

                rc = TRPMAssertTrap(pVCpu, u8Interrupt, TRPM_HARDWARE_INT);
                AssertRCSuccess(rc);
                STAM_REL_COUNTER_INC(&pVCpu->vmm.s.StatR0HaltExec);
                return rc;
            }
        }
    }
    /*
     * SMI is not implemented yet, at least not here.
     */
    else if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_SMI))
    {
        Log12(("vmmR0DoHaltInterrupt: CPU%d failed #3\n", pVCpu->idCpu));
        STAM_REL_COUNTER_INC(&pVCpu->vmm.s.StatR0HaltToR3);
        return VINF_EM_HALT;
    }
    /*
     * NMI.
     */
    else if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_NMI))
    {
        if (enmInterruptibility < CPUMINTERRUPTIBILITY_NMI_INHIBIT)
        {
            /** @todo later. */
            Log12(("vmmR0DoHaltInterrupt: CPU%d failed #2 (uMWait=%u enmInt=%d)\n", pVCpu->idCpu, uMWait, enmInterruptibility));
            STAM_REL_COUNTER_INC(&pVCpu->vmm.s.StatR0HaltToR3);
            return VINF_EM_HALT;
        }
    }
    /*
     * Nested-guest virtual interrupt.
     */
    else if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_NESTED_GUEST))
    {
        if (enmInterruptibility < CPUMINTERRUPTIBILITY_VIRT_INT_DISABLED)
        {
            /** @todo NSTVMX: NSTSVM: Remember, we might have to check and perform VM-exits
             *        here before injecting the virtual interrupt. See emR3ForcedActions
             *        for details. */
            Log12(("vmmR0DoHaltInterrupt: CPU%d failed #1 (uMWait=%u enmInt=%d)\n", pVCpu->idCpu, uMWait, enmInterruptibility));
            STAM_REL_COUNTER_INC(&pVCpu->vmm.s.StatR0HaltToR3);
            return VINF_EM_HALT;
        }
    }

    if (VMCPU_FF_TEST_AND_CLEAR(pVCpu, VMCPU_FF_UNHALT))
    {
        STAM_REL_COUNTER_INC(&pVCpu->vmm.s.StatR0HaltExec);
        Log11(("vmmR0DoHaltInterrupt: CPU%d success VINF_SUCCESS (UNHALT)\n", pVCpu->idCpu));
        return VINF_SUCCESS;
    }
    if (uMWait > 1)
    {
        STAM_REL_COUNTER_INC(&pVCpu->vmm.s.StatR0HaltExec);
        Log11(("vmmR0DoHaltInterrupt: CPU%d success VINF_SUCCESS (uMWait=%u > 1)\n", pVCpu->idCpu, uMWait));
        return VINF_SUCCESS;
    }

    Log12(("vmmR0DoHaltInterrupt: CPU%d failed #0 (uMWait=%u enmInt=%d)\n", pVCpu->idCpu, uMWait, enmInterruptibility));
    STAM_REL_COUNTER_INC(&pVCpu->vmm.s.StatR0HaltToR3);
    return VINF_EM_HALT;
}


/**
 * This does one round of vmR3HaltGlobal1Halt().
 *
 * The rational here is that we'll reduce latency in interrupt situations if we
 * don't go to ring-3 immediately on a VINF_EM_HALT (guest executed HLT or
 * MWAIT), but do one round of blocking here instead and hope the interrupt is
 * raised in the meanwhile.
 *
 * If we go to ring-3 we'll quit the inner HM/NEM loop in EM and end up in the
 * outer loop, which will then call VMR3WaitHalted() and that in turn will do a
 * ring-0 call (unless we're too close to a timer event).  When the interrupt
 * wakes us up, we'll return from ring-0 and EM will by instinct do a
 * rescheduling (because of raw-mode) before it resumes the HM/NEM loop and gets
 * back to VMMR0EntryFast().
 *
 * @returns VINF_SUCCESS or VINF_EM_HALT.
 * @param   pGVM        The ring-0 VM structure.
 * @param   pGVCpu      The ring-0 virtual CPU structure.
 *
 * @todo r=bird: All the blocking/waiting and EMT managment should move out of
 *       the VM module, probably to VMM.  Then this would be more weird wrt
 *       parameters and statistics.
 */
static int vmmR0DoHalt(PGVM pGVM, PGVMCPU pGVCpu)
{
    /*
     * Do spin stat historization.
     */
    if (++pGVCpu->vmm.s.cR0Halts & 0xff)
    { /* likely */ }
    else if (pGVCpu->vmm.s.cR0HaltsSucceeded > pGVCpu->vmm.s.cR0HaltsToRing3)
    {
        pGVCpu->vmm.s.cR0HaltsSucceeded = 2;
        pGVCpu->vmm.s.cR0HaltsToRing3   = 0;
    }
    else
    {
        pGVCpu->vmm.s.cR0HaltsSucceeded = 0;
        pGVCpu->vmm.s.cR0HaltsToRing3   = 2;
    }

    /*
     * Flags that makes us go to ring-3.
     */
    uint32_t const fVmFFs  = VM_FF_TM_VIRTUAL_SYNC            | VM_FF_PDM_QUEUES              | VM_FF_PDM_DMA
                           | VM_FF_DBGF                       | VM_FF_REQUEST                 | VM_FF_CHECK_VM_STATE
                           | VM_FF_RESET                      | VM_FF_EMT_RENDEZVOUS          | VM_FF_PGM_NEED_HANDY_PAGES
                           | VM_FF_PGM_NO_MEMORY              | VM_FF_DEBUG_SUSPEND;
    uint64_t const fCpuFFs = VMCPU_FF_TIMER                   | VMCPU_FF_PDM_CRITSECT         | VMCPU_FF_IEM
                           | VMCPU_FF_REQUEST                 | VMCPU_FF_DBGF                 | VMCPU_FF_HM_UPDATE_CR3
                           | VMCPU_FF_HM_UPDATE_PAE_PDPES     | VMCPU_FF_PGM_SYNC_CR3         | VMCPU_FF_PGM_SYNC_CR3_NON_GLOBAL
                           | VMCPU_FF_TO_R3                   | VMCPU_FF_IOM;

    /*
     * Check preconditions.
     */
    unsigned const             uMWait              = EMMonitorWaitIsActive(pGVCpu);
    CPUMINTERRUPTIBILITY const enmInterruptibility = CPUMGetGuestInterruptibility(pGVCpu);
    if (   pGVCpu->vmm.s.fMayHaltInRing0
        && !TRPMHasTrap(pGVCpu)
        && (   enmInterruptibility == CPUMINTERRUPTIBILITY_UNRESTRAINED
            || uMWait > 1))
    {
        if (   !VM_FF_IS_ANY_SET(pGVM, fVmFFs)
            && !VMCPU_FF_IS_ANY_SET(pGVCpu, fCpuFFs))
        {
            /*
             * Interrupts pending already?
             */
            if (VMCPU_FF_TEST_AND_CLEAR(pGVCpu, VMCPU_FF_UPDATE_APIC))
                APICUpdatePendingInterrupts(pGVCpu);

            /*
             * Flags that wake up from the halted state.
             */
            uint64_t const fIntMask = VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_INTERRUPT_PIC | VMCPU_FF_INTERRUPT_NESTED_GUEST
                                    | VMCPU_FF_INTERRUPT_NMI  | VMCPU_FF_INTERRUPT_SMI | VMCPU_FF_UNHALT;

            if (VMCPU_FF_IS_ANY_SET(pGVCpu, fIntMask))
                return vmmR0DoHaltInterrupt(pGVCpu, uMWait, enmInterruptibility);
            ASMNopPause();

            /*
             * Check out how long till the next timer event.
             */
            uint64_t u64Delta;
            uint64_t u64GipTime = TMTimerPollGIP(pGVM, pGVCpu, &u64Delta);

            if (   !VM_FF_IS_ANY_SET(pGVM, fVmFFs)
                && !VMCPU_FF_IS_ANY_SET(pGVCpu, fCpuFFs))
            {
                if (VMCPU_FF_TEST_AND_CLEAR(pGVCpu, VMCPU_FF_UPDATE_APIC))
                    APICUpdatePendingInterrupts(pGVCpu);

                if (VMCPU_FF_IS_ANY_SET(pGVCpu, fIntMask))
                    return vmmR0DoHaltInterrupt(pGVCpu, uMWait, enmInterruptibility);

                /*
                 * Wait if there is enough time to the next timer event.
                 */
                if (u64Delta >= pGVCpu->vmm.s.cNsSpinBlockThreshold)
                {
                    /* If there are few other CPU cores around, we will procrastinate a
                       little before going to sleep, hoping for some device raising an
                       interrupt or similar.   Though, the best thing here would be to
                       dynamically adjust the spin count according to its usfulness or
                       something... */
                    if (   pGVCpu->vmm.s.cR0HaltsSucceeded > pGVCpu->vmm.s.cR0HaltsToRing3
                        && RTMpGetOnlineCount() >= 4)
                    {
                        /** @todo Figure out how we can skip this if it hasn't help recently...
                         *        @bugref{9172#c12} */
                        uint32_t cSpinLoops = 42;
                        while (cSpinLoops-- > 0)
                        {
                            ASMNopPause();
                            if (VMCPU_FF_TEST_AND_CLEAR(pGVCpu, VMCPU_FF_UPDATE_APIC))
                                APICUpdatePendingInterrupts(pGVCpu);
                            ASMNopPause();
                            if (VM_FF_IS_ANY_SET(pGVM, fVmFFs))
                            {
                                STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3FromSpin);
                                STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3);
                                return VINF_EM_HALT;
                            }
                            ASMNopPause();
                            if (VMCPU_FF_IS_ANY_SET(pGVCpu, fCpuFFs))
                            {
                                STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3FromSpin);
                                STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3);
                                return VINF_EM_HALT;
                            }
                            ASMNopPause();
                            if (VMCPU_FF_IS_ANY_SET(pGVCpu, fIntMask))
                            {
                                STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltExecFromSpin);
                                return vmmR0DoHaltInterrupt(pGVCpu, uMWait, enmInterruptibility);
                            }
                            ASMNopPause();
                        }
                    }

                    /*
                     * We have to set the state to VMCPUSTATE_STARTED_HALTED here so ring-3
                     * knows when to notify us (cannot access VMINTUSERPERVMCPU::fWait from here).
                     * After changing the state we must recheck the force flags of course.
                     */
                    if (VMCPU_CMPXCHG_STATE(pGVCpu, VMCPUSTATE_STARTED_HALTED, VMCPUSTATE_STARTED))
                    {
                        if (   !VM_FF_IS_ANY_SET(pGVM, fVmFFs)
                            && !VMCPU_FF_IS_ANY_SET(pGVCpu, fCpuFFs))
                        {
                            if (VMCPU_FF_TEST_AND_CLEAR(pGVCpu, VMCPU_FF_UPDATE_APIC))
                                APICUpdatePendingInterrupts(pGVCpu);

                            if (VMCPU_FF_IS_ANY_SET(pGVCpu, fIntMask))
                            {
                                VMCPU_CMPXCHG_STATE(pGVCpu, VMCPUSTATE_STARTED, VMCPUSTATE_STARTED_HALTED);
                                return vmmR0DoHaltInterrupt(pGVCpu, uMWait, enmInterruptibility);
                            }

                            /* Okay, block! */
                            uint64_t const u64StartSchedHalt   = RTTimeNanoTS();
                            int rc = GVMMR0SchedHalt(pGVM, pGVCpu, u64GipTime);
                            uint64_t const u64EndSchedHalt     = RTTimeNanoTS();
                            uint64_t const cNsElapsedSchedHalt = u64EndSchedHalt - u64StartSchedHalt;
                            Log10(("vmmR0DoHalt: CPU%d: halted %llu ns\n", pGVCpu->idCpu, cNsElapsedSchedHalt));

                            VMCPU_CMPXCHG_STATE(pGVCpu, VMCPUSTATE_STARTED, VMCPUSTATE_STARTED_HALTED);
                            STAM_REL_PROFILE_ADD_PERIOD(&pGVCpu->vmm.s.StatR0HaltBlock, cNsElapsedSchedHalt);
                            if (   rc == VINF_SUCCESS
                                || rc == VERR_INTERRUPTED)
                            {
                                /* Keep some stats like ring-3 does. */
                                int64_t const cNsOverslept = u64EndSchedHalt - u64GipTime;
                                if (cNsOverslept > 50000)
                                    STAM_REL_PROFILE_ADD_PERIOD(&pGVCpu->vmm.s.StatR0HaltBlockOverslept, cNsOverslept);
                                else if (cNsOverslept < -50000)
                                    STAM_REL_PROFILE_ADD_PERIOD(&pGVCpu->vmm.s.StatR0HaltBlockInsomnia,  cNsElapsedSchedHalt);
                                else
                                    STAM_REL_PROFILE_ADD_PERIOD(&pGVCpu->vmm.s.StatR0HaltBlockOnTime,    cNsElapsedSchedHalt);

                                /*
                                 * Recheck whether we can resume execution or have to go to ring-3.
                                 */
                                if (   !VM_FF_IS_ANY_SET(pGVM, fVmFFs)
                                    && !VMCPU_FF_IS_ANY_SET(pGVCpu, fCpuFFs))
                                {
                                    if (VMCPU_FF_TEST_AND_CLEAR(pGVCpu, VMCPU_FF_UPDATE_APIC))
                                        APICUpdatePendingInterrupts(pGVCpu);
                                    if (VMCPU_FF_IS_ANY_SET(pGVCpu, fIntMask))
                                    {
                                        STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltExecFromBlock);
                                        return vmmR0DoHaltInterrupt(pGVCpu, uMWait, enmInterruptibility);
                                    }
                                    STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3PostNoInt);
                                    Log12(("vmmR0DoHalt: CPU%d post #2 - No pending interrupt\n", pGVCpu->idCpu));
                                }
                                else
                                {
                                    STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3PostPendingFF);
                                    Log12(("vmmR0DoHalt: CPU%d post #1 - Pending FF\n", pGVCpu->idCpu));
                                }
                            }
                            else
                            {
                                STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3Other);
                                Log12(("vmmR0DoHalt: CPU%d GVMMR0SchedHalt failed: %Rrc\n", pGVCpu->idCpu, rc));
                            }
                        }
                        else
                        {
                            VMCPU_CMPXCHG_STATE(pGVCpu, VMCPUSTATE_STARTED, VMCPUSTATE_STARTED_HALTED);
                            STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3PendingFF);
                            Log12(("vmmR0DoHalt: CPU%d failed #5 - Pending FF\n", pGVCpu->idCpu));
                        }
                    }
                    else
                    {
                        STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3Other);
                        Log12(("vmmR0DoHalt: CPU%d failed #4 - enmState=%d\n", pGVCpu->idCpu, VMCPU_GET_STATE(pGVCpu)));
                    }
                }
                else
                {
                    STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3SmallDelta);
                    Log12(("vmmR0DoHalt: CPU%d failed #3 - delta too small: %RU64\n", pGVCpu->idCpu, u64Delta));
                }
            }
            else
            {
                STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3PendingFF);
                Log12(("vmmR0DoHalt: CPU%d failed #2 - Pending FF\n", pGVCpu->idCpu));
            }
        }
        else
        {
            STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3PendingFF);
            Log12(("vmmR0DoHalt: CPU%d failed #1 - Pending FF\n", pGVCpu->idCpu));
        }
    }
    else
    {
        STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3Other);
        Log12(("vmmR0DoHalt: CPU%d failed #0 - fMayHaltInRing0=%d TRPMHasTrap=%d enmInt=%d uMWait=%u\n",
               pGVCpu->idCpu, pGVCpu->vmm.s.fMayHaltInRing0, TRPMHasTrap(pGVCpu), enmInterruptibility, uMWait));
    }

    STAM_REL_COUNTER_INC(&pGVCpu->vmm.s.StatR0HaltToR3);
    return VINF_EM_HALT;
}


/**
 * VMM ring-0 thread-context callback.
 *
 * This does common HM state updating and calls the HM-specific thread-context
 * callback.
 *
 * This is used together with RTThreadCtxHookCreate() on platforms which
 * supports it, and directly from VMMR0EmtPrepareForBlocking() and
 * VMMR0EmtResumeAfterBlocking() on platforms which don't.
 *
 * @param   enmEvent    The thread-context event.
 * @param   pvUser      Opaque pointer to the VMCPU.
 *
 * @thread  EMT(pvUser)
 */
static DECLCALLBACK(void) vmmR0ThreadCtxCallback(RTTHREADCTXEVENT enmEvent, void *pvUser)
{
    PVMCPUCC pVCpu = (PVMCPUCC)pvUser;

    switch (enmEvent)
    {
        case RTTHREADCTXEVENT_IN:
        {
            /*
             * Linux may call us with preemption enabled (really!) but technically we
             * cannot get preempted here, otherwise we end up in an infinite recursion
             * scenario (i.e. preempted in resume hook -> preempt hook -> resume hook...
             * ad infinitum). Let's just disable preemption for now...
             */
            /** @todo r=bird: I don't believe the above. The linux code is clearly enabling
             *        preemption after doing the callout (one or two functions up the
             *        call chain). */
            /** @todo r=ramshankar: See @bugref{5313#c30}. */
            RTTHREADPREEMPTSTATE ParanoidPreemptState = RTTHREADPREEMPTSTATE_INITIALIZER;
            RTThreadPreemptDisable(&ParanoidPreemptState);

            /* We need to update the VCPU <-> host CPU mapping. */
            RTCPUID idHostCpu;
            uint32_t iHostCpuSet = RTMpCurSetIndexAndId(&idHostCpu);
            pVCpu->iHostCpuSet = iHostCpuSet;
            ASMAtomicWriteU32(&pVCpu->idHostCpu, idHostCpu);

            /* In the very unlikely event that the GIP delta for the CPU we're
               rescheduled needs calculating, try force a return to ring-3.
               We unfortunately cannot do the measurements right here. */
            if (RT_LIKELY(!SUPIsTscDeltaAvailableForCpuSetIndex(iHostCpuSet)))
            { /* likely */ }
            else
                VMCPU_FF_SET(pVCpu, VMCPU_FF_TO_R3);

            /* Invoke the HM-specific thread-context callback. */
            HMR0ThreadCtxCallback(enmEvent, pvUser);

            /* Restore preemption. */
            RTThreadPreemptRestore(&ParanoidPreemptState);
            break;
        }

        case RTTHREADCTXEVENT_OUT:
        {
            /* Invoke the HM-specific thread-context callback. */
            HMR0ThreadCtxCallback(enmEvent, pvUser);

            /*
             * Sigh. See VMMGetCpu() used by VMCPU_ASSERT_EMT(). We cannot let several VCPUs
             * have the same host CPU associated with it.
             */
            pVCpu->iHostCpuSet = UINT32_MAX;
            ASMAtomicWriteU32(&pVCpu->idHostCpu, NIL_RTCPUID);
            break;
        }

        default:
            /* Invoke the HM-specific thread-context callback. */
            HMR0ThreadCtxCallback(enmEvent, pvUser);
            break;
    }
}


/**
 * Creates thread switching hook for the current EMT thread.
 *
 * This is called by GVMMR0CreateVM and GVMMR0RegisterVCpu.  If the host
 * platform does not implement switcher hooks, no hooks will be create and the
 * member set to NIL_RTTHREADCTXHOOK.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @thread  EMT(pVCpu)
 */
VMMR0_INT_DECL(int) VMMR0ThreadCtxHookCreateForEmt(PVMCPUCC pVCpu)
{
    VMCPU_ASSERT_EMT(pVCpu);
    Assert(pVCpu->vmmr0.s.hCtxHook == NIL_RTTHREADCTXHOOK);

#if 1 /* To disable this stuff change to zero. */
    int rc = RTThreadCtxHookCreate(&pVCpu->vmmr0.s.hCtxHook, 0, vmmR0ThreadCtxCallback, pVCpu);
    if (RT_SUCCESS(rc))
    {
        pVCpu->pGVM->vmm.s.fIsUsingContextHooks = true;
        return rc;
    }
#else
    RT_NOREF(vmmR0ThreadCtxCallback);
    int rc = VERR_NOT_SUPPORTED;
#endif

    pVCpu->vmmr0.s.hCtxHook = NIL_RTTHREADCTXHOOK;
    pVCpu->pGVM->vmm.s.fIsUsingContextHooks = false;
    if (rc == VERR_NOT_SUPPORTED)
        return VINF_SUCCESS;

    LogRelMax(32, ("RTThreadCtxHookCreate failed! rc=%Rrc pVCpu=%p idCpu=%RU32\n", rc, pVCpu, pVCpu->idCpu));
    return VINF_SUCCESS; /* Just ignore it, we can live without context hooks. */
}


/**
 * Destroys the thread switching hook for the specified VCPU.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @remarks Can be called from any thread.
 */
VMMR0_INT_DECL(void) VMMR0ThreadCtxHookDestroyForEmt(PVMCPUCC pVCpu)
{
    int rc = RTThreadCtxHookDestroy(pVCpu->vmmr0.s.hCtxHook);
    AssertRC(rc);
    pVCpu->vmmr0.s.hCtxHook = NIL_RTTHREADCTXHOOK;
}


/**
 * Disables the thread switching hook for this VCPU (if we got one).
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @thread  EMT(pVCpu)
 *
 * @remarks This also clears GVMCPU::idHostCpu, so the mapping is invalid after
 *          this call.  This means you have to be careful with what you do!
 */
VMMR0_INT_DECL(void) VMMR0ThreadCtxHookDisable(PVMCPUCC pVCpu)
{
    /*
     * Clear the VCPU <-> host CPU mapping as we've left HM context.
     * @bugref{7726#c19} explains the need for this trick:
     *
     *      VMXR0CallRing3Callback/SVMR0CallRing3Callback &
     *      hmR0VmxLeaveSession/hmR0SvmLeaveSession disables context hooks during
     *      longjmp & normal return to ring-3, which opens a window where we may be
     *      rescheduled without changing GVMCPUID::idHostCpu and cause confusion if
     *      the CPU starts executing a different EMT.  Both functions first disables
     *      preemption and then calls HMR0LeaveCpu which invalids idHostCpu, leaving
     *      an opening for getting preempted.
     */
    /** @todo Make HM not need this API!  Then we could leave the hooks enabled
     *        all the time. */

    /*
     * Disable the context hook, if we got one.
     */
    if (pVCpu->vmmr0.s.hCtxHook != NIL_RTTHREADCTXHOOK)
    {
        Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));
        ASMAtomicWriteU32(&pVCpu->idHostCpu, NIL_RTCPUID);
        int rc = RTThreadCtxHookDisable(pVCpu->vmmr0.s.hCtxHook);
        AssertRC(rc);
    }
}


/**
 * Internal version of VMMR0ThreadCtxHooksAreRegistered.
 *
 * @returns true if registered, false otherwise.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
DECLINLINE(bool) vmmR0ThreadCtxHookIsEnabled(PVMCPUCC pVCpu)
{
    return RTThreadCtxHookIsEnabled(pVCpu->vmmr0.s.hCtxHook);
}


/**
 * Whether thread-context hooks are registered for this VCPU.
 *
 * @returns true if registered, false otherwise.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMR0_INT_DECL(bool) VMMR0ThreadCtxHookIsEnabled(PVMCPUCC pVCpu)
{
    return vmmR0ThreadCtxHookIsEnabled(pVCpu);
}


/**
 * Returns the ring-0 release logger instance.
 *
 * @returns Pointer to release logger, NULL if not configured.
 * @param   pVCpu       The cross context virtual CPU structure of the caller.
 * @thread  EMT(pVCpu)
 */
VMMR0_INT_DECL(PRTLOGGER) VMMR0GetReleaseLogger(PVMCPUCC pVCpu)
{
    return pVCpu->vmmr0.s.RelLogger.pLogger;
}


#ifdef VBOX_WITH_STATISTICS
/**
 * Record return code statistics
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   rc          The status code.
 */
static void vmmR0RecordRC(PVMCC pVM, PVMCPUCC pVCpu, int rc)
{
    /*
     * Collect statistics.
     */
    switch (rc)
    {
        case VINF_SUCCESS:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetNormal);
            break;
        case VINF_EM_RAW_INTERRUPT:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetInterrupt);
            break;
        case VINF_EM_RAW_INTERRUPT_HYPER:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetInterruptHyper);
            break;
        case VINF_EM_RAW_GUEST_TRAP:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetGuestTrap);
            break;
        case VINF_EM_RAW_RING_SWITCH:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetRingSwitch);
            break;
        case VINF_EM_RAW_RING_SWITCH_INT:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetRingSwitchInt);
            break;
        case VINF_EM_RAW_STALE_SELECTOR:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetStaleSelector);
            break;
        case VINF_EM_RAW_IRET_TRAP:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetIRETTrap);
            break;
        case VINF_IOM_R3_IOPORT_READ:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetIORead);
            break;
        case VINF_IOM_R3_IOPORT_WRITE:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetIOWrite);
            break;
        case VINF_IOM_R3_IOPORT_COMMIT_WRITE:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetIOCommitWrite);
            break;
        case VINF_IOM_R3_MMIO_READ:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetMMIORead);
            break;
        case VINF_IOM_R3_MMIO_WRITE:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetMMIOWrite);
            break;
        case VINF_IOM_R3_MMIO_COMMIT_WRITE:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetMMIOCommitWrite);
            break;
        case VINF_IOM_R3_MMIO_READ_WRITE:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetMMIOReadWrite);
            break;
        case VINF_PATM_HC_MMIO_PATCH_READ:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetMMIOPatchRead);
            break;
        case VINF_PATM_HC_MMIO_PATCH_WRITE:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetMMIOPatchWrite);
            break;
        case VINF_CPUM_R3_MSR_READ:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetMSRRead);
            break;
        case VINF_CPUM_R3_MSR_WRITE:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetMSRWrite);
            break;
        case VINF_EM_RAW_EMULATE_INSTR:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetEmulate);
            break;
        case VINF_PATCH_EMULATE_INSTR:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetPatchEmulate);
            break;
        case VINF_EM_RAW_EMULATE_INSTR_LDT_FAULT:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetLDTFault);
            break;
        case VINF_EM_RAW_EMULATE_INSTR_GDT_FAULT:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetGDTFault);
            break;
        case VINF_EM_RAW_EMULATE_INSTR_IDT_FAULT:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetIDTFault);
            break;
        case VINF_EM_RAW_EMULATE_INSTR_TSS_FAULT:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetTSSFault);
            break;
        case VINF_CSAM_PENDING_ACTION:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetCSAMTask);
            break;
        case VINF_PGM_SYNC_CR3:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetSyncCR3);
            break;
        case VINF_PATM_PATCH_INT3:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetPatchInt3);
            break;
        case VINF_PATM_PATCH_TRAP_PF:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetPatchPF);
            break;
        case VINF_PATM_PATCH_TRAP_GP:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetPatchGP);
            break;
        case VINF_PATM_PENDING_IRQ_AFTER_IRET:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetPatchIretIRQ);
            break;
        case VINF_EM_RESCHEDULE_REM:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetRescheduleREM);
            break;
        case VINF_EM_RAW_TO_R3:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetToR3Total);
            if (VM_FF_IS_SET(pVM, VM_FF_TM_VIRTUAL_SYNC))
                STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetToR3TMVirt);
            else if (VM_FF_IS_SET(pVM, VM_FF_PGM_NEED_HANDY_PAGES))
                STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetToR3HandyPages);
            else if (VM_FF_IS_SET(pVM, VM_FF_PDM_QUEUES))
                STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetToR3PDMQueues);
            else if (VM_FF_IS_SET(pVM, VM_FF_EMT_RENDEZVOUS))
                STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetToR3Rendezvous);
            else if (VM_FF_IS_SET(pVM, VM_FF_PDM_DMA))
                STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetToR3DMA);
            else if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_TIMER))
                STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetToR3Timer);
            else if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_PDM_CRITSECT))
                STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetToR3CritSect);
            else if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_TO_R3))
                STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetToR3FF);
            else if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_IEM))
                STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetToR3Iem);
            else if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_IOM))
                STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetToR3Iom);
            else
                STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetToR3Unknown);
            break;

        case VINF_EM_RAW_TIMER_PENDING:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetTimerPending);
            break;
        case VINF_EM_RAW_INTERRUPT_PENDING:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetInterruptPending);
            break;
        case VINF_VMM_CALL_HOST:
            switch (pVCpu->vmm.s.enmCallRing3Operation)
            {
                case VMMCALLRING3_PDM_CRIT_SECT_ENTER:
                    STAM_COUNTER_INC(&pVM->vmm.s.StatRZCallPDMCritSectEnter);
                    break;
                case VMMCALLRING3_PDM_LOCK:
                    STAM_COUNTER_INC(&pVM->vmm.s.StatRZCallPDMLock);
                    break;
                case VMMCALLRING3_PGM_POOL_GROW:
                    STAM_COUNTER_INC(&pVM->vmm.s.StatRZCallPGMPoolGrow);
                    break;
                case VMMCALLRING3_PGM_LOCK:
                    STAM_COUNTER_INC(&pVM->vmm.s.StatRZCallPGMLock);
                    break;
                case VMMCALLRING3_PGM_MAP_CHUNK:
                    STAM_COUNTER_INC(&pVM->vmm.s.StatRZCallPGMMapChunk);
                    break;
                case VMMCALLRING3_PGM_ALLOCATE_HANDY_PAGES:
                    STAM_COUNTER_INC(&pVM->vmm.s.StatRZCallPGMAllocHandy);
                    break;
                case VMMCALLRING3_VMM_LOGGER_FLUSH:
                    STAM_COUNTER_INC(&pVM->vmm.s.StatRZCallLogFlush);
                    break;
                case VMMCALLRING3_VM_SET_ERROR:
                    STAM_COUNTER_INC(&pVM->vmm.s.StatRZCallVMSetError);
                    break;
                case VMMCALLRING3_VM_SET_RUNTIME_ERROR:
                    STAM_COUNTER_INC(&pVM->vmm.s.StatRZCallVMSetRuntimeError);
                    break;
                case VMMCALLRING3_VM_R0_ASSERTION:
                default:
                    STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetCallRing3);
                    break;
            }
            break;
        case VINF_PATM_DUPLICATE_FUNCTION:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetPATMDuplicateFn);
            break;
        case VINF_PGM_CHANGE_MODE:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetPGMChangeMode);
            break;
        case VINF_PGM_POOL_FLUSH_PENDING:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetPGMFlushPending);
            break;
        case VINF_EM_PENDING_REQUEST:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetPendingRequest);
            break;
        case VINF_EM_HM_PATCH_TPR_INSTR:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetPatchTPR);
            break;
        default:
            STAM_COUNTER_INC(&pVM->vmm.s.StatRZRetMisc);
            break;
    }
}
#endif /* VBOX_WITH_STATISTICS */


/**
 * The Ring 0 entry point, called by the fast-ioctl path.
 *
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   pVMIgnored      The cross context VM structure. The return code is
 *                          stored in pVM->vmm.s.iLastGZRc.
 * @param   idCpu           The Virtual CPU ID of the calling EMT.
 * @param   enmOperation    Which operation to execute.
 * @remarks Assume called with interrupts _enabled_.
 */
VMMR0DECL(void) VMMR0EntryFast(PGVM pGVM, PVMCC pVMIgnored, VMCPUID idCpu, VMMR0OPERATION enmOperation)
{
    RT_NOREF(pVMIgnored);

    /*
     * Validation.
     */
    if (   idCpu < pGVM->cCpus
        && pGVM->cCpus == pGVM->cCpusUnsafe)
    { /*likely*/ }
    else
    {
        SUPR0Printf("VMMR0EntryFast: Bad idCpu=%#x cCpus=%#x cCpusUnsafe=%#x\n", idCpu, pGVM->cCpus, pGVM->cCpusUnsafe);
        return;
    }

    PGVMCPU pGVCpu = &pGVM->aCpus[idCpu];
    RTNATIVETHREAD const hNativeThread = RTThreadNativeSelf();
    if (RT_LIKELY(   pGVCpu->hEMT            == hNativeThread
                  && pGVCpu->hNativeThreadR0 == hNativeThread))
    { /* likely */ }
    else
    {
        SUPR0Printf("VMMR0EntryFast: Bad thread idCpu=%#x hNativeSelf=%p pGVCpu->hEmt=%p pGVCpu->hNativeThreadR0=%p\n",
                    idCpu, hNativeThread, pGVCpu->hEMT, pGVCpu->hNativeThreadR0);
        return;
    }

    /*
     * SMAP fun.
     */
    VMM_CHECK_SMAP_SETUP();
    VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);

    /*
     * Perform requested operation.
     */
    switch (enmOperation)
    {
        /*
         * Run guest code using the available hardware acceleration technology.
         */
        case VMMR0_DO_HM_RUN:
        {
            for (;;) /* hlt loop */
            {
                /*
                 * Disable preemption.
                 */
                Assert(!vmmR0ThreadCtxHookIsEnabled(pGVCpu));
                RTTHREADPREEMPTSTATE PreemptState = RTTHREADPREEMPTSTATE_INITIALIZER;
                RTThreadPreemptDisable(&PreemptState);
                pGVCpu->vmmr0.s.pPreemptState = &PreemptState;

                /*
                 * Get the host CPU identifiers, make sure they are valid and that
                 * we've got a TSC delta for the CPU.
                 */
                RTCPUID  idHostCpu;
                uint32_t iHostCpuSet = RTMpCurSetIndexAndId(&idHostCpu);
                if (RT_LIKELY(   iHostCpuSet < RTCPUSET_MAX_CPUS
                              && SUPIsTscDeltaAvailableForCpuSetIndex(iHostCpuSet)))
                {
                    pGVCpu->iHostCpuSet = iHostCpuSet;
                    ASMAtomicWriteU32(&pGVCpu->idHostCpu, idHostCpu);

                    /*
                     * Update the periodic preemption timer if it's active.
                     */
                    if (pGVM->vmm.s.fUsePeriodicPreemptionTimers)
                        GVMMR0SchedUpdatePeriodicPreemptionTimer(pGVM, pGVCpu->idHostCpu, TMCalcHostTimerFrequency(pGVM, pGVCpu));
                    VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);

#ifdef VMM_R0_TOUCH_FPU
                    /*
                     * Make sure we've got the FPU state loaded so and we don't need to clear
                     * CR0.TS and get out of sync with the host kernel when loading the guest
                     * FPU state.  @ref sec_cpum_fpu (CPUM.cpp) and @bugref{4053}.
                     */
                    CPUMR0TouchHostFpu();
#endif
                    int  rc;
                    bool fPreemptRestored = false;
                    if (!HMR0SuspendPending())
                    {
                        /*
                         * Enable the context switching hook.
                         */
                        if (pGVCpu->vmmr0.s.hCtxHook != NIL_RTTHREADCTXHOOK)
                        {
                            Assert(!RTThreadCtxHookIsEnabled(pGVCpu->vmmr0.s.hCtxHook));
                            int rc2 = RTThreadCtxHookEnable(pGVCpu->vmmr0.s.hCtxHook); AssertRC(rc2);
                        }

                        /*
                         * Enter HM context.
                         */
                        rc = HMR0Enter(pGVCpu);
                        if (RT_SUCCESS(rc))
                        {
                            pGVCpu->vmmr0.s.fInHmContext = true;
                            VMCPU_SET_STATE(pGVCpu, VMCPUSTATE_STARTED_HM);

                            /*
                             * When preemption hooks are in place, enable preemption now that
                             * we're in HM context.
                             */
                            if (vmmR0ThreadCtxHookIsEnabled(pGVCpu))
                            {
                                fPreemptRestored = true;
                                pGVCpu->vmmr0.s.pPreemptState = NULL;
                                RTThreadPreemptRestore(&PreemptState);
                            }

                            /*
                             * Setup the longjmp machinery and execute guest code (calls HMR0RunGuestCode).
                             */
                            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
                            rc = vmmR0CallRing3SetJmp(&pGVCpu->vmm.s.CallRing3JmpBufR0, HMR0RunGuestCode, pGVM, pGVCpu);
                            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);

                            /*
                             * Assert sanity on the way out.  Using manual assertions code here as normal
                             * assertions are going to panic the host since we're outside the setjmp/longjmp zone.
                             */
                            if (RT_UNLIKELY(   VMCPU_GET_STATE(pGVCpu) != VMCPUSTATE_STARTED_HM
                                            && RT_SUCCESS_NP(rc)
                                            && rc != VINF_VMM_CALL_HOST ))
                            {
                                pGVM->vmm.s.szRing0AssertMsg1[0] = '\0';
                                RTStrPrintf(pGVM->vmm.s.szRing0AssertMsg2, sizeof(pGVM->vmm.s.szRing0AssertMsg2),
                                            "Got VMCPU state %d expected %d.\n", VMCPU_GET_STATE(pGVCpu), VMCPUSTATE_STARTED_HM);
                                rc = VERR_VMM_WRONG_HM_VMCPU_STATE;
                            }
#if 0
                            /** @todo Get rid of this. HM shouldn't disable the context hook. */
                            else if (RT_UNLIKELY(vmmR0ThreadCtxHookIsEnabled(pGVCpu)))
                            {
                                pGVM->vmm.s.szRing0AssertMsg1[0] = '\0';
                                RTStrPrintf(pGVM->vmm.s.szRing0AssertMsg2, sizeof(pGVM->vmm.s.szRing0AssertMsg2),
                                            "Thread-context hooks still enabled! VCPU=%p Id=%u rc=%d.\n", pGVCpu, pGVCpu->idCpu, rc);
                                rc = VERR_VMM_CONTEXT_HOOK_STILL_ENABLED;
                            }
#endif

                            pGVCpu->vmmr0.s.fInHmContext = false;
                            VMCPU_SET_STATE(pGVCpu, VMCPUSTATE_STARTED);
                        }
                        STAM_COUNTER_INC(&pGVM->vmm.s.StatRunGC);

                        /*
                         * Invalidate the host CPU identifiers before we disable the context
                         * hook / restore preemption.
                         */
                        pGVCpu->iHostCpuSet = UINT32_MAX;
                        ASMAtomicWriteU32(&pGVCpu->idHostCpu, NIL_RTCPUID);

                        /*
                         * Disable context hooks.  Due to unresolved cleanup issues, we
                         * cannot leave the hooks enabled when we return to ring-3.
                         *
                         * Note! At the moment HM may also have disabled the hook
                         *       when we get here, but the IPRT API handles that.
                         */
                        if (pGVCpu->vmmr0.s.hCtxHook != NIL_RTTHREADCTXHOOK)
                            RTThreadCtxHookDisable(pGVCpu->vmmr0.s.hCtxHook);
                    }
                    /*
                     * The system is about to go into suspend mode; go back to ring 3.
                     */
                    else
                    {
                        pGVCpu->iHostCpuSet = UINT32_MAX;
                        ASMAtomicWriteU32(&pGVCpu->idHostCpu, NIL_RTCPUID);
                        rc = VINF_EM_RAW_INTERRUPT;
                    }

                    /** @todo When HM stops messing with the context hook state, we'll disable
                     *        preemption again before the RTThreadCtxHookDisable call. */
                    if (!fPreemptRestored)
                    {
                        pGVCpu->vmmr0.s.pPreemptState = NULL;
                        RTThreadPreemptRestore(&PreemptState);
                    }

                    pGVCpu->vmm.s.iLastGZRc = rc;

                    /* Fire dtrace probe and collect statistics. */
                    VBOXVMM_R0_VMM_RETURN_TO_RING3_HM(pGVCpu, CPUMQueryGuestCtxPtr(pGVCpu), rc);
#ifdef VBOX_WITH_STATISTICS
                    vmmR0RecordRC(pGVM, pGVCpu, rc);
#endif
                    /*
                     * If this is a halt.
                     */
                    if (rc != VINF_EM_HALT)
                    { /* we're not in a hurry for a HLT, so prefer this path */ }
                    else
                    {
                        pGVCpu->vmm.s.iLastGZRc = rc = vmmR0DoHalt(pGVM, pGVCpu);
                        if (rc == VINF_SUCCESS)
                        {
                            pGVCpu->vmm.s.cR0HaltsSucceeded++;
                            continue;
                        }
                        pGVCpu->vmm.s.cR0HaltsToRing3++;
                    }
                }
                /*
                 * Invalid CPU set index or TSC delta in need of measuring.
                 */
                else
                {
                    pGVCpu->vmmr0.s.pPreemptState = NULL;
                    pGVCpu->iHostCpuSet = UINT32_MAX;
                    ASMAtomicWriteU32(&pGVCpu->idHostCpu, NIL_RTCPUID);
                    RTThreadPreemptRestore(&PreemptState);
                    if (iHostCpuSet < RTCPUSET_MAX_CPUS)
                    {
                        int rc = SUPR0TscDeltaMeasureBySetIndex(pGVM->pSession, iHostCpuSet, 0 /*fFlags*/,
                                                                2 /*cMsWaitRetry*/, 5*RT_MS_1SEC /*cMsWaitThread*/,
                                                                0 /*default cTries*/);
                        if (RT_SUCCESS(rc) || rc == VERR_CPU_OFFLINE)
                            pGVCpu->vmm.s.iLastGZRc = VINF_EM_RAW_TO_R3;
                        else
                            pGVCpu->vmm.s.iLastGZRc = rc;
                    }
                    else
                        pGVCpu->vmm.s.iLastGZRc = VERR_INVALID_CPU_INDEX;
                }
                break;

            } /* halt loop. */
            break;
        }

#ifdef VBOX_WITH_NEM_R0
# if defined(RT_ARCH_AMD64) && defined(RT_OS_WINDOWS)
        case VMMR0_DO_NEM_RUN:
        {
            /*
             * Setup the longjmp machinery and execute guest code (calls NEMR0RunGuestCode).
             */
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
#  ifdef VBOXSTRICTRC_STRICT_ENABLED
            int rc = vmmR0CallRing3SetJmp2(&pGVCpu->vmm.s.CallRing3JmpBufR0, (PFNVMMR0SETJMP2)NEMR0RunGuestCode, pGVM, idCpu);
#  else
            int rc = vmmR0CallRing3SetJmp2(&pGVCpu->vmm.s.CallRing3JmpBufR0, NEMR0RunGuestCode, pGVM, idCpu);
#  endif
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            STAM_COUNTER_INC(&pGVM->vmm.s.StatRunGC);

            pGVCpu->vmm.s.iLastGZRc = rc;

            /*
             * Fire dtrace probe and collect statistics.
             */
            VBOXVMM_R0_VMM_RETURN_TO_RING3_NEM(pGVCpu, CPUMQueryGuestCtxPtr(pGVCpu), rc);
#  ifdef VBOX_WITH_STATISTICS
            vmmR0RecordRC(pGVM, pGVCpu, rc);
#  endif
            break;
        }
# endif
#endif

        /*
         * For profiling.
         */
        case VMMR0_DO_NOP:
            pGVCpu->vmm.s.iLastGZRc = VINF_SUCCESS;
            break;

        /*
         * Shouldn't happen.
         */
        default:
            AssertMsgFailed(("%#x\n", enmOperation));
            pGVCpu->vmm.s.iLastGZRc = VERR_NOT_SUPPORTED;
            break;
    }
    VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
}


/**
 * Validates a session or VM session argument.
 *
 * @returns true / false accordingly.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   pClaimedSession The session claim to validate.
 * @param   pSession        The session argument.
 */
DECLINLINE(bool) vmmR0IsValidSession(PGVM pGVM, PSUPDRVSESSION pClaimedSession, PSUPDRVSESSION pSession)
{
    /* This must be set! */
    if (!pSession)
        return false;

    /* Only one out of the two. */
    if (pGVM && pClaimedSession)
        return false;
    if (pGVM)
        pClaimedSession = pGVM->pSession;
    return pClaimedSession == pSession;
}


/**
 * VMMR0EntryEx worker function, either called directly or when ever possible
 * called thru a longjmp so we can exit safely on failure.
 *
 * @returns VBox status code.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   idCpu           Virtual CPU ID argument. Must be NIL_VMCPUID if pVM
 *                          is NIL_RTR0PTR, and may be NIL_VMCPUID if it isn't
 * @param   enmOperation    Which operation to execute.
 * @param   pReqHdr         This points to a SUPVMMR0REQHDR packet. Optional.
 *                          The support driver validates this if it's present.
 * @param   u64Arg          Some simple constant argument.
 * @param   pSession        The session of the caller.
 *
 * @remarks Assume called with interrupts _enabled_.
 */
DECL_NO_INLINE(static, int) vmmR0EntryExWorker(PGVM pGVM, VMCPUID idCpu, VMMR0OPERATION enmOperation,
                                               PSUPVMMR0REQHDR pReqHdr, uint64_t u64Arg, PSUPDRVSESSION pSession)
{
    /*
     * Validate pGVM and idCpu for consistency and validity.
     */
    if (pGVM != NULL)
    {
        if (RT_LIKELY(((uintptr_t)pGVM & PAGE_OFFSET_MASK) == 0))
        { /* likely */ }
        else
        {
            SUPR0Printf("vmmR0EntryExWorker: Invalid pGVM=%p! (op=%d)\n", pGVM, enmOperation);
            return VERR_INVALID_POINTER;
        }

        if (RT_LIKELY(idCpu == NIL_VMCPUID || idCpu < pGVM->cCpus))
        { /* likely */ }
        else
        {
            SUPR0Printf("vmmR0EntryExWorker: Invalid idCpu %#x (cCpus=%#x)\n", idCpu, pGVM->cCpus);
            return VERR_INVALID_PARAMETER;
        }

        if (RT_LIKELY(   pGVM->enmVMState >= VMSTATE_CREATING
                      && pGVM->enmVMState <= VMSTATE_TERMINATED
                      && pGVM->pSession   == pSession
                      && pGVM->pSelf      == pGVM))
        { /* likely */ }
        else
        {
            SUPR0Printf("vmmR0EntryExWorker: Invalid pGVM=%p:{.enmVMState=%d, .cCpus=%#x, .pSession=%p(==%p), .pSelf=%p(==%p)}! (op=%d)\n",
                        pGVM, pGVM->enmVMState, pGVM->cCpus, pGVM->pSession, pSession, pGVM->pSelf, pGVM, enmOperation);
            return VERR_INVALID_POINTER;
        }
    }
    else if (RT_LIKELY(idCpu == NIL_VMCPUID))
    { /* likely */ }
    else
    {
        SUPR0Printf("vmmR0EntryExWorker: Invalid idCpu=%u\n", idCpu);
        return VERR_INVALID_PARAMETER;
    }

    /*
     * SMAP fun.
     */
    VMM_CHECK_SMAP_SETUP();
    VMM_CHECK_SMAP_CHECK(RT_NOTHING);

    /*
     * Process the request.
     */
    int rc;
    switch (enmOperation)
    {
        /*
         * GVM requests
         */
        case VMMR0_DO_GVMM_CREATE_VM:
            if (pGVM == NULL && u64Arg == 0 && idCpu == NIL_VMCPUID)
                rc = GVMMR0CreateVMReq((PGVMMCREATEVMREQ)pReqHdr, pSession);
            else
                rc = VERR_INVALID_PARAMETER;
            VMM_CHECK_SMAP_CHECK(RT_NOTHING);
            break;

        case VMMR0_DO_GVMM_DESTROY_VM:
            if (pReqHdr == NULL && u64Arg == 0)
                rc = GVMMR0DestroyVM(pGVM);
            else
                rc = VERR_INVALID_PARAMETER;
            VMM_CHECK_SMAP_CHECK(RT_NOTHING);
            break;

        case VMMR0_DO_GVMM_REGISTER_VMCPU:
            if (pGVM != NULL)
                rc = GVMMR0RegisterVCpu(pGVM, idCpu);
            else
                rc = VERR_INVALID_PARAMETER;
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_GVMM_DEREGISTER_VMCPU:
            if (pGVM != NULL)
                rc = GVMMR0DeregisterVCpu(pGVM, idCpu);
            else
                rc = VERR_INVALID_PARAMETER;
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_GVMM_SCHED_HALT:
            if (pReqHdr)
                return VERR_INVALID_PARAMETER;
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            rc = GVMMR0SchedHaltReq(pGVM, idCpu, u64Arg);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_GVMM_SCHED_WAKE_UP:
            if (pReqHdr || u64Arg)
                return VERR_INVALID_PARAMETER;
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            rc = GVMMR0SchedWakeUp(pGVM, idCpu);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_GVMM_SCHED_POKE:
            if (pReqHdr || u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GVMMR0SchedPoke(pGVM, idCpu);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_GVMM_SCHED_WAKE_UP_AND_POKE_CPUS:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GVMMR0SchedWakeUpAndPokeCpusReq(pGVM, (PGVMMSCHEDWAKEUPANDPOKECPUSREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_GVMM_SCHED_POLL:
            if (pReqHdr || u64Arg > 1)
                return VERR_INVALID_PARAMETER;
            rc = GVMMR0SchedPoll(pGVM, idCpu, !!u64Arg);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_GVMM_QUERY_STATISTICS:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GVMMR0QueryStatisticsReq(pGVM, (PGVMMQUERYSTATISTICSSREQ)pReqHdr, pSession);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_GVMM_RESET_STATISTICS:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GVMMR0ResetStatisticsReq(pGVM, (PGVMMRESETSTATISTICSSREQ)pReqHdr, pSession);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        /*
         * Initialize the R0 part of a VM instance.
         */
        case VMMR0_DO_VMMR0_INIT:
            rc = vmmR0InitVM(pGVM, RT_LODWORD(u64Arg), RT_HIDWORD(u64Arg));
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        /*
         * Does EMT specific ring-0 init.
         */
        case VMMR0_DO_VMMR0_INIT_EMT:
            rc = vmmR0InitVMEmt(pGVM, idCpu);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        /*
         * Terminate the R0 part of a VM instance.
         */
        case VMMR0_DO_VMMR0_TERM:
            rc = VMMR0TermVM(pGVM, 0 /*idCpu*/);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        /*
         * Update release or debug logger instances.
         */
        case VMMR0_DO_VMMR0_UPDATE_LOGGERS:
            if (idCpu == NIL_VMCPUID)
                return VERR_INVALID_CPU_ID;
            if (u64Arg <= 1 && pReqHdr != NULL)
                rc = vmmR0UpdateLoggers(pGVM, idCpu /*idCpu*/, (PVMMR0UPDATELOGGERSREQ)pReqHdr, u64Arg != 0);
            else
                return VERR_INVALID_PARAMETER;
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        /*
         * Attempt to enable hm mode and check the current setting.
         */
        case VMMR0_DO_HM_ENABLE:
            rc = HMR0EnableAllCpus(pGVM);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        /*
         * Setup the hardware accelerated session.
         */
        case VMMR0_DO_HM_SETUP_VM:
            rc = HMR0SetupVM(pGVM);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        /*
         * PGM wrappers.
         */
        case VMMR0_DO_PGM_ALLOCATE_HANDY_PAGES:
            if (idCpu == NIL_VMCPUID)
                return VERR_INVALID_CPU_ID;
            rc = PGMR0PhysAllocateHandyPages(pGVM, idCpu);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_PGM_FLUSH_HANDY_PAGES:
            if (idCpu == NIL_VMCPUID)
                return VERR_INVALID_CPU_ID;
            rc = PGMR0PhysFlushHandyPages(pGVM, idCpu);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_PGM_ALLOCATE_LARGE_HANDY_PAGE:
            if (idCpu == NIL_VMCPUID)
                return VERR_INVALID_CPU_ID;
            rc = PGMR0PhysAllocateLargeHandyPage(pGVM, idCpu);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_PGM_PHYS_SETUP_IOMMU:
            if (idCpu != 0)
                return VERR_INVALID_CPU_ID;
            rc = PGMR0PhysSetupIoMmu(pGVM);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_PGM_POOL_GROW:
            if (idCpu == NIL_VMCPUID)
                return VERR_INVALID_CPU_ID;
            rc = PGMR0PoolGrow(pGVM);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        /*
         * GMM wrappers.
         */
        case VMMR0_DO_GMM_INITIAL_RESERVATION:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0InitialReservationReq(pGVM, idCpu, (PGMMINITIALRESERVATIONREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_GMM_UPDATE_RESERVATION:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0UpdateReservationReq(pGVM, idCpu, (PGMMUPDATERESERVATIONREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_GMM_ALLOCATE_PAGES:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0AllocatePagesReq(pGVM, idCpu, (PGMMALLOCATEPAGESREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_GMM_FREE_PAGES:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0FreePagesReq(pGVM, idCpu, (PGMMFREEPAGESREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_GMM_FREE_LARGE_PAGE:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0FreeLargePageReq(pGVM, idCpu, (PGMMFREELARGEPAGEREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_GMM_QUERY_HYPERVISOR_MEM_STATS:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0QueryHypervisorMemoryStatsReq((PGMMMEMSTATSREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_GMM_QUERY_MEM_STATS:
            if (idCpu == NIL_VMCPUID)
                return VERR_INVALID_CPU_ID;
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0QueryMemoryStatsReq(pGVM, idCpu, (PGMMMEMSTATSREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_GMM_BALLOONED_PAGES:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0BalloonedPagesReq(pGVM, idCpu, (PGMMBALLOONEDPAGESREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_GMM_MAP_UNMAP_CHUNK:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0MapUnmapChunkReq(pGVM, (PGMMMAPUNMAPCHUNKREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_GMM_SEED_CHUNK:
            if (pReqHdr)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0SeedChunk(pGVM, idCpu, (RTR3PTR)u64Arg);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_GMM_REGISTER_SHARED_MODULE:
            if (idCpu == NIL_VMCPUID)
                return VERR_INVALID_CPU_ID;
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0RegisterSharedModuleReq(pGVM, idCpu, (PGMMREGISTERSHAREDMODULEREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_GMM_UNREGISTER_SHARED_MODULE:
            if (idCpu == NIL_VMCPUID)
                return VERR_INVALID_CPU_ID;
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0UnregisterSharedModuleReq(pGVM, idCpu, (PGMMUNREGISTERSHAREDMODULEREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_GMM_RESET_SHARED_MODULES:
            if (idCpu == NIL_VMCPUID)
                return VERR_INVALID_CPU_ID;
            if (    u64Arg
                ||  pReqHdr)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0ResetSharedModules(pGVM, idCpu);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

#ifdef VBOX_WITH_PAGE_SHARING
        case VMMR0_DO_GMM_CHECK_SHARED_MODULES:
        {
            if (idCpu == NIL_VMCPUID)
                return VERR_INVALID_CPU_ID;
            if (    u64Arg
                ||  pReqHdr)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0CheckSharedModules(pGVM, idCpu);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;
        }
#endif

#if defined(VBOX_STRICT) && HC_ARCH_BITS == 64
        case VMMR0_DO_GMM_FIND_DUPLICATE_PAGE:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0FindDuplicatePageReq(pGVM, (PGMMFINDDUPLICATEPAGEREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;
#endif

        case VMMR0_DO_GMM_QUERY_STATISTICS:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0QueryStatisticsReq(pGVM, (PGMMQUERYSTATISTICSSREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_GMM_RESET_STATISTICS:
            if (u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = GMMR0ResetStatisticsReq(pGVM, (PGMMRESETSTATISTICSSREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        /*
         * A quick GCFGM mock-up.
         */
        /** @todo GCFGM with proper access control, ring-3 management interface and all that. */
        case VMMR0_DO_GCFGM_SET_VALUE:
        case VMMR0_DO_GCFGM_QUERY_VALUE:
        {
            if (pGVM || !pReqHdr || u64Arg || idCpu != NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            PGCFGMVALUEREQ pReq = (PGCFGMVALUEREQ)pReqHdr;
            if (pReq->Hdr.cbReq != sizeof(*pReq))
                return VERR_INVALID_PARAMETER;
            if (enmOperation == VMMR0_DO_GCFGM_SET_VALUE)
            {
                rc = GVMMR0SetConfig(pReq->pSession, &pReq->szName[0], pReq->u64Value);
                //if (rc == VERR_CFGM_VALUE_NOT_FOUND)
                //    rc = GMMR0SetConfig(pReq->pSession, &pReq->szName[0], pReq->u64Value);
            }
            else
            {
                rc = GVMMR0QueryConfig(pReq->pSession, &pReq->szName[0], &pReq->u64Value);
                //if (rc == VERR_CFGM_VALUE_NOT_FOUND)
                //    rc = GMMR0QueryConfig(pReq->pSession, &pReq->szName[0], &pReq->u64Value);
            }
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;
        }

        /*
         * PDM Wrappers.
         */
        case VMMR0_DO_PDM_DRIVER_CALL_REQ_HANDLER:
        {
            if (!pReqHdr || u64Arg || idCpu != NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = PDMR0DriverCallReqHandler(pGVM, (PPDMDRIVERCALLREQHANDLERREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;
        }

        case VMMR0_DO_PDM_DEVICE_CREATE:
        {
            if (!pReqHdr || u64Arg || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = PDMR0DeviceCreateReqHandler(pGVM, (PPDMDEVICECREATEREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;
        }

        case VMMR0_DO_PDM_DEVICE_GEN_CALL:
        {
            if (!pReqHdr || u64Arg)
                return VERR_INVALID_PARAMETER;
            rc = PDMR0DeviceGenCallReqHandler(pGVM, (PPDMDEVICEGENCALLREQ)pReqHdr, idCpu);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;
        }

        /** @todo Remove the once all devices has been converted to new style! @bugref{9218} */
        case VMMR0_DO_PDM_DEVICE_COMPAT_SET_CRITSECT:
        {
            if (!pReqHdr || u64Arg || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = PDMR0DeviceCompatSetCritSectReqHandler(pGVM, (PPDMDEVICECOMPATSETCRITSECTREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;
        }

        /*
         * Requests to the internal networking service.
         */
        case VMMR0_DO_INTNET_OPEN:
        {
            PINTNETOPENREQ pReq = (PINTNETOPENREQ)pReqHdr;
            if (u64Arg || !pReq || !vmmR0IsValidSession(pGVM, pReq->pSession, pSession) || idCpu != NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = IntNetR0OpenReq(pSession, pReq);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;
        }

        case VMMR0_DO_INTNET_IF_CLOSE:
            if (u64Arg || !pReqHdr || !vmmR0IsValidSession(pGVM, ((PINTNETIFCLOSEREQ)pReqHdr)->pSession, pSession) || idCpu != NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = IntNetR0IfCloseReq(pSession, (PINTNETIFCLOSEREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;


        case VMMR0_DO_INTNET_IF_GET_BUFFER_PTRS:
            if (u64Arg || !pReqHdr || !vmmR0IsValidSession(pGVM, ((PINTNETIFGETBUFFERPTRSREQ)pReqHdr)->pSession, pSession) || idCpu != NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = IntNetR0IfGetBufferPtrsReq(pSession, (PINTNETIFGETBUFFERPTRSREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_INTNET_IF_SET_PROMISCUOUS_MODE:
            if (u64Arg || !pReqHdr || !vmmR0IsValidSession(pGVM, ((PINTNETIFSETPROMISCUOUSMODEREQ)pReqHdr)->pSession, pSession) || idCpu != NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = IntNetR0IfSetPromiscuousModeReq(pSession, (PINTNETIFSETPROMISCUOUSMODEREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_INTNET_IF_SET_MAC_ADDRESS:
            if (u64Arg || !pReqHdr || !vmmR0IsValidSession(pGVM, ((PINTNETIFSETMACADDRESSREQ)pReqHdr)->pSession, pSession) || idCpu != NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = IntNetR0IfSetMacAddressReq(pSession, (PINTNETIFSETMACADDRESSREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_INTNET_IF_SET_ACTIVE:
            if (u64Arg || !pReqHdr || !vmmR0IsValidSession(pGVM, ((PINTNETIFSETACTIVEREQ)pReqHdr)->pSession, pSession) || idCpu != NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = IntNetR0IfSetActiveReq(pSession, (PINTNETIFSETACTIVEREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_INTNET_IF_SEND:
            if (u64Arg || !pReqHdr || !vmmR0IsValidSession(pGVM, ((PINTNETIFSENDREQ)pReqHdr)->pSession, pSession) || idCpu != NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = IntNetR0IfSendReq(pSession, (PINTNETIFSENDREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_INTNET_IF_WAIT:
            if (u64Arg || !pReqHdr || !vmmR0IsValidSession(pGVM, ((PINTNETIFWAITREQ)pReqHdr)->pSession, pSession) || idCpu != NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = IntNetR0IfWaitReq(pSession, (PINTNETIFWAITREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_INTNET_IF_ABORT_WAIT:
            if (u64Arg || !pReqHdr || !vmmR0IsValidSession(pGVM, ((PINTNETIFWAITREQ)pReqHdr)->pSession, pSession) || idCpu != NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = IntNetR0IfAbortWaitReq(pSession, (PINTNETIFABORTWAITREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

#if 0 //def VBOX_WITH_PCI_PASSTHROUGH
        /*
         * Requests to host PCI driver service.
         */
        case VMMR0_DO_PCIRAW_REQ:
            if (u64Arg || !pReqHdr || !vmmR0IsValidSession(pGVM, ((PPCIRAWSENDREQ)pReqHdr)->pSession, pSession) || idCpu != NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = PciRawR0ProcessReq(pGVM, pSession, (PPCIRAWSENDREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;
#endif

        /*
         * NEM requests.
         */
#ifdef VBOX_WITH_NEM_R0
# if defined(RT_ARCH_AMD64) && defined(RT_OS_WINDOWS)
        case VMMR0_DO_NEM_INIT_VM:
            if (u64Arg || pReqHdr || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = NEMR0InitVM(pGVM);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_NEM_INIT_VM_PART_2:
            if (u64Arg || pReqHdr || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = NEMR0InitVMPart2(pGVM);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_NEM_MAP_PAGES:
            if (u64Arg || pReqHdr || idCpu == NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = NEMR0MapPages(pGVM, idCpu);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_NEM_UNMAP_PAGES:
            if (u64Arg || pReqHdr || idCpu == NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = NEMR0UnmapPages(pGVM, idCpu);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_NEM_EXPORT_STATE:
            if (u64Arg || pReqHdr || idCpu == NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = NEMR0ExportState(pGVM, idCpu);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_NEM_IMPORT_STATE:
            if (pReqHdr || idCpu == NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = NEMR0ImportState(pGVM, idCpu, u64Arg);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_NEM_QUERY_CPU_TICK:
            if (u64Arg || pReqHdr || idCpu == NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = NEMR0QueryCpuTick(pGVM, idCpu);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_NEM_RESUME_CPU_TICK_ON_ALL:
            if (pReqHdr || idCpu == NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = NEMR0ResumeCpuTickOnAll(pGVM, idCpu, u64Arg);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

        case VMMR0_DO_NEM_UPDATE_STATISTICS:
            if (u64Arg || pReqHdr)
                return VERR_INVALID_PARAMETER;
            rc = NEMR0UpdateStatistics(pGVM, idCpu);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;

#   if 1 && defined(DEBUG_bird)
        case VMMR0_DO_NEM_EXPERIMENT:
            if (pReqHdr)
                return VERR_INVALID_PARAMETER;
            rc = NEMR0DoExperiment(pGVM, idCpu, u64Arg);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;
#   endif
# endif
#endif

        /*
         * IOM requests.
         */
        case VMMR0_DO_IOM_GROW_IO_PORTS:
        {
            if (pReqHdr || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = IOMR0IoPortGrowRegistrationTables(pGVM, u64Arg);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;
        }

        case VMMR0_DO_IOM_GROW_IO_PORT_STATS:
        {
            if (pReqHdr || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = IOMR0IoPortGrowStatisticsTable(pGVM, u64Arg);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;
        }

        case VMMR0_DO_IOM_GROW_MMIO_REGS:
        {
            if (pReqHdr || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = IOMR0MmioGrowRegistrationTables(pGVM, u64Arg);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;
        }

        case VMMR0_DO_IOM_GROW_MMIO_STATS:
        {
            if (pReqHdr || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = IOMR0MmioGrowStatisticsTable(pGVM, u64Arg);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;
        }

        case VMMR0_DO_IOM_SYNC_STATS_INDICES:
        {
            if (pReqHdr || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = IOMR0IoPortSyncStatisticsIndices(pGVM);
            if (RT_SUCCESS(rc))
                rc = IOMR0MmioSyncStatisticsIndices(pGVM);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;
        }

        /*
         * DBGF requests.
         */
#ifdef VBOX_WITH_DBGF_TRACING
        case VMMR0_DO_DBGF_TRACER_CREATE:
        {
            if (!pReqHdr || u64Arg || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = DBGFR0TracerCreateReqHandler(pGVM, (PDBGFTRACERCREATEREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;
        }

        case VMMR0_DO_DBGF_TRACER_CALL_REQ_HANDLER:
        {
            if (!pReqHdr || u64Arg)
                return VERR_INVALID_PARAMETER;
# if 0 /** @todo */
            rc = DBGFR0TracerGenCallReqHandler(pGVM, (PDBGFTRACERGENCALLREQ)pReqHdr, idCpu);
# else
            rc = VERR_NOT_IMPLEMENTED;
# endif
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;
        }
#endif

        case VMMR0_DO_DBGF_BP_INIT:
        {
            if (!pReqHdr || u64Arg || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = DBGFR0BpInitReqHandler(pGVM, (PDBGFBPINITREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;
        }

        case VMMR0_DO_DBGF_BP_CHUNK_ALLOC:
        {
            if (!pReqHdr || u64Arg || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = DBGFR0BpChunkAllocReqHandler(pGVM, (PDBGFBPCHUNKALLOCREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;
        }

        case VMMR0_DO_DBGF_BP_L2_TBL_CHUNK_ALLOC:
        {
            if (!pReqHdr || u64Arg || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = DBGFR0BpL2TblChunkAllocReqHandler(pGVM, (PDBGFBPL2TBLCHUNKALLOCREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;
        }

        case VMMR0_DO_DBGF_BP_OWNER_INIT:
        {
            if (!pReqHdr || u64Arg || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = DBGFR0BpOwnerInitReqHandler(pGVM, (PDBGFBPOWNERINITREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;
        }

        case VMMR0_DO_DBGF_BP_PORTIO_INIT:
        {
            if (!pReqHdr || u64Arg || idCpu != 0)
                return VERR_INVALID_PARAMETER;
            rc = DBGFR0BpPortIoInitReqHandler(pGVM, (PDBGFBPINITREQ)pReqHdr);
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;
        }


        /*
         * TM requests.
         */
        case VMMR0_DO_TM_GROW_TIMER_QUEUE:
        {
            if (pReqHdr || idCpu == NIL_VMCPUID)
                return VERR_INVALID_PARAMETER;
            rc = TMR0TimerQueueGrow(pGVM, RT_HI_U32(u64Arg), RT_LO_U32(u64Arg));
            VMM_CHECK_SMAP_CHECK2(pGVM, RT_NOTHING);
            break;
        }

        /*
         * For profiling.
         */
        case VMMR0_DO_NOP:
        case VMMR0_DO_SLOW_NOP:
            return VINF_SUCCESS;

        /*
         * For testing Ring-0 APIs invoked in this environment.
         */
        case VMMR0_DO_TESTS:
            /** @todo make new test */
            return VINF_SUCCESS;

        default:
            /*
             * We're returning VERR_NOT_SUPPORT here so we've got something else
             * than -1 which the interrupt gate glue code might return.
             */
            Log(("operation %#x is not supported\n", enmOperation));
            return VERR_NOT_SUPPORTED;
    }
    return rc;
}


/**
 * This is just a longjmp wrapper function for VMMR0EntryEx calls.
 *
 * @returns VBox status code.
 * @param   pvArgs      The argument package
 */
static DECLCALLBACK(int) vmmR0EntryExWrapper(void *pvArgs)
{
    PGVMCPU pGVCpu = (PGVMCPU)pvArgs;
    return vmmR0EntryExWorker(pGVCpu->vmmr0.s.pGVM,
                              pGVCpu->vmmr0.s.idCpu,
                              pGVCpu->vmmr0.s.enmOperation,
                              pGVCpu->vmmr0.s.pReq,
                              pGVCpu->vmmr0.s.u64Arg,
                              pGVCpu->vmmr0.s.pSession);
}


/**
 * The Ring 0 entry point, called by the support library (SUP).
 *
 * @returns VBox status code.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   pVM             The cross context VM structure.
 * @param   idCpu           Virtual CPU ID argument. Must be NIL_VMCPUID if pVM
 *                          is NIL_RTR0PTR, and may be NIL_VMCPUID if it isn't
 * @param   enmOperation    Which operation to execute.
 * @param   pReq            Pointer to the SUPVMMR0REQHDR packet. Optional.
 * @param   u64Arg          Some simple constant argument.
 * @param   pSession        The session of the caller.
 * @remarks Assume called with interrupts _enabled_.
 */
VMMR0DECL(int) VMMR0EntryEx(PGVM pGVM, PVMCC pVM, VMCPUID idCpu, VMMR0OPERATION enmOperation,
                            PSUPVMMR0REQHDR pReq, uint64_t u64Arg, PSUPDRVSESSION pSession)
{
    /*
     * Requests that should only happen on the EMT thread will be
     * wrapped in a setjmp so we can assert without causing trouble.
     */
    if (   pVM  != NULL
        && pGVM != NULL
        && pVM  == pGVM /** @todo drop pVM or pGVM */
        && idCpu < pGVM->cCpus
        && pGVM->pSession == pSession
        && pGVM->pSelf    == pVM)
    {
        switch (enmOperation)
        {
            /* These might/will be called before VMMR3Init. */
            case VMMR0_DO_GMM_INITIAL_RESERVATION:
            case VMMR0_DO_GMM_UPDATE_RESERVATION:
            case VMMR0_DO_GMM_ALLOCATE_PAGES:
            case VMMR0_DO_GMM_FREE_PAGES:
            case VMMR0_DO_GMM_BALLOONED_PAGES:
            /* On the mac we might not have a valid jmp buf, so check these as well. */
            case VMMR0_DO_VMMR0_INIT:
            case VMMR0_DO_VMMR0_TERM:

            case VMMR0_DO_PDM_DEVICE_CREATE:
            case VMMR0_DO_PDM_DEVICE_GEN_CALL:
            case VMMR0_DO_IOM_GROW_IO_PORTS:
            case VMMR0_DO_IOM_GROW_IO_PORT_STATS:
            case VMMR0_DO_DBGF_BP_INIT:
            case VMMR0_DO_DBGF_BP_CHUNK_ALLOC:
            case VMMR0_DO_DBGF_BP_L2_TBL_CHUNK_ALLOC:
            {
                PGVMCPU        pGVCpu        = &pGVM->aCpus[idCpu];
                RTNATIVETHREAD hNativeThread = RTThreadNativeSelf();
                if (RT_LIKELY(   pGVCpu->hEMT            == hNativeThread
                              && pGVCpu->hNativeThreadR0 == hNativeThread))
                {
                    if (!pGVCpu->vmm.s.CallRing3JmpBufR0.pvSavedStack)
                        break;

                    pGVCpu->vmmr0.s.pGVM         = pGVM;
                    pGVCpu->vmmr0.s.idCpu        = idCpu;
                    pGVCpu->vmmr0.s.enmOperation = enmOperation;
                    pGVCpu->vmmr0.s.pReq         = pReq;
                    pGVCpu->vmmr0.s.u64Arg       = u64Arg;
                    pGVCpu->vmmr0.s.pSession     = pSession;
                    return vmmR0CallRing3SetJmpEx(&pGVCpu->vmm.s.CallRing3JmpBufR0, vmmR0EntryExWrapper, pGVCpu,
                                                  ((uintptr_t)u64Arg << 16) | (uintptr_t)enmOperation);
                }
                return VERR_VM_THREAD_NOT_EMT;
            }

            default:
            case VMMR0_DO_PGM_POOL_GROW:
                break;
        }
    }
    return vmmR0EntryExWorker(pGVM, idCpu, enmOperation, pReq, u64Arg, pSession);
}


/**
 * Checks whether we've armed the ring-0 long jump machinery.
 *
 * @returns @c true / @c false
 * @param   pVCpu           The cross context virtual CPU structure.
 * @thread  EMT
 * @sa      VMMIsLongJumpArmed
 */
VMMR0_INT_DECL(bool) VMMR0IsLongJumpArmed(PVMCPUCC pVCpu)
{
#ifdef RT_ARCH_X86
    return pVCpu->vmm.s.CallRing3JmpBufR0.eip
        && !pVCpu->vmm.s.CallRing3JmpBufR0.fInRing3Call;
#else
    return pVCpu->vmm.s.CallRing3JmpBufR0.rip
        && !pVCpu->vmm.s.CallRing3JmpBufR0.fInRing3Call;
#endif
}


/**
 * Checks whether we've done a ring-3 long jump.
 *
 * @returns @c true / @c false
 * @param   pVCpu       The cross context virtual CPU structure.
 * @thread  EMT
 */
VMMR0_INT_DECL(bool) VMMR0IsInRing3LongJump(PVMCPUCC pVCpu)
{
    return pVCpu->vmm.s.CallRing3JmpBufR0.fInRing3Call;
}


/**
 * Locking helper that deals with HM context and checks if the thread can block.
 *
 * @returns VINF_SUCCESS if we can block.  Returns @a rcBusy or
 *          VERR_VMM_CANNOT_BLOCK if not able to block.
 * @param   pVCpu       The cross context virtual CPU structure of the calling
 *                      thread.
 * @param   rcBusy      What to return in case of a blocking problem.  Will IPE
 *                      if VINF_SUCCESS and we cannot block.
 * @param   pszCaller   The caller (for logging problems).
 * @param   pvLock      The lock address (for logging problems).
 * @param   pCtx        Where to return context info for the resume call.
 * @thread  EMT(pVCpu)
 */
VMMR0_INT_DECL(int) VMMR0EmtPrepareToBlock(PVMCPUCC pVCpu, int rcBusy, const char *pszCaller, void *pvLock,
                                           PVMMR0EMTBLOCKCTX pCtx)
{
    const char *pszMsg;

    /*
     * Check that we are allowed to block.
     */
    if (RT_LIKELY(VMMRZCallRing3IsEnabled(pVCpu)))
    {
        /*
         * Are we in HM context and w/o a context hook?  If so work the context hook.
         */
        if (pVCpu->idHostCpu != NIL_RTCPUID)
        {
            Assert(pVCpu->iHostCpuSet != UINT32_MAX);
            Assert(pVCpu->vmmr0.s.fInHmContext);

            if (pVCpu->vmmr0.s.hCtxHook == NIL_RTTHREADCTXHOOK)
            {
                vmmR0ThreadCtxCallback(RTTHREADCTXEVENT_OUT, pVCpu);
                if (pVCpu->vmmr0.s.pPreemptState)
                    RTThreadPreemptRestore(pVCpu->vmmr0.s.pPreemptState);

                pCtx->uMagic          = VMMR0EMTBLOCKCTX_MAGIC;
                pCtx->fWasInHmContext = true;
                return VINF_SUCCESS;
            }
        }

        if (RT_LIKELY(!pVCpu->vmmr0.s.pPreemptState))
        {
            /*
             * Not in HM context or we've got hooks, so just check that preemption
             * is enabled.
             */
            if (RT_LIKELY(RTThreadPreemptIsEnabled(NIL_RTTHREAD)))
            {
                pCtx->uMagic          = VMMR0EMTBLOCKCTX_MAGIC;
                pCtx->fWasInHmContext = false;
                return VINF_SUCCESS;
            }
            pszMsg = "Preemption is disabled!";
        }
        else
            pszMsg = "Preemption state w/o HM state!";
    }
    else
        pszMsg = "Ring-3 calls are disabled!";

    static uint32_t volatile s_cWarnings = 0;
    if (++s_cWarnings < 50)
        SUPR0Printf("VMMR0EmtPrepareToBlock: %s pvLock=%p pszCaller=%s rcBusy=%p\n", pszMsg, pvLock, pszCaller, rcBusy);
    pCtx->uMagic          = VMMR0EMTBLOCKCTX_MAGIC_DEAD;
    pCtx->fWasInHmContext = false;
    return rcBusy != VINF_SUCCESS ? rcBusy : VERR_VMM_CANNOT_BLOCK;
}


/**
 * Counterpart to VMMR0EmtPrepareToBlock.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling
 *                      thread.
 * @param   pCtx        The context structure used with VMMR0EmtPrepareToBlock.
 * @thread  EMT(pVCpu)
 */
VMMR0_INT_DECL(void) VMMR0EmtResumeAfterBlocking(PVMCPUCC pVCpu, PVMMR0EMTBLOCKCTX pCtx)
{
    AssertReturnVoid(pCtx->uMagic == VMMR0EMTBLOCKCTX_MAGIC);
    if (pCtx->fWasInHmContext)
    {
        if (pVCpu->vmmr0.s.pPreemptState)
            RTThreadPreemptDisable(pVCpu->vmmr0.s.pPreemptState);

        pCtx->fWasInHmContext = false;
        vmmR0ThreadCtxCallback(RTTHREADCTXEVENT_IN, pVCpu);
    }
    pCtx->uMagic = VMMR0EMTBLOCKCTX_MAGIC_DEAD;
}


/**
 * Updates the EMT loggers for the VM.
 *
 * @returns VBox status code.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   idCpu           The ID of the calling EMT.
 * @param   pReq            The request data.
 * @param   fRelease        Which logger set to update.
 * @thread  EMT(idCpu)
 */
static int vmmR0UpdateLoggers(PGVM pGVM, VMCPUID idCpu, PVMMR0UPDATELOGGERSREQ pReq, bool fRelease)
{
    /*
     * Check sanity.  First we require EMT to be calling us.
     */
    AssertReturn(idCpu < pGVM->cCpus, VERR_INVALID_CPU_ID);
    AssertReturn(pGVM->aCpus[idCpu].hEMT == RTThreadNativeSelf(), VERR_INVALID_CPU_ID);

    AssertReturn(pReq->Hdr.cbReq >= RT_UOFFSETOF_DYN(VMMR0UPDATELOGGERSREQ, afGroups[0]), VERR_INVALID_PARAMETER);
    AssertReturn(pReq->cGroups < _8K, VERR_INVALID_PARAMETER);
    AssertReturn(pReq->Hdr.cbReq == RT_UOFFSETOF_DYN(VMMR0UPDATELOGGERSREQ, afGroups[pReq->cGroups]), VERR_INVALID_PARAMETER);

    /*
     * Adjust flags.
     */
    /* Always buffered: */
    pReq->fFlags |= RTLOGFLAGS_BUFFERED;
    /* These doesn't make sense at present: */
    pReq->fFlags &= ~(RTLOGFLAGS_FLUSH | RTLOGFLAGS_WRITE_THROUGH);
    /* We've traditionally skipped the group restrictions. */
    pReq->fFlags &= ~RTLOGFLAGS_RESTRICT_GROUPS;

    /*
     * Do the updating.
     */
    int rc = VINF_SUCCESS;
    for (idCpu = 0; idCpu < pGVM->cCpus; idCpu++)
    {
        PGVMCPU pGVCpu = &pGVM->aCpus[idCpu];
        PRTLOGGER pLogger = fRelease ? pGVCpu->vmmr0.s.RelLogger.pLogger : pGVCpu->vmmr0.s.Logger.pLogger;
        if (pLogger)
            rc = RTLogBulkUpdate(pLogger, pReq->fFlags, pReq->uGroupCrc32, pReq->cGroups, pReq->afGroups);
    }

    return rc;
}


/**
 * Common worker for vmmR0LogFlush and vmmR0LogRelFlush.
 */
static bool vmmR0LoggerFlushCommon(PRTLOGGER pLogger, PRTLOGBUFFERDESC pBufDesc, bool fRelease)
{
    RT_NOREF(pBufDesc, fRelease);

    /*
     * Convert the pLogger into a GVMCPU handle and 'call' back to Ring-3.
     * (This is a bit paranoid code.)
     */
    if (RT_VALID_PTR(pLogger))
    {
        if (   pLogger->u32Magic == RTLOGGER_MAGIC
            && (pLogger->u32UserValue1 & VMMR0_LOGGER_FLAGS_MAGIC_MASK) == VMMR0_LOGGER_FLAGS_MAGIC_VALUE
            && pLogger->u64UserValue2 == pLogger->u64UserValue3)
        {
            if (!(pLogger->u32UserValue1 & VMMR0_LOGGER_FLAGS_FLUSHING_DISABLED))
            {
                PGVMCPU const pGVCpu = (PGVMCPU)(uintptr_t)pLogger->u64UserValue2;
                if (   RT_VALID_PTR(pGVCpu)
                    && ((uintptr_t)pGVCpu & PAGE_OFFSET_MASK) == 0)
                {
                    RTNATIVETHREAD const hNativeSelf = RTThreadNativeSelf();
                    if (   hNativeSelf == pGVCpu->hEMT
                        && RT_VALID_PTR(pGVCpu->pGVM))
                    {
                        /*
                         * Check that the jump buffer is armed.
                         */
#ifdef RT_ARCH_X86
                        if (   pGVCpu->vmm.s.CallRing3JmpBufR0.eip != 0
                            && !pGVCpu->vmm.s.CallRing3JmpBufR0.fInRing3Call)
#else
                        if (   pGVCpu->vmm.s.CallRing3JmpBufR0.rip != 0
                            && !pGVCpu->vmm.s.CallRing3JmpBufR0.fInRing3Call)
#endif
                        {
                            VMMRZCallRing3(pGVCpu->pGVM, pGVCpu, VMMCALLRING3_VMM_LOGGER_FLUSH, fRelease);
                        }
#ifdef DEBUG
                        else SUPR0Printf("vmmR0LoggerFlush: Jump buffer isn't armed!\n");
#endif
                    }
#ifdef DEBUG
                    else SUPR0Printf("vmmR0LoggerFlush: pLogger=%p pGVCpu=%p hEMT=%p hNativeSelf=%p!\n",
                                     pLogger, pGVCpu, pGVCpu->hEMT, hNativeSelf);
#endif

                }
#ifdef DEBUG
                else SUPR0Printf("vmmR0LoggerFlush: pLogger=%p pGVCpu=%p!\n", pLogger, pGVCpu);
#endif
            }
            /* else: quiet */
        }
#ifdef DEBUG
        else SUPR0Printf("vmmR0LoggerFlush: pLogger=%p u32Magic=%#x u32UserValue1=%#x u64UserValue2=%#RX64 u64UserValue3=%#RX64!\n",
                        pLogger, pLogger->u32Magic, pLogger->u32UserValue1, pLogger->u64UserValue2, pLogger->u64UserValue3);
#endif
    }
#ifdef DEBUG
    else SUPR0Printf("vmmR0LoggerFlush: pLogger=%p!\n", pLogger);
#endif
    return true;
}


/**
 * @callback_method_impl{FNRTLOGFLUSH, Release logger buffer flush callback.}
 */
static DECLCALLBACK(bool) vmmR0LogRelFlush(PRTLOGGER pLogger, PRTLOGBUFFERDESC pBufDesc)
{
    return vmmR0LoggerFlushCommon(pLogger, pBufDesc, true /*fRelease*/);
}


/**
 * @callback_method_impl{FNRTLOGFLUSH, Logger (debug) buffer flush callback.}
 */
static DECLCALLBACK(bool) vmmR0LogFlush(PRTLOGGER pLogger, PRTLOGBUFFERDESC pBufDesc)
{
#ifdef LOG_ENABLED
    return vmmR0LoggerFlushCommon(pLogger, pBufDesc, false /*fRelease*/);
#else
    RT_NOREF(pLogger, pBufDesc);
    return true;
#endif
}

#ifdef LOG_ENABLED

/**
 * Disables flushing of the ring-0 debug log.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMR0_INT_DECL(void) VMMR0LogFlushDisable(PVMCPUCC pVCpu)
{
    pVCpu->vmmr0.s.fLogFlushingDisabled = true;
    if (pVCpu->vmmr0.s.Logger.pLogger)
        pVCpu->vmmr0.s.Logger.pLogger->u32UserValue1 |= VMMR0_LOGGER_FLAGS_FLUSHING_DISABLED;
    if (pVCpu->vmmr0.s.RelLogger.pLogger)
        pVCpu->vmmr0.s.RelLogger.pLogger->u32UserValue1 |= VMMR0_LOGGER_FLAGS_FLUSHING_DISABLED;
}


/**
 * Enables flushing of the ring-0 debug log.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMR0_INT_DECL(void) VMMR0LogFlushEnable(PVMCPUCC pVCpu)
{
    pVCpu->vmmr0.s.fLogFlushingDisabled = false;
    if (pVCpu->vmmr0.s.Logger.pLogger)
        pVCpu->vmmr0.s.Logger.pLogger->u32UserValue1 &= ~VMMR0_LOGGER_FLAGS_FLUSHING_DISABLED;
    if (pVCpu->vmmr0.s.RelLogger.pLogger)
        pVCpu->vmmr0.s.RelLogger.pLogger->u32UserValue1 &= ~VMMR0_LOGGER_FLAGS_FLUSHING_DISABLED;
}


/**
 * Checks if log flushing is disabled or not.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMR0_INT_DECL(bool) VMMR0IsLogFlushDisabled(PVMCPUCC pVCpu)
{
    return pVCpu->vmmr0.s.fLogFlushingDisabled;
}

#endif /* LOG_ENABLED */

/*
 * Override RTLogGetDefaultInstanceEx so we can do logging from EMTs in ring-0.
 */
DECLEXPORT(PRTLOGGER) RTLogGetDefaultInstanceEx(uint32_t fFlagsAndGroup)
{
#ifdef LOG_ENABLED
    PGVMCPU pGVCpu = GVMMR0GetGVCpuByEMT(NIL_RTNATIVETHREAD);
    if (pGVCpu)
    {
        PRTLOGGER pLogger = pGVCpu->vmmr0.s.Logger.pLogger;
        if (RT_VALID_PTR(pLogger))
        {
            if (   pLogger->u64UserValue2 == (uintptr_t)pGVCpu
                && pLogger->u64UserValue3 == (uintptr_t)pGVCpu)
            {
                if (!(pGVCpu->vmmr0.s.fLogFlushingDisabled))
                    return RTLogCheckGroupFlags(pLogger, fFlagsAndGroup);
                return NULL;
            }
        }
    }
#endif
    return SUPR0GetDefaultLogInstanceEx(fFlagsAndGroup);
}


/*
 * Override RTLogRelGetDefaultInstanceEx so we can do LogRel to VBox.log from EMTs in ring-0.
 */
DECLEXPORT(PRTLOGGER) RTLogRelGetDefaultInstanceEx(uint32_t fFlagsAndGroup)
{
    PGVMCPU pGVCpu = GVMMR0GetGVCpuByEMT(NIL_RTNATIVETHREAD);
    if (pGVCpu)
    {
        PRTLOGGER pLogger = pGVCpu->vmmr0.s.RelLogger.pLogger;
        if (RT_VALID_PTR(pLogger))
        {
            if (   pLogger->u64UserValue2 == (uintptr_t)pGVCpu
                && pLogger->u64UserValue3 == (uintptr_t)pGVCpu)
            {
                if (!(pGVCpu->vmmr0.s.fLogFlushingDisabled))
                    return RTLogCheckGroupFlags(pLogger, fFlagsAndGroup);
                return NULL;
            }
        }
    }
    return SUPR0GetDefaultLogRelInstanceEx(fFlagsAndGroup);
}


/*
 * Jump back to ring-3 if we're the EMT and the longjmp is armed.
 *
 * @returns true if the breakpoint should be hit, false if it should be ignored.
 */
DECLEXPORT(bool) RTCALL RTAssertShouldPanic(void)
{
#if 0
    return true;
#else
    PVMCC pVM = GVMMR0GetVMByEMT(NIL_RTNATIVETHREAD);
    if (pVM)
    {
        PVMCPUCC pVCpu = VMMGetCpu(pVM);

        if (pVCpu)
        {
# ifdef RT_ARCH_X86
            if (    pVCpu->vmm.s.CallRing3JmpBufR0.eip
                &&  !pVCpu->vmm.s.CallRing3JmpBufR0.fInRing3Call)
# else
            if (    pVCpu->vmm.s.CallRing3JmpBufR0.rip
                &&  !pVCpu->vmm.s.CallRing3JmpBufR0.fInRing3Call)
# endif
            {
                int rc = VMMRZCallRing3(pVM, pVCpu, VMMCALLRING3_VM_R0_ASSERTION, 0);
                return RT_FAILURE_NP(rc);
            }
        }
    }
# ifdef RT_OS_LINUX
    return true;
# else
    return false;
# endif
#endif
}


/*
 * Override this so we can push it up to ring-3.
 */
DECLEXPORT(void) RTCALL RTAssertMsg1Weak(const char *pszExpr, unsigned uLine, const char *pszFile, const char *pszFunction)
{
    /*
     * To the log.
     */
    LogAlways(("\n!!R0-Assertion Failed!!\n"
               "Expression: %s\n"
               "Location  : %s(%d) %s\n",
               pszExpr, pszFile, uLine, pszFunction));

    /*
     * To the global VMM buffer.
     */
    PVMCC pVM = GVMMR0GetVMByEMT(NIL_RTNATIVETHREAD);
    if (pVM)
        RTStrPrintf(pVM->vmm.s.szRing0AssertMsg1, sizeof(pVM->vmm.s.szRing0AssertMsg1),
                    "\n!!R0-Assertion Failed!!\n"
                    "Expression: %.*s\n"
                    "Location  : %s(%d) %s\n",
                    sizeof(pVM->vmm.s.szRing0AssertMsg1) / 4 * 3, pszExpr,
                    pszFile, uLine, pszFunction);

    /*
     * Continue the normal way.
     */
    RTAssertMsg1(pszExpr, uLine, pszFile, pszFunction);
}


/**
 * Callback for RTLogFormatV which writes to the ring-3 log port.
 * See PFNLOGOUTPUT() for details.
 */
static DECLCALLBACK(size_t) rtLogOutput(void *pv, const char *pachChars, size_t cbChars)
{
    for (size_t i = 0; i < cbChars; i++)
    {
        LogAlways(("%c", pachChars[i])); NOREF(pachChars);
    }

    NOREF(pv);
    return cbChars;
}


/*
 * Override this so we can push it up to ring-3.
 */
DECLEXPORT(void) RTCALL RTAssertMsg2WeakV(const char *pszFormat, va_list va)
{
    va_list vaCopy;

    /*
     * Push the message to the loggers.
     */
    PRTLOGGER pLog = RTLogGetDefaultInstance(); /* Don't initialize it here... */
    if (pLog)
    {
        va_copy(vaCopy, va);
        RTLogFormatV(rtLogOutput, pLog, pszFormat, vaCopy);
        va_end(vaCopy);
    }
    pLog = RTLogRelGetDefaultInstance();
    if (pLog)
    {
        va_copy(vaCopy, va);
        RTLogFormatV(rtLogOutput, pLog, pszFormat, vaCopy);
        va_end(vaCopy);
    }

    /*
     * Push it to the global VMM buffer.
     */
    PVMCC pVM = GVMMR0GetVMByEMT(NIL_RTNATIVETHREAD);
    if (pVM)
    {
        va_copy(vaCopy, va);
        RTStrPrintfV(pVM->vmm.s.szRing0AssertMsg2, sizeof(pVM->vmm.s.szRing0AssertMsg2), pszFormat, vaCopy);
        va_end(vaCopy);
    }

    /*
     * Continue the normal way.
     */
    RTAssertMsg2V(pszFormat, va);
}

