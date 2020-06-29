/**
 * @file scion_node.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @brief Defines SCION_Node and its associated constants and type-definitions.
 *
 */

// TODO: Should this really be here? Or is that something we wanna customize on the node?
// TODO: Also why not use constants?
#define FIXED_BEACONS_NUMBER_TO_SEND 5
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

typedef std::unordered_set<beacon *> beacons_with_equal_length;
typedef std::map<uint16_t, beacons_with_equal_length *> equal_as_beacons_sorted_by_length;

// typedefs to reduce the number of arguments in constructor
// beaconing_period, expiration_period
typedef std::pair<ns3::Time, int64_t> simulator_params;
// 0:latency_coef, 1:bandwidth_coef, 2:AS_level_diversity_coef, 3:link_level_diversity_coef
typedef std::tuple<ld, ld, ld, ld> coefficients;

class SCION_Node : public ns3::Node { // TODO: How about aggregating instead?

public:
    //AS properties
    // TODO: Possibility for some memory optimizations with respect to cache if we think about which values
    // are used together most often -> reorder
    uint16_t as_number;
    int64_t now;
    ns3::Time beaconing_period;
    int64_t expiration_period;
    ld latency_coef, bandwidth_coef, AS_level_diversity_coef, link_level_diversity_coef;
    int32_t AS_max_bwd;

    // Interfaces Properties *****************************************************************************************************
    // TODO: Is this enum in the right place?
    enum neighbour_relation {CORE = 0, PEER = 1, CUSTOMER = 2, PROVIDER = 3};
    std::vector<uint16_t> neighbors;
    std::unordered_map<uint16_t, std::vector<std::pair<uint16_t, neighbour_relation>>> interfaces_per_neighbor_as;

    std::vector<std::pair<ld, ld> > interfaces_coordinates;
    std::vector<std::vector<ld> > intra_as_latencies;
    std::vector<int32_t> inter_as_bwds;

    // beacon store structures ***************************************************************************************************
    std::unordered_map<uint16_t, equal_as_beacons_sorted_by_length *> beacon_store;
    std::unordered_map<uint16_t, std::multimap <ld, beacon* >* > beacons_sorted_by_score;
    std::unordered_map<std::string, beacon*> path_map_to_beacon;// See beacon.h key
    // helper structures ********************************************************************************************************
    std::unordered_map<uint16_t, uint64_t> next_round_valid_beacons_count_per_src_as;

    // Beaconing Algorithm ***************************************************************************************************************
    BeaconingStrategy* strategy;

    // statistics ***************************************************************************************************************
    std::unordered_map<uint16_t, uint64_t> valid_beacons_count_per_src_as;
    std::unordered_map<int64_t, std::vector<uint32_t> > bytes_sent_per_interface_per_period;

    SCION_Node(uint16_t as_number, uint32_t system_id, coefficients coefs, const simulator_params &periods, BeaconingStrategy* strategy) :
    Node(   system_id), as_number(as_number), beaconing_period(periods.first), expiration_period(periods.second),
    latency_coef(std::get<0>(coefs)), bandwidth_coef(std::get<1>(coefs)), AS_level_diversity_coef(std::get<2>(coefs)),
    link_level_diversity_coef(std::get<3>(coefs)), strategy(strategy){}

    virtual ~SCION_Node() {}

    void DoInitializations();

    virtual void CoreBeaconing() = 0;

    virtual void IntraISDBeaconing() = 0;

    virtual void ProcessReceivedBeacons(uint16_t src_as_no, uint16_t ingress_if, beacon* the_beacon) = 0;

    void FinalPathEvaluation(std::map<ld, uint64_t> &satisfaction_stat,
                             std::map<ld, uint64_t> &AS_level_diversity_stat,
                             std::map<ld, uint64_t> &link_level_diversity_stat);

protected:
    void UpdateTimeAndStats();
    std::unordered_map<uint16_t, std::vector<uint16_t>> GetValidInterfaces(SCION_Node::neighbour_relation rel);
private:
    std::pair<ld, ld> calculate_final_diversity_scores(beacon *the_beacon);
};
#endif //SCION_BEACONING_SIMMULATOR_SCION_NODE_H
