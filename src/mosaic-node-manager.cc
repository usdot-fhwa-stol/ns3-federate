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

    void MosaicNodeManager::InitLte(Ptr<PointToPointEpcHelper> epcHelper, NodeContainer eNodeB, int numOfNode){
        
        m_lteHelper = CreateObject<LteHelper>();
        m_lteV2xHelper = CreateObject<LteV2xHelper>();
        m_epcHelper = epcHelper;
        
        m_lteHelper->SetAttribute("UseSidelink", BooleanValue (true));
        m_lteHelper->SetEpcHelper(m_epcHelper);
        m_lteHelper->DisableNewEnbPhy();
        m_lteV2xHelper->SetLteHelper(m_lteHelper);

        m_lteHelper->SetEnbAntennaModelType ("ns3::NistParabolic3dAntennaModel");
        
        m_lteHelper->SetAttribute ("UseSameUlDlPropagationCondition", BooleanValue(true));
        Config::SetDefault ("ns3::LteEnbNetDevice::UlEarfcn", StringValue ("54990"));
        //Config::SetDefault ("ns3::CniUrbanmicrocellPropagationLossModel::Frequency", DoubleValue(5800e6));
        m_lteHelper->SetAttribute ("PathlossModel", StringValue ("ns3::CniUrbanmicrocellPropagationLossModel"));

        // Topology eNodeB
        Ptr<ListPositionAllocator> pos_eNB = CreateObject<ListPositionAllocator>(); 
        pos_eNB->Add(Vector(0, 0, 0));

        std::cout << "FEDERATE DEBUG: Create predefine node" << std::endl;
        NodeContainer predefineNode;
        predefineNode.Create(numOfNode);
        
        MobilityHelper mobility;
        mobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
        Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();

        // Set the distant position to (10000, 10000, 0) which is faraway from the scenario
        positionAlloc->Add(Vector(10000, 10000, 0));
        mobility.SetPositionAllocator(positionAlloc);
        mobility.Install(predefineNode);

        // Install mobility eNodeB
        MobilityHelper mob_eNB;
        mob_eNB.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        mob_eNB.SetPositionAllocator(pos_eNB);
        mob_eNB.Install(eNodeB);

        NetDeviceContainer enbDevs = m_lteHelper->InstallEnbDevice(eNodeB);

        BuildingsHelper::Install (eNodeB);
        BuildingsHelper::Install (predefineNode);
        BuildingsHelper::MakeMobilityModelConsistent();  
        
        // NetDeviceContainer ueDevs = m_lteHelper->InstallUeDevice (predefineNode);

        for (uint16_t i=0; i<predefineNode.GetN();i++)
        {
            std::cout << "FEDERATE DEBUG: install UEDevice to predefine node " << std::endl;
            NetDeviceContainer ueDev = m_lteHelper->InstallUeDevice(predefineNode.Get(i));
            m_ueDevs.Add(ueDev);

            m_ns3Id2DeviceId[predefineNode.Get(i)->GetId()] = i;

            std::cout << "FEDERATE DEBUG: predefine node ID: " << predefineNode.Get(i)->GetId() << std::endl;
            m_preDefineNodeIds.push_back(predefineNode.Get(i)->GetId());

        }

        // Install the IP stack on the UEs
        NS_LOG_INFO ("Installing IP stack..."); 
        InternetStackHelper internet;
        internet.Install (predefineNode); 

        // Assign IP adress to UEs

        // Assign an IPv4 address to the LTE device
        std::cout << "FEDERATE DEBUG: assign IP to the device" << std::endl;
        Ipv4InterfaceContainer vehicleIpIface = m_epcHelper->AssignUeIpv4Address(m_ueDevs);
        Ipv4StaticRoutingHelper Ipv4RoutingHelper;

        // Set up static routing for the node to use the default gateway provided by the EPC helper
        for(uint32_t i = 0; i < predefineNode.GetN(); ++i)
        {
            Ptr<Node> ueNode = predefineNode.Get(i);
            // Set the default gateway for the UE
            Ptr<Ipv4StaticRouting> ueStaticRouting = Ipv4RoutingHelper.GetStaticRouting(ueNode->GetObject<Ipv4>());
            ueStaticRouting->SetDefaultRoute (m_epcHelper->GetUeDefaultGatewayAddress(), 1);
        }

        // // Attach the LTE device to the eNodeB (base station)
        std::cout << "FEDERATE DEBUG: attach lte device to the eNodeB" << std::endl;
        m_lteHelper->Attach(m_ueDevs);

        std::cout << "FEDERATE DEBUG: assign group L2 address" << std::endl;
        m_groupL2Address = 0x00;
        Ipv4AddressGenerator::Init(Ipv4Address ("10.1.0.0"), Ipv4Mask("255.255.0.0"));
        m_clientRespondersAddress = Ipv4AddressGenerator::NextAddress (Ipv4Mask ("255.255.0.0"));



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

            return;
        } else if (m_commType == LTE) {
            std::cout << "FEDERATE DEBUG: Created node " << m_preDefineNodeIds.back() << std::endl;
            m_mosaic2ns3ID[ID] = m_preDefineNodeIds.back();
            m_preDefineNodeIds.pop_back();

            NodeContainer singleNode;
            singleNode.Add(NodeList::GetNode(m_mosaic2ns3ID[ID]));
            uint32_t ns3Id = m_mosaic2ns3ID[ID];
            uint32_t netDeviceId = m_ns3Id2DeviceId[m_mosaic2ns3ID[ID]];

            // pick up the node from pool and set the new coordinates
            singleNode.Get(0)->GetObject<ConstantPositionMobilityModel>();
            mobilityModel->SetPosition(position); 

            NetDeviceContainer ueDev;
            ueDev.Add(m_ueDevs.Get(netDeviceId));

            // Create and activate a sidelink bearer for V2X communication
            std::cout << "FEDERATE DEBUG: Create and activate a sidelink bearer for V2X communication" << std::endl;
            Ptr<LteSlTft> tft = Create<LteSlTft>(LteSlTft::BIDIRECTIONAL, m_clientRespondersAddress, m_groupL2Address); 
            m_lteV2xHelper->ActivateSidelinkBearer(Simulator::Now(), ueDev, tft);
            m_ns3ID2UniqueAddress[ID] = m_clientRespondersAddress;
            m_groupL2Address++;
            m_clientRespondersAddress = Ipv4AddressGenerator::NextAddress (Ipv4Mask ("255.255.0.0"));

            // Install the V2X sidelink configuration on the LTE device
            std::cout << "FEDERATE DEBUG: Install the V2X sidelink configuration on the LTE device" << std::endl;
            m_lteHelper->InstallSidelinkV2xConfiguration(ueDev, m_ueSidelinkConfiguration);            

        }
        else{
            NS_LOG_ERROR("Unknown communication type:" << m_commType);
            return;
        }


        // //Install app
        // NS_LOG_INFO("Install MosaicProxyApp application on node " << singleNode->GetId());
        // std::cout << "FEDERATE DEBUG: Install MosaicProxyApp application on node " << std::endl;
        // Ptr<MosaicProxyApp> app = CreateObject<MosaicProxyApp>();
        // app->SetNodeManager(this);
        // singleNode->AddApplication(app);
        // app->SetSockets();

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
        Ptr<MosaicProxyApp> app = DynamicCast<MosaicProxyApp> (node->GetApplication(0));
        if (app == nullptr) {
            NS_LOG_ERROR("Node " << nodeId << " was not initialized properly, MosaicProxyApp is missing");
            return;
        }
        if (m_commType == DSRC) {
            app->TransmitPacket(protocolID, msgID, payLength, ipv4Add);
        }
        else if (m_commType == LTE) {
            // For LTE communication, send message to sidelink
            // clientRespondersAddress is stored in m_ns3ID2UniqueAddress which a way for the sidelink communication
            app->TransmitPacket(protocolID, msgID, payLength, m_ns3ID2UniqueAddress[nodeId]);
        }
        else{
            NS_LOG_ERROR("Unknown communication type:" << m_commType);
            return;
        }
    }

    void MosaicNodeManager::AddRecvPacket(unsigned long long recvTime, Ptr<Packet> pack, int nodeID, int msgID) {
        if (m_isDeactivated[nodeID]) {
            return;
        }
        
        m_serverPtr->AddRecvPacket(recvTime, pack, nodeID, msgID);
    }

    void MosaicNodeManager::UpdateNodePosition(uint32_t nodeId, Vector position) {
        if (m_isDeactivated[nodeId]) {
            return;
        }
        
        Ptr<Node> node = NodeList::GetNode(nodeId);
        Ptr<MobilityModel> mobModel = node->GetObject<MobilityModel> ();
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
        
        Ptr<Node> node = NodeList::GetNode(nodeId);

        Ptr<Application> app = node->GetApplication(0);
        Ptr<MosaicProxyApp> ssa = app->GetObject<MosaicProxyApp>();
        if (!ssa) {
            NS_LOG_ERROR("No app found on node " << nodeId << " !");
            return;
        }
        if (radioTurnedOn) {
            ssa->Enable();
            if (transmitPower > -1) {
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
                    // Ptr<LteUeNetDevice> netDev = DynamicCast<LteUeNetDevice> (node->GetDevice(1));
                    // if (netDev == nullptr) {
                    //     NS_LOG_ERROR("Inconsistency: no matching NetDevice found on node while configuring");
                    //     return;
                    // } 
                    // Ptr<LteUePhy> uePhy = DynamicCast<LteUePhy> (netDev->GetPhy());
                    // if (uePhy != 0){
                        
                    //     uePhy->SetTxPower(txDBm);
                    // }
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
        if (!m_ueSidelinkConfiguration){
            NS_LOG_ERROR("Sidelink config has not initialized yet");
            return;
        }
        if (!m_lteHelper){
            NS_LOG_ERROR("LTE helper has not initialized yet");
            return;
        }
        m_ueSidelinkConfiguration->SetSlV2xPreconfiguration(preconfiguration);

        // Apply the configuration to all UEs to ensure that all devices have a consistent and updated configuration
        m_lteHelper->InstallSidelinkV2xConfiguration (m_ueDevs, m_ueSidelinkConfiguration);

    }
}
