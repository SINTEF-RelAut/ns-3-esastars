/**
 * @file beacon.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 */

#ifndef SCION_BEACONING_SIMULATOR_BEACON_H
#define SCION_BEACONING_SIMULATOR_BEACON_H

#include <string>
#include <vector>

#include "src/SCION/headers/path_segment.h"

namespace ns3 {

#define BEACON_HEADER_SIZE 86
#define BEACON_HOP_SIZE 132

typedef long double ld;

typedef uint64_t link_information; // sender_as   eg_if   receiver_as   ing_if
                                   // <-16bit-> <-16bit-> <--16bit-->  <-16bit->

typedef std::vector<link_information> path;
typedef std::vector<uint16_t> isd_path;

enum static_info_type_t {LATENCY = 0, BW = 1, CO2 = 2};

typedef std::map<static_info_type_t, float> static_info_extension_t;

struct Beacon
{
    static_info_extension_t static_info_extension;

    uint16_t initiation_time;
    uint16_t expiration_time;

    uint16_t next_initiation_time;
    uint16_t next_expiration_time;

    bool is_new;
    bool is_valid;

    path the_path;

    std::string key;

    isd_path the_isd_path;

    Beacon (static_info_extension_t& static_info_extension, uint16_t i, uint16_t e, uint16_t nxt_i, uint16_t nxt_e, bool n,
            bool v, path p, std::string k, isd_path isdp)
        : static_info_extension(static_info_extension),
          initiation_time (i),
          expiration_time (e),
          next_initiation_time (nxt_i),
          next_expiration_time (nxt_e),
          is_new (n),
          is_valid (v),
          the_path (p),
          key (k),
          the_isd_path(isdp)
    {
    }

    Beacon (Beacon& the_beacon) :
        static_info_extension(the_beacon.static_info_extension),
        initiation_time(the_beacon.initiation_time),
        expiration_time(the_beacon.expiration_time),
        next_initiation_time(the_beacon.next_initiation_time),
        next_expiration_time (the_beacon.next_expiration_time),
        is_new (the_beacon.is_new),
        is_valid(the_beacon.is_valid),
        the_path(the_beacon.the_path),
        key(the_beacon.key),
        the_isd_path(the_beacon.the_isd_path)
    {
    }

    void ExtractPathSegment(PathSegment &pathSegment);
};
}
#endif //SCION_BEACONING_SIMULATOR_BEACON_H
