/* $Id$ */
/** @file
 * VBox Qt GUI - UIWizardCloneVDExpertPage class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_clonevd_UIWizardCloneVDExpertPage_h
#define FEQT_INCLUDED_SRC_wizards_clonevd_UIWizardCloneVDExpertPage_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UINativeWizardPage.h"

/* COM includes: */
#include "COMEnums.h"

/* Forward declarations: */
class QGroupBox;
class UIDiskFormatsComboBox;
class UIDiskVariantWidget;
class UIMediumSizeAndPathGroupBox;


/** Expert page of the Clone Virtual Disk Image wizard: */
class UIWizardCloneVDExpertPage : public UINativeWizardPage
{
    Q_OBJECT;

public:

    /** Constructs the page.
      * @param  comSourceVirtualDisk  Brings the initial source disk to make copy from.
      * @param  enmDeviceType         Brings the device type to limit format to. */
    UIWizardCloneVDExpertPage(KDeviceType enmDeviceType, qulonglong uSourceDiskLogicaSize);

private slots:

    /** Handles medium format change. */
    void sltMediumFormatChanged();

    /** Handles target disk change. */
    void sltSelectLocationButtonClicked();
    void sltMediumPathChanged(const QString &strPath);
    void sltMediumVariantChanged(qulonglong uVariant);
    void sltMediumSizeChanged(qulonglong uSize);

private:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE;

    /** Prepares the page. */
    virtual void initializePage() RT_OVERRIDE;

    /** Returns whether the page is complete. */
    virtual bool isComplete() const RT_OVERRIDE;

    /** Returns whether the page is valid. */
    virtual bool validatePage() RT_OVERRIDE;

    void prepare(KDeviceType enmDeviceType, qulonglong uSourceDiskLogicaSize);

    /** Sets the target disk name and location. */
    void setTargetLocation();
    void updateDiskWidgetsAfterMediumFormatChange();

    UIDiskFormatsComboBox *m_pFormatComboBox;
    UIDiskVariantWidget *m_pVariantWidget;
    UIMediumSizeAndPathGroupBox *m_pMediumSizePathGroupBox;
    QGroupBox *m_pFormatVariantGroupBox;
    KDeviceType m_enmDeviceType;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_clonevd_UIWizardCloneVDExpertPage_h */
