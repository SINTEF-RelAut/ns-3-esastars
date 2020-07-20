/**
 * @file scion_node.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see scion_node.h
 * @brief Defines the member functions of the SCION_Node.
 */

#include "../headers/scion_node.h"
#include "../headers/utils.h"
#include "../headers/beaconing_strategy.h"
#include "ns3/core-module.h"

/**
 * The intra as latencies are estimated by calculating the great circle latencies
 * based on the interface coordinates. Also initializes AS_max_bwd with the highest
 * bandwith found at any border router of this AS.
 *
 * @see calculate_great_circle_latency
 */
void SCION_Node::DoInitializations() {
    intra_as_latencies.resize(this->ns3::Node::GetNDevices());
    for (uint64_t i = 0; i < this->ns3::Node::GetNDevices(); ++i) {
        intra_as_latencies.at(i).resize(this->ns3::Node::GetNDevices());
    }

    for (uint32_t i = 0; i < this->ns3::Node::GetNDevices(); ++i) {
        for (uint32_t j = i + 1; j < this->ns3::Node::GetNDevices(); ++j) {
            intra_as_latencies.at(i).at(j) = calculate_great_circle_latency(interfaces_coordinates.at(i).first,
                                                                            interfaces_coordinates.at(i).second,
                                                                            interfaces_coordinates.at(j).first,
                                                                            interfaces_coordinates.at(j).second);
            intra_as_latencies.at(j).at(i) = intra_as_latencies.at(i).at(j); // TODO this could be half the size since symmetrical.
        }
    }

    AS_max_bwd = 0;
    for (auto const curr_bwd:inter_as_bwds) {
        if (curr_bwd > AS_max_bwd) {
            AS_max_bwd = curr_bwd;
        }
    }
}

/**
 * Calculates the link-level and as-level path diversity scores and a quality metric
 * (based on the latency and bandwidth coefficients in the beacons and the node) for each beacon.
 * Saves the numeric value and the distribution (frequency of occurrence of a certain score) in the passed maps.
 *
 * @see calculate_final_diversity_scores
 *
 * @param satisfaction_stat Is filled with the seen satisfaction scores for each beacon.
 * @param AS_level_diversity_stat Is filled with the AS level diversity scores seen for each beacon.
 * @param link_level_diversity_stat Is filled with the link level diversity scores seen for each beacon.
 */
void SCION_Node::FinalPathEvaluation(std::map<ld, uint64_t> &satisfaction_stat,
                                     std::map<ld, uint64_t> &AS_level_diversity_stat,
                                     std::map<ld, uint64_t> &link_level_diversity_stat) {
    for (auto const &the_beacon_pair:path_map_to_beacon) {
        beacon* the_beacon = the_beacon_pair.second;
        if (the_beacon->is_valid) {
            std::pair<ld, ld> diversity_scores = this->calculate_final_diversity_scores(the_beacon);
            ld AS_level_diversity_score = diversity_scores.first;
            ld link_level_diversity_score = diversity_scores.second;
            ld latency_score = 1 - the_beacon->latency_stat / 1000;
            ld bwd_score = the_beacon->bwd_stat / AS_max_bwd;

            ld score =
                    (latency_score * latency_coef + bwd_score * bandwidth_coef +
                     AS_level_diversity_score * AS_level_diversity_coef +
                     link_level_diversity_score * link_level_diversity_coef) /
                    (latency_coef + bandwidth_coef + AS_level_diversity_coef + link_level_diversity_coef);

            score = roundl(score * 1000) / 1000;
            AS_level_diversity_score = roundl(AS_level_diversity_score * 1000) / 1000;
            link_level_diversity_score = roundl(link_level_diversity_score * 1000) / 1000;


            if (satisfaction_stat.find(score) != satisfaction_stat.end()) {
                satisfaction_stat.at(score)++;
            } else {
                satisfaction_stat.insert(std::make_pair(score, 1));
            }

            if (AS_level_diversity_stat.find(AS_level_diversity_score) != AS_level_diversity_stat.end()) {
                AS_level_diversity_stat.at(AS_level_diversity_score)++;
            } else {
                AS_level_diversity_stat.insert(std::make_pair(AS_level_diversity_score, 1));
            }

            if (link_level_diversity_stat.find(link_level_diversity_score) != link_level_diversity_stat.end()) {
                link_level_diversity_stat.at(link_level_diversity_score)++;
            } else {
                link_level_diversity_stat.insert(std::make_pair(link_level_diversity_score, 1));
            }
        }
    }
}

/**
 * Prints the number of ASes that are reachable until now, updates the now field of the node to current simulator time and initializes the structure
 * which will be filled with the number of bytes sent on each interface during the next beaconing period.
 */
void SCION_Node::UpdateTimeAndStats(){
    // Print statistics until now to see some sense of progress
    std::cout << this->as_number << "\t" <<this->valid_beacons_count_per_src_as.size() << std::endl; // Print number of source ASes
    // TODO: Not sure if this should be here?
    this->now = ns3::Simulator::Now().ToInteger(ns3::Time::NS);
    // TODO: Since the # of neighbours is fixed, we could use an Array here instead of a vector for a bit less overhead & for cache optimisation (?).
    this->bytes_sent_per_interface_per_period.insert(std::make_pair(now, std::vector<uint32_t> (this->GetNDevices(), 0)));
}

/**
 * @param rel The filtering interface relation.
 * @return All interfaces with the provided relation indexable by the remote AS number.
 */
std::unordered_map<uint16_t, std::vector<uint16_t>> SCION_Node::GetValidInterfaces(SCION_Node::neighbour_relation rel){
    // Select the valid interfaces
    std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces_per_as = std::unordered_map<uint16_t, std::vector<uint16_t>>();
    for(auto &[neighbour_as_no, interfaces]: this->interfaces_per_neighbor_as){
        std::vector<uint16_t> valid_interfaces = std::vector<uint16_t>();
        for(auto &[intf_no, relation]: interfaces ){
            if (relation == rel){
               valid_interfaces.push_back(intf_no);
            }
        }
        valid_interfaces_per_as.insert({neighbour_as_no, valid_interfaces});
    }
    return valid_interfaces_per_as;
}

/**
 *  Iterates over all the beacons which originated at the same source AS then the passed beacon and computes
 *  the average AS level diversity and link level diversity scores.
 *  @see AS_level_jaccard_distance_between_two_paths
 *  @see link_level_jaccard_distance_between_two_paths
 *
 * @param the_beacon The beacon holding the path for which you would like to get the diversity scores.
 * @return Pair(Average AS-lvl diversity, Average link-lvl diversity) of the passed beacon.
 */
std::pair<ld, ld> SCION_Node::calculate_final_diversity_scores(beacon *the_beacon) {
    ld AS_level_diversity_score = 0;
    ld link_level_diversity_score = 0;
    int32_t counter = 0;
    uint16_t src_as = *the_beacon->the_path->at(0);
    equal_as_beacons_sorted_by_length *equal_scr_as_beacons = beacon_store.at(src_as);
    for (auto const &received_if_beacon_vector_pair : *equal_scr_as_beacons) {
        for (auto const &curr_beacon : *received_if_beacon_vector_pair.second) {
            if (curr_beacon != the_beacon) {
                AS_level_diversity_score += AS_level_jaccard_distance_between_two_paths(the_beacon,
                                                                                        curr_beacon);
                link_level_diversity_score += link_level_jaccard_distance_between_two_paths(the_beacon,
                                                                                            curr_beacon);
                counter++;
            }
        }
    }
    return (std::make_pair(AS_level_diversity_score / counter, link_level_diversity_score / counter));
}