#include "IceManager.h"
#include "RabbitCommonLog.h"
#include <QThread>

CIceManager::CIceManager(QSharedPointer<CIceSignal> signal, QObject *parent)
    : QObject(parent)
{
    SetSignal(signal);
}

std::shared_ptr<rtc::PeerConnection> CIceManager::GetPeerConnect(
        QSharedPointer<CIceSignal> signal,
        rtc::Configuration conf,
        CDataChannelIceChannel *channel)
{
    Q_ASSERT(channel);

    std::shared_ptr<rtc::PeerConnection> pc;
    auto it = m_PeerConnections.find(channel->GetPeerUser());
    if(it != m_PeerConnections.end())
    {
        pc = it.value().pc;
    } else {

        pc = std::make_shared<rtc::PeerConnection>(conf);
        if(!pc)
        {
            LOG_MODEL_ERROR("CIceManger", "Peer connect don't open");
            return nullptr;
        }

        LOG_MODEL_DEBUG("CIceManger",
                        "New rtc::PeerConnection: user:%s;peer:%s;id:%s",
                        channel->GetUser().toStdString().c_str(),
                        channel->GetPeerUser().toStdString().c_str(),
                        channel->GetChannelId().toStdString().c_str()
                        );
        QMutexLocker lock(&m_mutexPeerConnection);
        m_PeerConnections[channel->GetPeerUser()] = {pc, 0};

        pc->onStateChange([](rtc::PeerConnection::State state) {
            LOG_MODEL_DEBUG("CIceManger", "PeerConnection State: %d", state);
        });
        pc->onGatheringStateChange(
                    [](rtc::PeerConnection::GatheringState state) {
            Q_UNUSED(state)
            //LOG_MODEL_DEBUG("CIceManger", "Gathering status: %d", state);
        });
        pc->onLocalDescription(
                    [=](rtc::Description description) {
            LOG_MODEL_DEBUG("CDataChannelIce",
                            "onLocalDescription: The thread id: 0x%X",
                            QThread::currentThreadId());
            /*
            LOG_MODEL_DEBUG("CIceManger", "onLocalDescription: user:%s; peer:%s; channel:%s; sdp: %s",
                            channel->GetUser().toStdString().c_str(),
                            channel->GetPeerUser().toStdString().c_str(),
                            channel->GetChannelId().toStdString().c_str(),
                            std::string(description).c_str());//*/
            // Send to the peer through the signal channel
            if(channel->GetPeerUser().isEmpty())
                LOG_MODEL_ERROR("DataChannel", "Please set peer user and channel id");
            signal->SendDescription(channel->GetPeerUser(),
                                    channel->GetChannelId(),
                                    description,
                                    channel->GetUser());
        });
        pc->onLocalCandidate(
                    [=](rtc::Candidate candidate){
            LOG_MODEL_DEBUG("CDataChannelIce",
                            "onLocalCandidate: The thread id: 0x%X",
                            QThread::currentThreadId());
            /*
            LOG_MODEL_DEBUG("CIceManger", "onLocalCandidate: user:%s; peer:%s; channel:%s; candidate: %s, mid: %s",
                            channel->GetUser().toStdString().c_str(),
                            channel->GetPeerUser().toStdString().c_str(),
                            channel->GetChannelId().toStdString().c_str(),
                            std::string(candidate).c_str(),
                            candidate.mid().c_str());//*/
            // Send to the peer through the signal channel
            if(channel->GetPeerUser().isEmpty())
                LOG_MODEL_ERROR("DataChannel", "Please set peer user and channel id");
            signal->SendCandiate(channel->GetPeerUser(),
                                 channel->GetChannelId(),
                                 candidate);
        });
        pc->onDataChannel([=](std::shared_ptr<rtc::DataChannel> dc) {
            LOG_MODEL_DEBUG("CIceManger", "onDataChannel: From %s revice data channel",
                            channel->GetPeerUser().toStdString().c_str());
            if(m_Channel.find(channel->GetChannelId()) == m_Channel.end())
                m_Channel[channel->GetChannelId()] = channel;
            m_Channel[channel->GetChannelId()]->SetDataChannel(dc);
        });
    }

    return pc;
}

int CIceManager::AddDataChannel(CDataChannelIceChannel *dc)
{
    Q_ASSERT(dc);
    if(!dc) return -1;
    auto it = m_Channel.find(dc->GetChannelId());
    if(m_Channel.end() != it)
    {
        LOG_MODEL_ERROR("DataChannel", "The data channel is exist: %s",
                        dc->GetChannelId().toStdString().c_str());
        return -1;
    }
    m_Channel[dc->GetChannelId()] = dc;

    QMutexLocker lock(&m_mutexPeerConnection);
    m_PeerConnections[dc->GetPeerUser()].nDataChannelCount++;

    return 0;
}

int CIceManager::CloseDataChannel(CDataChannelIceChannel *dc)
{
    m_Channel.remove(dc->GetChannelId());
    QMutexLocker lock(&m_mutexPeerConnection);
    m_PeerConnections[dc->GetPeerUser()].nDataChannelCount--;
    if(0 == m_PeerConnections[dc->GetPeerUser()].nDataChannelCount)
    {
        auto pc = m_PeerConnections[dc->GetPeerUser()].pc;
        pc->close();
        m_PeerConnections.remove(dc->GetPeerUser());
    }
    return 0;
}

int CIceManager::SetSignal(QSharedPointer<CIceSignal> signal)
{
    bool check = false;

    if(signal)
    {
        check = connect(signal.get(),
                        SIGNAL(sigDescription(const QString&,
                                              const QString&,
                                              const QString&,
                                              const QString&,
                                              const QString&)),
                        this,
                        SLOT(slotSignalReceiverDescription(const QString&,
                                                           const QString&,
                                                           const QString&,
                                                           const QString&,
                                                           const QString&)));
        Q_ASSERT(check);
        check = connect(signal.get(),
                        SIGNAL(sigCandiate(const QString&,
                                           const QString&,
                                           const QString&,
                                           const QString&,
                                           const QString&)),
                        this,
                        SLOT(slotSignalReceiverCandiate(const QString&,
                                                        const QString&,
                                                        const QString&,
                                                        const QString&,
                                                        const QString&)));
        Q_ASSERT(check);
    }
    return 0;
}

void CIceManager::slotSignalReceiverCandiate(const QString& fromUser,
                                            const QString &toUser,
                                            const QString &channelId,
                                            const QString& mid,
                                            const QString& sdp)
{
    /*
    LOG_MODEL_DEBUG("CDataChannelIce",
                    "slotSignalReceiverCandiate fromUser:%s; toUser:%s; channelId:%s; mid:%s; sdp:%s",
                    fromUser.toStdString().c_str(),
                    toUser.toStdString().c_str(),
                    channelId.toStdString().c_str(),
                    mid.toStdString().c_str(),
                    sdp.toStdString().c_str()); //*/
    auto it = m_PeerConnections.find(fromUser);
    if(m_PeerConnections.end() == it)
        return;
    auto pc = m_PeerConnections[fromUser].pc;
    if(pc)
    {
        rtc::Candidate candiate(sdp.toStdString(), mid.toStdString());
        pc->addRemoteCandidate(candiate);
    }
}

void CIceManager::slotSignalReceiverDescription(const QString& fromUser,
                                               const QString &toUser,
                                               const QString &channelId,
                                               const QString &type,
                                               const QString &sdp)
{
    //*
    LOG_MODEL_DEBUG("CIceManager",
                    "slotSignalReceiverDescription fromUser:%s; toUser:%s; channelId:%s; type:%s; sdp:%s",
                    fromUser.toStdString().c_str(),
                    toUser.toStdString().c_str(),
                    channelId.toStdString().c_str(),
                    type.toStdString().c_str(),
                    sdp.toStdString().c_str()); //*/

    auto it = m_PeerConnections.find(fromUser);
    if(m_PeerConnections.end() == it)
        return;
    auto pc = m_PeerConnections[fromUser].pc;
    if(pc)
    {
        rtc::Description des(sdp.toStdString(), type.toStdString());
        pc->setRemoteDescription(des);
    }
}
