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


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "include/ExtPackUtil.h"

#include <iprt/ctype.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/manifest.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/vfs.h>
#include <iprt/tar.h>
#include <iprt/zip.h>
#include <iprt/cpp/xml.h>

#include <VBox/log.h>


/**
 * Worker for VBoxExtPackLoadDesc that loads the plug-in descriptors.
 *
 * @returns Same as VBoxExtPackLoadDesc.
 * @param   pVBoxExtPackElm
 * @param   pcPlugIns           Where to return the number of plug-ins in the
 *                              array.
 * @param   paPlugIns           Where to return the plug-in descriptor array.
 *                              (RTMemFree it even on failure)
 */
static iprt::MiniString *
vboxExtPackLoadPlugInDescs(const xml::ElementNode *pVBoxExtPackElm,
                           uint32_t *pcPlugIns, PVBOXEXTPACKPLUGINDESC *paPlugIns)
{
    *pcPlugIns = 0;
    *paPlugIns = NULL;

    /** @todo plug-ins */
    NOREF(pVBoxExtPackElm);

    return NULL;
}

/**
 * Clears the extension pack descriptor.
 *
 * @param   a_pExtPackDesc  The descriptor to clear.
 */
static void vboxExtPackClearDesc(PVBOXEXTPACKDESC a_pExtPackDesc)
{
    a_pExtPackDesc->strName.setNull();
    a_pExtPackDesc->strDescription.setNull();
    a_pExtPackDesc->strVersion.setNull();
    a_pExtPackDesc->uRevision = 0;
    a_pExtPackDesc->strMainModule.setNull();
    a_pExtPackDesc->strVrdeModule.setNull();
    a_pExtPackDesc->cPlugIns = 0;
    a_pExtPackDesc->paPlugIns = NULL;
}

/**
 * Load the extension pack descriptor from an XML document.
 *
 * @returns NULL on success, pointer to an error message on failure (caller
 *          deletes it).
 * @param   a_pDoc              Pointer to the the XML document.
 * @param   a_pExtPackDesc      Where to store the extension pack descriptor.
 */
static iprt::MiniString *vboxExtPackLoadDescFromDoc(xml::Document *a_pDoc, PVBOXEXTPACKDESC a_pExtPackDesc)
{
    /*
     * Get the main element and check its version.
     */
    const xml::ElementNode *pVBoxExtPackElm = a_pDoc->getRootElement();
    if (   !pVBoxExtPackElm
        || strcmp(pVBoxExtPackElm->getName(), "VirtualBoxExtensionPack") != 0)
        return new iprt::MiniString("No VirtualBoxExtensionPack element");

    iprt::MiniString strFormatVersion;
    if (!pVBoxExtPackElm->getAttributeValue("version", strFormatVersion))
        return new iprt::MiniString("Missing format version");
    if (!strFormatVersion.equals("1.0"))
        return &(new iprt::MiniString("Unsupported format version: "))->append(strFormatVersion);

    /*
     * Read and validate mandatory bits.
     */
    const xml::ElementNode *pNameElm = pVBoxExtPackElm->findChildElement("Name");
    if (!pNameElm)
        return new iprt::MiniString("The 'Name' element is missing");
    const char *pszName = pNameElm->getValue();
    if (!VBoxExtPackIsValidName(pszName))
        return &(new iprt::MiniString("Invalid name: "))->append(pszName);

    const xml::ElementNode *pDescElm = pVBoxExtPackElm->findChildElement("Description");
    if (!pDescElm)
        return new iprt::MiniString("The 'Description' element is missing");
    const char *pszDesc = pDescElm->getValue();
    if (!pszDesc || *pszDesc == '\0')
        return new iprt::MiniString("The 'Description' element is empty");
    if (strpbrk(pszDesc, "\n\r\t\v\b") != NULL)
        return new iprt::MiniString("The 'Description' must not contain control characters");

    const xml::ElementNode *pVersionElm = pVBoxExtPackElm->findChildElement("Version");
    if (!pVersionElm)
        return new iprt::MiniString("The 'Version' element is missing");
    const char *pszVersion = pVersionElm->getValue();
    if (!pszVersion || *pszVersion == '\0')
        return new iprt::MiniString("The 'Version' element is empty");
    if (!VBoxExtPackIsValidVersionString(pszVersion))
        return &(new iprt::MiniString("Invalid version string: "))->append(pszVersion);

    uint32_t uRevision;
    if (!pVersionElm->getAttributeValue("revision", uRevision))
        uRevision = 0;

    const xml::ElementNode *pMainModuleElm = pVBoxExtPackElm->findChildElement("MainModule");
    if (!pMainModuleElm)
        return new iprt::MiniString("The 'MainModule' element is missing");
    const char *pszMainModule = pMainModuleElm->getValue();
    if (!pszMainModule || *pszMainModule == '\0')
        return new iprt::MiniString("The 'MainModule' element is empty");
    if (!VBoxExtPackIsValidModuleString(pszMainModule))
        return &(new iprt::MiniString("Invalid main module string: "))->append(pszMainModule);

    /*
     * The VRDE module, optional.
     * Accept both none and empty as tokens of no VRDE module.
     */
    const char *pszVrdeModule = NULL;
    const xml::ElementNode *pVrdeModuleElm = pVBoxExtPackElm->findChildElement("VRDEModule");
    if (pVrdeModuleElm)
    {
        pszVrdeModule = pVrdeModuleElm->getValue();
        if (!pszVrdeModule || *pszVrdeModule == '\0')
            pszVrdeModule = NULL;
        else if (!VBoxExtPackIsValidModuleString(pszVrdeModule))
            return &(new iprt::MiniString("Invalid VRDE module string: "))->append(pszVrdeModule);
    }

    /*
     * Parse plug-in descriptions.
     */
    uint32_t                cPlugIns  = 0;
    PVBOXEXTPACKPLUGINDESC  paPlugIns = NULL;
    iprt::MiniString *pstrRet = vboxExtPackLoadPlugInDescs(pVBoxExtPackElm, &cPlugIns, &paPlugIns);
    if (pstrRet)
    {
        RTMemFree(paPlugIns);
        return pstrRet;
    }

    /*
     * Everything seems fine, fill in the return values and return successfully.
     */
    a_pExtPackDesc->strName         = pszName;
    a_pExtPackDesc->strDescription  = pszDesc;
    a_pExtPackDesc->strVersion      = pszVersion;
    a_pExtPackDesc->uRevision       = uRevision;
    a_pExtPackDesc->strMainModule   = pszMainModule;
    a_pExtPackDesc->strVrdeModule   = pszVrdeModule;
    a_pExtPackDesc->cPlugIns        = cPlugIns;
    a_pExtPackDesc->paPlugIns       = paPlugIns;

    return NULL;
}

/**
 * Reads the extension pack descriptor.
 *
 * @returns NULL on success, pointer to an error message on failure (caller
 *          deletes it).
 * @param   a_pszDir        The directory containing the description file.
 * @param   a_pExtPackDesc  Where to store the extension pack descriptor.
 * @param   a_pObjInfo      Where to store the object info for the file (unix
 *                          attribs). Optional.
 */
iprt::MiniString *VBoxExtPackLoadDesc(const char *a_pszDir, PVBOXEXTPACKDESC a_pExtPackDesc, PRTFSOBJINFO a_pObjInfo)
{
    vboxExtPackClearDesc(a_pExtPackDesc);

    /*
     * Validate, open and parse the XML file.
     */
    char szFilePath[RTPATH_MAX];
    int vrc = RTPathJoin(szFilePath, sizeof(szFilePath), a_pszDir, VBOX_EXTPACK_DESCRIPTION_NAME);
    if (RT_FAILURE(vrc))
        return new iprt::MiniString("RTPathJoin failed with %Rrc", vrc);

    RTFSOBJINFO ObjInfo;
    vrc = RTPathQueryInfoEx(szFilePath, &ObjInfo,  RTFSOBJATTRADD_UNIX, RTPATH_F_ON_LINK);
    if (RT_FAILURE(vrc))
        return &(new iprt::MiniString())->printf("RTPathQueryInfoEx failed with %Rrc", vrc);
    if (a_pObjInfo)
        *a_pObjInfo = ObjInfo;
    if (!RTFS_IS_FILE(ObjInfo.Attr.fMode))
    {
        if (RTFS_IS_SYMLINK(ObjInfo.Attr.fMode))
            return new iprt::MiniString("The XML file is symlinked, that is not allowed");
        return &(new iprt::MiniString)->printf("The XML file is not a file (fMode=%#x)", ObjInfo.Attr.fMode);
    }

    xml::Document       Doc;
    {
        xml::XmlFileParser  Parser;
        try
        {
            Parser.read(szFilePath, Doc);
        }
        catch (xml::XmlError Err)
        {
            return new iprt::MiniString(Err.what());
        }
    }

    /*
     * Hand the xml doc over to the common code.
     */
    return vboxExtPackLoadDescFromDoc(&Doc, a_pExtPackDesc);
}

/**
 * Reads the extension pack descriptor.
 *
 * @returns NULL on success, pointer to an error message on failure (caller
 *          deletes it).
 * @param   a_pszDir        The directory containing the description file.
 * @param   a_pExtPackDesc  Where to store the extension pack descriptor.
 * @param   a_pObjInfo      Where to store the object info for the file (unix
 *                          attribs). Optional.
 */
iprt::MiniString *VBoxExtPackLoadDescFromVfsFile(RTVFSFILE hVfsFile, PVBOXEXTPACKDESC a_pExtPackDesc, PRTFSOBJINFO a_pObjInfo)
{
    vboxExtPackClearDesc(a_pExtPackDesc);

    /*
     * Query the object info.
     */
    RTFSOBJINFO ObjInfo;
    int rc = RTVfsFileQueryInfo(hVfsFile, &ObjInfo, RTFSOBJATTRADD_UNIX);
    if (RT_FAILURE(rc))
        return &(new iprt::MiniString)->printf("RTVfsFileQueryInfo failed: %Rrc", rc);
    if (a_pObjInfo)
        *a_pObjInfo = ObjInfo;

    /*
     * The simple approach, read the whole thing into memory and pass this to
     * the XML parser.
     */

    /* Check the file size. */
    if (ObjInfo.cbObject > _1M || ObjInfo.cbObject < 0)
        return &(new iprt::MiniString)->printf("The XML file is too large (%'RU64 bytes)", ObjInfo.cbObject);
    size_t const cbFile = (size_t)ObjInfo.cbObject;

    /* Rewind to the start of the file. */
    rc = RTVfsFileSeek(hVfsFile, 0, RTFILE_SEEK_BEGIN, NULL);
    if (RT_FAILURE(rc))
        return &(new iprt::MiniString)->printf("RTVfsFileSeek(,0,BEGIN) failed: %Rrc", rc);

    /* Allocate memory and read the file content into it. */
    void *pvFile = RTMemTmpAlloc(cbFile);
    if (!pvFile)
        return &(new iprt::MiniString)->printf("RTMemTmpAlloc(%zu) failed", cbFile);

    iprt::MiniString *pstrErr = NULL;
    rc = RTVfsFileRead(hVfsFile, pvFile, cbFile, NULL);
    if (RT_FAILURE(rc))
        pstrErr = &(new iprt::MiniString)->printf("RTVfsFileRead failed: %Rrc", rc);

    /*
     * Parse the file.
     */
    xml::Document Doc;
    if (RT_SUCCESS(rc))
    {
        xml::XmlMemParser   Parser;
        iprt::MiniString    strFileName = VBOX_EXTPACK_DESCRIPTION_NAME;
        try
        {
            Parser.read(pvFile, cbFile, strFileName, Doc);
        }
        catch (xml::XmlError Err)
        {
            pstrErr = new iprt::MiniString(Err.what());
            rc = VERR_PARSE_ERROR;
        }
    }
    RTMemTmpFree(pvFile);

    /*
     * Hand the xml doc over to the common code.
     */
    if (RT_SUCCESS(rc))
        pstrErr = vboxExtPackLoadDescFromDoc(&Doc, a_pExtPackDesc);

    return pstrErr;
}

/**
 * Frees all resources associated with a extension pack descriptor.
 *
 * @param   a_pExtPackDesc      The extension pack descriptor which members
 *                              should be freed.
 */
void VBoxExtPackFreeDesc(PVBOXEXTPACKDESC a_pExtPackDesc)
{
    if (!a_pExtPackDesc)
        return;

    a_pExtPackDesc->strName.setNull();
    a_pExtPackDesc->strDescription.setNull();
    a_pExtPackDesc->strVersion.setNull();
    a_pExtPackDesc->uRevision = 0;
    a_pExtPackDesc->strMainModule.setNull();
    a_pExtPackDesc->strVrdeModule.setNull();
    a_pExtPackDesc->cPlugIns = 0;
    RTMemFree(a_pExtPackDesc->paPlugIns);
    a_pExtPackDesc->paPlugIns = NULL;
}

/**
 * Extract the extension pack name from the tarball path.
 *
 * @returns String containing the name on success, the caller must delete it.
 *          NULL if no valid name was found or if we ran out of memory.
 * @param   pszTarball          The path to the tarball.
 */
iprt::MiniString *VBoxExtPackExtractNameFromTarballPath(const char *pszTarball)
{
    /*
     * Skip ahead to the filename part and count the number of characters
     * that matches the criteria for a mangled extension pack name.
     */
    const char *pszSrc = RTPathFilename(pszTarball);
    if (!pszSrc)
        return NULL;

    size_t off = 0;
    while (RT_C_IS_ALNUM(pszSrc[off]) || pszSrc[off] == '_')
        off++;

    /*
     * Check min and max name limits.
     */
    if (   off > VBOX_EXTPACK_NAME_MAX_LEN
        || off < VBOX_EXTPACK_NAME_MIN_LEN)
        return NULL;

    /*
     * Return the unmangled name.
     */
    return VBoxExtPackUnmangleName(pszSrc, off);
}

/**
 * Validates the extension pack name.
 *
 * @returns true if valid, false if not.
 * @param   pszName             The name to validate.
 * @sa      VBoxExtPackExtractNameFromTarballPath
 */
bool VBoxExtPackIsValidName(const char *pszName)
{
    if (!pszName)
        return false;

    /*
     * Check the characters making up the name, only english alphabet
     * characters, decimal digits and spaces are allowed.
     */
    size_t off = 0;
    while (pszName[off])
    {
        if (!RT_C_IS_ALNUM(pszName[off]) && pszName[off] != ' ')
            return false;
        off++;
    }

    /*
     * Check min and max name limits.
     */
    if (   off > VBOX_EXTPACK_NAME_MAX_LEN
        || off < VBOX_EXTPACK_NAME_MIN_LEN)
        return false;

    return true;
}

/**
 * Checks if an alledged manged extension pack name.
 *
 * @returns true if valid, false if not.
 * @param   pszMangledName      The mangled name to validate.
 * @param   cchMax              The max number of chars to test.
 * @sa      VBoxExtPackMangleName
 */
bool VBoxExtPackIsValidMangledName(const char *pszMangledName, size_t cchMax /*= RTSTR_MAX*/)
{
    if (!pszMangledName)
        return false;

    /*
     * Check the characters making up the name, only english alphabet
     * characters, decimal digits and underscores (=space) are allowed.
     */
    size_t off = 0;
    while (off < cchMax && pszMangledName[off])
    {
        if (!RT_C_IS_ALNUM(pszMangledName[off]) && pszMangledName[off] != '_')
            return false;
        off++;
    }

    /*
     * Check min and max name limits.
     */
    if (   off > VBOX_EXTPACK_NAME_MAX_LEN
        || off < VBOX_EXTPACK_NAME_MIN_LEN)
        return false;

    return true;
}

/**
 * Mangle an extension pack name so it can be used by a directory or file name.
 *
 * @returns String containing the mangled name on success, the caller must
 *          delete it.  NULL on failure.
 * @param   pszName             The unmangled name.
 * @sa      VBoxExtPackUnmangleName, VBoxExtPackIsValidMangledName
 */
iprt::MiniString *VBoxExtPackMangleName(const char *pszName)
{
    AssertReturn(VBoxExtPackIsValidName(pszName), NULL);

    char    szTmp[VBOX_EXTPACK_NAME_MAX_LEN + 1];
    size_t  off = 0;
    char    ch;
    while ((ch = pszName[off]) != '\0')
    {
        if (ch == ' ')
            ch = '_';
        szTmp[off++] = ch;
    }
    szTmp[off] = '\0';
    Assert(VBoxExtPackIsValidMangledName(szTmp));

    return new iprt::MiniString(szTmp, off);
}

/**
 * Unmangle an extension pack name (reverses VBoxExtPackMangleName).
 *
 * @returns String containing the mangled name on success, the caller must
 *          delete it.  NULL on failure.
 * @param   pszMangledName      The mangled name.
 * @param   cchMax              The max name length.  RTSTR_MAX is fine.
 * @sa      VBoxExtPackMangleName, VBoxExtPackIsValidMangledName
 */
iprt::MiniString *VBoxExtPackUnmangleName(const char *pszMangledName, size_t cchMax)
{
    AssertReturn(VBoxExtPackIsValidMangledName(pszMangledName, cchMax), NULL);

    char    szTmp[VBOX_EXTPACK_NAME_MAX_LEN + 1];
    size_t  off = 0;
    char    ch;
    while (   off < cchMax
           && (ch = pszMangledName[off]) != '\0')
    {
        if (ch == '_')
            ch = ' ';
        else
            AssertReturn(RT_C_IS_ALNUM(ch) || ch == ' ', NULL);
        szTmp[off++] = ch;
    }
    szTmp[off] = '\0';
    AssertReturn(VBoxExtPackIsValidName(szTmp), NULL);

    return new iprt::MiniString(szTmp, off);
}

/**
 * Constructs the extension pack directory path.
 *
 * A combination of RTPathJoin and VBoxExtPackMangleName.
 *
 * @returns IPRT status code like RTPathJoin.
 * @param   pszExtPackDir   Where to return the directory path.
 * @param   cbExtPackDir    The size of the return buffer.
 * @param   pszParentDir    The parent directory (".../Extensions").
 * @param   pszName         The extension pack name, unmangled.
 */
int VBoxExtPackCalcDir(char *pszExtPackDir, size_t cbExtPackDir, const char *pszParentDir, const char *pszName)
{
    AssertReturn(VBoxExtPackIsValidName(pszName), VERR_INTERNAL_ERROR_5);

    iprt::MiniString *pstrMangledName = VBoxExtPackMangleName(pszName);
    if (!pstrMangledName)
        return VERR_INTERNAL_ERROR_4;

    int vrc = RTPathJoin(pszExtPackDir, cbExtPackDir, pszParentDir, pstrMangledName->c_str());
    delete pstrMangledName;

    return vrc;
}


/**
 * Validates the extension pack version string.
 *
 * @returns true if valid, false if not.
 * @param   pszVersion          The version string to validate.
 */
bool VBoxExtPackIsValidVersionString(const char *pszVersion)
{
    if (!pszVersion || *pszVersion == '\0')
        return false;

    /* 1.x.y.z... */
    for (;;)
    {
        if (!RT_C_IS_DIGIT(*pszVersion))
            return false;
        do
            pszVersion++;
        while (RT_C_IS_DIGIT(*pszVersion));
        if (*pszVersion != '.')
            break;
        pszVersion++;
    }

    /* upper case string + numbers indicating the build type */
    if (*pszVersion == '-' || *pszVersion == '_')
    {
        do
            pszVersion++;
        while (   RT_C_IS_DIGIT(*pszVersion)
               || RT_C_IS_UPPER(*pszVersion)
               || *pszVersion == '-'
               || *pszVersion == '_');
    }

    /* revision or nothing */
    if (*pszVersion != '\0')
    {
        if (*pszVersion != 'r')
            return false;
        do
            pszVersion++;
        while (RT_C_IS_DIGIT(*pszVersion));
    }

    return *pszVersion == '\0';
}

/**
 * Validates an extension pack module string.
 *
 * @returns true if valid, false if not.
 * @param   pszModule           The module string to validate.
 */
bool VBoxExtPackIsValidModuleString(const char *pszModule)
{
    if (!pszModule || *pszModule == '\0')
        return false;

    /* Restricted charset, no extensions (dots). */
    while (   RT_C_IS_ALNUM(*pszModule)
           || *pszModule == '-'
           || *pszModule == '_')
        pszModule++;

    return *pszModule == '\0';
}

/**
 * RTStrPrintfv wrapper.
 *
 * @returns @a rc
 * @param   rc                  The status code to return.
 * @param   pszError            The error buffer.
 * @param   cbError             The size of the buffer.
 * @param   pszFormat           The error message format string.
 * @param   ...                 Format arguments.
 */
static int vboxExtPackReturnError(int rc, char *pszError, size_t cbError, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    RTStrPrintfV(pszError, cbError, pszFormat, va);
    va_end(va);
    return rc;
}

/**
 * RTStrPrintfv wrapper.
 *
 * @param   pszError            The error buffer.
 * @param   cbError             The size of the buffer.
 * @param   pszFormat           The error message format string.
 * @param   ...                 Format arguments.
 */
static void vboxExtPackSetError(char *pszError, size_t cbError, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    RTStrPrintfV(pszError, cbError, pszFormat, va);
    va_end(va);
}

/**
 * Verifies the manifest and its signature.
 *
 * @returns VBox status code, failures with message.
 * @param   hManifestFile       The xml from the extension pack.
 * @param   pszExtPackName      The expected extension pack name.  This can be
 *                              NULL, in which we don't have any expectations.
 * @param   pszError            Where to store an error message on failure.
 * @param   cbError             The size of the buffer @a pszError points to.
 */
static int vboxExtPackVerifyXml(RTVFSFILE hXmlFile, const char *pszExtPackName, char *pszError, size_t cbError)
{
    /*
     * Load the XML.
     */
    VBOXEXTPACKDESC     ExtPackDesc;
    iprt::MiniString   *pstrErr = VBoxExtPackLoadDescFromVfsFile(hXmlFile, &ExtPackDesc, NULL);
    if (pstrErr)
    {
        RTStrCopy(pszError, cbError, pstrErr->c_str());
        delete pstrErr;
        return VERR_PARSE_ERROR;
    }

    /*
     * Check the name.
     */
    /** @todo drop this restriction after the old install interface is
     *        dropped. */
    int rc = VINF_SUCCESS;
    if (   pszExtPackName
        && !ExtPackDesc.strName.equalsIgnoreCase(pszExtPackName))
        rc = vboxExtPackReturnError(VERR_NOT_EQUAL, pszError, cbError,
                                    "The name of the downloaded file and the name stored inside the extension pack does not match"
                                    " (xml='%s' file='%s')", ExtPackDesc.strName.c_str(), pszExtPackName);
    return rc;
}

/**
 * Verifies the manifest and its signature.
 *
 * @returns VBox status code, failures with message.
 * @param   hOurManifest    The manifest we compiled.
 * @param   hManifestFile   The manifest file in the extension pack.
 * @param   hSignatureFile  The manifest signature file.
 * @param   pszError            Where to store an error message on failure.
 * @param   cbError             The size of the buffer @a pszError points to.
 */
static int vboxExtPackVerifyManifestAndSignature(RTMANIFEST hOurManifest, RTVFSFILE hManifestFile, RTVFSFILE hSignatureFile,
                                                 char *pszError, size_t cbError)
{
    /*
     * Read the manifest from the extension pack.
     */
    int rc = RTVfsFileSeek(hManifestFile, 0, RTFILE_SEEK_BEGIN, NULL);
    if (RT_FAILURE(rc))
        return vboxExtPackReturnError(rc, pszError, cbError, "RTVfsFileSeek failed: %Rrc", rc);

    RTMANIFEST hTheirManifest;
    rc = RTManifestCreate(0 /*fFlags*/, &hTheirManifest);
    if (RT_FAILURE(rc))
        return vboxExtPackReturnError(rc, pszError, cbError, "RTManifestCreate failed: %Rrc", rc);

    RTVFSIOSTREAM hVfsIos = RTVfsFileToIoStream(hManifestFile);
    rc = RTManifestReadStandard(hTheirManifest, hVfsIos);
    RTVfsIoStrmRelease(hVfsIos);
    if (RT_SUCCESS(rc))
    {
        /*
         * Compare the manifests.
         */
        static const char *s_apszIgnoreEntries[] =
        {
            VBOX_EXTPACK_MANIFEST_NAME,
            VBOX_EXTPACK_SIGNATURE_NAME,
            "./" VBOX_EXTPACK_MANIFEST_NAME,
            "./" VBOX_EXTPACK_SIGNATURE_NAME,
            NULL
        };
        char szError[RTPATH_MAX];
        rc = RTManifestEqualsEx(hOurManifest, hTheirManifest, &s_apszIgnoreEntries[0], NULL,
                                RTMANIFEST_EQUALS_IGN_MISSING_ATTRS /*fFlags*/,
                                szError, sizeof(szError));
        if (RT_SUCCESS(rc))
        {
            /*
             * Validate the manifest file signature.
             */
            /** @todo implement signature stuff */
            NOREF(hSignatureFile);

        }
        else if (rc == VERR_NOT_EQUAL && szError[0])
            vboxExtPackSetError(pszError, cbError, "Manifest mismatch: %s", szError);
        else
            vboxExtPackSetError(pszError, cbError, "RTManifestEqualsEx failed: %Rrc", rc);
#if 0
        RTVFSIOSTREAM hVfsIosStdOut = NIL_RTVFSIOSTREAM;
        RTVfsIoStrmFromStdHandle(RTHANDLESTD_OUTPUT, RTFILE_O_WRITE, true, &hVfsIosStdOut);
        RTVfsIoStrmWrite(hVfsIosStdOut, "Our:\n", sizeof("Our:\n") - 1, true, NULL);
        RTManifestWriteStandard(hOurManifest, hVfsIosStdOut);
        RTVfsIoStrmWrite(hVfsIosStdOut, "Their:\n", sizeof("Their:\n") - 1, true, NULL);
        RTManifestWriteStandard(hTheirManifest, hVfsIosStdOut);
#endif
    }
    else
        vboxExtPackSetError(pszError, cbError, "Error parsing '%s': %Rrc", VBOX_EXTPACK_MANIFEST_NAME, rc);

    RTManifestRelease(hTheirManifest);
    return rc;
}


/**
 * Validates a name in an extension pack.
 *
 * We restrict the charset to try make sure the extension pack can be unpacked
 * on all file systems.
 *
 * @returns VBox status code, failures with message.
 * @param   pszName             The name to validate.
 * @param   pszError            Where to store an error message on failure.
 * @param   cbError             The size of the buffer @a pszError points to.
 */
static int vboxExtPackValidateMemberName(const char *pszName, char *pszError, size_t cbError)
{
    if (RTPathStartsWithRoot(pszName))
        return vboxExtPackReturnError(VERR_PATH_IS_NOT_RELATIVE, pszError, cbError, "'%s': starts with root spec", pszName);

    const char *pszErr = NULL;
    const char *psz = pszName;
    int ch;
    while ((ch = *psz) != '\0')
    {
        /* Character set restrictions. */
        if (ch < 0 || ch >= 128)
        {
            pszErr = "Only 7-bit ASCII allowed";
            break;
        }
        if (ch <= 31 || ch == 127)
        {
            pszErr = "No control characters are not allowed";
            break;
        }
        if (ch == '\\')
        {
            pszErr = "Only backward slashes are not allowed";
            break;
        }
        if (strchr("'\":;*?|[]<>(){}", ch))
        {
            pszErr = "The characters ', \", :, ;, *, ?, |, [, ], <, >, (, ), { and } are not allowed";
            break;
        }

        /* Take the simple way out and ban all ".." sequences. */
        if (   ch     == '.'
            && psz[1] == '.')
        {
            pszErr = "Double dot sequence are not allowed";
            break;
        }

        /* Keep the tree shallow or the hardening checks will fail. */
        if (psz - pszName > VBOX_EXTPACK_MAX_MEMBER_NAME_LENGTH)
        {
            pszErr = "Too long";
            break;
        }

        /* advance */
        psz++;
    }

    if (pszErr)
        return vboxExtPackReturnError(VERR_INVALID_NAME, pszError, cbError,
                                      "Bad member name '%s' (pos %zu): %s", pszName, (size_t)(psz - pszName), pszErr);
    return RTEXITCODE_SUCCESS;
}


/**
 * Validates a file in an extension pack.
 *
 * @returns VBox status code, failures with message.
 * @param   pszName             The name of the file.
 * @param   hVfsObj             The VFS object.
 * @param   pszError            Where to store an error message on failure.
 * @param   cbError             The size of the buffer @a pszError points to.
 */
static int vboxExtPackValidateMemberFile(const char *pszName, RTVFSOBJ hVfsObj, char *pszError, size_t cbError)
{
    int rc = vboxExtPackValidateMemberName(pszName, pszError, cbError);
    if (RT_SUCCESS(rc))
    {
        RTFSOBJINFO ObjInfo;
        rc = RTVfsObjQueryInfo(hVfsObj, &ObjInfo, RTFSOBJATTRADD_NOTHING);
        if (RT_SUCCESS(rc))
        {
            if (ObjInfo.cbObject >= 9*_1G64)
                rc = vboxExtPackReturnError(VERR_OUT_OF_RANGE, pszError, cbError,
                                            "'%s': too large (%'RU64 bytes)",
                                            pszName, (uint64_t)ObjInfo.cbObject);
            if (!RTFS_IS_FILE(ObjInfo.Attr.fMode))
                rc = vboxExtPackReturnError(VERR_NOT_A_FILE, pszError, cbError,
                                            "The alleged file '%s' has a mode mask stating otherwise (%RTfmode)",
                                            pszName, ObjInfo.Attr.fMode);
        }
        else
            vboxExtPackSetError(pszError, cbError, "RTVfsObjQueryInfo failed on '%s': %Rrc", pszName, rc);
    }
    return rc;
}


/**
 * Validates a directory in an extension pack.
 *
 * @returns VBox status code, failures with message.
 * @param   pszName             The name of the directory.
 * @param   hVfsObj             The VFS object.
 * @param   pszError            Where to store an error message on failure.
 * @param   cbError             The size of the buffer @a pszError points to.
 */
static int vboxExtPackValidateMemberDir(const char *pszName, RTVFSOBJ hVfsObj, char *pszError, size_t cbError)
{
    int rc = vboxExtPackValidateMemberName(pszName, pszError, cbError);
    if (RT_SUCCESS(rc))
    {
        RTFSOBJINFO ObjInfo;
        rc = RTVfsObjQueryInfo(hVfsObj, &ObjInfo, RTFSOBJATTRADD_NOTHING);
        if (RT_SUCCESS(rc))
        {
            if (!RTFS_IS_DIRECTORY(ObjInfo.Attr.fMode))
                rc = vboxExtPackReturnError(VERR_NOT_A_DIRECTORY, pszError, cbError,
                                            "The alleged directory '%s' has a mode mask saying differently (%RTfmode)",
                                            pszName, ObjInfo.Attr.fMode);
        }
        else
            vboxExtPackSetError(pszError, cbError, "RTVfsObjQueryInfo failed on '%s': %Rrc", pszName, rc);
    }
    return rc;
}

/**
 * Validates a member of an extension pack.
 *
 * @returns VBox status code, failures with message.
 * @param   pszName             The name of the directory.
 * @param   enmType             The object type.
 * @param   hVfsObj             The VFS object.
 * @param   pszError            Where to store an error message on failure.
 * @param   cbError             The size of the buffer @a pszError points to.
 */
int VBoxExtPackValidateMember(const char *pszName, RTVFSOBJTYPE enmType, RTVFSOBJ hVfsObj, char *pszError, size_t cbError)
{
    Assert(cbError > 0);
    *pszError = '\0';

    int rc;
    if (   enmType == RTVFSOBJTYPE_FILE
        || enmType == RTVFSOBJTYPE_IO_STREAM)
        rc = vboxExtPackValidateMemberFile(pszName, hVfsObj, pszError, cbError);
    else if (   enmType == RTVFSOBJTYPE_DIR
             || enmType == RTVFSOBJTYPE_BASE)
        rc = vboxExtPackValidateMemberDir(pszName, hVfsObj, pszError, cbError);
    else
        rc = vboxExtPackReturnError(VERR_UNEXPECTED_FS_OBJ_TYPE, pszError, cbError,
                                    "'%s' is not a file or directory (enmType=%d)", pszName, enmType);
    return rc;
}


/**
 * Rewinds the tarball file handle and creates a gunzip | tar chain that
 * results in a filesystem stream.
 *
 * @returns VBox status code, failures with message.
 * @param   hTarballFile        The handle to the tarball file.
 * @param   pszError            Where to store an error message on failure.
 * @param   cbError             The size of the buffer @a pszError points to.
 * @param   phTarFss            Where to return the filesystem stream handle.
 */
int VBoxExtPackOpenTarFss(RTFILE hTarballFile, char *pszError, size_t cbError, PRTVFSFSSTREAM phTarFss)
{
    Assert(cbError > 0);
    *pszError = '\0';
    *phTarFss = NIL_RTVFSFSSTREAM;

    /*
     * Rewind the file and set up a VFS chain for it.
     */
    int rc = RTFileSeek(hTarballFile, 0, RTFILE_SEEK_BEGIN, NULL);
    if (RT_FAILURE(rc))
        return vboxExtPackReturnError(rc, pszError, cbError, "Failed seeking to the start of the tarball: %Rrc", rc);

    RTVFSIOSTREAM hTarballIos;
    rc = RTVfsIoStrmFromRTFile(hTarballFile, RTFILE_O_READ | RTFILE_O_DENY_WRITE | RTFILE_O_OPEN, true /*fLeaveOpen*/,
                               &hTarballIos);
    if (RT_FAILURE(rc))
        return vboxExtPackReturnError(rc, pszError, cbError, "RTVfsIoStrmFromRTFile failed: %Rrc", rc);

    RTVFSIOSTREAM hGunzipIos;
    rc = RTZipGzipDecompressIoStream(hTarballIos, 0 /*fFlags*/, &hGunzipIos);
    if (RT_SUCCESS(rc))
    {
        RTVFSFSSTREAM hTarFss;
        rc = RTZipTarFsStreamFromIoStream(hGunzipIos, 0 /*fFlags*/, &hTarFss);
        if (RT_SUCCESS(rc))
        {
            RTVfsIoStrmRelease(hGunzipIos);
            RTVfsIoStrmRelease(hTarballIos);
            *phTarFss = hTarFss;
            return VINF_SUCCESS;
        }
        vboxExtPackSetError(pszError, cbError, "RTZipTarFsStreamFromIoStream failed: %Rrc", rc);
        RTVfsIoStrmRelease(hGunzipIos);
    }
    else
        vboxExtPackSetError(pszError, cbError, "RTZipGzipDecompressIoStream failed: %Rrc", rc);
    RTVfsIoStrmRelease(hTarballIos);
    return rc;
}


/**
 * Validates the extension pack tarball prior to unpacking.
 *
 * Operations performed:
 *      - Mandatory files.
 *      - Manifest check.
 *      - Manifest seal check.
 *      - XML check, match name.
 *
 * @returns VBox status code, failures with message.
 * @param   hTarballFile        The handle to open the @a pszTarball file.
 * @param   pszExtPackName      The name of the extension pack name.  NULL if
 *                              the name is not fixed.
 * @param   pszTarball          The name of the tarball in case we have to
 *                              complain about something.
 * @param   pszError            Where to store an error message on failure.
 * @param   cbError             The size of the buffer @a pszError points to.
 * @param   phValidManifest     Where to optionally return the handle to fully
 *                              validated the manifest for the extension pack.
 *                              This includes all files.
 * @param   phXmlFile           Where to optionally return the memorized XML
 *                              file.
 *
 * @todo    This function is a bit too long and should be split up if possible.
 */
int VBoxExtPackValidateTarball(RTFILE hTarballFile, const char *pszExtPackName, const char *pszTarball,
                               char *pszError, size_t cbError, PRTMANIFEST phValidManifest, PRTVFSFILE phXmlFile)
{
    /*
     * Clear return values.
     */
    if (phValidManifest)
        *phValidManifest = NIL_RTMANIFEST;
    if (phXmlFile)
        *phXmlFile = NIL_RTVFSFILE;
    Assert(cbError > 1);
    *pszError = '\0';
    NOREF(pszTarball);

    /*
     * Open the tar.gz filesystem stream and set up an manifest in-memory file.
     */
    RTVFSFSSTREAM hTarFss;
    int rc = VBoxExtPackOpenTarFss(hTarballFile, pszError, cbError, &hTarFss);
    if (RT_FAILURE(rc))
        return rc;

    RTMANIFEST hOurManifest;
    rc = RTManifestCreate(0 /*fFlags*/, &hOurManifest);
    if (RT_SUCCESS(rc))
    {
        /*
         * Process the tarball (would be nice to move this to a function).
         */
        RTVFSFILE hXmlFile      = NIL_RTVFSFILE;
        RTVFSFILE hManifestFile = NIL_RTVFSFILE;
        RTVFSFILE hSignatureFile= NIL_RTVFSFILE;
        for (;;)
        {
            /*
             * Get the next stream object.
             */
            char           *pszName;
            RTVFSOBJ        hVfsObj;
            RTVFSOBJTYPE    enmType;
            rc = RTVfsFsStrmNext(hTarFss, &pszName, &enmType, &hVfsObj);
            if (RT_FAILURE(rc))
            {
                if (rc != VERR_EOF)
                    vboxExtPackSetError(pszError, cbError, "RTVfsFsStrmNext failed: %Rrc", rc);
                else
                    rc = VINF_SUCCESS;
                break;
            }
            const char     *pszAdjName = pszName[0] == '.' && pszName[1] == '/' ? &pszName[2] : pszName;

            /*
             * Check the type & name validity.
             *
             * N.B. We will always reach the end of the loop before breaking on
             *      failure - cleanup reasons.
             */
            rc = VBoxExtPackValidateMember(pszName, enmType, hVfsObj, pszError, cbError);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Check if this is one of the standard files.
                 */
                PRTVFSFILE phVfsFile;
                if (!strcmp(pszAdjName, VBOX_EXTPACK_DESCRIPTION_NAME))
                    phVfsFile = &hXmlFile;
                else if (!strcmp(pszAdjName, VBOX_EXTPACK_MANIFEST_NAME))
                    phVfsFile = &hManifestFile;
                else if (!strcmp(pszAdjName, VBOX_EXTPACK_SIGNATURE_NAME))
                    phVfsFile = &hSignatureFile;
                else
                    phVfsFile = NULL;
                if (phVfsFile)
                {
                    /*
                     * Make sure it's a file and that it isn't too large.
                     */
                    if (*phVfsFile != NIL_RTVFSFILE)
                        rc = vboxExtPackReturnError(VERR_DUPLICATE, pszError, cbError,
                                                    "There can only be one '%s'", pszAdjName);
                    else if (enmType != RTVFSOBJTYPE_IO_STREAM && enmType != RTVFSOBJTYPE_FILE)
                        rc = vboxExtPackReturnError(VERR_NOT_A_FILE, pszError, cbError,
                                                    "Standard member '%s' is not a file", pszAdjName);
                    else
                    {
                        RTFSOBJINFO ObjInfo;
                        rc = RTVfsObjQueryInfo(hVfsObj, &ObjInfo, RTFSOBJATTRADD_NOTHING);
                        if (RT_SUCCESS(rc))
                        {
                            if (!RTFS_IS_FILE(ObjInfo.Attr.fMode))
                                rc = vboxExtPackReturnError(VERR_NOT_A_FILE, pszError, cbError,
                                                            "Standard member '%s' is not a file", pszAdjName);
                            else if (ObjInfo.cbObject >= _1M)
                                rc = vboxExtPackReturnError(VERR_OUT_OF_RANGE, pszError, cbError,
                                                            "Standard member '%s' is too large: %'RU64 bytes (max 1 MB)",
                                                            pszAdjName, (uint64_t)ObjInfo.cbObject);
                            else
                            {
                                /*
                                 * Make an in memory copy of the stream.
                                 */
                                RTVFSIOSTREAM hVfsIos = RTVfsObjToIoStream(hVfsObj);
                                rc = RTVfsMemorizeIoStreamAsFile(hVfsIos, RTFILE_O_READ, phVfsFile);
                                if (RT_SUCCESS(rc))
                                {
                                    /*
                                     * To simplify the code below, replace
                                     * hVfsObj with the memorized file.
                                     */
                                    RTVfsObjRelease(hVfsObj);
                                    hVfsObj = RTVfsObjFromFile(*phVfsFile);
                                }
                                else
                                    vboxExtPackSetError(pszError, cbError, "RTVfsMemorizeIoStreamAsFile failed on '%s': %Rrc", pszName, rc);
                                RTVfsIoStrmRelease(hVfsIos);
                            }
                        }
                        else
                            vboxExtPackSetError(pszError, cbError, "RTVfsObjQueryInfo failed on '%s': %Rrc", pszName, rc);
                    }
                }
            }

            /*
             * Add any I/O stream to the manifest
             */
            if (   RT_SUCCESS(rc)
                && (   enmType == RTVFSOBJTYPE_FILE
                    || enmType == RTVFSOBJTYPE_IO_STREAM))
            {
                RTVFSIOSTREAM hVfsIos = RTVfsObjToIoStream(hVfsObj);
                rc = RTManifestEntryAddIoStream(hOurManifest, hVfsIos, pszAdjName, RTMANIFEST_ATTR_SIZE | RTMANIFEST_ATTR_SHA256);
                if (RT_FAILURE(rc))
                    vboxExtPackSetError(pszError, cbError, "RTManifestEntryAddIoStream failed on '%s': %Rrc", pszAdjName, rc);
                RTVfsIoStrmRelease(hVfsIos);
            }

            /*
             * Clean up and break out on failure.
             */
            RTVfsObjRelease(hVfsObj);
            RTStrFree(pszName);
            if (RT_FAILURE(rc))
                break;
        }

        /*
         * If we've successfully processed the tarball, verify that the
         * mandatory files are present.
         */
        if (RT_SUCCESS(rc))
        {
            if (hXmlFile == NIL_RTVFSFILE)
                rc = vboxExtPackReturnError(VERR_MISSING, pszError, cbError, "Mandator file '%s' is missing", VBOX_EXTPACK_DESCRIPTION_NAME);
            if (hManifestFile == NIL_RTVFSFILE)
                rc = vboxExtPackReturnError(VERR_MISSING, pszError, cbError, "Mandator file '%s' is missing", VBOX_EXTPACK_MANIFEST_NAME);
            if (hSignatureFile == NIL_RTVFSFILE)
                rc = vboxExtPackReturnError(VERR_MISSING, pszError, cbError, "Mandator file '%s' is missing", VBOX_EXTPACK_SIGNATURE_NAME);
        }

        /*
         * Check the manifest and it's signature.
         */
        if (RT_SUCCESS(rc))
            rc = vboxExtPackVerifyManifestAndSignature(hOurManifest, hManifestFile, hSignatureFile, pszError, cbError);

        /*
         * Check the XML.
         */
        if (RT_SUCCESS(rc))
            rc = vboxExtPackVerifyXml(hXmlFile, pszExtPackName, pszError, cbError);

        /*
         * Returns objects.
         */
        if (RT_SUCCESS(rc))
        {
            if (phValidManifest)
            {
                RTManifestRetain(hOurManifest);
                *phValidManifest = hOurManifest;
            }
            if (phXmlFile)
            {
                RTVfsFileRetain(hXmlFile);
                *phXmlFile = hXmlFile;
            }
        }

        /*
         * Release our object references.
         */
        RTManifestRelease(hOurManifest);
        RTVfsFileRelease(hXmlFile);
        RTVfsFileRelease(hManifestFile);
        RTVfsFileRelease(hSignatureFile);
    }
    else
        vboxExtPackSetError(pszError, cbError, "RTManifestCreate failed: %Rrc", rc);
    RTVfsFsStrmRelease(hTarFss);

    return rc;
}

