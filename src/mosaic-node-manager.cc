#include "mosaic-node-manager.h"

#include <cmath>
#include <sstream>
#include <utility>

#include "ns3/csma-net-device.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/loopback-net-device.h"
#include "ns3/node-list.h"
#include "ns3/output-stream-wrapper.h"
#include "ns3/point-to-point-net-device.h"

#include "mosaic-ns3-bridge.h"
#include "mosaic-proxy-app.h"

NS_LOG_COMPONENT_DEFINE ("MosaicNodeManager");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (MosaicNodeManager);

TypeId
MosaicNodeManager::GetTypeId (void)
{
  static TypeId tid =
    TypeId ("ns3::MosaicNodeManager")
      .SetParent<Object> ()
      .AddConstructor<MosaicNodeManager> ()
      .AddAttribute ("numExtraRadioNodes",
                     "Number of extra spare radio nodes, usable after simulation started",
                     UintegerValue (10),
                     MakeUintegerAccessor (&MosaicNodeManager::m_numExtraRadioNodes),
                     MakeUintegerChecker<uint16_t> ());
  return tid;
}

MosaicNodeManager::MosaicNodeManager ()
  : m_backboneAddressHelper ("5.0.0.0", "255.0.0.0"),
    m_radioAddressHelper ("6.0.0.0", "255.0.0.0", "0.0.0.2"),
    m_centralFrequencyHz (5.9e9),
    m_channelBandwidthHz (20e6),
    m_defaultNumerology (0),
    m_defaultGnbTxPowerDbm (30.0)
{
  // Wired backbone.
  m_csmaHelper.SetChannelAttribute ("DataRate", StringValue ("100Gb/s"));
  m_csmaHelper.SetChannelAttribute ("Delay", TimeValue (NanoSeconds (6560)));

  // 5G-LENA NR helper stack.
  m_epcHelper = CreateObject<NrPointToPointEpcHelper> ();
  m_beamformingHelper = CreateObject<IdealBeamformingHelper> ();
  m_nrHelper = CreateObject<NrHelper> ();
  m_nrHelper->SetBeamformingHelper (m_beamformingHelper);
  m_nrHelper->SetEpcHelper (m_epcHelper);

  // Align with the simple 5G test style the user shared: no shadowing and no
  // channel update period to keep the initial integration deterministic.
  Config::SetDefault ("ns3::ThreeGppChannelModel::UpdatePeriod",
                      TimeValue (MilliSeconds (0)));
  m_nrHelper->SetChannelConditionModelAttribute ("UpdatePeriod",
                                                 TimeValue (MilliSeconds (0)));
  m_nrHelper->SetPathlossAttribute ("ShadowingEnabled", BooleanValue (false));

  // Antenna defaults similar to the user's test harness.
  m_nrHelper->SetUeAntennaAttribute ("NumRows", UintegerValue (2));
  m_nrHelper->SetUeAntennaAttribute ("NumColumns", UintegerValue (4));
  m_nrHelper->SetUeAntennaAttribute (
    "AntennaElement",
    PointerValue (CreateObject<IsotropicAntennaModel> ()));

  m_nrHelper->SetGnbAntennaAttribute ("NumRows", UintegerValue (4));
  m_nrHelper->SetGnbAntennaAttribute ("NumColumns", UintegerValue (8));
  m_nrHelper->SetGnbAntennaAttribute (
    "AntennaElement",
    PointerValue (CreateObject<IsotropicAntennaModel> ()));

  m_epcHelper->SetAttribute ("S1uLinkDelay", TimeValue (MilliSeconds (0)));

  // Single-band default NR operating band. This is intentionally simple so the
  // class is usable as a clean base. More advanced multi-band/BWP config can be
  // layered on later.
  CcBwpCreator ccBwpCreator;
  CcBwpCreator::SimpleOperationBandConf bandConf (m_centralFrequencyHz,
                                                  m_channelBandwidthHz,
                                                  1,
                                                  BandwidthPartInfo::UMi_StreetCanyon);
  OperationBandInfo band = ccBwpCreator.CreateOperationBandContiguousCc (bandConf);
  m_nrHelper->InitializeOperationBand (&band);
  m_allBwps = CcBwpCreator::GetAllBwps ({band});
}

void
MosaicNodeManager::Configure (MosaicNs3Bridge *serverPtr)
{
  NS_LOG_INFO ("Initialize Node Infrastructure (backbone + NR core)...");
  m_serverPtr = serverPtr;

  Ptr<Node> pgw = m_epcHelper->GetPgwNode ();
  Ptr<Node> sgw = m_epcHelper->GetSgwNode ();

  if (m_backboneDevices.GetN () == 0)
    {
      NS_LOG_INFO ("Setup backbone connection...");
      m_backboneNodes.Add (pgw);
      m_backboneDevices = m_csmaHelper.Install (m_backboneNodes);
      m_backboneAddressHelper.Assign (m_backboneDevices);

      NS_LOG_INFO ("Configure routing for PGW...");
      Ptr<Ipv4StaticRouting> pgwStaticRouting =
        m_ipv4RoutingHelper.GetStaticRouting (pgw->GetObject<Ipv4> ());
      // Devices are typically 0:Loopback 1:TunDevice 2:SGW 3:Backbone.
      pgwStaticRouting->AddNetworkRouteTo (Ipv4Address ("10.0.0.0"),
                                           "255.0.0.0",
                                           1);
      pgwStaticRouting->AddNetworkRouteTo (Ipv4Address ("10.5.0.0"),
                                           "255.255.0.0",
                                           3);
      pgwStaticRouting->AddNetworkRouteTo (Ipv4Address ("10.6.0.0"),
                                           "255.255.0.0",
                                           3);
    }

  NS_LOG_DEBUG ("[node=" << pgw->GetId () << "] PGW");
  for (uint32_t i = 0; i < pgw->GetObject<Ipv4> ()->GetNInterfaces (); ++i)
    {
      Ipv4InterfaceAddress iaddr = pgw->GetObject<Ipv4> ()->GetAddress (i, 0);
      NS_LOG_DEBUG (" if_" << i << " dev=" << pgw->GetDevice (i) << " iaddr=" << iaddr);
    }

  if (sgw != nullptr)
    {
      NS_LOG_DEBUG ("[node=" << sgw->GetId () << "] SGW");
      for (uint32_t i = 0; i < sgw->GetObject<Ipv4> ()->GetNInterfaces (); ++i)
        {
          Ipv4InterfaceAddress iaddr = sgw->GetObject<Ipv4> ()->GetAddress (i, 0);
          NS_LOG_DEBUG (" if_" << i << " dev=" << sgw->GetDevice (i) << " iaddr=" << iaddr);
        }
    }
}

void
MosaicNodeManager::OnStart ()
{
  NS_LOG_INFO ("Do the final configuration...");

  if (m_enbNodes.GetN () > 1)
    {
      m_nrHelper->AddX2Interface (m_enbNodes);
    }

  NS_LOG_INFO ("Setup extra radioNode's...");
  for (uint32_t i = 0; i < m_numExtraRadioNodes; ++i)
    {
      Ptr<Node> node = CreateRadioNodeHelper ();
      m_extraRadioNodes.Add (node);
    }

  PrintNodeConfigs (m_enbNodes, 10);
  PrintNodeConfigs (m_backboneNodes, 10);
  PrintNodeConfigs (m_radioNodes, 10);
  PrintNodeConfigs (m_extraRadioNodes, 10);
}

void
MosaicNodeManager::OnShutdown ()
{
  NS_LOG_FUNCTION (this);
  NS_LOG_DEBUG ("Print IP assignment for all radioNodes");
  PrintNodeConfigs (m_radioNodes);
}

void
MosaicNodeManager::PrintNodeConfigsDeviceAgnostic (NodeContainer nodes, uint32_t maxNum)
{
  for (uint32_t u = 0; u < nodes.GetN () && u < maxNum; ++u)
    {
      Ptr<Node> node = nodes.Get (u);
      Ptr<Ipv4> ipv4proto = node->GetObject<Ipv4> ();

      NS_LOG_DEBUG ("[node=" << node->GetId () << "]");
      for (uint32_t i = 0; i < node->GetNDevices (); ++i)
        {
          Ipv4InterfaceAddress iaddr;
          Ptr<NetDevice> device = node->GetDevice (i);
          int32_t ipif =
            DynamicCast<Ipv4L3Protocol> (ipv4proto)->GetInterfaceForDevice (device);
          if (ipif != -1)
            {
              iaddr = ipv4proto->GetAddress (ipif, 0);
              NS_LOG_DEBUG (" if_" << i << " dev=" << device
                                    << " type=" << device->GetInstanceTypeId ().GetName ()
                                    << " iaddr=" << iaddr);
            }
          else
            {
              NS_LOG_DEBUG (" if_" << i << " dev=" << device
                                    << " type=" << device->GetInstanceTypeId ().GetName ());
            }
        }
    }
}

void
MosaicNodeManager::PrintNodeConfigs (NodeContainer nodes, uint32_t maxNum)
{
  for (uint32_t u = 0; u < nodes.GetN () && u < maxNum; ++u)
    {
      Ptr<Node> node = nodes.Get (u);
      Ptr<Ipv4> ipv4proto = node->GetObject<Ipv4> ();

      NS_LOG_DEBUG ("[node=" << node->GetId () << "]");
      for (uint32_t i = 0; i < node->GetNDevices (); ++i)
        {
          Ptr<NetDevice> device = node->GetDevice (i);
          int32_t ipif =
            DynamicCast<Ipv4L3Protocol> (ipv4proto)->GetInterfaceForDevice (device);
          std::stringstream ipAddressString;
          if (ipif != -1)
            {
              for (uint32_t j = 0; j < ipv4proto->GetNAddresses (ipif); ++j)
                {
                  Ipv4InterfaceAddress iaddr = ipv4proto->GetAddress (ipif, j);
                  ipAddressString << "|" << iaddr.GetLocal ();
                }
            }

          NS_LOG_DEBUG (" if_" << i << " dev=" << device
                                << " type=" << device->GetInstanceTypeId ().GetName ()
                                << " \taddr=" << ipAddressString.str ());
        }
    }

  if (nodes.GetN () > 0)
    {
      std::stringstream ss;
      nodes.Get (0)->GetObject<Ipv4> ()->GetRoutingProtocol ()->PrintRoutingTable (
        Create<OutputStreamWrapper> (&ss));
      NS_LOG_LOGIC (ss.str ());
    }
}

void
MosaicNodeManager::RejectAnyUeConnectionRequest ()
{
  NS_LOG_WARN ("RejectAnyUeConnectionRequest is not wired for NR RRC admission control in this version.");
}

uint32_t
MosaicNodeManager::GetNs3NodeId (uint32_t mosaicNodeId)
{
  auto it = m_mosaic2nsdrei.find (mosaicNodeId);
  if (it == m_mosaic2nsdrei.end ())
    {
      NS_LOG_ERROR ("Node ID " << mosaicNodeId << " not found in m_mosaic2nsdrei");
      std::exit (1);
    }
  return it->second;
}

uint32_t
MosaicNodeManager::GetMosaicNodeId (uint32_t ns3NodeId)
{
  auto it = m_nsdrei2mosaic.find (ns3NodeId);
  if (it == m_nsdrei2mosaic.end ())
    {
      NS_LOG_ERROR ("Node ID " << ns3NodeId << " not found in m_nsdrei2mosaic");
      std::exit (1);
    }
  return it->second;
}

void
MosaicNodeManager::CreateNodeB (Vector position)
{
  Ptr<Node> node = CreateObject<Node> ();
  m_enbNodes.Add (node);

  m_mobilityHelper.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  m_mobilityHelper.Install (node);

  NetDeviceContainer gnbDevs = m_nrHelper->InstallGnbDevice (NodeContainer (node), m_allBwps);
  Ptr<NetDevice> gnbDev = gnbDevs.Get (0);
  m_enbDevices.Add (gnbDev);

  for (uint32_t bwp = 0; bwp < NrHelper::GetNumberBwp (gnbDev); ++bwp)
    {
      Ptr<NrGnbPhy> phy = NrHelper::GetGnbPhy (gnbDev, bwp);
      if (phy != nullptr)
        {
          phy->SetAttribute ("Numerology", UintegerValue (m_defaultNumerology));
          phy->SetAttribute ("TxPower", DoubleValue (m_defaultGnbTxPowerDbm));
        }
    }

  Ptr<NrGnbNetDevice> nrGnbDev = DynamicCast<NrGnbNetDevice> (gnbDev);
  if (nrGnbDev != nullptr)
    {
      nrGnbDev->UpdateConfig ();
    }

  Ptr<MobilityModel> mobModel = node->GetObject<MobilityModel> ();
  mobModel->SetPosition (position);

  NS_LOG_INFO ("[node=" << node->GetId () << "] Create gNodeB (NR): dev=" << gnbDev);
}

void
MosaicNodeManager::CreateWiredNode (uint32_t mosaicNodeId)
{
  if (m_mosaic2nsdrei.find (mosaicNodeId) != m_mosaic2nsdrei.end ())
    {
      NS_LOG_ERROR ("Cannot create node with id=" << mosaicNodeId << " multiple times.");
      std::exit (1);
    }

  Ptr<Node> node = CreateObject<Node> ();
  NS_LOG_INFO ("Create wired node " << mosaicNodeId << "->" << node->GetId ());
  m_mosaic2nsdrei[mosaicNodeId] = node->GetId ();
  m_nsdrei2mosaic[node->GetId ()] = mosaicNodeId;
  m_isWiredNode[node->GetId ()] = true;
  m_backboneNodes.Add (node);

  m_internetHelper.Install (node);
  Ptr<Ipv4> ipv4proto = node->GetObject<Ipv4> ();

  Ptr<CsmaChannel> ch = DynamicCast<CsmaChannel> (m_backboneDevices.Get (0)->GetChannel ());
  Ptr<NetDevice> device = m_csmaHelper.Install (node, ch).Get (0);
  m_backboneDevices.Add (device);
  m_backboneAddressHelper.Assign (device);
  (void) ipv4proto->GetInterfaceForDevice (device);

  Ptr<MosaicProxyApp> app = CreateObject<MosaicProxyApp> ();
  app->SetRecvCallback (MakeCallback (&MosaicNodeManager::RecvCellMsg, this));
  node->AddApplication (app);
  app->SetSockets (interface_e::ETH);
  m_cellApp[node->GetId ()] = app;
}

Ptr<Node>
MosaicNodeManager::CreateRadioNodeHelper (void)
{
  Ptr<Node> node = CreateObject<Node> ();

  m_internetHelper.Install (node);
  Ptr<Ipv4> ipv4proto = node->GetObject<Ipv4> ();

  m_mobilityHelper.SetMobilityModel ("ns3::ConstantVelocityMobilityModel");
  m_mobilityHelper.Install (node);

  NetDeviceContainer ueDevs = m_nrHelper->InstallUeDevice (NodeContainer (node), m_allBwps);
  Ptr<NetDevice> radioDev = ueDevs.Get (0);
  m_radioDevice[node->GetId ()] = radioDev;

  m_epcHelper->AssignUeIpv4Address (ueDevs);

  int32_t ifIndex = ipv4proto->GetInterfaceForDevice (radioDev);
  if (ifIndex >= 0)
    {
      Ptr<Ipv4StaticRouting> ueStaticRouting =
        m_ipv4RoutingHelper.GetStaticRouting (ipv4proto);
      ueStaticRouting->SetDefaultRoute (m_epcHelper->GetUeDefaultGatewayAddress (), ifIndex);
    }

  Ptr<NrUeNetDevice> nrUeDev = DynamicCast<NrUeNetDevice> (radioDev);
  if (nrUeDev != nullptr)
    {
      nrUeDev->UpdateConfig ();
    }

  Ptr<MosaicProxyApp> radioApp = CreateObject<MosaicProxyApp> ();
  radioApp->SetRecvCallback (MakeCallback (&MosaicNodeManager::RecvWifiMsg, this));
  node->AddApplication (radioApp);
  radioApp->SetSockets (interface_e::CELL);
  m_radioApp[node->GetId ()] = radioApp;

  Ptr<MosaicProxyApp> cellApp = CreateObject<MosaicProxyApp> ();
  cellApp->SetRecvCallback (MakeCallback (&MosaicNodeManager::RecvCellMsg, this));
  node->AddApplication (cellApp);
  cellApp->SetSockets (interface_e::CELL);
  m_cellApp[node->GetId ()] = cellApp;

  NS_LOG_INFO ("[node=" << node->GetId () << "] Create radio node (NR UE): dev=" << radioDev);
  return node;
}

void
MosaicNodeManager::CreateRadioNode (uint32_t mosaicNodeId, Vector position)
{
  if (m_mosaic2nsdrei.find (mosaicNodeId) != m_mosaic2nsdrei.end ())
    {
      NS_LOG_ERROR ("Cannot create node with id=" << mosaicNodeId << " multiple times.");
      std::exit (1);
    }

  Ptr<Node> node = CreateRadioNodeHelper ();

  NS_LOG_INFO ("Create radio node " << mosaicNodeId << "->" << node->GetId ());
  m_mosaic2nsdrei[mosaicNodeId] = node->GetId ();
  m_nsdrei2mosaic[node->GetId ()] = mosaicNodeId;
  m_isRadioNode[node->GetId ()] = true;
  m_radioNodes.Add (node);

  UpdateNodePosition (mosaicNodeId, position);
}

void
MosaicNodeManager::ActivateRadioNode (uint32_t mosaicNodeId, Vector position)
{
  if (m_mosaic2nsdrei.find (mosaicNodeId) != m_mosaic2nsdrei.end ())
    {
      NS_LOG_ERROR ("Cannot create node with id=" << mosaicNodeId << " multiple times.");
      std::exit (1);
    }

  Ptr<Node> node;
  for (uint32_t i = 0; i < m_extraRadioNodes.GetN (); ++i)
    {
      node = m_extraRadioNodes.Get (i);
      if (m_nsdrei2mosaic.find (node->GetId ()) == m_nsdrei2mosaic.end ())
        {
          NS_LOG_INFO ("Activate radio node " << mosaicNodeId << "->" << node->GetId ());
          m_mosaic2nsdrei[mosaicNodeId] = node->GetId ();
          m_nsdrei2mosaic[node->GetId ()] = mosaicNodeId;
          m_isRadioNode[node->GetId ()] = true;
          m_isDeactivated[node->GetId ()] = false;
          m_radioNodes.Add (node);
          break;
        }
    }

  if (m_mosaic2nsdrei.find (mosaicNodeId) == m_mosaic2nsdrei.end ())
    {
      NS_LOG_ERROR ("No available node found. Increase number of extra radio nodes!");
      std::exit (1);
    }

  UpdateNodePosition (mosaicNodeId, position);
}

void
MosaicNodeManager::UpdateNodePosition (uint32_t mosaicNodeId, Vector position)
{
  uint32_t nodeId = GetNs3NodeId (mosaicNodeId);
  if (m_isDeactivated[nodeId])
    {
      return;
    }

  Ptr<Node> node = NodeList::GetNode (nodeId);
  Ptr<MobilityModel> mobModel = node->GetObject<MobilityModel> ();
  mobModel->SetPosition (position);
}

void
MosaicNodeManager::RemoveNode (uint32_t mosaicNodeId)
{
  uint32_t nodeId = GetNs3NodeId (mosaicNodeId);
  if (m_isDeactivated[nodeId])
    {
      return;
    }

  auto itRadio = m_radioApp.find (nodeId);
  if (itRadio != m_radioApp.end ())
    {
      Ptr<MosaicProxyApp> app = DynamicCast<MosaicProxyApp> (itRadio->second);
      if (app)
        {
          app->Disable ();
        }
    }

  auto itCell = m_cellApp.find (nodeId);
  if (itCell != m_cellApp.end ())
    {
      Ptr<MosaicProxyApp> app = DynamicCast<MosaicProxyApp> (itCell->second);
      if (app)
        {
          app->Disable ();
        }
    }

  m_isDeactivated[nodeId] = true;
}

bool
MosaicNodeManager::HasIpv4Address (Ptr<Ipv4> ipv4, int32_t ifIndex, Ipv4Address ip) const
{
  if (ipv4 == nullptr || ifIndex < 0)
    {
      return false;
    }

  for (uint32_t j = 0; j < ipv4->GetNAddresses (ifIndex); ++j)
    {
      if (ipv4->GetAddress (ifIndex, j).GetLocal () == ip)
        {
          return true;
        }
    }
  return false;
}

Ptr<CsmaNetDevice>
MosaicNodeManager::FindFirstCsmaDevice (Ptr<Node> node) const
{
  for (uint32_t i = 0; i < node->GetNDevices (); ++i)
    {
      Ptr<CsmaNetDevice> csmaDev = DynamicCast<CsmaNetDevice> (node->GetDevice (i));
      if (csmaDev != nullptr)
        {
          return csmaDev;
        }
    }
  return nullptr;
}

void
MosaicNodeManager::ConfigureRadioBearer (uint32_t mosaicNodeId,
                                         double transmitPower,
                                         Ipv4Address ip)
{
  uint32_t nodeId = GetNs3NodeId (mosaicNodeId);
  NS_ASSERT_MSG (m_isRadioNode[nodeId],
                 "ConfigureRadioBearer: node must be a radio node.");

  Ptr<Node> node = NodeList::GetNode (nodeId);
  Ptr<Ipv4> ipv4proto = node->GetObject<Ipv4> ();

  Ptr<NetDevice> radioDev;
  auto itDev = m_radioDevice.find (nodeId);
  if (itDev != m_radioDevice.end ())
    {
      radioDev = itDev->second;
    }

  if (radioDev == nullptr)
    {
      NS_LOG_ERROR ("ConfigureRadioBearer: radio device missing on node " << nodeId);
      std::exit (1);
    }

  int32_t ifIndex = ipv4proto->GetInterfaceForDevice (radioDev);
  if (ifIndex < 0)
    {
      NS_LOG_ERROR ("ConfigureRadioBearer: no IPv4 interface found for radio device on node "
                    << nodeId);
      std::exit (1);
    }

  // Preserve the old logical WiFi IP if requested by MOSAIC, but only as an
  // extra address on the same NR-backed UE interface.
  if (!HasIpv4Address (ipv4proto, ifIndex, ip))
    {
      Ipv4InterfaceAddress ipv4Addr (ip, "255.0.0.0");
      ipv4proto->AddAddress (ifIndex, ipv4Addr);
    }

  // Preserve historical semantics: DSRC code treated transmitPower as linear
  // power and converted it to dBm. Apply the same mapping to UE PHY if positive.
  if (transmitPower > 0.0)
    {
      double txPowerDbm = 10.0 * std::log10 (transmitPower);
      for (uint32_t bwp = 0; bwp < NrHelper::GetNumberBwp (radioDev); ++bwp)
        {
          Ptr<NrUePhy> phy = NrHelper::GetUePhy (radioDev, bwp);
          if (phy != nullptr)
            {
              phy->SetAttribute ("TxPower", DoubleValue (txPowerDbm));
            }
        }
    }

  std::stringstream ss;
  for (uint32_t j = 0; j < ipv4proto->GetNAddresses (ifIndex); ++j)
    {
      Ipv4InterfaceAddress iaddr = ipv4proto->GetAddress (ifIndex, j);
      ss << "|" << iaddr.GetLocal ();
    }
  NS_LOG_DEBUG ("[node=" << node->GetId () << "] radio bearer addr=" << ss.str ());
}

void
MosaicNodeManager::ConfigureWifiRadio (uint32_t mosaicNodeId,
                                       double transmitPower,
                                       Ipv4Address ip)
{
  uint32_t nodeId = GetNs3NodeId (mosaicNodeId);
  if (m_isDeactivated[nodeId])
    {
      return;
    }
  if (m_isWifiRadioConfigured[nodeId])
    {
      NS_LOG_ERROR ("Cannot configure WIFI (radio) interface multiple times. Ignoring.");
      return;
    }
  m_isWifiRadioConfigured[nodeId] = true;

  auto itRadio = m_radioApp.find (nodeId);
  if (itRadio == m_radioApp.end ())
    {
      NS_LOG_ERROR ("No radio app found on node " << nodeId << " !");
      std::exit (1);
    }
  Ptr<MosaicProxyApp> radioApp = DynamicCast<MosaicProxyApp> (itRadio->second);
  if (!radioApp)
    {
      NS_LOG_ERROR ("Radio app on node " << nodeId << " has wrong type!");
      std::exit (1);
    }
  radioApp->Enable ();

  ConfigureRadioBearer (mosaicNodeId, transmitPower, ip);
}

void
MosaicNodeManager::ConfigureCellRadio (uint32_t mosaicNodeId, Ipv4Address ip)
{
  uint32_t nodeId = GetNs3NodeId (mosaicNodeId);
  if (m_isDeactivated[nodeId])
    {
      return;
    }
  if (m_isCellRadioConfigured[nodeId])
    {
      NS_LOG_ERROR ("Cannot configure CELL interface multiple times. Ignoring.");
      return;
    }
  m_isCellRadioConfigured[nodeId] = true;

  NS_LOG_INFO ("ConfigureCellRadio: [node=" << nodeId << "] ip=" << ip);

  bool partOf10 =
    ip.CombineMask ("255.0.0.0").Get () == Ipv4Address ("10.0.0.0").Get ();
  bool partOf105 =
    ip.CombineMask ("255.255.0.0").Get () == Ipv4Address ("10.5.0.0").Get ();
  bool partOf106 =
    ip.CombineMask ("255.255.0.0").Get () == Ipv4Address ("10.6.0.0").Get ();
  NS_ASSERT_MSG (partOf10, "The ip for all nodes must be part of 10.0.0.0/8 network.");

  if (m_isRadioNode[nodeId])
    {
      NS_ASSERT_MSG (!partOf105,
                     "The ip for radio nodes must not be part of 10.5.0.0/16 network.");
      NS_ASSERT_MSG (!partOf106,
                     "The ip for radio nodes must not be part of 10.6.0.0/16 network.");

      auto itCell = m_cellApp.find (nodeId);
      if (itCell == m_cellApp.end ())
        {
          NS_LOG_ERROR ("No cell app found on radio node " << nodeId << " !");
          std::exit (1);
        }
      Ptr<MosaicProxyApp> cellApp = DynamicCast<MosaicProxyApp> (itCell->second);
      if (!cellApp)
        {
          NS_LOG_ERROR ("Cell app on node " << nodeId << " has wrong type!");
          std::exit (1);
        }
      cellApp->Enable ();

      Ptr<NetDevice> radioDev = m_radioDevice[nodeId];
      Ptr<Node> node = NodeList::GetNode (nodeId);
      Ptr<Ipv4> ipv4proto = node->GetObject<Ipv4> ();
      int32_t ifIndex = ipv4proto->GetInterfaceForDevice (radioDev);
      if (ifIndex >= 0 && !HasIpv4Address (ipv4proto, ifIndex, ip))
        {
          ipv4proto->AddAddress (ifIndex, Ipv4InterfaceAddress (ip, "255.0.0.0"));
        }

      if (!m_isRadioAttached[nodeId])
        {
          NetDeviceContainer ueDevs;
          ueDevs.Add (radioDev);
          m_nrHelper->AttachToClosestGnb (ueDevs, m_enbDevices);
          m_isRadioAttached[nodeId] = true;
        }
    }
  else if (m_isWiredNode[nodeId])
    {
      NS_ASSERT_MSG (partOf105 || partOf106,
                     "The ip for wired nodes must be part of 10.5.0.0/16 or 10.6.0.0/16.");

      auto itCell = m_cellApp.find (nodeId);
      if (itCell == m_cellApp.end ())
        {
          NS_LOG_ERROR ("No cell/backbone app found on wired node " << nodeId << " !");
          std::exit (1);
        }
      Ptr<MosaicProxyApp> csmaApp = DynamicCast<MosaicProxyApp> (itCell->second);
      if (!csmaApp)
        {
          NS_LOG_ERROR ("Backbone app on node " << nodeId << " has wrong type!");
          std::exit (1);
        }
      csmaApp->Enable ();

      Ptr<Node> node = NodeList::GetNode (nodeId);
      Ptr<Ipv4> ipv4proto = node->GetObject<Ipv4> ();
      Ptr<CsmaNetDevice> csmaDev = FindFirstCsmaDevice (node);
      if (csmaDev != nullptr)
        {
          int32_t ifIndex = ipv4proto->GetInterfaceForDevice (csmaDev);
          if (ifIndex >= 0 && !HasIpv4Address (ipv4proto, ifIndex, ip))
            {
              ipv4proto->AddAddress (ifIndex, Ipv4InterfaceAddress (ip, "255.255.0.0"));
            }

          Ptr<Ipv4StaticRouting> serverStaticRouting =
            m_ipv4RoutingHelper.GetStaticRouting (ipv4proto);
          serverStaticRouting->SetDefaultRoute (Ipv4Address ("5.0.0.1"), ifIndex);
        }
    }
  else
    {
      NS_LOG_ERROR ("Invalid State: Node has to be either radio or wired node.");
      std::exit (1);
    }
}

void
MosaicNodeManager::SendRadioMsg (uint32_t mosaicNodeId,
                                 Ipv4Address dstAddr,
                                 ClientServerChannelSpace::RadioChannel channel,
                                 uint32_t msgID,
                                 uint32_t payLength)
{
  uint32_t nodeId = GetNs3NodeId (mosaicNodeId);
  if (channel != ClientServerChannelSpace::RadioChannel::PROTO_CCH)
    {
      NS_LOG_ERROR ("NR path currently supports only PROTO_CCH as radio channel.");
      std::exit (1);
    }

  NS_ASSERT_MSG (m_isRadioNode[nodeId],
                 "Cannot use radio communication on wired nodes.");

  auto itRadio = m_radioApp.find (nodeId);
  if (itRadio == m_radioApp.end ())
    {
      NS_LOG_ERROR ("Radio app not found on node " << nodeId);
      return;
    }

  Ptr<MosaicProxyApp> app = DynamicCast<MosaicProxyApp> (itRadio->second);
  if (!app)
    {
      NS_LOG_ERROR ("Radio app on node " << nodeId << " has wrong type!");
      return;
    }
  app->TransmitPacket (dstAddr, msgID, payLength);
}

void
MosaicNodeManager::SendWifiMsg (uint32_t mosaicNodeId,
                                Ipv4Address dstAddr,
                                ClientServerChannelSpace::RadioChannel channel,
                                uint32_t msgID,
                                uint32_t payLength)
{
  uint32_t nodeId = GetNs3NodeId (mosaicNodeId);
  if (m_isDeactivated[nodeId])
    {
      return;
    }

  SendRadioMsg (mosaicNodeId, dstAddr, channel, msgID, payLength);
}

void
MosaicNodeManager::SendCellMsg (uint32_t mosaicNodeId,
                                Ipv4Address dstAddr,
                                uint32_t msgID,
                                uint32_t payLength)
{
  uint32_t nodeId = GetNs3NodeId (mosaicNodeId);
  if (m_isDeactivated[nodeId])
    {
      return;
    }

  auto itApp = m_cellApp.find (nodeId);
  if (itApp == m_cellApp.end ())
    {
      NS_LOG_ERROR ("Cell/backbone app not found on node " << nodeId);
      return;
    }

  Ptr<MosaicProxyApp> app = DynamicCast<MosaicProxyApp> (itApp->second);
  if (!app)
    {
      NS_LOG_ERROR ("Cell/backbone app on node " << nodeId << " has wrong type!");
      return;
    }
  app->TransmitPacket (dstAddr, msgID, payLength);
}

void
MosaicNodeManager::RecvWifiMsg (unsigned long long recvTime,
                                uint32_t ns3NodeId,
                                int msgID)
{
  if (m_isDeactivated[ns3NodeId])
    {
      return;
    }
  uint32_t nodeId = GetMosaicNodeId (ns3NodeId);
  m_serverPtr->writeReceiveWifiMessage (recvTime, nodeId, msgID);
}

void
MosaicNodeManager::RecvCellMsg (unsigned long long recvTime,
                                uint32_t ns3NodeId,
                                int msgID)
{
  if (m_isDeactivated[ns3NodeId])
    {
      return;
    }
  uint32_t nodeId = GetMosaicNodeId (ns3NodeId);
  m_serverPtr->writeReceiveCellMessage (recvTime, nodeId, msgID);
}

} // namespace ns3
