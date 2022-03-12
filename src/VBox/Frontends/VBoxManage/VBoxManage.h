/* $Id$ */
/** @file
 * VBoxManage - VirtualBox command-line interface, internal header file.
 */

/*
 * Copyright (C) 2006-2022 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef VBOX_INCLUDED_SRC_VBoxManage_VBoxManage_h
#define VBOX_INCLUDED_SRC_VBoxManage_VBoxManage_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifndef VBOX_ONLY_DOCS
#include <VBox/com/com.h>
#include <VBox/com/ptr.h>
#include <VBox/com/VirtualBox.h>
#include <VBox/com/string.h>
#include <VBox/com/array.h>
#endif /* !VBOX_ONLY_DOCS */

#include <iprt/types.h>
#include <iprt/message.h>
#include <iprt/stream.h>
#include <iprt/getopt.h>

#ifndef VBOX_ONLY_DOCS
# include "VBoxManageBuiltInHelp.h"
# include "PasswordInput.h"
#endif

#ifdef VBOX_WITH_VBOXMANAGE_NLS
# include "VirtualBoxTranslator.h"
#endif


////////////////////////////////////////////////////////////////////////////////
//
// definitions
//
////////////////////////////////////////////////////////////////////////////////

/**
 * This defines a a_CtxName::tr function that gives the translator context as
 * well as providing a shorter way to call VirtualBoxTranslator::translate.
 */
#ifdef VBOX_WITH_VBOXMANAGE_NLS
# define DECLARE_TRANSLATION_CONTEXT(a_CtxName) \
struct a_CtxName \
{ \
   static const char *tr(const char *pszSource, const char *pszComment = NULL, const size_t uNum = ~(size_t)0) \
   { \
       return VirtualBoxTranslator::translate(NULL, #a_CtxName, pszSource, pszComment, uNum); \
   } \
}
#else
# define DECLARE_TRANSLATION_CONTEXT(a_CtxName) \
struct a_CtxName \
{ \
   static const char *tr(const char *pszSource, const char *pszComment = NULL, const size_t uNum = ~(size_t)0) \
   { \
       RT_NOREF(pszComment, uNum); \
       return pszSource;  \
   } \
}
#endif

/**
 * Defines an option with two variants, producing two RTGETOPTDEF entries.
 *
 * This is mainly for replacing character-soup option names like
 * --natlocalhostreachable and --biossystemtimeoffset with more easily parsed
 * ones, like --nat-localhost-reachable and --bios-system-time-offset, without
 * removing the legacy name.
 */
#define OPT2(a_pszWordDashWord, a_pszWordSoup, a_chOptOrValue, a_fFlags) \
    { a_pszWordDashWord, a_chOptOrValue, a_fFlags }, \
    { a_pszWordSoup,     a_chOptOrValue, a_fFlags }

/** A single option variant of OPT2 for better looking tables. */
#define OPT1(a_pszOption, a_chOptOrValue, a_fFlags) \
    { a_pszOption, a_chOptOrValue, a_fFlags }


/** @name Syntax diagram category, i.e. the command.
 * @{ */
typedef enum
{
    USAGE_INVALID = 0,
    USAGE_MODIFYMEDIUM,
    USAGE_CREATEHOSTIF,
    USAGE_REMOVEHOSTIF,
    USAGE_GETEXTRADATA,
    USAGE_SETEXTRADATA,
    USAGE_SETPROPERTY,
    USAGE_USBFILTER,
    USAGE_I_LOADSYMS,
    USAGE_I_LOADMAP,
    USAGE_I_SETHDUUID,
    USAGE_CONVERTFROMRAW,
    USAGE_I_LISTPARTITIONS,
    USAGE_I_CREATERAWVMDK,
    USAGE_I_MODINSTALL,
    USAGE_I_MODUNINSTALL,
    USAGE_I_RENAMEVMDK,
#ifdef VBOX_WITH_GUEST_PROPS
    USAGE_GUESTPROPERTY,
#endif  /* VBOX_WITH_GUEST_PROPS defined */
    USAGE_I_CONVERTTORAW,
    USAGE_METRICS,
    USAGE_I_CONVERTHD,
    USAGE_HOSTONLYIFS,
    USAGE_I_DUMPHDINFO,
    USAGE_STORAGEATTACH,
#ifdef VBOX_WITH_GUEST_CONTROL
    USAGE_GUESTCONTROL,
#endif  /* VBOX_WITH_GUEST_CONTROL defined */
    USAGE_I_DEBUGLOG,
    USAGE_I_SETHDPARENTUUID,
    USAGE_I_PASSWORDHASH,
    USAGE_I_GUESTSTATS,
    USAGE_I_REPAIRHD,
    USAGE_NATNETWORK,
    USAGE_USBDEVSOURCE,
    /* Insert new entries before this line, but only if it is not an option
     * to go for the new style command and help handling (see e.g. extpack,
     * unattend or mediumio. */
    USAGE_S_NEWCMD = 10000, /**< new style command with no old help support */
    USAGE_S_ALL,
    USAGE_S_DUMPOPTS
} USAGECATEGORY;
/** @} */


#define HELP_SCOPE_USBFILTER_ADD        RT_BIT_64(0)
#define HELP_SCOPE_USBFILTER_MODIFY     RT_BIT_64(1)
#define HELP_SCOPE_USBFILTER_REMOVE     RT_BIT_64(2)

#ifdef VBOX_WITH_GUEST_CONTROL
# define HELP_SCOPE_GSTCTRL_RUN             RT_BIT(0)
# define HELP_SCOPE_GSTCTRL_START           RT_BIT(1)
# define HELP_SCOPE_GSTCTRL_COPYFROM        RT_BIT(2)
# define HELP_SCOPE_GSTCTRL_COPYTO          RT_BIT(3)
# define HELP_SCOPE_GSTCTRL_MKDIR           RT_BIT(4)
# define HELP_SCOPE_GSTCTRL_RMDIR           RT_BIT(5)
# define HELP_SCOPE_GSTCTRL_RM              RT_BIT(6)
# define HELP_SCOPE_GSTCTRL_MV              RT_BIT(7)
# define HELP_SCOPE_GSTCTRL_MKTEMP          RT_BIT(8)
# define HELP_SCOPE_GSTCTRL_LIST            RT_BIT(9)
# define HELP_SCOPE_GSTCTRL_CLOSEPROCESS    RT_BIT(10)
# define HELP_SCOPE_GSTCTRL_CLOSESESSION    RT_BIT(11)
# define HELP_SCOPE_GSTCTRL_STAT            RT_BIT(12)
# define HELP_SCOPE_GSTCTRL_UPDATEGA        RT_BIT(13)
# define HELP_SCOPE_GSTCTRL_WATCH           RT_BIT(14)
# define HELP_SCOPE_GSTCTRL_WAITRUNLEVEL    RT_BIT(15)
#endif

/** command handler argument */
struct HandlerArg
{
    int argc;
    char **argv;

#ifndef VBOX_ONLY_DOCS
    ComPtr<IVirtualBox> virtualBox;
    ComPtr<ISession> session;
#endif
};

/** flag whether we're in internal mode */
extern bool g_fInternalMode;

/** showVMInfo details */
typedef enum
{
    VMINFO_NONE             = 0,
    VMINFO_STANDARD         = 1,    /**< standard details */
    VMINFO_FULL             = 2,    /**< both */
    VMINFO_MACHINEREADABLE  = 3,    /**< both, and make it machine readable */
    VMINFO_COMPACT          = 4
} VMINFO_DETAILS;


////////////////////////////////////////////////////////////////////////////////
//
// global variables
//
////////////////////////////////////////////////////////////////////////////////

extern bool g_fDetailedProgress;        // in VBoxManage.cpp


////////////////////////////////////////////////////////////////////////////////
//
// prototypes
//
////////////////////////////////////////////////////////////////////////////////

/* VBoxManageHelp.cpp */

/* Legacy help infrastructure, to be replaced by new one using generated help. */
void printUsage(USAGECATEGORY enmCommand, uint64_t fSubcommandScope, PRTSTREAM pStrm);
RTEXITCODE errorSyntax(USAGECATEGORY enmCommand, const char *pszFormat, ...);
RTEXITCODE errorSyntaxEx(USAGECATEGORY enmCommand, uint64_t fSubcommandScope, const char *pszFormat, ...);
RTEXITCODE errorGetOpt(USAGECATEGORY enmCommand, int rc, union RTGETOPTUNION const *pValueUnion);
RTEXITCODE errorGetOptEx(USAGECATEGORY enmCommand, uint64_t fSubcommandScope, int rc, union RTGETOPTUNION const *pValueUnion);

void printUsageInternal(USAGECATEGORY enmCommand, PRTSTREAM pStrm);

#ifndef VBOX_ONLY_DOCS
void        setCurrentCommand(enum HELP_CMD_VBOXMANAGE enmCommand);
void        setCurrentSubcommand(uint64_t fCurSubcommandScope);

void        printUsage(PRTSTREAM pStrm);
void        printHelp(PRTSTREAM pStrm);
RTEXITCODE  errorNoSubcommand(void);
RTEXITCODE  errorUnknownSubcommand(const char *pszSubCmd);
RTEXITCODE  errorTooManyParameters(char **papszArgs);
RTEXITCODE  errorGetOpt(int rcGetOpt, union RTGETOPTUNION const *pValueUnion);
RTEXITCODE  errorFetchValue(int iValueNo, const char *pszOption, int rcGetOptFetchValue, union RTGETOPTUNION const *pValueUnion);
RTEXITCODE  errorSyntax(const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(1, 2);
RTEXITCODE  errorSyntaxV(const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(1, 0);
HRESULT     errorSyntaxHr(const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(1, 2);
RTEXITCODE  errorArgument(const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(1, 2);
HRESULT     errorArgumentHr(const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(1, 2);

# define SHOW_PROGRESS_NONE     0
# define SHOW_PROGRESS_DESC     RT_BIT_32(0)
# define SHOW_PROGRESS          RT_BIT_32(1)
# define SHOW_PROGRESS_DETAILS  RT_BIT_32(2)
HRESULT showProgress(ComPtr<IProgress> progress, uint32_t fFlags = SHOW_PROGRESS);
#endif

/* VBoxManage.cpp */
void showLogo(PRTSTREAM pStrm);

#ifndef VBOX_ONLY_DOCS
RTEXITCODE handleInternalCommands(HandlerArg *a);
#endif /* !VBOX_ONLY_DOCS */

/* VBoxManageControlVM.cpp */
RTEXITCODE handleControlVM(HandlerArg *a);

/* VBoxManageModifyVM.cpp */
#ifndef VBOX_ONLY_DOCS
void parseGroups(const char *pcszGroups, com::SafeArray<BSTR> *pGroups);
# ifdef VBOX_WITH_RECORDING
int parseScreens(const char *pcszScreens, com::SafeArray<BOOL> *pScreens);
# endif
#endif
RTEXITCODE handleModifyVM(HandlerArg *a);

/* VBoxManageDebugVM.cpp */
RTEXITCODE handleDebugVM(HandlerArg *a);

/* VBoxManageGuestProp.cpp */
extern void usageGuestProperty(PRTSTREAM pStrm, const char *pcszSep1, const char *pcszSep2);

/* VBoxManageGuestCtrl.cpp */
extern void usageGuestControl(PRTSTREAM pStrm, const char *pcszSep1, const char *pcszSep2, uint64_t fSubcommandScope);

#ifndef VBOX_ONLY_DOCS
/* VBoxManageGuestProp.cpp */
RTEXITCODE handleGuestProperty(HandlerArg *a);

/* VBoxManageGuestCtrl.cpp */
RTEXITCODE handleGuestControl(HandlerArg *a);

/* VBoxManageVMInfo.cpp */
HRESULT showSnapshots(ComPtr<ISnapshot> &rootSnapshot,
                      ComPtr<ISnapshot> &currentSnapshot,
                      VMINFO_DETAILS details,
                      const com::Utf8Str &prefix = "",
                      int level = 0);
RTEXITCODE handleShowVMInfo(HandlerArg *a);
HRESULT showVMInfo(ComPtr<IVirtualBox> pVirtualBox,
                   ComPtr<IMachine> pMachine,
                   ComPtr<ISession> pSession,
                   VMINFO_DETAILS details = VMINFO_NONE);
const char *machineStateToName(MachineState_T machineState, bool fShort);
HRESULT showBandwidthGroups(ComPtr<IBandwidthControl> &bwCtrl,
                            VMINFO_DETAILS details);
void outputMachineReadableString(const char *pszName, const char *pszValue, bool fQuoteName = false, bool fNewline = true);
void outputMachineReadableString(const char *pszName, com::Bstr const *pbstrValue, bool fQuoteName = false, bool fNewline = true);
void outputMachineReadableStringWithFmtName(const char *pszValue, bool fQuoteName, const char *pszNameFmt, ...) RT_IPRT_FORMAT_ATTR(3, 4);
void outputMachineReadableStringWithFmtName(com::Bstr const *pbstrValue, bool fQuoteName, const char *pszNameFmt, ...) RT_IPRT_FORMAT_ATTR(3, 4);
void outputMachineReadableBool(const char *pszName, BOOL const *pfValue);
void outputMachineReadableBool(const char *pszName, bool const *pfValue);
void outputMachineReadableULong(const char *pszName, ULONG *uValue);
void outputMachineReadableLong64(const char *pszName, LONG64 *uValue);

/* VBoxManageList.cpp */
RTEXITCODE handleList(HandlerArg *a);

/* VBoxManageMetrics.cpp */
RTEXITCODE handleMetrics(HandlerArg *a);

/* VBoxManageMisc.cpp */
RTEXITCODE handleRegisterVM(HandlerArg *a);
RTEXITCODE handleUnregisterVM(HandlerArg *a);
RTEXITCODE handleCreateVM(HandlerArg *a);
RTEXITCODE handleCloneVM(HandlerArg *a);
RTEXITCODE handleStartVM(HandlerArg *a);
RTEXITCODE handleDiscardState(HandlerArg *a);
RTEXITCODE handleAdoptState(HandlerArg *a);
RTEXITCODE handleGetExtraData(HandlerArg *a);
RTEXITCODE handleSetExtraData(HandlerArg *a);
RTEXITCODE handleSetProperty(HandlerArg *a);
RTEXITCODE handleSharedFolder(HandlerArg *a);
RTEXITCODE handleExtPack(HandlerArg *a);
RTEXITCODE handleUnattended(HandlerArg *a);
RTEXITCODE handleMoveVM(HandlerArg *a);
RTEXITCODE handleCloudProfile(HandlerArg *a);

/* VBoxManageDisk.cpp */
HRESULT openMedium(HandlerArg *a, const char *pszFilenameOrUuid,
                   DeviceType_T enmDevType, AccessMode_T enmAccessMode,
                   ComPtr<IMedium> &pMedium, bool fForceNewUuidOnOpen,
                   bool fSilent);
RTEXITCODE handleCreateMedium(HandlerArg *a);
RTEXITCODE handleModifyMedium(HandlerArg *a);
RTEXITCODE handleCloneMedium(HandlerArg *a);
RTEXITCODE handleMediumProperty(HandlerArg *a);
RTEXITCODE handleEncryptMedium(HandlerArg *a);
RTEXITCODE handleCheckMediumPassword(HandlerArg *a);
RTEXITCODE handleConvertFromRaw(HandlerArg *a);
HRESULT showMediumInfo(const ComPtr<IVirtualBox> &pVirtualBox,
                       const ComPtr<IMedium> &pMedium,
                       const char *pszParentUUID,
                       bool fOptLong);
RTEXITCODE handleShowMediumInfo(HandlerArg *a);
RTEXITCODE handleCloseMedium(HandlerArg *a);
RTEXITCODE handleMediumIO(HandlerArg *a);
int parseMediumType(const char *psz, MediumType_T *penmMediumType);
int parseBool(const char *psz, bool *pb);

/* VBoxManageStorageController.cpp */
RTEXITCODE handleStorageAttach(HandlerArg *a);
RTEXITCODE handleStorageController(HandlerArg *a);

// VBoxManageAppliance.cpp
RTEXITCODE handleImportAppliance(HandlerArg *a);
RTEXITCODE handleExportAppliance(HandlerArg *a);
RTEXITCODE handleSignAppliance(HandlerArg *a);

// VBoxManageSnapshot.cpp
RTEXITCODE handleSnapshot(HandlerArg *a);

/* VBoxManageUSB.cpp */
RTEXITCODE handleUSBFilter(HandlerArg *a);
RTEXITCODE handleUSBDevSource(HandlerArg *a);

/* VBoxManageHostonly.cpp */
RTEXITCODE handleHostonlyIf(HandlerArg *a);
#ifdef VBOX_WITH_VMNET
RTEXITCODE handleHostonlyNet(HandlerArg *a);
#endif /* VBOX_WITH_VMNET */

/* VBoxManageDHCPServer.cpp */
RTEXITCODE handleDHCPServer(HandlerArg *a);

/* VBoxManageNATNetwork.cpp */
RTEXITCODE handleNATNetwork(HandlerArg *a);
RTEXITCODE listNATNetworks(bool fLong, bool fSorted,
                           const ComPtr<IVirtualBox> &pVirtualBox);

/* VBoxManageBandwidthControl.cpp */
RTEXITCODE handleBandwidthControl(HandlerArg *a);

/* VBoxManageCloud.cpp */
RTEXITCODE handleCloud(HandlerArg *a);

/* VBoxManageCloudMachine.cpp */
RTEXITCODE handleCloudMachine(HandlerArg *a, int iFirst,
                              const char *pcszProviderName,
                              const char *pcszProfileName);
RTEXITCODE listCloudMachines(HandlerArg *a, int iFirst,
                              const char *pcszProviderName,
                              const char *pcszProfileName);
RTEXITCODE handleCloudShowVMInfo(HandlerArg *a, int iFirst,
                                 const char *pcszProviderName,
                                 const char *pcszProfileName);

/* VBoxManageUpdateCheck.cpp */
RTEXITCODE handleUpdateCheck(HandlerArg *a);

/* VBoxManageModifyNvram.cpp */
RTEXITCODE handleModifyNvram(HandlerArg *a);

#endif /* !VBOX_ONLY_DOCS */

#endif /* !VBOX_INCLUDED_SRC_VBoxManage_VBoxManage_h */
