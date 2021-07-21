//
// Created by seyedali on 19.07.21.
//

#ifndef NS_3_BEACONING_SIMULATOR_PATH_SERVER_H
#define NS_3_BEACONING_SIMULATOR_PATH_SERVER_H

#include <unordered_map>
#include <vector>
#include <map>
#include "ns3/nstime.h"
namespace ns3 {

    typedef uint32_t ia_t;
    typedef uint64_t src_dst_ia_t;

#define MAKE_IA(isd, as) ((((uint32_t) isd) << 16) | ((uint32_t) as))
#define GET_ISDN(input) ((uint16_t) ((input) >> 16))
#define GET_ASN(input)  ((uint16_t) ((input) & 0x0000ffff))

#define MAKE_IA_PAIR(src_ia, dst_ia) ((((uint64_t) src_ia) << 32)| ((uint64_t) dst_ia))
#define MAKE_SRC_DST_PAIR(src_isd, src_as, dst_isd, dst_as) ((((uint64_t) src_isd) << 48)| (((uint64_t) src_as) << 32) | (((uint64_t) dst_isd) << 16) | ((uint64_t) dst_as))
#define GET_SRC_ISD(input) ((uint16_t) ((input) >> 48))
#define GET_SRC_AS(input) ((uint16_t) (((input) & 0x0000ffff00000000) >> 32))
#define GET_DST_ISD(input) ((uint16_t) (((input) & 0x00000000ffff0000) >> 16))
#define GET_DST_AS(input) ((uint16_t) ((input) & 0x000000000000ffff))


    enum path_segment_type  {
        CORE_SEG = 0, UP_SEG = 1, DOWN_SEG = 2
    };
    class SCION_AS;

    struct PathSegment {
        ia_t originator;
        uint16_t initiation_time;
        uint16_t expiration_time;

        bool reverse;

        std::vector<uint64_t> hops;

        PathSegment(){}

        PathSegment (PathSegment& pathSegment) :
        originator(pathSegment.originator),
        initiation_time(pathSegment.initiation_time),
        expiration_time(pathSegment.expiration_time),
        hops(pathSegment.hops) {}
    };

    typedef std::unordered_map<std::string, PathSegment*> reg_path_segs_to_one_as_t;
    typedef std::multimap<uint16_t, PathSegment*> cached_path_segs_per_src_dst_t;

    typedef std::unordered_map<ia_t, cached_path_segs_per_src_dst_t*> cached_path_segs_per_dst_t;

    typedef std::unordered_map<ia_t, reg_path_segs_to_one_as_t*> registered_path_segs_dataset_t;
    typedef std::unordered_map<ia_t, cached_path_segs_per_dst_t*> cached_path_segs_dataset_t;

    class PathServer {
    public:
        void RegisterCorePathSegment (PathSegment& pathSegment, std::string key);
        void RegisterUpPathSegment (PathSegment& pathSegment, std::string key);
        void RegisterDownPathSegment (PathSegment& pathSegment, std::string key);

        void ReceiveRequestForPathSegmentFromHost (path_segment_type path_type, ia_t src_ia, ia_t dst_ia, uint32_t host_addr, uint32_t req_id);
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
