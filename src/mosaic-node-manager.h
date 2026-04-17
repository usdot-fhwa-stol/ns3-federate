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

#ifndef MOSAIC_NODE_MANAGER_H
#define MOSAIC_NODE_MANAGER_H

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

    //Forward declaration to prevent circular dependency
    class MosaicNs3Bridge;

    /**
     * @class MosaicNodeManager
     * @brief The class MosaicNodeManager manages the creation, the initial 
     * placement, and the position updates of ns3 nodes. It also manages the
     * node ID translation between MOSAIC and NS3 domain.
     */
    class MosaicNodeManager : public Object {
    public:
        static TypeId GetTypeId(void);

        MosaicNodeManager();
        virtual ~MosaicNodeManager() = default;

        void Configure(MosaicNs3Bridge* serverPtr);

        /**
         * @brief this function is called just before leaving simulation time zero
         */
        void OnStart(void);

        void OnShutdown(void);

        /**
         * @brief this function will change the gNB settings such, that no UE can request a connection.
         * This is especially required, so that only gNB changes initiated by the handover algorithm remain.
         * ATTENTION: You cannot insert more nodes into the network after this change, the initial connection will fail.
         */
        void RejectAnyUeConnectionRequest(void);

        /**
         * @brief create a new gNodeB
         *
         * @param position the new node position as a Vector
         */
        void CreateNodeB(Vector position);

        /**
         * @brief create a new wired node
         *
         * @param mosaicNodeId id of the node
         */
        void CreateWiredNode(uint32_t mosaicNodeId);

        /**
         * @brief create a new radio node (before simulation started)
         *
         * @param mosaicNodeId id of the node
         * @param position the new node position as a Vector
         */
        void CreateRadioNode(uint32_t mosaicNodeId, Vector position);

        /**
         * @brief activate a radio node (after simulation started)
         *
         * @param mosaicNodeId id of the node
         * @param position the new node position as a Vector
         */
        void ActivateRadioNode(uint32_t mosaicNodeId, Vector position);

        /**
         * @brief update the node position
         *
         * @param mosaicNodeId id of the node
         * @param position the new node position as a Vector
         */
        void UpdateNodePosition(uint32_t mosaicNodeId, Vector position);
        
        /**
         * @brief Remove the node as good as possible
         * It is not allowed to delete a node during the simulation.
         * The node will be deactivated as good as possible.
         *
         * @param mosaicNodeId id of the node
         */
        void RemoveNode(uint32_t mosaicNodeId);

        /**
         * @brief Legacy compatibility entry point. In the NR-only implementation,
         *        this configures the single NR radio interface.
         */
        void ConfigureWifiRadio(uint32_t mosaicNodeId, double transmitPower, Ipv4Address ip);

        /**
         * @brief Sets the provided configuration, and attaches the UE to a gNB
         */
        void ConfigureCellRadio(uint32_t mosaicNodeId, Ipv4Address ip);

        /**
         * @brief Legacy compatibility entry point. In the NR-only implementation,
         *        this sends through the NR interface.
         */
        void SendWifiMsg(uint32_t mosaicNodeId, Ipv4Address dstAddr, ClientServerChannelSpace::RadioChannel channel, uint32_t msgID, uint32_t payLength);

        /**
         * @brief start the sending of a cell message on a node
         *
         * @param mosaicNodeId id of the node
         * @param dstAddr the IPv4 destination address
         * @param msgID the msgID of the message
         * @param payLength the length of the message
         */
        void SendCellMsg(uint32_t mosaicNodeId, Ipv4Address dstAddr, uint32_t msgID, uint32_t payLength);

        void RecvWifiMsg(unsigned long long recvTime, uint32_t ns3NodeId, int msgID);

        void RecvCellMsg(unsigned long long recvTime, uint32_t ns3NodeId, int msgID);

        // Must be public to be accessible by ns-3 object creation routine
        uint16_t m_numExtraRadioNodes;

    private:

        /**
         * @brief translate the MOSAIC node IDs to Ns3 node IDs
         */
        uint32_t GetNs3NodeId(uint32_t mosaicNodeId);

        /**
         * @brief translate the Ns3 node IDs to MOSAIC node IDs
         */
        uint32_t GetMosaicNodeId(uint32_t ns3NodeId);

        /**
         * @brief Create a radio node and return it
         */ 
        Ptr<Node> CreateRadioNodeHelper(void);

        Ptr<NrUeNetDevice> GetNrUeDevice(Ptr<Node> node) const;
        Ptr<NrGnbNetDevice> GetNrGnbDevice(Ptr<Node> node) const;
        Ptr<NetDevice> GetBackboneNetDevice(Ptr<Node> node) const;
        void BuildNrSpectrum();

        /**
         * @brief Print important information about device/interface configuration
         */
        void PrintNodeConfigs(NodeContainer nodes, uint32_t maxNum = 100);

        /**
         * @brief Print important information about device/interface configuration
         */
        void PrintNodeConfigsDeviceAgnostic(NodeContainer nodes, uint32_t maxNum = 100);

        MosaicNs3Bridge *m_serverPtr;
        std::map<uint32_t, uint32_t> m_mosaic2nsdrei;
        std::map<uint32_t, uint32_t> m_nsdrei2mosaic;
        std::unordered_map<uint32_t, bool> m_isRadioNode;
        std::unordered_map<uint32_t, bool> m_isWiredNode;
        std::unordered_map<uint32_t, bool> m_isCellRadioConfigured;
        std::unordered_map<uint32_t, bool> m_isWifiRadioConfigured;
        std::unordered_map<uint32_t, bool> m_isDeactivated;

        /** Helpers **/
        // NR
        Ptr<NrHelper> m_nrHelper;
        Ptr<NrPointToPointEpcHelper> m_epcHelper;
        Ptr<IdealBeamformingHelper> m_beamformingHelper;
        CcBwpCreator m_ccBwpCreator;
        OperationBandInfo m_operationBand;
        BandwidthPartInfoPtrVector m_allBwps;
        bool m_nrSpectrumBuilt;
        // Wired
        CsmaHelper m_csmaHelper;
        // Internet
        InternetStackHelper m_internetHelper;   
        Ipv4StaticRoutingHelper m_ipv4RoutingHelper;
        // IP
        Ipv4AddressHelper m_backboneAddressHelper;
        // Mobility
        MobilityHelper m_mobilityHelper;

        /** Nodes and Devices **/
        NodeContainer m_backboneNodes;
        NetDeviceContainer m_backboneDevices;
        NodeContainer m_gnbNodes;
        NetDeviceContainer m_gnbDevices;
        NodeContainer m_radioNodes;
        NodeContainer m_extraRadioNodes;

        /** NR configuration **/
        double m_nrCentralFrequencyHz;
        double m_nrBandwidthHz;
        uint16_t m_nrNumerology;
        double m_nrTxPowerDbm;
        bool m_nrShadowingEnabled;
        uint16_t m_nrUeAntennaRows;
        uint16_t m_nrUeAntennaColumns;
        uint16_t m_nrGnbAntennaRows;
        uint16_t m_nrGnbAntennaColumns;
    };
} // namespace ns3
#endif /* MOSAIC_NODE_MANAGER_H */
