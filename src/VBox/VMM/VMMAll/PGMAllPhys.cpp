/* $Id$ */
/** @file
 * PGM - Page Manager and Monitor, Physical Memory Addressing.
 */

/*
 * Copyright (C) 2006-2007 innotek GmbH
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation,
 * in version 2 as it comes in the "COPYING" file of the VirtualBox OSE
 * distribution. VirtualBox OSE is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/** @def PGM_IGNORE_RAM_FLAGS_RESERVED
 * Don't respect the MM_RAM_FLAGS_RESERVED flag when converting to HC addresses.
 *
 * Since this flag is currently incorrectly kept set for ROM regions we will
 * have to ignore it for now so we don't break stuff.
 *
 * @todo this has been fixed now I believe, remove this hack.
 */
#define PGM_IGNORE_RAM_FLAGS_RESERVED


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_PGM_PHYS
#include <VBox/pgm.h>
#include <VBox/trpm.h>
#include <VBox/vmm.h>
#include <VBox/iom.h>
#include "PGMInternal.h"
#include <VBox/vm.h>
#include <VBox/param.h>
#include <VBox/err.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/asm.h>
#include <VBox/log.h>
#ifdef IN_RING3
# include <iprt/thread.h>
#endif



/**
 * Checks if Address Gate 20 is enabled or not.
 *
 * @returns true if enabled.
 * @returns false if disabled.
 * @param   pVM     VM handle.
 */
PGMDECL(bool) PGMPhysIsA20Enabled(PVM pVM)
{
    LogFlow(("PGMPhysIsA20Enabled %d\n", pVM->pgm.s.fA20Enabled));
    return !!pVM->pgm.s.fA20Enabled ; /* stupid MS compiler doesn't trust me. */
}


/**
 * Validates a GC physical address.
 *
 * @returns true if valid.
 * @returns false if invalid.
 * @param   pVM     The VM handle.
 * @param   GCPhys  The physical address to validate.
 */
PGMDECL(bool) PGMPhysIsGCPhysValid(PVM pVM, RTGCPHYS GCPhys)
{
    PPGMPAGE pPage = pgmPhysGetPage(&pVM->pgm.s, GCPhys);
    return pPage != NULL;
}


/**
 * Checks if a GC physical address is a normal page,
 * i.e. not ROM, MMIO or reserved.
 *
 * @returns true if normal.
 * @returns false if invalid, ROM, MMIO or reserved page.
 * @param   pVM     The VM handle.
 * @param   GCPhys  The physical address to check.
 */
PGMDECL(bool) PGMPhysIsGCPhysNormal(PVM pVM, RTGCPHYS GCPhys)
{
    PPGMPAGE pPage = pgmPhysGetPage(&pVM->pgm.s, GCPhys);
    return pPage
        && !(pPage->HCPhys & (MM_RAM_FLAGS_MMIO | MM_RAM_FLAGS_ROM | MM_RAM_FLAGS_RESERVED | MM_RAM_FLAGS_MMIO2));
}


/**
 * Converts a GC physical address to a HC physical address.
 *
 * @returns VINF_SUCCESS on success.
 * @returns VERR_PGM_PHYS_PAGE_RESERVED it it's a valid GC physical
 *          page but has no physical backing.
 * @returns VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS if it's not a valid
 *          GC physical address.
 *
 * @param   pVM     The VM handle.
 * @param   GCPhys  The GC physical address to convert.
 * @param   pHCPhys Where to store the HC physical address on success.
 */
PGMDECL(int) PGMPhysGCPhys2HCPhys(PVM pVM, RTGCPHYS GCPhys, PRTHCPHYS pHCPhys)
{
    PPGMPAGE pPage;
    int rc = pgmPhysGetPageEx(&pVM->pgm.s, GCPhys, &pPage);
    if (VBOX_FAILURE(rc))
        return rc;

#ifndef PGM_IGNORE_RAM_FLAGS_RESERVED
    if (RT_UNLIKELY(pPage->HCPhys & MM_RAM_FLAGS_RESERVED)) /** @todo PAGE FLAGS */
        return VERR_PGM_PHYS_PAGE_RESERVED;
#endif

    *pHCPhys = PGM_PAGE_GET_HCPHYS(pPage) | (GCPhys & PAGE_OFFSET_MASK);
    return VINF_SUCCESS;
}


#ifdef NEW_PHYS_CODE


/**
 * Replace a zero or shared page with new page that we can write to.
 *
 * @returns VBox status.
 * @todo    Define the return values and propagate them up the call tree..
 *
 * @param   pVM         The VM address.
 * @param   pPage       The physical page tracking structure.
 * @param   GCPhys      The address of the page.
 *
 * @remarks Called from within the PGM critical section.
 */
int pgmPhysAllocPage(PVM pVM, PPGMPAGE pPage, RTGCPHYS GCPhys)
{
    return VERR_NOT_IMPLEMENTED;
}


/**
 * Deal with pages that are not writable, i.e. not in the ALLOCATED state.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PGM_PHYS_PAGE_RESERVED it it's a valid page but has no physical backing.
 *
 * @param   pVM         The VM address.
 * @param   pPage       The physical page tracking structure.
 * @param   GCPhys      The address of the page.
 *
 * @remarks Called from within the PGM critical section.
 */
int pgmPhysPageMakeWritable(PVM pVM, PPGMPAGE pPage, RTGCPHYS GCPhys)
{
    switch (pPage->u2State)
    {
        case PGM_PAGE_STATE_WRITE_MONITORED:
            pPage->fWrittenTo = true;
            pPage->u2State = PGM_PAGE_STATE_WRITE_ALLOCATED;
            /* fall thru */
        case PGM_PAGE_STATE_ALLOCATED:
            return VINF_SUCCESS;

        /*
         * Zero pages can be dummy pages for MMIO or reserved memory,
         * so we need to check the flags before joining cause with
         * shared page replacement.
         */
        case PGM_PAGE_STATE_ZERO:
            if (    PGM_PAGE_IS_MMIO(pPage)
                ||  PGM_PAGE_IS_RESERVED(pPage))
                return VERR_PGM_PHYS_PAGE_RESERVED;
            /* fall thru */
        case PGM_PAGE_STATE_SHARED:
            return pgmPhysAllocPage(pVM, pPage, GCPhys);
    }
}


#ifdef IN_RING3

/**
 * Tree enumeration callback for dealing with age rollover.
 * It will perform a simple compression of the current age.
 */
static DECLCALLBACK(int) pgmR3PhysChunkAgeingRolloverCallback(PAVLU32NODECORE pNode, void *pvUser)
{
    /* ASSMES iNow = 4 */
    PPGMCHUNKR3MAPPING pChunk = (PPGMCHUNKR3MAPPING)pNode;
    if (pChunk->iAge >= UINT32_C(0xffffff00))
        pChunk->iAge = 3;
    else if (pChunk->iAge >= UINT32_C(0xfffff000))
        pChunk->iAge = 2;
    else if (pChunk->iAge)
        pChunk->iAge = 1;
    return 0;
}


/**
 * Tree enumeration callback that updates the chunks that have
 * been used since the last
 */
static DECLCALLBACK(int) pgmR3PhysChunkAgeingCallback(PAVLU32NODECORE pNode, void *pvUser)
{
    PPGMCHUNKR3MAPPING pChunk = (PPGMCHUNKR3MAPPING)pNode;
    if (!pChunk->iAge)
    {
        PVM pVM = (PVM)pvUser;
        RTAvllU32Remove(&pVM->pgm.s.R3ChunkTlb.pAgeTree, pChunk->AgeCore.Key);
        pChunk->AgeCore.Key = pChunk->iAge = pVM->pgm.s.R3ChunkTlb.iNow;
        RTAvllU32Insert(&pVM->pgm.s.R3ChunkTlb.pAgeTree, &pChunk->AgeCore);
    }

    return 0;
}


/**
 * Performs ageing of the ring-3 chunk mappings.
 *
 * @param   pVM         The VM handle.
 */
PGMR3DECL(void) PGMR3PhysChunkAgeing(PVM pVM)
{
    pVM->pgm.s.R3ChunkMap.AgeingCountdown = RT_MIN(pVM->pgm.s.R3ChunkMap.cMax / 4, 1024);
    pVM->pgm.s.R3ChunkMap.iNow++;
    if (pVM->pgm.s.R3ChunkMap.iNow == 0)
    {
        pVM->pgm.s.R3ChunkMap.iNow = 20;
        RTAvlU32DoWithAll(&pVM->pgm.s.R3ChunkMap.pTree, true /*fFromLeft*/, pgmR3PhysChunkAgeingRolloverCallback, NULL);
    }
    RTAvlU32DoWithAll(&pVM->pgm.s.R3ChunkMap.pTree, true /*fFromLeft*/, pgmR3PhysChunkAgeingCallback, pVM);
}


/**
 * The structure passed in the pvUser argument of pgmR3PhysChunkUnmapCandidateCallback().
 */
typedef struct PGMR3PHYSCHUNKUNMAPCB
{
    PVM                 pVM;            /**< The VM handle. */
    PPGMR3CHUNKMAP      pChunk;         /**< The chunk to unmap. */
} PGMR3PHYSCHUNKUNMAPCB, *PPGMR3PHYSCHUNKUNMAPCB;


/**
 * Callback used to find the mapping that's been unused for
 * the longest time.
 */
static DECLCALLBACK(int) pgmR3PhysChunkUnmapCandidateCallback(PAVLLU32NODECORE pNode, void *pvUser)
{
    do
    {
        PPGMR3CHUNKMAP pChunk = (PPGMR3CHUNKMAP)((uint8_t *)pNode - RT_OFFSETOF(PGMR3CHUNKMAP, AgeCore));
        if (    pChunk->iAge
            &&  !pChunk->cRefs)
        {
            /*
             * Check that it's not in any of the TLBs.
             */
            PVM pVM = ((PPGMR3PHYSCHUNKUNMAPCB)pvUser)->pVM;
            for (unsigned i = 0; i < RT_ELEMENTS(pVM->pgm.s.R3ChunkTlb->aEntries); i++)
                if (pVM->pgm.s.R3ChunkTlb->aEntries[i].pChunk == pChunk)
                {
                    pChunk = NULL;
                    break;
                }
            if (pChunk)
                for (unsigned i = 0; i < RT_ELEMENTS(pVM->pgm.s.CTXSUFF(PhysTlb)->aEntries); i++)
                    if (pVM->pgm.s.CTXSUFF(PhysTlb)->aEntries[i].pChunk == pChunk)
                    {
                        pChunk = NULL;
                        break;
                    }
            if (pChunk)
            {
                ((PPGMR3PHYSCHUNKUNMAPCB)pvUser)->pChunk = pChunk;
                return 1; /* done */
            }
        }

        /* next with the same age - this version of the AVL API doesn't enumerate the list, so we have to do it. */
        pNode = pNode->pList;
    } while (pNode);
    return 0;
}


/**
 * Finds a good candidate for unmapping when the ring-3 mapping cache is full.
 *
 * The candidate will not be part of any TLBs, so no need to flush
 * anything afterwards.
 *
 * @returns Chunk id.
 * @param   pVM         The VM handle.
 */
int pgmR3PhysChunkFindUnmapCandidate(PVM pVM)
{
    /*
     * Do tree ageing first?
     */
    if (pVM->pgm.s.R3ChunkMap.AgeingCountdown-- == 0)
        pgmR3PhysChunkAgeing(pVM);

    /*
     * Enumerate the age tree starting with the left most node.
     */
    PGMR3PHYSCHUNKUNMAPCB Args;
    Args.pVM = pVM;
    Args.pChunk = NULL;
    if (RTAvlU32DoWithAll(&pVM->pgm.s.R3ChunkMap.pAgeTree, true /*fFromLeft*/, pgmR3PhysChunkUnmapCandidateCallback, pVM))
        return Args.pChunk->idChunk;
    return INT32_MAX;
}


/**
 * Maps the given chunk into the ring-3 mapping cache.
 *
 * This will call ring-0.
 *
 * @returns VBox status code.
 * @param   pVM         The VM handle.
 * @param   idChunk     The chunk in question.
 * @param   ppChunk     Where to store the chunk tracking structure.
 *
 * @remarks Called from within the PGM critical section.
 */
int pgmR3PhysChunkMap(PVM pVM, uint32_t idChunk, PPPGMCHUNKR3MAPPING ppChunk)
{
    /*
     * Allocate a new tracking structure first.
     */
#if 0 /* for later when we've got a separate mapping method for ring-0. */
    PPGMCHUNKR3MAPPING pChunk = (PPGMCHUNKR3MAPPING)MMR3HeapAlloc(pVM, MM_TAG_PGM_CHUNK_MAPPING, sizeof(*pChunk));
#else
    PPGMCHUNKR3MAPPING pChunk = (PPGMCHUNKR3MAPPING)MMHyperAlloc(pVM, MM_TAG_PGM_CHUNK_MAPPING, sizeof(*pChunk));
#endif
    AssertReturn(pChunk, VERR_NO_MEMORY);
    pChunk->Core.Key = idChunk;
    pChunk->pv = NULL;
    pChunk->cRefs = 0;
    pChunk->iAge = 0;

    /*
     * Request the ring-0 part to map the chunk in question and if
     * necessary unmap another one to make space in the mapping cache.
     */
    PGMMAPCHUNKREQ Req;
    Req.pvR3 = NULL;
    Req.idChunkMap = idChunck;
    Req.idChunkUnmap = INT32_MAX;
    if (pVM->pgm.R3ChunkMap.c >= pVM->pgm.R3ChunkMap.cMax)
        Req.idChunkUnmap = pgmR3PhysChunkFindUnmapCandidate(pVM);
    /** @todo SUPCallVMMR0Ex needs to support in+out or similar.  */
    int rc = SUPCallVMMR0Ex(pVM->pVMR0, VMMR0_DO_PGM_MAP_CHUNK, &Req, sizeof(Req));
    if (VBOX_SUCCESS(rc))
    {
        /*
         * Update the tree.
         */
        /* insert the new one. */
        AssertPtr(Req.pvR3);
        pChunk->pv = Req.pvR3;
        bool fRc = RTAvlU32Insert(&pVM->pgm.s.R3ChunkMap.Tree, &pChunk->Core);
        AssertRelease(fRc);
        pVM->pgm.s.R3ChunkMap.c++;

        /* remove the unmapped one. */
        if (Req.idChunkUnmap != INT32_MAX)
        {
            PPGMCHUNKR3MAPPING pUnmappedChunk = (PPGMCHUNKR3MAPPING)RTAvlU32Remove(&pVM->pgm.s.R3ChunkMap.Tree, Req.idChunkUnmap);
            AssertRelease(pUnmappedChunk);
            pUnmappedChunk->pv = NULL;
            pUnmappedChunk->Key = INT32_MAX;
#if 0 /* for later when we've got a separate mapping method for ring-0. */
            MMR3HeapFree(pUnmappedChunk);
#else
            MMHyperFree(pVM, pUnmappedChunk);
#endif
            pVM->pgm.R3ChunkMap.c--;
        }
    }
    else
    {
        AssertRC(rc);
#if 0 /* for later when we've got a separate mapping method for ring-0. */
        MMR3HeapFree(pChunk);
#else
        MMHyperFree(pVM, pChunk);
#endif
        pChunk = NULL;
    }

    *ppChunk = pChunk;
    return rc;
}
#endif /* IN_RING3 */


/**
 * Maps a page into the current virtual address space so it can be accessed.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PGM_PHYS_PAGE_RESERVED it it's a valid page but has no physical backing.
 *
 * @param   pVM         The VM address.
 * @param   pPage       The physical page tracking structure.
 * @param   GCPhys      The address of the page.
 * @param   ppMap       Where to store the address of the mapping tracking structure.
 * @param   ppv         Where to store the mapping address of the page. The page
 *                      offset is masked off!
 *
 * @remarks Called from within the PGM critical section.
 */
int pgmPhysPageMap(PVM pVM, PPGMPAGE pPage, RTGCPHYS GCPhys, PPPGMPAGEMAP ppMap, void **ppv)
{
#ifdef IN_GC
    /*
     * Just some sketchy GC code.
     */
    *ppMap = NULL;
    RTHCPHYS HCPhys = pPage->HCPhys & PGM_HCPHYS_PAGE_MASK;
    Assert(HCPhys != pVM->pgm.s.HCPhysZeroPg)
    return PGMGCDynMapHCPage(pVM, HCPhys, ppv);

#else /* IN_RING3 || IN_RING0 */

/**
 * Calculates the index of a guest page in the Ring-3 Chunk TLB.
 * @returns Chunk TLB index.
 * @param   idChunk         The Chunk ID.
 */
#define PGM_R3CHUNKTLB_IDX(idChunk)     ( (idChunk) & (PGM_R3CHUNKTLB_ENTRIES - 1) )

    /*
     * Find/make Chunk TLB entry for the mapping chunk.
     */
    PPGMR3CHUNK pChunk;
    const uint32_t idChunk = PGM_PAGE_GET_PAGEID(pPage) >> XXX_CHUNKID_SHIFT;
    PGMR3CHUNKTLBE pTlbe = &pVM->pgm.s.R3ChunkTlb.aEntries[PGM_R3CHUNKTLB_IDX(idChunk)];
    if (pTlbe->idChunk == idChunk)
    {
        STAM_COUNTER_INC(&pVM->pgm.s.StatR3ChunkTlbHits);
        pChunk = pTlbe->pChunk;
    }
    else
    {
        STAM_COUNTER_INC(&pVM->pgm.s.StatR3ChunkTlbMisses);

        /*
         * Find the chunk, map it if necessary.
         */
        pChunk = (PPGMR3CHUNK)RTAvlU32Get(&pVM->pgm.s.R3ChunkMap.Tree, idChunk);
        if (!pChunk)
        {
#ifdef IN_RING0
            int rc = VMMR0CallHost(pVM, VMMCALLHOST_PGM_MAP_CHUNK, idChunk);
            AssertRCReturn(rc, rc);
            pChunk = (PPGMR3CHUNK)RTAvlU32Get(&pVM->pgm.s.R3ChunkMap.Tree, idChunk);
            Assert(pChunk);
#else
            int rc = pgmR3PhysChunkMap(pVM, idChunk, &pChunk);
            if (VBOX_FAILURE(rc))
                return rc;
#endif
        }

        /*
         * Enter it into the Chunk TLB.
         */
        pTlbe->idChunk = idChunk;
        pTlbe->pChunk = pChunk;
        pChunk->iAge = 0;
    }

    *ppv = (uint8_t *)pMap->pv + (iPage << PAGE_SHIFT);
    *ppMap = pChunk;
    return VINF_SUCCESS;
#endif /* IN_RING3 */
}


/**
 * Calculates the index of a guest page in the Physical TLB.
 * @returns Physical TLB index.
 * @param   GCPhys      The guest physical address.
 */
#define PGM_R3PHYSTLB_IDX(GCPhys) ( ((GCPhys) >> PAGE_SHIFT) & (PGM_R3PHYSTLB_ENTRIES - 1) )

#if defined(IN_RING3) || defined(IN_RING0)
# define PGM_PHYSTLB_IDX(GCPhys)   PGM_R3PHYSTLB_IDX(GCPhys)
# define PGMPHYSTLBE PGMR3PHYSTLBE
#else /* IN_GC */
# define PGM_PHYSTLB_IDX(GCPhys)   PGM_GCPHYSTLB_IDX(GCPhys)
# define PGMPHYSTLBE PGMGCPHYSTLBE
#endif


/**
 * Load a guest page into the ring-3 physical TLB.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success
 * @retval  VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS if it's not a valid physical address.
 * @param   pPGM        The PGM instance pointer.
 * @param   GCPhys      The guest physical address in question.
 */
int pgmPhysPageLoadIntoTlb(PPGM pPGM, RTGCPHYS GCPhys)
{
    STAM_COUNTER_INC(&pPGM->StatR3PhysTlbMisses);

    /*
     * Find the ram range.
     * 99.8% of requests are expected to be in the first range.
     */
    PPGMRAMRANGE pRam = CTXSUFF(pPGM->pRamRanges);
    RTGCPHYS off = GCPhys - pRam->GCPhys;
    if (RT_UNLIKELY(off >= pRam->cb))
    {
        do
        {
            pRam = CTXSUFF(pRam->pNext);
            if (!pRam)
                return VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS;
            off = GCPhys - pRam->GCPhys;
        } while (off >= pRam->cb);
    }

    /*
     * Map the page.
     * Make a special case for the zero page as it is kind of special.
     */
    PPGMPAGE pPage = &pRam->aPages[off >> PAGE_SHIFT];
    PPGMR3PHYSTLBE pTlbe = &pPGM->CTXSUFF(PhysTlb).aEntries[PGM_PHYSTLB_IDX(GCPhys)];
    if (PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_ZERO)
    {
        void *pv;
        PPGMPAGEMAP pMap;
        int rc = pgmPhysPageMap(pVM, pPage, GCPhys, &pMap, &pv);
        if (VBOX_FAILURE(rc))
            return rc;
        pTlbe->pMap = pMap;
        pTlbe->pv = pv;
    }
    else
    {
        Assert(PGM_PAGE_GET_HCPHYS(pPage) == pPGM->HCPhysZeroPg);
        pTlbe->pMap = NULL;
        pTlbe->pv = pPGM->pvZeroPgR3;
    }
    pTlbe->pPage = pPage;
    return VINF_SUCCESS;
}


/**
 * Queries the Physical TLB entry for a physical guest page,
 * attemting to load the TLB entry if necessary.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success
 * @retval  VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS if it's not a valid physical address.
 * @param   pPgm        The PGM instance handle.
 * @param   GCPhys      The address of the guest page.
 * @param   ppTlbe      Where to store the pointer to the TLB entry.
 */
DECLINLINE(int) pgmPhysPageQueryTlbe(PPGM pPgm, RTGCPHYS GCPhys, PPPGMPHYSTLBE ppTlbe)
{
    int rc;
    PGMPHYSTLBE pTlbe = &pPgm->CTXSUFF(PhysTlb).aEntries[PGM_PHYSTLB_IDX(GCPhys)];
    if (pTlbe->GCPhys == (GCPhys & X86_PTE_PAE_PG_MASK))
    {
        STAM_COUNTER_INC(&pPgm->StatR3PhysTlbHits);
        rc = VINF_SUCCESS;
    }
    else
        rc = pgmPhysPageLoadIntoTlb(pVM, GCPhys);
    *ppTlbe = pTlbe;
    return rc;
}


#endif /* NEW_PHYS_CODE */


/**
 * Requests the mapping of a guest page into the current context.
 *
 * This API should only be used for very short term, as it will consume
 * scarse resources (R0 and GC) in the mapping cache. When you're done
 * with the page, call PGMPhysGCPhys2CCPtrRelease() ASAP to release it.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PGM_PHYS_PAGE_RESERVED it it's a valid page but has no physical backing.
 * @retval  VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS if it's not a valid physical address.
 *
 * @param   pVM         The VM handle.
 * @param   GCPhys      The guest physical address of the page that should be mapped.
 * @param   ppv         Where to store the address corresponding to GCPhys.
 *
 * @remark  Avoid calling this API from within critical sections (other than
 *          the PGM one) because of the deadlock risk.
 */
PGMDECL(int) PGMPhysGCPhys2CCPtr(PVM pVM, RTGCPHYS GCPhys, void **ppv)
{
# ifdef NEW_PHYS_CODE
    int rc = pgmLock(pVM);
    AssertRCReturn(rc);

#ifdef IN_GC
    /* Until a physical TLB is implemented for GC, let PGMGCDynMapGCPageEx handle it. */
    return PGMGCDynMapGCPageEx(pVM, GCPhys, ppv);

#else
    /*
     * Query the Physical TLB entry for the page (may fail).
     */
    PGMPHYSTLBE pTlbe;
    int rc = pgmPhysPageQueryTlbe(&pVM->pgm.s, GCPhys, &pTlbe);
    if (RT_SUCCESS(rc))
    {
        /*
         * If the page is shared, the zero page, or being write monitored
         * it must be converted to an page that's writable if possible.
         */
        PPGMPAGE pPage = pTlbe->pPage;
        if (RT_UNLIKELY(pPage->u2State != PGM_PAGE_STATE_ALLOCATED))
            rc = pgmPhysPageMakeWritable(pVM, pPage, GCPhys);
        if (RT_SUCCESS(rc))
        {
            /*
             * Now, just perform the locking and calculate the return address.
             */
            PPGMPAGEMAP pMap = pTlbe->pMap;
            pMap->cRefs++;
            if (RT_LIKELY(pPage->cLocks != PGM_PAGE_MAX_LOCKS))
                if (RT_UNLIKELY(++pPage->cLocks == PGM_PAGE_MAX_LOCKS))
                {
                    AssertMsgFailed(("%VGp is entering permanent locked state!\n", GCPhys));
                    pMap->cRefs++; /* Extra ref to prevent it from going away. */
                }

            *ppv = (void *)((uintptr_t)pTlbe->pv | (GCPhys & PAGE_OFFSET_MASK));
        }
    }

    pgmUnlock(pVM);
    return rc;

#endif /* IN_RING3 || IN_RING0 */

#else
    /*
     * Temporary fallback code.
     */
# ifdef IN_GC
    return PGMGCDynMapGCPageEx(pVM, GCPhys, ppv);
# else
    return PGMPhysGCPhys2HCPtr(pVM, GCPhys, 1, ppv);
# endif
#endif
}


/**
 * Release the mapping of a guest page.
 *
 * This is the counterpart to the PGMPhysGCPhys2CCPtr.
 *
 * @param   pVM         The VM handle.
 * @param   GCPhys      The address that was mapped using PGMPhysGCPhys2CCPtr.
 * @param   pv          The address that PGMPhysGCPhys2CCPtr returned.
 */
PGMDECL(void) PGMPhysGCPhys2CCPtrRelease(PVM pVM, RTGCPHYS GCPhys, void *pv)
{
#ifdef NEW_PHYS_CODE
#ifdef IN_GC
    /* currently nothing to do here. */
/* --- postponed
#elif defined(IN_RING0)
*/

#else   /* IN_RING3 */
    pgmLock(pVM);

    /*
     * Try the Physical TLB cache.
     * There's a high likely hood that this will work out since it's a short-term lock.
     */
    PPGMR3PHYSTLBE pTlbe = &pVM->pgm.s.R3PhysTlb.aEntries[PGM_R3PHYSTLB_IDX(GCPhys)];
    if (RT_LIKELY(pTlbe->GCPhys == (GCPhys & X86_PTE_PAE_PG_MASK)))
    {
        PPGMPAGE pPage = pTlbe->pPage;
        Assert(PGM_PAGE_IS_NORMAL(pPage));
        Assert(pPage->cLocks >= 1);
        if (pPage->cLocks != PGM_PAGE_MAX_LOCKS)
            pPage->cLocks--;

        PPGMR3CHUNK pChunk = pTlbe->pChunk;
        Assert(pChunk->cRefs >= 1);
        pChunk->cRefs--;
        pChunk->iAge = 0;
    }
    else
    {
        /*
         * Find the page and unlock it.
         */
        PPGMRAMRANGE pRam = CTXSUFF(pVM->pgm.s.pRamRanges);
        RTGCPHYS off = GCPhys - pRam->GCPhys;
        if (RT_UNLIKELY(off >= pRam->cb))
        {
            do
            {
                pRam = CTXSUFF(pRam->pNext);
                AssertMsgRelease(pRam, ("GCPhys=%RGp\n", GCPhys));
                off = GCPhys - pRam->GCPhys;
            } while (off >= pRam->cb);
        }
        PPGMPAGE pPage = &pRam->aPages[off >> PAGE_SHIFT];
        Assert(PGM_PAGE_IS_NORMAL(pTlbe->pPage));
        Assert(pPage->cLocks >= 1);
        if (pPage->cLocks != PGM_PAGE_MAX_LOCKS)
            pPage->cLocks--;

        /*
         * Now find the chunk mapping and unlock it.
         */
        PPGMR3CHUNK pChunk;
        const uint32_t idChunk = PGM_PAGE_GET_PAGEID(pPage) >> XXX_CHUNKID_SHIFT;
        PGMR3CHUNKTLBE pTlbe = &pVM->pgm.s.R3ChunkTlb.aEntries[PGM_R3CHUNKTLB_IDX(idChunk)];
        if (pTlbe->idChunk == idChunk)
            pChunk = pTlbe->pChunk;
        else
        {
            pChunk = (PPGMR3CHUNK)RTAvlU32Get(&pVM->pgm.s.R3ChunkMap.Tree, idChunk);
            AssertMsgRelease(pChunk, ("GCPhys=%RGp\n", GCPhys));
            pChunk->iAge = 0;
        }
        Assert(pChunk->cRefs >= 1);
        pChunk->cRefs--;
    }

    pgmUnlock(pVM);
#endif /* IN_RING3 */
#else
    NOREF(pVM);
    NOREF(GCPhys);
    NOREF(pv);
#endif
}


/**
 * Converts a GC physical address to a HC pointer.
 *
 * @returns VINF_SUCCESS on success.
 * @returns VERR_PGM_PHYS_PAGE_RESERVED it it's a valid GC physical
 *          page but has no physical backing.
 * @returns VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS if it's not a valid
 *          GC physical address.
 * @returns VERR_PGM_GCPHYS_RANGE_CROSSES_BOUNDARY if the range crosses
 *          a dynamic ram chunk boundary
 * @param   pVM     The VM handle.
 * @param   GCPhys  The GC physical address to convert.
 * @param   cbRange Physical range
 * @param   pHCPtr  Where to store the HC pointer on success.
 */
PGMDECL(int) PGMPhysGCPhys2HCPtr(PVM pVM, RTGCPHYS GCPhys, RTUINT cbRange, PRTHCPTR pHCPtr)
{
#ifdef PGM_DYNAMIC_RAM_ALLOC
    if ((GCPhys & PGM_DYNAMIC_CHUNK_BASE_MASK) != ((GCPhys+cbRange-1) & PGM_DYNAMIC_CHUNK_BASE_MASK))
    {
        AssertMsgFailed(("%VGp - %VGp crosses a chunk boundary!!\n", GCPhys, GCPhys+cbRange));
        LogRel(("PGMPhysGCPhys2HCPtr %VGp - %VGp crosses a chunk boundary!!\n", GCPhys, GCPhys+cbRange));
        return VERR_PGM_GCPHYS_RANGE_CROSSES_BOUNDARY;
    }
#endif

    PPGMRAMRANGE pRam;
    PPGMPAGE pPage;
    int rc = pgmPhysGetPageAndRangeEx(&pVM->pgm.s, GCPhys, &pPage, &pRam);
    if (VBOX_FAILURE(rc))
        return rc;

#ifndef PGM_IGNORE_RAM_FLAGS_RESERVED
    if (RT_UNLIKELY(PGM_PAGE_IS_RESERVED(pPage)))
        return VERR_PGM_PHYS_PAGE_RESERVED;
#endif

    RTGCPHYS off = GCPhys - pRam->GCPhys;
    if (RT_UNLIKELY(off + cbRange > pRam->cb))
    {
        AssertMsgFailed(("%VGp - %VGp crosses a chunk boundary!!\n", GCPhys, GCPhys + cbRange));
        return VERR_PGM_GCPHYS_RANGE_CROSSES_BOUNDARY;
    }

    if (pRam->fFlags & MM_RAM_FLAGS_DYNAMIC_ALLOC)
    {
        unsigned iChunk = (off >> PGM_DYNAMIC_CHUNK_SHIFT);
        *pHCPtr = (RTHCPTR)((RTHCUINTPTR)CTXSUFF(pRam->pavHCChunk)[iChunk] + (off & PGM_DYNAMIC_CHUNK_OFFSET_MASK));
    }
    else if (RT_LIKELY(pRam->pvHC))
        *pHCPtr = (RTHCPTR)((RTHCUINTPTR)pRam->pvHC + off);
    else
        return VERR_PGM_PHYS_PAGE_RESERVED;
    return VINF_SUCCESS;
}


/**
 * Converts a guest pointer to a GC physical address.
 *
 * This uses the current CR3/CR0/CR4 of the guest.
 *
 * @returns VBox status code.
 * @param   pVM         The VM Handle
 * @param   GCPtr       The guest pointer to convert.
 * @param   pGCPhys     Where to store the GC physical address.
 */
PGMDECL(int) PGMPhysGCPtr2GCPhys(PVM pVM, RTGCPTR GCPtr, PRTGCPHYS pGCPhys)
{
    int rc = PGM_GST_PFN(GetPage,pVM)(pVM, (RTGCUINTPTR)GCPtr, NULL, pGCPhys);
    if (pGCPhys && VBOX_SUCCESS(rc))
        *pGCPhys |= (RTGCUINTPTR)GCPtr & PAGE_OFFSET_MASK;
    return rc;
}


/**
 * Converts a guest pointer to a HC physical address.
 *
 * This uses the current CR3/CR0/CR4 of the guest.
 *
 * @returns VBox status code.
 * @param   pVM         The VM Handle
 * @param   GCPtr       The guest pointer to convert.
 * @param   pHCPhys     Where to store the HC physical address.
 */
PGMDECL(int) PGMPhysGCPtr2HCPhys(PVM pVM, RTGCPTR GCPtr, PRTHCPHYS pHCPhys)
{
    RTGCPHYS GCPhys;
    int rc = PGM_GST_PFN(GetPage,pVM)(pVM, (RTGCUINTPTR)GCPtr, NULL, &GCPhys);
    if (VBOX_SUCCESS(rc))
        rc = PGMPhysGCPhys2HCPhys(pVM, GCPhys | ((RTGCUINTPTR)GCPtr & PAGE_OFFSET_MASK), pHCPhys);
    return rc;
}


/**
 * Converts a guest pointer to a HC pointer.
 *
 * This uses the current CR3/CR0/CR4 of the guest.
 *
 * @returns VBox status code.
 * @param   pVM         The VM Handle
 * @param   GCPtr       The guest pointer to convert.
 * @param   pHCPtr      Where to store the HC virtual address.
 */
PGMDECL(int) PGMPhysGCPtr2HCPtr(PVM pVM, RTGCPTR GCPtr, PRTHCPTR pHCPtr)
{
    RTGCPHYS GCPhys;
    int rc = PGM_GST_PFN(GetPage,pVM)(pVM, (RTGCUINTPTR)GCPtr, NULL, &GCPhys);
    if (VBOX_SUCCESS(rc))
        rc = PGMPhysGCPhys2HCPtr(pVM, GCPhys | ((RTGCUINTPTR)GCPtr & PAGE_OFFSET_MASK), 1 /* we always stay within one page */, pHCPtr);
    return rc;
}


/**
 * Converts a guest virtual address to a HC pointer by specfied CR3 and flags.
 *
 * @returns VBox status code.
 * @param   pVM         The VM Handle
 * @param   GCPtr       The guest pointer to convert.
 * @param   cr3         The guest CR3.
 * @param   fFlags      Flags used for interpreting the PD correctly: X86_CR4_PSE and X86_CR4_PAE
 * @param   pHCPtr      Where to store the HC pointer.
 *
 * @remark  This function is used by the REM at a time where PGM could
 *          potentially not be in sync. It could also be used by a
 *          future DBGF API to cpu state independent conversions.
 */
PGMDECL(int) PGMPhysGCPtr2HCPtrByGstCR3(PVM pVM, RTGCPTR GCPtr, uint32_t cr3, unsigned fFlags, PRTHCPTR pHCPtr)
{
    /*
     * PAE or 32-bit?
     */
    int rc;
    if (!(fFlags & X86_CR4_PAE))
    {
        PX86PD pPD;
        rc = PGM_GCPHYS_2_PTR(pVM, cr3 & X86_CR3_PAGE_MASK, &pPD);
        if (VBOX_SUCCESS(rc))
        {
            VBOXPDE Pde = pPD->a[(RTGCUINTPTR)GCPtr >> X86_PD_SHIFT];
            if (Pde.n.u1Present)
            {
                if ((fFlags & X86_CR4_PSE) && Pde.b.u1Size)
                {   /* (big page) */
                    rc = PGMPhysGCPhys2HCPtr(pVM, (Pde.u & X86_PDE4M_PG_MASK) | ((RTGCUINTPTR)GCPtr & X86_PAGE_4M_OFFSET_MASK), 1 /* we always stay within one page */, pHCPtr);
                }
                else
                {   /* (normal page) */
                    PVBOXPT pPT;
                    rc = PGM_GCPHYS_2_PTR(pVM, Pde.u & X86_PDE_PG_MASK, &pPT);
                    if (VBOX_SUCCESS(rc))
                    {
                        VBOXPTE Pte = pPT->a[((RTGCUINTPTR)GCPtr >> X86_PT_SHIFT) & X86_PT_MASK];
                        if (Pte.n.u1Present)
                            return PGMPhysGCPhys2HCPtr(pVM, (Pte.u & X86_PTE_PG_MASK) | ((RTGCUINTPTR)GCPtr & PAGE_OFFSET_MASK), 1 /* we always stay within one page */, pHCPtr);
                        rc = VERR_PAGE_NOT_PRESENT;
                    }
                }
            }
            else
                rc = VERR_PAGE_TABLE_NOT_PRESENT;
        }
    }
    else
    {
        /** @todo long mode! */
        PX86PDPTR pPdptr;
        rc = PGM_GCPHYS_2_PTR(pVM, cr3 & X86_CR3_PAE_PAGE_MASK, &pPdptr);
        if (VBOX_SUCCESS(rc))
        {
            X86PDPE Pdpe = pPdptr->a[((RTGCUINTPTR)GCPtr >> X86_PDPTR_SHIFT) & X86_PDPTR_MASK];
            if (Pdpe.n.u1Present)
            {
                PX86PDPAE pPD;
                rc = PGM_GCPHYS_2_PTR(pVM, Pdpe.u & X86_PDPE_PG_MASK, &pPD);
                if (VBOX_SUCCESS(rc))
                {
                    X86PDEPAE Pde = pPD->a[((RTGCUINTPTR)GCPtr >> X86_PD_PAE_SHIFT) & X86_PD_PAE_MASK];
                    if (Pde.n.u1Present)
                    {
                        if ((fFlags & X86_CR4_PSE) && Pde.b.u1Size)
                        {   /* (big page) */
                            rc = PGMPhysGCPhys2HCPtr(pVM, (Pde.u & X86_PDE4M_PAE_PG_MASK) | ((RTGCUINTPTR)GCPtr & X86_PAGE_4M_OFFSET_MASK), 1 /* we always stay within one page */, pHCPtr);
                        }
                        else
                        {   /* (normal page) */
                            PX86PTPAE pPT;
                            rc = PGM_GCPHYS_2_PTR(pVM, (Pde.u & X86_PDE_PAE_PG_MASK), &pPT);
                            if (VBOX_SUCCESS(rc))
                            {
                                X86PTEPAE Pte = pPT->a[((RTGCUINTPTR)GCPtr >> X86_PT_PAE_SHIFT) & X86_PT_PAE_MASK];
                                if (Pte.n.u1Present)
                                    return PGMPhysGCPhys2HCPtr(pVM, (Pte.u & X86_PTE_PAE_PG_MASK) | ((RTGCUINTPTR)GCPtr & PAGE_OFFSET_MASK), 1 /* we always stay within one page */, pHCPtr);
                                rc = VERR_PAGE_NOT_PRESENT;
                            }
                        }
                    }
                    else
                        rc = VERR_PAGE_TABLE_NOT_PRESENT;
                }
            }
            else
                rc = VERR_PAGE_TABLE_NOT_PRESENT;
        }
    }
    return rc;
}


#undef LOG_GROUP
#define LOG_GROUP LOG_GROUP_PGM_PHYS_ACCESS


#ifdef IN_RING3
/**
 * Cache PGMPhys memory access
 *
 * @param   pVM             VM Handle.
 * @param   pCache          Cache structure pointer
 * @param   GCPhys          GC physical address
 * @param   pbHC            HC pointer corresponding to physical page
 *
 * @thread  EMT.
 */
static void pgmPhysCacheAdd(PVM pVM, PGMPHYSCACHE *pCache, RTGCPHYS GCPhys, uint8_t *pbHC)
{
    uint32_t iCacheIndex;

    GCPhys = PAGE_ADDRESS(GCPhys);
    pbHC   = (uint8_t *)PAGE_ADDRESS(pbHC);

    iCacheIndex = ((GCPhys >> PAGE_SHIFT) & PGM_MAX_PHYSCACHE_ENTRIES_MASK);

    ASMBitSet(&pCache->aEntries, iCacheIndex);

    pCache->Entry[iCacheIndex].GCPhys = GCPhys;
    pCache->Entry[iCacheIndex].pbHC   = pbHC;
}
#endif

/**
 * Read physical memory.
 *
 * This API respects access handlers and MMIO. Use PGMPhysReadGCPhys() if you
 * want to ignore those.
 *
 * @param   pVM             VM Handle.
 * @param   GCPhys          Physical address start reading from.
 * @param   pvBuf           Where to put the read bits.
 * @param   cbRead          How many bytes to read.
 */
PGMDECL(void) PGMPhysRead(PVM pVM, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead)
{
#ifdef IN_RING3
    bool fGrabbedLock = false;
#endif

    AssertMsg(cbRead > 0, ("don't even think about reading zero bytes!\n"));
    if (cbRead == 0)
        return;

    LogFlow(("PGMPhysRead: %VGp %d\n", GCPhys, cbRead));

#ifdef IN_RING3
    if (!VM_IS_EMT(pVM))
    {
        pgmLock(pVM);
        fGrabbedLock = true;
    }
#endif

    /*
     * Copy loop on ram ranges.
     */
    PPGMRAMRANGE    pCur = CTXSUFF(pVM->pgm.s.pRamRanges);
    for (;;)
    {
        /* Find range. */
        while (pCur && GCPhys > pCur->GCPhysLast)
            pCur = CTXSUFF(pCur->pNext);
        /* Inside range or not? */
        if (pCur && GCPhys >= pCur->GCPhys)
        {
            /*
             * Must work our way thru this page by page.
             */
            RTGCPHYS off = GCPhys - pCur->GCPhys;
            while (off < pCur->cb)
            {
                unsigned iPage = off >> PAGE_SHIFT;
                PPGMPAGE pPage = &pCur->aPages[iPage];
                size_t   cb;

                /* Physical chunk in dynamically allocated range not present? */
                if (RT_UNLIKELY(!PGM_PAGE_GET_HCPHYS(pPage)))
                {
                    /* Treat it as reserved; return zeros */
                    cb = PAGE_SIZE - (off & PAGE_OFFSET_MASK);
                    if (cb >= cbRead)
                    {
                        memset(pvBuf, 0, cbRead);
                        goto end;
                    }
                    memset(pvBuf, 0, cb);
                }
                else
                {
                    switch (pPage->HCPhys & (MM_RAM_FLAGS_RESERVED | MM_RAM_FLAGS_MMIO | MM_RAM_FLAGS_VIRTUAL_ALL | MM_RAM_FLAGS_PHYSICAL_ALL | MM_RAM_FLAGS_ROM)) /** @todo PAGE FLAGS */
                    {
                        /*
                         * Normal memory or ROM.
                         */
                        case 0:
                        case MM_RAM_FLAGS_ROM:
                        case MM_RAM_FLAGS_ROM | MM_RAM_FLAGS_RESERVED:
                        //case MM_RAM_FLAGS_ROM | MM_RAM_FLAGS_MMIO2: /* = shadow */ - //MMIO2 isn't in the mask.
                        case MM_RAM_FLAGS_PHYSICAL_WRITE:
                        case MM_RAM_FLAGS_MMIO2 | MM_RAM_FLAGS_PHYSICAL_WRITE: // MMIO2 isn't in the mask.
                        case MM_RAM_FLAGS_VIRTUAL_WRITE:
                        {
#ifdef IN_GC
                            void *pvSrc = NULL;
                            PGMGCDynMapHCPage(pVM, PGM_PAGE_GET_HCPHYS(pPage), &pvSrc);
                            pvSrc = (char *)pvSrc + (off & PAGE_OFFSET_MASK);
#else
                            void *pvSrc = PGMRAMRANGE_GETHCPTR(pCur, off)
#endif
                            cb = PAGE_SIZE - (off & PAGE_OFFSET_MASK);
                            if (cb >= cbRead)
                            {
#if defined(IN_RING3) && defined(PGM_PHYSMEMACCESS_CACHING)
                                if (cbRead <= 4 && !fGrabbedLock /* i.e. EMT */)
                                    pgmPhysCacheAdd(pVM, &pVM->pgm.s.pgmphysreadcache, GCPhys, (uint8_t*)pvSrc);
#endif /* IN_RING3 && PGM_PHYSMEMACCESS_CACHING */
                                memcpy(pvBuf, pvSrc, cbRead);
                                goto end;
                            }
                            memcpy(pvBuf, pvSrc, cb);
                            break;
                        }

                        /*
                         * All reserved, nothing there.
                         */
                        case MM_RAM_FLAGS_RESERVED:
                            cb = PAGE_SIZE - (off & PAGE_OFFSET_MASK);
                            if (cb >= cbRead)
                            {
                                memset(pvBuf, 0, cbRead);
                                goto end;
                            }
                            memset(pvBuf, 0, cb);
                            break;

                        /*
                         * Physical handler.
                         */
                        case MM_RAM_FLAGS_PHYSICAL_ALL:
                        case MM_RAM_FLAGS_MMIO2 | MM_RAM_FLAGS_PHYSICAL_ALL: /** r=bird: MMIO2 isn't in the mask! */
                        {
                            int rc = VINF_PGM_HANDLER_DO_DEFAULT;
                            cb = PAGE_SIZE - (off & PAGE_OFFSET_MASK);
#ifdef IN_RING3 /** @todo deal with this in GC and R0! */

                            /* find and call the handler */
                            PPGMPHYSHANDLER pNode = (PPGMPHYSHANDLER)RTAvlroGCPhysRangeGet(&pVM->pgm.s.pTreesHC->PhysHandlers, GCPhys);
                            if (pNode && pNode->pfnHandlerR3)
                            {
                                size_t cbRange = pNode->Core.KeyLast - GCPhys + 1;
                                if (cbRange < cb)
                                    cb = cbRange;
                                if (cb > cbRead)
                                    cb = cbRead;

                                void *pvSrc = PGMRAMRANGE_GETHCPTR(pCur, off)

                                /** @note Dangerous assumption that HC handlers don't do anything that really requires an EMT lock! */
                                rc = pNode->pfnHandlerR3(pVM, GCPhys, pvSrc, pvBuf, cb, PGMACCESSTYPE_READ, pNode->pvUserR3);
                            }
#endif /* IN_RING3 */
                            if (rc == VINF_PGM_HANDLER_DO_DEFAULT)
                            {
#ifdef IN_GC
                                void *pvSrc = NULL;
                                PGMGCDynMapHCPage(pVM, PGM_PAGE_GET_HCPHYS(pPage), &pvSrc);
                                pvSrc = (char *)pvSrc + (off & PAGE_OFFSET_MASK);
#else
                                void *pvSrc = PGMRAMRANGE_GETHCPTR(pCur, off)
#endif

                                if (cb >= cbRead)
                                {
                                    memcpy(pvBuf, pvSrc, cbRead);
                                    goto end;
                                }
                                memcpy(pvBuf, pvSrc, cb);
                            }
                            else if (cb >= cbRead)
                                goto end;
                            break;
                        }

                        case MM_RAM_FLAGS_VIRTUAL_ALL:
                        {
                            int rc = VINF_PGM_HANDLER_DO_DEFAULT;
                            cb = PAGE_SIZE - (off & PAGE_OFFSET_MASK);
#ifdef IN_RING3 /** @todo deal with this in GC and R0! */
                            /* Search the whole tree for matching physical addresses (rather expensive!) */
                            PPGMVIRTHANDLER pNode;
                            unsigned iPage;
                            int rc2 = pgmHandlerVirtualFindByPhysAddr(pVM, GCPhys, &pNode, &iPage);
                            if (VBOX_SUCCESS(rc2) && pNode->pfnHandlerHC)
                            {
                                size_t cbRange = pNode->Core.KeyLast - GCPhys + 1;
                                if (cbRange < cb)
                                    cb = cbRange;
                                if (cb > cbRead)
                                    cb = cbRead;
                                RTGCUINTPTR GCPtr = ((RTGCUINTPTR)pNode->GCPtr & PAGE_BASE_GC_MASK)
                                                  + (iPage << PAGE_SHIFT) + (off & PAGE_OFFSET_MASK);

                                void *pvSrc = PGMRAMRANGE_GETHCPTR(pCur, off)

                                /** @note Dangerous assumption that HC handlers don't do anything that really requires an EMT lock! */
                                rc = pNode->pfnHandlerHC(pVM, (RTGCPTR)GCPtr, pvSrc, pvBuf, cb, PGMACCESSTYPE_READ, 0);
                            }
#endif /* IN_RING3 */
                            if (rc == VINF_PGM_HANDLER_DO_DEFAULT)
                            {
#ifdef IN_GC
                                void *pvSrc = NULL;
                                PGMGCDynMapHCPage(pVM, PGM_PAGE_GET_HCPHYS(pPage), &pvSrc);
                                pvSrc = (char *)pvSrc + (off & PAGE_OFFSET_MASK);
#else
                                void *pvSrc = PGMRAMRANGE_GETHCPTR(pCur, off)
#endif
                                if (cb >= cbRead)
                                {
                                    memcpy(pvBuf, pvSrc, cbRead);
                                    goto end;
                                }
                                memcpy(pvBuf, pvSrc, cb);
                            }
                            else if (cb >= cbRead)
                                goto end;
                            break;
                        }

                        /*
                         * The rest needs to be taken more carefully.
                         */
                        default:
#if 1                   /** @todo r=bird: Can you do this properly please. */
                            /** @todo Try MMIO; quick hack */
                            if (cbRead <= 4 && IOMMMIORead(pVM, GCPhys, (uint32_t *)pvBuf, cbRead) == VINF_SUCCESS)
                                goto end;
#endif

                            /** @todo fix me later. */
                            AssertReleaseMsgFailed(("Unknown read at %VGp size %d implement the complex physical reading case %x\n",
                                                    GCPhys, cbRead,
                                                    pPage->HCPhys & (MM_RAM_FLAGS_RESERVED | MM_RAM_FLAGS_MMIO | MM_RAM_FLAGS_VIRTUAL_ALL | MM_RAM_FLAGS_PHYSICAL_ALL | MM_RAM_FLAGS_ROM))); /** @todo PAGE FLAGS */
                            cb = PAGE_SIZE - (off & PAGE_OFFSET_MASK);
                            break;
                    }
                }
                cbRead -= cb;
                off    += cb;
                pvBuf   = (char *)pvBuf + cb;
            }

            GCPhys = pCur->GCPhysLast + 1;
        }
        else
        {
            LogFlow(("PGMPhysRead: Unassigned %VGp size=%d\n", GCPhys, cbRead));

            /*
             * Unassigned address space.
             */
            size_t cb;
            if (    !pCur
                ||  (cb = pCur->GCPhys - GCPhys) >= cbRead)
            {
                memset(pvBuf, 0, cbRead);
                goto end;
            }

            memset(pvBuf, 0, cb);
            cbRead -= cb;
            pvBuf   = (char *)pvBuf + cb;
            GCPhys += cb;
        }
    }
end:
#ifdef IN_RING3
    if (fGrabbedLock)
        pgmUnlock(pVM);
#endif
    return;
}

/**
 * Write to physical memory.
 *
 * This API respects access handlers and MMIO. Use PGMPhysReadGCPhys() if you
 * want to ignore those.
 *
 * @param   pVM             VM Handle.
 * @param   GCPhys          Physical address to write to.
 * @param   pvBuf           What to write.
 * @param   cbWrite         How many bytes to write.
 */
PGMDECL(void) PGMPhysWrite(PVM pVM, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite)
{
#ifdef IN_RING3
    bool fGrabbedLock = false;
#endif

    AssertMsg(!pVM->pgm.s.fNoMorePhysWrites, ("Calling PGMPhysWrite after pgmR3Save()!\n"));
    AssertMsg(cbWrite > 0, ("don't even think about writing zero bytes!\n"));
    if (cbWrite == 0)
        return;

    LogFlow(("PGMPhysWrite: %VGp %d\n", GCPhys, cbWrite));

#ifdef IN_RING3
    if (!VM_IS_EMT(pVM))
    {
        pgmLock(pVM);
        fGrabbedLock = true;
    }
#endif
    /*
     * Copy loop on ram ranges.
     */
    PPGMRAMRANGE    pCur = CTXSUFF(pVM->pgm.s.pRamRanges);
    for (;;)
    {
        /* Find range. */
        while (pCur && GCPhys > pCur->GCPhysLast)
            pCur = CTXSUFF(pCur->pNext);
        /* Inside range or not? */
        if (pCur && GCPhys >= pCur->GCPhys)
        {
            /*
             * Must work our way thru this page by page.
             */
            unsigned off = GCPhys - pCur->GCPhys;
            while (off < pCur->cb)
            {
                unsigned iPage = off >> PAGE_SHIFT;
                PPGMPAGE pPage = &pCur->aPages[iPage];

                /* Physical chunk in dynamically allocated range not present? */
                if (RT_UNLIKELY(!PGM_PAGE_GET_HCPHYS(pPage)))
                {
                    int rc;
#ifdef IN_RING3
                    if (fGrabbedLock)
                    {
                        pgmUnlock(pVM);
                        rc = pgmr3PhysGrowRange(pVM, GCPhys);
                        if (rc == VINF_SUCCESS)
                            PGMPhysWrite(pVM, GCPhys, pvBuf, cbWrite); /* try again; can't assume pCur is still valid (paranoia) */
                        return;
                    }
                    rc = pgmr3PhysGrowRange(pVM, GCPhys);
#else
                    rc = CTXALLMID(VMM, CallHost)(pVM, VMMCALLHOST_PGM_RAM_GROW_RANGE, GCPhys);
#endif
                    if (rc != VINF_SUCCESS)
                        goto end;
                }

                size_t   cb;
                /** @todo r=bird: missing MM_RAM_FLAGS_ROM here, we shall not allow anyone to overwrite the ROM! */
                switch (pPage->HCPhys & (MM_RAM_FLAGS_RESERVED | MM_RAM_FLAGS_MMIO | MM_RAM_FLAGS_MMIO2 | MM_RAM_FLAGS_VIRTUAL_ALL | MM_RAM_FLAGS_VIRTUAL_WRITE | MM_RAM_FLAGS_PHYSICAL_ALL | MM_RAM_FLAGS_PHYSICAL_WRITE)) /** @todo PAGE FLAGS */
                {
                    /*
                     * Normal memory, MMIO2 or writable shadow ROM.
                     */
                    case 0:
                    case MM_RAM_FLAGS_MMIO2:
                    case MM_RAM_FLAGS_ROM | MM_RAM_FLAGS_MMIO2: /* shadow rom */
                    {
#ifdef IN_GC
                        void *pvDst = NULL;
                        PGMGCDynMapHCPage(pVM, PGM_PAGE_GET_HCPHYS(pPage), &pvDst);
                        pvDst = (char *)pvDst + (off & PAGE_OFFSET_MASK);
#else
                        void *pvDst = PGMRAMRANGE_GETHCPTR(pCur, off)
#endif
                        cb = PAGE_SIZE - (off & PAGE_OFFSET_MASK);
                        if (cb >= cbWrite)
                        {
#if defined(IN_RING3) && defined(PGM_PHYSMEMACCESS_CACHING)
                            if (cbWrite <= 4 && !fGrabbedLock /* i.e. EMT */)
                                pgmPhysCacheAdd(pVM, &pVM->pgm.s.pgmphyswritecache, GCPhys, (uint8_t*)pvDst);
#endif /* IN_RING3 && PGM_PHYSMEMACCESS_CACHING */
                            memcpy(pvDst, pvBuf, cbWrite);
                            goto end;
                        }
                        memcpy(pvDst, pvBuf, cb);
                        break;
                    }

                    /*
                     * All reserved, nothing there.
                     */
                    case MM_RAM_FLAGS_RESERVED:
                    case MM_RAM_FLAGS_RESERVED | MM_RAM_FLAGS_MMIO2:
                        cb = PAGE_SIZE - (off & PAGE_OFFSET_MASK);
                        if (cb >= cbWrite)
                            goto end;
                        break;

                    /*
                     * Physical handler.
                     */
                    case MM_RAM_FLAGS_PHYSICAL_ALL:
                    case MM_RAM_FLAGS_PHYSICAL_WRITE:
                    case MM_RAM_FLAGS_MMIO2 | MM_RAM_FLAGS_PHYSICAL_ALL:
                    case MM_RAM_FLAGS_MMIO2 | MM_RAM_FLAGS_PHYSICAL_WRITE:
                    {
                        int rc = VINF_PGM_HANDLER_DO_DEFAULT;
                        cb = PAGE_SIZE - (off & PAGE_OFFSET_MASK);
#ifdef IN_RING3 /** @todo deal with this in GC and R0! */
                        /* find and call the handler */
                        PPGMPHYSHANDLER pNode = (PPGMPHYSHANDLER)RTAvlroGCPhysRangeGet(&pVM->pgm.s.pTreesHC->PhysHandlers, GCPhys);
                        if (pNode && pNode->pfnHandlerR3)
                        {
                            size_t cbRange = pNode->Core.KeyLast - GCPhys + 1;
                            if (cbRange < cb)
                                cb = cbRange;
                            if (cb > cbWrite)
                                cb = cbWrite;

                            void *pvDst = PGMRAMRANGE_GETHCPTR(pCur, off)

                            /** @note Dangerous assumption that HC handlers don't do anything that really requires an EMT lock! */
                            rc = pNode->pfnHandlerR3(pVM, GCPhys, pvDst, (void *)pvBuf, cb, PGMACCESSTYPE_WRITE, pNode->pvUserR3);
                        }
#endif /* IN_RING3 */
                        if (rc == VINF_PGM_HANDLER_DO_DEFAULT)
                        {
#ifdef IN_GC
                            void *pvDst = NULL;
                            PGMGCDynMapHCPage(pVM, PGM_PAGE_GET_HCPHYS(pPage), &pvDst);
                            pvDst = (char *)pvDst + (off & PAGE_OFFSET_MASK);
#else
                            void *pvDst = PGMRAMRANGE_GETHCPTR(pCur, off)
#endif
                            if (cb >= cbWrite)
                            {
                                memcpy(pvDst, pvBuf, cbWrite);
                                goto end;
                            }
                            memcpy(pvDst, pvBuf, cb);
                        }
                        else if (cb >= cbWrite)
                            goto end;
                        break;
                    }

                    case MM_RAM_FLAGS_VIRTUAL_ALL:
                    case MM_RAM_FLAGS_VIRTUAL_WRITE:
                    {
                        int rc = VINF_PGM_HANDLER_DO_DEFAULT;
                        cb = PAGE_SIZE - (off & PAGE_OFFSET_MASK);
#ifdef IN_RING3
/** @todo deal with this in GC and R0! */
                        /* Search the whole tree for matching physical addresses (rather expensive!) */
                        PPGMVIRTHANDLER pNode;
                        unsigned iPage;
                        int rc2 = pgmHandlerVirtualFindByPhysAddr(pVM, GCPhys, &pNode, &iPage);
                        if (VBOX_SUCCESS(rc2) && pNode->pfnHandlerHC)
                        {
                            size_t cbRange = pNode->Core.KeyLast - GCPhys + 1;
                            if (cbRange < cb)
                                cb = cbRange;
                            if (cb > cbWrite)
                                cb = cbWrite;
                            RTGCUINTPTR GCPtr = ((RTGCUINTPTR)pNode->GCPtr & PAGE_BASE_GC_MASK)
                                              + (iPage << PAGE_SHIFT) + (off & PAGE_OFFSET_MASK);

                            void *pvDst = PGMRAMRANGE_GETHCPTR(pCur, off)

                            /** @note Dangerous assumption that HC handlers don't do anything that really requires an EMT lock! */
                            rc = pNode->pfnHandlerHC(pVM, (RTGCPTR)GCPtr, pvDst, (void *)pvBuf, cb, PGMACCESSTYPE_WRITE, 0);
                        }
#endif /* IN_RING3 */
                        if (rc == VINF_PGM_HANDLER_DO_DEFAULT)
                        {
#ifdef IN_GC
                            void *pvDst = NULL;
                            PGMGCDynMapHCPage(pVM, PGM_PAGE_GET_HCPHYS(pPage), &pvDst);
                            pvDst = (char *)pvDst + (off & PAGE_OFFSET_MASK);
#else
                            void *pvDst = PGMRAMRANGE_GETHCPTR(pCur, off)
#endif
                            if (cb >= cbWrite)
                            {
                                memcpy(pvDst, pvBuf, cbWrite);
                                goto end;
                            }
                            memcpy(pvDst, pvBuf, cb);
                        }
                        else if (cb >= cbWrite)
                            goto end;
                        break;
                    }

                    /*
                     * Physical write handler + virtual write handler.
                     * Consider this a quick workaround for the CSAM + shadow caching problem.
                     *
                     * We hand it to the shadow caching first since it requires the unchanged
                     * data. CSAM will have to put up with it already being changed.
                     */
                    case MM_RAM_FLAGS_PHYSICAL_WRITE | MM_RAM_FLAGS_VIRTUAL_WRITE:
                    {
                        int rc = VINF_PGM_HANDLER_DO_DEFAULT;
                        cb = PAGE_SIZE - (off & PAGE_OFFSET_MASK);
#ifdef IN_RING3 /** @todo deal with this in GC and R0! */
                        /* 1. The physical handler */
                        PPGMPHYSHANDLER pPhysNode = (PPGMPHYSHANDLER)RTAvlroGCPhysRangeGet(&pVM->pgm.s.pTreesHC->PhysHandlers, GCPhys);
                        if (pPhysNode && pPhysNode->pfnHandlerR3)
                        {
                            size_t cbRange = pPhysNode->Core.KeyLast - GCPhys + 1;
                            if (cbRange < cb)
                                cb = cbRange;
                            if (cb > cbWrite)
                                cb = cbWrite;

                            void *pvDst = PGMRAMRANGE_GETHCPTR(pCur, off)

                            /** @note Dangerous assumption that HC handlers don't do anything that really requires an EMT lock! */
                            rc = pPhysNode->pfnHandlerR3(pVM, GCPhys, pvDst, (void *)pvBuf, cb, PGMACCESSTYPE_WRITE, pPhysNode->pvUserR3);
                        }

                        /* 2. The virtual handler (will see incorrect data) */
                        PPGMVIRTHANDLER pVirtNode;
                        unsigned iPage;
                        int rc2 = pgmHandlerVirtualFindByPhysAddr(pVM, GCPhys, &pVirtNode, &iPage);
                        if (VBOX_SUCCESS(rc2) && pVirtNode->pfnHandlerHC)
                        {
                            size_t cbRange = pVirtNode->Core.KeyLast - GCPhys + 1;
                            if (cbRange < cb)
                                cb = cbRange;
                            if (cb > cbWrite)
                                cb = cbWrite;
                            RTGCUINTPTR GCPtr = ((RTGCUINTPTR)pVirtNode->GCPtr & PAGE_BASE_GC_MASK)
                                              + (iPage << PAGE_SHIFT) + (off & PAGE_OFFSET_MASK);

                            void *pvDst = PGMRAMRANGE_GETHCPTR(pCur, off)

                            /** @note Dangerous assumption that HC handlers don't do anything that really requires an EMT lock! */
                            rc2 = pVirtNode->pfnHandlerHC(pVM, (RTGCPTR)GCPtr, pvDst, (void *)pvBuf, cb, PGMACCESSTYPE_WRITE, 0);
                            if (    (   rc2 != VINF_PGM_HANDLER_DO_DEFAULT
                                     && rc == VINF_PGM_HANDLER_DO_DEFAULT)
                                ||  (   VBOX_FAILURE(rc2)
                                     && VBOX_SUCCESS(rc)))
                                rc = rc2;
                        }
#endif /* IN_RING3 */
                        if (rc == VINF_PGM_HANDLER_DO_DEFAULT)
                        {
#ifdef IN_GC
                            void *pvDst = NULL;
                            PGMGCDynMapHCPage(pVM, PGM_PAGE_GET_HCPHYS(pPage), &pvDst);
                            pvDst = (char *)pvDst + (off & PAGE_OFFSET_MASK);
#else
                            void *pvDst = PGMRAMRANGE_GETHCPTR(pCur, off)
#endif
                            if (cb >= cbWrite)
                            {
                                memcpy(pvDst, pvBuf, cbWrite);
                                goto end;
                            }
                            memcpy(pvDst, pvBuf, cb);
                        }
                        else if (cb >= cbWrite)
                            goto end;
                        break;
                    }


                    /*
                     * The rest needs to be taken more carefully.
                     */
                    default:
#if 1                   /** @todo r=bird: Can you do this properly please. */
                        /** @todo Try MMIO; quick hack */
                        if (cbWrite <= 4 && IOMMMIOWrite(pVM, GCPhys, *(uint32_t *)pvBuf, cbWrite) == VINF_SUCCESS)
                            goto end;
#endif

                        /** @todo fix me later. */
                        AssertReleaseMsgFailed(("Unknown write at %VGp size %d implement the complex physical writing case %x\n",
                                                GCPhys, cbWrite,
                                                (pPage->HCPhys & (MM_RAM_FLAGS_RESERVED | MM_RAM_FLAGS_MMIO | MM_RAM_FLAGS_MMIO2 | MM_RAM_FLAGS_VIRTUAL_ALL | MM_RAM_FLAGS_VIRTUAL_WRITE | MM_RAM_FLAGS_PHYSICAL_ALL | MM_RAM_FLAGS_PHYSICAL_WRITE)))); /** @todo PAGE FLAGS */
                        /* skip the write */
                        cb = cbWrite;
                        break;
                }

                cbWrite -= cb;
                off    += cb;
                pvBuf   = (const char *)pvBuf + cb;
            }

            GCPhys = pCur->GCPhysLast + 1;
        }
        else
        {
            /*
             * Unassigned address space.
             */
            size_t cb;
            if (    !pCur
                ||  (cb = pCur->GCPhys - GCPhys) >= cbWrite)
                goto end;

            cbWrite -= cb;
            pvBuf   = (const char *)pvBuf + cb;
            GCPhys += cb;
        }
    }
end:
#ifdef IN_RING3
    if (fGrabbedLock)
        pgmUnlock(pVM);
#endif
    return;
}

#ifndef IN_GC /* Ring 0 & 3 only */

/**
 * Read from guest physical memory by GC physical address, bypassing
 * MMIO and access handlers.
 *
 * @returns VBox status.
 * @param   pVM         VM handle.
 * @param   pvDst       The destination address.
 * @param   GCPhysSrc   The source address (GC physical address).
 * @param   cb          The number of bytes to read.
 */
PGMDECL(int) PGMPhysReadGCPhys(PVM pVM, void *pvDst, RTGCPHYS GCPhysSrc, size_t cb)
{
    /*
     * Anything to be done?
     */
    if (!cb)
        return VINF_SUCCESS;

    /*
     * Loop ram ranges.
     */
    for (PPGMRAMRANGE pRam = CTXSUFF(pVM->pgm.s.pRamRanges);
         pRam;
         pRam = pRam->CTXSUFF(pNext))
    {
        RTGCPHYS off = GCPhysSrc - pRam->GCPhys;
        if (off < pRam->cb)
        {
            if (pRam->fFlags & MM_RAM_FLAGS_DYNAMIC_ALLOC)
            {
                /* Copy page by page as we're not dealing with a linear HC range. */
                for (;;)
                {
                    /* convert */
                    void *pvSrc;
                    int rc = pgmRamGCPhys2HCPtrWithRange(pVM, pRam, GCPhysSrc, &pvSrc);
                    if (VBOX_FAILURE(rc))
                        return rc;

                    /* copy */
                    size_t cbRead = PAGE_SIZE - ((RTGCUINTPTR)GCPhysSrc & PAGE_OFFSET_MASK);
                    if (cbRead >= cb)
                    {
                        memcpy(pvDst, pvSrc, cb);
                        return VINF_SUCCESS;
                    }
                    memcpy(pvDst, pvSrc, cbRead);

                    /* next */
                    cb         -= cbRead;
                    pvDst       = (uint8_t *)pvDst + cbRead;
                    GCPhysSrc  += cbRead;
                }
            }
            else if (pRam->pvHC)
            {
                /* read */
                size_t cbRead = pRam->cb - off;
                if (cbRead >= cb)
                {
                    memcpy(pvDst, (uint8_t *)pRam->pvHC + off, cb);
                    return VINF_SUCCESS;
                }
                memcpy(pvDst, (uint8_t *)pRam->pvHC + off, cbRead);

                /* next */
                cb        -= cbRead;
                pvDst      = (uint8_t *)pvDst + cbRead;
                GCPhysSrc += cbRead;
            }
            else
                return VERR_PGM_PHYS_PAGE_RESERVED;
        }
        else if (GCPhysSrc < pRam->GCPhysLast)
            break;
    }
    return VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS;
}


/**
 * Write to guest physical memory referenced by GC pointer.
 * Write memory to GC physical address in guest physical memory.
 *
 * This will bypass MMIO and access handlers.
 *
 * @returns VBox status.
 * @param   pVM         VM handle.
 * @param   GCPhysDst   The GC physical address of the destination.
 * @param   pvSrc       The source buffer.
 * @param   cb          The number of bytes to write.
 */
PGMDECL(int) PGMPhysWriteGCPhys(PVM pVM, RTGCPHYS GCPhysDst, const void *pvSrc, size_t cb)
{
    /*
     * Anything to be done?
     */
    if (!cb)
        return VINF_SUCCESS;

    LogFlow(("PGMPhysWriteGCPhys: %VGp %d\n", GCPhysDst, cb));

    /*
     * Loop ram ranges.
     */
    for (PPGMRAMRANGE pRam = CTXSUFF(pVM->pgm.s.pRamRanges);
         pRam;
         pRam = pRam->CTXSUFF(pNext))
    {
        RTGCPHYS off = GCPhysDst - pRam->GCPhys;
        if (off < pRam->cb)
        {
#ifdef NEW_PHYS_CODE
/** @todo PGMRamGCPhys2HCPtrWithRange. */
#endif
            if (pRam->fFlags & MM_RAM_FLAGS_DYNAMIC_ALLOC)
            {
                /* Copy page by page as we're not dealing with a linear HC range. */
                for (;;)
                {
                    /* convert */
                    void *pvDst;
                    int rc = pgmRamGCPhys2HCPtrWithRange(pVM, pRam, GCPhysDst, &pvDst);
                    if (VBOX_FAILURE(rc))
                        return rc;

                    /* copy */
                    size_t cbWrite = PAGE_SIZE - ((RTGCUINTPTR)GCPhysDst & PAGE_OFFSET_MASK);
                    if (cbWrite >= cb)
                    {
                        memcpy(pvDst, pvSrc, cb);
                        return VINF_SUCCESS;
                    }
                    memcpy(pvDst, pvSrc, cbWrite);

                    /* next */
                    cb         -= cbWrite;
                    pvSrc       = (uint8_t *)pvSrc + cbWrite;
                    GCPhysDst  += cbWrite;
                }
            }
            else if (pRam->pvHC)
            {
                /* write */
                size_t cbWrite = pRam->cb - off;
                if (cbWrite >= cb)
                {
                    memcpy((uint8_t *)pRam->pvHC + off, pvSrc, cb);
                    return VINF_SUCCESS;
                }
                memcpy((uint8_t *)pRam->pvHC + off, pvSrc, cbWrite);

                /* next */
                cb         -= cbWrite;
                GCPhysDst  += cbWrite;
                pvSrc       = (uint8_t *)pvSrc + cbWrite;
            }
            else
                return VERR_PGM_PHYS_PAGE_RESERVED;
        }
        else if (GCPhysDst < pRam->GCPhysLast)
            break;
    }
    return VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS;
}


/**
 * Read from guest physical memory referenced by GC pointer.
 *
 * This function uses the current CR3/CR0/CR4 of the guest and will
 * bypass access handlers and not set any accessed bits.
 *
 * @returns VBox status.
 * @param   pVM         VM handle.
 * @param   pvDst       The destination address.
 * @param   GCPtrSrc    The source address (GC pointer).
 * @param   cb          The number of bytes to read.
 */
PGMDECL(int) PGMPhysReadGCPtr(PVM pVM, void *pvDst, RTGCPTR GCPtrSrc, size_t cb)
{
    /*
     * Anything to do?
     */
    if (!cb)
        return VINF_SUCCESS;

    /*
     * Optimize reads within a single page.
     */
    if (((RTGCUINTPTR)GCPtrSrc & PAGE_OFFSET_MASK) + cb <= PAGE_SIZE)
    {
        void *pvSrc;
        int rc = PGMPhysGCPtr2HCPtr(pVM, GCPtrSrc, &pvSrc);
        if (VBOX_FAILURE(rc))
            return rc;
        memcpy(pvDst, pvSrc, cb);
        return VINF_SUCCESS;
    }

    /*
     * Page by page.
     */
    for (;;)
    {
        /* convert */
        void *pvSrc;
        int rc = PGMPhysGCPtr2HCPtr(pVM, GCPtrSrc, &pvSrc);
        if (VBOX_FAILURE(rc))
            return rc;

        /* copy */
        size_t cbRead = PAGE_SIZE - ((RTGCUINTPTR)GCPtrSrc & PAGE_OFFSET_MASK);
        if (cbRead >= cb)
        {
            memcpy(pvDst, pvSrc, cb);
            return VINF_SUCCESS;
        }
        memcpy(pvDst, pvSrc, cbRead);

        /* next */
        cb         -= cbRead;
        pvDst       = (uint8_t *)pvDst + cbRead;
        GCPtrSrc   += cbRead;
    }
}


/**
 * Write to guest physical memory referenced by GC pointer.
 *
 * This function uses the current CR3/CR0/CR4 of the guest and will
 * bypass access handlers and not set dirty or accessed bits.
 *
 * @returns VBox status.
 * @param   pVM         VM handle.
 * @param   GCPtrDst    The destination address (GC pointer).
 * @param   pvSrc       The source address.
 * @param   cb          The number of bytes to write.
 */
PGMDECL(int) PGMPhysWriteGCPtr(PVM pVM, RTGCPTR GCPtrDst, const void *pvSrc, size_t cb)
{
    /*
     * Anything to do?
     */
    if (!cb)
        return VINF_SUCCESS;

    LogFlow(("PGMPhysWriteGCPtr: %VGv %d\n", GCPtrDst, cb));

    /*
     * Optimize writes within a single page.
     */
    if (((RTGCUINTPTR)GCPtrDst & PAGE_OFFSET_MASK) + cb <= PAGE_SIZE)
    {
        void *pvDst;
        int rc = PGMPhysGCPtr2HCPtr(pVM, GCPtrDst, &pvDst);
        if (VBOX_FAILURE(rc))
            return rc;
        memcpy(pvDst, pvSrc, cb);
        return VINF_SUCCESS;
    }

    /*
     * Page by page.
     */
    for (;;)
    {
        /* convert */
        void *pvDst;
        int rc = PGMPhysGCPtr2HCPtr(pVM, GCPtrDst, &pvDst);
        if (VBOX_FAILURE(rc))
            return rc;

        /* copy */
        size_t cbWrite = PAGE_SIZE - ((RTGCUINTPTR)GCPtrDst & PAGE_OFFSET_MASK);
        if (cbWrite >= cb)
        {
            memcpy(pvDst, pvSrc, cb);
            return VINF_SUCCESS;
        }
        memcpy(pvDst, pvSrc, cbWrite);

        /* next */
        cb         -= cbWrite;
        pvSrc       = (uint8_t *)pvSrc + cbWrite;
        GCPtrDst   += cbWrite;
    }
}

/**
 * Read from guest physical memory referenced by GC pointer.
 *
 * This function uses the current CR3/CR0/CR4 of the guest and will
 * respect access handlers and set accessed bits.
 *
 * @returns VBox status.
 * @param   pVM         VM handle.
 * @param   pvDst       The destination address.
 * @param   GCPtrSrc    The source address (GC pointer).
 * @param   cb          The number of bytes to read.
 */
/** @todo use the PGMPhysReadGCPtr name and rename the unsafe one to something appropriate */
PGMDECL(int) PGMPhysReadGCPtrSafe(PVM pVM, void *pvDst, RTGCPTR GCPtrSrc, size_t cb)
{
    RTGCPHYS    GCPhys;
    RTGCUINTPTR offset;
    int         rc;

    /*
     * Anything to do?
     */
    if (!cb)
        return VINF_SUCCESS;

    LogFlow(("PGMPhysReadGCPtrSafe: %VGv %d\n", GCPtrSrc, cb));

    /*
     * Optimize reads within a single page.
     */
    if (((RTGCUINTPTR)GCPtrSrc & PAGE_OFFSET_MASK) + cb <= PAGE_SIZE)
    {
        /* Convert virtual to physical address */
        rc = PGMPhysGCPtr2GCPhys(pVM, GCPtrSrc, &GCPhys);
        AssertRCReturn(rc, rc);

        /* mark the guest page as accessed. */
        rc = PGMGstModifyPage(pVM, GCPtrSrc, 1, X86_PTE_A, ~(uint64_t)(X86_PTE_A));
        AssertRC(rc);

        PGMPhysRead(pVM, GCPhys, pvDst, cb);
        return VINF_SUCCESS;
    }

    /*
     * Page by page.
     */
    for (;;)
    {
        /* Convert virtual to physical address */
        rc = PGMPhysGCPtr2GCPhys(pVM, GCPtrSrc, &GCPhys);
        AssertRCReturn(rc, rc);

        /* mark the guest page as accessed. */
        int rc = PGMGstModifyPage(pVM, GCPtrSrc, 1, X86_PTE_A, ~(uint64_t)(X86_PTE_A));
        AssertRC(rc);

        /* copy */
        size_t cbRead = PAGE_SIZE - ((RTGCUINTPTR)GCPtrSrc & PAGE_OFFSET_MASK);
        if (cbRead >= cb)
        {
            PGMPhysRead(pVM, GCPhys, pvDst, cb);
            return VINF_SUCCESS;
        }
        PGMPhysRead(pVM, GCPhys, pvDst, cbRead);

        /* next */
        cb         -= cbRead;
        pvDst       = (uint8_t *)pvDst + cbRead;
        GCPtrSrc   += cbRead;
    }
}


/**
 * Write to guest physical memory referenced by GC pointer.
 *
 * This function uses the current CR3/CR0/CR4 of the guest and will
 * respect access handlers and set dirty and accessed bits.
 *
 * @returns VBox status.
 * @param   pVM         VM handle.
 * @param   GCPtrDst    The destination address (GC pointer).
 * @param   pvSrc       The source address.
 * @param   cb          The number of bytes to write.
 */
/** @todo use the PGMPhysWriteGCPtr name and rename the unsafe one to something appropriate */
PGMDECL(int) PGMPhysWriteGCPtrSafe(PVM pVM, RTGCPTR GCPtrDst, const void *pvSrc, size_t cb)
{
    RTGCPHYS    GCPhys;
    RTGCUINTPTR offset;
    int         rc;

    /*
     * Anything to do?
     */
    if (!cb)
        return VINF_SUCCESS;

    LogFlow(("PGMPhysWriteGCPtrSafe: %VGv %d\n", GCPtrDst, cb));

    /*
     * Optimize writes within a single page.
     */
    if (((RTGCUINTPTR)GCPtrDst & PAGE_OFFSET_MASK) + cb <= PAGE_SIZE)
    {
        /* Convert virtual to physical address */
        rc = PGMPhysGCPtr2GCPhys(pVM, GCPtrDst, &GCPhys);
        AssertRCReturn(rc, rc);

        /* mark the guest page as accessed and dirty. */
        rc = PGMGstModifyPage(pVM, GCPtrDst, 1, X86_PTE_A | X86_PTE_D, ~(uint64_t)(X86_PTE_A | X86_PTE_D));
        AssertRC(rc);

        PGMPhysWrite(pVM, GCPhys, pvSrc, cb);
        return VINF_SUCCESS;
    }

    /*
     * Page by page.
     */
    for (;;)
    {
        /* Convert virtual to physical address */
        rc = PGMPhysGCPtr2GCPhys(pVM, GCPtrDst, &GCPhys);
        AssertRCReturn(rc, rc);

        /* mark the guest page as accessed and dirty. */
        rc = PGMGstModifyPage(pVM, GCPtrDst, 1, X86_PTE_A | X86_PTE_D, ~(uint64_t)(X86_PTE_A | X86_PTE_D));
        AssertRC(rc);

        /* copy */
        size_t cbWrite = PAGE_SIZE - ((RTGCUINTPTR)GCPtrDst & PAGE_OFFSET_MASK);
        if (cbWrite >= cb)
        {
            PGMPhysWrite(pVM, GCPhys, pvSrc, cb);
            return VINF_SUCCESS;
        }
        PGMPhysWrite(pVM, GCPhys, pvSrc, cbWrite);

        /* next */
        cb         -= cbWrite;
        pvSrc       = (uint8_t *)pvSrc + cbWrite;
        GCPtrDst   += cbWrite;
    }
}

/**
 * Write to guest physical memory referenced by GC pointer and update the PTE.
 *
 * This function uses the current CR3/CR0/CR4 of the guest and will
 * bypass access handlers and set any dirty and accessed bits in the PTE.
 *
 * If you don't want to set the dirty bit, use PGMPhysWriteGCPtr().
 *
 * @returns VBox status.
 * @param   pVM         VM handle.
 * @param   GCPtrDst    The destination address (GC pointer).
 * @param   pvSrc       The source address.
 * @param   cb          The number of bytes to write.
 */
PGMDECL(int) PGMPhysWriteGCPtrDirty(PVM pVM, RTGCPTR GCPtrDst, const void *pvSrc, size_t cb)
{
    /*
     * Anything to do?
     */
    if (!cb)
        return VINF_SUCCESS;

    /*
     * Optimize writes within a single page.
     */
    if (((RTGCUINTPTR)GCPtrDst & PAGE_OFFSET_MASK) + cb <= PAGE_SIZE)
    {
        void *pvDst;
        int rc = PGMPhysGCPtr2HCPtr(pVM, GCPtrDst, &pvDst);
        if (VBOX_FAILURE(rc))
            return rc;
        memcpy(pvDst, pvSrc, cb);
        rc = PGMGstModifyPage(pVM, GCPtrDst, cb, X86_PTE_A | X86_PTE_D, ~(uint64_t)(X86_PTE_A | X86_PTE_D));
        AssertRC(rc);
        return VINF_SUCCESS;
    }

    /*
     * Page by page.
     */
    for (;;)
    {
        /* convert */
        void *pvDst;
        int rc = PGMPhysGCPtr2HCPtr(pVM, GCPtrDst, &pvDst);
        if (VBOX_FAILURE(rc))
            return rc;

        /* mark the guest page as accessed and dirty. */
        rc = PGMGstModifyPage(pVM, GCPtrDst, 1, X86_PTE_A | X86_PTE_D, ~(uint64_t)(X86_PTE_A | X86_PTE_D));
        AssertRC(rc);

        /* copy */
        size_t  cbWrite = PAGE_SIZE - ((RTGCUINTPTR)GCPtrDst & PAGE_OFFSET_MASK);
        if (cbWrite >= cb)
        {
            memcpy(pvDst, pvSrc, cb);
            return VINF_SUCCESS;
        }
        memcpy(pvDst, pvSrc, cbWrite);

        /* next */
        cb         -= cbWrite;
        GCPtrDst   += cbWrite;
        pvSrc       = (char *)pvSrc + cbWrite;
    }
}

#endif /* !IN_GC */



/**
 * Performs a read of guest virtual memory for instruction emulation.
 *
 * This will check permissions, raise exceptions and update the access bits.
 *
 * The current implementation will bypass all access handlers. It may later be
 * changed to at least respect MMIO.
 *
 *
 * @returns VBox status code suitable to scheduling.
 * @retval  VINF_SUCCESS if the read was performed successfully.
 * @retval  VINF_EM_RAW_GUEST_TRAP if an exception was raised but not dispatched yet.
 * @retval  VINF_TRPM_XCPT_DISPATCHED if an exception was raised and dispatched.
 *
 * @param   pVM         The VM handle.
 * @param   pCtxCore    The context core.
 * @param   pvDst       Where to put the bytes we've read.
 * @param   GCPtrSrc    The source address.
 * @param   cb          The number of bytes to read. Not more than a page.
 *
 * @remark  This function will dynamically map physical pages in GC. This may unmap
 *          mappings done by the caller. Be careful!
 */
PGMDECL(int) PGMPhysInterpretedRead(PVM pVM, PCPUMCTXCORE pCtxCore, void *pvDst, RTGCUINTPTR GCPtrSrc, size_t cb)
{
    Assert(cb <= PAGE_SIZE);

/** @todo r=bird: This isn't perfect!
 *  -# It's not checking for reserved bits being 1.
 *  -# It's not correctly dealing with the access bit.
 *  -# It's not respecting MMIO memory or any other access handlers.
 */
    /*
     * 1. Translate virtual to physical. This may fault.
     * 2. Map the physical address.
     * 3. Do the read operation.
     * 4. Set access bits if required.
     */
    int rc;
    unsigned cb1 = PAGE_SIZE - (GCPtrSrc & PAGE_OFFSET_MASK);
    if (cb <= cb1)
    {
        /*
         * Not crossing pages.
         */
        RTGCPHYS GCPhys;
        uint64_t fFlags;
        rc = PGM_GST_PFN(GetPage,pVM)(pVM, GCPtrSrc, &fFlags, &GCPhys);
        if (VBOX_SUCCESS(rc))
        {
            /** @todo we should check reserved bits ... */
            void *pvSrc;
            rc = PGM_GCPHYS_2_PTR(pVM, GCPhys, &pvSrc);
            switch (rc)
            {
                case VINF_SUCCESS:
Log(("PGMPhysInterpretedRead: pvDst=%p pvSrc=%p cb=%d\n", pvDst, (uint8_t *)pvSrc + (GCPtrSrc & PAGE_OFFSET_MASK), cb));
                    memcpy(pvDst, (uint8_t *)pvSrc + (GCPtrSrc & PAGE_OFFSET_MASK), cb);
                    break;
                case VERR_PGM_PHYS_PAGE_RESERVED:
                case VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS:
                    memset(pvDst, 0, cb);
                    break;
                default:
                    return rc;
            }

            /** @todo access bit emulation isn't 100% correct. */
            if (!(fFlags & X86_PTE_A))
            {
                rc = PGM_GST_PFN(ModifyPage,pVM)(pVM, GCPtrSrc, 1, X86_PTE_A, ~(uint64_t)X86_PTE_A);
                AssertRC(rc);
            }
            return VINF_SUCCESS;
        }
    }
    else
    {
        /*
         * Crosses pages.
         */
        unsigned cb2 = cb - cb1;
        uint64_t fFlags1;
        RTGCPHYS GCPhys1;
        uint64_t fFlags2;
        RTGCPHYS GCPhys2;
        rc = PGM_GST_PFN(GetPage,pVM)(pVM, GCPtrSrc, &fFlags1, &GCPhys1);
        if (VBOX_SUCCESS(rc))
            rc = PGM_GST_PFN(GetPage,pVM)(pVM, GCPtrSrc + cb1, &fFlags2, &GCPhys2);
        if (VBOX_SUCCESS(rc))
        {
            /** @todo we should check reserved bits ... */
AssertMsgFailed(("cb=%d cb1=%d cb2=%d GCPtrSrc=%VGv\n", cb, cb1, cb2, GCPtrSrc));
            void *pvSrc1;
            rc = PGM_GCPHYS_2_PTR(pVM, GCPhys1, &pvSrc1);
            switch (rc)
            {
                case VINF_SUCCESS:
                    memcpy(pvDst, (uint8_t *)pvSrc1 + (GCPtrSrc & PAGE_OFFSET_MASK), cb1);
                    break;
                case VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS:
                    memset(pvDst, 0, cb1);
                    break;
                default:
                    return rc;
            }

            void *pvSrc2;
            rc = PGM_GCPHYS_2_PTR(pVM, GCPhys2, &pvSrc2);
            switch (rc)
            {
                case VINF_SUCCESS:
                    memcpy((uint8_t *)pvDst + cb2, pvSrc2, cb2);
                    break;
                case VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS:
                    memset((uint8_t *)pvDst + cb2, 0, cb2);
                    break;
                default:
                    return rc;
            }

            if (!(fFlags1 & X86_PTE_A))
            {
                rc = PGM_GST_PFN(ModifyPage,pVM)(pVM, GCPtrSrc, 1, X86_PTE_A, ~(uint64_t)X86_PTE_A);
                AssertRC(rc);
            }
            if (!(fFlags2 & X86_PTE_A))
            {
                rc = PGM_GST_PFN(ModifyPage,pVM)(pVM, GCPtrSrc + cb1, 1, X86_PTE_A, ~(uint64_t)X86_PTE_A);
                AssertRC(rc);
            }
            return VINF_SUCCESS;
        }
    }

    /*
     * Raise a #PF.
     */
    uint32_t uErr;

    /* Get the current privilege level. */
    uint32_t cpl = CPUMGetGuestCPL(pVM, pCtxCore);
    switch (rc)
    {
        case VINF_SUCCESS:
            uErr = (cpl >= 2) ? X86_TRAP_PF_RSVD | X86_TRAP_PF_US : X86_TRAP_PF_RSVD;
            break;

        case VERR_PAGE_NOT_PRESENT:
        case VERR_PAGE_TABLE_NOT_PRESENT:
            uErr = (cpl >= 2) ? X86_TRAP_PF_US : 0;
            break;

        default:
            AssertMsgFailed(("rc=%Vrc GCPtrSrc=%VGv cb=%#x\n", rc, GCPtrSrc, cb));
            return rc;
    }
    Log(("PGMPhysInterpretedRead: GCPtrSrc=%VGv cb=%#x -> #PF(%#x)\n", GCPtrSrc, cb, uErr));
    return TRPMRaiseXcptErrCR2(pVM, pCtxCore, X86_XCPT_PF, uErr, GCPtrSrc);
}

/// @todo PGMDECL(int) PGMPhysInterpretedWrite(PVM pVM, PCPUMCTXCORE pCtxCore, RTGCPTR GCPtrDst, const void *pvSrc, size_t cb)

