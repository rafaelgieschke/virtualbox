/* $Id$ */
/** @file
 * Classes for reading/parsing/saving scripts for unattended installation.
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

#ifndef MAIN_INCLUDED_UnattendedScript_h
#define MAIN_INCLUDED_UnattendedScript_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "TextScript.h"
#include "iprt/expreval.h"

using namespace xml;

class Unattended;


/**
 * Generic unattended text script template editor.
 *
 * This just perform variable replacements, no other editing possible.
 *
 * Everything happens during saveToString, parse is a noop.
 */
class UnattendedScriptTemplate : public BaseTextScript
{
protected:
    /** Where to get the replacement strings from. */
    Unattended *mpUnattended;

public:
    DECLARE_TRANSLATE_METHODS(UnattendedScriptTemplate)

    UnattendedScriptTemplate(Unattended *pUnattended, const char *pszDefaultTemplateFilename, const char *pszDefaultFilename);
    virtual ~UnattendedScriptTemplate()             {}

    HRESULT parse()                                 { return S_OK; }
    HRESULT saveToString(Utf8Str &rStrDst);

protected:
    typedef enum
    {
        kValueEscaping_None,
        kValueEscaping_Bourne,
        kValueEscaping_XML_Element,
        kValueEscaping_XML_Attribute_Double_Quotes
    } kEvalEscaping_T;

    /**
     * Gets the replacement value for the given placeholder.
     *
     * @returns COM status code.
     * @param   pachPlaceholder The placholder string.  Not zero terminated.
     * @param   cchPlaceholder  The length of the placeholder.
     * @param   fOutputting     Indicates whether we actually need the correct value
     *                          or is just syntax checking excluded template parts.
     * @param   rValue          Where to return the value.
     */
    HRESULT getReplacement(const char *pachPlaceholder, size_t cchPlaceholder, bool fOutputting, RTCString &rValue);

    /**
     * Gets the replacement value for the given expression placeholder
     * (@@VBOX_INSERT[expr]@@ and friends).
     *
     * @returns COM status code.
     * @param   hEvaluator      The evaluator to use for the expression.
     * @param   pachPlaceholder The placholder string.  Not zero terminated.
     * @param   cchPlaceholder  The length of the placeholder.
     * @param   fOutputting     Indicates whether we actually need the correct value
     *                          or is just syntax checking excluded template parts.
     * @param   ppszValue       Where to return the value.  Free by calling
     *                          RTStrFree.  Set to NULL for empty string.
     */
    HRESULT getReplacementForExpr(RTEXPREVAL hEvaluator, const char *pachPlaceholder, size_t cchPlaceholder,
                                  bool fOutputting, char **ppszValue) RT_NOEXCEPT;

    /**
     * Resolves a conditional expression.
     *
     * @returns COM status code.
     * @param   hEvaluator      The evaluator to use for the expression.
     * @param   pachPlaceholder The placholder string.  Not zero terminated.
     * @param   cchPlaceholder  The length of the placeholder.
     * @param   pfOutputting    Where to return the result of the conditional. This
     *                          holds the current outputting state on input in case
     *                          someone want to sanity check anything.
     */
    HRESULT resolveConditionalExpr(RTEXPREVAL hEvaluator, const char *pachPlaceholder, size_t cchPlaceholder,
                                   bool *pfOutputting) RT_NOEXCEPT;

    /** @callback_method_impl{FNRTEXPREVALQUERYVARIABLE}  */
    static DECLCALLBACK(int) queryVariableForExpr(const char *pchName, size_t cchName, void *pvUser,
                                                  char **ppszValue) RT_NOEXCEPT;

    /**
     * Gets a variable.
     *
     * This is used both for getting replacements (@@VBOX_INSERT_XXX@@) and in
     * expressions (@@VBOX_INSERT[expr]@@, @@VBOX_COND[expr]@@).
     *
     * @returns VBox status code.
     * @retval  VERR_NOT_FOUND if variable does not exist.
     *
     * @param   pchName             The variable name.  Not zero terminated.
     * @param   cchName             The length of the name.
     * @param   rstrTmp             String object that can be used for keeping the
     *                              value returned via @a *ppszValue.
     * @param   ppszValue           If a value is desired, this is where to return
     *                              it.  This points to a string that should be
     *                              accessible for a little while after the function
     *                              returns.  Use @a rstrTmp for storage if
     *                              necessary.
     *
     *                              This will be NULL when called from the 'defined'
     *                              operator.  In which case no errors should be
     *                              set.
     * @throws  std::bad_alloc
     * @see     FNRTEXPREVALQUERYVARIABLE
     */
    virtual int queryVariable(const char *pchName, size_t cchName, Utf8Str &rstrTmp, const char **ppszValue);

    /**
     * Get the result of a conditional.
     *
     * @returns COM status code.
     * @param   pachPlaceholder     The placholder string.  Not zero terminated.
     * @param   cchPlaceholder      The length of the placeholder.
     * @param   pfOutputting        Where to return the result of the conditional.
     *                              This holds the current outputting state on input
     *                              in case someone want to sanity check anything.
     */
    virtual HRESULT getConditional(const char *pachPlaceholder, size_t cchPlaceholder, bool *pfOutputting);
};

#if 0 /* convert when we fix SUSE */
/**
 * SUSE unattended XML file editor.
 */
class UnattendedSUSEXMLScript : public UnattendedXMLScript
{
public:
    DECLARE_TRANSLATE_METHODS(UnattendedSUSEXMLScript)

    UnattendedSUSEXMLScript(VirtualBoxBase *pSetError, const char *pszDefaultFilename = "autoinst.xml")
        : UnattendedXMLScript(pSetError, pszDefaultFilename) {}
    ~UnattendedSUSEXMLScript() {}

    HRESULT parse();

protected:
    HRESULT setFieldInElement(xml::ElementNode *pElement, const DataId enmDataId, const Utf8Str &rStrValue);

private:
    //////////////////New functions//////////////////////////////
    /** @throws std::bad_alloc */
    HRESULT LoopThruSections(const xml::ElementNode *pelmRoot);
    /** @throws std::bad_alloc */
    HRESULT HandleUserAccountsSection(const xml::ElementNode *pelmSection);
    /** @throws std::bad_alloc */
    Utf8Str createProbableValue(const DataId enmDataId, const xml::ElementNode *pCurElem);
    /** @throws std::bad_alloc */
    Utf8Str createProbableUserHomeDir(const xml::ElementNode *pCurElem);
    //////////////////New functions//////////////////////////////
};
#endif


#endif /* !MAIN_INCLUDED_UnattendedScript_h */

