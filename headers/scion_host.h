//
// Created by seyedali on 19.07.21.
//

#ifndef NS_3_BEACONING_SIMULATOR_SCION_HOST_H
#define NS_3_BEACONING_SIMULATOR_SCION_HOST_H

#include <unordered_map>
#include "ns3/nstime.h"

#include "src/SCION/headers/path_segment.h"
#include "src/SCION/headers/path_server.h"
#include "src/SCION/headers/scion_packet.h"

namespace ns3 {

    class SCION_AS;
    class PathServer;

    class SCIONHost {

    public:
        SCIONHost(Ptr<SCION_AS> node, uint32_t local_address, double latitude, double longitude) : node(node), local_address(local_address),  latitude (latitude), longitude (longitude) {}

        void ReceiveRegisteredPathSegments (path_segment_type path_type, ia_t src_ia, ia_t dst_ia, reg_path_segs_to_one_as_t* path_segments);
        void ReceiveCachedPathSegments (path_segment_type path_type, ia_t src_ia, ia_t dst_ia, cached_path_segs_per_dst_t* path_seg);
        void SendArbitraryPacket(ia_t dst_ia, uint32_t host_address);
    private:

        double  latitude;
        double  longitude;

        Ptr<SCION_AS> node;

        uint32_t local_address;

        uint32_t unique_path_req_id;


        cached_path_segs_dataset_t cached_up_path_segments;
        cached_path_segs_dataset_t cached_core_path_segments;
        cached_path_segs_dataset_t cached_down_path_segments;

        void remove_expired_segments();
        void search_in_cached_segments(ia_t dst_ia, std::vector<PathSegment*>& path);
        void request_for_path_segments(ia_t dst_ia);
        void send_request_for_path_segments (path_segment_type path_type, ia_t src_ia, ia_t dst_ia);

        void cache_path_segment (path_segment_type path_type, ia_t src_ia, ia_t dst_ia, PathSegment* path_seg);

        void try_sending(SCIONPacket packet, uint16_t count);

        void send_packet(SCIONPacket packet);

        void construct_path();
    };
}

#endif //NS_3_BEACONING_SIMULATOR_SCION_HOST_H
