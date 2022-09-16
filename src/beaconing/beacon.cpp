/**
 * @file beacon.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 */

#include "src/SCION/headers/beaconing/beacon.h"
#include "src/SCION/headers/externs.h"
#include "src/SCION/headers/utils.h"
namespace ns3 {
void
Beacon::ExtractPathSegmentFromPushBasedBeacon (PathSegment &pathSegment) const
{
  pathSegment.initiationTime = nextInitiationTime;
  pathSegment.expirationTime = nextExpirationTime;

  pathSegment.originator =
      (((uint32_t) isdPath.at (0)) << 16) | (((uint32_t) UPPER_16_BITS (path.at (0))));

  uint64_t previousHop = 0;
  bool lastHop = true;
  for (std::vector<uint64_t>::const_reverse_iterator hop = path.rbegin ();
       hop != path.rend (); ++hop)
    {
      uint16_t ingress = 0;
      uint16_t egress = 0;
      uint16_t as = 0;

      if (lastHop)
        {
          as = SECOND_LOWER_16_BITS (*hop);
          ingress = LOWER_16_BITS (*hop);
          lastHop = false;
        }
      else
        {
          as = UPPER_16_BITS (previousHop);
          egress = SECOND_UPPER_16_BITS (previousHop);
          ingress = LOWER_16_BITS (*hop);
        }

      uint16_t isd = g_asToIsdMap.at (as);
      previousHop = *hop;
      uint64_t hopField = (((uint64_t) isd) << 48) | (((uint64_t) as) << 32) |
                           (((uint64_t) ingress) << 16) | ((uint64_t) egress);
      pathSegment.hops.push_back (hopField);
    }

  uint16_t egress = SECOND_UPPER_16_BITS (previousHop);
  uint16_t ingress = 0;
  uint16_t as = UPPER_16_BITS (previousHop);
  uint16_t isd = g_asToIsdMap.at (as);

  uint64_t hopField = (((uint64_t) isd) << 48) | (((uint64_t) as) << 32) |
                       (((uint64_t) ingress) << 16) | ((uint64_t) egress);
  pathSegment.hops.push_back (hopField);
}

void
Beacon::ExtractPathSegmentFromPullBasedBeacon (PathSegment &pathSegment) const
{
  pathSegment.initiationTime = nextInitiationTime;
  pathSegment.expirationTime = nextExpirationTime;

  pathSegment.originator = (((uint32_t) isdPath.back ()) << 16) |
                           (((uint32_t) SECOND_LOWER_16_BITS (path.back ())));

  uint64_t previousHop = 0;
  bool lastHop = true;
  for (std::vector<uint64_t>::const_iterator hop = path.begin (); hop != path.end (); ++hop)
    {
      uint16_t ingress = 0;
      uint16_t egress = 0;
      uint16_t as = 0;

      if (lastHop)
        {
          as = UPPER_16_BITS (*hop);
          ingress = SECOND_UPPER_16_BITS (*hop);
          lastHop = false;
        }
      else
        {
          as = SECOND_LOWER_16_BITS (previousHop);
          egress = LOWER_16_BITS (previousHop);
          ingress = SECOND_UPPER_16_BITS (*hop);
        }

      uint16_t isd = g_asToIsdMap.at (as);
      previousHop = *hop;
      uint64_t hopField = (((uint64_t) isd) << 48) | (((uint64_t) as) << 32) |
                           (((uint64_t) ingress) << 16) | ((uint64_t) egress);
      pathSegment.hops.push_back (hopField);
    }

  uint16_t egress = LOWER_16_BITS (previousHop);
  uint16_t ingress = 0;
  uint16_t as = SECOND_LOWER_16_BITS (previousHop);
  uint16_t isd = g_asToIsdMap.at (as);

  uint64_t hopField = (((uint64_t) isd) << 48) | (((uint64_t) as) << 32) |
                       (((uint64_t) ingress) << 16) | ((uint64_t) egress);
  pathSegment.hops.push_back (hopField);
}
} // namespace ns3