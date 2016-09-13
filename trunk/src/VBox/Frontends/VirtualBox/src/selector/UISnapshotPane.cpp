/* $Id$ */
/** @file
 * VBox Qt GUI - UISnapshotPane class implementation.
 */

/*
 * Copyright (C) 2006-2016 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifdef VBOX_WITH_PRECOMPILED_HEADERS
# include <precomp.h>
#else  /* !VBOX_WITH_PRECOMPILED_HEADERS */

/* Qt includes: */
# include <QDateTime>
# include <QHeaderView>
# include <QMenu>
# include <QScrollBar>
# include <QPointer>
# include <QApplication>

/* GUI includes: */
# include "UIIconPool.h"
# include "UIMessageCenter.h"
# include "VBoxSnapshotDetailsDlg.h"
# include "UISnapshotPane.h"
# include "VBoxTakeSnapshotDlg.h"
# include "UIWizardCloneVM.h"
# include "UIToolBar.h"
# include "UIVirtualBoxEventHandler.h"
# include "UIConverter.h"
# include "UIModalWindowManager.h"
# include "UIExtraDataManager.h"

/* COM includes: */
# include "CConsole.h"

#endif /* !VBOX_WITH_PRECOMPILED_HEADERS */

/* Qt includes: */
#if QT_VERSION < 0x050000
# include <QWindowsStyle>
#endif /* QT_VERSION < 0x050000 */


/** QTreeWidgetItem subclass for snapshots items. */
class SnapshotWgtItem : public QTreeWidgetItem
{
public:

    /** Item type for SnapshotWgtItem. */
    enum { ItemType = QTreeWidgetItem::UserType + 1 };

    /** Constructs normal snapshot item (child of tree-widget). */
    SnapshotWgtItem(UISnapshotPane *pSnapshotWidget, QTreeWidget *pTreeWidget, const CSnapshot &comSnapshot)
        : QTreeWidgetItem(pTreeWidget, ItemType)
        , m_pSnapshotWidget(pSnapshotWidget)
        , m_fCurrentState(false)
        , m_comSnapshot(comSnapshot)
    {
    }

    /** Constructs normal snapshot item (child of tree-widget-item). */
    SnapshotWgtItem(UISnapshotPane *pSnapshotWidget, QTreeWidgetItem *pRootItem, const CSnapshot &comSnapshot)
        : QTreeWidgetItem(pRootItem, ItemType)
        , m_pSnapshotWidget(pSnapshotWidget)
        , m_fCurrentState(false)
        , m_comSnapshot(comSnapshot)
    {
    }

    /** Constructs "current state" item (child of tree-widget). */
    SnapshotWgtItem(UISnapshotPane *pSnapshotWidget, QTreeWidget *pTreeWidget, const CMachine &comMachine)
        : QTreeWidgetItem(pTreeWidget, ItemType)
        , m_pSnapshotWidget(pSnapshotWidget)
        , m_fCurrentState(true)
        , m_comMachine(comMachine)
    {
        /* Fetch current machine state: */
        updateCurrentState(m_comMachine.GetState());
    }

    /** Constructs "current state" item (child of tree-widget-item). */
    SnapshotWgtItem(UISnapshotPane *pSnapshotWidget, QTreeWidgetItem *pRootItem, const CMachine &comMachine)
        : QTreeWidgetItem(pRootItem, ItemType)
        , m_pSnapshotWidget(pSnapshotWidget)
        , m_fCurrentState(true)
        , m_comMachine(comMachine)
    {
        /* Fetch current machine state: */
        updateCurrentState(m_comMachine.GetState());
    }

    /** Returns item data for corresponding @a iColumn and @a iRole. */
    QVariant data(int iColumn, int iRole) const
    {
        switch (iRole)
        {
            case Qt::DisplayRole:
            {
                /* Call to the base-class for "current state" item, compose ourselves otherwise: */
                return m_fCurrentState ? QTreeWidgetItem::data(iColumn, iRole) : QVariant(QString("%1%2")
                                         .arg(QTreeWidgetItem::data(iColumn, Qt::DisplayRole).toString())
                                         .arg(QTreeWidgetItem::data(iColumn, Qt::UserRole).toString()));
            }
            case Qt::SizeHintRole:
            {
                /* Determine the icon metric: */
                const QStyle *pStyle = QApplication::style();
                const int iIconMetric = pStyle->pixelMetric(QStyle::PM_SmallIconSize);
                /* Determine the minimum size-hint for this tree-widget-item: */
                const QSize baseSizeHint = QTreeWidgetItem::data(iColumn, iRole).toSize();
                /* Determine the effective height-hint for this tree-widget-item: */
                const int iEffectiveHeightHint = qMax(baseSizeHint.height(),
                                                      iIconMetric + 2 * 2 /* margins */);
                /* Return size-hint for this tree-widget-item: */
                return QSize(baseSizeHint.width(), iEffectiveHeightHint);
            }
            default:
                break;
        }

        /* Call to base-class: */
        return QTreeWidgetItem::data(iColumn, iRole);
    }

    /** Returns item text for corresponding @a iColumn. */
    QString text(int iColumn) const
    {
        return QTreeWidgetItem::data(iColumn, Qt::DisplayRole).toString();
    }

    /** Returns whether this is the "current state" item. */
    bool isCurrentStateItem() const
    {
        return m_comSnapshot.isNull();
    }

    /** Calculates and returns the current item level. */
    int level() const
    {
        const QTreeWidgetItem *pItem = this;
        int iResult = 0;
        while (pItem->parent())
        {
            ++iResult;
            pItem = pItem->parent();
        }
        return iResult;
    }

    /** Returns whether the font is bold. */
    bool bold() const { return font (0).bold(); }
    /** Defines whether the font is @a fBold. */
    void setBold(bool fBold)
    {
        /* Update font: */
        QFont myFont = font(0);
        myFont.setBold(fBold);
        setFont(0, myFont);

        /* Adjust text: */
        adjustText();
    }

    /** Returns whether the font is italic. */
    bool italic() const { return font (0).italic(); }
    /** Defines whether the font is @a fItalic. */
    void setItalic(bool fItalic)
    {
        /* Update font: */
        QFont myFont = font(0);
        myFont.setItalic(fItalic);
        setFont(0, myFont);

        /* Adjust text: */
        adjustText();
    }

    /** Returns item machine. */
    CMachine machine() const { return m_comMachine; }
    /** Returns item snapshot. */
    CSnapshot snapshot() const { return m_comSnapshot; }
    /** Returns item snapshot ID. */
    QString snapshotID() const { return m_strSnapshotID; }

    /** Recaches the item's contents. */
    void recache()
    {
        /* For "current state" item: */
        if (m_fCurrentState)
        {
            AssertReturnVoid(!m_comMachine.isNull());
            m_fCurrentStateModified = m_comMachine.GetCurrentStateModified();
            setText(0, m_fCurrentStateModified ?
                       UISnapshotPane::tr("Current State (changed)", "Current State (Modified)") :
                       UISnapshotPane::tr("Current State", "Current State (Unmodified)"));
            m_strDesc = m_fCurrentStateModified ?
                        UISnapshotPane::tr("The current state differs from the state stored in the current snapshot") :
                        parent() != 0 ?
                        UISnapshotPane::tr("The current state is identical to the state stored in the current snapshot") :
                        QString();
        }
        /* For others: */
        else
        {
            AssertReturnVoid(!m_comSnapshot.isNull());
            m_strSnapshotID = m_comSnapshot.GetId();
            setText(0, m_comSnapshot.GetName());
            m_fOnline = m_comSnapshot.GetOnline();
            setIcon(0, m_pSnapshotWidget->snapshotItemIcon(m_fOnline));
            m_strDesc = m_comSnapshot.GetDescription();
            m_timestamp.setTime_t(m_comSnapshot.GetTimeStamp() / 1000);
            m_fCurrentStateModified = false;
        }

        /* Adjust text: */
        adjustText();
        /* Update tool-tip: */
        recacheToolTip();
    }

    /** Returns current machine state. */
    KMachineState getCurrentState() const
    {
        /* Make sure machine is valid: */
        if (m_comMachine.isNull())
            return KMachineState_Null;

        /* Return cached state: */
        return m_enmMachineState;
    }

    /** Recaches current machine state. */
    void updateCurrentState(KMachineState enmState)
    {
        /* Make sure machine is valid: */
        if (m_comMachine.isNull())
            return;

        /* Set corresponding icon: */
        setIcon(0, gpConverter->toIcon(enmState));
        /* Cache new state: */
        m_enmMachineState = enmState;
        /* Update timestamp: */
        m_timestamp.setTime_t(m_comMachine.GetLastStateChange() / 1000);
    }

    /** Updates item age. */
    SnapshotAgeFormat updateAge()
    {
        /* Prepare age: */
        QString strAge;

        /* Age: [date time|%1d ago|%1h ago|%1min ago|%1sec ago] */
        SnapshotAgeFormat enmAgeFormat;
        const QDateTime now = QDateTime::currentDateTime();
        QDateTime then = m_timestamp;
        if (then > now)
            then = now; /* can happen if the host time is wrong */
        if (then.daysTo(now) > 30)
        {
            strAge = UISnapshotPane::tr(" (%1)").arg(then.toString(Qt::LocalDate));
            enmAgeFormat = SnapshotAgeFormat_Max;
        }
        else if (then.secsTo(now) > 60 * 60 * 24)
        {
            strAge = UISnapshotPane::tr(" (%1 ago)").arg(VBoxGlobal::daysToString(then.secsTo(now) / 60 / 60 / 24));
            enmAgeFormat = SnapshotAgeFormat_InDays;
        }
        else if (then.secsTo(now) > 60 * 60)
        {
            strAge = UISnapshotPane::tr(" (%1 ago)").arg(VBoxGlobal::hoursToString(then.secsTo(now) / 60 / 60));
            enmAgeFormat = SnapshotAgeFormat_InHours;
        }
        else if (then.secsTo(now) > 60)
        {
            strAge = UISnapshotPane::tr(" (%1 ago)").arg(VBoxGlobal::minutesToString(then.secsTo(now) / 60));
            enmAgeFormat = SnapshotAgeFormat_InMinutes;
        }
        else
        {
            strAge = UISnapshotPane::tr(" (%1 ago)").arg(VBoxGlobal::secondsToString(then.secsTo(now)));
            enmAgeFormat = SnapshotAgeFormat_InSeconds;
        }

        /* Update data: */
        setData(0, Qt::UserRole, strAge);

        /* Return age: */
        return enmAgeFormat;
    }

protected:

    /** Adjusts item text. */
    void adjustText()
    {
        if (!treeWidget()) return; /* only for initialised items */
        QFontMetrics metrics(font(0));
        int iHei0 = (metrics.height() > 16 ?
                     metrics.height() /* text */ : 16 /* icon */) +
                    2 * 2 /* 2 pixel per margin */;
        int iWid0 = metrics.width(text(0)) /* text */ +
                    treeWidget()->indentation() /* indent */ +
                    16 /* icon */;

        /* Adjust size finally: */
        setSizeHint(0, QSize(iWid0, iHei0));
    }

    /** Recaches item tool-tip. */
    void recacheToolTip()
    {
        /* Is the saved date today? */
        const QString strName = text(0);

        /* Compose date time: */
        const bool fDateTimeToday = m_timestamp.date() == QDate::currentDate();
        QString strDateTime = fDateTimeToday ?
                              m_timestamp.time().toString(Qt::LocalDate) :
                              m_timestamp.toString(Qt::LocalDate);

        /* Prepare details: */
        QString strDetails;

        /* For snapshot item: */
        if (!m_comSnapshot.isNull())
        {
            /* The current snapshot is always bold: */
            if (bold())
                strDetails = UISnapshotPane::tr(" (current, ", "Snapshot details");
            else
                strDetails = " (";

            /* Add online/offline information: */
            strDetails += m_fOnline ? UISnapshotPane::tr("online)", "Snapshot details")
                                    : UISnapshotPane::tr("offline)", "Snapshot details");

            /* Add date/time information: */
            if (fDateTimeToday)
                strDateTime = UISnapshotPane::tr("Taken at %1", "Snapshot (time)").arg(strDateTime);
            else
                strDateTime = UISnapshotPane::tr("Taken on %1", "Snapshot (date + time)").arg(strDateTime);
        }
        /* For "current state" item: */
        else
        {
            strDateTime = UISnapshotPane::tr("%1 since %2", "Current State (time or date + time)")
                          .arg(gpConverter->toString(m_enmMachineState)).arg(strDateTime);
        }

        /* Prepare tool-tip: */
        QString strToolTip = QString("<nobr><b>%1</b>%2</nobr><br><nobr>%3</nobr>")
                             .arg(strName).arg(strDetails).arg(strDateTime);

        /* Append description if any: */
        if (!m_strDesc.isEmpty())
            strToolTip += "<hr>" + m_strDesc;

        /* Assign tool-tip finally: */
        setToolTip(0, strToolTip);
    }

private:

    /** Holds the pointer to the snapshot-widget this item belongs to. */
    QPointer<UISnapshotPane> m_pSnapshotWidget;

    /** Holds whether this is a "current state" item. */
    bool                     m_fCurrentState;

    /** Holds the snapshot COM wrapper. */
    CSnapshot                m_comSnapshot;
    /** Holds the machine COM wrapper. */
    CMachine                 m_comMachine;

    /** Holds the current snapshot ID. */
    QString                  m_strSnapshotID;
    /** Holds whether the current snapshot is online one. */
    bool                     m_fOnline;

    /** Holds the item description. */
    QString                  m_strDesc;
    /** Holds the item timestamp. */
    QDateTime                m_timestamp;

    /** Holds whether the current state is modified. */
    bool                     m_fCurrentStateModified;
    /** Holds the cached machine state. */
    KMachineState            m_enmMachineState;
};


/** Simple guard block to prevent cyclic call caused by:
  * changing tree-widget item content (rename) leads to snapshot
  * update & snapshot update leads to changing tree-widget item content. */
class SnapshotEditBlocker
{
public:

    /** Constructs edit blocker. */
    SnapshotEditBlocker(bool &fProtector)
        : m_fProtector(fProtector)
    {
        m_fProtector = true;
    }

    /** Destructs edit blocker. */
    virtual ~SnapshotEditBlocker()
    {
        m_fProtector = false;
    }

private:

    /** Holds the protector flag reference. */
    bool &m_fProtector;
};


UISnapshotPane::UISnapshotPane(QWidget *pParent)
    : QIWithRetranslateUI<QWidget>(pParent)
    , m_pCurrentSnapshotItem(0)
    , m_fEditProtector(false)
    , m_pSnapshotItemActionGroup(new QActionGroup(this))
    , m_pCurrentStateItemActionGroup(new QActionGroup(this))
    , m_pActionRestoreSnapshot(new QAction(m_pSnapshotItemActionGroup))
    , m_pActionDeleteSnapshot(new QAction(m_pSnapshotItemActionGroup))
    , m_pActionShowSnapshotDetails(new QAction(m_pSnapshotItemActionGroup))
    , m_pActionTakeSnapshot(new QAction(m_pCurrentStateItemActionGroup))
    , m_pActionCloneSnapshot(new QAction(m_pCurrentStateItemActionGroup))
    , m_fShapshotOperationsAllowed(false)
{
    /* Apply UI decorations: */
    Ui::UISnapshotPane::setupUi(this);

    /* No header: */
    m_pTreeWidget->header()->hide();

#if QT_VERSION < 0x050000
    // WORKAROUND:
    // The snapshots widget is not very useful if there are a lot
    // of snapshots in a tree and the current Qt style decides not
    // to draw lines (branches) between the snapshot nodes; it is
    // then often unclear which snapshot is a child of another.
    // So on platforms whose styles do not normally draw branches,
    // we use QWindowsStyle which is present on every platform and
    // draws required thing like we want. */
// #if defined(RT_OS_DARWIN) || defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS)
    QWindowsStyle *pTreeWidgetStyle = new QWindowsStyle;
    m_pTreeWidget->setStyle(pTreeWidgetStyle);
    connect(m_pTreeWidget, SIGNAL(destroyed(QObject *)), pTreeWidgetStyle, SLOT(deleteLater()));
// #endif
#endif /* QT_VERSION < 0x050000 */

    /* Cache pixmaps: */
    m_snapshotIconOffline = UIIconPool::iconSet(":/snapshot_offline_16px.png");
    m_snapshotIconOnline = UIIconPool::iconSet(":/snapshot_online_16px.png");

    /* Determine icon metric: */
    const QStyle *pStyle = QApplication::style();
    const int iIconMetric = (int)(pStyle->pixelMetric(QStyle::PM_SmallIconSize) * 1.375);

    /* Create tool-bar: */
    UIToolBar *pToolBar = new UIToolBar(this);
    pToolBar->setIconSize(QSize(iIconMetric, iIconMetric));
    pToolBar->setSizePolicy (QSizePolicy::Fixed, QSizePolicy::Fixed);
    /* Add actions into tool-bar: */
    pToolBar->addAction(m_pActionTakeSnapshot);
    pToolBar->addSeparator();
    pToolBar->addAction(m_pActionRestoreSnapshot);
    pToolBar->addAction(m_pActionDeleteSnapshot);
    pToolBar->addAction(m_pActionShowSnapshotDetails);
    pToolBar->addSeparator();
    pToolBar->addAction(m_pActionCloneSnapshot);
    /* Add tool-bar into layout: */
    ((QVBoxLayout*)layout())->insertWidget(0, pToolBar);

    /* Setup action icons: */
    m_pActionRestoreSnapshot->setIcon(UIIconPool::iconSetFull(
        ":/snapshot_restore_22px.png", ":/snapshot_restore_16px.png",
        ":/snapshot_restore_disabled_22px.png", ":/snapshot_restore_disabled_16px.png"));
    m_pActionDeleteSnapshot->setIcon(UIIconPool::iconSetFull(
        ":/snapshot_delete_22px.png", ":/snapshot_delete_16px.png",
        ":/snapshot_delete_disabled_22px.png", ":/snapshot_delete_disabled_16px.png"));
    m_pActionShowSnapshotDetails->setIcon(UIIconPool::iconSetFull(
        ":/snapshot_show_details_22px.png", ":/snapshot_show_details_16px.png",
        ":/snapshot_show_details_disabled_22px.png", ":/snapshot_details_show_disabled_16px.png"));
    m_pActionTakeSnapshot->setIcon(UIIconPool::iconSetFull(
        ":/snapshot_take_22px.png", ":/snapshot_take_16px.png",
        ":/snapshot_take_disabled_22px.png", ":/snapshot_take_disabled_16px.png"));
    m_pActionCloneSnapshot->setIcon(UIIconPool::iconSetFull(
        ":/vm_clone_22px.png", ":/vm_clone_16px.png",
        ":/vm_clone_disabled_22px.png", ":/vm_clone_disabled_16px.png"));
    /* Setup action shortcuts: */
    m_pActionRestoreSnapshot->setShortcut(QString("Ctrl+Shift+R"));
    m_pActionDeleteSnapshot->setShortcut(QString("Ctrl+Shift+D"));
    m_pActionShowSnapshotDetails->setShortcut(QString("Ctrl+Space"));
    m_pActionTakeSnapshot->setShortcut(QString("Ctrl+Shift+S"));
    m_pActionCloneSnapshot->setShortcut(QString("Ctrl+Shift+C"));

    /* Setup timer: */
    m_ageUpdateTimer.setSingleShot(true);

    /* Setup tree-widget connections: */
    connect(m_pTreeWidget, SIGNAL(currentItemChanged(QTreeWidgetItem *, QTreeWidgetItem *)),
            this, SLOT(sltCurrentItemChanged(QTreeWidgetItem *)));
    connect(m_pTreeWidget, SIGNAL(customContextMenuRequested(const QPoint &)),
            this, SLOT(sltContextMenuRequested(const QPoint &)));
    connect(m_pTreeWidget, SIGNAL(itemChanged(QTreeWidgetItem *, int)),
            this, SLOT(sltItemChanged(QTreeWidgetItem *)));
    connect(m_pTreeWidget, SIGNAL(itemDoubleClicked(QTreeWidgetItem *, int)),
            this, SLOT(sltItemDoubleClicked(QTreeWidgetItem *)));
    /* Setup snapshot operation connections: */
    connect(m_pActionTakeSnapshot, SIGNAL(triggered()), this, SLOT(sltTakeSnapshot()));
    connect(m_pActionRestoreSnapshot, SIGNAL(triggered()), this, SLOT(sltRestoreSnapshot()));
    connect(m_pActionDeleteSnapshot, SIGNAL(triggered()), this, SLOT(sltDeleteSnapshot()));
    connect(m_pActionShowSnapshotDetails, SIGNAL(triggered()), this, SLOT(sltShowSnapshotDetails()));
    connect(m_pActionCloneSnapshot, SIGNAL(triggered()), this, SLOT(sltCloneSnapshot()));
    /* Setup Main event connections: */
    connect(gVBoxEvents, SIGNAL(sigMachineDataChange(QString)),
            this, SLOT(sltMachineDataChange(QString)));
    connect(gVBoxEvents, SIGNAL(sigMachineStateChange(QString, KMachineState)),
            this, SLOT(sltMachineStateChange(QString, KMachineState)));
    connect(gVBoxEvents, SIGNAL(sigSessionStateChange(QString, KSessionState)),
            this, SLOT(sltSessionStateChange(QString, KSessionState)));
    /* Setup timer event connections: */
    connect(&m_ageUpdateTimer, SIGNAL(timeout()), this, SLOT(sltUpdateSnapshotsAge()));

    /* Translate finally: */
    retranslateUi();
}

void UISnapshotPane::setMachine(const CMachine &comMachine)
{
    /* Cache passed machine: */
    m_comMachine = comMachine;

    /* Cache machine details: */
    if (m_comMachine.isNull())
    {
        m_strMachineID = QString();
        m_enmSessionState = KSessionState_Null;
        m_fShapshotOperationsAllowed = false;
    }
    else
    {
        m_strMachineID = comMachine.GetId();
        m_enmSessionState = comMachine.GetSessionState();
        m_fShapshotOperationsAllowed = gEDataManager->machineSnapshotOperationsEnabled(m_strMachineID);
    }

    /* Refresh everything: */
    refreshAll();
}

void UISnapshotPane::retranslateUi()
{
    /* Translate uic generated strings: */
    Ui::UISnapshotPane::retranslateUi(this);

    /* Translate actions names: */
    m_pActionRestoreSnapshot->setText(tr("&Restore Snapshot"));
    m_pActionDeleteSnapshot->setText(tr("&Delete Snapshot"));
    m_pActionShowSnapshotDetails->setText(tr("S&how Details"));
    m_pActionTakeSnapshot->setText(tr("Take &Snapshot"));
    m_pActionCloneSnapshot->setText(tr("&Clone..."));
    /* Translate actions status-tips: */
    m_pActionRestoreSnapshot->setStatusTip(tr("Restore selected snapshot of the virtual machine"));
    m_pActionDeleteSnapshot->setStatusTip(tr("Delete selected snapshot of the virtual machine"));
    m_pActionShowSnapshotDetails->setStatusTip(tr("Display a window with selected snapshot details"));
    m_pActionTakeSnapshot->setStatusTip(tr("Take a snapshot of the current virtual machine state"));
    m_pActionCloneSnapshot->setStatusTip(tr("Clone selected virtual machine"));
    /* Translate actions tool-tips: */
    m_pActionRestoreSnapshot->setToolTip(m_pActionRestoreSnapshot->statusTip() +
        QString(" (%1)").arg(m_pActionRestoreSnapshot->shortcut().toString()));
    m_pActionDeleteSnapshot->setToolTip(m_pActionDeleteSnapshot->statusTip() +
        QString(" (%1)").arg(m_pActionDeleteSnapshot->shortcut().toString()));
    m_pActionShowSnapshotDetails->setToolTip(m_pActionShowSnapshotDetails->statusTip() +
        QString(" (%1)").arg(m_pActionShowSnapshotDetails->shortcut().toString()));
    m_pActionTakeSnapshot->setToolTip(m_pActionTakeSnapshot->statusTip() +
        QString(" (%1)").arg(m_pActionTakeSnapshot->shortcut().toString()));
    m_pActionCloneSnapshot->setToolTip(m_pActionCloneSnapshot->statusTip() +
        QString(" (%1)").arg(m_pActionCloneSnapshot->shortcut().toString()));
}

void UISnapshotPane::sltCurrentItemChanged(QTreeWidgetItem *pItem)
{
    /* Make the selected item visible: */
    const SnapshotWgtItem *pSnapshotItem = pItem ? static_cast<const SnapshotWgtItem*>(pItem) : 0;
    if (pSnapshotItem)
    {
        m_pTreeWidget->horizontalScrollBar()->setValue(0);
        m_pTreeWidget->scrollToItem(pSnapshotItem);
        m_pTreeWidget->horizontalScrollBar()->setValue(m_pTreeWidget->indentation() * pSnapshotItem->level());
    }

    /* Whether another direct session is open or not: */
    const bool fBusy = m_enmSessionState != KSessionState_Unlocked;

    /* Acquire machine state of the "current state" item: */
    KMachineState enmState = KMachineState_Null;
    if (currentStateItem())
        enmState = currentStateItem()->getCurrentState();

    /* Whether taking or deleting snapshots is possible right now: */
    const bool canTakeDeleteSnapshot =    !fBusy
                                       || enmState == KMachineState_PoweredOff
                                       || enmState == KMachineState_Saved
                                       || enmState == KMachineState_Aborted
                                       || enmState == KMachineState_Running
                                       || enmState == KMachineState_Paused;

    /* Enable/disable snapshot operations: */
    m_pActionRestoreSnapshot->setEnabled(!fBusy && m_pCurrentSnapshotItem && pSnapshotItem && !pSnapshotItem->isCurrentStateItem());
    m_pActionDeleteSnapshot->setEnabled(m_fShapshotOperationsAllowed &&
                                        canTakeDeleteSnapshot && m_pCurrentSnapshotItem && pSnapshotItem && !pSnapshotItem->isCurrentStateItem());
    m_pActionShowSnapshotDetails->setEnabled(m_pCurrentSnapshotItem && pSnapshotItem && !pSnapshotItem->isCurrentStateItem());
    m_pActionTakeSnapshot->setEnabled(m_fShapshotOperationsAllowed &&
                                      ((canTakeDeleteSnapshot && m_pCurrentSnapshotItem && pSnapshotItem && pSnapshotItem->isCurrentStateItem()) ||
                                      (pSnapshotItem && !m_pCurrentSnapshotItem)));
    m_pActionCloneSnapshot->setEnabled(!fBusy && pSnapshotItem);
}

void UISnapshotPane::sltContextMenuRequested(const QPoint &point)
{
    /* Search for corresponding snapshot item: */
    const QTreeWidgetItem *pItem = m_pTreeWidget->itemAt(point);
    const SnapshotWgtItem *pSnapshotItem = pItem ? static_cast<const SnapshotWgtItem*>(pItem) : 0;
    if (!pSnapshotItem)
        return;

    /* Prepare menu: */
    QMenu menu;
    /* For snapshot item: */
    if (m_pCurrentSnapshotItem && !pSnapshotItem->isCurrentStateItem())
    {
        menu.addAction(m_pActionRestoreSnapshot);
        menu.addAction(m_pActionDeleteSnapshot);
        menu.addAction(m_pActionShowSnapshotDetails);
        menu.addSeparator();
        menu.addAction(m_pActionCloneSnapshot);
    }
    /* For "current state" item: */
    else
    {
        menu.addAction(m_pActionTakeSnapshot);
        menu.addSeparator();
        menu.addAction(m_pActionCloneSnapshot);
    }

    /* Show menu: */
    menu.exec(m_pTreeWidget->viewport()->mapToGlobal(point));
}

void UISnapshotPane::sltItemChanged(QTreeWidgetItem *pItem)
{
    /* Make sure nothing is being edited currently: */
    if (m_fEditProtector)
        return;

    /* Make sure snapshot item was changed: */
    const SnapshotWgtItem *pSnapshotItem = pItem ? static_cast<const SnapshotWgtItem*>(pItem) : 0;

    if (pSnapshotItem)
    {
        /* Rename corresponding snapshot: */
        CSnapshot comSnapshot = pSnapshotItem->snapshotID().isNull() ? CSnapshot() : m_comMachine.FindSnapshot(pSnapshotItem->snapshotID());
        if (!comSnapshot.isNull() && comSnapshot.isOk() && comSnapshot.GetName() != pSnapshotItem->text(0))
            comSnapshot.SetName(pSnapshotItem->text(0));
    }
}

void UISnapshotPane::sltItemDoubleClicked(QTreeWidgetItem *pItem)
{
    /* Make sure nothing is being edited currently: */
    if (m_fEditProtector)
        return;

    /* Make sure snapshot item was double-clicked: */
    const SnapshotWgtItem *pSnapshotItem = pItem ? static_cast<const SnapshotWgtItem*>(pItem) : 0;
    if (!pSnapshotItem)
        return;

    /* Handle Ctrl+DoubleClick: */
    if (QApplication::keyboardModifiers() == Qt::ControlModifier)
    {
        /* As snapshot-restore procedure: */
        sltRestoreSnapshot(true /* suppress non-critical warnings */);
    }
}

void UISnapshotPane::sltTakeSnapshot()
{
    takeSnapshot();
}

void UISnapshotPane::sltRestoreSnapshot(bool fSuppressNonCriticalWarnings /* = false */)
{
    /* Get currently chosen item: */
    const SnapshotWgtItem *pSnapshotItem = m_pTreeWidget->currentItem() ? static_cast<const SnapshotWgtItem*>(m_pTreeWidget->currentItem()) : 0;
    AssertReturnVoid(pSnapshotItem);

    /* Detemine snapshot id: */
    const QString strSnapshotID = pSnapshotItem->snapshotID();
    AssertReturnVoid(!strSnapshotID.isNull());
    /* Get currently desired snapshot: */
    const CSnapshot comSnapshot = m_comMachine.FindSnapshot(strSnapshotID);

    /* Ask the user if he really wants to restore the snapshot: */
    int iResultCode = AlertButton_Ok;
    if (!fSuppressNonCriticalWarnings || m_comMachine.GetCurrentStateModified())
    {
        iResultCode = msgCenter().confirmSnapshotRestoring(comSnapshot.GetName(), m_comMachine.GetCurrentStateModified());
        if (iResultCode & AlertButton_Cancel)
            return;
    }

    /* If user also confirmed new snapshot creation: */
    if (iResultCode & AlertOption_CheckBox)
    {
        /* Take snapshot of changed current state: */
        m_pTreeWidget->setCurrentItem(currentStateItem());
        if (!takeSnapshot())
            return;
    }

    /* Open a direct session (this call will handle all errors): */
    CSession comSession = vboxGlobal().openSession(m_strMachineID);
    if (comSession.isNull())
        return;

    /* Restore chosen snapshot: */
    CMachine comMachine = comSession.GetMachine();
    CProgress comProgress = comMachine.RestoreSnapshot(comSnapshot);
    if (comMachine.isOk())
    {
        msgCenter().showModalProgressDialog(comProgress, m_comMachine.GetName(), ":/progress_snapshot_restore_90px.png");
        if (comProgress.GetResultCode() != 0)
            msgCenter().cannotRestoreSnapshot(comProgress, comSnapshot.GetName(), m_comMachine.GetName());
    }
    else
        msgCenter().cannotRestoreSnapshot(comMachine, comSnapshot.GetName(), m_comMachine.GetName());

    /* Unlock machine finally: */
    comSession.UnlockMachine();
}

void UISnapshotPane::sltDeleteSnapshot()
{
    /* Get currently chosen item: */
    const SnapshotWgtItem *pSnapshotItem = !m_pTreeWidget->currentItem() ? 0 :
        static_cast<const SnapshotWgtItem*>(m_pTreeWidget->currentItem());
    AssertReturnVoid(pSnapshotItem);

    /* Detemine snapshot id: */
    const QString strSnapshotID = pSnapshotItem->snapshotID();
    AssertReturnVoid(!strSnapshotID.isNull());
    /* Get currently desired snapshot: */
    const CSnapshot comSnapshot = m_comMachine.FindSnapshot(strSnapshotID);

    /* Confirm snapshot removal: */
    if (!msgCenter().confirmSnapshotRemoval(comSnapshot.GetName()))
        return;

    /** @todo check available space on the target filesystem etc etc. */
#if 0
    if (!msgCenter().warnAboutSnapshotRemovalFreeSpace(comSnapshot.GetName(),
                                                       "/home/juser/.VirtualBox/Machines/SampleVM/Snapshots/{01020304-0102-0102-0102-010203040506}.vdi",
                                                       "59 GiB",
                                                       "15 GiB"))
        return;
#endif

    /* Open a session (this call will handle all errors): */
    bool fBusy = m_enmSessionState != KSessionState_Unlocked;
    CSession comSession;
    if (fBusy)
        comSession = vboxGlobal().openExistingSession(m_strMachineID);
    else
        comSession = vboxGlobal().openSession(m_strMachineID);
    if (comSession.isNull())
        return;

    /* Remove chosen snapshot: */
    CMachine comMachine = comSession.GetMachine();
    CProgress comProgress = comMachine.DeleteSnapshot(strSnapshotID);
    if (comMachine.isOk())
    {
        msgCenter().showModalProgressDialog(comProgress, m_comMachine.GetName(), ":/progress_snapshot_discard_90px.png");
        if (comProgress.GetResultCode() != 0)
            msgCenter().cannotRemoveSnapshot(comProgress,  comSnapshot.GetName(), m_comMachine.GetName());
    }
    else
        msgCenter().cannotRemoveSnapshot(comMachine,  comSnapshot.GetName(), m_comMachine.GetName());

    /* Unlock machine finally: */
    comSession.UnlockMachine();
}

void UISnapshotPane::sltShowSnapshotDetails()
{
    /* Get currently chosen item: */
    const SnapshotWgtItem *pSnapshotItem = !m_pTreeWidget->currentItem() ? 0 :
        static_cast<const SnapshotWgtItem*>(m_pTreeWidget->currentItem());
    AssertReturnVoid(pSnapshotItem);

    /* Get desired snapshot: */
    const CSnapshot comSnapshot = pSnapshotItem->snapshot();
    AssertReturnVoid(!comSnapshot.isNull());

    // WTF?
    CMachine comMachine = comSnapshot.GetMachine();

    /* Show Snapshot Details dialog: */
    VBoxSnapshotDetailsDlg pDlg(this);
    pDlg.getFromSnapshot(comSnapshot);

    if (pDlg.exec() == QDialog::Accepted)
        pDlg.putBackToSnapshot();
}

void UISnapshotPane::sltCloneSnapshot()
{
    /* Get currently chosen item: */
    const SnapshotWgtItem *pSnapshotItem = !m_pTreeWidget->currentItem() ? 0 :
        static_cast<const SnapshotWgtItem*>(m_pTreeWidget->currentItem());
    AssertReturnVoid(pSnapshotItem);

    /* Get desired machine/snapshot: */
    CMachine comMachine;
    CSnapshot comSnapshot;
    if (pSnapshotItem->isCurrentStateItem())
        comMachine = pSnapshotItem->machine();
    else
    {
        comSnapshot = pSnapshotItem->snapshot();
        AssertReturnVoid(!comSnapshot.isNull());
        comMachine = comSnapshot.GetMachine();
    }
    AssertReturnVoid(!comMachine.isNull());

    /* Show Clone VM wizard: */
    UISafePointerWizard pWizard = new UIWizardCloneVM(this, comMachine, comSnapshot);
    pWizard->prepare();
    pWizard->exec();
    if (pWizard)
        delete pWizard;
}

void UISnapshotPane::sltMachineDataChange(QString strMachineID)
{
    /* Prevent snapshot editing in the meantime: */
    SnapshotEditBlocker guardBlock(m_fEditProtector);

    /* Make sure it's our VM: */
    if (strMachineID != m_strMachineID)
        return;

    /* Recache state current item: */
    currentStateItem()->recache();
}

void UISnapshotPane::sltMachineStateChange(QString strMachineID, KMachineState enmState)
{
    /* Prevent snapshot editing in the meantime: */
    SnapshotEditBlocker guardBlock(m_fEditProtector);

    /* Make sure it's our VM: */
    if (strMachineID != m_strMachineID)
        return;

    /* Recache new machine state: */
    currentStateItem()->recache();
    currentStateItem()->updateCurrentState(enmState);
}

void UISnapshotPane::sltSessionStateChange(QString strMachineID, KSessionState enmState)
{
    /* Prevent snapshot editing in the meantime: */
    SnapshotEditBlocker guardBlock(m_fEditProtector);

    /* Make sure it's our VM: */
    if (strMachineID != m_strMachineID)
        return;

    /* Recache new session state: */
    m_enmSessionState = enmState;
    sltCurrentItemChanged(m_pTreeWidget->currentItem());
}

void UISnapshotPane::sltUpdateSnapshotsAge()
{
    /* Stop timer if active: */
    if (m_ageUpdateTimer.isActive())
        m_ageUpdateTimer.stop();

    /* Search for smallest snapshot age to optimize timer timeout: */
    const SnapshotAgeFormat age = traverseSnapshotAge(m_pTreeWidget->invisibleRootItem());
    switch (age)
    {
        case SnapshotAgeFormat_InSeconds:
            m_ageUpdateTimer.setInterval(5 * 1000);
            break;
        case SnapshotAgeFormat_InMinutes:
            m_ageUpdateTimer.setInterval(60 * 1000);
            break;
        case SnapshotAgeFormat_InHours:
            m_ageUpdateTimer.setInterval(60 * 60 * 1000);
            break;
        case SnapshotAgeFormat_InDays:
            m_ageUpdateTimer.setInterval(24 * 60 * 60 * 1000);
            break;
        default:
            m_ageUpdateTimer.setInterval(0);
            break;
    }

    /* Restart timer if necessary: */
    if (m_ageUpdateTimer.interval() > 0)
        m_ageUpdateTimer.start();
}

bool UISnapshotPane::takeSnapshot()
{
    /* Prepare result: */
    bool fIsValid = true;

    // WTF?
    const SnapshotWgtItem *pSnapshotItem = m_pTreeWidget->currentItem() ? static_cast<const SnapshotWgtItem*>(m_pTreeWidget->currentItem()) : 0;
    AssertReturn(pSnapshotItem, (bool)0);

    /* Open a session (this call will handle all errors): */
    CSession comSession;
    if (m_enmSessionState != KSessionState_Unlocked)
        comSession = vboxGlobal().openExistingSession(m_strMachineID);
    else
        comSession = vboxGlobal().openSession(m_strMachineID);
    fIsValid = !comSession.isNull();

    if (fIsValid)
    {
        /* Get corresponding machine object also: */
        CMachine comMachine = comSession.GetMachine();

        /* Create take-snapshot dialog: */
        QWidget *pDlgParent = windowManager().realParentWindow(this);
        QPointer<VBoxTakeSnapshotDlg> pDlg = new VBoxTakeSnapshotDlg(pDlgParent, m_comMachine);
        windowManager().registerNewParent(pDlg, pDlgParent);

        // TODO: Assign corresponding icon through sub-dialog API: */
        pDlg->mLbIcon->setPixmap(vboxGlobal().vmGuestOSTypeIcon(m_comMachine.GetOSTypeId()));

        /* Search for the max available snapshot index: */
        int iMaxSnapShotIndex = 0;
        QString strSnapshotName = tr("Snapshot %1");
        QRegExp regExp(QString("^") + strSnapshotName.arg("([0-9]+)") + QString("$"));
        QTreeWidgetItemIterator iterator(m_pTreeWidget);
        while (*iterator)
        {
            QString strSnapshot = static_cast<SnapshotWgtItem*>(*iterator)->text(0);
            int iPos = regExp.indexIn(strSnapshot);
            if (iPos != -1)
                iMaxSnapShotIndex = regExp.cap(1).toInt() > iMaxSnapShotIndex ? regExp.cap(1).toInt() : iMaxSnapShotIndex;
            ++iterator;
        }
        // TODO: Assign corresponding snapshot name through sub-dialog API: */
        pDlg->mLeName->setText(strSnapshotName.arg(iMaxSnapShotIndex + 1));

        /* Show Take Snapshot dialog: */
        bool fDialogAccepted = pDlg->exec() == QDialog::Accepted;

        /* Is the dialog still valid? */
        if (pDlg)
        {
            /* Acquire snapshot name/description: */
            const QString strSnapshotName = pDlg->mLeName->text().trimmed();
            const QString strSnapshotDescription = pDlg->mTeDescription->toPlainText();

            /* Destroy dialog early: */
            delete pDlg;

            /* Was the dialog accepted? */
            if (fDialogAccepted)
            {
                /* Take snapshot: */
                QString strSnapshotID;
                CProgress comProgress = comMachine.TakeSnapshot(strSnapshotName, strSnapshotDescription, true, strSnapshotID);
                if (comMachine.isOk())
                {
                    msgCenter().showModalProgressDialog(comProgress, m_comMachine.GetName(), ":/progress_snapshot_create_90px.png");
                    if (!comProgress.isOk() || comProgress.GetResultCode() != 0)
                    {
                        msgCenter().cannotTakeSnapshot(comProgress, m_comMachine.GetName());
                        fIsValid = false;
                    }
                }
                else
                {
                    msgCenter().cannotTakeSnapshot(comMachine, m_comMachine.GetName());
                    fIsValid = false;
                }
            }
            else
                fIsValid = false;
        }
        else
            fIsValid = false;

        /* Unlock machine finally: */
        comSession.UnlockMachine();
    }

    /* Return result: */
    return fIsValid;
}

void UISnapshotPane::refreshAll()
{
    /* Prevent snapshot editing in the meantime: */
    SnapshotEditBlocker guardBlock(m_fEditProtector);

    /* If VM is null, just updated the current itm: */
    if (m_comMachine.isNull())
    {
        sltCurrentItemChanged();
        return;
    }

    /* Remember the selected item and it's first child: */
    QString strSelectedItem, strFirstChildOfSelectedItem;
    const SnapshotWgtItem *pSnapshotItem = !m_pTreeWidget->currentItem() ? 0 :
        static_cast<const SnapshotWgtItem*>(m_pTreeWidget->currentItem());
    if (pSnapshotItem)
    {
        strSelectedItem = pSnapshotItem->snapshotID();
        if (pSnapshotItem->child(0))
            strFirstChildOfSelectedItem = static_cast<const SnapshotWgtItem*>(pSnapshotItem->child(0))->snapshotID();
    }

    /* Clear the tree: */
    m_pTreeWidget->clear();

    /* If machine has snapshots: */
    if (m_comMachine.GetSnapshotCount() > 0)
    {
        /* Get the first snapshot: */
        const CSnapshot comSnapshot = m_comMachine.FindSnapshot(QString());

        /* Populate snapshot tree: */
        populateSnapshots(comSnapshot, 0);
        /* And make sure it has current snapshot item: */
        Assert(m_pCurrentSnapshotItem);

        /* Add the "current state" item as a child to current snapshot item: */
        SnapshotWgtItem *pCsi = new SnapshotWgtItem(this, m_pCurrentSnapshotItem, m_comMachine);
        pCsi->setBold(true);
        pCsi->recache();

        /* Search for a previously selected item: */
        SnapshotWgtItem *pCurrentItem = findItem(strSelectedItem);
        if (pCurrentItem == 0)
            pCurrentItem = findItem(strFirstChildOfSelectedItem);
        if (pCurrentItem == 0)
            pCurrentItem = currentStateItem();

        /* Choose current item: */
        m_pTreeWidget->scrollToItem(pCurrentItem);
        m_pTreeWidget->setCurrentItem(pCurrentItem);
        sltCurrentItemChanged(pCurrentItem);
    }
    /* If machine has no snapshots: */
    else
    {
        /* There is no current snapshot item: */
        m_pCurrentSnapshotItem = 0;

        /* Add the "current state" item as a child of tree-widget: */
        SnapshotWgtItem *pCsi = new SnapshotWgtItem(this, m_pTreeWidget, m_comMachine);
        pCsi->setBold(true);
        pCsi->recache();

        /* Choose current item: */
        m_pTreeWidget->setCurrentItem(pCsi);
        sltCurrentItemChanged(pCsi);
    }

    /* Update age: */
    sltUpdateSnapshotsAge();

    /* Adjust tree-widget: */
    m_pTreeWidget->resizeColumnToContents(0);
}

SnapshotWgtItem *UISnapshotPane::findItem(const QString &strSnapshotID) const
{
    /* Search for the first item with required ID: */
    QTreeWidgetItemIterator it(m_pTreeWidget);
    while (*it)
    {
        SnapshotWgtItem *pSnapshotItem = static_cast<SnapshotWgtItem*>(*it);
        if (pSnapshotItem->snapshotID() == strSnapshotID)
            return pSnapshotItem;
        ++it;
    }

    /* Null by default: */
    return 0;
}

SnapshotWgtItem *UISnapshotPane::currentStateItem() const
{
    /* Last child of the current snapshot item if any or first child of invisible root item otherwise: */
    QTreeWidgetItem *pCsi = m_pCurrentSnapshotItem ?
                            m_pCurrentSnapshotItem->child(m_pCurrentSnapshotItem->childCount() - 1) :
                            m_pTreeWidget->invisibleRootItem()->child(0);
    return static_cast<SnapshotWgtItem*>(pCsi);
}

void UISnapshotPane::populateSnapshots(const CSnapshot &comSnapshot, QTreeWidgetItem *pItem)
{
    /* Create a child of passed item: */
    SnapshotWgtItem *pSnapshotItem = pItem ? new SnapshotWgtItem(this, pItem, comSnapshot) :
                                             new SnapshotWgtItem(this, m_pTreeWidget, comSnapshot);
    /* And recache it's content: */
    pSnapshotItem->recache();

    /* Mark current snapshot item bold and remember it: */
    CSnapshot comCurrentSnapshot = m_comMachine.GetCurrentSnapshot();
    if (!comCurrentSnapshot.isNull() && comCurrentSnapshot.GetId() == comSnapshot.GetId())
    {
        pSnapshotItem->setBold(true);
        m_pCurrentSnapshotItem = pSnapshotItem;
    }

    /* Walk through the children recursively: */
    const CSnapshotVector snapshots = comSnapshot.GetChildren();
    foreach (const CSnapshot &comIteratedSnapshot, snapshots)
        populateSnapshots(comIteratedSnapshot, pSnapshotItem);

    /* Expand the newly created item: */
    pSnapshotItem->setExpanded(true);
    /* And mark it as editable: */
    pSnapshotItem->setFlags(pSnapshotItem->flags() | Qt::ItemIsEditable);
}

SnapshotAgeFormat UISnapshotPane::traverseSnapshotAge(QTreeWidgetItem *pItem) const
{
    /* Make sure passed root is of valid type: */
    SnapshotWgtItem *pSnapshotItem = pItem->type() == SnapshotWgtItem::ItemType ?
                                     static_cast<SnapshotWgtItem*>(pItem) : 0;

    /* Fetch the snapshot age of the root if it's valid: */
    SnapshotAgeFormat age = pSnapshotItem ? pSnapshotItem->updateAge() : SnapshotAgeFormat_Max;

    /* Walk through the children recursively: */
    for (int i = 0; i < pItem->childCount(); ++i)
    {
        /* Fetch the smallest snapshot age of the children: */
        const SnapshotAgeFormat newAge = traverseSnapshotAge(pItem->child(i));
        /* Remember the smallest snapshot age among existing: */
        age = newAge < age ? newAge : age;
    }

    /* Return result: */
    return age;
}

