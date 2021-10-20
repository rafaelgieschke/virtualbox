/* $Id$ */
/** @file
 * VBox Qt GUI - UIWizardImportAppPageExpert class implementation.
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

/* Qt includes: */
#include <QCheckBox>
#include <QFileInfo>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIComboBox.h"
#include "QIToolButton.h"
#include "UIApplianceImportEditorWidget.h"
#include "UICommon.h"
#include "UIEmptyFilePathSelector.h"
#include "UIFilePathSelector.h"
#include "UIFormEditorWidget.h"
#include "UIIconPool.h"
#include "UIMessageCenter.h"
#include "UIToolBox.h"
#include "UIVirtualBoxEventHandler.h"
#include "UIVirtualBoxManager.h"
#include "UIWizardImportApp.h"
#include "UIWizardImportAppPageExpert.h"
#include "UIWizardImportAppPageSettings.h"
#include "UIWizardImportAppPageSource.h"

/* COM includes: */
#include "CSystemProperties.h"

/* Namespaces: */
using namespace UIWizardImportAppSettings;
using namespace UIWizardImportAppSource;


UIWizardImportAppPageExpert::UIWizardImportAppPageExpert(bool fImportFromOCIByDefault, const QString &strFileName)
    : m_fImportFromOCIByDefault(fImportFromOCIByDefault)
    , m_strFileName(strFileName)
    , m_pToolBox(0)
    , m_pSourceLayout(0)
    , m_pSourceLabel(0)
    , m_pSourceComboBox(0)
    , m_pSettingsWidget1(0)
    , m_pLocalContainerLayout(0)
    , m_pFileSelector(0)
    , m_pCloudContainerLayout(0)
    , m_pProfileComboBox(0)
    , m_pProfileToolButton(0)
    , m_pProfileInstanceList(0)
    , m_pSettingsWidget2(0)
    , m_pApplianceWidget(0)
    , m_pLabelImportFilePath(0)
    , m_pEditorImportFilePath(0)
    , m_pLabelMACImportPolicy(0)
    , m_pComboMACImportPolicy(0)
    , m_pLabelAdditionalOptions(0)
    , m_pCheckboxImportHDsAsVDI(0)
    , m_pFormEditor(0)
{
    /* Prepare main layout: */
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    if (pMainLayout)
    {
        /* Prepare tool-box: */
        m_pToolBox = new UIToolBox(this);
        if (m_pToolBox)
        {
            /* Prepare source widget: */
            QWidget *pWidgetSource = new QWidget(m_pToolBox);
            if (pWidgetSource)
            {
                /* Prepare source layout: */
                m_pSourceLayout = new QGridLayout(pWidgetSource);
                if (m_pSourceLayout)
                {
                    m_pSourceLayout->setContentsMargins(0, 0, 0, 0);

                    /* Prepare source combo: */
                    m_pSourceComboBox = new QIComboBox(pWidgetSource);
                    if (m_pSourceComboBox)
                        m_pSourceLayout->addWidget(m_pSourceComboBox, 0, 0);

                    /* Prepare settings widget 1: */
                    m_pSettingsWidget1 = new QStackedWidget(pWidgetSource);
                    if (m_pSettingsWidget1)
                    {
                        /* Prepare local container: */
                        QWidget *pContainerLocal = new QWidget(m_pSettingsWidget1);
                        if (pContainerLocal)
                        {
                            /* Prepare local widget layout: */
                            m_pLocalContainerLayout = new QGridLayout(pContainerLocal);
                            if (m_pLocalContainerLayout)
                            {
                                m_pLocalContainerLayout->setContentsMargins(0, 0, 0, 0);
                                m_pLocalContainerLayout->setRowStretch(1, 1);

                                /* Prepare file-path selector: */
                                m_pFileSelector = new UIEmptyFilePathSelector(pContainerLocal);
                                if (m_pFileSelector)
                                {
                                    m_pFileSelector->setHomeDir(uiCommon().documentsPath());
                                    m_pFileSelector->setMode(UIEmptyFilePathSelector::Mode_File_Open);
                                    m_pFileSelector->setButtonPosition(UIEmptyFilePathSelector::RightPosition);
                                    m_pFileSelector->setEditable(true);
                                    m_pLocalContainerLayout->addWidget(m_pFileSelector, 0, 0);
                                }
                            }

                            /* Add into widget: */
                            m_pSettingsWidget1->addWidget(pContainerLocal);
                        }

                        /* Prepare cloud container: */
                        QWidget *pContainerCloud = new QWidget(m_pSettingsWidget1);
                        if (pContainerCloud)
                        {
                            /* Prepare cloud container layout: */
                            m_pCloudContainerLayout = new QGridLayout(pContainerCloud);
                            if (m_pCloudContainerLayout)
                            {
                                m_pCloudContainerLayout->setContentsMargins(0, 0, 0, 0);
                                m_pCloudContainerLayout->setRowStretch(1, 1);

                                /* Prepare profile layout: */
                                QHBoxLayout *pLayoutProfile = new QHBoxLayout;
                                if (pLayoutProfile)
                                {
                                    pLayoutProfile->setContentsMargins(0, 0, 0, 0);
                                    pLayoutProfile->setSpacing(1);

                                    /* Prepare profile combo-box: */
                                    m_pProfileComboBox = new QIComboBox(pContainerCloud);
                                    if (m_pProfileComboBox)
                                        pLayoutProfile->addWidget(m_pProfileComboBox);

                                    /* Prepare profile tool-button: */
                                    m_pProfileToolButton = new QIToolButton(pContainerCloud);
                                    if (m_pProfileToolButton)
                                    {
                                        m_pProfileToolButton->setIcon(UIIconPool::iconSet(":/cloud_profile_manager_16px.png",
                                                                                          ":/cloud_profile_manager_disabled_16px.png"));
                                        pLayoutProfile->addWidget(m_pProfileToolButton);
                                    }

                                    /* Add into layout: */
                                    m_pCloudContainerLayout->addLayout(pLayoutProfile, 0, 0);
                                }

                                /* Create profile instances table: */
                                m_pProfileInstanceList = new QListWidget(pContainerCloud);
                                if (m_pProfileInstanceList)
                                {
                                    const QFontMetrics fm(m_pProfileInstanceList->font());
                                    const int iFontWidth = fm.width('x');
                                    const int iTotalWidth = 50 * iFontWidth;
                                    const int iFontHeight = fm.height();
                                    const int iTotalHeight = 4 * iFontHeight;
                                    m_pProfileInstanceList->setMinimumSize(QSize(iTotalWidth, iTotalHeight));
//                                    m_pProfileInstanceList->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
                                    m_pProfileInstanceList->setAlternatingRowColors(true);
                                    m_pCloudContainerLayout->addWidget(m_pProfileInstanceList, 1, 0);
                                }
                            }

                            /* Add into widget: */
                            m_pSettingsWidget1->addWidget(pContainerCloud);
                        }

                        /* Add into layout: */
                        m_pSourceLayout->addWidget(m_pSettingsWidget1, 1, 0);
                    }
                }

                /* Add into tool-box: */
                m_pToolBox->insertPage(0, pWidgetSource, QString());
            }

            /* Prepare settings widget 2: */
            m_pSettingsWidget2 = new QStackedWidget(m_pToolBox);
            if (m_pSettingsWidget2)
            {
                /* Prepare appliance container: */
                QWidget *pContainerAppliance = new QWidget(m_pSettingsWidget2);
                if (pContainerAppliance)
                {
                    /* Prepare appliance layout: */
                    QGridLayout *pLayoutAppliance = new QGridLayout(pContainerAppliance);
                    if (pLayoutAppliance)
                    {
                        pLayoutAppliance->setContentsMargins(0, 0, 0, 0);

                        /* Prepare appliance widget: */
                        m_pApplianceWidget = new UIApplianceImportEditorWidget(pContainerAppliance);
                        if (m_pApplianceWidget)
                        {
                            m_pApplianceWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
                            pLayoutAppliance->addWidget(m_pApplianceWidget, 0, 0, 1, 3);
                        }

                        /* Prepare import path label: */
                        m_pLabelImportFilePath = new QLabel(pContainerAppliance);
                        if (m_pLabelImportFilePath)
                        {
                            m_pLabelImportFilePath->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                            pLayoutAppliance->addWidget(m_pLabelImportFilePath, 1, 0);
                        }
                        /* Prepare import path selector: */
                        m_pEditorImportFilePath = new UIFilePathSelector(pContainerAppliance);
                        if (m_pEditorImportFilePath)
                        {
                            m_pEditorImportFilePath->setResetEnabled(true);
                            m_pEditorImportFilePath->setDefaultPath(uiCommon().virtualBox().GetSystemProperties().GetDefaultMachineFolder());
                            m_pEditorImportFilePath->setPath(uiCommon().virtualBox().GetSystemProperties().GetDefaultMachineFolder());
                            m_pLabelImportFilePath->setBuddy(m_pEditorImportFilePath);
                            pLayoutAppliance->addWidget(m_pEditorImportFilePath, 1, 1, 1, 2);
                        }

                        /* Prepare MAC address policy label: */
                        m_pLabelMACImportPolicy = new QLabel(pContainerAppliance);
                        if (m_pLabelMACImportPolicy)
                        {
                            m_pLabelMACImportPolicy->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                            pLayoutAppliance->addWidget(m_pLabelMACImportPolicy, 2, 0);
                        }
                        /* Prepare MAC address policy combo: */
                        m_pComboMACImportPolicy = new QIComboBox(pContainerAppliance);
                        if (m_pComboMACImportPolicy)
                        {
                            m_pComboMACImportPolicy->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
                            m_pLabelMACImportPolicy->setBuddy(m_pComboMACImportPolicy);
                            pLayoutAppliance->addWidget(m_pComboMACImportPolicy, 2, 1, 1, 2);
                        }

                        /* Prepare additional options label: */
                        m_pLabelAdditionalOptions = new QLabel(pContainerAppliance);
                        if (m_pLabelAdditionalOptions)
                        {
                            m_pLabelAdditionalOptions->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                            pLayoutAppliance->addWidget(m_pLabelAdditionalOptions, 3, 0);
                        }
                        /* Prepare import HDs as VDIs checkbox: */
                        m_pCheckboxImportHDsAsVDI = new QCheckBox(pContainerAppliance);
                        {
                            m_pCheckboxImportHDsAsVDI->setCheckState(Qt::Checked);
                            pLayoutAppliance->addWidget(m_pCheckboxImportHDsAsVDI, 3, 1);
                        }
                    }

                    /* Add into layout: */
                    m_pSettingsWidget2->addWidget(pContainerAppliance);
                }

                /* Prepare form editor container: */
                QWidget *pContainerFormEditor = new QWidget(m_pSettingsWidget2);
                if (pContainerFormEditor)
                {
                    /* Prepare form editor layout: */
                    QVBoxLayout *pLayoutFormEditor = new QVBoxLayout(pContainerFormEditor);
                    if (pLayoutFormEditor)
                    {
                        pLayoutFormEditor->setContentsMargins(0, 0, 0, 0);

                        /* Prepare form editor widget: */
                        m_pFormEditor = new UIFormEditorWidget(pContainerFormEditor);
                        if (m_pFormEditor)
                            pLayoutFormEditor->addWidget(m_pFormEditor);
                    }

                    /* Add into layout: */
                    m_pSettingsWidget2->addWidget(pContainerFormEditor);
                }

                /* Add into tool-box: */
                m_pToolBox->insertPage(1, m_pSettingsWidget2, QString());
            }

            /* Add into layout: */
            pMainLayout->addWidget(m_pToolBox);
        }

        /* Add stretch: */
        pMainLayout->addStretch(1);
    }

    /* Setup connections: */
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigCloudProfileRegistered,
            this, &UIWizardImportAppPageExpert::sltHandleSourceComboChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigCloudProfileChanged,
            this, &UIWizardImportAppPageExpert::sltHandleSourceComboChange);
    connect(m_pSourceComboBox, static_cast<void(QIComboBox::*)(int)>(&QIComboBox::activated),
            this, &UIWizardImportAppPageExpert::sltHandleSourceComboChange);
    connect(m_pFileSelector, &UIEmptyFilePathSelector::pathChanged,
            this, &UIWizardImportAppPageExpert::sltHandleImportedFileSelectorChange);
    connect(m_pProfileComboBox, static_cast<void(QIComboBox::*)(int)>(&QIComboBox::currentIndexChanged),
            this, &UIWizardImportAppPageExpert::sltHandleProfileComboChange);
    connect(m_pProfileToolButton, &QIToolButton::clicked,
            this, &UIWizardImportAppPageExpert::sltHandleProfileButtonClick);
    connect(m_pProfileInstanceList, &QListWidget::currentRowChanged,
            this, &UIWizardImportAppPageExpert::sltHandleInstanceListChange);
    connect(m_pEditorImportFilePath, &UIFilePathSelector::pathChanged,
            this, &UIWizardImportAppPageExpert::sltHandleImportPathEditorChange);
    connect(m_pComboMACImportPolicy, static_cast<void(QIComboBox::*)(int)>(&QIComboBox::currentIndexChanged),
            this, &UIWizardImportAppPageExpert::sltHandleMACImportPolicyComboChange);
    connect(m_pCheckboxImportHDsAsVDI, &QCheckBox::stateChanged,
            this, &UIWizardImportAppPageExpert::sltHandleImportHDsAsVDICheckBoxChange);

    /* Parse passed full group name if any: */
    if (   m_fImportFromOCIByDefault
        && !m_strFileName.isEmpty())
    {
        const QString strProviderShortName = m_strFileName.section('/', 1, 1);
        const QString strProfileName = m_strFileName.section('/', 2, 2);
        if (!strProviderShortName.isEmpty() && !strProfileName.isEmpty())
        {
            m_strSource = strProviderShortName;
            m_strProfileName = strProfileName;
        }
    }
}

UIWizardImportApp *UIWizardImportAppPageExpert::wizard() const
{
    return qobject_cast<UIWizardImportApp*>(UINativeWizardPage::wizard());
}

void UIWizardImportAppPageExpert::retranslateUi()
{
    /* Translate tool-box: */
    if (m_pToolBox)
    {
        m_pToolBox->setPageTitle(0, UIWizardImportApp::tr("Source"));
        m_pToolBox->setPageTitle(1, UIWizardImportApp::tr("Settings"));
    }

    /* Translate hardcoded values of Source combo-box: */
    if (m_pSourceComboBox)
    {
        m_pSourceComboBox->setItemText(0, UIWizardImportApp::tr("Local File System"));
        m_pSourceComboBox->setItemData(0, UIWizardImportApp::tr("Import from local file system."), Qt::ToolTipRole);

        /* Translate received values of Source combo-box.
         * We are enumerating starting from 0 for simplicity: */
        for (int i = 0; i < m_pSourceComboBox->count(); ++i)
            if (isSourceCloudOne(m_pSourceComboBox, i))
            {
                m_pSourceComboBox->setItemText(i, m_pSourceComboBox->itemData(i, SourceData_Name).toString());
                m_pSourceComboBox->setItemData(i, UIWizardImportApp::tr("Import from cloud service provider."), Qt::ToolTipRole);
            }
    }

    /* Translate file selector: */
    if (m_pFileSelector)
    {
        m_pFileSelector->setChooseButtonToolTip(UIWizardImportApp::tr("Choose a virtual appliance file to import..."));
        m_pFileSelector->setFileDialogTitle(UIWizardImportApp::tr("Please choose a virtual appliance file to import"));
        m_pFileSelector->setFileFilters(UIWizardImportApp::tr("Open Virtualization Format (%1)").arg("*.ova *.ovf"));
    }

    /* Translate profile stuff: */
    if (m_pProfileToolButton)
        m_pProfileToolButton->setToolTip(UIWizardImportApp::tr("Open Cloud Profile Manager..."));

    /* Translate path selector label: */
    if (m_pLabelImportFilePath)
        m_pLabelImportFilePath->setText(tr("&Machine Base Folder:"));

    /* Translate MAC import policy label: */
    if (m_pLabelMACImportPolicy)
        m_pLabelMACImportPolicy->setText(tr("MAC Address &Policy:"));

    /* Translate additional options label: */
    if (m_pLabelAdditionalOptions)
        m_pLabelAdditionalOptions->setText(tr("Additional Options:"));
    /* Translate additional option check-box: */
    if (m_pCheckboxImportHDsAsVDI)
    {
        m_pCheckboxImportHDsAsVDI->setText(tr("&Import hard drives as VDI"));
        m_pCheckboxImportHDsAsVDI->setToolTip(tr("Import all the hard drives that belong to this appliance in VDI format."));
    }

    /* Translate separate stuff: */
    retranslateMACImportPolicyCombo(m_pComboMACImportPolicy);

    /* Update tool-tips: */
    updateSourceComboToolTip(m_pSourceComboBox);
    updateMACImportPolicyComboToolTip(m_pComboMACImportPolicy);
}

void UIWizardImportAppPageExpert::initializePage()
{
    /* Choose 1st tool to be chosen initially: */
    m_pToolBox->setCurrentPage(0);
    /* Populate sources: */
    populateSources(m_pSourceComboBox,
                    m_fImportFromOCIByDefault,
                    m_strSource);
    /* Translate page: */
    retranslateUi();

    /* Choose initially focused widget: */
    if (wizard()->isSourceCloudOne())
        m_pProfileInstanceList->setFocus();
    else
        m_pFileSelector->setFocus();

    /* Fetch it, asynchronously: */
    QMetaObject::invokeMethod(this, "sltAsyncInit", Qt::QueuedConnection);
}

bool UIWizardImportAppPageExpert::isComplete() const
{
    /* Initial result: */
    bool fResult = true;

    /* Check whether there was cloud source selected: */
    if (wizard()->isSourceCloudOne())
        fResult =    wizard()->cloudAppliance().isNotNull()
                  && wizard()->vsdImportForm().isNotNull();
    else
        fResult = wizard()->localAppliance().isNotNull();

    /* Return result: */
    return fResult;
}

bool UIWizardImportAppPageExpert::validatePage()
{
    /* Initial result: */
    bool fResult = true;

    /* Check whether there was cloud source selected: */
    if (wizard()->isSourceCloudOne())
    {
        /* Make sure table has own data committed: */
        m_pFormEditor->makeSureEditorDataCommitted();

        /* Check whether we have proper VSD form: */
        CVirtualSystemDescriptionForm comForm = wizard()->vsdImportForm();
        fResult = comForm.isNotNull();

        /* Give changed VSD back to appliance: */
        if (fResult)
        {
            comForm.GetVirtualSystemDescription();
            fResult = comForm.isOk();
            if (!fResult)
                msgCenter().cannotAcquireVirtualSystemDescriptionFormProperty(comForm);
        }
    }
    else
    {
        /* Make sure widget has own data committed: */
        m_pApplianceWidget->prepareImport();
    }

    /* Try to import appliance: */
    if (fResult)
        fResult = wizard()->importAppliance();

    /* Return result: */
    return fResult;
}

void UIWizardImportAppPageExpert::sltAsyncInit()
{
    /* If we have file name passed,
     * check if specified file contains valid appliance: */
    if (   !m_fImportFromOCIByDefault
        && !m_strFileName.isEmpty()
        && !wizard()->setFile(m_strFileName))
    {
        wizard()->reject();
        return;
    }

    /* Refresh page widgets: */
    sltHandleSourceComboChange();
}

void UIWizardImportAppPageExpert::sltHandleSourceComboChange()
{
    /* Update combo tool-tip: */
    updateSourceComboToolTip(m_pSourceComboBox);

    /* Update wizard fields: */
    wizard()->setSourceCloudOne(isSourceCloudOne(m_pSourceComboBox));

    /* Refresh page widgets: */
    UIWizardImportAppSource::refreshStackedWidget(m_pSettingsWidget1,
                                                  wizard()->isSourceCloudOne());
    UIWizardImportAppSettings::refreshStackedWidget(m_pSettingsWidget2,
                                                    wizard()->isSourceCloudOne());

    // WORKAROUND:
    // We want to free some vertical space from m_pSettingsWidget1:
    const bool fCloudCase = wizard()->isSourceCloudOne();
    m_pProfileComboBox->setVisible(fCloudCase);
    m_pProfileToolButton->setVisible(fCloudCase);
    m_pProfileInstanceList->setVisible(fCloudCase);

    /* Refresh local stuff: */
    sltHandleImportedFileSelectorChange();
    refreshMACAddressImportPolicies(m_pComboMACImportPolicy,
                                    wizard()->isSourceCloudOne());
    sltHandleMACImportPolicyComboChange();
    sltHandleImportHDsAsVDICheckBoxChange();

    /* Refresh cloud stuff: */
    refreshProfileCombo(m_pProfileComboBox,
                        source(m_pSourceComboBox),
                        m_strProfileName,
                        wizard()->isSourceCloudOne());
    sltHandleProfileComboChange();

    /* Notify about changes: */
    emit completeChanged();
}

void UIWizardImportAppPageExpert::sltHandleImportedFileSelectorChange()
{
    /* Update local stuff (only if something changed): */
    if (m_pFileSelector->isModified())
    {
        /* Create local appliance: */
        wizard()->setFile(path(m_pFileSelector));
        m_pFileSelector->resetModified();
    }

    /* Refresh appliance widget: */
    refreshApplianceWidget(m_pApplianceWidget,
                           wizard()->localAppliance(),
                           wizard()->isSourceCloudOne());

    /* Update import path: */
    sltHandleImportPathEditorChange();

    /* Notify about changes: */
    emit completeChanged();
}

void UIWizardImportAppPageExpert::sltHandleProfileComboChange()
{
    /* Refresh profile instances: */
    refreshCloudProfileInstances(m_pProfileInstanceList,
                                 source(m_pSourceComboBox),
                                 profileName(m_pProfileComboBox),
                                 wizard()->isSourceCloudOne());
    sltHandleInstanceListChange();

    /* Notify about changes: */
    emit completeChanged();
}

void UIWizardImportAppPageExpert::sltHandleProfileButtonClick()
{
    /* Open Cloud Profile Manager: */
    if (gpManager)
        gpManager->openCloudProfileManager();
}

void UIWizardImportAppPageExpert::sltHandleInstanceListChange()
{
    /* Create cloud appliance and VSD import form: */
    CAppliance comAppliance;
    CVirtualSystemDescriptionForm comForm;
    refreshCloudStuff(comAppliance,
                      comForm,
                      wizard(),
                      machineId(m_pProfileInstanceList),
                      source(m_pSourceComboBox),
                      profileName(m_pProfileComboBox),
                      wizard()->isSourceCloudOne());
    wizard()->setCloudAppliance(comAppliance);
    wizard()->setVsdImportForm(comForm);

    /* Refresh form properties table: */
    refreshFormPropertiesTable(m_pFormEditor,
                               wizard()->vsdImportForm(),
                               wizard()->isSourceCloudOne());

    /* Notify about changes: */
    emit completeChanged();
}

void UIWizardImportAppPageExpert::sltHandleImportPathEditorChange()
{
    AssertPtrReturnVoid(m_pApplianceWidget);
    AssertPtrReturnVoid(m_pEditorImportFilePath);
    m_pApplianceWidget->setVirtualSystemBaseFolder(m_pEditorImportFilePath->path());
}

void UIWizardImportAppPageExpert::sltHandleMACImportPolicyComboChange()
{
    /* Update combo tool-tip: */
    updateMACImportPolicyComboToolTip(m_pComboMACImportPolicy);

    /* Update wizard fields: */
    wizard()->setMACAddressImportPolicy(macAddressImportPolicy(m_pComboMACImportPolicy));
}

void UIWizardImportAppPageExpert::sltHandleImportHDsAsVDICheckBoxChange()
{
    /* Update wizard fields: */
    wizard()->setImportHDsAsVDI(isImportHDsAsVDI(m_pCheckboxImportHDsAsVDI));
}