#ifndef MOSAIC_LTE_PROXY_APP_H
#define MOSAIC_LTE_PROXY_APP_H

#include "ns3/application.h"
#include "ns3/socket.h"
#include "ns3/lte-v2x-helper.h"
#include "mosaic-node-manager.h"

#include "ns3/lte-v2x-helper.h"


namespace ns3
{

    class MosaicLteProxyApp : public MosaicProxyApp
    {
    public:
        MosaicLteProxyApp() = default;

        virtual ~MosaicLteProxyApp() = default;

        static TypeId GetTypeId(void) override;

        void Configure(Ptr<LteV2xHelper> lteV2xHelper, Ipv4Address respondAddress, uint32_t groupL2Address);

        void SetSockets(void) override;

        void TransmitPacket(uint32_t protocolID, uint32_t msgID, uint32_t payLength, Ipv4Address address) override;
        
        virtual void DoDispose(void) override;

    private:
        void Receive(Ptr<Socket> socket) override;

        Ptr<Socket> m_host;
        Ptr<Socket> m_sink;
        Ptr<LteV2xHelper> m_lteV2xHelper;
        Ipv4Address m_respondAddress;
        uint32_t m_groupL2Address;
        MosaicNodeManager *m_nodeManager;
        bool m_active;
        uint16_t m_port = 8000;
    };

} // namespace ns3

#endif /* MOSAIC_LTE_PROXY_APP_H */