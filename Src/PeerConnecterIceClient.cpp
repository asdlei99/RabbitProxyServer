//! @author Kang Lin(kl222@126.com)

#include "PeerConnecterIceClient.h"
#include "ParameterSocks.h"
#include "IceSignalWebSocket.h"
#include "RabbitCommonLog.h"
#include <QJsonDocument>
#include <QtEndian>
#include "ProxyServerSocks.h"
#include <QThread>
#include "DataChannelIceChannel.h"

CPeerConnecterIceClient::CPeerConnecterIceClient(CProxyServerSocks *pServer, QObject *parent)
    : CPeerConnecter(parent),
      m_pServer(pServer),
      m_nPeerPort(0),
      m_nBindPort(0),
      m_Status(CONNECT)
{
}

CPeerConnecterIceClient::~CPeerConnecterIceClient()
{
    qDebug() << "CPeerConnecterIceClient::~CPeerConnecterIceClient()";
}

int CPeerConnecterIceClient::CreateDataChannel(const QString &peer,
                                               const QString &user,
                                               const QString &channelId,
                                               bool bData)
{
    CParameterSocks* pPara = qobject_cast<CParameterSocks*>(m_pServer->Getparameter());
    if(!pPara) return -1;
    
    #if USE_ONE_PEERCONNECTION_ONE_DATACHANNEL
    m_DataChannel = QSharedPointer<CDataChannelIce>(
                new CDataChannelIce(m_pServer->GetSignal(), this),
                &QObject::deleteLater);
    #else
    m_DataChannel = QSharedPointer<CDataChannelIceChannel>(
                new CDataChannelIceChannel(m_pServer->GetSignal(),
                                           m_pServer->GetIceManager(),
                                           this),
                &QObject::deleteLater);
    #endif

    if(!m_DataChannel) return -1;

    bool check = false;
    check = connect(m_DataChannel.get(), SIGNAL(sigConnected()),
                    this, SLOT(slotDataChannelConnected()));
    Q_ASSERT(check);
    check = connect(m_DataChannel.get(), SIGNAL(sigDisconnected()),
                    this, SLOT(slotDataChannelDisconnected()));
    Q_ASSERT(check);
    check = connect(m_DataChannel.get(), SIGNAL(sigError(int, const QString&)),
                    this, SLOT(slotDataChannelError(int, const QString&)));
    Q_ASSERT(check);
    check = connect(m_DataChannel.get(), SIGNAL(readyRead()),
                    this, SLOT(slotDataChannelReadyRead()));
    Q_ASSERT(check);
    
    rtc::Configuration config;
    if(!pPara->GetStunServer().isEmpty() && pPara->GetStunPort())
        config.iceServers.push_back(
                    rtc::IceServer(pPara->GetStunServer().toStdString().c_str(),
                                   pPara->GetStunPort()));
    if(!pPara->GetTurnServer().isEmpty() && pPara->GetTurnPort())
        config.iceServers.push_back(
                    rtc::IceServer(pPara->GetTurnServer().toStdString().c_str(),
                                   pPara->GetTurnPort(),
                                   pPara->GetTurnUser().toStdString().c_str(),
                                   pPara->GetTurnPassword().toStdString().c_str()));
    //m_DataChannel->SetConfigure(config);

    if(m_DataChannel->open(config, user, peer, channelId, bData))
    {
        m_szError = tr("Data channel open fail");
        LOG_MODEL_ERROR("PeerConnecterIce", m_szError.toStdString().c_str());
        emit sigError(emERROR::NetWorkUnreachable, m_szError);
    }

    return 0;
}

void CPeerConnecterIceClient::slotDataChannelConnected()
{
    int nLen = sizeof (strClientRequst) + m_peerAddress.toStdString().size();
    QSharedPointer<char> buf(new char[nLen]);
    memset(buf.data(), 0, nLen);
    strClientRequst* requst = reinterpret_cast<strClientRequst*>(buf.data());
    requst->version = 0;
    requst->command = 1; //Connect
    requst->port = qToBigEndian(m_nPeerPort);
    requst->len = m_peerAddress.toStdString().size();
    memcpy(requst->host, m_peerAddress.toStdString().c_str(), m_peerAddress.toStdString().size());
    if(m_DataChannel)
    {
        LOG_MODEL_INFO("CPeerConnecterIceClient",
                        "Data channel connected: peer:%s;channel:%s;ip:%s;port:%d",
                        m_DataChannel->GetPeerUser().toStdString().c_str(),
                        m_DataChannel->GetChannelId().toStdString().c_str(),
                        m_peerAddress.toStdString().c_str(),
                        m_nPeerPort);
        m_DataChannel->write(buf.data(), nLen);
    }
}

void CPeerConnecterIceClient::slotDataChannelDisconnected()
{
    LOG_MODEL_INFO("CPeerConnecterIceClient",
                    "Data channel disconnected: peer:%s;channel:%s;ip:%s;port:%d",
                    m_DataChannel->GetPeerUser().toStdString().c_str(),
                    m_DataChannel->GetChannelId().toStdString().c_str(),
                    m_peerAddress.toStdString().c_str(),
                    m_nPeerPort);
    emit sigDisconnected();
}

void CPeerConnecterIceClient::slotDataChannelError(int nErr, const QString& szErr)
{
    LOG_MODEL_ERROR("CPeerConnecterIceClient",
                    "Data channel error: %d %s; peer:%s;channel:%s;ip:%s;port:%d",
                    nErr, szErr.toStdString().c_str(),
                    m_DataChannel->GetPeerUser().toStdString().c_str(),
                    m_DataChannel->GetChannelId().toStdString().c_str(),
                    m_peerAddress.toStdString().c_str(),
                    m_nPeerPort);
    emit sigError(nErr, szErr);
}

void CPeerConnecterIceClient::slotDataChannelReadyRead()
{
    //LOG_MODEL_DEBUG("CPeerConnecterIceClient", "slotDataChannelReadyRead");
    if(!m_DataChannel) return;

    if(CONNECT == m_Status)
    {
        OnConnectionReply();
        return;
    }

    emit sigReadyRead();
}

int CPeerConnecterIceClient::OnConnectionReply()
{
    int nRet = 0;

    if(!m_DataChannel)
    {
        emit sigError(-1, "Data channel is null");
        return -1;
    }

    m_Buffer.append(m_DataChannel->readAll());
    if(CheckBufferLength(sizeof(strClientRequst))) return ERROR_CONTINUE_READ;
    
    strReply* pReply = reinterpret_cast<strReply*>(m_Buffer.data());
    if(emERROR::Success == pReply->rep)
    {
        if(CheckBufferLength(sizeof(strClientRequst) + pReply->len)) return ERROR_CONTINUE_READ;
        m_nBindPort = qFromBigEndian(pReply->port);
        std::string add(pReply->host, pReply->len);
        m_bindAddress = add.c_str();
        LOG_MODEL_DEBUG("CPeerConnecterIceClient",
                        "CPeerConnecterIceClient::OnConnectionReply(): ip:%s;port:%d",
                        m_bindAddress.toStdString().c_str(), m_nBindPort);
        m_Status = FORWORD;
        m_Buffer.clear();
        emit sigConnected();
    }
    else
        emit sigError(pReply->rep, tr("Ice connect reply fail"));
    return nRet;
}

int CPeerConnecterIceClient::Connect(const QString &address, quint16 nPort)
{
    int nRet = 0;

    if(!m_pServer->GetSignal()->IsOpen())
    {
        m_szError = tr("Signal don't open");
        LOG_MODEL_ERROR("PeerConnecterIce", m_szError.toStdString().c_str());
        emit sigError(emERROR::Unkown, m_szError);
        return -1;
    }

    m_peerAddress = address;
    m_nPeerPort = nPort;

    CParameterSocks* pPara = qobject_cast<CParameterSocks*>(m_pServer->Getparameter());
    if(pPara->GetPeerUser().isEmpty())
    {
        m_szError = tr("Please set peer user");
        LOG_MODEL_ERROR("PeerConnecterIce", m_szError.toStdString().c_str());
        emit sigError(emERROR::NetWorkUnreachable, m_szError.toStdString().c_str());
        return -2;
    }
    nRet = CreateDataChannel(pPara->GetPeerUser(), pPara->GetSignalUser(),
                             pPara->GenerateChannelId(),
                             true);

    return nRet;
}

qint64 CPeerConnecterIceClient::Read(char *buf, qint64 nLen)
{
    if(!m_DataChannel || !m_DataChannel->isOpen()) return -1;

    return m_DataChannel->read(buf, nLen);
}

QByteArray CPeerConnecterIceClient::ReadAll()
{
    if(!m_DataChannel || !m_DataChannel->isOpen())
    {
        LOG_MODEL_ERROR("CPeerConnecterIceClient", "CPeerConnecterIceClient::ReadAll(): Data channel is not open");
        return QByteArray();
    }
    return m_DataChannel->readAll();
}

int CPeerConnecterIceClient::Write(const char *buf, qint64 nLen)
{
    if(!m_DataChannel || !m_DataChannel->isOpen())
    {
        LOG_MODEL_ERROR("CPeerConnecterIceClient", "CPeerConnecterIceClient::Write: Data channel is not open");
        return -1;
    }
    return m_DataChannel->write(buf, nLen);
}

int CPeerConnecterIceClient::Close()
{
    int nRet = 0;
    m_pServer->GetSignal()->disconnect(this);

    if(m_DataChannel)
    {
        m_DataChannel->disconnect();
        m_DataChannel->close();
        m_DataChannel.clear();
    }

    nRet = CPeerConnecter::Close();
    return nRet;
}

QHostAddress CPeerConnecterIceClient::LocalAddress()
{
    return QHostAddress(m_bindAddress);
}

quint16 CPeerConnecterIceClient::LocalPort()
{
    return m_nBindPort;
}

QString CPeerConnecterIceClient::ErrorString()
{
    return m_szError;
}

int CPeerConnecterIceClient::CheckBufferLength(int nLength)
{
    int nRet = nLength - m_Buffer.size();
    if(nRet > 0)
    {
        LOG_MODEL_DEBUG("CPeerConnecterIceClient",
            "CheckBufferLength %d < %d", m_Buffer.size(), nLength);
        return nRet;
    }
    return 0;
}
