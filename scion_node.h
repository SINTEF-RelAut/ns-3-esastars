//
// Created by chrissy on 10.06.20.
//

// TODO: Should this really be here? Or is that something we wanna customize on the node?
// TODO: Also why not use constants?
#define FIXED_BEACONS_NUMBER_TO_SEND 5
#define FIXED_BEACONS_NUMBER_TO_STORE 50

#ifndef SCION_BEACONING_SIMMULATOR_SCION_NODE_H
#define SCION_BEACONING_SIMMULATOR_SCION_NODE_H
//#include "ns3/core-module.h"
#include "beacon.h"
#include "beaconing_strategy.h"
#include "ns3/network-module.h"
//#include "ns3/point-to-point-helper.h"
//#include "ns3/point-to-point-net-device.h"
//#include "ns3/point-to-point-channel.h"
#include <unordered_set>

// TODO: Think about how to unify these?? Seyedali proposed to change all to set
typedef std::unordered_set<beacon *> beacons_received_from_same_as;
typedef std::unordered_map<uint16_t, beacons_received_from_same_as *> beacons_with_same_src_as;
//typedef std::vector<beacon *> beacons_with_equal_length;
//typedef std::map<uint16_t, beacons_with_equal_length *> beacons_with_same_src_as;

class SCION_Node : public ns3::Node { // TODO: How about aggregating instead?

public:

    //AS properties
    uint16_t as_number;
    int64_t now;
    Time beaconing_period;
    int64_t expiration_period;
    ld latency_coef, bandwidth_coef, AS_level_diversity_coef, link_level_diversity_coef;
    int32_t AS_max_bwd;

    // Interfaces Properties *****************************************************************************************************
    std::vector<uint16_t> neighbors;
    std::unordered_map<uint16_t, std::vector<uint16_t> > interfaces_per_neighbor_as;

    std::vector<std::pair<ld, ld> > interfaces_coordinates;
    std::vector<std::vector<ld> > intra_as_latencies;
    std::vector<int32_t> inter_as_bwds;

    // beacon store structures ***************************************************************************************************
    std::unordered_map<uint16_t, beacons_with_same_src_as *> beacon_store;
    std::unordered_map<uint16_t, std::multimap <ld, beacon* >* > beacons_sorted_by_score;
    std::unordered_map<std::string, beacon*> path_map_to_beacon;
    // TODO: Think about how to best unify this?
    //std::unordered_map<uint16_t, beacons_with_same_src_as *> beacon_store;
    //std::unordered_map<std::string, beacon *> path_map_to_beacon;
    // TODO: Is this the right spot? This would probably be better suited in a centralized "config" object
    int64_t expiration_period; // TODO: Initialize if you want to keep this on the node
    // helper structures ********************************************************************************************************
    std::unordered_map<uint16_t, uint64_t> next_round_valid_beacons_count_per_src_as;

    // statistics ***************************************************************************************************************
    BeaconingStrategy stragegy; // TODO: Initialize somewhere

    // statistics ***************************************************************************************************************
    std::unordered_map<uint16_t, uint64_t> valid_beacons_count_per_src_as;
    std::unordered_map<int64_t, std::vector<uint32_t> > bytes_sent_per_interface_per_period;

    SCION_Node(uint16_t as_number, uint32_t system_id, ld latency_coef, ld bandwidth_coef, ld AS_level_diversity_coef,
           ld link_level_diversity_coef, Time beaconing_period, int64_t expiration_period) : Node(
            system_id), as_number(as_number), latency_coef(latency_coef), bandwidth_coef(bandwidth_coef),
                                           AS_level_diversity_coef(AS_level_diversity_coef),
                                           link_level_diversity_coef(link_level_diversity_coef),
                                           beaconing_period(beaconing_period),
                                           expiration_period(expiration_period) {}

    void DoInitializations();

    virtual void CoreBeaconing() = 0;

    virtual void IntraISDBeaconing() = 0;

    void ProcessReceivedBeacons(uint16_t src_as_no, uint16_t ingress_if, beacon* the_beacon);

    void FinalPathEvaluation(std::map<ld, uint64_t> &satisfaction_stat,
                             std::map<ld, uint64_t> &AS_level_diversity_stat,
                             std::map<ld, uint64_t> &link_level_diversity_stat);

protected:
    virtual std::unordered_map<uint16_t, std::vector<uint16_t>> select_valid_interfaces() = 0;

private:
    std::pair<ld, ld> calculate_final_diversity_scores(beacon *the_beacon);
};
#endif //SCION_BEACONING_SIMMULATOR_SCION_NODE_H
