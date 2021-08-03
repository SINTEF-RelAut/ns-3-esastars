//
// Created by seyedali on 19.07.21.
//

#ifndef NS_3_BEACONING_SIMULATOR_PATH_SERVER_H
#define NS_3_BEACONING_SIMULATOR_PATH_SERVER_H

#include <unordered_map>
#include <vector>
#include <map>

#include "ns3/nstime.h"

#include "src/SCION/headers/path_segment.h"
#include "src/SCION/headers/scion_packet.h"
#include "src/SCION/headers/scion_capable_node.h"


namespace ns3 {
    class PathServer : public SCIONCapableNode {
    public:
        PathServer(uint32_t system_id, uint16_t isd_number, uint16_t as_number, host_addr_t local_address,
                   double latitude, double longitude, Ptr<Node> AS ) :
                   SCIONCapableNode(system_id, isd_number, as_number, local_address, latitude, longitude, AS){}


        void RegisterCorePathSegment (PathSegment& pathSegment, std::string key);
        void RegisterUpPathSegment (PathSegment& pathSegment, std::string key);
        void RegisterDownPathSegment (PathSegment& pathSegment, std::string key);

    private:
        registered_path_segs_dataset_t registered_core_segments;
        registered_path_segs_dataset_t registered_up_segments;
        registered_path_segs_dataset_t registered_down_segments;

        cached_path_segs_dataset_t cached_core_segments;
        cached_path_segs_dataset_t cached_up_segments;
        cached_path_segs_dataset_t cached_down_segments;


        void process_received_packet(uint16_t local_if, SCIONPacket* packet) override;

        void process_local_host_request_for_path (path_segment_type path_type, ia_t src_ia, ia_t dst_ia, host_addr_t host_addr);

        void send_registered_path_to_local_host(host_addr_t host_addr, path_segment_type path_type, ia_t src_ia, ia_t dst_ia, const reg_path_segs_to_one_as_t*);
    };
}

#endif //NS_3_BEACONING_SIMULATOR_PATH_SERVER_H
