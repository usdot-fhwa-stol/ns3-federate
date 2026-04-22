#ifndef MOSAIC_NODE_MANAGER_H
#define MOSAIC_NODE_MANAGER_H

#include <set>
#include <string>
#include <unordered_map>

#include "ns3/node-container.h"
#include "ns3/vector.h"

#include "ns3/csma-helper.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/mobility-helper.h"
#include "ns3/nr-module.h"

#include "client-server-channel.h"

namespace ns3 {

class MosaicNs3Bridge;

class MosaicNodeManager : public Object
{
  public:
    static TypeId GetTypeId(void);

    MosaicNodeManager();
    ~MosaicNodeManager() override = default;

    void Configure(MosaicNs3Bridge* serverPtr);
    void OnStart(void);
    void OnShutdown(void);
    void RejectAnyUeConnectionRequest(void);

    void CreateNodeB(Vector position);
    void CreateWiredNode(uint32_t mosaicNodeId);
    void CreateRadioNode(uint32_t mosaicNodeId, Vector position);
    void ActivateRadioNode(uint32_t mosaicNodeId, Vector position);
    void UpdateNodePosition(uint32_t mosaicNodeId, Vector position);
    void RemoveNode(uint32_t mosaicNodeId);

    void ConfigureWifiRadio(uint32_t mosaicNodeId, double transmitPower, Ipv4Address ip);
    void ConfigureCellRadio(uint32_t mosaicNodeId, Ipv4Address ip);

    void SendWifiMsg(uint32_t mosaicNodeId,
                     Ipv4Address dstAddr,
                     ClientServerChannelSpace::RadioChannel channel,
                     uint32_t msgID,
                     uint32_t payLength);
    void SendCellMsg(uint32_t mosaicNodeId,
                     Ipv4Address dstAddr,
                     uint32_t msgID,
                     uint32_t payLength);

    void RecvWifiMsg(unsigned long long recvTime, uint32_t ns3NodeId, int msgID);
    void RecvCellMsg(unsigned long long recvTime, uint32_t ns3NodeId, int msgID);

    uint16_t m_numExtraRadioNodes;

  private:
    uint32_t GetNs3NodeId(uint32_t mosaicNodeId);
    uint32_t GetMosaicNodeId(uint32_t ns3NodeId);

    Ptr<Node> CreateRadioNodeHelper(void);
    Ptr<NrUeNetDevice> GetNrUeDevice(Ptr<Node> node) const;
    Ptr<NetDevice> GetBackboneNetDevice(Ptr<Node> node) const;

    void BuildNrSpectrum();
    void InstallSidelinkPreconfiguration(const NetDeviceContainer& ueDevices);
    void InstallSidelinkTrafficFilters(const NetDeviceContainer& ueDevices);
    void ConfigureNodeIpv4ForSidelink(Ptr<Node> node,
                                      Ptr<NetDevice> device,
                                      Ipv4Address ip,
                                      Ipv4Mask mask);
    void ActivateSidelinkBearer(const NetDeviceContainer& ueDevices, Ipv4Address remoteAddress);

    void PrintNodeConfigs(NodeContainer nodes, uint32_t maxNum = 100);
    void PrintNodeConfigsDeviceAgnostic(NodeContainer nodes, uint32_t maxNum = 100);

    MosaicNs3Bridge* m_serverPtr;
    std::map<uint32_t, uint32_t> m_mosaic2nsdrei;
    std::map<uint32_t, uint32_t> m_nsdrei2mosaic;
    std::unordered_map<uint32_t, bool> m_isRadioNode;
    std::unordered_map<uint32_t, bool> m_isWiredNode;
    std::unordered_map<uint32_t, bool> m_isCellRadioConfigured;
    std::unordered_map<uint32_t, bool> m_isWifiRadioConfigured;
    std::unordered_map<uint32_t, bool> m_isDeactivated;
    std::unordered_map<uint32_t, bool> m_isSidelinkBearerActivated;

    Ptr<NrHelper> m_nrHelper;
    Ptr<NrPointToPointEpcHelper> m_epcHelper;
    Ptr<NrSlHelper> m_nrSlHelper;
    Ptr<IdealBeamformingHelper> m_beamformingHelper;
    CcBwpCreator m_ccBwpCreator;
    OperationBandInfo m_operationBand;
    BandwidthPartInfoPtrVector m_allBwps;
    bool m_nrSpectrumBuilt;

    CsmaHelper m_csmaHelper;
    InternetStackHelper m_internetHelper;
    Ipv4StaticRoutingHelper m_ipv4RoutingHelper;
    Ipv4AddressHelper m_backboneAddressHelper;
    MobilityHelper m_mobilityHelper;

    NodeContainer m_backboneNodes;
    NetDeviceContainer m_backboneDevices;
    NodeContainer m_gnbNodes;
    NodeContainer m_radioNodes;
    NodeContainer m_extraRadioNodes;

    double m_nrCentralFrequencyHz;
    double m_nrBandwidthHz;
    uint16_t m_nrNumerology;
    double m_nrTxPowerDbm;
    bool m_nrShadowingEnabled;
    uint16_t m_nrUeAntennaRows;
    uint16_t m_nrUeAntennaColumns;

    uint8_t m_slBwpId;
    uint16_t m_slSensingWindowMs;
    uint16_t m_slSelectionWindow;
    uint16_t m_slFreqResourcePscch;
    uint16_t m_slSubchannelSize;
    uint16_t m_slMaxNumPerReserve;
    uint16_t m_slReservePeriodMs;
    uint16_t m_slMcs;
    uint32_t m_slDstL2Id;
    bool m_slHarqEnabled;
    std::string m_slTddPattern;
};

} // namespace ns3

#endif