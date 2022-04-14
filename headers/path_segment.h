//
// Created by seyedali on 21.07.21.
//

#ifndef SCION_SIMULATOR_PATH_SEGMENT_H
#define SCION_SIMULATOR_PATH_SEGMENT_H

#include <map>
#include <unordered_map>
#include <vector>

namespace ns3 {
    typedef uint32_t ia_t;
    typedef uint64_t src_dst_ia_t;

#define MAKE_IA(isd, as) ((((uint32_t) isd) << 16) | ((uint32_t) as))
#define GET_ISDN(input) ((uint16_t) ((input) >> 16))
#define GET_ASN(input) ((uint16_t) ((input) &0x0000ffff))

#define MAKE_IA_PAIR(src_ia, dst_ia) ((((uint64_t) src_ia) << 32) | ((uint64_t) dst_ia))
#define MAKE_SRC_DST_PAIR(src_isd, src_as, dst_isd, dst_as)                                                            \
    ((((uint64_t) src_isd) << 48) | (((uint64_t) src_as) << 32) | (((uint64_t) dst_isd) << 16) | ((uint64_t) dst_as))

#define GET_SRC_ISD(input) ((uint16_t) ((input) >> 48))
#define GET_SRC_AS(input) ((uint16_t) (((input) &0x0000ffff00000000) >> 32))
#define GET_DST_ISD(input) ((uint16_t) (((input) &0x00000000ffff0000) >> 16))
#define GET_DST_AS(input) ((uint16_t) ((input) &0x000000000000ffff))

#define GET_HOP_ISD(input) ((uint16_t) ((input) >> 48))
#define GET_HOP_AS(input) ((uint16_t) (((input) &0x0000ffff00000000) >> 32))
#define GET_HOP_IA(input) ((ia_t) (((input) &0xffffffff00000000) >> 32))
#define GET_HOP_ING_IF(input) ((uint16_t) (((input) &0x00000000ffff0000) >> 16))
#define GET_HOP_EG_IF(input) ((uint16_t) ((input) &0x000000000000ffff))
#define GET_HOP_AS_ING(input) ((uint32_t) (((input) &0x0000ffffffff0000) >> 16))

    enum path_segment_type { CORE_SEG = 0, UP_SEG = 1, DOWN_SEG = 2 };

    struct PathSegment {
        ia_t originator;
        uint16_t initiation_time;
        uint16_t expiration_time;

        bool reverse;

        std::vector<uint64_t> hops;

        PathSegment() {}

        PathSegment(PathSegment &pathSegment)
            : originator(pathSegment.originator), initiation_time(pathSegment.initiation_time),
              expiration_time(pathSegment.expiration_time), reverse(pathSegment.reverse), hops(pathSegment.hops) {}
    };

    typedef std::unordered_map<std::string, PathSegment *> reg_path_segs_to_one_as_t;
    typedef std::multimap<uint16_t, const PathSegment *> cached_path_segs_per_src_dst_t;

    typedef std::unordered_map<ia_t, cached_path_segs_per_src_dst_t *> cached_path_segs_per_dst_t;

    typedef std::unordered_map<ia_t, reg_path_segs_to_one_as_t *> registered_path_segs_dataset_t;
    typedef std::unordered_map<ia_t, cached_path_segs_per_dst_t *> cached_path_segs_dataset_t;
} // namespace ns3
#endif //SCION_SIMULATOR_PATH_SEGMENT_H
