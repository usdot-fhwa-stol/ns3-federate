#include "mosaic-node-manager.h"

#include <cstdlib>
#include <sstream>

#include "ns3/constant-velocity-mobility-model.h"
#include "ns3/csma-net-device.h"
#include "ns3/loopback-net-device.h"
#include "ns3/node-list.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/simulator.h"

#include "mosaic-ns3-bridge.h"
#include "mosaic-proxy-app.h"

NS_LOG_COMPONENT_DEFINE("MosaicNodeManager");

namespace {

bool
IsTimingDebugEnabledNodeManager()
{
    static const bool enabled = [] {
        const char* env = std::getenv("MOSAIC_TIMING_DEBUG");
        return env != nullptr && std::string(env) == "1";
    }();
    return enabled;
}

void
TimingDebugNodeManager(const std::string& msg)
{
    if (IsTimingDebugEnabledNodeManager())
    {
        std::cout << "[TIMING][NodeManager] " << msg << std::endl;
    }
}

} // namespace

namespace ns3
{

namespace
{
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

} // anonymous namespace

NS_OBJECT_ENSURE_REGISTERED(MosaicNodeManager);

TypeId
MosaicNodeManager::GetTypeId(void)
{
    static TypeId tid =
        TypeId("ns3::MosaicNodeManager")
            .SetParent<Object>()
            .AddConstructor<MosaicNodeManager>()
            .AddAttribute("numExtraRadioNodes",
                          "Number of spare pre-created NR UE nodes kept in a ready pool",
                          UintegerValue(20),
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
                          "NR numerology to apply on SL BWP 0",
                          UintegerValue(1),
                          MakeUintegerAccessor(&MosaicNodeManager::m_nrNumerology),
                          MakeUintegerChecker<uint16_t>())
            .AddAttribute("nrTxPowerDbm",
                          "UE NR Tx power in dBm",
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
            .AddAttribute("slBwpId",
                          "Sidelink BWP id",
                          UintegerValue(0),
                          MakeUintegerAccessor(&MosaicNodeManager::m_slBwpId),
                          MakeUintegerChecker<uint8_t>())
            .AddAttribute("slSensingWindowMs",
                          "Sidelink sensing window in ms",
                          UintegerValue(100),
                          MakeUintegerAccessor(&MosaicNodeManager::m_slSensingWindowMs),
                          MakeUintegerChecker<uint16_t>())
            .AddAttribute("slSelectionWindow",
                          "Sidelink selection window in slots",
                          UintegerValue(5),
                          MakeUintegerAccessor(&MosaicNodeManager::m_slSelectionWindow),
                          MakeUintegerChecker<uint16_t>())
            .AddAttribute("slFreqResourcePscch",
                          "Sidelink PSCCH RBs",
                          UintegerValue(10),
                          MakeUintegerAccessor(&MosaicNodeManager::m_slFreqResourcePscch),
                          MakeUintegerChecker<uint16_t>())
            .AddAttribute("slSubchannelSize",
                          "Sidelink subchannel size in RBs",
                          UintegerValue(10),
                          MakeUintegerAccessor(&MosaicNodeManager::m_slSubchannelSize),
                          MakeUintegerChecker<uint16_t>())
            .AddAttribute("slMaxNumPerReserve",
                          "Sidelink max num per reserve",
                          UintegerValue(3),
                          MakeUintegerAccessor(&MosaicNodeManager::m_slMaxNumPerReserve),
                          MakeUintegerChecker<uint16_t>())
            .AddAttribute("slReservePeriodMs",
                          "Sidelink reserve period in ms",
                          UintegerValue(100),
                          MakeUintegerAccessor(&MosaicNodeManager::m_slReservePeriodMs),
                          MakeUintegerChecker<uint16_t>())
            .AddAttribute("slMcs",
                          "Sidelink scheduler fixed MCS",
                          UintegerValue(14),
                          MakeUintegerAccessor(&MosaicNodeManager::m_slMcs),
                          MakeUintegerChecker<uint16_t>())
            .AddAttribute("slDstL2Id",
                          "Sidelink destination L2 id",
                          UintegerValue(255),
                          MakeUintegerAccessor(&MosaicNodeManager::m_slDstL2Id),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("slHarqEnabled",
                          "Enable sidelink HARQ",
                          BooleanValue(true),
                          MakeBooleanAccessor(&MosaicNodeManager::m_slHarqEnabled),
                          MakeBooleanChecker())
            .AddAttribute("slTddPattern",
                          "Sidelink TDD pattern string",
                          StringValue("DL|DL|DL|F|UL|UL|UL|UL|UL|UL|"),
                          MakeStringAccessor(&MosaicNodeManager::m_slTddPattern),
                          MakeStringChecker());
    return tid;
}

MosaicNodeManager::MosaicNodeManager()
    : m_serverPtr(nullptr),
      m_nrSpectrumBuilt(false),
      m_backboneAddressHelper("5.0.0.0", "255.0.0.0")
{
    m_beamformingHelper = CreateObject<IdealBeamformingHelper>();
    m_epcHelper = CreateObject<NrPointToPointEpcHelper>();
    m_nrHelper = CreateObject<NrHelper>();
    m_nrSlHelper = CreateObject<NrSlHelper>();

    m_nrHelper->SetBeamformingHelper(m_beamformingHelper);
    m_nrHelper->SetEpcHelper(m_epcHelper);
    m_nrSlHelper->SetEpcHelper(m_epcHelper);


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

    NS_LOG_INFO("BuildNrSpectrum() enter");
    NS_ASSERT_MSG(m_nrHelper != nullptr, "m_nrHelper is null");

    m_beamformingHelper->SetAttribute("BeamformingMethod",
                                      TypeIdValue(DirectPathBeamforming::GetTypeId()));
    m_epcHelper->SetAttribute("S1uLinkDelay", TimeValue(MilliSeconds(0)));

    m_nrHelper->SetChannelConditionModelAttribute("UpdatePeriod", TimeValue(MilliSeconds(0)));
    m_nrHelper->SetPathlossAttribute("ShadowingEnabled", BooleanValue(m_nrShadowingEnabled));
    m_nrHelper->SetUePhyAttribute("TxPower", DoubleValue(m_nrTxPowerDbm));
    m_nrHelper->SetUeAntennaAttribute("NumRows", UintegerValue(m_nrUeAntennaRows));
    m_nrHelper->SetUeAntennaAttribute("NumColumns", UintegerValue(m_nrUeAntennaColumns));

    // Follow official NR sidelink examples before InstallUeDevice().
    m_nrHelper->SetUeMacTypeId(NrSlUeMac::GetTypeId());
    m_nrHelper->SetUeMacAttribute("EnableSensing", BooleanValue(false));
    m_nrHelper->SetUeMacAttribute("T1", UintegerValue(2));
    m_nrHelper->SetUeMacAttribute("T2", UintegerValue(33));
    m_nrHelper->SetUeMacAttribute("ActivePoolId", UintegerValue(0));

    m_nrHelper->SetBwpManagerTypeId(TypeId::LookupByName("ns3::NrSlBwpManagerUe"));
    m_nrHelper->SetUeBwpManagerAlgorithmAttribute("GBR_MC_PUSH_TO_TALK",
                                                  UintegerValue(m_slBwpId));

    const uint8_t numCcPerBand = 1;
    CcBwpCreator::SimpleOperationBandConf bandConf(m_nrCentralFrequencyHz,
                                                   m_nrBandwidthHz,
                                                   numCcPerBand,
                                                   BandwidthPartInfo::UMi_StreetCanyon);

    m_operationBand = m_ccBwpCreator.CreateOperationBandContiguousCc(bandConf);
    m_nrHelper->InitializeOperationBand(&m_operationBand);
    m_allBwps = CcBwpCreator::GetAllBwps({m_operationBand});

    NS_ASSERT_MSG(!m_allBwps.empty(), "m_allBwps is empty after InitializeOperationBand");

    NS_LOG_INFO("BuildNrSpectrum() done, bwps=" << m_allBwps.size()
                                                << " centerFreqHz=" << m_nrCentralFrequencyHz
                                                << " bandwidthHz=" << m_nrBandwidthHz
                                                << " numerology=" << m_nrNumerology);

    m_nrSpectrumBuilt = true;
}

void
MosaicNodeManager::Configure(MosaicNs3Bridge* serverPtr)
{
    NS_LOG_INFO("Initialize Node Infrastructure...");
    m_serverPtr = serverPtr;
    BuildNrSpectrum();
}

void
MosaicNodeManager::OnStart(void)
{
    NS_LOG_INFO("Do the final configuration...");
    NS_LOG_INFO("Pre-created radio pool size=" << m_extraRadioNodes.GetN());
    PrintNodeConfigs(m_backboneNodes, 10);
    PrintNodeConfigs(m_gnbNodes, 10);
    PrintNodeConfigs(m_radioNodes, 10);
    PrintNodeConfigs(m_extraRadioNodes, 10);
}

void
MosaicNodeManager::OnShutdown(void)
{
    NS_LOG_FUNCTION(this);
    PrintNodeConfigs(m_radioNodes);
}

void
MosaicNodeManager::RejectAnyUeConnectionRequest(void)
{
    NS_LOG_INFO("RejectAnyUeConnectionRequest() ignored in sidelink-oriented build");
}

uint32_t
MosaicNodeManager::GetNs3NodeId(uint32_t mosaicNodeId)
{
    auto it = m_mosaic2nsdrei.find(mosaicNodeId);
    if (it == m_mosaic2nsdrei.end())
    {
        NS_LOG_ERROR("Node ID " << mosaicNodeId << " not found in m_mosaic2nsdrei");
        std::exit(1);
    }
    return it->second;
}

uint32_t
MosaicNodeManager::GetMosaicNodeId(uint32_t ns3NodeId)
{
    auto it = m_nsdrei2mosaic.find(ns3NodeId);
    if (it == m_nsdrei2mosaic.end())
    {
        NS_LOG_ERROR("Node ID " << ns3NodeId << " not found in m_nsdrei2mosaic");
        std::exit(1);
    }
    return it->second;
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
            Ptr<NetDevice> device = node->GetDevice(i);
            int32_t ipif = DynamicCast<Ipv4L3Protocol>(ipv4proto)->GetInterfaceForDevice(device);
            if (ipif != -1)
            {
                Ipv4InterfaceAddress iaddr = ipv4proto->GetAddress(ipif, 0);
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
MosaicNodeManager::CreateNodeB(Vector position)
{
    NS_LOG_INFO("CreateNodeB called in sidelink-oriented build; treat as bootstrap/no-op");
    BuildNrSpectrum();

    uint32_t targetPoolSize = std::max<uint32_t>(1u, static_cast<uint32_t>(m_numExtraRadioNodes));
    while (m_extraRadioNodes.GetN() < targetPoolSize)
    {
        Ptr<Node> ueNode = CreateRadioNodeHelper();
        m_extraRadioNodes.Add(ueNode);
        m_isDeactivated[ueNode->GetId()] = true;
        NS_LOG_INFO("Pre-created sidelink radio pool node " << ueNode->GetId());
    }

    NS_LOG_INFO("Sidelink bootstrap complete. Virtual anchor position=("
                << position.x << ", " << position.y << ", " << position.z << ")");
}

void
MosaicNodeManager::CreateWiredNode(uint32_t mosaicNodeId)
{
    if (m_mosaic2nsdrei.find(mosaicNodeId) != m_mosaic2nsdrei.end())
    {
        NS_LOG_ERROR("Cannot create node with id=" << mosaicNodeId << " multiple times.");
        std::exit(1);
    }

    Ptr<Node> node = CreateObject<Node>();
    NS_LOG_INFO("Create wired node " << mosaicNodeId << "->" << node->GetId());
    m_mosaic2nsdrei[mosaicNodeId] = node->GetId();
    m_nsdrei2mosaic[node->GetId()] = mosaicNodeId;
    m_isWiredNode[node->GetId()] = true;
    m_isDeactivated[node->GetId()] = false;
    m_backboneNodes.Add(node);

    m_internetHelper.Install(node);

    if (m_backboneDevices.GetN() == 0)
    {
        m_backboneDevices = m_csmaHelper.Install(m_backboneNodes);
        m_backboneAddressHelper.Assign(m_backboneDevices);
    }
    else
    {
        Ptr<CsmaChannel> ch = DynamicCast<CsmaChannel>(m_backboneDevices.Get(0)->GetChannel());
        Ptr<NetDevice> device = m_csmaHelper.Install(node, ch).Get(0);
        m_backboneDevices.Add(device);
        m_backboneAddressHelper.Assign(device);
    }

    Ptr<MosaicProxyApp> app = CreateObject<MosaicProxyApp>();
    app->SetRecvCallback(MakeCallback(&MosaicNodeManager::RecvCellMsg, this));
    node->AddApplication(app);
    app->SetSockets(interface_e::ETH);
}

void
MosaicNodeManager::InstallSidelinkPreconfiguration(const NetDeviceContainer& ueDevices)
{
    NS_LOG_INFO("InstallSidelinkPreconfiguration() enter, ueCount=" << ueDevices.GetN());

    if (ueDevices.GetN() == 0)
    {
        return;
    }

    std::set<uint8_t> bwpIdContainer;
    bwpIdContainer.insert(m_slBwpId);

    // Follow official NR sidelink examples: configure error model + AMC + scheduler
    // before PrepareUeForSidelink().
    m_nrSlHelper->SetSlErrorModel("ns3::NrEesmIrT1");
    m_nrSlHelper->SetUeSlAmcAttribute("AmcModel", EnumValue(NrAmc::ErrorModel));
    m_nrSlHelper->SetNrSlSchedulerTypeId(NrSlUeMacSchedulerFixedMcs::GetTypeId());
    m_nrSlHelper->SetUeSlSchedulerAttribute("Mcs", UintegerValue(m_slMcs));
    m_nrSlHelper->PrepareUeForSidelink(ueDevices, bwpIdContainer);

    LteRrcSap::SlResourcePoolNr slResourcePoolNr;
    Ptr<NrSlCommResourcePoolFactory> ptrFactory = Create<NrSlCommResourcePoolFactory>();

    std::vector<std::bitset<1>> slBitmap = {1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1};
    ptrFactory->SetSlTimeResources(slBitmap);
    ptrFactory->SetSlSensingWindow(m_slSensingWindowMs);
    ptrFactory->SetSlSelectionWindow(m_slSelectionWindow);
    ptrFactory->SetSlFreqResourcePscch(m_slFreqResourcePscch);
    ptrFactory->SetSlSubchannelSize(m_slSubchannelSize);
    ptrFactory->SetSlMaxNumPerReserve(m_slMaxNumPerReserve);
    std::list<uint16_t> resourceReservePeriodList = {0, m_slReservePeriodMs};
    ptrFactory->SetSlResourceReservePeriodList(resourceReservePeriodList);
    slResourcePoolNr = ptrFactory->CreatePool();

    LteRrcSap::SlResourcePoolConfigNr slresoPoolConfigNr;
    slresoPoolConfigNr.haveSlResourcePoolConfigNr = true;
    LteRrcSap::SlResourcePoolIdNr slResourcePoolIdNr;
    slResourcePoolIdNr.id = 0;
    slresoPoolConfigNr.slResourcePoolId = slResourcePoolIdNr;
    slresoPoolConfigNr.slResourcePool = slResourcePoolNr;

    LteRrcSap::SlBwpPoolConfigCommonNr slBwpPoolConfigCommonNr;
    slBwpPoolConfigCommonNr.slTxPoolSelectedNormal[slResourcePoolIdNr.id] = slresoPoolConfigNr;

    LteRrcSap::Bwp bwp;
    bwp.numerology = m_nrNumerology;
    bwp.symbolsPerSlots = 14;
    bwp.rbPerRbg = 1;
    // 20 MHz @ mu=1 is ~51 RBs; use a conservative valid value if config is smaller.
    bwp.bandwidth = std::max<uint16_t>(std::max<uint16_t>(m_slSubchannelSize, 10), 50);

    LteRrcSap::SlBwpGeneric slBwpGeneric;
    slBwpGeneric.bwp = bwp;
    slBwpGeneric.slLengthSymbols = LteRrcSap::GetSlLengthSymbolsEnum(14);
    slBwpGeneric.slStartSymbol = LteRrcSap::GetSlStartSymbolEnum(0);

    LteRrcSap::SlBwpConfigCommonNr slBwpConfigCommonNr;
    slBwpConfigCommonNr.haveSlBwpGeneric = true;
    slBwpConfigCommonNr.slBwpGeneric = slBwpGeneric;
    slBwpConfigCommonNr.haveSlBwpPoolConfigCommonNr = true;
    slBwpConfigCommonNr.slBwpPoolConfigCommonNr = slBwpPoolConfigCommonNr;

    LteRrcSap::SlFreqConfigCommonNr slFreConfigCommonNr;
    for (const auto& it : bwpIdContainer)
    {
        slFreConfigCommonNr.slBwpList[it] = slBwpConfigCommonNr;
    }

    LteRrcSap::TddUlDlConfigCommon tddUlDlConfigCommon;
    tddUlDlConfigCommon.tddPattern = m_slTddPattern;

    LteRrcSap::SlPreconfigGeneralNr slPreconfigGeneralNr;
    slPreconfigGeneralNr.slTddConfig = tddUlDlConfigCommon;

    LteRrcSap::SlUeSelectedConfig slUeSelectedPreConfig;
    slUeSelectedPreConfig.slProbResourceKeep = 0;

    LteRrcSap::SlPsschTxParameters psschParams;
    psschParams.slMaxTxTransNumPssch = 5;

    LteRrcSap::SlPsschTxConfigList psschTxConfigList;
    psschTxConfigList.slPsschTxParameters[0] = psschParams;
    slUeSelectedPreConfig.slPsschTxConfigList = psschTxConfigList;

    LteRrcSap::SidelinkPreconfigNr slPreConfigNr;
    slPreConfigNr.slPreconfigGeneral = slPreconfigGeneralNr;
    slPreConfigNr.slUeSelectedPreConfig = slUeSelectedPreConfig;
    slPreConfigNr.slPreconfigFreqInfoList[m_slBwpId] = slFreConfigCommonNr;

    m_nrSlHelper->InstallNrSlPreConfiguration(ueDevices, slPreConfigNr);

    NS_LOG_INFO("InstallSidelinkPreconfiguration() done");
}

void
MosaicNodeManager::ActivateSidelinkBearer(const NetDeviceContainer& ueDevices,
                                          Ipv4Address remoteAddress)
{
    NS_LOG_INFO("ActivateSidelinkBearer() enter, ueCount=" << ueDevices.GetN()
                                                           << " remoteAddress=" << remoteAddress);

    if (ueDevices.GetN() == 0)
    {
        return;
    }

    Ptr<LteSlTft> tft;
    SidelinkInfo slInfo;
    slInfo.m_castType = SidelinkInfo::CastType::Groupcast;
    slInfo.m_dstL2Id = m_slDstL2Id;

    slInfo.m_dynamic = true;
    slInfo.m_rri = MilliSeconds(m_slReservePeriodMs);
    slInfo.m_pdb = MilliSeconds(100);
    slInfo.m_harqEnabled = m_slHarqEnabled;
    tft = Create<LteSlTft>(LteSlTft::Direction::BIDIRECTIONAL, remoteAddress, slInfo);
    m_nrSlHelper->ActivateNrSlBearer(Seconds(0), ueDevices, tft);

    for (uint32_t i = 0; i < ueDevices.GetN(); ++i)
    {
        Ptr<NrUeNetDevice> ueDev = DynamicCast<NrUeNetDevice>(ueDevices.Get(i));
        if (ueDev != nullptr)
        {
            m_isSidelinkBearerActivated[ueDev->GetNode()->GetId()] = true;
        }
    }

    NS_LOG_INFO("ActivateSidelinkBearer() done");
}

void
MosaicNodeManager::InstallSidelinkTrafficFilters(const NetDeviceContainer& ueDevices)
{
    NS_LOG_INFO("InstallSidelinkTrafficFilters() enter, ueCount=" << ueDevices.GetN());
    ActivateSidelinkBearer(ueDevices, Ipv4Address("225.0.0.0"));
}

void
MosaicNodeManager::ConfigureNodeIpv4ForSidelink(Ptr<Node> node,
                                                Ptr<NetDevice> device,
                                                Ipv4Address ip,
                                                Ipv4Mask mask)
{
    Ptr<Ipv4> ipv4proto = node->GetObject<Ipv4>();
    NS_ASSERT_MSG(ipv4proto != nullptr, "IPv4 object is null");

    int32_t ifIndex = ipv4proto->GetInterfaceForDevice(device);
    EnsureIpv4Alias(ipv4proto, ifIndex, ip, mask);
}

Ptr<Node>
MosaicNodeManager::CreateRadioNodeHelper(void)
{
    NS_LOG_INFO("CreateRadioNodeHelper() enter");
    BuildNrSpectrum();

    NS_ASSERT_MSG(m_nrHelper != nullptr, "m_nrHelper is null");
    NS_ASSERT_MSG(m_nrSlHelper != nullptr, "m_nrSlHelper is null");
    NS_ASSERT_MSG(!m_allBwps.empty(), "m_allBwps is empty");

    Ptr<Node> node = CreateObject<Node>();
    NS_ASSERT_MSG(node != nullptr, "Failed to create radio node");

    m_internetHelper.Install(node);

    m_mobilityHelper.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    m_mobilityHelper.Install(node);

    Ptr<ConstantVelocityMobilityModel> cvmm = node->GetObject<ConstantVelocityMobilityModel>();
    NS_ASSERT_MSG(cvmm != nullptr, "ConstantVelocityMobilityModel is null");
    cvmm->SetPosition(GetPoolParkingPosition(node->GetId()));
    cvmm->SetVelocity(Vector(0.0, 0.0, 0.0));

    NetDeviceContainer ueDevices = m_nrHelper->InstallUeDevice(NodeContainer(node), m_allBwps);
    NS_ASSERT_MSG(ueDevices.GetN() == 1, "InstallUeDevice did not return exactly one UE device");

    Ptr<NrUeNetDevice> ueDevice = DynamicCast<NrUeNetDevice>(ueDevices.Get(0));
    NS_ASSERT_MSG(ueDevice != nullptr, "Failed to cast installed UE device to NrUeNetDevice");

    // Follow the official examples: assign UE IPv4 addresses through the EPC
    // helper and install a default route before activating SL bearers.
    Ipv4InterfaceContainer ueIpIface = m_epcHelper->AssignUeIpv4Address(ueDevices);
    (void) ueIpIface;
    Ptr<Ipv4StaticRouting> ueStaticRouting =
        m_ipv4RoutingHelper.GetStaticRouting(node->GetObject<Ipv4>());
    NS_ASSERT_MSG(ueStaticRouting != nullptr, "UE static routing object is null");
    ueStaticRouting->SetDefaultRoute(m_epcHelper->GetUeDefaultGatewayAddress(), 1);

    // Official examples call UpdateConfig() after InstallUeDevice() and before
    // PrepareUeForSidelink().
    ueDevice->UpdateConfig();

    InstallSidelinkPreconfiguration(ueDevices);
    InstallSidelinkTrafficFilters(ueDevices);

    Ptr<MosaicProxyApp> cellApp = CreateObject<MosaicProxyApp>();
    NS_ASSERT_MSG(cellApp != nullptr, "Failed to create MosaicProxyApp");

    cellApp->SetRecvCallback(MakeCallback(&MosaicNodeManager::RecvCellMsg, this));
    node->AddApplication(cellApp);
    cellApp->SetSockets(interface_e::CELL);
    cellApp->Disable();

    NS_LOG_INFO("CreateRadioNodeHelper() created nodeId=" << node->GetId());
    return node;
}

void
MosaicNodeManager::CreateRadioNode(uint32_t mosaicNodeId, Vector position)
{
    if (m_mosaic2nsdrei.find(mosaicNodeId) != m_mosaic2nsdrei.end())
    {
        NS_LOG_ERROR("Cannot create node with id=" << mosaicNodeId << " multiple times.");
        std::exit(1);
    }

    Ptr<Node> node = nullptr;
    for (uint32_t i = 0; i < m_extraRadioNodes.GetN(); ++i)
    {
        Ptr<Node> candidate = m_extraRadioNodes.Get(i);
        if (candidate != nullptr && m_nsdrei2mosaic.find(candidate->GetId()) == m_nsdrei2mosaic.end())
        {
            node = candidate;
            break;
        }
    }

    if (node == nullptr)
    {
        NS_LOG_WARN("No pre-created sidelink radio node available; creating one on demand");
        node = CreateRadioNodeHelper();
        NS_ASSERT_MSG(node != nullptr, "CreateRadioNodeHelper returned null node");
        m_extraRadioNodes.Add(node);
        m_isDeactivated[node->GetId()] = true;
    }

    NS_LOG_INFO("Create radio node " << mosaicNodeId << " -> " << node->GetId());

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
    CreateRadioNode(mosaicNodeId, position);
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
    NS_ASSERT_MSG(mobModel != nullptr, "MobilityModel is null");
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

    for (uint32_t i = 0; i < node->GetNApplications(); i++)
    {
        Ptr<MosaicProxyApp> app = DynamicCast<MosaicProxyApp>(node->GetApplication(i));
        if (app)
        {
            app->Disable();
        }
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
        m_isSidelinkBearerActivated[nodeId] = false;

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
            m_nrHelper->GetUePhy(ueDev, 0)->SetAttribute("TxPower", DoubleValue(10 * log10(transmitPower)));
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

    if (m_isRadioNode[nodeId])
    {
        Ptr<MosaicProxyApp> cellApp = DynamicCast<MosaicProxyApp>(node->GetApplication(0));
        if (!cellApp)
        {
            NS_LOG_ERROR("No cell app found on node " << nodeId << " !");
            std::exit(1);
        }
        cellApp->Enable();

        Ptr<NrUeNetDevice> device = GetNrUeDevice(node);
        if (device == nullptr)
        {
            NS_LOG_ERROR("No NR UE device found on node " << nodeId);
            std::exit(1);
        }

        ConfigureNodeIpv4ForSidelink(node, device, ip, Ipv4Mask("255.0.0.0"));
        Ptr<Ipv4StaticRouting> ueStaticRouting =
            m_ipv4RoutingHelper.GetStaticRouting(node->GetObject<Ipv4>());
        if (ueStaticRouting != nullptr)
        {
            ueStaticRouting->SetDefaultRoute(m_epcHelper->GetUeDefaultGatewayAddress(), 1);
        }
        NS_LOG_INFO("Configured radio node " << nodeId << " for sidelink-style IP routing");
    }
    else if (m_isWiredNode[nodeId])
    {
        Ptr<MosaicProxyApp> csmaApp = DynamicCast<MosaicProxyApp>(node->GetApplication(0));
        if (!csmaApp)
        {
            NS_LOG_ERROR("No csma app found on node " << nodeId << " !");
            std::exit(1);
        }
        csmaApp->Enable();

        Ptr<NetDevice> device = GetBackboneNetDevice(node);
        if (device == nullptr)
        {
            NS_LOG_ERROR("No backbone device found on wired node " << nodeId);
            std::exit(1);
        }

        ConfigureNodeIpv4ForSidelink(node, device, ip, Ipv4Mask("255.255.0.0"));
    }
    else
    {
        NS_LOG_ERROR("Invalid State: Node has to be either radio or wired node.");
        std::exit(1);
    }
}

void
MosaicNodeManager::SendWifiMsg(uint32_t mosaicNodeId,
                               Ipv4Address dstAddr,
                               ClientServerChannelSpace::RadioChannel channel,
                               uint32_t msgID,
                               uint32_t payLength)
{
    if (channel != ClientServerChannelSpace::RadioChannel::PROTO_CCH)
    {
        NS_LOG_WARN("NR-only implementation ignores legacy DSRC channel selection.");
    }
    SendCellMsg(mosaicNodeId, dstAddr, msgID, payLength);
}

void
MosaicNodeManager::SendCellMsg(uint32_t mosaicNodeId,
                               Ipv4Address dstAddr,
                               uint32_t msgID,
                               uint32_t payLength)
{
    if (IsTimingDebugEnabledNodeManager())
    {
        std::ostringstream oss;
        oss << "SEND_CELL_ENTER"
            << " msgId=" << msgID
            << " mosaicNodeId=" << mosaicNodeId
            << " requestedDst=" << dstAddr
            << " payload=" << payLength
            << " simNowNs=" << Simulator::Now().GetNanoSeconds();
        TimingDebugNodeManager(oss.str());
    }

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

    Ipv4Address slGroupAddr("225.0.0.0");
    NS_LOG_DEBUG("[node=" << nodeId << "] using sidelink multicast dst=" << slGroupAddr
                         << " (requested dst=" << dstAddr << ") msgID=" << msgID
                         << " len=" << payLength);

    if (IsTimingDebugEnabledNodeManager())
    {
        std::ostringstream oss;
        oss << "SEND_CELL_RESOLVED"
            << " msgId=" << msgID
            << " mosaicNodeId=" << mosaicNodeId
            << " ns3NodeId=" << nodeId
            << " requestedDst=" << dstAddr
            << " actualDst=" << slGroupAddr
            << " payload=" << payLength
            << " simNowNs=" << Simulator::Now().GetNanoSeconds();
        TimingDebugNodeManager(oss.str());
    }

    app->TransmitPacket(slGroupAddr, msgID, payLength);
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
    if (IsTimingDebugEnabledNodeManager())
    {
        std::ostringstream oss;
        oss << "RECV_CELL_FORWARD"
            << " recvTimeNs=" << recvTime
            << " ns3NodeId=" << ns3NodeId
            << " msgId=" << msgID
            << " simNowNs=" << Simulator::Now().GetNanoSeconds();
        TimingDebugNodeManager(oss.str());
    }

    if (m_isDeactivated[ns3NodeId])
    {
        return;
    }
    uint32_t nodeId = GetMosaicNodeId(ns3NodeId);
    m_serverPtr->writeReceiveCellMessage(recvTime, nodeId, msgID);
}

} // namespace ns3