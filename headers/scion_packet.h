//
// Created by seyedali on 19.07.21.
//

#ifndef NS_3_BEACONING_SIMULATOR_SCION_PACKET_H
#define NS_3_BEACONING_SIMULATOR_SCION_PACKET_H
#include "src/SCION/headers/path_server.h"
#include "ns3/nstime.h"
namespace ns3 {
    typedef uint32_t host_addr;

    struct SCIONPacket {
        std::vector<PathSegment*> path;

        bool path_reversed;

        Time timestamp;

        ia_t src_ia;
        ia_t dst_ia;

        host_addr src_host;
        host_addr dst_host;

        uint8_t* payload;

    };
}
#endif //NS_3_BEACONING_SIMULATOR_SCION_PACKET_H
