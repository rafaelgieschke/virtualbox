/* $Id$ */
/** @file
 * VBox Qt GUI - UIWizardCloneVDPageBasic1 class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_clonevd_UIWizardCloneVDPageBasic1_h
#define FEQT_INCLUDED_SRC_wizards_clonevd_UIWizardCloneVDPageBasic1_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UINativeWizardPage.h"

/* COM includes: */
#include "COMEnums.h"
#include "CMediumFormat.h"

/* Forward declarations: */
class QIRichTextLabel;
class UIDiskFormatsGroupBox;


// /** 1st page of the Clone Virtual Disk Image wizard (base part): */
// class UIWizardCloneVDPage1 : public UIWizardPageBase
// {
// protected:

//     /** Constructs page basis. */
//     UIWizardCloneVDPage1();

//     /** Adds format button.
//       * @param  pParent          Brings the parent to add button to.
//       * @param  pFormatsLayout   Brings the layout to insert button to.
//       * @param  enmDeviceType    Brings the device type all buttons should be restricted to.
//       * @param  comMediumFormat  Brings the medium format object to acquire format from.
//       * @param  fPreferred       Brings whether curretn format is preferred or not. */
//     void addFormatButton(QWidget *pParent,
//                          QVBoxLayout *pFormatsLayout,
//                          KDeviceType enmDeviceType,
//                          CMediumFormat comMediumFormat,
//                          bool fPreferred = false);

//     /** Returns 'mediumFormat' field value. */
//     CMediumFormat mediumFormat() const;
//     /** Defines 'mediumFormat' field value. */
//     void setMediumFormat(const CMediumFormat &comMediumFormat);

//     /** Holds the format button-group instance. */
//     QButtonGroup         *m_pFormatButtonGroup;
//     /** Holds the format description list. */
//     QList<CMediumFormat>  m_formats;
//     /** Holds the format name list. */
//     QStringList           m_formatNames;
// };


/** 2nd page of the Clone Virtual Disk Image wizard (basic extension): */
class UIWizardCloneVDPageBasic1 : public UINativeWizardPage
{
    Q_OBJECT;

public:

    /** Constructs basic page.
      * @param  enmDeviceType  Brings the device type to limit format to. */
    UIWizardCloneVDPageBasic1(KDeviceType enmDeviceType);

private:

    /** Handles translation event. */
    virtual void retranslateUi() /* override */;
    void prepare(KDeviceType enmDeviceType);

    /** Prepares the page. */
    virtual void initializePage() /* override */;

    /** Returns whether the page is complete. */
    virtual bool isComplete() const /* override */;

    /** Holds the description label instance. */
    QIRichTextLabel *m_pLabel;
    UIDiskFormatsGroupBox *m_pFormatGroupBox;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_clonevd_UIWizardCloneVDPageBasic1_h */
