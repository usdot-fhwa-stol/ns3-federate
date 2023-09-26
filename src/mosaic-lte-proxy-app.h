#ifndef MOSAIC_LTE_PROXY_APP_H
#define MOSAIC_LTE_PROXY_APP_H

#include "ns3/application.h"
#include "ns3/socket.h"
#include "ns3/lte-v2x-helper.h"
#include "mosaic-node-manager.h"

namespace ns3
{

    class MosaicLteProxyApp : public Application
    {
    public:
        static TypeId GetTypeId(void);
        MosaicLteProxyApp();
        virtual ~MosaicLteProxyApp();

        void SetNodeManager(MosaicNodeManager *nodeManager);
        void SetLteV2xHelper(Ptr<LteV2xHelper> lteV2xHelper);
        void SetSockets(void);
        void TransmitPacket(uint32_t protocolID, uint32_t msgID, uint32_t payLength, Ipv4Address address);
        void Enable(void);
        void Disable(void);
        virtual void DoDispose(void);

    private:
        void Receive(Ptr<Socket> socket);

        Ptr<Socket> m_host;
        Ptr<Socket> m_sink;
        Ptr<LteV2xHelper> m_lteV2xHelper;
        MosaicNodeManager *m_nodeManager;
        bool m_active;
        uint16_t m_port;
        // ... other member variables
    };

} // namespace ns3

#endif /* MOSAIC_LTE_PROXY_APP_H */
