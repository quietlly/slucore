﻿// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "bitcoingui.h"

#include "bitcoinunits.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "modaloverlay.h"
#include "networkstyle.h"
#include "notificator.h"
#include "openuridialog.h"
#include "optionsdialog.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "rpcconsole.h"
#include "utilitydialog.h"
#include "validation.h"
#include "rpc/server.h"
#include "navigationbar.h"
#include "titlebar.h"
#include "silubiumversionchecker.h"
#include "upload.h"

#include <QProcess>

#ifdef ENABLE_WALLET
#include "walletframe.h"
#include "walletmodel.h"
#include "wallet/wallet.h"
#endif // ENABLE_WALLET

#ifdef Q_OS_MAC
#include "macdockiconhandler.h"
#endif

#include "chainparams.h"
#include "init.h"
#include "ui_interface.h"
#include "util.h"

#include <iostream>

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDesktopWidget>
#include <QDragEnterEvent>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QDockWidget>
#include <QSizeGrip>
#include <QDesktopServices>

#if QT_VERSION < 0x050000
#include <QTextDocument>
#include <QUrl>
#else
#include <QUrlQuery>
#endif

const std::string BitcoinGUI::DEFAULT_UIPLATFORM =
        #if defined(Q_OS_MAC)
        "macosx"
        #elif defined(Q_OS_WIN)
        "windows"
        #else
        "other"
        #endif
        ;

/** Display name for default wallet name. Uses tilde to avoid name
 * collisions in the future with additional wallets */
const QString BitcoinGUI::DEFAULT_WALLET = "~Default";

/** Switch for showing the backup overlay modal screen*/
bool showBackupOverlay = false;

BitcoinGUI::BitcoinGUI(const PlatformStyle *_platformStyle, const NetworkStyle *networkStyle, QWidget *parent) :
    QMainWindow(parent),
    enableWallet(false),
    clientModel(0),
    walletFrame(0),
    unitDisplayControl(0),
    labelWalletEncryptionIcon(0),
    labelWalletHDStatusIcon(0),
    connectionsControl(0),
    labelBlocksIcon(0),
    progressBarLabel(0),
    progressBar(0),
    progressDialog(0),
    appMenuBar(0),
    appTitleBar(0),
    appNavigationBar(0),
    overviewAction(0),
    historyAction(0),
    quitAction(0),
    sendCoinsAction(0),
    sendCoinsMenuAction(0),
    usedSendingAddressesAction(0),
    usedReceivingAddressesAction(0),
    signMessageAction(0),
    verifyMessageAction(0),
    aboutAction(0),
    receiveCoinsAction(0),
    receiveCoinsMenuAction(0),
    optionsAction(0),
    toggleHideAction(0),
    encryptWalletAction(0),
    backupWalletAction(0),
    restoreWalletAction(0),
    changePassphraseAction(0),
    unlockWalletAction(0),
    lockWalletAction(0),
    aboutQtAction(0),
    openRPCConsoleAction(0),
    openAction(0),
    showHelpMessageAction(0),
    smartContractAction(0),
    createContractAction(0),
    sendToContractAction(0),
    callContractAction(0),
    QRCTokenAction(0),
    sendTokenAction(0),
    receiveTokenAction(0),
    addTokenAction(0),
    trayIcon(0),
    trayIconMenu(0),
    notificator(0),
    rpcConsole(0),
    helpMessageDialog(0),
    modalOverlay(0),
    silubiumVersionChecker(0),
    modalBackupOverlay(0),
    prevBlocks(0),
    spinnerFrame(0),
    platformStyle(_platformStyle)
{
    QSettings settings;
    if (!restoreGeometry(settings.value("MainWindowGeometry").toByteArray())) {
        // Restore failed (perhaps missing setting), center the window
        move(QApplication::desktop()->availableGeometry().center() - frameGeometry().center());
    }
    QString strLanguage=GetLangTerritory();
    QString windowTitle = tr(PACKAGE_NAME) + " - ";
#ifdef ENABLE_WALLET
    enableWallet = WalletModel::isWalletEnabled();
#endif // ENABLE_WALLET
    if(enableWallet)
    {
        if(strLanguage.contains("zh"))
            windowTitle += tr("钱包");
        else
            windowTitle += tr("Wallet");
    } else {
        if(strLanguage.contains("zh"))
            windowTitle += tr("节点");
        else
            windowTitle += tr("Node");
    }
    windowTitle += " " + networkStyle->getTitleAddText();
#ifndef Q_OS_MAC
    QApplication::setWindowIcon(networkStyle->getTrayAndWindowIcon());
    setWindowIcon(networkStyle->getTrayAndWindowIcon());
#else
    MacDockIconHandler::instance()->setIcon(networkStyle->getAppIcon());
#endif
    setWindowTitle(windowTitle);

#if defined(Q_OS_MAC) && QT_VERSION < 0x050000
    // This property is not implemented in Qt 5. Setting it has no effect.
    // A replacement API (QtMacUnifiedToolBar) is available in QtMacExtras.
    setUnifiedTitleAndToolBarOnMac(true);
#endif

    rpcConsole = new RPCConsole(_platformStyle, 0);
    helpMessageDialog = new HelpMessageDialog(this, false);
#ifdef ENABLE_WALLET
    if(enableWallet)
    {
        /** Create wallet frame and make it the central widget */
        walletFrame = new WalletFrame(_platformStyle, this);
        setCentralWidget(walletFrame);
    } else
#endif // ENABLE_WALLET
    {
        /* When compiled without wallet or -disablewallet is provided,
         * the central widget is the rpc console.
         */
        setCentralWidget(rpcConsole);
    }

    // Accept D&D of URIs
    setAcceptDrops(true);

    // Create actions for the toolbar, menu bar and tray/dock icon
    // Needs walletFrame to be initialized
    createActions();

    // Create application menu bar
    createMenuBar();

    // Create the toolbars
    createToolBars();

    // Create the title bar
    createTitleBars();

    // Create system tray icon and notification
    createTrayIcon(networkStyle);

    // Create status bar
    statusBar();

    // Enable the size grip (right size grip), add new size grip (left size grip) and set the status bar style
    statusBar()->setSizeGripEnabled(true);
    statusBar()->addWidget(new QSizeGrip(statusBar()));
    statusBar()->setStyleSheet("QSizeGrip { width: 3px; height: 25px; border: 0px solid black; } \n QStatusBar::item { border: 0px solid black; }");

    // Status bar notification icons
    QFrame *frameBlocks = new QFrame();
    frameBlocks->setContentsMargins(0,0,0,0);
    frameBlocks->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    QHBoxLayout *frameBlocksLayout = new QHBoxLayout(frameBlocks);
    frameBlocksLayout->setContentsMargins(3,0,0,0);
    frameBlocksLayout->setSpacing(3);
    unitDisplayControl = new UnitDisplayStatusBarControl(platformStyle);
    labelWalletEncryptionIcon = new QLabel();
    labelWalletHDStatusIcon = new QLabel();
    connectionsControl = new GUIUtil::ClickableLabel();
    labelBlocksIcon = new GUIUtil::ClickableLabel();
    labelStakingIcon = new QLabel();
    if(enableWallet)
    {
        frameBlocksLayout->addStretch();
        frameBlocksLayout->addWidget(unitDisplayControl);
        frameBlocksLayout->addStretch();
        frameBlocksLayout->addWidget(labelWalletEncryptionIcon);
        frameBlocksLayout->addWidget(labelWalletHDStatusIcon);
    }
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelStakingIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(connectionsControl);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelBlocksIcon);
    frameBlocksLayout->addStretch();

    if (gArgs.GetBoolArg("-staking", true))
    {
        QTimer *timerStakingIcon = new QTimer(labelStakingIcon);
        connect(timerStakingIcon, SIGNAL(timeout()), this, SLOT(updateStakingIcon()));
        timerStakingIcon->start(1000);

        updateStakingIcon();
    }

    // Progress bar and label for blocks download
    progressBarLabel = new QLabel();
    progressBarLabel->setVisible(false);
    progressBar = new GUIUtil::ProgressBar();
    progressBar->setAlignment(Qt::AlignCenter);
    progressBar->setVisible(false);

    // Override style sheet for progress bar for styles that have a segmented progress bar,
    // as they make the text unreadable (workaround for issue #1071)
    // See https://qt-project.org/doc/qt-4.8/gallery.html
    QString curStyle = QApplication::style()->metaObject()->className();
    if(curStyle == "QWindowsStyle" || curStyle == "QWindowsXPStyle")
    {
        progressBar->setStyleSheet("QProgressBar { background-color: #e8e8e8; border: 1px solid grey; border-radius: 7px; padding: 1px; text-align: center; } QProgressBar::chunk { background: QLinearGradient(x1: 0, y1: 0, x2: 1, y2: 0, stop: 0 #FF8000, stop: 1 orange); border-radius: 7px; margin: 0px; }");
    }

    statusBar()->addWidget(progressBarLabel);
    statusBar()->addWidget(progressBar);
    statusBar()->addPermanentWidget(frameBlocks);

    // Install event filter to be able to catch status tip events (QEvent::StatusTip)
    this->installEventFilter(this);

    // Initially wallet actions should be disabled
    setWalletActionsEnabled(false);

    // Subscribe to notifications from core
    subscribeToCoreSignals();

    connect(connectionsControl, SIGNAL(clicked(QPoint)), this, SLOT(toggleNetworkActive()));

    modalOverlay = new ModalOverlay(this->centralWidget());
    modalBackupOverlay = new ModalOverlay(this, ModalOverlay::Backup);
//    silubiumVersionChecker = new SilubiumVersionChecker(this);

//    if(fCheckForUpdates && silubiumVersionChecker->newVersionAvailable())
//    {
//        QString link = QString("<a href=%1>%2</a>").arg(SILUBIUM_RELEASES, SILUBIUM_RELEASES);
//        QString message(tr("New version of Silubium wallet is available on the Silubium source code repository: <br /> %1. <br />It is recommended to download it and update this application").arg(link));
//        QMessageBox::information(this, tr("Check for updates"), message);
//    }

#ifdef ENABLE_WALLET
    if(enableWallet) {
        connect(walletFrame, SIGNAL(requestedSyncWarningInfo()), this, SLOT(showModalOverlay()));
        connect(labelBlocksIcon, SIGNAL(clicked(QPoint)), this, SLOT(showModalOverlay()));
        connect(progressBar, SIGNAL(clicked(QPoint)), this, SLOT(showModalOverlay()));
        connect(modalBackupOverlay, SIGNAL(backupWallet()), walletFrame, SLOT(backupWallet()));
    }
#endif

    setStyleSheet("QMainWindow::separator { width: 1px; height: 1px; margin: 0px; padding: 0px; }");
}

BitcoinGUI::~BitcoinGUI()
{
    // Unsubscribe from notifications from core
    unsubscribeFromCoreSignals();

    QSettings settings;
    settings.setValue("MainWindowGeometry", saveGeometry());
    if(trayIcon) // Hide tray icon, as deleting will let it linger until quit (on Ubuntu)
        trayIcon->hide();
#ifdef Q_OS_MAC
    delete appMenuBar;
    MacDockIconHandler::cleanup();
#endif

    delete rpcConsole;
}

void BitcoinGUI::createActions()
{
    QActionGroup *tabGroup = new QActionGroup(this);
    QString strLanguage=GetLangTerritory();

    if(strLanguage.contains("zh_CN"))
    {
        overviewAction = new QAction(platformStyle->MultiStatesIcon(":/icons/gaschange"), tr("我的钱包(&w)"), this);//overview
        overviewAction->setStatusTip(tr("显示钱包的概况"));
        sendCoinsAction = new QAction(platformStyle->MultiStatesIcon(":/icons/send_to"), tr("发送(&S)"), this);
        sendCoinsAction->setStatusTip(tr("发送SLU到Silubium地址"));
    }
    else
    {
        overviewAction = new QAction(platformStyle->MultiStatesIcon(":/icons/gaschange"), tr("My &wallet"), this);//overview
        overviewAction->setStatusTip(tr("Show general overview of wallet"));
        sendCoinsAction = new QAction(platformStyle->MultiStatesIcon(":/icons/send_to"), tr("&Send"), this);
        sendCoinsAction->setStatusTip(tr("Send coins to a Silubium address"));
    }
    overviewAction->setToolTip(overviewAction->statusTip());
    overviewAction->setCheckable(true);
    overviewAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_1));
    tabGroup->addAction(overviewAction);

    sendCoinsAction->setToolTip(sendCoinsAction->statusTip());
    sendCoinsAction->setCheckable(true);
    sendCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_2));
    tabGroup->addAction(sendCoinsAction);

    sendCoinsMenuAction = new QAction(platformStyle->MenuColorIcon(":/icons/send"), sendCoinsAction->text(), this);
    sendCoinsMenuAction->setStatusTip(sendCoinsAction->statusTip());
    sendCoinsMenuAction->setToolTip(sendCoinsMenuAction->statusTip());

    if(strLanguage.contains("zh_CN"))
    {
        receiveCoinsAction = new QAction(platformStyle->MultiStatesIcon(":/icons/receive_from"), tr("接收(&R)"), this);
        receiveCoinsAction->setStatusTip(tr("为交易生成一个Silubium地址和二维码)"));
    }
    else
    {
        receiveCoinsAction = new QAction(platformStyle->MultiStatesIcon(":/icons/receive_from"), tr("&Receive"), this);
        receiveCoinsAction->setStatusTip(tr("Request payments (generates QR codes and silubium: URIs)"));
    }
    receiveCoinsAction->setToolTip(receiveCoinsAction->statusTip());
    receiveCoinsAction->setCheckable(true);
    receiveCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_3));
    tabGroup->addAction(receiveCoinsAction);

    receiveCoinsMenuAction = new QAction(platformStyle->MenuColorIcon(":/icons/receiving_addresses"), receiveCoinsAction->text(), this);
    receiveCoinsMenuAction->setStatusTip(receiveCoinsAction->statusTip());
    receiveCoinsMenuAction->setToolTip(receiveCoinsMenuAction->statusTip());

    if(strLanguage.contains("zh_CN"))
    {
        smartContractAction = new QAction(platformStyle->MultiStatesIcon(":/icons/smart_contract"), tr("智能合约(&C)"), this);
        smartContractAction->setStatusTip(tr("智能合约"));
    }
    else
    {
        smartContractAction = new QAction(platformStyle->MultiStatesIcon(":/icons/smart_contract"), tr("Smart &Contracts"), this);
        smartContractAction->setStatusTip(tr("Smart contracts"));
    }
    smartContractAction->setToolTip(smartContractAction->statusTip());
    smartContractAction->setCheckable(true);
    smartContractAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_4));
    if(gArgs.IsArgSet("-showdbg"))
        tabGroup->addAction(smartContractAction);

    if(strLanguage.contains("zh_CN"))
    {
        createContractAction = new QAction(tr("创建"), this);
        sendToContractAction = new QAction(tr("发送到"), this);
        callContractAction = new QAction(tr("调用"), this);
    }
    else
    {
        createContractAction = new QAction(tr("Create"), this);
        sendToContractAction = new QAction(tr("Send To"), this);
        callContractAction = new QAction(tr("Call"), this);
    }




    if(strLanguage.contains("zh_CN"))
    {
        historyAction = new QAction(platformStyle->MultiStatesIcon(":/icons/history"), tr("交易记录(&T)"), this);
        historyAction->setStatusTip(tr("浏览交易历史"));
    }
    else
    {
        historyAction = new QAction(platformStyle->MultiStatesIcon(":/icons/history"), tr("&Transactions"), this);
        historyAction->setStatusTip(tr("Browse transaction history"));
    }
    historyAction->setToolTip(historyAction->statusTip());
    historyAction->setCheckable(true);
    historyAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_5));
    tabGroup->addAction(historyAction);

    if(strLanguage.contains("zh_CN"))
    {
        QRCTokenAction = new QAction(platformStyle->MultiStatesIcon(":/icons/qrctoken"), tr("&SRC代币"), this);
        QRCTokenAction->setStatusTip(tr("SRC代币操作 (发送, 接收或者添加代币列表)"));
    }
    else
    {
        QRCTokenAction = new QAction(platformStyle->MultiStatesIcon(":/icons/qrctoken"), tr("&SRC Tokens"), this);
        QRCTokenAction->setStatusTip(tr("SRC Tokens (send, receive or add Tokens in list)"));
    }
    QRCTokenAction->setToolTip(QRCTokenAction->statusTip());
    QRCTokenAction->setCheckable(true);
    QRCTokenAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_6));
    tabGroup->addAction(QRCTokenAction);

    if(strLanguage.contains("zh_CN"))
    {
        sendTokenAction = new QAction(tr("发送"), this);
        receiveTokenAction = new QAction(tr("收到"), this);
        addTokenAction = new QAction(tr("添加代币"), this);
    }
    else
    {
        sendTokenAction = new QAction(tr("Send"), this);
        receiveTokenAction = new QAction(tr("Receive"), this);
        addTokenAction = new QAction(tr("Add Token"), this);
    }

#ifdef ENABLE_WALLET
    // These showNormalIfMinimized are needed because Send Coins and Receive Coins
    // can be triggered from the tray menu, and need to show the GUI to be useful.
    connect(overviewAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(overviewAction, SIGNAL(triggered()), this, SLOT(gotoOverviewPage()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(gotoSendCoinsPage()));
    connect(sendCoinsMenuAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(sendCoinsMenuAction, SIGNAL(triggered()), this, SLOT(gotoSendCoinsPage()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(gotoReceiveCoinsPage()));
    connect(receiveCoinsMenuAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(receiveCoinsMenuAction, SIGNAL(triggered()), this, SLOT(gotoReceiveCoinsPage()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(gotoHistoryPage()));
    connect(createContractAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(createContractAction, SIGNAL(triggered()), this, SLOT(gotoCreateContractPage()));
    connect(sendToContractAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(sendToContractAction, SIGNAL(triggered()), this, SLOT(gotoSendToContractPage()));
    connect(callContractAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(callContractAction, SIGNAL(triggered()), this, SLOT(gotoCallContractPage()));
    connect(sendTokenAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(sendTokenAction, SIGNAL(triggered()), this, SLOT(gotoSendTokenPage()));
    connect(receiveTokenAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(receiveTokenAction, SIGNAL(triggered()), this, SLOT(gotoReceiveTokenPage()));
    connect(addTokenAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(addTokenAction, SIGNAL(triggered()), this, SLOT(gotoAddTokenPage()));
#endif // ENABLE_WALLET

    if(strLanguage.contains("zh_CN"))
    {
        quitAction = new QAction(platformStyle->MenuColorIcon(":/icons/quit"), tr("退出(&x)"), this);
        quitAction->setStatusTip(tr("退出程序"));
    }
    else
    {
        quitAction = new QAction(platformStyle->MenuColorIcon(":/icons/quit"), tr("E&xit"), this);
        quitAction->setStatusTip(tr("Quit application"));
    }
    quitAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q));
    quitAction->setMenuRole(QAction::QuitRole);

    if(strLanguage.contains("zh_CN"))
    {
        aboutAction = new QAction(platformStyle->MenuColorIcon(":/icons/about"), tr("关于%1(&A)").arg(tr(PACKAGE_NAME)), this);
        aboutAction->setStatusTip(tr("显示关于%1的信息").arg(tr(PACKAGE_NAME)));
    }
    else
    {
        aboutAction = new QAction(platformStyle->MenuColorIcon(":/icons/about"), tr("&About %1").arg(tr(PACKAGE_NAME)), this);
        aboutAction->setStatusTip(tr("Show information about %1").arg(tr(PACKAGE_NAME)));
    }
    aboutAction->setMenuRole(QAction::AboutRole);
    aboutAction->setEnabled(false);


    if(strLanguage.contains("zh_CN"))
    {
        aboutQtAction = new QAction(platformStyle->MenuColorIcon(":/icons/about_qt"), tr("关于&Qt"), this);
        aboutQtAction->setStatusTip(tr("显示关于Qt的信息"));
    }
    else
    {
        aboutQtAction = new QAction(platformStyle->MenuColorIcon(":/icons/about_qt"), tr("About &Qt"), this);
        aboutQtAction->setStatusTip(tr("Show information about Qt"));
    }
    aboutQtAction->setMenuRole(QAction::AboutQtRole);

    if(strLanguage.contains("zh_CN"))
    {
        optionsAction = new QAction(platformStyle->MenuColorIcon(":/icons/options"), tr("选项(&O)..."), this);
        optionsAction->setStatusTip(tr("修改%1配置选项").arg(tr(PACKAGE_NAME)));
    }
    else
    {
        optionsAction = new QAction(platformStyle->MenuColorIcon(":/icons/options"), tr("&Options..."), this);
        optionsAction->setStatusTip(tr("Modify configuration options for %1").arg(tr(PACKAGE_NAME)));
    }
    optionsAction->setMenuRole(QAction::PreferencesRole);
    optionsAction->setEnabled(false);


    if(strLanguage.contains("zh_CN"))
    {
        toggleHideAction = new QAction(platformStyle->MenuColorIcon(":/icons/about"), tr("显示(&S) / 隐藏"), this);
        toggleHideAction->setStatusTip(tr("显示或隐藏主窗口"));

        encryptWalletAction = new QAction(platformStyle->MenuColorIcon(":/icons/encrypt"), tr("加密钱包(&E)..."), this);
        encryptWalletAction->setStatusTip(tr("加密属于你的钱包的私钥"));
    }
    else
    {
        toggleHideAction = new QAction(platformStyle->MenuColorIcon(":/icons/about"), tr("&Show / Hide"), this);
        toggleHideAction->setStatusTip(tr("Show or hide the main Window"));

        encryptWalletAction = new QAction(platformStyle->MenuColorIcon(":/icons/encrypt"), tr("&Encrypt Wallet..."), this);
        encryptWalletAction->setStatusTip(tr("Encrypt the private keys that belong to your wallet"));
    }
    encryptWalletAction->setCheckable(true);

    if(strLanguage.contains("zh_CN"))
    {
        backupWalletAction = new QAction(platformStyle->MenuColorIcon(":/icons/filesave"), tr("备份钱包(&B)..."), this);
        backupWalletAction->setStatusTip(tr("备份钱包数据到另一个地方"));
        restoreWalletAction = new QAction(platformStyle->MenuColorIcon(":/icons/restore"), tr("恢复钱包(&R)..."), this);
        restoreWalletAction->setStatusTip(tr("从另一个地方恢复钱包"));
        changePassphraseAction = new QAction(platformStyle->MenuColorIcon(":/icons/key"), tr("改变口令(&C)..."), this);
        changePassphraseAction->setStatusTip(tr("改变用作钱包加密的口令"));
        unlockWalletAction = new QAction(platformStyle->MenuColorIcon(":/icons/lock_open"), tr("解锁钱包(&U)..."), this);
        unlockWalletAction->setToolTip(tr("解锁钱包"));
    }
    else
    {
        backupWalletAction = new QAction(platformStyle->MenuColorIcon(":/icons/filesave"), tr("&Backup Wallet..."), this);
        backupWalletAction->setStatusTip(tr("Backup wallet to another location"));
        restoreWalletAction = new QAction(platformStyle->MenuColorIcon(":/icons/restore"), tr("&Restore Wallet..."), this);
        restoreWalletAction->setStatusTip(tr("Restore wallet from another location"));
        changePassphraseAction = new QAction(platformStyle->MenuColorIcon(":/icons/key"), tr("&Change Passphrase..."), this);
        changePassphraseAction->setStatusTip(tr("Change the passphrase used for wallet encryption"));
        unlockWalletAction = new QAction(platformStyle->MenuColorIcon(":/icons/lock_open"), tr("&Unlock Wallet..."), this);
        unlockWalletAction->setToolTip(tr("Unlock wallet"));
    }
    unlockWalletAction->setObjectName("unlockWalletAction");
    if(strLanguage.contains("zh_CN"))
    {
        lockWalletAction = new QAction(platformStyle->MenuColorIcon(":/icons/lock_closed"), tr("加锁钱包(&L)"), this);
        lockWalletAction->setToolTip(tr("解锁钱包"));
        signMessageAction = new QAction(platformStyle->MenuColorIcon(":/icons/edit"), tr("签名信息(&m)..."), this);
        signMessageAction->setStatusTip(tr("用SILUBIUM地址关联的私钥为消息签名，以证明您拥有这个SILUBIUM地址"));
        verifyMessageAction = new QAction(platformStyle->MenuColorIcon(":/icons/verify"), tr("校验信息(&V)..."), this);
        verifyMessageAction->setStatusTip(tr("校验消息，确保该消息是由指定的SILUBIUM地址所有者签名的"));
        openRPCConsoleAction = new QAction(platformStyle->MenuColorIcon(":/icons/debugwindow"), tr("调试窗口(&D)"), this);
        openRPCConsoleAction->setStatusTip(tr("打开调试和分析控制台窗口"));
    }
    else
    {
        lockWalletAction = new QAction(platformStyle->MenuColorIcon(":/icons/lock_closed"), tr("&Lock Wallet"), this);
        lockWalletAction->setToolTip(tr("Lock wallet"));
        signMessageAction = new QAction(platformStyle->MenuColorIcon(":/icons/edit"), tr("Sign &message..."), this);
        signMessageAction->setStatusTip(tr("Sign messages with your Silubium addresses to prove you own them"));
        verifyMessageAction = new QAction(platformStyle->MenuColorIcon(":/icons/verify"), tr("&Verify message..."), this);
        verifyMessageAction->setStatusTip(tr("Verify messages to ensure they were signed with specified Silubium addresses"));

        openRPCConsoleAction = new QAction(platformStyle->MenuColorIcon(":/icons/debugwindow"), tr("&Debug window"), this);
        openRPCConsoleAction->setStatusTip(tr("Open debugging and diagnostic console"));
    }
    // initially disable the debug window menu item
    openRPCConsoleAction->setEnabled(false);

    if(strLanguage.contains("zh_CN"))
    {
        usedSendingAddressesAction = new QAction(platformStyle->MenuColorIcon(":/icons/address-book"), tr("发送地址表(&S)..."), this);
        usedSendingAddressesAction->setStatusTip(tr("显示用于发送的地址和标签列表"));
        usedReceivingAddressesAction = new QAction(platformStyle->MenuColorIcon(":/icons/address-book"), tr("接收地址表(&R)..."), this);
        usedReceivingAddressesAction->setStatusTip(tr("显示用于接收的地址和标签列表"));
        openAction = new QAction(platformStyle->MenuColorIcon(":/icons/open"), tr("打开&URI..."), this);
        openAction->setStatusTip(tr("打开一个silubium的URI或者付款请求"));
        showHelpMessageAction = new QAction(platformStyle->MenuColorIcon(":/icons/info"), tr("显示命令行选项(&C)"), this);
        showHelpMessageAction->setStatusTip(tr("显示%1的帮助消息以帮助你选择一个合适的Silubium命令行选项").arg(tr(PACKAGE_NAME)));
        merketAction =new QAction(platformStyle->MenuColorIcon(":/icons/interest"), tr("交易所"), this);
        merketAction->setStatusTip(tr("欢迎访问Silubium交易所!"));
        updaterAction = new QAction(platformStyle->MenuColorIcon(":/icons/updater"),tr("在线升级"), this);
        updaterAction->setStatusTip(tr("检测是否存在新版本，Windows版本可以在线升级。"));
    }
    else
    {
        usedSendingAddressesAction = new QAction(platformStyle->MenuColorIcon(":/icons/address-book"), tr("&Sending addresses..."), this);
        usedSendingAddressesAction->setStatusTip(tr("Show the list of used sending addresses and labels"));
        usedReceivingAddressesAction = new QAction(platformStyle->MenuColorIcon(":/icons/address-book"), tr("&Receiving addresses..."), this);
        usedReceivingAddressesAction->setStatusTip(tr("Show the list of used receiving addresses and labels"));
        openAction = new QAction(platformStyle->MenuColorIcon(":/icons/open"), tr("Open &URI..."), this);
        openAction->setStatusTip(tr("Open a silubium: URI or payment request"));
        showHelpMessageAction = new QAction(platformStyle->MenuColorIcon(":/icons/info"), tr("&Command-line options"), this);
        showHelpMessageAction->setStatusTip(tr("Show the %1 help message to get a list with possible Silubium command-line options").arg(tr(PACKAGE_NAME)));
        merketAction =new QAction(platformStyle->MenuColorIcon(":/icons/interest"), tr("Markets"), this);
        merketAction->setStatusTip(tr("Welcome to Silubium matkets!"));
        updaterAction = new QAction(platformStyle->MenuColorIcon(":/icons/updater"),tr("Online Update"), this);
        updaterAction->setStatusTip(tr("Check whether there is a new version, and the Windows version can be upgraded online."));
    }
    showHelpMessageAction->setMenuRole(QAction::NoRole);
    connect(quitAction, SIGNAL(triggered()), qApp, SLOT(quit()));
    connect(aboutAction, SIGNAL(triggered()), this, SLOT(aboutClicked()));
    connect(aboutQtAction, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
    connect(optionsAction, SIGNAL(triggered()), this, SLOT(optionsClicked()));
    connect(toggleHideAction, SIGNAL(triggered()), this, SLOT(toggleHidden()));
    connect(showHelpMessageAction, SIGNAL(triggered()), this, SLOT(showHelpMessageClicked()));
    connect(updaterAction,SIGNAL(triggered()),this,SLOT(onlineUpgrade()));
    connect(openRPCConsoleAction, SIGNAL(triggered()), this, SLOT(showDebugWindow()));
    // prevents an open debug window from becoming stuck/unusable on client shutdown
    connect(quitAction, SIGNAL(triggered()), rpcConsole, SLOT(hide()));
    connect(merketAction, SIGNAL(triggered()), this, SLOT(openMerkets()));
#ifdef ENABLE_WALLET
    if(walletFrame)
    {
        connect(encryptWalletAction, SIGNAL(triggered(bool)), walletFrame, SLOT(encryptWallet(bool)));
        connect(backupWalletAction, SIGNAL(triggered()), walletFrame, SLOT(backupWallet()));
        connect(restoreWalletAction, SIGNAL(triggered()), walletFrame, SLOT(restoreWallet()));
        connect(changePassphraseAction, SIGNAL(triggered()), walletFrame, SLOT(changePassphrase()));
        connect(unlockWalletAction, SIGNAL(triggered()), walletFrame, SLOT(unlockWallet()));
        connect(lockWalletAction, SIGNAL(triggered()), walletFrame, SLOT(lockWallet()));
        connect(signMessageAction, SIGNAL(triggered()), this, SLOT(gotoSignMessageTab()));
        connect(verifyMessageAction, SIGNAL(triggered()), this, SLOT(gotoVerifyMessageTab()));
        connect(usedSendingAddressesAction, SIGNAL(triggered()), walletFrame, SLOT(usedSendingAddresses()));
        connect(usedReceivingAddressesAction, SIGNAL(triggered()), walletFrame, SLOT(usedReceivingAddresses()));
        connect(openAction, SIGNAL(triggered()), this, SLOT(openClicked()));
    }
#endif // ENABLE_WALLET

    new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_C), this, SLOT(showDebugWindowActivateConsole()));
    new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_D), this, SLOT(showDebugWindow()));
}

void BitcoinGUI::createMenuBar()
{
#ifdef Q_OS_MAC
    // Create a decoupled menu bar on Mac which stays even if the window is closed
    appMenuBar = new QMenuBar();
#else
    // Get the main window's menu bar on other platforms
    appMenuBar = menuBar();
#endif

    // Configure the menus

    bool ischina=isChina();
    QMenu *file,*settings,*help;

    if(ischina)
        file = appMenuBar->addMenu(tr("文件(&F)"));
    else
        file = appMenuBar->addMenu(tr("&File"));
    if(walletFrame)
    {
        file->addAction(openAction);
        file->addAction(backupWalletAction);
        file->addAction(restoreWalletAction);
        if (gArgs.IsArgSet("-showdbg"))
        {
            file->addAction(signMessageAction);
            file->addAction(verifyMessageAction);
        }
        file->addSeparator();
        file->addAction(usedSendingAddressesAction);
        file->addAction(usedReceivingAddressesAction);
        file->addSeparator();
    }
    file->addAction(quitAction);

    if(ischina)
        settings = appMenuBar->addMenu(tr("设置(&S)"));
    else
        settings = appMenuBar->addMenu(tr("&Settings"));
    if(walletFrame)
    {
        settings->addAction(encryptWalletAction);
        settings->addAction(changePassphraseAction);
        settings->addAction(unlockWalletAction);
        settings->addAction(lockWalletAction);
        settings->addSeparator();
    }
    settings->addAction(optionsAction);

    if(ischina)//判断是否中文
        help = appMenuBar->addMenu(tr("帮助(&H)"));
    else
        help = appMenuBar->addMenu(tr("&Help"));
    if(walletFrame)
    {
        if (gArgs.IsArgSet("-showdbg"))
            help->addAction(openRPCConsoleAction);
    }
    help->addAction(showHelpMessageAction);
    help->addAction(merketAction);
    help->addAction(updaterAction);
    help->addSeparator();
    help->addAction(aboutAction);
    if (gArgs.IsArgSet("-showdbg"))
        help->addAction(aboutQtAction);
}

void BitcoinGUI::createToolBars()
{
    if(walletFrame)
    {
        // Create custom tool bar component
        appNavigationBar = new NavigationBar();
        addDockWindows(Qt::LeftDockWidgetArea, appNavigationBar);

        // Fill the component with actions
        appNavigationBar->addAction(overviewAction);
        appNavigationBar->addAction(sendCoinsAction);
        appNavigationBar->addAction(receiveCoinsAction);
        appNavigationBar->addAction(historyAction);
        QList<QAction*> contractActions;
        contractActions.append(createContractAction);
        contractActions.append(sendToContractAction);
        contractActions.append(callContractAction);
        if (gArgs.IsArgSet("-showdbg"))
            appNavigationBar->mapGroup(smartContractAction, contractActions);
        QList<QAction*> tokenActions;
        tokenActions.append(sendTokenAction);
        tokenActions.append(receiveTokenAction);
        tokenActions.append(addTokenAction);
        appNavigationBar->mapGroup(QRCTokenAction, tokenActions);
        appNavigationBar->buildUi();
        overviewAction->setChecked(true);
    }
}

void BitcoinGUI::createTitleBars()
{
    if(walletFrame)
    {
        // Create custom title bar component
        appTitleBar = new TitleBar(platformStyle);
        addDockWindows(Qt::TopDockWidgetArea, appTitleBar);
        connect(appNavigationBar, SIGNAL(resized(QSize)), appTitleBar, SLOT(on_navigationResized(QSize)));
    }
}

void BitcoinGUI::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;
    if(_clientModel)
    {
        // Create system tray menu (or setup the dock menu) that late to prevent users from calling actions,
        // while the client has not yet fully loaded
        createTrayIconMenu();

        // Keep up to date with client
        updateNetworkState();
        connect(_clientModel, SIGNAL(numConnectionsChanged(int)), this, SLOT(setNumConnections(int)));
        connect(_clientModel, SIGNAL(networkActiveChanged(bool)), this, SLOT(setNetworkActive(bool)));

        modalOverlay->setKnownBestHeight(_clientModel->getHeaderTipHeight(), QDateTime::fromTime_t(_clientModel->getHeaderTipTime()));
        setNumBlocks(_clientModel->getNumBlocks(), _clientModel->getLastBlockDate(), _clientModel->getVerificationProgress(nullptr), false);
        connect(_clientModel, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)), this, SLOT(setNumBlocks(int,QDateTime,double,bool)));

        // Receive and report messages from client model
        connect(_clientModel, SIGNAL(message(QString,QString,unsigned int)), this, SLOT(message(QString,QString,unsigned int)));

        // Show progress dialog
        connect(_clientModel, SIGNAL(showProgress(QString,int)), this, SLOT(showProgress(QString,int)));

        rpcConsole->setClientModel(_clientModel);
#ifdef ENABLE_WALLET
        if(walletFrame)
        {
            walletFrame->setClientModel(_clientModel);
        }
#endif // ENABLE_WALLET
        unitDisplayControl->setOptionsModel(_clientModel->getOptionsModel());
        
        OptionsModel* optionsModel = _clientModel->getOptionsModel();
        if(optionsModel)
        {
            // be aware of the tray icon disable state change reported by the OptionsModel object.
            connect(optionsModel,SIGNAL(hideTrayIconChanged(bool)),this,SLOT(setTrayIconVisible(bool)));

            // initialize the disable state of the tray icon with the current value in the model.
            setTrayIconVisible(optionsModel->getHideTrayIcon());
        }

        modalOverlay->setKnownBestHeight(clientModel->getHeaderTipHeight(), QDateTime::fromTime_t(clientModel->getHeaderTipTime()));
    } else {
        // Disable possibility to show main window via action
        toggleHideAction->setEnabled(false);
        if(trayIconMenu)
        {
            // Disable context menu on tray icon
            trayIconMenu->clear();
        }
        // Propagate cleared model to child objects
        rpcConsole->setClientModel(nullptr);
#ifdef ENABLE_WALLET
        if (walletFrame)
        {
            walletFrame->setClientModel(nullptr);
        }
#endif // ENABLE_WALLET
        unitDisplayControl->setOptionsModel(nullptr);
    }
}

#ifdef ENABLE_WALLET
bool BitcoinGUI::addWallet(const QString& name, WalletModel *walletModel)
{
    if(!walletFrame)
        return false;
    setWalletActionsEnabled(true);
    appTitleBar->setModel(walletModel);
    if(showBackupOverlay && walletModel && !(walletModel->hasWalletBackup()))
    {
        showModalBackupOverlay();
    }
    return walletFrame->addWallet(name, walletModel);
}

bool BitcoinGUI::setCurrentWallet(const QString& name)
{
    if(!walletFrame)
        return false;
    return walletFrame->setCurrentWallet(name);
}

void BitcoinGUI::removeAllWallets()
{
    if(!walletFrame)
        return;
    setWalletActionsEnabled(false);
    walletFrame->removeAllWallets();
}
#endif // ENABLE_WALLET

void BitcoinGUI::setWalletActionsEnabled(bool enabled)
{
    overviewAction->setEnabled(enabled);
    sendCoinsAction->setEnabled(enabled);
    sendCoinsMenuAction->setEnabled(enabled);
    receiveCoinsAction->setEnabled(enabled);
    receiveCoinsMenuAction->setEnabled(enabled);
    historyAction->setEnabled(enabled);
    encryptWalletAction->setEnabled(enabled);
    backupWalletAction->setEnabled(enabled);
    restoreWalletAction->setEnabled(enabled);
    changePassphraseAction->setEnabled(enabled);
    signMessageAction->setEnabled(enabled);
    verifyMessageAction->setEnabled(enabled);
    usedSendingAddressesAction->setEnabled(enabled);
    usedReceivingAddressesAction->setEnabled(enabled);
    openAction->setEnabled(enabled);
}

void BitcoinGUI::createTrayIcon(const NetworkStyle *networkStyle)
{
#ifndef Q_OS_MAC
    trayIcon = new QSystemTrayIcon(this);
    QString toolTip = tr("%1 client").arg(tr(PACKAGE_NAME)) + " " + networkStyle->getTitleAddText();
    trayIcon->setToolTip(toolTip);
    trayIcon->setIcon(networkStyle->getTrayAndWindowIcon());
    trayIcon->hide();
#endif

    notificator = new Notificator(QApplication::applicationName(), trayIcon, this);
}

void BitcoinGUI::createTrayIconMenu()
{
#ifndef Q_OS_MAC
    // return if trayIcon is unset (only on non-Mac OSes)
    if (!trayIcon)
        return;

    trayIconMenu = new QMenu(this);
    trayIcon->setContextMenu(trayIconMenu);

    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
            this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
#else
    // Note: On Mac, the dock icon is used to provide the tray's functionality.
    MacDockIconHandler *dockIconHandler = MacDockIconHandler::instance();
    dockIconHandler->setMainWindow((QMainWindow *)this);
    trayIconMenu = dockIconHandler->dockMenu();
#endif

    // Configuration of the tray icon (or dock icon) icon menu
    trayIconMenu->addAction(toggleHideAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(sendCoinsMenuAction);
    trayIconMenu->addAction(receiveCoinsMenuAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(signMessageAction);
    trayIconMenu->addAction(verifyMessageAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(optionsAction);
    trayIconMenu->addAction(openRPCConsoleAction);
#ifndef Q_OS_MAC // This is built-in on Mac
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);
#endif
}

#ifndef Q_OS_MAC
void BitcoinGUI::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if(reason == QSystemTrayIcon::Trigger)
    {
        // Click on system tray icon triggers show/hide of the main window
        toggleHidden();
    }
}
#endif

void BitcoinGUI::optionsClicked()
{
    if(!clientModel || !clientModel->getOptionsModel())
        return;

    OptionsDialog dlg(this, enableWallet);
    dlg.setModel(clientModel->getOptionsModel());
    dlg.exec();
}

void BitcoinGUI::aboutClicked()
{
    if(!clientModel)
        return;

    HelpMessageDialog dlg(this, true);
    dlg.exec();
}

void BitcoinGUI::showDebugWindow()
{
    rpcConsole->showNormal();
    rpcConsole->show();
    rpcConsole->raise();
    rpcConsole->activateWindow();
}

void BitcoinGUI::showDebugWindowActivateConsole()
{
    rpcConsole->setTabFocus(RPCConsole::TAB_CONSOLE);
    showDebugWindow();
}

void BitcoinGUI::showHelpMessageClicked()
{
    helpMessageDialog->show();
}

void BitcoinGUI::openMerkets()
{
    QDesktopServices::openUrl(QUrl(QString("https://www.silktrader.net")));
}

void BitcoinGUI::onlineUpgrade()
{
    silubiumVersionChecker=new SilubiumVersionChecker(this);
    if(silubiumVersionChecker->newVersionAvailable())
    {
        if(silubiumVersionChecker->isWin())
        {
            QUpload updater;
            if(updater.exec()==QDialog::Accepted)
            {
                quitAction->activate(QAction::Trigger);
            }
        }
        else
        {
            if(isChina())
            {
                QString link = QString("<a href=%1>%2</a>").arg(SILUBIUM_RELEASES, SILUBIUM_RELEASES);
                QString message(tr("源代码仓库有新版本的Silubium钱包可用！<br/>%1<br/>强烈建议下载并升级这个应用。").arg(link));
                QMessageBox::information(this, tr("检测升级"), message);
            }
            else
            {
                QString link = QString("<a href=%1>%2</a>").arg(SILUBIUM_RELEASES, SILUBIUM_RELEASES);
                QString message(tr("New version of Silubium wallet is available on the Silubium source code repository: <br /> %1. <br />It is recommended to download it and update this application").arg(link));
                QMessageBox::information(this, tr("Check for Updates"), message);
            }
        }

    }
    else
    {
        if(!isChina())
            QMessageBox::information(this,tr("Check for updates"),tr("This is the latest version!"));
        else
            QMessageBox::information(this,tr("检测升级"),tr("当前已经是最新版本！"));
    }
}

#ifdef ENABLE_WALLET
void BitcoinGUI::openClicked()
{
    OpenURIDialog dlg(this);
    if(dlg.exec())
    {
        Q_EMIT receivedURI(dlg.getURI());
    }
}

void BitcoinGUI::gotoOverviewPage()
{
    overviewAction->setChecked(true);
    if (walletFrame) walletFrame->gotoOverviewPage();
}

void BitcoinGUI::gotoHistoryPage()
{
    historyAction->setChecked(true);
    if (walletFrame) walletFrame->gotoHistoryPage();
}

void BitcoinGUI::gotoSendTokenPage()
{
    sendTokenAction->setChecked(true);
    QRCTokenAction->setChecked(true);
    if (walletFrame) walletFrame->gotoSendTokenPage();
}

void BitcoinGUI::gotoReceiveTokenPage()
{
    receiveTokenAction->setChecked(true);
    QRCTokenAction->setChecked(true);
    if (walletFrame) walletFrame->gotoReceiveTokenPage();
}

void BitcoinGUI::gotoAddTokenPage()
{
    addTokenAction->setChecked(true);
    QRCTokenAction->setChecked(true);
    if (walletFrame) walletFrame->gotoAddTokenPage();
}

void BitcoinGUI::gotoReceiveCoinsPage()
{
    receiveCoinsAction->setChecked(true);
    if (walletFrame) walletFrame->gotoReceiveCoinsPage();
}

void BitcoinGUI::gotoSendCoinsPage(QString addr)
{
    sendCoinsAction->setChecked(true);
    if (walletFrame) walletFrame->gotoSendCoinsPage(addr);
}

void BitcoinGUI::gotoCreateContractPage()
{
    if (walletFrame) walletFrame->gotoCreateContractPage();
}

void BitcoinGUI::gotoSendToContractPage()
{
    if (walletFrame) walletFrame->gotoSendToContractPage();
}

void BitcoinGUI::gotoCallContractPage()
{
    if (walletFrame) walletFrame->gotoCallContractPage();
}

void BitcoinGUI::gotoSignMessageTab(QString addr)
{
    if (walletFrame) walletFrame->gotoSignMessageTab(addr);
}

void BitcoinGUI::gotoVerifyMessageTab(QString addr)
{
    if (walletFrame) walletFrame->gotoVerifyMessageTab(addr);
}
#endif // ENABLE_WALLET

void BitcoinGUI::updateNetworkState()
{
    int count = clientModel->getNumConnections();
    QString icon;
    switch(count)
    {
    case 0: icon = ":/icons/connect_0"; break;
    case 1: case 2: case 3: icon = ":/icons/connect_1"; break;
    case 4: case 5: case 6: icon = ":/icons/connect_2"; break;
    case 7: case 8: case 9: icon = ":/icons/connect_3"; break;
    default: icon = ":/icons/connect_4"; break;
    }

    QString tooltip;
    if(isChina())
    {
        if (clientModel->getNetworkActive()) {

            tooltip = tr("%n个活跃的连接到Silubium网络", "", count) + QString(".<br>") + tr("单击这里可以关闭网络连接。");
        } else {
            tooltip = tr("网络连接关闭。") + QString("<br>") + tr("单击这里再次使能网络连接。");
            icon = ":/icons/network_disabled";
        }
    }
    else
    {
        if (clientModel->getNetworkActive()) {

            tooltip = tr("%n active connection(s) to Silubium network", "", count) + QString(".<br>") + tr("Click to disable network activity.");
        } else {
            tooltip = tr("Network activity disabled.") + QString("<br>") + tr("Click to enable network activity again.");
            icon = ":/icons/network_disabled";
        }
    }

    // Don't word-wrap this (fixed-width) tooltip
    tooltip = QString("<nobr>") + tooltip + QString("</nobr>");
    connectionsControl->setToolTip(tooltip);

    connectionsControl->setPixmap(QIcon(icon).pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
}

void BitcoinGUI::setNumConnections(int count)
{
    updateNetworkState();
}

void BitcoinGUI::setNetworkActive(bool networkActive)
{
    updateNetworkState();
}

void BitcoinGUI::updateHeadersSyncProgressLabel()
{
    int64_t headersTipTime = clientModel->getHeaderTipTime();
    int headersTipHeight = clientModel->getHeaderTipHeight();
    int estHeadersLeft = (GetTime() - headersTipTime) / Params().GetConsensus().nPowTargetSpacing;
    if (estHeadersLeft > HEADER_HEIGHT_DELTA_SYNC)
        progressBarLabel->setText(tr("Syncing Headers (%1%)...").arg(QString::number(100.0 / (headersTipHeight+estHeadersLeft)*headersTipHeight, 'f', 1)));
}

bool BitcoinGUI::isChina()
{
    bool bFindChina=false;
    if(GetLangTerritory().contains("zh_CN"))
        bFindChina=true;
    return bFindChina;
}

void BitcoinGUI::setNumBlocks(int count, const QDateTime& blockDate, double nVerificationProgress, bool header)
{
    bool china=isChina();

    if (modalOverlay)
    {
        if (header)
            modalOverlay->setKnownBestHeight(count, blockDate);
        else
            modalOverlay->tipUpdate(count, blockDate, nVerificationProgress);
    }
    if (!clientModel)
        return;

    // Prevent orphan statusbar messages (e.g. hover Quit in main menu, wait until chain-sync starts -> garbled text)
    statusBar()->clearMessage();

    // Acquire current block source
    enum BlockSource blockSource = clientModel->getBlockSource();
    switch (blockSource) {
    case BLOCK_SOURCE_NETWORK:
        if (header) {
            updateHeadersSyncProgressLabel();
            return;
        }
        if(china)
            progressBarLabel->setText(tr("从网络同步..."));
        else
            progressBarLabel->setText(tr("Synchronizing with network..."));
        updateHeadersSyncProgressLabel();
        break;
    case BLOCK_SOURCE_DISK:
        if(!china)
        {
            if (header) {
                progressBarLabel->setText(tr("Indexing blocks on disk..."));
            } else {
                progressBarLabel->setText(tr("Processing blocks on disk..."));
            }
        }
        else
        {
            if (header) {
                progressBarLabel->setText(tr("在磁盘上建立块索引..."));
            } else {
                progressBarLabel->setText(tr("在磁盘上处理块..."));
            }
        }
        break;
    case BLOCK_SOURCE_REINDEX:
        if(china)
            progressBarLabel->setText(tr("在磁盘上重建块索引..."));
        else
            progressBarLabel->setText(tr("Reindexing blocks on disk..."));
        break;
    case BLOCK_SOURCE_NONE:
        if (header) {
            return;
        }
        if(china)
            progressBarLabel->setText(tr("连接到节点..."));
        else
            progressBarLabel->setText(tr("Connecting to peers..."));
        break;
    }

    QString tooltip;

    QDateTime currentDate = QDateTime::currentDateTime();
    qint64 secs = blockDate.secsTo(currentDate);

    if(!china)
        tooltip = tr("Processed %n block(s) of transaction history.", "", count);
    else
        tooltip = tr("已处理%n块的交易历史。", "", count);
    // Set icon state: spinning if catching up, tick otherwise
    if(secs < 90*60)
    {
        if(china)
            tooltip = tr("到目前为止") + QString(".<br>") + tooltip;
        else
            tooltip = tr("Up to date") + QString(".<br>") + tooltip;
        labelBlocksIcon->setPixmap(QIcon(":/icons/synced").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));

#ifdef ENABLE_WALLET
        if(walletFrame)
        {
            walletFrame->showOutOfSyncWarning(false);
            modalOverlay->showHide(true, true);
        }
#endif // ENABLE_WALLET

        progressBarLabel->setVisible(false);
        progressBar->setVisible(false);

        // notify tip changed when the sync is finished
        if(fBatchProcessingMode)
        {
            fBatchProcessingMode = false;
            QMetaObject::invokeMethod(clientModel, "tipChanged", Qt::QueuedConnection);
        }
    }
    else
    {
        QString timeBehindText = GUIUtil::formatNiceTimeOffset(secs);

        progressBarLabel->setVisible(true);
        if(china)
            progressBar->setFormat(tr("落后%1").arg(timeBehindText));
        else
            progressBar->setFormat(tr("%1 behind").arg(timeBehindText));
        progressBar->setMaximum(1000000000);
        progressBar->setValue(nVerificationProgress * 1000000000.0 + 0.5);
        progressBar->setVisible(true);

        if(china)
            tooltip = tr("追加数据...") + QString("<br>") + tooltip;
        else
            tooltip = tr("Catching up...") + QString("<br>") + tooltip;
        if(count != prevBlocks)
        {
            labelBlocksIcon->setPixmap(QIcon(QString(
                                                 ":/movies/spinner-%1").arg(spinnerFrame, 3, 10, QChar('0')))
                                       .pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
            spinnerFrame = (spinnerFrame + 1) % SPINNER_FRAMES;
        }
        prevBlocks = count;

#ifdef ENABLE_WALLET
        if(walletFrame)
        {
            walletFrame->showOutOfSyncWarning(true);
            modalOverlay->showHide();
        }
#endif // ENABLE_WALLET
        if(china)
        {
            tooltip += QString("<br>");
            tooltip += tr("最后收到的块在%1前被生成。").arg(timeBehindText);
            tooltip += QString("<br>");
            tooltip += tr("此后的交易将不可见.");
        }
        else
        {
            tooltip += QString("<br>");
            tooltip += tr("Last received block was generated %1 ago.").arg(timeBehindText);
            tooltip += QString("<br>");
            tooltip += tr("Transactions after this will not yet be visible.");
        }
    }

    // Don't word-wrap this (fixed-width) tooltip
    tooltip = QString("<nobr>") + tooltip + QString("</nobr>");

    labelBlocksIcon->setToolTip(tooltip);
    progressBarLabel->setToolTip(tooltip);
    progressBar->setToolTip(tooltip);
}

void BitcoinGUI::message(const QString &title, const QString &message, unsigned int style, bool *ret)
{
    QString strTitle = tr("Silubium"); // default title
    // Default to information icon
    int nMBoxIcon = QMessageBox::Information;
    int nNotifyIcon = Notificator::Information;

    QString msgType;

    // Prefer supplied title over style based title
    if (!title.isEmpty()) {
        msgType = title;
    }
    else {
        switch (style) {
        case CClientUIInterface::MSG_ERROR:
            msgType = tr("Error");
            break;
        case CClientUIInterface::MSG_WARNING:
            msgType = tr("Warning");
            break;
        case CClientUIInterface::MSG_INFORMATION:
            msgType = tr("Information");
            break;
        default:
            break;
        }
    }
    // Append title to "Bitcoin - "
    if (!msgType.isEmpty())
        strTitle += " - " + msgType;

    // Check for error/warning icon
    if (style & CClientUIInterface::ICON_ERROR) {
        nMBoxIcon = QMessageBox::Critical;
        nNotifyIcon = Notificator::Critical;
    }
    else if (style & CClientUIInterface::ICON_WARNING) {
        nMBoxIcon = QMessageBox::Warning;
        nNotifyIcon = Notificator::Warning;
    }

    // Display message
    if (style & CClientUIInterface::MODAL) {
        // Check for buttons, use OK as default, if none was supplied
        QMessageBox::StandardButton buttons;
        if (!(buttons = (QMessageBox::StandardButton)(style & CClientUIInterface::BTN_MASK)))
            buttons = QMessageBox::Ok;

        showNormalIfMinimized();
        QMessageBox mBox((QMessageBox::Icon)nMBoxIcon, strTitle, message, buttons, this);
        if(isChina())
            mBox.setButtonText (QMessageBox::Ok,QString("确 定(&O)"));
        int r = mBox.exec();
        if (ret != nullptr)
            *ret = r == QMessageBox::Ok;
    }
    else
        notificator->notify((Notificator::Class)nNotifyIcon, strTitle, message);
}

void BitcoinGUI::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
#ifndef Q_OS_MAC // Ignored on Mac
    if(e->type() == QEvent::WindowStateChange)
    {
        if(clientModel && clientModel->getOptionsModel() && clientModel->getOptionsModel()->getMinimizeToTray())
        {
            QWindowStateChangeEvent *wsevt = static_cast<QWindowStateChangeEvent*>(e);
            if(!(wsevt->oldState() & Qt::WindowMinimized) && isMinimized())
            {
                QTimer::singleShot(0, this, SLOT(hide()));
                e->ignore();
            }
        }
    }
#endif
}

void BitcoinGUI::closeEvent(QCloseEvent *event)
{
#ifndef Q_OS_MAC // Ignored on Mac
    if(clientModel && clientModel->getOptionsModel())
    {
        if(!clientModel->getOptionsModel()->getMinimizeOnClose())
        {
            // close rpcConsole in case it was open to make some space for the shutdown window
            rpcConsole->close();

            QApplication::quit();
        }
        else
        {
            QMainWindow::showMinimized();
            event->ignore();
        }
    }
#else
    QMainWindow::closeEvent(event);
#endif
}

void BitcoinGUI::showEvent(QShowEvent *event)
{
    // enable the debug window when the main window shows up
    openRPCConsoleAction->setEnabled(true);
    aboutAction->setEnabled(true);
    optionsAction->setEnabled(true);
}

#ifdef ENABLE_WALLET
void BitcoinGUI::incomingTransaction(const QString& date, int unit, const CAmount& amount, const QString& type, const QString& address, const QString& label)
{
    // On new transaction, make an info balloon
    if(isChina())
    {
        QString msg = tr("日期: %1\n").arg(date) +
                tr("金额: %1\n").arg(BitcoinUnits::formatWithUnit(unit, amount, true)) +
                tr("类型: %1\n").arg(type);
        if (!label.isEmpty())
            msg += tr("标签: %1\n").arg(label);
        else if (!address.isEmpty())
            msg += tr("地址: %1\n").arg(address);
        message((amount)<0 ? tr("发送交易") : tr("流入交易"),
                msg, CClientUIInterface::MSG_INFORMATION);
    }
    else
    {
        QString msg = tr("Date: %1\n").arg(date) +
                tr("Amount: %1\n").arg(BitcoinUnits::formatWithUnit(unit, amount, true)) +
                tr("Type: %1\n").arg(type);
        if (!label.isEmpty())
            msg += tr("Label: %1\n").arg(label);
        else if (!address.isEmpty())
            msg += tr("Address: %1\n").arg(address);
        message((amount)<0 ? tr("Sent transaction") : tr("Incoming transaction"),
                msg, CClientUIInterface::MSG_INFORMATION);
    }

}

void BitcoinGUI::incomingTokenTransaction(const QString& date, const QString& amount, const QString& type, const QString& address, const QString& label, const QString& title)
{
    // On new transaction, make an info balloon
    if(isChina())
    {
        QString msg = tr("日期: %1\n").arg(date) +
                tr("金额: %1\n").arg(amount) +
                tr("类型: %1\n").arg(type);
        if (!label.isEmpty())
            msg += tr("标签: %1\n").arg(label);
        else if (!address.isEmpty())
            msg += tr("地址: %1\n").arg(address);
        message(title, msg, CClientUIInterface::MSG_INFORMATION);
    }
    else
    {
        QString msg = tr("Date: %1\n").arg(date) +
                tr("Amount: %1\n").arg(amount) +
                tr("Type: %1\n").arg(type);
        if (!label.isEmpty())
            msg += tr("Label: %1\n").arg(label);
        else if (!address.isEmpty())
            msg += tr("Address: %1\n").arg(address);
        message(title, msg, CClientUIInterface::MSG_INFORMATION);
    }

}
#endif // ENABLE_WALLET

void BitcoinGUI::dragEnterEvent(QDragEnterEvent *event)
{
    // Accept only URIs
    if(event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void BitcoinGUI::dropEvent(QDropEvent *event)
{
    if(event->mimeData()->hasUrls())
    {
        for (const QUrl &uri : event->mimeData()->urls())
        {
            Q_EMIT receivedURI(uri.toString());
        }
    }
    event->acceptProposedAction();
}

bool BitcoinGUI::eventFilter(QObject *object, QEvent *event)
{
    // Catch status tip events
    if (event->type() == QEvent::StatusTip)
    {
        // Prevent adding text from setStatusTip(), if we currently use the status bar for displaying other stuff
        if (progressBarLabel->isVisible() || progressBar->isVisible())
            return true;
    }
    return QMainWindow::eventFilter(object, event);
}

#ifdef ENABLE_WALLET
bool BitcoinGUI::handlePaymentRequest(const SendCoinsRecipient& recipient)
{
    // URI has to be valid
    if (walletFrame && walletFrame->handlePaymentRequest(recipient))
    {
        showNormalIfMinimized();
        gotoSendCoinsPage();
        return true;
    }
    return false;
}

void BitcoinGUI::setHDStatus(int hdEnabled)
{
    labelWalletHDStatusIcon->setPixmap(QIcon(hdEnabled ? ":/icons/hd_enabled" : ":/icons/hd_disabled").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
    if(isChina())
        labelWalletHDStatusIcon->setToolTip(hdEnabled ? tr("高清密钥生成是<b>可能的</b>") : tr("高清密钥生成是<b>被禁止的</b>"));
    else
        labelWalletHDStatusIcon->setToolTip(hdEnabled ? tr("HD key generation is <b>enabled</b>") : tr("HD key generation is <b>disabled</b>"));

    // eventually disable the QLabel to set its opacity to 50%
    labelWalletHDStatusIcon->setEnabled(hdEnabled);
}

void BitcoinGUI::setEncryptionStatus(int status)
{
    switch(status)
    {
    case WalletModel::Unencrypted:
        labelWalletEncryptionIcon->hide();
        encryptWalletAction->setChecked(false);
        changePassphraseAction->setEnabled(false);
        unlockWalletAction->setVisible(false);
        lockWalletAction->setVisible(false);
        encryptWalletAction->setEnabled(true);
        break;
    case WalletModel::Unlocked:
        labelWalletEncryptionIcon->show();
        if(fWalletUnlockStakingOnly)
        {
            labelWalletEncryptionIcon->setPixmap(QIcon(":/icons/lock_staking").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
            labelWalletEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>unlocked for staking only</b>"));
        }
        else
        {
            labelWalletEncryptionIcon->setPixmap(QIcon(":/icons/lock_open").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
            labelWalletEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>unlocked</b>"));
        }
        encryptWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        unlockWalletAction->setVisible(false);
        lockWalletAction->setVisible(true);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        break;
    case WalletModel::Locked:
        labelWalletEncryptionIcon->show();
        labelWalletEncryptionIcon->setPixmap(QIcon(":/icons/lock_closed").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelWalletEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>locked</b>"));
        encryptWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        unlockWalletAction->setVisible(true);
        lockWalletAction->setVisible(false);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        break;
    }
}
#endif // ENABLE_WALLET

void BitcoinGUI::showNormalIfMinimized(bool fToggleHidden)
{
    if(!clientModel)
        return;

    // activateWindow() (sometimes) helps with keyboard focus on Windows
    if (isHidden())
    {
        show();
        activateWindow();
    }
    else if (isMinimized())
    {
        showNormal();
        activateWindow();
    }
    else if (GUIUtil::isObscured(this))
    {
        raise();
        activateWindow();
    }
    else if(fToggleHidden)
        hide();
}

void BitcoinGUI::toggleHidden()
{
    showNormalIfMinimized(true);
}

void BitcoinGUI::updateWeight(CWalletRef pwalletMain)
{
    if(!pwalletMain)
        return;

    TRY_LOCK(cs_main, lockMain);
    if (!lockMain)
        return;

    TRY_LOCK(pwalletMain->cs_wallet, lockWallet);
    if (!lockWallet)
        return;

#ifdef ENABLE_WALLET
    if (pwalletMain)
        nWeight = pwalletMain->GetStakeWeight();
#endif
}

void BitcoinGUI::updateStakingIcon()
{
    if(ShutdownRequested())
        return;

    CWalletRef pwalletMain = vpwallets.empty() ? 0 : vpwallets[0];
    if(!pwalletMain)
        return;

    updateWeight(pwalletMain);

    if (nLastCoinStakeSearchInterval && nWeight)
    {
        uint64_t nWeight = this->nWeight;
        uint64_t nNetworkWeight = GetPoSKernelPS();
        const Consensus::Params& consensusParams = Params().GetConsensus();
        int64_t nTargetSpacing = consensusParams.nPowTargetSpacing;

        unsigned nEstimateTime = nTargetSpacing * nNetworkWeight / nWeight;

        QString text;
        if (nEstimateTime < 60)
        {
            text = tr("%n second(s)", "", nEstimateTime);
        }
        else if (nEstimateTime < 60*60)
        {
            text = tr("%n minute(s)", "", nEstimateTime/60);
        }
        else if (nEstimateTime < 24*60*60)
        {
            text = tr("%n hour(s)", "", nEstimateTime/(60*60));
        }
        else
        {
            text = tr("%n day(s)", "", nEstimateTime/(60*60*24));
        }

        nWeight /= COIN;
        nNetworkWeight /= COIN;

        labelStakingIcon->setPixmap(QIcon(":/icons/staking_on").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
        if(GetLangTerritory().contains("zh_CN"))
            labelStakingIcon->setToolTip(tr("挖矿中.<br>你的权重是：%1<br>全网权重是：%2<br>预计收益时间是：%3").arg(nWeight).arg(nNetworkWeight).arg(text));
        else
            labelStakingIcon->setToolTip(tr("Staking.<br>Your weight is %1<br>Network weight is %2<br>Expected time to earn reward is %3").arg(nWeight).arg(nNetworkWeight).arg(text));
    }
    else
    {
        labelStakingIcon->setPixmap(QIcon(":/icons/staking_off").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
        if(GetLangTerritory().contains("zh_CN"))
        {
            if (g_connman == 0 || g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0)
                labelStakingIcon->setToolTip(tr("由于离线，没有挖矿。"));
            else if (IsInitialBlockDownload())
                labelStakingIcon->setToolTip(tr("钱包同步中，没有挖矿"));
            else if (!nWeight)
                labelStakingIcon->setToolTip(tr("没有成熟的COIN，没有挖矿"));
            else if (pwalletMain && pwalletMain->IsLocked())
                labelStakingIcon->setToolTip(tr("钱包被锁，没有挖矿"));
            else
                labelStakingIcon->setToolTip(tr("没有挖矿"));
        }
        else
        {
            if (g_connman == 0 || g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0)
                labelStakingIcon->setToolTip(tr("Not staking because wallet is offline"));
            else if (IsInitialBlockDownload())
                labelStakingIcon->setToolTip(tr("Not staking because wallet is syncing"));
            else if (!nWeight)
                labelStakingIcon->setToolTip(tr("Not staking because you don't have mature coins"));
            else if (pwalletMain && pwalletMain->IsLocked())
                labelStakingIcon->setToolTip(tr("Not staking because wallet is locked"));
            else
                labelStakingIcon->setToolTip(tr("Not staking"));
        }


    }
}

void BitcoinGUI::detectShutdown()
{
    if (ShutdownRequested())
    {
        if(rpcConsole)
            rpcConsole->hide();
        qApp->quit();
    }
}

void BitcoinGUI::showProgress(const QString &title, int nProgress)
{
    if (nProgress == 0)
    {
        progressDialog = new QProgressDialog(title, "", 0, 100);
        progressDialog->setWindowModality(Qt::ApplicationModal);
        progressDialog->setMinimumDuration(0);
        progressDialog->setCancelButton(0);
        progressDialog->setAutoClose(false);
        progressDialog->setValue(0);
    }
    else if (nProgress == 100)
    {
        if (progressDialog)
        {
            progressDialog->close();
            progressDialog->deleteLater();
        }
    }
    else if (progressDialog)
        progressDialog->setValue(nProgress);
}

void BitcoinGUI::setTrayIconVisible(bool fHideTrayIcon)
{
    if (trayIcon)
    {
        trayIcon->setVisible(!fHideTrayIcon);
    }
}

void BitcoinGUI::showModalOverlay()
{
    if (modalOverlay && (progressBar->isVisible() || modalOverlay->isLayerVisible()))
        modalOverlay->toggleVisibility();
}

void BitcoinGUI::showModalBackupOverlay()
{
    if (modalBackupOverlay)
        modalBackupOverlay->toggleVisibility();
}

void BitcoinGUI::setTabBarInfo(QObject *into)
{
    if(appTitleBar)
    {
        appTitleBar->setTabBarInfo(into);
    }
}

static bool ThreadSafeMessageBox(BitcoinGUI *gui, const std::string& message, const std::string& caption, unsigned int style)
{
    bool modal = (style & CClientUIInterface::MODAL);
    // The SECURE flag has no effect in the Qt GUI.
    // bool secure = (style & CClientUIInterface::SECURE);
    style &= ~CClientUIInterface::SECURE;
    bool ret = false;
    // In case of modal message, use blocking connection to wait for user to click a button
    QMetaObject::invokeMethod(gui, "message",
                              modal ? GUIUtil::blockingGUIThreadConnection() : Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(caption)),
                              Q_ARG(QString, QString::fromStdString(message)),
                              Q_ARG(unsigned int, style),
                              Q_ARG(bool*, &ret));
    return ret;
}

void BitcoinGUI::subscribeToCoreSignals()
{
    // Connect signals to client
    uiInterface.ThreadSafeMessageBox.connect(boost::bind(ThreadSafeMessageBox, this, _1, _2, _3));
    uiInterface.ThreadSafeQuestion.connect(boost::bind(ThreadSafeMessageBox, this, _1, _3, _4));
}

void BitcoinGUI::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    uiInterface.ThreadSafeMessageBox.disconnect(boost::bind(ThreadSafeMessageBox, this, _1, _2, _3));
    uiInterface.ThreadSafeQuestion.disconnect(boost::bind(ThreadSafeMessageBox, this, _1, _3, _4));
}

void BitcoinGUI::toggleNetworkActive()
{
    if (clientModel) {
        clientModel->setNetworkActive(!clientModel->getNetworkActive());
    }
}

void BitcoinGUI::addDockWindows(Qt::DockWidgetArea area, QWidget* widget)
{
    QDockWidget *dock = new QDockWidget(this);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    dock->setAllowedAreas(area);
    QWidget* titleBar = new QWidget();
    titleBar->setMaximumSize(0, 0);
    dock->setTitleBarWidget(titleBar);
    dock->setWidget(widget);
    addDockWidget(area, dock);
}
extern ArgsManager gArgs;
QString BitcoinGUI::GetLangTerritory()
{
    QSettings settings;
    // Get desired locale (e.g. "de_DE")
    // 1) System default language
    QString lang_territory = QLocale::system().name();
    // 2) Language from QSettings
    QString lang_territory_qsettings = settings.value("language", "").toString();
    if(!lang_territory_qsettings.isEmpty())
        lang_territory = lang_territory_qsettings;
    // 3) -lang command line argument
    //    lang_territory = QString::fromStdString(gArgs.GetArg("-lang", lang_territory.toStdString()));
    return lang_territory;
}

UnitDisplayStatusBarControl::UnitDisplayStatusBarControl(const PlatformStyle *platformStyle) :
    optionsModel(0),
    menu(0)
{
    createContextMenu();
    setToolTip(tr("Unit to show amounts in. Click to select another unit."));
    QList<BitcoinUnits::Unit> units = BitcoinUnits::availableUnits();
    int max_width = 0;
    const QFontMetrics fm(font());
    for (const BitcoinUnits::Unit unit : units)
    {
        max_width = qMax(max_width, fm.width(BitcoinUnits::name(unit)));
    }
    setMinimumSize(max_width, 0);
    setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}

/** So that it responds to button clicks */
void UnitDisplayStatusBarControl::mousePressEvent(QMouseEvent *event)
{
    onDisplayUnitsClicked(event->pos());
}

/** Creates context menu, its actions, and wires up all the relevant signals for mouse events. */
void UnitDisplayStatusBarControl::createContextMenu()
{
    menu = new QMenu(this);
    for (BitcoinUnits::Unit u : BitcoinUnits::availableUnits())
    {
        QAction *menuAction = new QAction(QString(BitcoinUnits::name(u)), this);
        menuAction->setData(QVariant(u));
        menu->addAction(menuAction);
    }
    connect(menu,SIGNAL(triggered(QAction*)),this,SLOT(onMenuSelection(QAction*)));
}

/** Lets the control know about the Options Model (and its signals) */
void UnitDisplayStatusBarControl::setOptionsModel(OptionsModel *_optionsModel)
{
    if (_optionsModel)
    {
        this->optionsModel = _optionsModel;

        // be aware of a display unit change reported by the OptionsModel object.
        connect(_optionsModel,SIGNAL(displayUnitChanged(int)),this,SLOT(updateDisplayUnit(int)));

        // initialize the display units label with the current value in the model.
        updateDisplayUnit(_optionsModel->getDisplayUnit());
    }
}

/** When Display Units are changed on OptionsModel it will refresh the display text of the control on the status bar */
void UnitDisplayStatusBarControl::updateDisplayUnit(int newUnits)
{
    setText(BitcoinUnits::name(newUnits));
}

/** Shows context menu with Display Unit options by the mouse coordinates */
void UnitDisplayStatusBarControl::onDisplayUnitsClicked(const QPoint& point)
{
    QPoint globalPos = mapToGlobal(point);
    menu->exec(globalPos);
}

/** Tells underlying optionsModel to update its current display unit. */
void UnitDisplayStatusBarControl::onMenuSelection(QAction* action)
{
    if (action)
    {
        optionsModel->setDisplayUnit(action->data());
    }
}


