//
// Created by seyedali on 28.07.21.
//

#ifndef NS_3_BEACONING_SIMULATOR_BORDER_ROUTER_H
#define NS_3_BEACONING_SIMULATOR_BORDER_ROUTER_H

#include "ns3/network-module.h"
#include "ns3/node.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/nstime.h"

#include "src/SCION/headers/scion_capable_node.h"

namespace ns3 {
    class SCION_AS;
    class SCIONHost;

    class BorderRouter : public SCIONCapableNode {
    public:
        BorderRouter(uint32_t system_id, uint16_t isd_number, uint16_t as_number, uint32_t local_address,
                     double latitude, double longitude) :
                     SCIONCapableNode(system_id, isd_number, as_number, local_address, latitude, longitude)

        {
            processing_delay = MicroSeconds(10);
            queueing_delay = Time(0);
        }

    private:
        void process_received_packet(uint16_t local_if, SCIONPacket& packet) override;

        std::unordered_set<uint16_t> interfaces_to_local_as;
        std::unordered_set<uint16_t> interfaces_to_remote_as;


    };
}
#endif //NS_3_BEACONING_SIMULATOR_BORDER_ROUTER_H
