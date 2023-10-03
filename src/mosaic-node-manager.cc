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
#include "mosaic-lte-proxy-app.h"
#include "ns3/string.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/log.h"
#include "ns3/constant-velocity-mobility-model.h"
#include "ns3/log.h"
#include "ns3/wifi-net-device.h"
#include "ns3/node-list.h"

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

    void MosaicNodeManager::Configure(MosaicNs3Server* serverPtr, CommunicationType commType=ClientServerChannelSpace::CommunicationType::DSRC) {
        m_serverPtr = serverPtr;
        if(commType == ClientServerChannelSpace::CommunicationType::DSRC){
            m_wifiChannelHelper.AddPropagationLoss(m_lossModel);
            m_wifiChannelHelper.SetPropagationDelay(m_delayModel);
            m_channel = m_wifiChannelHelper.Create();
            m_wifiPhyHelper.SetChannel(m_channel);
        } else if (commType == ClientServerChannelSpace::CommunicationType::LTE){
            m_lteHelper = CreateObject<LteHelper>();
            m_lteV2xHelper = CreateObject<LteV2xHelper>();
            m_epcHelper = CreateObject<PointToPointEpcHelper>();
            
            m_lteHelper->SetAttribute("UseSidelink", BooleanValue (true));
            m_lteHelper->SetEpcHelper(m_epcHelper);
            m_lteHelper->DisableNewEnbPhy();
            m_lteV2xHelper->SetLteHelper(m_lteHelper);


            m_lteHelper->SetEnbAntennaModelType ("ns3::NistParabolic3dAntennaModel");
            
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
            BuildingsHelper::MakeMobilityModelConsistent();  

            m_groupL2Address = 0x01;
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
        else{
            NS_LOG_ERROR("Unknown communication type:" << commType);
        }
    }

    void MosaicNodeManager::CreateMosaicNode(int ID, Vector position, CommunicationType commType=ClientServerChannelSpace::CommunicationType::DSRC) {
        if (m_isDeactivated[ID]) {
            return;
        }
        Ptr<Node> singleNode = CreateObject<Node>();
        
        NS_LOG_INFO("Created node " << singleNode->GetId());
        m_mosaic2ns3ID[ID] = singleNode->GetId();

        // Install the appropriate device based on communication type
        if (commType == ClientServerChannelSpace::CommunicationType::DSRC) {
            NS_LOG_INFO ("Creating wifi helpers for the node...");
            InternetStackHelper internet;   
            internet.Install(singleNode);
            NetDeviceContainer netDevices = m_wifi80211pHelper.Install(m_wifiPhyHelper, m_waveMacHelper, singleNode);
            m_ipAddressHelper.Assign(netDevices);

            //Install mobility model
            NS_LOG_INFO("Install ConstantVelocityMobilityModel on node " << singleNode->GetId());
            Ptr<ConstantVelocityMobilityModel> mobModel = CreateObject<ConstantVelocityMobilityModel>();
            mobModel->SetPosition(position);
            singleNode->AggregateObject(mobModel);

        } else if (commType == ClientServerChannelSpace::CommunicationType::LTE) {
            
            // Associate the node with buildings for better radio propagation modeling
            BuildingsHelper::Install(singleNode);

            // Ensure that the mobility models of all nodes are consistent with their positions
            BuildingsHelper::MakeMobilityModelConsistent(); 

            // Install an LTE device on the node
            NetDeviceContainer ueDev = m_lteHelper->InstallUeDevice(singleNode);
            m_ueDevs.Add(ueDev);
            // Install the internet stack on the node
            InternetStackHelper internet;
            internet.Install(singleNode);

            // Assign an IPv4 address to the LTE device
            Ipv4InterfaceContainer vehicleIpIface = m_epcHelper->AssignUeIpv4Address(ueDev);

            // Set up static routing for the node to use the default gateway provided by the EPC helper
            Ipv4StaticRoutingHelper Ipv4RoutingHelper;
            Ptr<Ipv4StaticRouting> vehicleStaticRouting = Ipv4RoutingHelper.GetStaticRouting(singleNode->GetObject<Ipv4>());
            vehicleStaticRouting->SetDefaultRoute(m_epcHelper->GetUeDefaultGatewayAddress(), 1);

            // Attach the LTE device to the eNodeB (base station)
            m_lteHelper->Attach(ueDev);

            // Create and activate a sidelink bearer for V2X communication
            Ptr<LteSlTft> tft = Create<LteSlTft>(LteSlTft::BIDIRECTIONAL, m_clientRespondersAddress, m_groupL2Address); 
            m_lteV2xHelper->ActivateSidelinkBearer(Simulator::Now(), ueDev, tft);

            m_groupL2Address++;
            m_clientRespondersAddress = Ipv4AddressGenerator::NextAddress (Ipv4Mask ("255.255.0.0"));

            // Install the V2X sidelink configuration on the LTE device
            m_lteHelper->InstallSidelinkV2xConfiguration(ueDev, m_ueSidelinkConfiguration);            

            //Install mobility model
            Ptr<ConstantVelocityMobilityModel> mobModel = CreateObject<ConstantVelocityMobilityModel>();
            mobModel->SetPosition(position);
            singleNode->AggregateObject(mobModel);

        }
        else{
            NS_LOG_ERROR("Unknown communication type:" << commType);
            m_mosaic2ns3ID.erase(singleNode->GetId);
            singleNode = nullptr;
        }

        //Install app
        NS_LOG_INFO("Install MosaicProxyApp application on node " << singleNode->GetId());
        Ptr<MosaicProxyApp> app = CreateObject<MosaicProxyApp>();
        app->SetNodeManager(this);
        singleNode->AddApplication(app);
        app->SetSockets();


        return;
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
        app->TransmitPacket(protocolID, msgID, payLength, ipv4Add);
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
    void MosaicNodeManager::ConfigureNodeRadio(uint32_t nodeId, bool radioTurnedOn, int transmitPower, CommunicationType commType=ClientServerChannelSpace::CommunicationType::DSRC) {
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
                if (commType == ClientServerChannelSpace::CommunicationType::DSRC) {
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
                } else if (commType == ClientServerChannelSpace::CommunicationType::LTE) {
                    Ptr<NetDevice> netDev = DynamicCast<NetDevice> (node->GetDevice(1));
                    if (netDev == nullptr) {
                        NS_LOG_ERROR("Inconsistency: no matching NetDevice found on node while configuring");
                        return;
                    } 
                    Ptr<LteUePhy> uePhy = DynamicCast<LteUePhy> (netDev->GetPhy());
                    if (uePhy != 0){
                        uePhy->SetTxPower(txDBm);
                    }
                }
                else{
                    NS_LOG_ERROR("Unknown communication type:" << commType);
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
