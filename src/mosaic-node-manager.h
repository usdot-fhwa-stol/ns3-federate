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

#ifndef MOSAICNODEMANAGER_H
#define MOSAICNODEMANAGER_H

#include <unordered_map>

#include "ns3/ipv4-address-helper.h"
#include "ns3/node-container.h"
#include "ns3/wifi-80211p-helper.h"
#include "ns3/wave-mac-helper.h"
#include "ns3/vector.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/lte-helper.h"
#include "ClientServerChannel.h"

#include "ns3/lte-helper.h"
#include "ns3/lte-v2x-helper.h"

#include "ns3/point-to-point-epc-helper.h"

#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/mobility-helper.h"
#include "ns3/sl-v2x-preconfig-pool-factory.h"
#include "ns3/ipv4-address-generator.h"


namespace ns3 {

    using namespace ClientServerChannelSpace;

    //Forward declaration to prevent circular dependency
    class MosaicNs3Server;

    // Define the communication types

    /**
     * @class MosaicNodeManager
     * @brief The class MosaicNodeManager manages the creation, the initial 
     * placement, and the position updates of ns3 nodes.
     */
    class MosaicNodeManager : public Object {
    public:
        static TypeId GetTypeId(void);

        MosaicNodeManager();
        virtual ~MosaicNodeManager() = default;

        void Configure(MosaicNs3Server* serverPtr, CommunicationType commType=DSRC);

        void CreateMosaicNode(int ID, Vector position);
        void UpdateNodePosition(uint32_t nodeId, Vector position);
        void ConfigureNodeRadio(uint32_t nodeId, bool radioTurnedOn, int transmitPower);
        void ConfigureSidelink(LteRrcSap::SlV2xPreconfiguration preconfiguration);
        void SendMsg(uint32_t nodeId, uint32_t protocolID, uint32_t msgID, uint32_t payLenght, Ipv4Address ipv4Add);
        bool ActivateNode(uint32_t nodeId);
        void DeactivateNode(uint32_t nodeId);

        void AddRecvPacket(unsigned long long recvTime, Ptr<Packet> pack, int nodeID, int msgID);

        uint32_t GetNs3NodeId(uint32_t nodeId);

        //Must be public to be accessible by ns-3 object creation routine
        std::string m_lossModel;
        std::string m_delayModel;

    private:
        MosaicNs3Server *m_serverPtr;
        std::map<uint32_t, uint32_t> m_mosaic2ns3ID;
        std::map<uint32_t, Ipv4Address> m_ns3ID2UniqueAddress;
        std::unordered_map<uint32_t, bool> m_isDeactivated;

        //Channel
        YansWifiChannelHelper m_wifiChannelHelper;
        Ptr<YansWifiChannel> m_channel;

        //PHY
        YansWifiPhyHelper m_wifiPhyHelper = YansWifiPhyHelper::Default();

        //MAC
        NqosWaveMacHelper m_waveMacHelper = NqosWaveMacHelper::Default();

        //Assembler
        Wifi80211pHelper m_wifi80211pHelper = Wifi80211pHelper::Default();

        // LTE Helper
        Ptr<LteHelper> m_lteHelper;
        Ptr<LteV2xHelper> m_lteV2xHelper;
        Ptr<PointToPointEpcHelper> m_epcHelper;
        Ptr<LteUeRrcSl> m_ueSidelinkConfiguration;

        Ipv4AddressHelper m_ipAddressHelper;

        NetDeviceContainer m_ueDevs;

        uint32_t m_groupL2Address;
        Ipv4Address m_clientRespondersAddress;

        CommunicationType m_commType = 0;

    };
}
#endif
