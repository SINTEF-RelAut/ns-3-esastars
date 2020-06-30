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
/** Convenience */
typedef long double ld;
/**
 * @brief Holds 0:as_no, 1:egress_intf_no, 2:remote_as_no and 3:remote_ingress_intf_no.
 *
 * We use a small int to store some identifying information on the links used in the beacons.
 * Expected order of information:
 *
 *  - link_info[0] = AS_number
 *  - link_info[1] = egress_interface_number;
 *  - link_info[2] = remote_as_number;
 *  - link_info[3] = remote_ingress_interface_number;
*/
typedef uint16_t *link_information;
/**
 * @brief A path is a sequence of links.
 * @see link_information
 */
typedef std::vector<link_information> path;

/**
 * @brief Beacons travel from AS to AS, and are appended with the information of the links they
 * traverse. This is how paths get discovered by the ASes in SCION.
 *
 * Beacons are propagated periodically and do expire.
 */
struct beacon {
    int64_t initiation_time, expiration_time;
    /** Internal helpers.*/
    int64_t next_initiation_time, next_expiration_time;
    /** Aggregators holding information about the path latency and bandwidth. */
    ld latency_stat, bwd_stat;
    /** @see path */
    path *the_path;
    /** For performant search in the beacon store of the nodes @see SCION_Node.path_map_to_beacon.*/
    std::string key; // TODO: Maybe this should be a member function instead?
    /** Internal helper.*/
    bool is_new;
    /** Internal helper.*/
    bool is_valid;
};
#endif //SCION_BEACONING_SIMMULATOR_BEACON_H
