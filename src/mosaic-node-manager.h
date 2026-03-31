#ifndef MOSAIC_NODE_MANAGER_H
#define MOSAIC_NODE_MANAGER_H

#include <unordered_map>
#include <map>

#include "ns3/application.h"
#include "ns3/core-module.h"
#include "ns3/csma-helper.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/mobility-helper.h"
#include "ns3/net-device-container.h"
#include "ns3/node-container.h"
#include "ns3/vector.h"

#include "ns3/cc-bwp-helper.h"
#include "ns3/ideal-beamforming-helper.h"
#include "ns3/nr-gnb-net-device.h"
#include "ns3/nr-helper.h"
#include "ns3/nr-point-to-point-epc-helper.h"
#include "ns3/nr-ue-net-device.h"

#include "client-server-channel.h"

namespace ns3 {

class MosaicNs3Bridge;

/**
 * Clean NR-targeted MosaicNodeManager.
 *
 * External MOSAIC-facing APIs are preserved (ConfigureWifiRadio, SendWifiMsg,
 * etc.) so the upper MOSAIC layers do not need to change, but internally the
 * wireless path is backed by CTTC 5G-LENA NR UE/gNB devices.
 */
class MosaicNodeManager : public Object
{
public:
  static TypeId GetTypeId (void);

  MosaicNodeManager ();
  ~MosaicNodeManager () override = default;

  void Configure (MosaicNs3Bridge *serverPtr);
  void OnStart (void);
  void OnShutdown (void);
  void RejectAnyUeConnectionRequest (void);

  void CreateNodeB (Vector position);
  void CreateWiredNode (uint32_t mosaicNodeId);
  void CreateRadioNode (uint32_t mosaicNodeId, Vector position);
  void ActivateRadioNode (uint32_t mosaicNodeId, Vector position);
  void UpdateNodePosition (uint32_t mosaicNodeId, Vector position);
  void RemoveNode (uint32_t mosaicNodeId);

  void ConfigureWifiRadio (uint32_t mosaicNodeId, double transmitPower, Ipv4Address ip);
  void ConfigureCellRadio (uint32_t mosaicNodeId, Ipv4Address ip);

  void SendWifiMsg (uint32_t mosaicNodeId,
                    Ipv4Address dstAddr,
                    ClientServerChannelSpace::RadioChannel channel,
                    uint32_t msgID,
                    uint32_t payLength);

  void SendCellMsg (uint32_t mosaicNodeId,
                    Ipv4Address dstAddr,
                    uint32_t msgID,
                    uint32_t payLength);

  void RecvWifiMsg (unsigned long long recvTime, uint32_t ns3NodeId, int msgID);
  void RecvCellMsg (unsigned long long recvTime, uint32_t ns3NodeId, int msgID);

  // Must remain public for ns-3 attribute construction.
  uint16_t m_numExtraRadioNodes;

private:
  uint32_t GetNs3NodeId (uint32_t mosaicNodeId);
  uint32_t GetMosaicNodeId (uint32_t ns3NodeId);

  Ptr<Node> CreateRadioNodeHelper (void);

  void PrintNodeConfigs (NodeContainer nodes, uint32_t maxNum = 100);
  void PrintNodeConfigsDeviceAgnostic (NodeContainer nodes, uint32_t maxNum = 100);

  void ConfigureRadioBearer (uint32_t mosaicNodeId,
                             double transmitPower,
                             Ipv4Address ip);

  void SendRadioMsg (uint32_t mosaicNodeId,
                     Ipv4Address dstAddr,
                     ClientServerChannelSpace::RadioChannel channel,
                     uint32_t msgID,
                     uint32_t payLength);

  bool HasIpv4Address (Ptr<Ipv4> ipv4, int32_t ifIndex, Ipv4Address ip) const;
  Ptr<CsmaNetDevice> FindFirstCsmaDevice (Ptr<Node> node) const;

  MosaicNs3Bridge *m_serverPtr {nullptr};

  std::map<uint32_t, uint32_t> m_mosaic2nsdrei;
  std::map<uint32_t, uint32_t> m_nsdrei2mosaic;

  std::unordered_map<uint32_t, bool> m_isRadioNode;
  std::unordered_map<uint32_t, bool> m_isWiredNode;
  std::unordered_map<uint32_t, bool> m_isCellRadioConfigured;
  std::unordered_map<uint32_t, bool> m_isWifiRadioConfigured;
  std::unordered_map<uint32_t, bool> m_isDeactivated;
  std::unordered_map<uint32_t, bool> m_isRadioAttached;

  // Explicit handles instead of device/app index assumptions.
  std::unordered_map<uint32_t, Ptr<NetDevice>> m_radioDevice;
  std::unordered_map<uint32_t, Ptr<Application>> m_radioApp;
  std::unordered_map<uint32_t, Ptr<Application>> m_cellApp;

  // 5G-LENA helpers.
  Ptr<NrPointToPointEpcHelper> m_epcHelper;
  Ptr<IdealBeamformingHelper> m_beamformingHelper;
  Ptr<NrHelper> m_nrHelper;
  BandwidthPartInfoPtrVector m_allBwps;

  // Wired backbone.
  CsmaHelper m_csmaHelper;

  // Internet.
  InternetStackHelper m_internetHelper;
  Ipv4StaticRoutingHelper m_ipv4RoutingHelper;

  // IP addressing.
  Ipv4AddressHelper m_backboneAddressHelper;
  Ipv4AddressHelper m_radioAddressHelper;

  // Mobility.
  MobilityHelper m_mobilityHelper;

  // Nodes and devices.
  NodeContainer m_backboneNodes;
  NetDeviceContainer m_backboneDevices;
  NodeContainer m_enbNodes;     // gNB nodes in NR
  NetDeviceContainer m_enbDevices; // gNB devices in NR
  NodeContainer m_radioNodes;
  NodeContainer m_extraRadioNodes;

  // Default NR configuration used by this manager.
  double m_centralFrequencyHz;
  double m_channelBandwidthHz;
  uint8_t m_defaultNumerology;
  double m_defaultGnbTxPowerDbm;
};

} // namespace ns3

#endif /* MOSAIC_NODE_MANAGER_H */
