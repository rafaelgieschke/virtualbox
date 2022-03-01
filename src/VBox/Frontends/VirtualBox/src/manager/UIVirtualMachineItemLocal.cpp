/* $Id$ */
/** @file
 * VBox Qt GUI - UIVirtualMachineItem class implementation.
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

/* Qt includes: */
#include <QFileInfo>
#include <QIcon>

/* GUI includes: */
#include "UICommon.h"
#include "UIConverter.h"
#include "UIErrorString.h"
#include "UIExtraDataManager.h"
#include "UIIconPool.h"
#include "UIVirtualMachineItemLocal.h"
#ifdef VBOX_WS_MAC
# include <ApplicationServices/ApplicationServices.h>
#endif /* VBOX_WS_MAC */

/* COM includes: */
#include "CSnapshot.h"
#include "CVirtualBoxErrorInfo.h"


/*********************************************************************************************************************************
*   Class UIVirtualMachineItemLocal implementation.                                                                              *
*********************************************************************************************************************************/

UIVirtualMachineItemLocal::UIVirtualMachineItemLocal(const CMachine &comMachine)
    : UIVirtualMachineItem(UIVirtualMachineItemType_Local)
    , m_comMachine(comMachine)
    , m_cSnaphot(0)
    , m_enmMachineState(KMachineState_Null)
    , m_enmSessionState(KSessionState_Null)
{
    recache();
}

UIVirtualMachineItemLocal::~UIVirtualMachineItemLocal()
{
}

void UIVirtualMachineItemLocal::recache()
{
    /* Determine attributes which are always available: */
    m_uId = m_comMachine.GetId();
    m_strSettingsFile = m_comMachine.GetSettingsFilePath();

    /* Now determine whether VM is accessible: */
    m_fAccessible = m_comMachine.GetAccessible();
    if (m_fAccessible)
    {
        /* Reset last access error information: */
        m_strAccessError.clear();

        /* Determine own VM attributes: */
        m_strName = m_comMachine.GetName();
        m_strOSTypeId = m_comMachine.GetOSTypeId();
        m_groups = m_comMachine.GetGroups().toList();

        /* Determine snapshot attributes: */
        CSnapshot comSnapshot = m_comMachine.GetCurrentSnapshot();
        m_strSnapshotName = comSnapshot.isNull() ? QString() : comSnapshot.GetName();
#if QT_VERSION >= QT_VERSION_CHECK(5, 8, 0)
        m_lastStateChange.setSecsSinceEpoch(m_comMachine.GetLastStateChange() / 1000);
#else
        m_lastStateChange.setTime_t(m_comMachine.GetLastStateChange() / 1000);
#endif
        m_cSnaphot = m_comMachine.GetSnapshotCount();

        /* Determine VM states: */
        m_enmMachineState = m_comMachine.GetState();
        m_strMachineStateName = gpConverter->toString(m_enmMachineState);
        m_machineStateIcon = gpConverter->toIcon(m_enmMachineState);
        m_enmSessionState = m_comMachine.GetSessionState();
        m_strSessionStateName = gpConverter->toString(m_enmSessionState);

        /* Determine configuration access level: */
        m_enmConfigurationAccessLevel = ::configurationAccessLevel(m_enmSessionState, m_enmMachineState);
        /* Also take restrictions into account: */
        if (   m_enmConfigurationAccessLevel != ConfigurationAccessLevel_Null
            && !gEDataManager->machineReconfigurationEnabled(m_uId))
            m_enmConfigurationAccessLevel = ConfigurationAccessLevel_Null;

        /* Determine PID finally: */
        if (   m_enmMachineState == KMachineState_PoweredOff
            || m_enmMachineState == KMachineState_Saved
            || m_enmMachineState == KMachineState_Teleported
            || m_enmMachineState == KMachineState_Aborted
            || m_enmMachineState == KMachineState_AbortedSaved
           )
        {
            m_pid = (ULONG) ~0;
        }
        else
        {
            m_pid = m_comMachine.GetSessionPID();
        }

        /* Determine whether we should show this VM details: */
        m_fHasDetails = gEDataManager->showMachineInVirtualBoxManagerDetails(m_uId);
    }
    else
    {
        /* Update last access error information: */
        m_strAccessError = UIErrorString::formatErrorInfo(m_comMachine.GetAccessError());

        /* Determine machine name on the basis of settings file only: */
        QFileInfo fi(m_strSettingsFile);
        m_strName = UICommon::hasAllowedExtension(fi.completeSuffix(), VBoxFileExts)
                  ? fi.completeBaseName()
                  : fi.fileName();
        /* Reset other VM attributes: */
        m_strOSTypeId = QString();
        m_groups.clear();

        /* Reset snapshot attributes: */
        m_strSnapshotName = QString();
        m_lastStateChange = QDateTime::currentDateTime();
        m_cSnaphot = 0;

        /* Reset VM states: */
        m_enmMachineState = KMachineState_Null;
        m_machineStateIcon = gpConverter->toIcon(KMachineState_Aborted);
        m_enmSessionState = KSessionState_Null;

        /* Reset configuration access level: */
        m_enmConfigurationAccessLevel = ConfigurationAccessLevel_Null;

        /* Reset PID finally: */
        m_pid = (ULONG) ~0;

        /* Reset whether we should show this VM details: */
        m_fHasDetails = true;
    }

    /* Recache item pixmap: */
    recachePixmap();

    /* Retranslate finally: */
    retranslateUi();
}

void UIVirtualMachineItemLocal::recachePixmap()
{
    /* If machine is accessible: */
    if (m_fAccessible)
    {
        /* First, we are trying to acquire personal machine guest OS type icon: */
        m_pixmap = generalIconPool().userMachinePixmapDefault(m_comMachine, &m_logicalPixmapSize);
        /* If there is nothing, we are using icon corresponding to cached guest OS type: */
        if (m_pixmap.isNull())
            m_pixmap = generalIconPool().guestOSTypePixmapDefault(m_strOSTypeId, &m_logicalPixmapSize);
    }
    /* Otherwise: */
    else
    {
        /* We are using "Other" guest OS type icon: */
        m_pixmap = generalIconPool().guestOSTypePixmapDefault("Other", &m_logicalPixmapSize);
    }
}

bool UIVirtualMachineItemLocal::isItemEditable() const
{
    return    accessible()
           && sessionState() == KSessionState_Unlocked;
}

bool UIVirtualMachineItemLocal::isItemRemovable() const
{
    return    !accessible()
           || sessionState() == KSessionState_Unlocked;
}

bool UIVirtualMachineItemLocal::isItemSaved() const
{
    return    accessible()
           && (   machineState() == KMachineState_Saved
               || machineState() == KMachineState_AbortedSaved);
}

bool UIVirtualMachineItemLocal::isItemPoweredOff() const
{
    return    accessible()
           && (   machineState() == KMachineState_PoweredOff
               || machineState() == KMachineState_Saved
               || machineState() == KMachineState_Teleported
               || machineState() == KMachineState_Aborted
               || machineState() == KMachineState_AbortedSaved);
}

bool UIVirtualMachineItemLocal::isItemStarted() const
{
    return    isItemRunning()
           || isItemPaused();
}

bool UIVirtualMachineItemLocal::isItemRunning() const
{
    return    accessible()
           && (   machineState() == KMachineState_Running
               || machineState() == KMachineState_Teleporting
               || machineState() == KMachineState_LiveSnapshotting);
}

bool UIVirtualMachineItemLocal::isItemRunningHeadless() const
{
    if (isItemRunning())
    {
        /* Open session to determine which frontend VM is started with: */
        CSession comSession = uiCommon().openExistingSession(id());
        if (!comSession.isNull())
        {
            /* Acquire the session name: */
            const QString strSessionName = comSession.GetMachine().GetSessionName();
            /* Close the session early: */
            comSession.UnlockMachine();
            /* Check whether we are in 'headless' session: */
            return strSessionName == "headless";
        }
    }
    return false;
}

bool UIVirtualMachineItemLocal::isItemPaused() const
{
    return    accessible()
           && (   machineState() == KMachineState_Paused
               || machineState() == KMachineState_TeleportingPausedVM);
}

bool UIVirtualMachineItemLocal::isItemStuck() const
{
    return    accessible()
           && machineState() == KMachineState_Stuck;
}

bool UIVirtualMachineItemLocal::isItemCanBeSwitchedTo() const
{
    return    const_cast<CMachine&>(m_comMachine).CanShowConsoleWindow()
           || isItemRunningHeadless();
}

void UIVirtualMachineItemLocal::retranslateUi()
{
    /* This is used in tool-tip generation: */
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
    const QString strDateTime = m_lastStateChange.date() == QDate::currentDate()
                              ? QLocale::system().toString(m_lastStateChange.time(), QLocale::ShortFormat)
                              : QLocale::system().toString(m_lastStateChange, QLocale::ShortFormat);
#else
    const QString strDateTime = (m_lastStateChange.date() == QDate::currentDate())
                              ? m_lastStateChange.time().toString(Qt::LocalDate)
                              : m_lastStateChange.toString(Qt::LocalDate);
#endif

    /* If machine is accessible: */
    if (m_fAccessible)
    {
        /* Update tool-tip: */
        m_strToolTipText = QString("<b>%1</b>").arg(m_strName);
        if (!m_strSnapshotName.isNull())
            m_strToolTipText += QString(" (%1)").arg(m_strSnapshotName);
        m_strToolTipText = tr("<nobr>%1<br></nobr>"
                              "<nobr>%2 since %3</nobr><br>"
                              "<nobr>Session %4</nobr>",
                              "VM tooltip (name, last state change, session state)")
                              .arg(m_strToolTipText)
                              .arg(gpConverter->toString(m_enmMachineState))
                              .arg(strDateTime)
                              .arg(gpConverter->toString(m_enmSessionState).toLower());
    }
    /* Otherwise: */
    else
    {
        /* Update tool-tip: */
        m_strToolTipText = tr("<nobr><b>%1</b><br></nobr>"
                              "<nobr>Inaccessible since %2</nobr>",
                              "Inaccessible VM tooltip (name, last state change)")
                              .arg(m_strSettingsFile)
                              .arg(strDateTime);

        /* We have our own translation for Null states: */
        m_strMachineStateName = tr("Inaccessible");
        m_strSessionStateName = tr("Inaccessible");
    }
}
