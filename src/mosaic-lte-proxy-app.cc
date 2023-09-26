#include "mosaic-lte-proxy-app.h"
#include "ns3/log.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/simulator.h"
#include "ns3/flow-id-tag.h"

NS_LOG_COMPONENT_DEFINE("MosaicLteProxyApp");

namespace ns3
{

    NS_OBJECT_ENSURE_REGISTERED(MosaicLteProxyApp);

    TypeId
    MosaicLteProxyApp::GetTypeId(void)
    {
        static TypeId tid = TypeId("ns3::MosaicLteProxyApp")
                                .SetParent<Application>()
                                .AddConstructor<MosaicLteProxyApp>()
                                .AddAttribute("Port", "The socket port for messages",
                                              UintegerValue(8010),
                                              MakeUintegerAccessor(&MosaicLteProxyApp::m_port),
                                              MakeUintegerChecker<uint16_t>());
        return tid;
    }

    void
    MosaicLteProxyApp::SetLteV2xHelper(Ptr<LteV2xHelper> lteV2xHelper)
    {
        m_lteV2xHelper = lteV2xHelper;
    }

    void
    MosaicLteProxyApp::SetSockets(void)
    {
        NS_LOG_INFO("set sockets on node " << GetNode()->GetId());

        if (m_lteV2xHelper)
        {
            NetDeviceContainer txUe = NetDeviceContainer(GetNode()->GetDevice(0));
            Ipv4Address clientRespondersAddress = Ipv4Address("10.1.1.1"); // Example address, modify as needed
            uint32_t groupL2Address = 1;                                   // Example address, modify as needed
            uint16_t application_port = 12345;                             // Example port, modify as needed

            NetDeviceContainer rxUes = m_lteV2xHelper->RemoveNetDevice(txUe, txUe.Get(0));
            Ptr<LteSlTft> tft = Create<LteSlTft>(LteSlTft::TRANSMIT, clientRespondersAddress, groupL2Address);
            m_lteV2xHelper->ActivateSidelinkBearer(Seconds(0.0), txUe, tft);
            tft = Create<LteSlTft>(LteSlTft::RECEIVE, clientRespondersAddress, groupL2Address);
            m_lteV2xHelper->ActivateSidelinkBearer(Seconds(0.0), rxUes, tft);

            if (!m_host)
            {
                m_host = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
                m_host->Bind();
                m_host->Connect(InetSocketAddress(clientRespondersAddress, application_port));
                m_host->SetAllowBroadcast(true);
                m_host->ShutdownRecv();

                Ptr<LteUeMac> ueMac = DynamicCast<LteUeMac>(txUe.Get(0)->GetObject<LteUeNetDevice>()->GetMac());
                //   ueMac->TraceConnectWithoutContext ("SidelinkV2xAnnouncement", MakeBoundCallback (&MosaicLteProxyApp::SidelinkV2xAnnouncementMacTrace, this));
            }else{
                NS_FATAL_ERROR("creation attempt of a host for MosaicLteProxyApp that has already a host active");
            }

            if (!m_sink)
            {
                m_sink = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
                InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), application_port);
                m_sink->Bind(local);
                m_sink->SetRecvCallback(MakeCallback(&MosaicLteProxyApp::Receive, this));
            }
            else{
                NS_FATAL_ERROR("creation attempt of a sink for MosaicLteProxyApp that has already a sink active");
            }
        }
        else{
            NS_LOG_ERROR("LTE V2X helper must be setup firstly.");
        }
    }

    void
    MosaicLteProxyApp::TransmitPacket(uint32_t protocolID, uint32_t msgID, uint32_t payLength, Ipv4Address address)
    {
        NS_LOG_FUNCTION(protocolID << msgID << payLength << address);

        if (!m_active)
        {
            return;
        }

        Ptr<Packet> packet = Create<Packet>(payLength);
        FlowIdTag msgIDTag;
        msgIDTag.SetFlowId(msgID);
        packet->AddByteTag(msgIDTag);

        InetSocketAddress ipSA = InetSocketAddress(address, m_port);
        m_host->SendTo(packet, 0, ipSA);
    }

    void
    MosaicLteProxyApp::Receive(Ptr<Socket> socket)
    {
        NS_LOG_FUNCTION_NOARGS();
        if (!m_active)
        {
            return;
        }

        Ptr<Packet> packet;
        packet = socket->Recv();

        FlowIdTag Tag;
        int msgID;
        if (packet->FindFirstMatchingByteTag(Tag))
        {
            msgID = Tag.GetFlowId();
        }
        else
        {
            NS_LOG_ERROR("Error, message has no msgIdTag");
            msgID = -1;
        }

        m_nodeManager->AddRecvPacket(Simulator::Now().GetNanoSeconds(), packet, GetNode()->GetId(), msgID);
    }

    void
    MosaicLteProxyApp::DoDispose(void)
    {
        NS_LOG_FUNCTION_NOARGS();
        m_host = 0;
        m_sink = 0;
        Application::DoDispose();
    }

} // namespace ns3
