/**
 * @file scion_node.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @brief Defines SCION_Node and its associated constants and type-definitions.
 *
 */

// TODO: Should this really be here? Or is that something we wanna customize on the node?
// TODO: Also why not use constants?
/**
 * @brief Steers how many beacons are maximally disseminated to each neighbour per fixed source AS.
 */
#define FIXED_BEACONS_NUMBER_TO_SEND 5
/**
 * @brief Steers how many beacons are maximally stored on each node per fixed source AS.
 */
#define FIXED_BEACONS_NUMBER_TO_STORE 50

#ifndef SCION_BEACONING_SIMMULATOR_SCION_NODE_H
#define SCION_BEACONING_SIMMULATOR_SCION_NODE_H
#include "beacon.h"
#include "ns3/network-module.h"
#include "ns3/node.h"
#include <unordered_set>
#include <unordered_map>
// Forward declaration because of circular dependency
// TODO: Could implement a Factory to get rid of this => low priority
class BeaconingStrategy;

/** @brief Holds a set of beacons with constant length.*/
typedef std::unordered_set<beacon *> beacons_with_equal_length;

/**
 * @brief Holds beacons originating at a fixed source AS indexable by their hop count.
 */
typedef std::map<uint16_t, beacons_with_equal_length *> equal_as_beacons_sorted_by_length;

/**
 * @brief Holds 0:beaconing_period, 1:expiration_period.
 *
 * Used to reduce the number of parameters we need to pass into the ns3::CreateObject constructor wrapper.
 *
 * Expected order:
 * - beaconing_period
 * - expiration_period
 */
typedef std::pair<ns3::Time, int64_t> simulator_params;

/**
 * @brief Holds 0:latency_coef, 1:bandwidth_coef, 2:AS_level_diversity_coef, 3:link_level_diversity_coef
 *
 * Used to reduce the number of parameters we need to pass into the ns3::CreateObject constructor wrapper.
 *
 * Expected order:
 * - latency_coef
 * - bandwidth_coef
 * - AS_level_diversity_coef
 * - link_level_diversity_coef
 */
typedef std::tuple<ld, ld, ld, ld> coefficients;

/**
 * @brief This is the base definition of a SCION enabled Node. A SCION_Node models an Autonomous System.
 */
class SCION_Node : public ns3::Node { // TODO: How about aggregating instead?

public:
    //AS properties
    // TODO: Possibility for some memory optimizations with respect to cache if we think about which values
    // are used together most often -> reorder
    /** @brief The autonomous system number of this node. */
    uint16_t as_number;
    /** @brief The current simulator time. */
    int64_t now;
    /** @brief Periodicity of beaconing. */
    ns3::Time beaconing_period;
    /** @brief Expiration time of beacon. */
    int64_t expiration_period;
    /** @brief AS preferences. */
    ld latency_coef, bandwidth_coef, AS_level_diversity_coef, link_level_diversity_coef;
    /** @brief Largest amount of bandwidth found on any border router link. */
    int32_t AS_max_bwd;

    // Interfaces Properties *****************************************************************************************************
    // TODO: Is this enum in the right place?

    /**
     * @brief Different types of links.
     *
     * In typical BGP-enabled internet topologies, there are Peer, and Customer links. The Provider type was introduced
     * to model the reverse directionality of a customer link, the Core type is found between Core-ASes in SCION-topologies.
     */
    enum neighbour_relation {CORE = 0, PEER = 1, CUSTOMER = 2, PROVIDER = 3};
    /** @brief Holds the AS numbers of all the neighbours of the node*/
    std::vector<uint16_t> neighbors;
    /** @brief Holds a mapping of AS numbers and their connected interfaces & relations to this node. */
    std::unordered_map<uint16_t, std::vector<std::pair<uint16_t, neighbour_relation>>> interfaces_per_neighbor_as;
    /** @brief Holds the coordinates of the border router locations between ASes.
     *
     * The pair of border routers are assumed to be in close proximity (same room) which is why we do not model
     * any latency between them.
     */
    std::vector<std::pair<ld, ld>> interfaces_coordinates;
    /** @brief Holds the estimated latencies between the border routers inside this AS.*/
    std::vector<std::vector<ld>> intra_as_latencies;
    /** @brief Holds the bandwidths of the links between border routers of ASes. */
    std::vector<int32_t> inter_as_bwds;

    // beacon store structures ***************************************************************************************************
    /** @brief Pointers to all the beacons indexable by their source AS and their hop count.*/
    std::unordered_map<uint16_t, equal_as_beacons_sorted_by_length *> beacon_store;
    /** @brief Pointers to all the beacons indexable by their source AS and score.
     *
     * The second level of this structure is iterable by the beacon score in ascending order.
     * */
    std::unordered_map<uint16_t, std::multimap <ld, beacon* >* > beacons_sorted_by_score;
    /** @brief Pointers to all the beacons indexable by their key.
     *
     * This is done for efficiency. @see key
     * */
    std::unordered_map<std::string, beacon*> path_map_to_beacon;
    // helper structures ********************************************************************************************************
    // TODO
    std::unordered_map<uint16_t, uint64_t> next_round_valid_beacons_count_per_src_as;

    // Beaconing Algorithm ***************************************************************************************************************
    /** @brief Pointer to the BeaconingStrategy used to execute the correct beaconing behaviour.*/
    BeaconingStrategy* strategy;

    // statistics ***************************************************************************************************************
    // TODO
    std::unordered_map<uint16_t, uint64_t> valid_beacons_count_per_src_as;
    /** @brief Collects how many bytes would have been sent over which interface for every beacon sent in an epoch.
     *
     * The outer map structure is indexed by time. Then the vector index corresponds to the interface number.
     * */
    std::unordered_map<int64_t, std::vector<uint32_t> > bytes_sent_per_interface_per_period;

    SCION_Node(uint16_t as_number, uint32_t system_id, coefficients coefs, const simulator_params &periods, BeaconingStrategy* strategy) :
    Node(   system_id), as_number(as_number), beaconing_period(periods.first), expiration_period(periods.second),
    latency_coef(std::get<0>(coefs)), bandwidth_coef(std::get<1>(coefs)), AS_level_diversity_coef(std::get<2>(coefs)),
    link_level_diversity_coef(std::get<3>(coefs)), strategy(strategy){}

    virtual ~SCION_Node() {}

    /**
     * @brief Initializes the intra_as_latencies and the as_max_bw fields.
     */
    void DoInitializations();

    /**
     * @brief Called to initiate CoreBeaconing.
     *
     * Must be overwritten by descendants of SCION_Node.
     */
    virtual void CoreBeaconing() = 0;

    /**
     * @brief Called to initiate IntraISDBeaconing.
     *
     * Must be overwritten by descendants of SCION_Node.
     */
    virtual void IntraISDBeaconing() = 0;

    /**
     * @brief Called to initiate the processing of beacons received in the last beaconing period.
     *
     * Must be overwritten by descendants of SCION_Node.
     */
    virtual void ProcessReceivedBeacons(uint16_t src_as_no, uint16_t ingress_if, beacon* the_beacon) = 0;

    /**
     * @brief Iterates over all the beacons and fills the provided structures with the distribution of achieved scores.
     */
    void FinalPathEvaluation(std::map<ld, uint64_t> &satisfaction_stat,
                             std::map<ld, uint64_t> &AS_level_diversity_stat,
                             std::map<ld, uint64_t> &link_level_diversity_stat);

protected:
    /**
     * @brief Prints the number of discovered ASes so far, updates the node time and initializes some memory for bandwidth statistics.
     */
    void UpdateTimeAndStats();

    /**
     * @brief Returns all interfaces on this node which had the passed relation.
     */
    std::unordered_map<uint16_t, std::vector<uint16_t>> GetValidInterfaces(SCION_Node::neighbour_relation rel);

private:
    /**
     *  @brief Returns the average as-level diversity and link-level diversity scores of the passed beacon compared to all other beacons the node has which
     *  originated at the same source as number.
     */
    std::pair<ld, ld> calculate_final_diversity_scores(beacon *the_beacon);
};
#endif //SCION_BEACONING_SIMMULATOR_SCION_NODE_H
