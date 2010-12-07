/* $Id$ */
/** @file
 * VirtualBox Main - Extension Pack Utilities and definitions, VBoxC, VBoxSVC, ++.
 */

/*
 * Copyright (C) 2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ____H_EXTPACKUTIL
#define ____H_EXTPACKUTIL

#include <iprt/cpp/ministring.h>
#include <iprt/fs.h>
#include <iprt/vfs.h>


/** @name VBOX_EXTPACK_DESCRIPTION_NAME
 * The name of the description file in an extension pack.  */
#define VBOX_EXTPACK_DESCRIPTION_NAME   "ExtPack.xml"
/** @name VBOX_EXTPACK_DESCRIPTION_NAME
 * The name of the manifest file in an extension pack.  */
#define VBOX_EXTPACK_MANIFEST_NAME      "ExtPack.manifest"
/** @name VBOX_EXTPACK_SIGNATURE_NAME
 * The name of the signature file in an extension pack.  */
#define VBOX_EXTPACK_SIGNATURE_NAME     "ExtPack.signature"
/** @name VBOX_EXTPACK_SUFFIX
 * The suffix of a extension pack tarball. */
#define VBOX_EXTPACK_SUFFIX             ".vbox-extpack"

/** The minimum length (strlen) of a extension pack name. */
#define VBOX_EXTPACK_NAME_MIN_LEN       3
/** The max length (strlen) of a extension pack name. */
#define VBOX_EXTPACK_NAME_MAX_LEN       64

/** The architecture-dependent application data subdirectory where the
 * extension packs are installed.  Relative to RTPathAppPrivateArch. */
#define VBOX_EXTPACK_INSTALL_DIR        "ExtensionPacks"
/** The architecture-independent application data subdirectory where the
 * certificates are installed.  Relative to RTPathAppPrivateNoArch. */
#define VBOX_EXTPACK_CERT_DIR           "ExtPackCertificates"

/** The maximum entry name length.
 * Play short and safe. */
#define VBOX_EXTPACK_MAX_MEMBER_NAME_LENGTH 128


/**
 * Plug-in descriptor.
 */
typedef struct VBOXEXTPACKPLUGINDESC
{
    /** The name. */
    iprt::MiniString        strName;
    /** The module name. */
    iprt::MiniString        strModule;
    /** The description. */
    iprt::MiniString        strDescription;
    /** The frontend or component which it plugs into. */
    iprt::MiniString        strFrontend;
} VBOXEXTPACKPLUGINDESC;
/** Pointer to a plug-in descriptor. */
typedef VBOXEXTPACKPLUGINDESC *PVBOXEXTPACKPLUGINDESC;

/**
 * Extension pack descriptor
 *
 * This is the internal representation of the ExtPack.xml.
 */
typedef struct VBOXEXTPACKDESC
{
    /** The name. */
    iprt::MiniString        strName;
    /** The description. */
    iprt::MiniString        strDescription;
    /** The version string. */
    iprt::MiniString        strVersion;
    /** The internal revision number. */
    uint32_t                uRevision;
    /** The name of the main module. */
    iprt::MiniString        strMainModule;
    /** The name of the VRDE module, empty if none. */
    iprt::MiniString        strVrdeModule;
    /** The number of plug-in descriptors. */
    uint32_t                cPlugIns;
    /** Pointer to an array of plug-in descriptors. */
    PVBOXEXTPACKPLUGINDESC  paPlugIns;
} VBOXEXTPACKDESC;

/** Pointer to a extension pack descriptor. */
typedef VBOXEXTPACKDESC *PVBOXEXTPACKDESC;
/** Pointer to a const extension pack descriptor. */
typedef VBOXEXTPACKDESC const *PCVBOXEXTPACKDESC;


iprt::MiniString   *VBoxExtPackLoadDesc(const char *a_pszDir, PVBOXEXTPACKDESC a_pExtPackDesc, PRTFSOBJINFO a_pObjInfo);
iprt::MiniString   *VBoxExtPackLoadDescFromVfsFile(RTVFSFILE hVfsFile, PVBOXEXTPACKDESC a_pExtPackDesc, PRTFSOBJINFO a_pObjInfo);
iprt::MiniString   *VBoxExtPackExtractNameFromTarballPath(const char *pszTarball);
void                VBoxExtPackFreeDesc(PVBOXEXTPACKDESC a_pExtPackDesc);
bool                VBoxExtPackIsValidName(const char *pszName);
bool                VBoxExtPackIsValidMangledName(const char *pszMangledName, size_t cchMax = RTSTR_MAX);
iprt::MiniString   *VBoxExtPackMangleName(const char *pszName);
iprt::MiniString   *VBoxExtPackUnmangleName(const char *pszMangledName, size_t cbMax);
int                 VBoxExtPackCalcDir(char *pszExtPackDir, size_t cbExtPackDir, const char *pszParentDir, const char *pszName);
bool                VBoxExtPackIsValidVersionString(const char *pszName);
bool                VBoxExtPackIsValidModuleString(const char *pszModule);

int                 VBoxExtPackValidateMember(const char *pszName, RTVFSOBJTYPE enmType, RTVFSOBJ hVfsObj, char *pszError, size_t cbError);
int                 VBoxExtPackOpenTarFss(RTFILE hTarballFile, char *pszError, size_t cbError, PRTVFSFSSTREAM phTarFss);
int                 VBoxExtPackValidateTarball(RTFILE hTarballFile, const char *pszExtPackName, const char *pszTarball,
                                               char *pszError, size_t cbError, PRTMANIFEST phValidManifest, PRTVFSFILE phXmlFile);


#endif

