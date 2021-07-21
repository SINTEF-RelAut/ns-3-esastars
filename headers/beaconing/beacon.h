/**
 * @file beacon.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @brief Defines the beacon structure and its associated type-definitions.
 */

#ifndef SCION_BEACONING_SIMMULATOR_BEACON_H
#define SCION_BEACONING_SIMMULATOR_BEACON_H

#include <string>
#include <vector>

#include "src/SCION/headers/path_segment.h"

namespace ns3 {
/**
 * @brief in bytes
 */
#define BEACON_HEADER_SIZE 70

/**
 * @brief in bytes
 */
#define BEACON_HOP_SIZE 330

typedef long double ld;

/**
 * @brief a 64-bit integer to store information of a path segment
 * The most significant 16 bits represent sender AS
 * The next 16 bits represent egress interface
 * The next 16 bits represent receiver AS
 * The least significant 16 bits represent receiver ingress interface

*/
typedef uint64_t link_information;

/**
 * @brief A path is a sequence of links.
 * @see link_information
 */
typedef std::vector<link_information> path;

typedef std::vector<uint16_t> isd_path;

/**
 * @brief Beacons travel from AS to AS, and are appended with the information of the links they
 * traverse. This is how paths get discovered by the ASes in SCION.
 *
 * Beacons are propagated periodically and do expire.
 */
struct beacon
{
    /** @brief Aggregator holding information about the path latency. */
    float latency_stat;
    /** @brief Aggregator holding information about the path bandwidth. */
    float bwd_stat;
    /** @brief The initiation time of the beacon */
    uint16_t initiation_time;
    /** @brief The expiration time of the beacon */
    uint16_t expiration_time;
    /** @brief Internal helper.*/
    uint16_t next_initiation_time;
    /** @brief Internal helper.*/
    uint16_t next_expiration_time;
    /** @brief Internal helper.*/
    bool is_new;
    /** @brief Internal helper.
     *
     * For performance reasons, ns3's native send functionality is not used. Instead, if the remote ASes import
     * policy would keep the beacon in it's store, we directly write the beacon into its beacon store. These beacons
     * are marked invalid until the beaconing period is over, such that they are only disseminated in the next beaconing
     * period.
     *
     * @see GenerateBeaconAndSend
     * */
    bool is_valid;
    /** @see path */
    path the_path;

    /** @brief For performant search and traversal in the beacon store of the nodes
     *
     * @see SCION_Node.path_map_to_beacon.*/
    std::string key;

    isd_path the_isd_path;

    beacon (float l, float b, uint16_t i, uint16_t e, uint16_t nxt_i, uint16_t nxt_e, bool n,
            bool v, path p, std::string k, isd_path isdp)
        : latency_stat (l),
          bwd_stat (b),
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

    beacon (beacon& the_beacon) :
        latency_stat(the_beacon.latency_stat),
        bwd_stat(the_beacon.bwd_stat),
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
#endif //SCION_BEACONING_SIMMULATOR_BEACON_H
