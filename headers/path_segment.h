//
// Created by seyedali on 21.07.21.
//

#ifndef SCION_SIMULATOR_PATH_SEGMENT_H
#define SCION_SIMULATOR_PATH_SEGMENT_H

#include <map>
#include <unordered_map>
#include <vector>

namespace ns3 {
typedef uint32_t Ia_t;
typedef uint64_t SrcDstIa_t;

#define MAKE_IA(isd, as) ((((uint32_t) isd) << 16) | ((uint32_t) as))
#define GET_ISDN(input) ((uint16_t) ((input) >> 16))
#define GET_ASN(input) ((uint16_t) ((input) &0x0000ffff))

#define MAKE_IA_PAIR(src_ia, dst_ia) ((((uint64_t) src_ia) << 32) | ((uint64_t) dst_ia))
#define MAKE_SRC_DST_PAIR(src_isd, src_as, dst_isd, dst_as)                                    \
  ((((uint64_t) src_isd) << 48) | (((uint64_t) src_as) << 32) | (((uint64_t) dst_isd) << 16) | \
   ((uint64_t) dst_as))

#define GET_SRC_ISD(input) ((uint16_t) ((input) >> 48))
#define GET_SRC_AS(input) ((uint16_t) (((input) &0x0000ffff00000000) >> 32))
#define GET_DST_ISD(input) ((uint16_t) (((input) &0x00000000ffff0000) >> 16))
#define GET_DST_AS(input) ((uint16_t) ((input) &0x000000000000ffff))

#define GET_HOP_ISD(input) ((uint16_t) ((input) >> 48))
#define GET_HOP_AS(input) ((uint16_t) (((input) &0x0000ffff00000000) >> 32))
#define GET_HOP_IA(input) ((Ia_t) (((input) &0xffffffff00000000) >> 32))
#define GET_HOP_ING_IF(input) ((uint16_t) (((input) &0x00000000ffff0000) >> 16))
#define GET_HOP_EG_IF(input) ((uint16_t) ((input) &0x000000000000ffff))
#define GET_HOP_AS_ING(input) ((uint32_t) (((input) &0x0000ffffffff0000) >> 16))

enum PathSegmentType { coreSeg = 0, upSeg = 1, downSeg = 2 };

struct PathSegment
{
  Ia_t originator;
  uint16_t initiationTime;
  uint16_t expirationTime;

  bool reverse;

  std::vector<uint64_t> hops;

  PathSegment ()
  {
  }

  PathSegment (PathSegment &pathSegment)
      : originator (pathSegment.originator),
        initiationTime (pathSegment.initiationTime),
        expirationTime (pathSegment.expirationTime),
        reverse (pathSegment.reverse),
        hops (pathSegment.hops)
  {
  }
};

typedef std::unordered_map<std::string, PathSegment *> RegPathSegsToOneAs_t;
typedef std::multimap<uint16_t, const PathSegment *> CachedPathSegsPerSrcDst_t;

typedef std::unordered_map<Ia_t, CachedPathSegsPerSrcDst_t *> CachedPathSegsPerDst_t;

typedef std::unordered_map<Ia_t, RegPathSegsToOneAs_t *> RegisteredPathSegsDataset_t;
typedef std::unordered_map<Ia_t, CachedPathSegsPerDst_t *> CachedPathSegsDataset_t;
} // namespace ns3
#endif //SCION_SIMULATOR_PATH_SEGMENT_H
