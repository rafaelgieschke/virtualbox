/* $Id$ */
/** @file
 * VBox Qt GUI - UIWizardImportAppPageBasic2 class declaration.
 */

/*
 * Copyright (C) 2009-2021 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef FEQT_INCLUDED_SRC_wizards_importappliance_UIWizardImportAppPageBasic2_h
#define FEQT_INCLUDED_SRC_wizards_importappliance_UIWizardImportAppPageBasic2_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIWizardImportApp.h"
#include "UIWizardPage.h"

/* Forward declarations: */
class QCheckBox;
class QIComboBox;
class QLabel;
class QStackedWidget;
class QIRichTextLabel;
class UIApplianceImportEditorWidget;
class UIFilePathSelector;
class UIFormEditorWidget;

/** UIWizardPageBase extension for 2nd page of the Import Appliance wizard. */
class UIWizardImportAppPage2 : public UIWizardPageBase
{
protected:

    /** Constructs 2nd page base. */
    UIWizardImportAppPage2();

    /** Populates MAC address import policies. */
    void populateMACAddressImportPolicies();

    /** Updates page appearance. */
    virtual void updatePageAppearance();

    /** Updates MAC import policy combo tool-tips. */
    void updateMACImportPolicyComboToolTip();

    /** Refreshes form properties table. */
    void refreshFormPropertiesTable();

    /** Returns MAC address import policy. */
    MACAddressImportPolicy macAddressImportPolicy() const;
    /** Defines MAC address import @a enmPolicy. */
    void setMACAddressImportPolicy(MACAddressImportPolicy enmPolicy);

    /** Returns whether hard disks should be inported as VDIs. */
    bool importHDsAsVDI() const;

    /** Holds the settings widget 2 instance. */
    QStackedWidget *m_pSettingsWidget2;

    /** Holds the appliance widget instance. */
    UIApplianceImportEditorWidget *m_pApplianceWidget;
    /** Holds the import file-path label instance. */
    QLabel                        *m_pLabelImportFilePath;
    /** Holds the import file-path editor instance. */
    UIFilePathSelector            *m_pEditorImportFilePath;
    /** Holds the MAC address label instance. */
    QLabel                        *m_pLabelMACImportPolicy;
    /** Holds the MAC address combo instance. */
    QIComboBox                    *m_pComboMACImportPolicy;
    /** Holds the additional options label instance. */
    QLabel                        *m_pLabelAdditionalOptions;
    /** Holds the 'import HDs as VDI' checkbox instance. */
    QCheckBox                     *m_pCheckboxImportHDsAsVDI;

    /** Holds the Form Editor widget instance. */
    UIFormEditorWidget *m_pFormEditor;
};

/** UIWizardPage extension for 2nd page of the Import Appliance wizard, extends UIWizardImportAppPage2 as well. */
class UIWizardImportAppPageBasic2 : public UIWizardPage, public UIWizardImportAppPage2
{
    Q_OBJECT;
    Q_PROPERTY(MACAddressImportPolicy macAddressImportPolicy READ macAddressImportPolicy);
    Q_PROPERTY(bool importHDsAsVDI READ importHDsAsVDI);

public:

    /** Constructs 2nd basic page.
      * @param  strFileName  Brings appliance file name. */
    UIWizardImportAppPageBasic2(const QString &strFileName);

protected:

    /** Allows to access 'field()' from base part. */
    virtual QVariant fieldImp(const QString &strFieldName) const /* override */ { return UIWizardPage::field(strFieldName); }

    /** Handles translation event. */
    virtual void retranslateUi() /* override final */;

    /** Performs page initialization. */
    virtual void initializePage() /* override final */;
    /** Performs page cleanup. */
    virtual void cleanupPage() /* override final */;

    /** Performs page validation. */
    virtual bool validatePage() /* override final */;

    /** Updates page appearance. */
    virtual void updatePageAppearance() /* override final */;

private slots:

    /** Handles import path editor change. */
    void sltHandleImportPathEditorChange();
    /** Handles MAC address import policy combo change. */
    void sltHandleMACImportPolicyComboChange();

private:

    /** Handles the appliance file name. */
    QString  m_strFileName;

    /** Holds the description label instance. */
    QIRichTextLabel *m_pLabelDescription;

    /** Holds the signature/certificate info label instance. */
    QLabel *m_pCertLabel;

    /** Holds the certificate text template type. */
    enum {
        kCertText_Uninitialized = 0, kCertText_Unsigned,
        kCertText_IssuedTrusted,     kCertText_IssuedExpired,     kCertText_IssuedUnverified,
        kCertText_SelfSignedTrusted, kCertText_SelfSignedExpired, kCertText_SelfSignedUnverified
    } m_enmCertText;

    /** Holds the "signed by" information. */
    QString  m_strSignedBy;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_importappliance_UIWizardImportAppPageBasic2_h */