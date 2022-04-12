/**
 * @file beacon.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 */

#include "src/SCION/headers/beaconing/beacon.h"
#include "src/SCION/headers/externs.h"
#include "src/SCION/headers/utils.h"
namespace ns3 {
    void Beacon::ExtractPathSegment(PathSegment &pathSegment) {
        pathSegment.initiation_time = next_initiation_time;
        pathSegment.expiration_time = next_expiration_time;

        pathSegment.originator = (((uint32_t) the_isd_path.at(0)) << 16) | (((uint32_t) UPPER_16_BITS(the_path.at(0))));

        uint64_t previous_hop = 0;
        bool last_hop = true;
        for (std::vector<uint64_t>::reverse_iterator hop = the_path.rbegin(); hop != the_path.rend(); ++hop) {
            uint16_t ingress = 0;
            uint16_t egress = 0;
            uint16_t as = 0;

            if (last_hop) {
                as = SECOND_LOWER_16_BITS(*hop);
                ingress = LOWER_16_BITS(*hop);
                last_hop = false;
            } else {
                as = UPPER_16_BITS(previous_hop);
                egress = SECOND_UPPER_16_BITS(previous_hop);
                ingress = LOWER_16_BITS(*hop);
            }

            uint16_t isd = as_to_isd_map.at(as);
            previous_hop = *hop;
            uint64_t hop_field = (((uint64_t) isd) << 48) | (((uint64_t) as) << 32) | (((uint64_t) ingress) << 16) |
                                 ((uint64_t) egress);
            pathSegment.hops.push_back(hop_field);
        }

        uint16_t egress = SECOND_UPPER_16_BITS(previous_hop);
        uint16_t ingress = 0;
        uint16_t as = UPPER_16_BITS(previous_hop);
        uint16_t isd = as_to_isd_map.at(as);

        uint64_t hop_field =
                (((uint64_t) isd) << 48) | (((uint64_t) as) << 32) | (((uint64_t) ingress) << 16) | ((uint64_t) egress);
        pathSegment.hops.push_back(hop_field);
    }
} // namespace ns3