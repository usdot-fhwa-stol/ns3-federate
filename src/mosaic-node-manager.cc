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

#include "ns3/yans-wifi-phy.h"
#include "mosaic-node-manager.h"

#include "mosaic-ns3-server.h"

#include "ns3/wave-net-device.h"
#include "mosaic-proxy-app.h"
#include "ns3/string.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/log.h"
#include "ns3/constant-velocity-mobility-model.h"
#include "ns3/log.h"
#include "ns3/wifi-net-device.h"
#include "ns3/node-list.h"
#include "ns3/mobility-module.h"

NS_LOG_COMPONENT_DEFINE("MosaicNodeManager");

namespace ns3 {
    
    NS_OBJECT_ENSURE_REGISTERED(MosaicNodeManager);

    TypeId MosaicNodeManager::GetTypeId(void) {
        static TypeId tid = TypeId("ns3::MosaicNodeManager")
                .SetParent<Object>()
                .AddConstructor<MosaicNodeManager>()
                .AddAttribute("LossModel", "The used loss model",
                StringValue("ns3::FriisPropagationLossModel"),
                MakeStringAccessor(&MosaicNodeManager::m_lossModel),
                MakeStringChecker())
                .AddAttribute("DelayModel", "The used delay model",
                StringValue("ns3::ConstantSpeedPropagationDelayModel"),
                MakeStringAccessor(&MosaicNodeManager::m_delayModel),
                MakeStringChecker());
        return tid;
    }

    MosaicNodeManager::MosaicNodeManager() : m_ipAddressHelper("10.1.0.0", "255.255.0.0") {
    }

    void MosaicNodeManager::Configure(MosaicNs3Server* serverPtr, CommunicationType commType) {
        m_serverPtr = serverPtr;
        m_commType = commType;
    }

    void MosaicNodeManager::PhyRsrpSinrTrace(const std::string &path, uint16_t rnti, double rsrp, double sinr) {
        NS_LOG_INFO("PHY Layer Trace: RNTI=" << rnti << ", RSRP=" << rsrp << ", SINR=" << sinr);
    }

    void MosaicNodeManager::MacDlSchedulingTrace(const std::string &path, uint32_t frameNo, uint32_t subframeNo, uint16_t rnti, uint8_t mcs, uint16_t size) {
        NS_LOG_INFO("MAC Layer Trace: Frame=" << frameNo << ", Subframe=" << subframeNo << ", RNTI=" << rnti << ", MCS=" << mcs << ", Size=" << size);
    }

    void MosaicNodeManager::InitLte(int numOfNode){

        // Set the UEs power in dBm
        Config::SetDefault ("ns3::LteUePhy::RsrpUeMeasThreshold", DoubleValue (-10.0));
        Config::SetDefault ("ns3::LteUePowerControl::Pcmax", DoubleValue (50));
        Config::SetDefault ("ns3::LteUePowerControl::PsschTxPower", DoubleValue (50));
        Config::SetDefault ("ns3::LteUePowerControl::PscchTxPower", DoubleValue (50));
        // Enable V2X communication on PHY layer
        Config::SetDefault ("ns3::LteUePhy::EnableV2x", BooleanValue (true));

         std::cout << "FEDERATE DEBUG: Create predefine node" << std::endl;
        NodeContainer ueAllNodes;
        m_ueNodes.Create(numOfNode);
        ueAllNodes.Add(m_ueNodes);

        MobilityHelper mobility;
        mobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
        Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();

        // Set the distant position to (10000, 10000, 0) which is faraway from the scenario
        positionAlloc->Add(Vector(10000, 10000, 0));
        mobility.SetPositionAllocator(positionAlloc);
        mobility.Install(m_ueNodes);
 
        Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();

        m_lteHelper = CreateObject<LteHelper>();
        m_lteHelper->SetEpcHelper(epcHelper);
        m_lteHelper->DisableNewEnbPhy();

        m_lteV2xHelper = CreateObject<LteV2xHelper>();
        m_lteV2xHelper->SetLteHelper(m_lteHelper);

        m_lteHelper->SetEnbAntennaModelType ("ns3::NistParabolic3dAntennaModel");
        
        m_lteHelper->SetAttribute ("UseSameUlDlPropagationCondition", BooleanValue(true));
        Config::SetDefault ("ns3::LteEnbNetDevice::UlEarfcn", StringValue ("54990"));        
        m_lteHelper->SetAttribute ("PathlossModel", StringValue ("ns3::CniUrbanmicrocellPropagationLossModel"));
        
        NodeContainer eNodeB;
        eNodeB.Create(1); 

        // Topology eNodeB
        Ptr<ListPositionAllocator> pos_eNB = CreateObject<ListPositionAllocator>(); 
        pos_eNB->Add(Vector(0, 0, 0));

        // Install mobility eNodeB
        MobilityHelper mob_eNB;
        mob_eNB.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        mob_eNB.SetPositionAllocator(pos_eNB);
        mob_eNB.Install(eNodeB);
        
        NetDeviceContainer enbDevs = m_lteHelper->InstallEnbDevice(eNodeB);

        BuildingsHelper::Install (eNodeB);
        BuildingsHelper::Install (ueAllNodes);
        BuildingsHelper::MakeMobilityModelConsistent();  
        
        m_lteHelper->SetAttribute("UseSidelink", BooleanValue (true));

        NetDeviceContainer ueRespondersDevs = m_lteHelper->InstallUeDevice (m_ueNodes);
        NetDeviceContainer ueDevs;
        ueDevs.Add(ueRespondersDevs);
        
        for (uint16_t i=0; i<m_ueNodes.GetN();i++)
        {
            m_ns3Id2DeviceId[m_ueNodes.Get(i)->GetId()] = i;
            m_ueNodeIdList.push_back(m_ueNodes.Get(i)->GetId());
        }

        // Install the IP stack on the UEs
        NS_LOG_INFO ("Installing IP stack..."); 
        InternetStackHelper internet;
        internet.Install (ueAllNodes); 

        // Assign an IPv4 address to the LTE device
        std::cout << "FEDERATE DEBUG: assign IP to the device" << std::endl;
        Ipv4InterfaceContainer vehicleIpIface = epcHelper->AssignUeIpv4Address(ueDevs);
        Ipv4StaticRoutingHelper Ipv4RoutingHelper;

        // Set up static routing for the node to use the default gateway provided by the EPC helper
        for(uint32_t i = 0; i < ueAllNodes.GetN(); ++i)
        {
            Ptr<Node> ueNode = ueAllNodes.Get(i);
            // Set the default gateway for the UE
            Ptr<Ipv4StaticRouting> ueStaticRouting = Ipv4RoutingHelper.GetStaticRouting(ueNode->GetObject<Ipv4>());
            ueStaticRouting->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress(), 1);       
        }

        // // Attach the LTE device to the eNodeB (base station)
        std::cout << "FEDERATE DEBUG: attach lte device to the eNodeB" << std::endl;
        m_lteHelper->Attach(ueDevs);

        std::cout << "FEDERATE DEBUG: assign group L2 address" << std::endl;
        std::vector<NetDeviceContainer> txGroups = m_lteV2xHelper->AssociateForV2xBroadcast(ueRespondersDevs, numOfNode); 

        uint32_t groupL2Address = 0x00;
        Ipv4AddressGenerator::Init(Ipv4Address ("255.0.0.0"), Ipv4Mask("255.0.0.0"));
        Ipv4Address clientRespondersAddress = Ipv4AddressGenerator::NextAddress (Ipv4Mask ("255.0.0.0"));

        NetDeviceContainer activeTxUes;

        for(auto gIt=txGroups.begin(); gIt != txGroups.end(); gIt++){

            Ptr<NetDevice> ueDev = gIt->Get(0);
            Ptr<Node> ueNode = ueDev->GetNode();

            // Create and activate a sidelink bearer for V2X communication
            std::cout << "FEDERATE DEBUG: Create and activate a sidelink bearer for V2X communication" << std::endl;
            
            NetDeviceContainer txUe ((*gIt).Get(0));
            activeTxUes.Add(txUe);
            NetDeviceContainer rxUes = m_lteV2xHelper->RemoveNetDevice ((*gIt), txUe.Get (0));
            Ptr<LteSlTft> tft = Create<LteSlTft>(LteSlTft::TRANSMIT, clientRespondersAddress, groupL2Address); 
            m_lteV2xHelper->ActivateSidelinkBearer(Seconds(0.0), txUe, tft);
            tft = Create<LteSlTft>(LteSlTft::RECEIVE, clientRespondersAddress, groupL2Address); 
            m_lteV2xHelper->ActivateSidelinkBearer(Seconds(0.0), rxUes, tft);

            Ptr<LteUeMac> ueMac = DynamicCast<LteUeMac>( txUe.Get (0)->GetObject<LteUeNetDevice> ()->GetMac () );

            std::cout << "Install MosaicProxyApp on node " << ueNode->GetId() << std::endl;
            Ptr<MosaicProxyApp> app = CreateObject<MosaicProxyApp>();
            app->SetNodeManager(this);
            ueNode->AddApplication(app);
            app->SetCommType(m_commType);
            app->SetSockets(clientRespondersAddress);
            app->SetSockets();

            std::cout << "FEDERATE DEBUG: clientResponderAddress for node " << ueNode->GetId() << " : " << clientRespondersAddress << std::endl;
            m_ns3ID2UniqueAddress[ueNode->GetId()] = clientRespondersAddress;
            groupL2Address++;
            clientRespondersAddress = Ipv4AddressGenerator::NextAddress (Ipv4Mask ("255.0.0.0"));
        }
            
        
        // Sidelink configuration
        m_ueSidelinkConfiguration = CreateObject<LteUeRrcSl>();
        m_ueSidelinkConfiguration->SetSlEnabled(true);
        m_ueSidelinkConfiguration->SetV2xEnabled(true);

        LteRrcSap::SlV2xPreconfiguration preconfiguration;
        preconfiguration.v2xPreconfigFreqList.freq[0].v2xCommPreconfigGeneral.carrierFreq = 54890;
        preconfiguration.v2xPreconfigFreqList.freq[0].v2xCommPreconfigGeneral.slBandwidth = 30;
        
        preconfiguration.v2xPreconfigFreqList.freq[0].v2xCommTxPoolList.nbPools = 1;
        preconfiguration.v2xPreconfigFreqList.freq[0].v2xCommRxPoolList.nbPools = 1;

        SlV2xPreconfigPoolFactory pFactory;
        pFactory.SetHaveUeSelectedResourceConfig (true);
        pFactory.SetSlSubframe (std::bitset<20> (0xFFFFF));
        pFactory.SetAdjacencyPscchPssch (true);
        pFactory.SetSizeSubchannel (10);
        pFactory.SetNumSubchannel (3);
        pFactory.SetStartRbSubchannel (0);
        pFactory.SetStartRbPscchPool (0);
        pFactory.SetDataTxP0 (-4);
        pFactory.SetDataTxAlpha (0.9);

        preconfiguration.v2xPreconfigFreqList.freq[0].v2xCommTxPoolList.pools[0] = pFactory.CreatePool ();
        preconfiguration.v2xPreconfigFreqList.freq[0].v2xCommRxPoolList.pools[0] = pFactory.CreatePool ();
        m_ueSidelinkConfiguration->SetSlV2xPreconfiguration (preconfiguration); 

        m_lteHelper->InstallSidelinkV2xConfiguration(ueRespondersDevs, m_ueSidelinkConfiguration);  

        m_lteHelper->EnableTraces();

        // for (uint16_t i = 0; i < m_ueNodes.GetN(); i++) {
        //     Ptr<Node> singleNode = m_ueNodes.Get(i);
        //     Ptr<LteUeNetDevice> lteUeDev = singleNode->GetDevice(0)->GetObject<LteUeNetDevice>();

        //     // Attach trace source to PHY layer for RSRP/SINR measurements
        //     Ptr<LteUePhy> uePhy = lteUeDev->GetPhy();
        //     uePhy->TraceConnectWithoutContext("ReportCurrentCellRsrpSinr", MakeCallback(&MosaicNodeManager::PhyRsrpSinrTrace, this));

        //     // Attach trace source to MAC layer for scheduling
        //     Ptr<LteUeMac> ueMac = lteUeDev->GetMac();
        //     ueMac->TraceConnectWithoutContext("DlScheduling", MakeCallback(&MosaicNodeManager::MacDlSchedulingTrace, this));
        // }

    }

    void MosaicNodeManager::InitDsrc(){
        std::cout << "FEDERATE DEBUG: Initialize DSRC NS-3 node" << std::endl;
        m_wifiChannelHelper.AddPropagationLoss(m_lossModel);
        m_wifiChannelHelper.SetPropagationDelay(m_delayModel);
        m_channel = m_wifiChannelHelper.Create();
        m_wifiPhyHelper.SetChannel(m_channel);
    }

    void MosaicNodeManager::CreateMosaicNode(int ID, Vector position) {


        // Install the appropriate device based on communication type
        if (m_commType == DSRC) {
            if (m_isDeactivated[ID]) {
                return;
            }
            Ptr<Node> singleNode = CreateObject<Node>();
            
            NS_LOG_INFO("Created node " << singleNode->GetId());
            m_mosaic2ns3ID[ID] = singleNode->GetId();

            //Install Wave device
            NS_LOG_INFO("Install WAVE on node " << singleNode->GetId());
            InternetStackHelper internet;   
            internet.Install(singleNode);
            NetDeviceContainer netDevices = m_wifi80211pHelper.Install(m_wifiPhyHelper, m_waveMacHelper, singleNode);
            m_ipAddressHelper.Assign(netDevices);

            //Install app
            NS_LOG_INFO("Install MosaicProxyApp application on node " << singleNode->GetId());
            Ptr<MosaicProxyApp> app = CreateObject<MosaicProxyApp>();
            app->SetNodeManager(this);
            singleNode->AddApplication(app);
            app->SetSockets();

            //Install mobility model
            NS_LOG_INFO("Install MosaicMobilityModel on node " << singleNode->GetId());
            Ptr<ConstantVelocityMobilityModel> mobModel = CreateObject<ConstantVelocityMobilityModel>();
            mobModel->SetPosition(position);
            singleNode->AggregateObject(mobModel);

        } else if (m_commType == LTE) {
            std::cout << "FEDERATE DEBUG: Pickup node ID :" << m_ueNodeIdList.front() << " from node pool, set position to : " << position << std::endl;
            m_mosaic2ns3ID[ID] = m_ueNodeIdList.front();
            m_ueNodeIdList.erase(m_ueNodeIdList.begin());
            Ptr<Node> singleNode = NodeList::GetNode(m_mosaic2ns3ID[ID]);
            
            // pick up the node from pool and set the new coordinates
            Ptr<ConstantVelocityMobilityModel> mobModel = singleNode->GetObject<ConstantVelocityMobilityModel>();
            mobModel->SetPosition(position); 

            std::cout << "Completed Creating LTE Node" << std::endl;
        }
        else{
            NS_LOG_ERROR("Unknown communication type:" << m_commType);
            return;
        }
    }

    uint32_t MosaicNodeManager::GetNs3NodeId(uint32_t nodeId) {
        return m_mosaic2ns3ID[nodeId];
    }

    void MosaicNodeManager::SendMsg(uint32_t nodeId, uint32_t protocolID, uint32_t msgID, uint32_t payLength, Ipv4Address ipv4Add) {
        if (m_isDeactivated[nodeId]) {
            return;
        }
        NS_LOG_INFO("Mosaic MosaicNodeManager::SendMsg " << nodeId);
        Ptr<Node> node = NodeList::GetNode(m_mosaic2ns3ID[nodeId]);
        std::cout << "FEDERATE DEBUG: Retrieved Node ID " << m_mosaic2ns3ID[nodeId] << " with node " << node << std::endl;
        
        Ptr<MosaicProxyApp> app = DynamicCast<MosaicProxyApp> (node->GetApplication(0));
        if (app == nullptr) {
            std::cout << "FEDERATE DEBUG: Node " << nodeId << " was not initialized properly, MosaicProxyApp is missing" << std::endl;
            NS_LOG_ERROR("Node " << nodeId << " was not initialized properly, MosaicProxyApp is missing");
            return;
        }
        if (m_commType == DSRC) {
            app->TransmitPacket(protocolID, msgID, payLength, ipv4Add);
        }
        else if (m_commType == LTE) {
            // For LTE communication, send message to sidelink
            // clientRespondersAddress is stored in m_ns3ID2UniqueAddress which a way for the sidelink communication
            std::cout << "FEDERATE DEBUG: Send from address " << m_ns3ID2UniqueAddress[m_mosaic2ns3ID[nodeId]] << std::endl;
            app->TransmitPacket(protocolID, msgID, payLength, m_ns3ID2UniqueAddress[m_mosaic2ns3ID[nodeId]]);
        }
        else{
            NS_LOG_ERROR("Unknown communication type:" << m_commType);
            return;
        }
    }

    void MosaicNodeManager::AddRecvPacket(unsigned long long recvTime, Ptr<Packet> pack, int nodeID, int msgID) {
        uint32_t ns3NodeId = m_mosaic2ns3ID[nodeID];
        if (m_isDeactivated[ns3NodeId]) {
            return;
        }
        std::cout << "FEDERATE DEBUG: AddRecvPacket to " << ns3NodeId << std::endl;
        
        m_serverPtr->AddRecvPacket(recvTime, pack, nodeID, msgID);
    }

    void MosaicNodeManager::UpdateNodePosition(uint32_t nodeId, Vector position) {
        if (m_isDeactivated[nodeId]) {
            return;
        }
        uint32_t ns3NodeId = m_mosaic2ns3ID[nodeId];
        
        Ptr<Node> node = NodeList::GetNode(ns3NodeId);
        Ptr<MobilityModel> mobModel = node->GetObject<MobilityModel> ();
        std::cout << "FEDERATE DEBUG: UpdateNodePosition Node ID:" << ns3NodeId << " from " << mobModel->GetPosition() << " to: " << position << std::endl;
        mobModel->SetPosition(position);

    }

    void MosaicNodeManager::DeactivateNode(uint32_t nodeId) {
        if (m_isDeactivated[nodeId]) {
            return;
        }
        
        Ptr<Node> node = NodeList::GetNode(nodeId);
        Ptr<WifiNetDevice> netDev = DynamicCast<WifiNetDevice> (node->GetDevice(1));
        
        if (netDev == nullptr) {
            NS_LOG_ERROR("Node " << nodeId << " has no WifiNetDevice");
            return;
        }
        //Workaround: set a channel number, which no other phy uses. Channel will this way not let the phy
        //receive. Unfortunately, phys cannot be removed from channel, once added.
        netDev->GetPhy()->SetChannelNumber(0x0);
        netDev->GetPhy()->SetSleepMode();
        
        m_isDeactivated[nodeId] = true;
    }

    /**
     * @brief Evaluates configuration message and applies it to the node
     */
    void MosaicNodeManager::ConfigureNodeRadio(uint32_t nodeId, bool radioTurnedOn, int transmitPower) {
        if (m_isDeactivated[nodeId]) {
            return;
        }

        uint32_t ns3NodeId = m_mosaic2ns3ID[nodeId];
        Ptr<Node> node = NodeList::GetNode(ns3NodeId);
        if (node->GetNApplications() > 0) {
            Ptr<Application> app = node->GetApplication(0);
        } else {
            return;
        }
        std::cout << "FEDERATE DEBUG: ConfigureNodeRadio Node ID:" << ns3NodeId << " transmitPower: " << transmitPower << std::endl;
        Ptr<Application> app = node->GetApplication(0);
        Ptr<MosaicProxyApp> ssa = app->GetObject<MosaicProxyApp>();
        if (!ssa) {
            std::cout << "FEDERATE DEBUG: No app found on node " << std::endl;                        
            NS_LOG_ERROR("No app found on node " << ns3NodeId << " !");
            return;
        }
        if (radioTurnedOn) {
            ssa->Enable();
            std::cout << "FEDERATE DEBUG: radioTurnedOn: " << radioTurnedOn << std::endl;
            if (transmitPower > -1) {
                std::cout << "FEDERATE DEBUG: transmitPower: " << transmitPower << std::endl;
                double txDBm = 10 * log10((double) transmitPower);
                if (m_commType == DSRC) {
                    Ptr<WifiNetDevice> netDev = DynamicCast<WifiNetDevice> (node->GetDevice(1));
                    if (netDev == nullptr) {
                        NS_LOG_ERROR("Inconsistency: no matching NetDevice found on node while configuring");
                        return;
                    }                        
                    Ptr<YansWifiPhy> wavePhy = DynamicCast<YansWifiPhy> (netDev->GetPhy());
                    if (wavePhy != 0) {
                        wavePhy->SetTxPowerStart(txDBm);
                        wavePhy->SetTxPowerEnd(txDBm);
                    }
                } else if (m_commType == LTE) {
                    std::cout << "FEDERATE DEBUG: " << (node->GetDevice(0) == nullptr) << std::endl;
                    Ptr<LteUeNetDevice> netDev = DynamicCast<LteUeNetDevice> (node->GetDevice(0));
                    if (netDev == nullptr) {
                        std::cout << "FEDERATE DEBUG: Inconsistency: no matching NetDevice found on node while configuring" << std::endl;
                        NS_LOG_ERROR("Inconsistency: no matching NetDevice found on node while configuring");
                        return;
                    } 
                    Ptr<LteUePhy> uePhy = DynamicCast<LteUePhy> (netDev->GetPhy());
                    if (uePhy != 0){
                        std::cout << "FEDERATE DEBUG: set tx power of node " << ns3NodeId << " to be " << txDBm << std::endl;
                        uePhy->SetTxPower(txDBm);
                    }
                }
                else{
                    NS_LOG_ERROR("Unknown communication type:" << m_commType);
                    return;
                }
            }
        } else {
            ssa->Disable();
        }
    }

    void MosaicNodeManager::ConfigureSidelink(LteRrcSap::SlV2xPreconfiguration preconfiguration){
        // if (!m_ueSidelinkConfiguration){
        //     NS_LOG_ERROR("Sidelink config has not initialized yet");
        //     return;
        // }
        // if (!m_lteHelper){
        //     NS_LOG_ERROR("LTE helper has not initialized yet");
        //     return;
        // }
        // m_ueSidelinkConfiguration->SetSlV2xPreconfiguration(preconfiguration);

        // // Apply the configuration to all UEs to ensure that all devices have a consistent and updated configuration
        // m_lteHelper->InstallSidelinkV2xConfiguration (m_ueDevs, m_ueSidelinkConfiguration);

    }
}
