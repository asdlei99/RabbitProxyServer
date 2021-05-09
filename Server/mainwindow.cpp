//! @author Kang Lin(kl222@126.com)

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "ProxyServerSocks.h"
#include "FrmSocket.h"
#include "RabbitCommonDir.h"

#include <QFile>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    bool check = false;
    
    ui->setupUi(this);
    
    auto server = std::make_shared<CProxyServerSocks>(
                new CProxyServerSocks(this));
    m_Server.push_back(server);
    
    on_actionLoad_triggered();
    
    m_pTabWidget = new QTabWidget(this);
    setCentralWidget(m_pTabWidget);
    CFrmSocket* pFrmSocket = new CFrmSocket(server->Getparameter(), m_pTabWidget);
    check = connect(this, SIGNAL(sigSaveParameter()),
                    pFrmSocket, SLOT(slotAccept()));
    Q_ASSERT(check);
    m_pTabWidget->addTab(pFrmSocket, "Socket proxy server");
    
    on_actionStart_triggered();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_actionStart_triggered()
{
    on_actionStop_triggered();
    foreach (auto s, m_Server) {
        s->Start();
    }
}

void MainWindow::on_actionStop_triggered()
{
    foreach (auto s, m_Server) {
        s->Stop();
    }
}

void MainWindow::on_actionSave_triggered()
{
    emit sigSaveParameter();
    
    QSettings set(RabbitCommon::CDir::Instance()->GetFileUserConfigure(),
                  QSettings::IniFormat);
    foreach (auto s, m_Server) {
        s->Save(set);
    }
}

void MainWindow::on_actionLoad_triggered()
{
    QSettings set(RabbitCommon::CDir::Instance()->GetFileUserConfigure(),
                  QSettings::IniFormat);
    foreach (auto s, m_Server) {
        s->Load(set);
    }
}
