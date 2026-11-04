/*
 * Copyright (c) 2020 Fraunhofer FOKUS and others. All rights reserved.
 *
 * Contact: mosaic@fokus.fraunhofer.de
 *
 * This class is developed for the MOSAIC-NS-3 coupling.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "mosaic-proxy-app.h"

#include <cstdlib>
#include <sstream>

#include "ns3/csma-net-device.h"
#include "ns3/flow-id-tag.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/log.h"
#include "ns3/loopback-net-device.h"
#include "ns3/node.h"
#include "ns3/nr-gnb-net-device.h"
#include "ns3/nr-ue-net-device.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"

NS_LOG_COMPONENT_DEFINE("MosaicProxyApp");

namespace {

bool
IsTimingDebugEnabledProxyApp()
{
    static const bool enabled = [] {
        const char* env = std::getenv("MOSAIC_TIMING_DEBUG");
        return env != nullptr && std::string(env) == "1";
    }();
    return enabled;
}

void
TimingDebugProxyApp(const std::string& msg)
{
    if (IsTimingDebugEnabledProxyApp())
    {
        std::cout << "[TIMING][ProxyApp] " << msg << std::endl;
    }
}

} // namespace

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(MosaicProxyApp);

TypeId
MosaicProxyApp::GetTypeId(void)
{
    static TypeId tid =
        TypeId("ns3::MosaicProxyApp")
            .SetParent<Application>()
            .AddConstructor<MosaicProxyApp>()
            .AddAttribute("Port",
                          "UDP port used by the proxy app.",
                          UintegerValue(8010),
                          MakeUintegerAccessor(&MosaicProxyApp::m_port),
                          MakeUintegerChecker<uint16_t>());
    return tid;
}

MosaicProxyApp::MosaicProxyApp() = default;

MosaicProxyApp::~MosaicProxyApp() = default;

void
MosaicProxyApp::DoDispose()
{
    m_socket = nullptr;
    Application::DoDispose();
}

void
MosaicProxyApp::StartApplication()
{
    // Sockets are explicitly configured through SetSockets().
}

void
MosaicProxyApp::StopApplication()
{
    if (m_socket)
    {
        m_socket->Close();
        m_socket = nullptr;
    }
}

void
MosaicProxyApp::SetRecvCallback(Callback<void, unsigned long long, uint32_t, int> cb)
{
    m_recvCallback = cb;
}

void
MosaicProxyApp::Enable()
{
    m_enabled = true;
}

void
MosaicProxyApp::Disable()
{
    m_enabled = false;
}

uint32_t
MosaicProxyApp::ResolveOutgoingDeviceIndex(interface_e interfaceType) const
{
    Ptr<Node> node = GetNode();

    for (uint32_t i = 0; i < node->GetNDevices(); ++i)
    {
        Ptr<NetDevice> dev = node->GetDevice(i);

        if (DynamicCast<LoopbackNetDevice>(dev))
        {
            continue;
        }

        if (interfaceType == interface_e::ETH && DynamicCast<CsmaNetDevice>(dev))
        {
            return i;
        }

        if (interfaceType == interface_e::CELL &&
            (DynamicCast<NrUeNetDevice>(dev) || DynamicCast<NrGnbNetDevice>(dev)))
        {
            return i;
        }

        if (interfaceType == interface_e::WIFI)
        {
            // Legacy compatibility path: in NR-only build, WIFI traffic is forwarded over CELL.
            if (DynamicCast<NrUeNetDevice>(dev) || DynamicCast<NrGnbNetDevice>(dev))
            {
                return i;
            }
        }
    }

    NS_LOG_ERROR("No matching device found for requested interface on node " << node->GetId());
    return 0;
}

void
MosaicProxyApp::SetSockets(interface_e interfaceType)
{
    m_interfaceType = interfaceType;

    if (m_socket)
    {
        m_socket->Close();
        m_socket = nullptr;
    }

    m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());

    InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), m_port);
    if (m_socket->Bind(local) < 0)
    {
        NS_FATAL_ERROR("Failed to bind MosaicProxyApp socket on node " << GetNode()->GetId()
                                                                       << " port " << m_port);
        return;
    }

    uint32_t outDeviceIndex = ResolveOutgoingDeviceIndex(interfaceType);
    if (outDeviceIndex < GetNode()->GetNDevices())
    {
        m_socket->BindToNetDevice(GetNode()->GetDevice(outDeviceIndex));
    }

    m_socket->SetAllowBroadcast(true);
    m_socket->SetRecvCallback(MakeCallback(&MosaicProxyApp::Receive, this));
}

void
MosaicProxyApp::TransmitPacket(Ipv4Address dstAddr, uint32_t msgId, uint32_t payloadLength)
{
    if (IsTimingDebugEnabledProxyApp())
    {
        std::ostringstream oss;
        oss << "TX_ENTER node=" << GetNode()->GetId()
            << " msgId=" << msgId
            << " dst=" << dstAddr
            << " payload=" << payloadLength
            << " simNowNs=" << Simulator::Now().GetNanoSeconds()
            << " enabled=" << (m_enabled ? 1 : 0);
        TimingDebugProxyApp(oss.str());
    }

    if (!m_enabled)
    {
        return;
    }

    Ptr<Packet> packet = Create<Packet>(payloadLength);

    FlowIdTag msgIdTag;
    msgIdTag.SetFlowId(msgId);
    packet->AddByteTag(msgIdTag);

    InetSocketAddress ipSA = InetSocketAddress(dstAddr, m_port);

    int result = m_socket->SendTo(packet, 0, ipSA);
    if (IsTimingDebugEnabledProxyApp())
    {
        std::ostringstream oss;
        oss << "TX_SENDTO node=" << GetNode()->GetId()
            << " msgId=" << msgId
            << " dst=" << dstAddr
            << " pktUid=" << packet->GetUid()
            << " simNowNs=" << Simulator::Now().GetNanoSeconds();
        TimingDebugProxyApp(oss.str());
    }

    if (result < 0)
    {
        if (m_socket->GetErrno() == Socket::SocketErrno::ERROR_MSGSIZE)
        {
            NS_LOG_ERROR("Packet too large: msgId=" << msgId
                                                    << " len=" << payloadLength
                                                    << " dst=" << dstAddr);
        }
        else
        {
            NS_LOG_ERROR("SendTo failed: errno=" << m_socket->GetErrno()
                                                 << " msgId=" << msgId
                                                 << " len=" << payloadLength
                                                 << " dst=" << dstAddr);
        }
    }
    else
    {
        NS_LOG_DEBUG("Sent packet: node=" << GetNode()->GetId()
                                          << " msgId=" << msgId
                                          << " len=" << payloadLength
                                          << " dst=" << dstAddr
                                          << " pktUid=" << packet->GetUid());
    }
}

void
MosaicProxyApp::Receive(Ptr<Socket> socket)
{
    if (!m_enabled)
    {
        return;
    }

    Address from;
    Ptr<Packet> packet;

    while ((packet = socket->RecvFrom(from)))
    {
        InetSocketAddress address = InetSocketAddress::ConvertFrom(from);

        FlowIdTag tag;
        int msgId = -1;

        if (packet->FindFirstMatchingByteTag(tag))
        {
            msgId = static_cast<int>(tag.GetFlowId());
        }
        else
        {
            NS_LOG_ERROR("Received packet without FlowIdTag on node "
                         << GetNode()->GetId()
                         << " from " << address.GetIpv4()
                         << " pktUid=" << packet->GetUid()
                         << " size=" << packet->GetSize());
        }

        NS_LOG_DEBUG("Received packet: node=" << GetNode()->GetId()
                                              << " from=" << address.GetIpv4()
                                              << " msgId=" << msgId
                                              << " pktUid=" << packet->GetUid()
                                              << " size=" << packet->GetSize());

        if (!m_recvCallback.IsNull())
        {
            uint32_t ns3NodeId = GetNode()->GetId();
            unsigned long long recvTime = Simulator::Now().GetNanoSeconds();

            if (IsTimingDebugEnabledProxyApp())
            {
                std::ostringstream oss;
                oss << "RX_CALLBACK node=" << ns3NodeId
                    << " msgId=" << msgId
                    << " from=" << address.GetIpv4()
                    << " pktUid=" << packet->GetUid()
                    << " recvTimeNs=" << recvTime;
                TimingDebugProxyApp(oss.str());
            }

            m_recvCallback(recvTime, ns3NodeId, msgId);
        }
    }
}

} // namespace ns3