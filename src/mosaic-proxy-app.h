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

#ifndef MOSAIC_PROXY_APP_H
#define MOSAIC_PROXY_APP_H

#include "ns3/application.h"
#include "ns3/address.h"
#include "ns3/callback.h"
#include "ns3/event-id.h"
#include "ns3/ipv4-address.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"
#include "ns3/socket.h"

namespace ns3 {

    enum class interface_e {
        WIFI,
        CELL,
        ETH
    };

    class MosaicProxyApp : public Application {
    public:
        static TypeId GetTypeId(void);

        MosaicProxyApp();
        ~MosaicProxyApp() override;

        void SetSockets(interface_e interfaceType);
        void SetRecvCallback(Callback<void, unsigned long long, uint32_t, int> cb);

        void Enable();
        void Disable();

        void TransmitPacket(Ipv4Address dstAddr, uint32_t msgId, uint32_t payloadLength);

    protected:
        void DoDispose() override;
        void StartApplication() override;
        void StopApplication() override;

    private:
        uint32_t ResolveOutgoingDeviceIndex(interface_e interfaceType) const;
        void Receive(Ptr<Socket> socket);

    private:
        Ptr<Socket> m_socket;
        Callback<void, unsigned long long, uint32_t, int> m_recvCallback;
        interface_e m_interfaceType{interface_e::ETH};
        bool m_enabled{false};
        uint16_t m_port{1234};
    };

} // namespace ns3

#endif /* MOSAIC_PROXY_APP_H */