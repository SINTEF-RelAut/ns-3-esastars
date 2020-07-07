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

/**
 * @brief in bytes
 *
 * This includes the standard header size (70 bytes) + the immediate bit used to indicate that a beacon
 * should be sent on in the same beaconing period instead of the next.
 */
#define BEACON_HEADER_SIZE 70 + 1

/**
 * @brief in bytes
 */
#define BEACON_HOP_SIZE 330

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
    /** @brief The initiation time of the beacon */
    int64_t initiation_time;
    /** @brief The expiration time of the beacon */
    int64_t expiration_time;
    /** @brief Internal helper.*/
    int64_t next_initiation_time;
    /** @brief Internal helper.*/
    int64_t next_expiration_time;
    /** @brief Aggregator holding information about the path latency. */
    ld latency_stat;
    /** @brief Aggregator holding information about the path bandwidth. */
    ld bwd_stat;
    /** @see path */
    path *the_path;
    /** @brief For performant search and traversal in the beacon store of the nodes
     *
     * @see SCION_Node.path_map_to_beacon.*/
    std::string key;
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
};
#endif //SCION_BEACONING_SIMMULATOR_BEACON_H
