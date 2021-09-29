/* $Id$ */
/** @file
 * VBox Qt GUI - UIMessageCenter class declaration.
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

#ifndef FEQT_INCLUDED_SRC_globals_UIMessageCenter_h
#define FEQT_INCLUDED_SRC_globals_UIMessageCenter_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QObject>

/* GUI includes: */
#include "UILibraryDefs.h"
#include "UIMediumDefs.h"

/* COM includes: */
#include "COMEnums.h"
#include "CProgress.h"

/* Forward declarations: */
class UIHelpBrowserDialog;
class UIMedium;
struct StorageSlot;
#ifdef VBOX_WITH_DRAG_AND_DROP
class CGuest;
#endif


/** Possible message types. */
enum MessageType
{
    MessageType_Info = 1,
    MessageType_Question,
    MessageType_Warning,
    MessageType_Error,
    MessageType_Critical,
    MessageType_GuruMeditation
};
Q_DECLARE_METATYPE(MessageType);


/** Singleton QObject extension
  * providing GUI with corresponding messages. */
class SHARED_LIBRARY_STUFF UIMessageCenter : public QObject
{
    Q_OBJECT;

signals:

    /** Asks to show message-box.
      * @param  pParent           Brings the message-box parent.
      * @param  enmType           Brings the message-box type.
      * @param  strMessage        Brings the message.
      * @param  strDetails        Brings the details.
      * @param  iButton1          Brings the button 1 type.
      * @param  iButton2          Brings the button 2 type.
      * @param  iButton3          Brings the button 3 type.
      * @param  strButtonText1    Brings the button 1 text.
      * @param  strButtonText2    Brings the button 2 text.
      * @param  strButtonText3    Brings the button 3 text.
      * @param  strAutoConfirmId  Brings whether this message can be auto-confirmed. */
    void sigToShowMessageBox(QWidget *pParent, MessageType enmType,
                             const QString &strMessage, const QString &strDetails,
                             int iButton1, int iButton2, int iButton3,
                             const QString &strButtonText1, const QString &strButtonText2, const QString &strButtonText3,
                             const QString &strAutoConfirmId, const QString &strHelpKeyword) const;

public:

    /** Creates message-center singleton. */
    static void create();
    /** Destroys message-center singleton. */
    static void destroy();

    /** Defines whether warning with particular @a strWarningName is @a fShown. */
    void setWarningShown(const QString &strWarningName, bool fShown) const;
    /** Returns whether warning with particular @a strWarningName is shown. */
    bool warningShown(const QString &strWarningName) const;

    /** Shows a general type of 'Message'.
      * @param  pParent            Brings the message-box parent.
      * @param  enmType            Brings the message-box type.
      * @param  strMessage         Brings the message.
      * @param  strDetails         Brings the details.
      * @param  pcszAutoConfirmId  Brings the auto-confirm ID.
      * @param  iButton1           Brings the button 1 type.
      * @param  iButton2           Brings the button 2 type.
      * @param  iButton3           Brings the button 3 type.
      * @param  strButtonText1     Brings the button 1 text.
      * @param  strButtonText2     Brings the button 2 text.
      * @param  strButtonText3     Brings the button 3 text.
      * @param  strHelpKeyword     Brings the help keyword string. */
    int message(QWidget *pParent, MessageType enmType,
                const QString &strMessage, const QString &strDetails,
                const char *pcszAutoConfirmId = 0,
                int iButton1 = 0, int iButton2 = 0, int iButton3 = 0,
                const QString &strButtonText1 = QString(),
                const QString &strButtonText2 = QString(),
                const QString &strButtonText3 = QString(),
                const QString &strHelpKeyword = QString()) const;

    /** Shows an 'Error' type of 'Message'.
      * Provides single Ok button.
      * @param  pParent            Brings the message-box parent.
      * @param  enmType            Brings the message-box type.
      * @param  strMessage         Brings the message.
      * @param  strDetails         Brings the details.
      * @param  pcszAutoConfirmId  Brings the auto-confirm ID.
      * @param  strHelpKeyword     Brings the help keyword string. */
    void error(QWidget *pParent, MessageType enmType,
               const QString &strMessage,
               const QString &strDetails,
               const char *pcszAutoConfirmId = 0,
               const QString &strHelpKeyword = QString()) const;

    /** Shows an 'Error with Question' type of 'Message'.
      * Provides Ok and Cancel buttons (called same way by default).
      * @param  pParent              Brings the message-box parent.
      * @param  enmType              Brings the message-box type.
      * @param  strMessage           Brings the message.
      * @param  strDetails           Brings the details.
      * @param  pcszAutoConfirmId    Brings the auto-confirm ID.
      * @param  strOkButtonText      Brings the Ok button text.
      * @param  strCancelButtonText  Brings the Cancel button text.
      * @param  strHelpKeyword     Brings the help keyword string. */
    bool errorWithQuestion(QWidget *pParent, MessageType enmType,
                           const QString &strMessage,
                           const QString &strDetails,
                           const char *pcszAutoConfirmId = 0,
                           const QString &strOkButtonText = QString(),
                           const QString &strCancelButtonText = QString(),
                           const QString &strHelpKeyword = QString()) const;

    /** Shows an 'Alert' type of 'Error'.
      * Omit details.
      * @param  pParent            Brings the message-box parent.
      * @param  enmType            Brings the message-box type.
      * @param  strMessage         Brings the message.
      * @param  pcszAutoConfirmId  Brings the auto-confirm ID.
      * @param  strHelpKeyword     Brings the help keyword string. */
    void alert(QWidget *pParent, MessageType enmType,
               const QString &strMessage,
               const char *pcszAutoConfirmId = 0,
               const QString &strHelpKeyword = QString()) const;

    /** Shows a 'Question' type of 'Message'.
      * Omit details.
      * @param  pParent            Brings the message-box parent.
      * @param  enmType            Brings the message-box type.
      * @param  strMessage         Brings the message.
      * @param  pcszAutoConfirmId  Brings the auto-confirm ID.
      * @param  iButton1           Brings the button 1 type.
      * @param  iButton2           Brings the button 2 type.
      * @param  iButton3           Brings the button 3 type.
      * @param  strButtonText1     Brings the button 1 text.
      * @param  strButtonText2     Brings the button 2 text.
      * @param  strButtonText3     Brings the button 3 text. */
    int question(QWidget *pParent, MessageType enmType,
                 const QString &strMessage,
                 const char *pcszAutoConfirmId = 0,
                 int iButton1 = 0, int iButton2 = 0, int iButton3 = 0,
                 const QString &strButtonText1 = QString(),
                 const QString &strButtonText2 = QString(),
                 const QString &strButtonText3 = QString()) const;

    /** Shows a 'Binary' type of 'Question'.
      * Omit details. Provides Ok and Cancel buttons (called same way by default).
      * @param  pParent              Brings the message-box parent.
      * @param  enmType              Brings the message-box type.
      * @param  strMessage           Brings the message.
      * @param  pcszAutoConfirmId    Brings the auto-confirm ID.
      * @param  strOkButtonText      Brings the button 1 text.
      * @param  strCancelButtonText  Brings the button 2 text.
      * @param  fDefaultFocusForOk   Brings whether Ok button should be focused initially. */
    bool questionBinary(QWidget *pParent, MessageType enmType,
                        const QString &strMessage,
                        const char *pcszAutoConfirmId = 0,
                        const QString &strOkButtonText = QString(),
                        const QString &strCancelButtonText = QString(),
                        bool fDefaultFocusForOk = true) const;

    /** Shows a 'Trinary' type of 'Question'.
      * Omit details. Provides Yes, No and Cancel buttons (called same way by default).
      * @param  pParent               Brings the message-box parent.
      * @param  enmType               Brings the message-box type.
      * @param  strMessage            Brings the message.
      * @param  pcszAutoConfirmId     Brings the auto-confirm ID.
      * @param  strChoice1ButtonText  Brings the button 1 text.
      * @param  strChoice2ButtonText  Brings the button 2 text.
      * @param  strCancelButtonText   Brings the button 3 text. */
    int questionTrinary(QWidget *pParent, MessageType enmType,
                        const QString &strMessage,
                        const char *pcszAutoConfirmId = 0,
                        const QString &strChoice1ButtonText = QString(),
                        const QString &strChoice2ButtonText = QString(),
                        const QString &strCancelButtonText = QString()) const;

    /** Shows a general type of 'Message with Option'.
      * @param  pParent              Brings the message-box parent.
      * @param  enmType              Brings the message-box type.
      * @param  strMessage           Brings the message.
      * @param  strOptionText        Brings the option text.
      * @param  fDefaultOptionValue  Brings the default option value.
      * @param  iButton1             Brings the button 1 type.
      * @param  iButton2             Brings the button 2 type.
      * @param  iButton3             Brings the button 3 type.
      * @param  strButtonText1       Brings the button 1 text.
      * @param  strButtonText2       Brings the button 2 text.
      * @param  strButtonText3       Brings the button 3 text. */
    int messageWithOption(QWidget *pParent, MessageType enmType,
                          const QString &strMessage,
                          const QString &strOptionText,
                          bool fDefaultOptionValue = true,
                          int iButton1 = 0, int iButton2 = 0, int iButton3 = 0,
                          const QString &strButtonText1 = QString(),
                          const QString &strButtonText2 = QString(),
                          const QString &strButtonText3 = QString()) const;

    /** Shows modal progress-dialog.
      * @param  comProgress   Brings the progress this dialog is based on.
      * @param  strTitle      Brings the title.
      * @param  strImage      Brings the image.
      * @param  pParent       Brings the parent.
      * @param  cMinDuration  Brings the minimum diration to show this dialog after expiring it. */
    bool showModalProgressDialog(CProgress &comProgress, const QString &strTitle,
                                 const QString &strImage = "", QWidget *pParent = 0,
                                 int cMinDuration = 2000);

    /** @name Startup warnings.
      * @{ */
        void cannotFindLanguage(const QString &strLangId, const QString &strNlsPath) const;
        void cannotLoadLanguage(const QString &strLangFile) const;

        void cannotInitUserHome(const QString &strUserHome) const;
        void cannotInitCOM(HRESULT rc) const;

        void cannotHandleRuntimeOption(const QString &strOption) const;

#ifdef RT_OS_LINUX
        void warnAboutWrongUSBMounted() const;
#endif

        void cannotStartSelector() const;
        void cannotStartRuntime() const;
    /** @} */

    /** @name General COM warnings.
      * @{ */
        void cannotCreateVirtualBoxClient(const CVirtualBoxClient &comClient) const;
        void cannotAcquireVirtualBox(const CVirtualBoxClient &comClient) const;

        void cannotFindMachineByName(const CVirtualBox &comVBox, const QString &strName) const;
        void cannotFindMachineById(const CVirtualBox &comVBox, const QUuid &uId) const;
        void cannotSetExtraData(const CVirtualBox &comVBox, const QString &strKey, const QString &strValue);
        void cannotOpenMedium(const CVirtualBox &comVBox, const QString &strLocation, QWidget *pParent = 0) const;

        void cannotOpenSession(const CSession &comSession) const;
        void cannotOpenSession(const CMachine &comMachine) const;
        void cannotOpenSession(const CProgress &comProgress, const QString &strMachineName) const;

        void cannotSetExtraData(const CMachine &machine, const QString &strKey, const QString &strValue);

        void cannotAttachDevice(const CMachine &machine, UIMediumDeviceType type, const QString &strLocation,
                                const StorageSlot &storageSlot, QWidget *pParent = 0);
        void cannotDetachDevice(const CMachine &machine, UIMediumDeviceType type, const QString &strLocation,
                                const StorageSlot &storageSlot, QWidget *pParent = 0) const;
        bool cannotRemountMedium(const CMachine &machine, const UIMedium &medium,
                                 bool fMount, bool fRetry, QWidget *pParent = 0) const;

        void cannotSetSystemProperties(const CSystemProperties &properties, QWidget *pParent = 0) const;
        void cannotSaveMachineSettings(const CMachine &machine, QWidget *pParent = 0) const;
        void cannotApplyCloudMachineFormSettings(const CForm &comForm, const QString &strName, QWidget *pParent = 0) const;
        void cannotApplyCloudMachineFormSettings(const CProgress &comProgress, const QString &strName, QWidget *pParent = 0) const;

        void cannotAddDiskEncryptionPassword(const CConsole &console);
    /** @} */

    /** @name Common warnings.
      * @{ */
        bool confirmResetMachine(const QString &strNames) const;

        void cannotSaveSettings(const QString strDetails, QWidget *pParent = 0) const;
        void warnAboutUnaccessibleUSB(const COMBaseWithEI &object, QWidget *pParent = 0) const;
        void warnAboutStateChange(QWidget *pParent = 0) const;
        bool confirmSettingsReloading(QWidget *pParent = 0) const;
        int confirmRemovingOfLastDVDDevice(QWidget *pParent = 0) const;
        bool confirmStorageBusChangeWithOpticalRemoval(QWidget *pParent = 0) const;
        bool confirmStorageBusChangeWithExcessiveRemoval(QWidget *pParent = 0) const;
        bool warnAboutIncorrectPort(QWidget *pParent = 0) const;
        bool warnAboutIncorrectAddress(QWidget *pParent = 0) const;
        bool warnAboutEmptyGuestAddress(QWidget *pParent = 0) const;
        bool warnAboutNameShouldBeUnique(QWidget *pParent = 0) const;
        bool warnAboutRulesConflict(QWidget *pParent = 0) const;
        bool confirmCancelingPortForwardingDialog(QWidget *pParent = 0) const;
    /** @} */

    /** @name VirtualBox Manager warnings.
      * @{ */
        bool warnAboutInaccessibleMedia() const;

        bool confirmDiscardSavedState(const QString &strNames) const;
        bool confirmTerminateCloudInstance(const QString &strNames) const;
        bool confirmACPIShutdownMachine(const QString &strNames) const;
        bool confirmPowerOffMachine(const QString &strNames) const;
        bool confirmStartMultipleMachines(const QString &strNames) const;
    /** @} */

    /** @name VirtualBox Manager / Chooser Pane warnings.
      * @{ */
        bool confirmAutomaticCollisionResolve(const QString &strName, const QString &strGroupName) const;
        /// @todo move after fixing thread stuff
        void cannotSetGroups(const CMachine &machine) const;
        bool confirmMachineItemRemoval(const QStringList &names) const;
        int confirmMachineRemoval(const QList<CMachine> &machines) const;
        int confirmCloudMachineRemoval(const QList<CCloudMachine> &machines) const;
    /** @} */

    /** @name VirtualBox Manager / Snapshot Pane warnings.
      * @{ */
        int confirmSnapshotRestoring(const QString &strSnapshotName, bool fAlsoCreateNewSnapshot) const;
        bool confirmSnapshotRemoval(const QString &strSnapshotName) const;
        bool warnAboutSnapshotRemovalFreeSpace(const QString &strSnapshotName, const QString &strTargetImageName,
                                               const QString &strTargetImageMaxSize, const QString &strTargetFileSystemFree) const;
    /** @} */

    /** @name VirtualBox Manager / Extension Manager warnings.
      * @{ */
        bool confirmInstallExtensionPack(const QString &strPackName, const QString &strPackVersion,
                                         const QString &strPackDescription, QWidget *pParent = 0) const;
        bool confirmReplaceExtensionPack(const QString &strPackName, const QString &strPackVersionNew,
                                         const QString &strPackVersionOld, const QString &strPackDescription,
                                         QWidget *pParent = 0) const;
        bool confirmRemoveExtensionPack(const QString &strPackName, QWidget *pParent = 0) const;
    /** @} */

    /** @name VirtualBox Manager / Media Manager warnings.
      * @{ */
        bool confirmMediumRelease(const UIMedium &medium, bool fInduced, QWidget *pParent = 0) const;
        bool confirmMediumRemoval(const UIMedium &medium, QWidget *pParent = 0) const;
        int confirmDeleteHardDiskStorage(const QString &strLocation, QWidget *pParent = 0) const;
    /** @} */

    /** @name VirtualBox Manager / Network Manager warnings.
      * @{ */
        bool confirmHostOnlyInterfaceRemoval(const QString &strName, QWidget *pParent = 0) const;
        bool confirmNATNetworkRemoval(const QString &strName, QWidget *pParent = 0) const;
    /** @} */

    /** @name VirtualBox Manager / Cloud Profile Manager warnings.
      * @{ */
        bool confirmCloudProfileRemoval(const QString &strName, QWidget *pParent = 0) const;
        bool confirmCloudProfilesImport(QWidget *pParent = 0) const;
        int confirmCloudProfileManagerClosing(QWidget *pParent = 0) const;
    /** @} */

    /** @name VirtualBox Manager / Cloud Console Manager warnings.
      * @{ */
        bool confirmCloudConsoleApplicationRemoval(const QString &strName, QWidget *pParent = 0) const;
        bool confirmCloudConsoleProfileRemoval(const QString &strName, QWidget *pParent = 0) const;
    /** @} */

    /** @name VirtualBox Manager / Cloud networking warnings.
      * @{ */
        void cannotAcquireCloudProviderManager(const CVirtualBox &comVBox, QWidget *pParent = 0) const;
        void cannotFindCloudProvider(const CCloudProviderManager &comManager, const QUuid &uId, QWidget *pParent = 0) const;
        void cannotFindCloudProfile(const CCloudProvider &comProvider, const QString &strName, QWidget *pParent = 0) const;
        void cannotCreateCloudClient(const CCloudProfile &comProfile, QWidget *pParent = 0) const;
        void cannotAcquireCloudProviderManagerParameter(const CCloudProviderManager &comManager, QWidget *pParent = 0) const;
        void cannotAcquireCloudProviderParameter(const CCloudProvider &comProvider, QWidget *pParent = 0) const;
        void cannotAcquireCloudProfileParameter(const CCloudProfile &comProfile, QWidget *pParent = 0) const;
        void cannotAcquireCloudClientParameter(const CCloudClient &comClient, QWidget *pParent = 0) const;
        void cannotAcquireCloudClientParameter(const CProgress &comProgress, QWidget *pParent = 0) const;
        void cannotAcquireCloudMachineParameter(const CCloudMachine &comMachine, QWidget *pParent = 0) const;
        void cannotAcquireCloudMachineParameter(const CProgress &comProgress, QWidget *pParent = 0) const;
    /** @} */

    /** @name VirtualBox Manager / Cloud machine settings warnings.
      * @{ */
        void cannotAssignFormValue(const QString &strError, QWidget *pParent = 0) const;
    /** @} */

    /** @name VirtualBox Manager / Downloading warnings.
      * @{ */
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
        bool confirmLookingForGuestAdditions() const;
        bool confirmDownloadGuestAdditions(const QString &strUrl, qulonglong uSize) const;
        void cannotSaveGuestAdditions(const QString &strURL, const QString &strTarget) const;
        bool proposeMountGuestAdditions(const QString &strUrl, const QString &strSrc) const;

        bool confirmLookingForUserManual(const QString &strMissedLocation) const;
        bool confirmDownloadUserManual(const QString &strURL, qulonglong uSize) const;
        void cannotSaveUserManual(const QString &strURL, const QString &strTarget) const;

        bool confirmLookingForExtensionPack(const QString &strExtPackName, const QString &strExtPackVersion) const;
        bool confirmDownloadExtensionPack(const QString &strExtPackName, const QString &strURL, qulonglong uSize) const;
        void cannotSaveExtensionPack(const QString &strExtPackName, const QString &strFrom, const QString &strTo) const;
        bool proposeInstallExtentionPack(const QString &strExtPackName, const QString &strFrom, const QString &strTo) const;
        bool proposeDeleteExtentionPack(const QString &strTo) const;
        bool proposeDeleteOldExtentionPacks(const QStringList &strFiles) const;
#endif /* VBOX_GUI_WITH_NETWORK_MANAGER */
    /** @} */

    /** @name Runtime UI warnings.
      * @{ */
        bool cannotRestoreSnapshot(const CMachine &machine, const QString &strSnapshotName, const QString &strMachineName) const;
        bool cannotRestoreSnapshot(const CProgress &progress, const QString &strSnapshotName, const QString &strMachineName) const;
        void cannotStartMachine(const CConsole &console, const QString &strName) const;
        void cannotStartMachine(const CProgress &progress, const QString &strName) const;

        bool warnAboutNetworkInterfaceNotFound(const QString &strMachineName, const QString &strIfNames) const;
        bool warnAboutVirtExInactiveFor64BitsGuest(bool fHWVirtExSupported) const;
        bool warnAboutVirtExInactiveForRecommendedGuest(bool fHWVirtExSupported) const;

        void warnAboutVBoxSVCUnavailable() const;
        bool warnAboutGuruMeditation(const QString &strLogFolder);
        void showRuntimeError(const CConsole &console, bool fFatal, const QString &strErrorId, const QString &strErrorMsg) const;

        bool confirmInputCapture(bool &fAutoConfirmed) const;
        bool confirmGoingFullscreen(const QString &strHotKey) const;
        bool confirmGoingSeamless(const QString &strHotKey) const;
        bool confirmGoingScale(const QString &strHotKey) const;

        bool cannotEnterFullscreenMode(ULONG uWidth, ULONG uHeight, ULONG uBpp, ULONG64 uMinVRAM) const;
        void cannotEnterSeamlessMode(ULONG uWidth, ULONG uHeight, ULONG uBpp, ULONG64 uMinVRAM) const;
        bool cannotSwitchScreenInFullscreen(quint64 uMinVRAM) const;
        void cannotSwitchScreenInSeamless(quint64 uMinVRAM) const;

#ifdef VBOX_WITH_DRAG_AND_DROP
        /// @todo move to notification-center as progress notification .. one day :)
        void cannotDropDataToGuest(const CDnDTarget &dndTarget, QWidget *pParent = 0) const;
        void cannotDropDataToGuest(const CProgress &progress, QWidget *pParent = 0) const;
        void cannotDropDataToHost(const CDnDSource &dndSource, QWidget *pParent = 0) const;
        void cannotDropDataToHost(const CProgress &progress, QWidget *pParent = 0) const;
#endif /* VBOX_WITH_DRAG_AND_DROP */
    /** @} */

    /** @name VirtualBox Manager / Wizard warnings.
      * @{ */
        /// @todo move to notification-center after wizards get theirs.. :)
        void cannotAcquireHardDiskLocation(const CMedium &comMedium, QWidget *pParent = 0) const;
        void cannotDeleteHardDiskStorage(const CMedium &medium, const QString &strLocation, QWidget *pParent = 0) const;
        void cannotDeleteHardDiskStorage(const CProgress &progress, const QString &strLocation, QWidget *pParent = 0) const;
        void cannotTakeSnapshot(const CMachine &machine, const QString &strMachineName, QWidget *pParent = 0) const;
        void cannotTakeSnapshot(const CProgress &progress, const QString &strMachineName, QWidget *pParent = 0) const;
        void cannotFindSnapshotByName(const CMachine &comMachine, const QString &strName, QWidget *pParent = 0) const;
        bool confirmHardDisklessMachine(QWidget *pParent = 0) const;
        void cannotCreateMachine(const CVirtualBox &vbox, QWidget *pParent = 0) const;
        void cannotRegisterMachine(const CVirtualBox &vbox, const QString &strMachineName, QWidget *pParent = 0) const;
        void cannotOverwriteHardDiskStorage(const QString &strLocation, QWidget *pParent = 0) const;
        void cannotCreateHardDiskStorage(const CVirtualBox &vbox, const QString &strLocation,QWidget *pParent = 0) const;
        void cannotCreateHardDiskStorage(const CMedium &medium, const QString &strLocation, QWidget *pParent = 0) const;
        void cannotCreateHardDiskStorage(const CProgress &progress, const QString &strLocation, QWidget *pParent = 0) const;
        void cannotCreateHardDiskStorageInFAT(const QString &strLocation, QWidget *pParent = 0) const;
        void cannotCreateMediumStorage(const CVirtualBox &comVBox, const QString &strLocation, QWidget *pParent = 0) const;
        void cannotCreateMediumStorage(const CMedium &comMedium, const QString &strLocation, QWidget *pParent = 0) const;
        void cannotCreateMediumStorage(const CProgress &comProgress, const QString &strLocation, QWidget *pParent = 0) const;
        void cannotRemoveMachineFolder(const QString &strFolderName, QWidget *pParent = 0) const;
        void cannotRewriteMachineFolder(const QString &strFolderName, QWidget *pParent = 0) const;
        void cannotCreateMachineFolder(const QString &strFolderName, QWidget *pParent = 0) const;
        void cannotCreateAppliance(const CVirtualBox &comVBox, QWidget *pParent = 0) const;
        void cannotCreateVirtualSystemDescription(const CAppliance &comAppliance, QWidget *pParent = 0) const;
        void cannotAcquireVirtualSystemDescription(const CAppliance &comAppliance, QWidget *pParent = 0) const;
        void cannotAddVirtualSystemDescriptionValue(const CVirtualSystemDescription &comDescription, QWidget *pParent = 0) const;
        void cannotAcquireVirtualSystemDescriptionFormProperty(const CVirtualSystemDescriptionForm &comForm, QWidget *pParent = 0) const;
        void cannotImportAppliance(CAppliance &appliance, QWidget *pParent = 0) const;
        void cannotImportAppliance(const CProgress &progress, const QString &strPath, QWidget *pParent = 0) const;
        bool cannotCheckFiles(const CAppliance &comAppliance, QWidget *pParent = 0) const;
        bool cannotCheckFiles(const CVFSExplorer &comVFSExplorer, QWidget *pParent = 0) const;
        bool cannotCheckFiles(const CProgress &comProgress, QWidget *pParent = 0) const;
        bool cannotRemoveFiles(const CVFSExplorer &comVFSExplorer, QWidget *pParent = 0) const;
        bool cannotRemoveFiles(const CProgress &comProgress, QWidget *pParent = 0) const;
        bool confirmExportMachinesInSaveState(const QStringList &machineNames, QWidget *pParent = 0) const;
        bool cannotExportAppliance(const CAppliance &comAppliance, QWidget *pParent = 0) const;
        void cannotExportAppliance(const CMachine &machine, const QString &strPath, QWidget *pParent = 0) const;
        bool cannotExportAppliance(const CProgress &comProgress, const QString &strPath, QWidget *pParent = 0) const;
        bool cannotAddDiskEncryptionPassword(const CAppliance &comAppliance, QWidget *pParent = 0);
        void cannotRunUnattendedGuestInstall(const CUnattended &comUnattendedInstall, QWidget *pParent = 0);
        bool confirmOverridingFile(const QString &strPath, QWidget *pParent = 0) const;
        bool confirmOverridingFiles(const QVector<QString> &strPaths, QWidget *pParent = 0) const;
    /** @} */

public slots:

    /* Handlers: Help menu stuff: */
    void sltShowHelpWebDialog();
    void sltShowBugTracker();
    void sltShowForums();
    void sltShowOracle();
    void sltShowHelpAboutDialog();
    void sltShowHelpHelpDialog();
    void sltResetSuppressedMessages();
    void sltShowUserManual(const QString &strLocation);

    /// @todo move it away ..
    void sltHelpBrowserClosed();
    void sltHandleHelpRequest();
    void sltHandleHelpRequestWithKeyword(const QString &strHelpKeyword);

private slots:

    /** Shows message-box.
      * @param  pParent           Brings the message-box parent.
      * @param  enmType           Brings the message-box type.
      * @param  strMessage        Brings the message.
      * @param  strDetails        Brings the details.
      * @param  iButton1          Brings the button 1 type.
      * @param  iButton2          Brings the button 2 type.
      * @param  iButton3          Brings the button 3 type.
      * @param  strButtonText1    Brings the button 1 text.
      * @param  strButtonText2    Brings the button 2 text.
      * @param  strButtonText3    Brings the button 3 text.
      * @param  strAutoConfirmId  Brings whether this message can be auto-confirmed.
      * @param  strHelpKeyword    Brings the help keyword string. */
    void sltShowMessageBox(QWidget *pParent, MessageType enmType,
                           const QString &strMessage, const QString &strDetails,
                           int iButton1, int iButton2, int iButton3,
                           const QString &strButtonText1, const QString &strButtonText2, const QString &strButtonText3,
                           const QString &strAutoConfirmId, const QString &strHelpKeyword) const;

private:

    /** Constructs message-center. */
    UIMessageCenter();
    /** Destructs message-center. */
    ~UIMessageCenter();

    /** Prepares all. */
    void prepare();
    /** Cleanups all. */
    void cleanup();

    /** Shows message-box.
      * @param  pParent           Brings the message-box parent.
      * @param  enmType           Brings the message-box type.
      * @param  strMessage        Brings the message.
      * @param  strDetails        Brings the details.
      * @param  iButton1          Brings the button 1 type.
      * @param  iButton2          Brings the button 2 type.
      * @param  iButton3          Brings the button 3 type.
      * @param  strButtonText1    Brings the button 1 text.
      * @param  strButtonText2    Brings the button 2 text.
      * @param  strButtonText3    Brings the button 3 text.
      * @param  strAutoConfirmId  Brings whether this message can be auto-confirmed.
      * @param  strHelpKeyword    Brings the help keyowrd. */
    int showMessageBox(QWidget *pParent, MessageType type,
                       const QString &strMessage, const QString &strDetails,
                       int iButton1, int iButton2, int iButton3,
                       const QString &strButtonText1, const QString &strButtonText2, const QString &strButtonText3,
                       const QString &strAutoConfirmId, const QString &strHelpKeyword) const;

    /// @todo move it away ..
    void showHelpBrowser(const QString &strHelpFilePath, QWidget *pParent = 0);

    /** Holds the list of shown warnings. */
    mutable QStringList m_warnings;

    /** Holds UIHelpBrowserDialog instance. */
    UIHelpBrowserDialog *m_pHelpBrowserDialog;

    /** Holds the singleton message-center instance. */
    static UIMessageCenter *s_pInstance;
    /** Returns the singleton message-center instance. */
    static UIMessageCenter *instance();
    /** Allows for shortcut access. */
    friend UIMessageCenter &msgCenter();
};

/** Singleton Message Center 'official' name. */
inline UIMessageCenter &msgCenter() { return *UIMessageCenter::instance(); }


#endif /* !FEQT_INCLUDED_SRC_globals_UIMessageCenter_h */