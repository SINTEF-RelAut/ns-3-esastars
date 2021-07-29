//
// Created by seyedali on 19.07.21.
//

#ifndef NS_3_BEACONING_SIMULATOR_SCION_PACKET_H
#define NS_3_BEACONING_SIMULATOR_SCION_PACKET_H

#include "ns3/nstime.h"
#include "ns3/object.h"

#include "src/SCION/headers/path_segment.h"

namespace ns3 {
    typedef uint16_t host_addr_t;
    typedef uint32_t packet_id_t;

    struct SCIONPacket {
        Time timestamp;

        std::vector<const PathSegment*> path;
        uint8_t* payload;

        packet_id_t id;

        ia_t src_ia;
        ia_t dst_ia;

        host_addr_t src_host;
        host_addr_t dst_host;

        uint16_t curr_inf;
        uint16_t cur_hopf;

        // The index of cross overs in each segment
        std::vector<uint8_t> shortcut_hopfs;

        bool path_reversed;

    };
}
#endif //NS_3_BEACONING_SIMULATOR_SCION_PACKET_H
