/**
 * @file beacon.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 */

#ifndef SCION_SIMULATOR_BEACON_H
#define SCION_SIMULATOR_BEACON_H

#include <set>
#include <string>
#include <utility>
#include <vector>
#include <unordered_set>

#include "src/SCION/headers/path_segment.h"

namespace ns3 {

#define BEACON_HEADER_SIZE 86
#define BEACON_HOP_SIZE 132
#define ORIGINATOR(beacon) (UPPER_16_BITS(beacon.the_path.front()))
#define ORIGINATOR_PTR(beacon) (UPPER_16_BITS(beacon->the_path.front()))
#define DST_AS(beacon) (beacon.beacon_direction == beacon_direction_t::PULL_BASED \
    ? beacon.optimization_target->target_as : UPPER_16_BITS(beacon.the_path.front()))

#define DST_AS_PTR(beacon) (beacon->beacon_direction == beacon_direction_t::PULL_BASED \
                                    ? beacon->optimization_target->target_as : UPPER_16_BITS(beacon->the_path.front()))

    typedef long double ld;

    typedef uint64_t link_information; // sender_as   eg_if   receiver_as   ing_if
                                       // <-16bit-> <-16bit-> <--16bit-->  <-16bit->

    typedef std::vector<link_information> path;
    typedef std::vector<uint16_t> isd_path;

    enum static_info_type_t { LATENCY = 0, BW = 1, CO2 = 2, FORBIDDEN_EDGES = 3 };
    typedef std::map<static_info_type_t, float> static_info_extension_t;

    typedef std::map<static_info_type_t, float> optimization_criteria_t;
    enum optimization_direction_t { FORWARD = 0, BACKWARD = 1, SYMMETRIC = 2 };
    typedef uint16_t target_as_t;
    typedef uint16_t target_id_t;
    typedef uint16_t target_if_group_t;

    struct optimization_target_t {
        const target_id_t target_id;
        const optimization_criteria_t criteria;
        const optimization_direction_t direction;
        const target_as_t target_as;
        const target_if_group_t target_if_group;
        const uint16_t no_beacons_per_optimization_target;
        const std::unordered_map<uint16_t, std::unordered_set<uint16_t>*> * const set_of_forbidden_edges;

        optimization_target_t (target_id_t target_id, optimization_criteria_t criteria, optimization_direction_t direction,
                              target_as_t target_as, target_if_group_t target_if_group,
                              uint16_t no_beacons_per_optimization_target,
                              const std::unordered_map<uint16_t, std::unordered_set<uint16_t>*> *set_of_forbidden_edges) :
              target_id (target_id), criteria(std::move(criteria)), direction (direction), target_as(target_as),
              target_if_group(target_if_group), no_beacons_per_optimization_target(no_beacons_per_optimization_target),
              set_of_forbidden_edges(set_of_forbidden_edges){}
    };

    enum beacon_direction_t { PUSH_BASED = 0, PULL_BASED = 1 };

    struct Beacon {
        static_info_extension_t static_info_extension;

        const optimization_target_t *optimization_target;

        beacon_direction_t beacon_direction;

        uint16_t initiation_time;
        uint16_t expiration_time;

        uint16_t next_initiation_time;
        uint16_t next_expiration_time;

        bool is_new;
        bool is_valid;

        path the_path;

        std::string key;

        isd_path the_isd_path;

        Beacon(static_info_extension_t &static_info_extension, const optimization_target_t *optimization_target,
               beacon_direction_t beacon_direction, uint16_t i, uint16_t e, uint16_t nxt_i, uint16_t nxt_e, bool n,
               bool v, path &p, std::string &k, isd_path &isdp)
            : static_info_extension(static_info_extension), optimization_target(optimization_target),
              beacon_direction(beacon_direction), initiation_time(i), expiration_time(e), next_initiation_time(nxt_i),
              next_expiration_time(nxt_e), is_new(n), is_valid(v), the_path(p), key(k), the_isd_path(isdp)

        {}

        void ExtractPathSegmentFromPushBasedBeacon(PathSegment &pathSegment) const;

        void ExtractPathSegmentFromPullBasedBeacon(PathSegment &pathSegment) const;
    };
} // namespace ns3
#endif //SCION_SIMULATOR_BEACON_H
