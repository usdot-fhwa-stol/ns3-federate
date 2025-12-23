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
 * You should have received a copy of the GNU General General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "mosaic-node-manager.h"
#include "mosaic-proxy-app.h"

// NS-3 includes needed by this implementation
#include "ns3/core-module.h"
#include "ns3/log.h"
#include "ns3/mobility-module.h"
#include "ns3/node-list.h"

#include "ns3/antenna-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/point-to-point-module.h"
#include "ns3/nr-module.h"

NS_LOG_COMPONENT_DEFINE("MosaicNodeManager");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(MosaicNodeManager);

TypeId
MosaicNodeManager::GetTypeId(void)
{
  static TypeId tid =
      TypeId("ns3::MosaicNodeManager")
          .SetParent<Object>()
          .AddConstructor<MosaicNodeManager>()
          .AddAttribute("LossModel",
                        "The used loss model",
                        StringValue("ns3::FriisPropagationLossModel"),
                        MakeStringAccessor(&MosaicNodeManager::m_lossModel),
                        MakeStringChecker())
          .AddAttribute("DelayModel",
                        "The used delay model",
                        StringValue("ns3::ConstantSpeedPropagationDelayModel"),
                        MakeStringAccessor(&MosaicNodeManager::m_delayModel),
                        MakeStringChecker());

  return tid;
}

MosaicNodeManager::MosaicNodeManager()
{
  // No IP helper needed for EPC-assigned UE IPs.
}

void
MosaicNodeManager::Configure(MosaicNs3Server* serverPtr)
{
  m_serverPtr = serverPtr;
  InitializeNrIfNeeded();
}

void
MosaicNodeManager::InitializeNrIfNeeded()
{
  if (m_nrInitialized)
    {
      return;
    }
  m_nrInitialized = true;

  // ==========================================================
  // EPC helper + PGW + remoteHost backhaul (like your test)
  // ==========================================================
  m_epcHelper = CreateObject<NrPointToPointEpcHelper>();
  m_pgw = m_epcHelper->GetPgwNode();

  // Create remote host (inside ns-3 topology)
  NodeContainer remoteHostContainer;
  remoteHostContainer.Create(1);
  m_remoteHost = remoteHostContainer.Get(0);

  InternetStackHelper internet;
  internet.Install(remoteHostContainer);

  // PGW <-> remoteHost link
  PointToPointHelper p2ph;
  p2ph.SetDeviceAttribute("DataRate", DataRateValue(DataRate("100Gb/s")));
  p2ph.SetDeviceAttribute("Mtu", UintegerValue(2500));
  p2ph.SetChannelAttribute("Delay", TimeValue(Seconds(0.0)));

  NetDeviceContainer internetDevices = p2ph.Install(m_pgw, m_remoteHost);

  Ipv4AddressHelper ipv4h;
  ipv4h.SetBase("1.0.0.0", "255.0.0.0");
  Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign(internetDevices);
  // index 0 is PGW side, index 1 is remoteHost side
  m_remoteHostAddr = internetIpIfaces.GetAddress(1);

  // Route from remoteHost to UE subnet.
  // NOTE: Many CTTC/NR examples use 7.0.0.0/8 for UE addresses.
  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  Ptr<Ipv4StaticRouting> remoteHostStaticRouting =
      ipv4RoutingHelper.GetStaticRouting(m_remoteHost->GetObject<Ipv4>());

  remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"),
                                             Ipv4Mask("255.0.0.0"),
                                             1);

  // ==========================================================
  // NR helper + beamforming
  // ==========================================================
  m_beamformingHelper = CreateObject<IdealBeamformingHelper>();
  m_nrHelper = CreateObject<NrHelper>();
  m_nrHelper->SetEpcHelper(m_epcHelper);
  m_nrHelper->SetBeamformingHelper(m_beamformingHelper);

  // Similar to your test harness
  Config::SetDefault("ns3::ThreeGppChannelModel::UpdatePeriod", TimeValue(MilliSeconds(0)));
  m_nrHelper->SetChannelConditionModelAttribute("UpdatePeriod", TimeValue(MilliSeconds(0)));
  m_nrHelper->SetPathlossAttribute("ShadowingEnabled", BooleanValue(false));

  m_beamformingHelper->SetAttribute("BeamformingMethod",
                                    TypeIdValue(DirectPathBeamforming::GetTypeId()));

  // Create single operational band (can extend to dual-band later)
  CcBwpCreator ccBwpCreator;
  const uint8_t numCcPerBand = 1;

  CcBwpCreator::SimpleOperationBandConf bandConf(m_centralFrequency,
                                                 m_bandwidth,
                                                 numCcPerBand,
                                                 BandwidthPartInfo::UMi_StreetCanyon);

  m_band = ccBwpCreator.CreateOperationBandContiguousCc(bandConf);
  m_nrHelper->InitializeOperationBand(&m_band);
  m_allBwps = CcBwpCreator::GetAllBwps({m_band});

  // Antennas (same spirit as your test)
  m_nrHelper->SetUeAntennaAttribute("NumRows", UintegerValue(2));
  m_nrHelper->SetUeAntennaAttribute("NumColumns", UintegerValue(4));
  m_nrHelper->SetUeAntennaAttribute("AntennaElement",
                                    PointerValue(CreateObject<IsotropicAntennaModel>()));

  m_nrHelper->SetGnbAntennaAttribute("NumRows", UintegerValue(4));
  m_nrHelper->SetGnbAntennaAttribute("NumColumns", UintegerValue(8));
  m_nrHelper->SetGnbAntennaAttribute("AntennaElement",
                                     PointerValue(CreateObject<IsotropicAntennaModel>()));

  // ==========================================================
  // Create a single gNB
  // ==========================================================
  m_gnbNodes.Create(1);
  internet.Install(m_gnbNodes); // reuse 'internet'

  Ptr<ConstantPositionMobilityModel> gnbMob = CreateObject<ConstantPositionMobilityModel>();
  gnbMob->SetPosition(Vector(0.0, 0.0, 1.5));
  m_gnbNodes.Get(0)->AggregateObject(gnbMob);

  m_gnbDevs = m_nrHelper->InstallGnbDevice(m_gnbNodes, m_allBwps);

  // Numerology + TxPower
  m_nrHelper->GetGnbPhy(m_gnbDevs.Get(0), 0)->SetAttribute("Numerology", UintegerValue(m_numerology));
  m_nrHelper->GetGnbPhy(m_gnbDevs.Get(0), 0)->SetAttribute("TxPower", DoubleValue(m_totalTxPowerDbm));

  for (auto it = m_gnbDevs.Begin(); it != m_gnbDevs.End(); ++it)
    {
      DynamicCast<NrGnbNetDevice>(*it)->UpdateConfig();
    }

  NS_LOG_INFO("NR initialized. PGW=" << m_pgw->GetId()
                                     << " RemoteHost=" << m_remoteHost->GetId()
                                     << " RemoteHostIP=" << m_remoteHostAddr);
}

Ipv4Address
MosaicNodeManager::ConfigureUeNetworking(Ptr<Node> ue, Ptr<NetDevice> ueDev)
{
  // Install IP stack on UE
  InternetStackHelper internet;
  internet.Install(ue);

  // Assign UE IPv4 via EPC
  Ipv4InterfaceContainer ifc = m_epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueDev));
  Ipv4Address ueIp = ifc.GetAddress(0);

  NS_LOG_INFO("Assigned UE IP: " << ueIp);

  // Default route to EPC gateway
  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  Ptr<Ipv4StaticRouting> ueStaticRouting =
      ipv4RoutingHelper.GetStaticRouting(ue->GetObject<Ipv4>());

  ueStaticRouting->SetDefaultRoute(m_epcHelper->GetUeDefaultGatewayAddress(), 1);

  return ueIp;
}

void
MosaicNodeManager::CreateMosaicNode(int ID, Vector position)
{
  if (m_isDeactivated[ID])
    {
      return;
    }

  InitializeNrIfNeeded();

  Ptr<Node> singleNode = CreateObject<Node>();
  m_mosaic2ns3ID[ID] = singleNode->GetId();
  m_ueNodesByMosaicId[ID] = singleNode;

  NS_LOG_INFO("Created UE node ns3Id=" << singleNode->GetId() << " for mosaicId=" << ID);

  // Mobility: MOSAIC updates position
  Ptr<ConstantVelocityMobilityModel> mobModel = CreateObject<ConstantVelocityMobilityModel>();
  mobModel->SetPosition(position);
  singleNode->AggregateObject(mobModel);

  // Install UE NR device
  NetDeviceContainer ueDevs = m_nrHelper->InstallUeDevice(NodeContainer(singleNode), m_allBwps);
  Ptr<NetDevice> ueDev = ueDevs.Get(0);
  m_ueDevByMosaicId[ID] = ueDev;

  // IP + default route (and store IP)
  Ipv4Address ueIp = ConfigureUeNetworking(singleNode, ueDev);
  m_ueIpByMosaicId[ID] = ueIp;

  // Attach to closest gNB
  m_nrHelper->AttachToClosestEnb(NetDeviceContainer(ueDev), m_gnbDevs);

  // Apply UE config
  DynamicCast<NrUeNetDevice>(ueDev)->UpdateConfig();

  // Install app
  Ptr<MosaicProxyApp> app = CreateObject<MosaicProxyApp>();
  app->SetNodeManager(this);
  singleNode->AddApplication(app);
  app->SetSockets();

  NS_LOG_INFO("Installed MosaicProxyApp on ns3Id=" << singleNode->GetId()
                                                   << " mosaicId=" << ID
                                                   << " ueIp=" << ueIp);
}

uint32_t
MosaicNodeManager::GetNs3NodeId(uint32_t nodeId)
{
  return m_mosaic2ns3ID[nodeId];
}

bool
MosaicNodeManager::ActivateNode(uint32_t nodeId)
{
  auto it = m_mosaic2ns3ID.find(nodeId);
  if (it == m_mosaic2ns3ID.end())
    {
      NS_LOG_ERROR("ActivateNode: unknown mosaic nodeId=" << nodeId);
      return false;
    }

  m_isDeactivated[nodeId] = false;

  Ptr<Node> node = NodeList::GetNode(it->second);
  Ptr<MosaicProxyApp> app = DynamicCast<MosaicProxyApp>(node->GetApplication(0));
  if (app)
    {
      app->Enable();
    }

  return true;
}

void
MosaicNodeManager::SendMsg(uint32_t nodeId,
                           uint32_t protocolID,
                           uint32_t msgID,
                           uint32_t payLength,
                           Ipv4Address ipv4Add)
{
  if (m_isDeactivated[nodeId])
    {
      return;
    }

  auto it = m_mosaic2ns3ID.find(nodeId);
  if (it == m_mosaic2ns3ID.end())
    {
      NS_LOG_ERROR("SendMsg: unknown mosaic nodeId=" << nodeId);
      return;
    }

  Ptr<Node> node = NodeList::GetNode(it->second);
  Ptr<MosaicProxyApp> app = DynamicCast<MosaicProxyApp>(node->GetApplication(0));
  if (app == nullptr)
    {
      NS_LOG_ERROR("Node mosaicId=" << nodeId << " ns3Id=" << it->second
                                    << " missing MosaicProxyApp");
      return;
    }

  app->TransmitPacket(protocolID, msgID, payLength, ipv4Add);
}

void
MosaicNodeManager::AddRecvPacket(unsigned long long recvTime, Ptr<Packet> pack, int nodeID, int msgID)
{
  if (m_isDeactivated[nodeID])
    {
      return;
    }

  m_serverPtr->AddRecvPacket(recvTime, pack, nodeID, msgID);
}

void
MosaicNodeManager::UpdateNodePosition(uint32_t nodeId, Vector position)
{
  if (m_isDeactivated[nodeId])
    {
      return;
    }

  auto it = m_mosaic2ns3ID.find(nodeId);
  if (it == m_mosaic2ns3ID.end())
    {
      NS_LOG_ERROR("UpdateNodePosition: unknown mosaic nodeId=" << nodeId);
      return;
    }

  Ptr<Node> node = NodeList::GetNode(it->second);
  Ptr<MobilityModel> mobModel = node->GetObject<MobilityModel>();
  if (!mobModel)
    {
      NS_LOG_ERROR("UpdateNodePosition: ns3Id=" << it->second << " has no MobilityModel");
      return;
    }
  mobModel->SetPosition(position);
}

void
MosaicNodeManager::DeactivateNode(uint32_t nodeId)
{
  if (m_isDeactivated[nodeId])
    {
      return;
    }

  auto it = m_mosaic2ns3ID.find(nodeId);
  if (it == m_mosaic2ns3ID.end())
    {
      NS_LOG_ERROR("DeactivateNode: unknown mosaic nodeId=" << nodeId);
      return;
    }

  Ptr<Node> node = NodeList::GetNode(it->second);
  Ptr<MosaicProxyApp> app = DynamicCast<MosaicProxyApp>(node->GetApplication(0));
  if (app)
    {
      app->Disable();
    }

  // NR PHY detach/sleep is not implemented here (version-dependent).
  // Treat "deactivate" as application-level disable + ignore sends.
  m_isDeactivated[nodeId] = true;
}

void
MosaicNodeManager::ConfigureNodeRadio(uint32_t nodeId, bool radioTurnedOn, int transmitPower)
{
  if (m_isDeactivated[nodeId])
    {
      return;
    }

  auto it = m_mosaic2ns3ID.find(nodeId);
  if (it == m_mosaic2ns3ID.end())
    {
      NS_LOG_ERROR("ConfigureNodeRadio: unknown mosaic nodeId=" << nodeId);
      return;
    }

  Ptr<Node> node = NodeList::GetNode(it->second);
  Ptr<MosaicProxyApp> ssa = node->GetApplication(0)->GetObject<MosaicProxyApp>();
  if (!ssa)
    {
      NS_LOG_ERROR("ConfigureNodeRadio: No MosaicProxyApp on ns3Id=" << it->second);
      return;
    }

  if (radioTurnedOn)
    {
      ssa->Enable();

      if (transmitPower > -1)
        {
          // Placeholder: mapping MOSAIC "mW" power to NR TxPower is version-specific.
          NS_LOG_WARN("ConfigureNodeRadio: NR transmitPower not implemented; ignoring transmitPower="
                      << transmitPower);
        }
    }
  else
    {
      ssa->Disable();
    }
}

} // namespace ns3
