//
// Created by seyedali on 19.07.21.
//

#ifndef NS_3_BEACONING_SIMULATOR_SCION_PACKET_H
#define NS_3_BEACONING_SIMULATOR_SCION_PACKET_H

#include <unordered_set>
#include <ns3/node.h>

#include "ns3/nstime.h"
#include "ns3/object.h"

#include "src/SCION/headers/path_segment.h"

namespace ns3 {
    typedef uint16_t host_addr_t;
    typedef uint32_t packet_id_t;

    enum payload_type_t {
            EMPTY = 0, PATH_REQ_FROM_HOST = 1, REG_PATHS_FROM_LOCAL_PS = 2
    };

    struct PathReqFromHost {
        ia_t src_ia, dst_ia;
        path_segment_type seg_type;
    };

    struct RegPathsFromLocalPS {
        const reg_path_segs_to_one_as_t* registered_path_segments;
        ia_t src_ia, dst_ia;
        path_segment_type seg_type;

    };

    union Payload {
        PathReqFromHost path_req_from_host;
        RegPathsFromLocalPS registered_paths_from_local_ps;
    };

    struct SCIONPacket {
    public:
        Time timestamp;

        const Ptr<Node> packet_originator; // This field is used for memory management of packets

        std::vector<const PathSegment*> path;

        Payload payload;

        const packet_id_t id; // This field is used for memory management of packets

        ia_t src_ia, dst_ia;

        payload_type_t payload_type;

        host_addr_t src_host, dst_host;

        uint16_t curr_inf, cur_hopf;

        uint16_t size; // size in bytes

        // The index of cross overs in each segment
        std::vector<uint8_t> shortcut_hopfs;

        bool path_reversed;

        SCIONPacket(const Ptr<Node> packet_originator, packet_id_t id) :  packet_originator(packet_originator), id(id) {}

    };




}
#endif //NS_3_BEACONING_SIMULATOR_SCION_PACKET_H
