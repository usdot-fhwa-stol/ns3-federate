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

#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/flow-id-tag.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/log.h"

NS_LOG_COMPONENT_DEFINE("MosaicProxyApp");

namespace ns3 {

    NS_OBJECT_ENSURE_REGISTERED(MosaicProxyApp);

    TypeId MosaicProxyApp::GetTypeId(void) {
        static TypeId tid = TypeId("ns3::MosaicProxyApp")
                .SetParent<Application> ()
                .AddConstructor<MosaicProxyApp> ()
                .AddAttribute("Port", "The socket port for messages",
                UintegerValue(8010),
                MakeUintegerAccessor(&MosaicProxyApp::m_port),
                MakeUintegerChecker<uint16_t> ())
                ;

        return tid;
    }

    void MosaicProxyApp::SetNodeManager(MosaicNodeManager* nodeManager) {
        m_nodeManager = nodeManager;
    }

    void MosaicProxyApp::DoDispose(void) {
        NS_LOG_FUNCTION_NOARGS();
        m_rxSocket = 0;
        Application::DoDispose();
    }

    void MosaicProxyApp::SetCommType(CommunicationType commType){
        m_commType = commType;
    }

    void MosaicProxyApp::Enable(void) {
        std::cout << "Enable proxy app" << std::endl;
        m_active = true;
    }

    void MosaicProxyApp::Disable(void) {
        std::cout << "Disable proxy app" << std::endl;
        m_active = false;
    }

    void MosaicProxyApp::SetMulticastAddr(Ipv4Address multicastAddress){
        m_multicastAddress = multicastAddress;
    }

    void MosaicProxyApp::SetTxSocket(){
        if (!m_txSocket){
            m_txSocket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
            m_txSocket->Bind();
            m_txSocket->Connect(InetSocketAddress(m_multicastAddress, m_port));
            m_txSocket->SetAllowBroadcast(true);
            m_txSocket->ShutdownRecv();
        }else{
            return;
        }
    }

    void MosaicProxyApp::SetRxSocket(void) {
        NS_LOG_INFO("set sockets on node " << GetNode()->GetId());

        if (!m_rxSocket) {
            m_rxSocket = Socket::CreateSocket(GetNode(), TypeId::LookupByName ("ns3::UdpSocketFactory"));
            m_rxSocket->Bind(InetSocketAddress(Ipv4Address::GetAny(), m_port));
            m_rxSocket->SetAllowBroadcast(true);
            m_rxSocket->SetRecvCallback(MakeCallback(&MosaicProxyApp::Receive, this));
        } else {
            NS_FATAL_ERROR("creation attempt of a socket for MosaicProxyApp that has already a socket active");
            return;
        }
    }

    void MosaicProxyApp::TransmitPacket(uint32_t protocolID, uint32_t msgID, uint32_t payLength, Ipv4Address address) {
        NS_LOG_FUNCTION(protocolID << msgID << payLength << address);
        if (!m_active) {
            return;
        }

        Ptr<Packet> packet = Create<Packet> (payLength);
        //Flow tag is used to match the sent message
        FlowIdTag msgIDTag;
        msgIDTag.SetFlowId(msgID);
        packet->AddByteTag(msgIDTag);

        m_sendCount++;

        std::cout<< "Node " << GetNode()->GetId() << " SENDING packet no. " << m_sendCount << " PacketID= " << packet->GetUid() << " at " << Simulator::Now().GetNanoSeconds() << " seconds | packet size = " << packet->GetSize() << std::endl;
        std::cout<< "Is commType DSRC: " << (m_commType == DSRC) << std::endl;
        std::cout<< "Is commType LTE: " << (m_commType == LTE) << std::endl;
        NS_LOG_INFO("Node " << GetNode()->GetId() << " SENDING packet no. " << m_sendCount << " PacketID= " << packet->GetUid() << " at " << Simulator::Now().GetNanoSeconds() << " seconds | packet size = " << packet->GetSize());
        
        //call the socket of this node to send the packet
        if (m_commType == DSRC){
            InetSocketAddress ipSA = InetSocketAddress(address, m_port);
            std::cout << "FEDERATE DEBUG: DSRC sends out the packet successfully: " << (m_rxSocket->SendTo(packet, 0, ipSA) == packet->GetSize()) << std::endl;
        }
        else if (m_commType == LTE){
            std::cout << "FEDERATE DEBUG: LTE sends out the packet successfully: " << (m_txSocket->Send(packet) == packet->GetSize()) << std::endl;
        }
    }

    /*
     * @brief Receive a packet from the socket
     * This method is called by the callback which is defined in the method MosaicProxyApp::SetSockets
     */
    void MosaicProxyApp::Receive(Ptr<Socket> socket) {
        NS_LOG_INFO("FEDERATE DEBUG: Receive Packet" );
        std::cout << "FEDERATE DEBUG: Receive Packet" << std::endl;
        NS_LOG_FUNCTION_NOARGS();
        if (!m_active) {
            return;
        }

        Ptr<Packet> packet;
        NS_LOG_INFO("Start Receiving - Call Socket -> Recv()");
        packet = socket->Recv();

        // m_recvCount++;

        FlowIdTag Tag;
        int msgID;
        //get the flowIdTag
        if (packet->FindFirstMatchingByteTag(Tag)) {
            //send the MsgID
            msgID = Tag.GetFlowId();
            //find the message and send it back
        } else {
            NS_LOG_ERROR("Error, message has no msgIdTag");
            msgID = -1;
        }

        //report the received messages to the MosaicNs3Server instance
        m_nodeManager->AddRecvPacket(Simulator::Now().GetNanoSeconds(), packet, GetNode()->GetId(), msgID);
        NS_LOG_INFO("Receiving message no. " << m_recvCount << " PacketID= " << packet->GetUid() << " at " << Simulator::Now().GetNanoSeconds() << " seconds | message size  = " << packet->GetSize() << " Bytes");
        NS_LOG_INFO("Reception on node " << GetNode()->GetId());
    }
} // namespace ns3
