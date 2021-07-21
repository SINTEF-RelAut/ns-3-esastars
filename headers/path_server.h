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

namespace ns3 {


    class SCION_AS;



    class PathServer {
    public:
        void RegisterCorePathSegment (PathSegment& pathSegment, std::string key);
        void RegisterUpPathSegment (PathSegment& pathSegment, std::string key);
        void RegisterDownPathSegment (PathSegment& pathSegment, std::string key);

        void ReceiveRequestForPathSegmentFromHost (path_segment_type path_type, ia_t src_ia, ia_t dst_ia, uint32_t host_addr);
    private:
        Ptr<SCION_AS> node;
        Time request_processing_delay;

        registered_path_segs_dataset_t registered_core_segments;
        registered_path_segs_dataset_t registered_up_segments;
        registered_path_segs_dataset_t registered_down_segments;

        cached_path_segs_dataset_t cached_core_segments;
        cached_path_segs_dataset_t cached_up_segments;
        cached_path_segs_dataset_t cached_down_segments;
    };
}

#endif //NS_3_BEACONING_SIMULATOR_PATH_SERVER_H
