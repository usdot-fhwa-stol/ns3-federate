/*
 * Copyright (c) 2020 Fraunhofer FOKUS and others. All rights reserved.
 *
 * Contact: mosaic@fokus.fokus.fraunhofer.de
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

#ifndef MOSAICNODEMANAGER_H
#define MOSAICNODEMANAGER_H

#include <map>
#include <string>
#include <unordered_map>

#include "ns3/node-container.h"
#include "ns3/vector.h"

// Internet/EPC routing
#include "ns3/internet-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/point-to-point-module.h"

// NR
#include "ns3/antenna-module.h"
#include "ns3/nr-module.h"

namespace ns3 {

class MosaicNs3Server;

/**
 * @class MosaicNodeManager
 * @brief Manages creation, initial placement, and position updates of ns-3 nodes.
 */
class MosaicNodeManager : public Object
{
public:
  static TypeId GetTypeId(void);

  MosaicNodeManager();
  virtual ~MosaicNodeManager() = default;

  void Configure(MosaicNs3Server* serverPtr);

  void CreateMosaicNode(int ID, Vector position);
  void UpdateNodePosition(uint32_t nodeId, Vector position);

  void ConfigureNodeRadio(uint32_t nodeId, bool radioTurnedOn, int transmitPower);

  void SendMsg(uint32_t nodeId,
               uint32_t protocolID,
               uint32_t msgID,
               uint32_t payLength,
               Ipv4Address ipv4Add);

  bool ActivateNode(uint32_t nodeId);
  void DeactivateNode(uint32_t nodeId);

  void AddRecvPacket(unsigned long long recvTime, Ptr<Packet> pack, int nodeID, int msgID);

  uint32_t GetNs3NodeId(uint32_t nodeId);

  // Must be public to be accessible by ns-3 object creation routine
  std::string m_lossModel;
  std::string m_delayModel;

private:
  MosaicNs3Server* m_serverPtr { nullptr };

  // MOSAIC external id -> ns-3 node id
  std::map<uint32_t, uint32_t> m_mosaic2ns3ID;

  // MOSAIC external id -> deactivated state
  std::unordered_map<uint32_t, bool> m_isDeactivated;

  // ========= 5G NR state =========
  bool m_nrInitialized { false };

  // Helpers
  Ptr<NrPointToPointEpcHelper> m_epcHelper;
  Ptr<IdealBeamformingHelper>  m_beamformingHelper;
  Ptr<NrHelper>                m_nrHelper;

  // EPC backhaul
  Ptr<Node>   m_pgw;
  Ptr<Node>   m_remoteHost;
  Ipv4Address m_remoteHostAddr;

  // gNB
  NodeContainer      m_gnbNodes;
  NetDeviceContainer m_gnbDevs;

  // UEs (indexed by MOSAIC nodeId)
  std::unordered_map<uint32_t, Ptr<Node>>      m_ueNodesByMosaicId;
  std::unordered_map<uint32_t, Ptr<NetDevice>> m_ueDevByMosaicId;
  std::unordered_map<uint32_t, Ipv4Address>    m_ueIpByMosaicId;

  // NR band/BWP config
  BandwidthPartInfoPtrVector m_allBwps;
  OperationBandInfo          m_band;

  // Parameters (wire to MOSAIC config later)
  uint16_t m_numerology { 4 };
  double   m_centralFrequency { 28e9 };
  double   m_bandwidth { 100e6 };
  double   m_totalTxPowerDbm { 4.0 };

private:
  void InitializeNrIfNeeded();
  Ipv4Address ConfigureUeNetworking(Ptr<Node> ue, Ptr<NetDevice> ueDev);
};

} // namespace ns3

#endif // MOSAICNODEMANAGER_H
