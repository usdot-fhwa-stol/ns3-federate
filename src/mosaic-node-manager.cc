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

#include "mosaic-node-manager.h"

#include "ns3/constant-velocity-mobility-model.h"
#include "ns3/csma-net-device.h"
#include "ns3/eps-bearer.h"
#include "ns3/epc-tft.h"
#include "ns3/loopback-net-device.h"
#include "ns3/node-list.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/simulator.h"

#include "mosaic-ns3-bridge.h"
#include "mosaic-proxy-app.h"

NS_LOG_COMPONENT_DEFINE("MosaicNodeManager");

namespace ns3
{

namespace
{
static constexpr uint16_t kProxyUdpPort = 8010;

// Idle pool parking area for pre-created / released UEs.
// Keep them separated, at reasonable 2D distance from gNB, and with valid UT height.
static constexpr double kPoolCenterX = 80.0;
static constexpr double kPoolCenterY = 80.0;
static constexpr double kPoolSpacing = 8.0;
static constexpr uint32_t kPoolCols = 10;
static constexpr double kPoolZ = 1.5;

static Vector
GetPoolParkingPosition(uint32_t nodeId)
{
    uint32_t idx = nodeId;
    uint32_t row = idx / kPoolCols;
    uint32_t col = idx % kPoolCols;

    return Vector(kPoolCenterX + col * kPoolSpacing,
                  kPoolCenterY + row * kPoolSpacing,
                  kPoolZ);
}

/**
 * Ensure an IPv4 alias exists on the specified interface.
 * Avoid adding duplicate addresses.
 */
static void
EnsureIpv4Alias(Ptr<Ipv4> ipv4, int32_t ifIndex, Ipv4Address ip, Ipv4Mask mask)
{
    if (ipv4 == nullptr || ifIndex < 0)
    {
        return;
    }

    for (uint32_t i = 0; i < ipv4->GetNAddresses(ifIndex); ++i)
    {
        Ipv4InterfaceAddress existing = ipv4->GetAddress(ifIndex, i);
        if (existing.GetLocal() == ip)
        {
            return;
        }
    }

    ipv4->AddAddress(ifIndex, Ipv4InterfaceAddress(ip, mask));
}

/**
 * Remove all IPv4 aliases on the given interface that belong to the specified prefix.
 * This is used when releasing a pre-created UE back to the pool, so the next reuse
 * does not accumulate old 10.x aliases.
 */
static void
RemoveIpv4AliasesInPrefix(Ptr<Ipv4> ipv4, int32_t ifIndex, Ipv4Address network, Ipv4Mask mask)
{
    if (ipv4 == nullptr || ifIndex < 0)
    {
        return;
    }

    for (int32_t i = static_cast<int32_t>(ipv4->GetNAddresses(ifIndex)) - 1; i >= 0; --i)
    {
        Ipv4InterfaceAddress existing = ipv4->GetAddress(ifIndex, static_cast<uint32_t>(i));
        if (existing.GetLocal().CombineMask(mask) == network)
        {
            ipv4->RemoveAddress(ifIndex, static_cast<uint32_t>(i));
        }
    }
}

static void
ActivateMosaicUdpDedicatedBearer(Ptr<NrHelper> nrHelper, Ptr<NetDevice> ueNetDevice)
{
    if (nrHelper == nullptr || ueNetDevice == nullptr)
    {
        NS_LOG_ERROR("Cannot activate dedicated bearer: helper or UE device is null");
        return;
    }

    Ptr<EpcTft> tft = Create<EpcTft>();

    EpcTft::PacketFilter pf;
    pf.localPortStart = kProxyUdpPort;
    pf.localPortEnd = kProxyUdpPort;
    pf.remotePortStart = kProxyUdpPort;
    pf.remotePortEnd = kProxyUdpPort;
    tft->Add(pf);

    EpsBearer bearer(EpsBearer::NGBR_VIDEO_TCP_DEFAULT);

    NS_LOG_INFO("Activating dedicated EPS bearer for Mosaic UDP flow on port " << kProxyUdpPort);
    nrHelper->ActivateDedicatedEpsBearer(ueNetDevice, bearer, tft);
}

} // anonymous namespace

NS_OBJECT_ENSURE_REGISTERED(MosaicNodeManager);

TypeId
MosaicNodeManager::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::MosaicNodeManager")
        .SetParent<Object>()
        .AddConstructor<MosaicNodeManager>()
        .AddAttribute("numExtraRadioNodes",
                      "Number of spare pre-created NR UE nodes kept in a ready pool",
                      UintegerValue(100),
                      MakeUintegerAccessor(&MosaicNodeManager::m_numExtraRadioNodes),
                      MakeUintegerChecker<uint16_t>())
        .AddAttribute("nrCentralFrequencyHz",
                      "NR carrier center frequency in Hz",
                      DoubleValue(5.9e9),
                      MakeDoubleAccessor(&MosaicNodeManager::m_nrCentralFrequencyHz),
                      MakeDoubleChecker<double>())
        .AddAttribute("nrBandwidthHz",
                      "NR channel bandwidth in Hz",
                      DoubleValue(20e6),
                      MakeDoubleAccessor(&MosaicNodeManager::m_nrBandwidthHz),
                      MakeDoubleChecker<double>())
        .AddAttribute("nrNumerology",
                      "NR numerology to apply on BWP 0",
                      UintegerValue(1),
                      MakeUintegerAccessor(&MosaicNodeManager::m_nrNumerology),
                      MakeUintegerChecker<uint16_t>())
        .AddAttribute("nrTxPowerDbm",
                      "gNB NR Tx power in dBm",
                      DoubleValue(30.0),
                      MakeDoubleAccessor(&MosaicNodeManager::m_nrTxPowerDbm),
                      MakeDoubleChecker<double>())
        .AddAttribute("nrShadowingEnabled",
                      "Enable 3GPP pathloss shadowing for NR",
                      BooleanValue(false),
                      MakeBooleanAccessor(&MosaicNodeManager::m_nrShadowingEnabled),
                      MakeBooleanChecker())
        .AddAttribute("nrUeAntennaRows",
                      "Number of UE NR antenna rows",
                      UintegerValue(1),
                      MakeUintegerAccessor(&MosaicNodeManager::m_nrUeAntennaRows),
                      MakeUintegerChecker<uint16_t>())
        .AddAttribute("nrUeAntennaColumns",
                      "Number of UE NR antenna columns",
                      UintegerValue(1),
                      MakeUintegerAccessor(&MosaicNodeManager::m_nrUeAntennaColumns),
                      MakeUintegerChecker<uint16_t>())
        .AddAttribute("nrGnbAntennaRows",
                      "Number of gNB NR antenna rows",
                      UintegerValue(4),
                      MakeUintegerAccessor(&MosaicNodeManager::m_nrGnbAntennaRows),
                      MakeUintegerChecker<uint16_t>())
        .AddAttribute("nrGnbAntennaColumns",
                      "Number of gNB NR antenna columns",
                      UintegerValue(4),
                      MakeUintegerAccessor(&MosaicNodeManager::m_nrGnbAntennaColumns),
                      MakeUintegerChecker<uint16_t>());
    return tid;
}

MosaicNodeManager::MosaicNodeManager()
    : m_nrSpectrumBuilt(false),
      m_backboneAddressHelper("5.0.0.0", "255.0.0.0")
{
    m_beamformingHelper = CreateObject<IdealBeamformingHelper>();
    m_epcHelper = CreateObject<NrPointToPointEpcHelper>();
    m_nrHelper = CreateObject<NrHelper>();
    m_nrHelper->SetBeamformingHelper(m_beamformingHelper);
    m_nrHelper->SetEpcHelper(m_epcHelper);

    m_csmaHelper.SetChannelAttribute("DataRate", StringValue("100Gb/s"));
    m_csmaHelper.SetChannelAttribute("Delay", TimeValue(NanoSeconds(6560)));
}

void
MosaicNodeManager::BuildNrSpectrum()
{
    if (m_nrSpectrumBuilt)
    {
        return;
    }

    NS_LOG_INFO("Setup NR band/BWP configuration...");
    NS_LOG_INFO("NR config:"
                << " centerFreqHz=" << m_nrCentralFrequencyHz
                << " bandwidthHz=" << m_nrBandwidthHz
                << " numerology=" << m_nrNumerology
                << " shadowing=" << m_nrShadowingEnabled
                << " ueAnt=" << m_nrUeAntennaRows << "x" << m_nrUeAntennaColumns
                << " gnbAnt=" << m_nrGnbAntennaRows << "x" << m_nrGnbAntennaColumns);

    m_beamformingHelper->SetAttribute("BeamformingMethod",
                                      TypeIdValue(DirectPathBeamforming::GetTypeId()));
    m_epcHelper->SetAttribute("S1uLinkDelay", TimeValue(MilliSeconds(0)));

    m_nrHelper->SetChannelConditionModelAttribute("UpdatePeriod", TimeValue(MilliSeconds(0)));
    m_nrHelper->SetPathlossAttribute("ShadowingEnabled", BooleanValue(m_nrShadowingEnabled));

    m_nrHelper->SetGnbPhyAttribute("Numerology", UintegerValue(m_nrNumerology));
    m_nrHelper->SetGnbPhyAttribute("TxPower", DoubleValue(m_nrTxPowerDbm));

    m_nrHelper->SetUeAntennaAttribute("NumRows", UintegerValue(m_nrUeAntennaRows));
    m_nrHelper->SetUeAntennaAttribute("NumColumns", UintegerValue(m_nrUeAntennaColumns));
    m_nrHelper->SetGnbAntennaAttribute("NumRows", UintegerValue(m_nrGnbAntennaRows));
    m_nrHelper->SetGnbAntennaAttribute("NumColumns", UintegerValue(m_nrGnbAntennaColumns));

    const uint8_t numCcPerBand = 1;
    CcBwpCreator::SimpleOperationBandConf bandConf(m_nrCentralFrequencyHz,
                                                   m_nrBandwidthHz,
                                                   numCcPerBand,
                                                   BandwidthPartInfo::UMi_StreetCanyon);

    m_operationBand = m_ccBwpCreator.CreateOperationBandContiguousCc(bandConf);
    m_nrHelper->InitializeOperationBand(&m_operationBand);
    m_allBwps = CcBwpCreator::GetAllBwps({m_operationBand});

    NS_LOG_INFO("NR BWP summary: totalBwps=" << m_allBwps.size());

    m_nrSpectrumBuilt = true;
}

void
MosaicNodeManager::Configure(MosaicNs3Bridge* serverPtr)
{
    NS_LOG_INFO("Initialize Node Infrastructure...");
    m_serverPtr = serverPtr;

    BuildNrSpectrum();

    NS_LOG_INFO("Setup core...");
    Ptr<Node> pgw = m_epcHelper->GetPgwNode();
    Ptr<Node> sgw = m_epcHelper->GetSgwNode();

    NS_LOG_INFO("Setup backbone connection...");
    m_backboneNodes.Add(pgw);
    m_backboneDevices = m_csmaHelper.Install(m_backboneNodes);
    m_backboneAddressHelper.Assign(m_backboneDevices);

    NS_LOG_INFO("Configure routing...");
    Ptr<Ipv4StaticRouting> pgwStaticRouting =
        m_ipv4RoutingHelper.GetStaticRouting(pgw->GetObject<Ipv4>());
    pgwStaticRouting->AddNetworkRouteTo(Ipv4Address("10.0.0.0"), "255.0.0.0", 1);
    pgwStaticRouting->AddNetworkRouteTo(Ipv4Address("10.5.0.0"), "255.255.0.0", 3);
    pgwStaticRouting->AddNetworkRouteTo(Ipv4Address("10.6.0.0"), "255.255.0.0", 3);
}

void
MosaicNodeManager::OnStart()
{
    NS_LOG_INFO("Do the final configuration...");
    NS_LOG_INFO("Pre-created radio pool size=" << m_extraRadioNodes.GetN());

    PrintNodeConfigs(m_gnbNodes, 10);
    PrintNodeConfigs(m_backboneNodes, 10);
    PrintNodeConfigs(m_radioNodes, 10);
    PrintNodeConfigs(m_extraRadioNodes, 10);
}

void
MosaicNodeManager::OnShutdown()
{
    NS_LOG_FUNCTION(this);
    NS_LOG_DEBUG("Print IP assignment for all radioNodes");
    PrintNodeConfigs(m_radioNodes);
}

Ptr<NrUeNetDevice>
MosaicNodeManager::GetNrUeDevice(Ptr<Node> node) const
{
    for (uint32_t i = 0; i < node->GetNDevices(); ++i)
    {
        Ptr<NrUeNetDevice> dev = DynamicCast<NrUeNetDevice>(node->GetDevice(i));
        if (dev)
        {
            return dev;
        }
    }
    return nullptr;
}

Ptr<NrGnbNetDevice>
MosaicNodeManager::GetNrGnbDevice(Ptr<Node> node) const
{
    for (uint32_t i = 0; i < node->GetNDevices(); ++i)
    {
        Ptr<NrGnbNetDevice> dev = DynamicCast<NrGnbNetDevice>(node->GetDevice(i));
        if (dev)
        {
            return dev;
        }
    }
    return nullptr;
}

Ptr<NetDevice>
MosaicNodeManager::GetBackboneNetDevice(Ptr<Node> node) const
{
    for (uint32_t i = 0; i < node->GetNDevices(); ++i)
    {
        Ptr<NetDevice> dev = node->GetDevice(i);
        if (DynamicCast<CsmaNetDevice>(dev))
        {
            return dev;
        }
    }
    return nullptr;
}

void
MosaicNodeManager::PrintNodeConfigsDeviceAgnostic(NodeContainer nodes, uint32_t maxNum)
{
    for (uint32_t u = 0; u < nodes.GetN() && u < maxNum; ++u)
    {
        Ptr<Node> node = nodes.Get(u);
        Ptr<Ipv4> ipv4proto = node->GetObject<Ipv4>();

        NS_LOG_DEBUG("[node=" << node->GetId() << "]");
        for (uint32_t i = 0; i < node->GetNDevices(); i++)
        {
            Ipv4InterfaceAddress iaddr;
            Ptr<NetDevice> device = node->GetDevice(i);
            int32_t ipif = DynamicCast<Ipv4L3Protocol>(ipv4proto)->GetInterfaceForDevice(device);
            if (ipif != -1)
            {
                iaddr = ipv4proto->GetAddress(ipif, 0);
                NS_LOG_DEBUG("  if_" << i << " dev=" << device << " type="
                                     << device->GetInstanceTypeId().GetName() << " iaddr=" << iaddr);
            }
            else
            {
                NS_LOG_DEBUG("  if_" << i << " dev=" << device << " type="
                                     << device->GetInstanceTypeId().GetName());
            }
        }
    }
}

void
MosaicNodeManager::PrintNodeConfigs(NodeContainer nodes, uint32_t maxNum)
{
    for (uint32_t u = 0; u < nodes.GetN() && u < maxNum; ++u)
    {
        Ptr<Node> node = nodes.Get(u);
        Ptr<Ipv4> ipv4proto = node->GetObject<Ipv4>();

        NS_LOG_DEBUG("[node=" << node->GetId() << "]");
        for (uint32_t i = 0; i < node->GetNDevices(); i++)
        {
            Ptr<NetDevice> device = node->GetDevice(i);

            int32_t ipif = DynamicCast<Ipv4L3Protocol>(ipv4proto)->GetInterfaceForDevice(device);
            std::stringstream ipAddressString;
            if (ipif != -1)
            {
                for (uint32_t j = 0; j < ipv4proto->GetNAddresses(ipif); j++)
                {
                    Ipv4InterfaceAddress iaddr = ipv4proto->GetAddress(ipif, j);
                    ipAddressString << "|" << iaddr.GetLocal();
                }
            }

            if (DynamicCast<LoopbackNetDevice>(device))
            {
                // nop
            }
            else if (DynamicCast<CsmaNetDevice>(device))
            {
                NS_LOG_DEBUG("  if_" << i << " dev=" << device << " ETH"
                                     << " \taddr=" << ipAddressString.str());
            }
            else if (DynamicCast<PointToPointNetDevice>(device))
            {
                NS_LOG_DEBUG("  if_" << i << " dev=" << device << " P2P"
                                     << " \taddr=" << ipAddressString.str());
            }
            else if (Ptr<NrUeNetDevice> ueDev = DynamicCast<NrUeNetDevice>(device))
            {
                NS_LOG_DEBUG("  if_" << i << " dev=" << device << " NR-UE"
                                     << " \taddr=" << ipAddressString.str()
                                     << " imsi=" << ueDev->GetImsi()
                                     << " cellId=" << ueDev->GetCellId());
            }
            else if (Ptr<NrGnbNetDevice> gnbDev = DynamicCast<NrGnbNetDevice>(device))
            {
                NS_LOG_DEBUG("  if_" << i << " dev=" << device << " NR-GNB"
                                     << " \taddr=" << ipAddressString.str()
                                     << " cellId=" << gnbDev->GetCellId());
            }
            else
            {
                NS_LOG_DEBUG("  if_" << i << " dev=" << device
                                     << " type=" << device->GetInstanceTypeId().GetName()
                                     << " \taddr=" << ipAddressString.str());
            }
        }
    }
}

void
MosaicNodeManager::RejectAnyUeConnectionRequest()
{
    NS_LOG_FUNCTION(this);
    NS_LOG_WARN("-------------------- change gNB settings now");
    NS_LOG_WARN("-------------------- only accept handover algorithm triggers");
    NS_LOG_WARN("-------------------- UEs cannot recover, if connection got lost once");
    Config::SetDefault("ns3::NrGnbRrc::AdmitRrcConnectionRequest", BooleanValue(false));
    for (uint32_t i = 0; i < m_gnbDevices.GetN(); i++)
    {
        Ptr<NrGnbNetDevice> device = DynamicCast<NrGnbNetDevice>(m_gnbDevices.Get(i));
        if (device && device->GetRrc())
        {
            device->GetRrc()->SetAttribute("AdmitRrcConnectionRequest", BooleanValue(false));
        }
    }
}

uint32_t
MosaicNodeManager::GetNs3NodeId(uint32_t mosaicNodeId)
{
    if (m_mosaic2nsdrei.find(mosaicNodeId) == m_mosaic2nsdrei.end())
    {
        NS_LOG_ERROR("Node ID " << mosaicNodeId << " not found in m_mosaic2nsdrei");
        exit(1);
    }
    return m_mosaic2nsdrei[mosaicNodeId];
}

uint32_t
MosaicNodeManager::GetMosaicNodeId(uint32_t ns3NodeId)
{
    if (m_nsdrei2mosaic.find(ns3NodeId) == m_nsdrei2mosaic.end())
    {
        NS_LOG_ERROR("Node ID " << ns3NodeId << " not found in m_nsdrei2mosaic");
        exit(1);
    }
    return m_nsdrei2mosaic[ns3NodeId];
}

void
MosaicNodeManager::CreateNodeB(Vector position)
{
    BuildNrSpectrum();

    Ptr<Node> node = CreateObject<Node>();
    m_gnbNodes.Add(node);

    m_mobilityHelper.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    m_mobilityHelper.Install(node);

    NetDeviceContainer gnbDevices = m_nrHelper->InstallGnbDevice(NodeContainer(node), m_allBwps);
    Ptr<NrGnbNetDevice> gnbDev = DynamicCast<NrGnbNetDevice>(gnbDevices.Get(0));
    if (gnbDev == nullptr)
    {
        NS_LOG_ERROR("Failed to install NR gNB device");
        exit(1);
    }

    gnbDev->UpdateConfig();

    m_gnbDevices.Add(gnbDevices.Get(0));
    NS_LOG_INFO("[node=" << node->GetId() << "] Create gNodeB: dev=" << gnbDevices.Get(0));

    Ptr<MobilityModel> mobModel = node->GetObject<MobilityModel>();
    mobModel->SetPosition(position);

    uint32_t targetPoolSize = std::max<uint32_t>(1u, static_cast<uint32_t>(m_numExtraRadioNodes));
    while (m_extraRadioNodes.GetN() < targetPoolSize)
    {
        Ptr<Node> ueNode = CreateRadioNodeHelper();
        m_extraRadioNodes.Add(ueNode);
        m_isDeactivated[ueNode->GetId()] = true;
        NS_LOG_INFO("Pre-created attached radio pool node " << ueNode->GetId());
    }
}

void
MosaicNodeManager::CreateWiredNode(uint32_t mosaicNodeId)
{
    if (m_mosaic2nsdrei.find(mosaicNodeId) != m_mosaic2nsdrei.end())
    {
        NS_LOG_ERROR("Cannot create node with id=" << mosaicNodeId << " multiple times.");
        exit(1);
    }

    Ptr<Node> node = CreateObject<Node>();
    NS_LOG_INFO("Create wired node " << mosaicNodeId << "->" << node->GetId());
    m_mosaic2nsdrei[mosaicNodeId] = node->GetId();
    m_nsdrei2mosaic[node->GetId()] = mosaicNodeId;
    m_isWiredNode[node->GetId()] = true;
    m_isDeactivated[node->GetId()] = false;
    m_backboneNodes.Add(node);

    m_internetHelper.Install(node);
    Ptr<Ipv4> ipv4proto = node->GetObject<Ipv4>();

    Ptr<CsmaChannel> ch = DynamicCast<CsmaChannel>(m_backboneDevices.Get(0)->GetChannel());
    Ptr<NetDevice> device = m_csmaHelper.Install(node, ch).Get(0);
    m_backboneDevices.Add(device);
    m_backboneAddressHelper.Assign(device);
    (void)ipv4proto->GetInterfaceForDevice(device);

    Ptr<MosaicProxyApp> app = CreateObject<MosaicProxyApp>();
    app->SetRecvCallback(MakeCallback(&MosaicNodeManager::RecvCellMsg, this));
    node->AddApplication(app);
    app->SetSockets(interface_e::ETH);
}

Ptr<Node>
MosaicNodeManager::CreateRadioNodeHelper(void)
{
    BuildNrSpectrum();

    Ptr<Node> node = CreateObject<Node>();
    m_internetHelper.Install(node);
    Ptr<Ipv4> ipv4proto = node->GetObject<Ipv4>();

    m_mobilityHelper.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    m_mobilityHelper.Install(node);

    Ptr<ConstantVelocityMobilityModel> cvmm = node->GetObject<ConstantVelocityMobilityModel>();
    if (cvmm != nullptr)
    {
        cvmm->SetPosition(GetPoolParkingPosition(node->GetId()));
        cvmm->SetVelocity(Vector(0.0, 0.0, 0.0));
    }

    NetDeviceContainer nrDevices = m_nrHelper->InstallUeDevice(NodeContainer(node), m_allBwps);
    Ptr<NrUeNetDevice> ueDevice = DynamicCast<NrUeNetDevice>(nrDevices.Get(0));
    if (ueDevice == nullptr)
    {
        NS_LOG_ERROR("Failed to install NR UE device");
        exit(1);
    }

    m_epcHelper->AssignUeIpv4Address(nrDevices);

    int32_t ifIndex = ipv4proto->GetInterfaceForDevice(nrDevices.Get(0));
    Ptr<Ipv4StaticRouting> ueStaticRouting = m_ipv4RoutingHelper.GetStaticRouting(ipv4proto);
    ueStaticRouting->SetDefaultRoute(m_epcHelper->GetUeDefaultGatewayAddress(), ifIndex);

    Ptr<MosaicProxyApp> cellApp = CreateObject<MosaicProxyApp>();
    cellApp->SetRecvCallback(MakeCallback(&MosaicNodeManager::RecvCellMsg, this));
    node->AddApplication(cellApp);
    cellApp->SetSockets(interface_e::CELL);
    cellApp->Disable();

    if (m_gnbDevices.GetN() > 0)
    {
        NetDeviceContainer ueContainer;
        ueContainer.Add(ueDevice);

        m_nrHelper->AttachToClosestEnb(ueContainer, m_gnbDevices);
        ueDevice->UpdateConfig();
        ActivateMosaicUdpDedicatedBearer(m_nrHelper, ueDevice);
    }

    return node;
}

void
MosaicNodeManager::CreateRadioNode(uint32_t mosaicNodeId, Vector position)
{
    if (m_mosaic2nsdrei.find(mosaicNodeId) != m_mosaic2nsdrei.end())
    {
        NS_LOG_ERROR("Cannot create node with id=" << mosaicNodeId << " multiple times.");
        exit(1);
    }

    Ptr<Node> node;
    for (uint32_t i = 0; i < m_extraRadioNodes.GetN(); ++i)
    {
        Ptr<Node> candidate = m_extraRadioNodes.Get(i);
        if (m_nsdrei2mosaic.find(candidate->GetId()) == m_nsdrei2mosaic.end())
        {
            node = candidate;
            break;
        }
    }

    if (node == nullptr)
    {
        NS_LOG_WARN("No pre-created NR UE available; creating one on demand");
        node = CreateRadioNodeHelper();
        m_extraRadioNodes.Add(node);
        m_isDeactivated[node->GetId()] = true;
    }

    NS_LOG_INFO("Create radio node " << mosaicNodeId << "->" << node->GetId());
    m_mosaic2nsdrei[mosaicNodeId] = node->GetId();
    m_nsdrei2mosaic[node->GetId()] = mosaicNodeId;
    m_isRadioNode[node->GetId()] = true;
    m_isWiredNode[node->GetId()] = false;
    m_isCellRadioConfigured[node->GetId()] = false;
    m_isWifiRadioConfigured[node->GetId()] = false;
    m_isDeactivated[node->GetId()] = false;
    m_radioNodes.Add(node);

    UpdateNodePosition(mosaicNodeId, position);
}

void
MosaicNodeManager::ActivateRadioNode(uint32_t mosaicNodeId, Vector position)
{
    if (m_mosaic2nsdrei.find(mosaicNodeId) != m_mosaic2nsdrei.end())
    {
        NS_LOG_ERROR("Cannot create node with id=" << mosaicNodeId << " multiple times.");
        exit(1);
    }

    Ptr<Node> node;
    for (uint32_t i = 0; i < m_extraRadioNodes.GetN(); ++i)
    {
        Ptr<Node> candidate = m_extraRadioNodes.Get(i);
        if (m_nsdrei2mosaic.find(candidate->GetId()) == m_nsdrei2mosaic.end())
        {
            node = candidate;
            break;
        }
    }

    if (node == nullptr)
    {
        NS_LOG_ERROR("No available prepared radio node found. Increase numExtraRadioNodes.");
        exit(1);
    }

    NS_LOG_INFO("Activate radio node " << mosaicNodeId << "->" << node->GetId());
    m_mosaic2nsdrei[mosaicNodeId] = node->GetId();
    m_nsdrei2mosaic[node->GetId()] = mosaicNodeId;
    m_isRadioNode[node->GetId()] = true;
    m_isWiredNode[node->GetId()] = false;
    m_isCellRadioConfigured[node->GetId()] = false;
    m_isWifiRadioConfigured[node->GetId()] = false;
    m_isDeactivated[node->GetId()] = false;
    m_radioNodes.Add(node);

    UpdateNodePosition(mosaicNodeId, position);
}

void
MosaicNodeManager::UpdateNodePosition(uint32_t mosaicNodeId, Vector position)
{
    uint32_t nodeId = GetNs3NodeId(mosaicNodeId);
    if (m_isDeactivated[nodeId])
    {
        return;
    }

    Ptr<Node> node = NodeList::GetNode(nodeId);
    Ptr<MobilityModel> mobModel = node->GetObject<MobilityModel>();
    mobModel->SetPosition(position);
}

void
MosaicNodeManager::RemoveNode(uint32_t mosaicNodeId)
{
    uint32_t nodeId = GetNs3NodeId(mosaicNodeId);
    if (m_isDeactivated[nodeId])
    {
        return;
    }

    Ptr<Node> node = NodeList::GetNode(nodeId);
    bool isRadio = m_isRadioNode[nodeId];
    bool isWired = m_isWiredNode[nodeId];

    uint32_t numApps = node->GetNApplications();
    for (uint32_t i = 0; i < numApps; i++)
    {
        Ptr<MosaicProxyApp> app = DynamicCast<MosaicProxyApp>(node->GetApplication(i));
        if (!app)
        {
            NS_LOG_ERROR("No app with index=" << i << " found on node " << nodeId << " !");
            exit(1);
        }
        app->Disable();
    }

    m_isDeactivated[nodeId] = true;

    if (isRadio)
    {
        NS_LOG_INFO("Release radio node " << mosaicNodeId << "->" << nodeId << " back to pre-created pool");

        m_mosaic2nsdrei.erase(mosaicNodeId);
        m_nsdrei2mosaic.erase(nodeId);

        m_isRadioNode[nodeId] = false;
        m_isCellRadioConfigured[nodeId] = false;
        m_isWifiRadioConfigured[nodeId] = false;

        Ptr<NrUeNetDevice> ueDevice = GetNrUeDevice(node);
        if (ueDevice != nullptr)
        {
            Ptr<Ipv4> ipv4proto = node->GetObject<Ipv4>();
            int32_t ifIndex = ipv4proto->GetInterfaceForDevice(ueDevice);
            RemoveIpv4AliasesInPrefix(ipv4proto, ifIndex, Ipv4Address("10.0.0.0"), Ipv4Mask("255.0.0.0"));
        }

        Ptr<MobilityModel> mobModel = node->GetObject<MobilityModel>();
        if (mobModel != nullptr)
        {
            mobModel->SetPosition(GetPoolParkingPosition(nodeId));
        }

        Ptr<ConstantVelocityMobilityModel> cvmm = node->GetObject<ConstantVelocityMobilityModel>();
        if (cvmm != nullptr)
        {
            cvmm->SetVelocity(Vector(0.0, 0.0, 0.0));
        }

        return;
    }

    if (isWired)
    {
        NS_LOG_INFO("Remove wired node " << mosaicNodeId << "->" << nodeId);
        return;
    }

    NS_LOG_WARN("RemoveNode called on node " << nodeId << " that is neither radio nor wired");
}

void
MosaicNodeManager::ConfigureWifiRadio(uint32_t mosaicNodeId, double transmitPower, Ipv4Address ip)
{
    uint32_t nodeId = GetNs3NodeId(mosaicNodeId);
    if (m_isDeactivated[nodeId])
    {
        return;
    }
    if (m_isWifiRadioConfigured[nodeId])
    {
        NS_LOG_ERROR("Cannot configure WIFI interface multiple times. Ignoring.");
        return;
    }
    m_isWifiRadioConfigured[nodeId] = true;

    if (transmitPower > -1)
    {
        Ptr<Node> node = NodeList::GetNode(nodeId);
        Ptr<NrUeNetDevice> ueDev = GetNrUeDevice(node);
        if (ueDev)
        {
            m_nrHelper->GetUePhy(ueDev, 0)->SetAttribute("TxPower",
                                                         DoubleValue(10 * log10(transmitPower)));
        }
    }

    ConfigureCellRadio(mosaicNodeId, ip);
}

void
MosaicNodeManager::ConfigureCellRadio(uint32_t mosaicNodeId, Ipv4Address ip)
{
    uint32_t nodeId = GetNs3NodeId(mosaicNodeId);
    if (m_isDeactivated[nodeId])
    {
        return;
    }
    if (m_isCellRadioConfigured[nodeId])
    {
        NS_LOG_ERROR("Cannot configure CELL interface multiple times. Ignoring.");
        return;
    }
    m_isCellRadioConfigured[nodeId] = true;

    NS_LOG_INFO("[node=" << nodeId << "] ip=" << ip);
    Ptr<Node> node = NodeList::GetNode(nodeId);

    bool partOf10 = ip.CombineMask("255.0.0.0").Get() == Ipv4Address("10.0.0.0").Get();
    bool partOf105 = ip.CombineMask("255.255.0.0").Get() == Ipv4Address("10.5.0.0").Get();
    bool partOf106 = ip.CombineMask("255.255.0.0").Get() == Ipv4Address("10.6.0.0").Get();
    NS_ASSERT_MSG(partOf10, "The ip for all nodes must be part of 10.0.0.0/8 network.");

    if (m_isRadioNode[nodeId])
    {
        NS_ASSERT_MSG(!partOf105, "The ip for radio nodes must not be part of 10.5.0.0/16 network.");
        NS_ASSERT_MSG(!partOf106, "The ip for radio nodes must not be part of 10.6.0.0/16 network.");

        Ptr<MosaicProxyApp> cellApp = DynamicCast<MosaicProxyApp>(node->GetApplication(0));
        if (!cellApp)
        {
            NS_LOG_ERROR("No cell app found on node " << nodeId << " !");
            exit(1);
        }
        cellApp->Enable();

        Ptr<NrUeNetDevice> device = GetNrUeDevice(node);
        if (device == nullptr)
        {
            NS_LOG_ERROR("No NR UE device found on node " << nodeId);
            exit(1);
        }

        Ptr<Ipv4> ipv4proto = node->GetObject<Ipv4>();
        int32_t ifIndex = ipv4proto->GetInterfaceForDevice(device);

        EnsureIpv4Alias(ipv4proto, ifIndex, ip, Ipv4Mask("255.0.0.0"));

        NS_LOG_INFO("NR UE already pre-attached; enabling app only");
    }
    else if (m_isWiredNode[nodeId])
    {
        NS_ASSERT_MSG(partOf105 || partOf106, "The ip for wired nodes must be part of 10.5.0.0/16 or 10.6.0.0/16 network.");

        Ptr<MosaicProxyApp> csmaApp = DynamicCast<MosaicProxyApp>(node->GetApplication(0));
        if (!csmaApp)
        {
            NS_LOG_ERROR("No csma app found on node " << nodeId << " !");
            exit(1);
        }
        csmaApp->Enable();

        Ptr<NetDevice> device = GetBackboneNetDevice(node);
        if (device == nullptr)
        {
            NS_LOG_ERROR("No backbone device found on wired node " << nodeId);
            exit(1);
        }

        Ptr<Ipv4> ipv4proto = node->GetObject<Ipv4>();
        int32_t ifIndex = ipv4proto->GetInterfaceForDevice(device);

        EnsureIpv4Alias(ipv4proto, ifIndex, ip, Ipv4Mask("255.255.0.0"));

        Ptr<Ipv4StaticRouting> serverStaticRouting = m_ipv4RoutingHelper.GetStaticRouting(ipv4proto);
        serverStaticRouting->SetDefaultRoute(Ipv4Address("5.0.0.1"), ifIndex);
    }
    else
    {
        NS_LOG_ERROR("Invalid State: Node has to be either radio or wired node.");
        exit(1);
    }
}

void
MosaicNodeManager::SendWifiMsg(uint32_t mosaicNodeId,
                               Ipv4Address dstAddr,
                               ClientServerChannelSpace::RadioChannel channel,
                               uint32_t msgID,
                               uint32_t payLength)
{
    uint32_t nodeId = GetNs3NodeId(mosaicNodeId);
    if (m_isDeactivated[nodeId])
    {
        return;
    }
    if (channel != ClientServerChannelSpace::RadioChannel::PROTO_CCH)
    {
        NS_LOG_WARN("NR-only implementation ignores legacy DSRC channel selection.");
    }
    SendCellMsg(mosaicNodeId, dstAddr, msgID, payLength);
}

void
MosaicNodeManager::SendCellMsg(uint32_t mosaicNodeId, Ipv4Address dstAddr, uint32_t msgID, uint32_t payLength)
{
    uint32_t nodeId = GetNs3NodeId(mosaicNodeId);
    if (m_isDeactivated[nodeId])
    {
        return;
    }

    NS_LOG_DEBUG("[node=" << nodeId << "] dst=" << dstAddr << " msgID=" << msgID << " len=" << payLength);

    Ptr<Node> node = NodeList::GetNode(nodeId);
    Ptr<MosaicProxyApp> app;
    if (m_isRadioNode[nodeId] || m_isWiredNode[nodeId])
    {
        app = DynamicCast<MosaicProxyApp>(node->GetApplication(0));
    }
    if (app == nullptr)
    {
        NS_LOG_ERROR("Node " << nodeId << " was not initialized properly, MosaicProxyApp is missing");
        return;
    }
    app->TransmitPacket(dstAddr, msgID, payLength);
}

void
MosaicNodeManager::RecvWifiMsg(unsigned long long recvTime, uint32_t ns3NodeId, int msgID)
{
    if (m_isDeactivated[ns3NodeId])
    {
        return;
    }
    uint32_t nodeId = GetMosaicNodeId(ns3NodeId);
    m_serverPtr->writeReceiveWifiMessage(recvTime, nodeId, msgID);
}

void
MosaicNodeManager::RecvCellMsg(unsigned long long recvTime, uint32_t ns3NodeId, int msgID)
{
    if (m_isDeactivated[ns3NodeId])
    {
        return;
    }
    uint32_t nodeId = GetMosaicNodeId(ns3NodeId);
    m_serverPtr->writeReceiveCellMessage(recvTime, nodeId, msgID);
}

} // namespace ns3