/**
 * @file beaconing_strategy.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see beaconing_strategy.h
 * @brief Implements the member functions of the BeaconingStrategy.
 */

#include "ns3/point-to-point-net-device.h"
#include "../headers/beaconing_strategy.h"
#include "../headers/utils.h"
#include "ns3/ptr.h"
#include <omp.h>

namespace ns3 {
    void BeaconingStrategy::SetNode(Ptr<SCION_Node> the_node) {
        this->node = the_node;
    }

/**
 * @see GenerateBeaconAndSend
 * @param valid_interfaces The interfaces along to initiate the beacons.
 * @param node The node from where to initiate the beacons
 */
    void
    BeaconingStrategy::InitiateBeacons(SCION_Node::neighbour_relation relation) {
        uint32_t neighbors_cnt = node->neighbors.size();
        omp_set_num_threads(NUM_CORE);

#pragma omp parallel for
        for (uint32_t i = 0; i < neighbors_cnt; ++i) {
            if (node->neighbors.at(i).second != relation) {
                continue;
            }

            uint16_t remote_as_no = node->neighbors.at(i).first;
            const auto &interfaces = node->interfaces_per_neighbor_as.at(remote_as_no);
            for (auto const &self_egress_if_no : interfaces) {
                std::pair<uint16_t, Ptr<SCION_Node>>
                        remote_as_if_pair = node->GetRemoteAsInfo(self_egress_if_no);

                uint16_t remote_ingress_if_no = remote_as_if_pair.first;
                Ptr<SCION_Node> remote_as = remote_as_if_pair.second;

                GenerateBeaconAndSend(
                        NULL, self_egress_if_no, remote_ingress_if_no, remote_as, 0,
                        node->inter_as_bwds.at(self_egress_if_no), false, 0.0);
            }
        }
    }


/**
 * - Updates the structures keeping track of how many bytes were sent over each interface during one period. => This is done every time
 * no matter if the beacon will be discarded by the remote AS and therefore not written into its beacon store. The reason is that in the
 * real deployment, the beacon must in any case reach the remote AS before it can run its import policy to decide to discard it or not.
 * - Determines if the beacon needs to be disseminated immediately by checking if the source AS number is already present in the
 * remote ASes counter structures.
 * - Checks if this exact beacon has already been sent in a previous beaconing period by searching for the beacon key in the remote
 * ASes path_map. If it is known, simply updates the initiation and expiration times and returns.
 * - Checks if the remote AS is already storing to many beacons from the source AS that originated the beacon. If this is the case
 * the full beacon store is handled and the function returns.
 *
 * Note that if the remote_as would discard this beacon after running its import policy, it is not created to save simulator memory.
 *
 * - Generates the new beacon by appending the nodes AS information and sets the initiation and expiration times.
 * The beacon gets written directly into the remote ASes beacon store and the path_map. It also triggers
 * the update of any additional beacon store structure a specialized strategy might need.
 * - Finally, it schedules the processing of the received beacons in case they need to be disseminated immediately.
 *
 * @see HandleFullBeaconStore
 * @see UpdateSpecializedBeaconStore
 * @see ProcessReceivedBeacons
 *
 * @param old_beacon Either the beacon on which to base the new beacon on, or NULL if this node is initiating a beacon.
 * @param self_egress_if_no The interface number on which to send the beacon.
 * @param remote_ingress_if_no The remote ingress interface number of the receiving AS.
 * @param node The node which is sending the beacon.
 * @param remote_as The node which is receiving the beacon.
 * @param latency The beacon latency (expected to be the old beacon latency aggregated with the intra AS latency or zero)
 * @param bwd The beacon bandwidth (expected to be min{old_beacon_bwd, traversed_intra_as_bwd} or the inter AS bandwidth at the egress interface))
 * @param immediate Flag which indicates if the beacon was marked to be disseminated immediately. In the real deployment this flag would be on the
 * beacon. This extra bit is currently _not_ included in the beacon header size.
 * @param latency_for_immediate The intra AS latency the beacon traversed, used for the proper scheduling timing.
 */
    void
    BeaconingStrategy::GenerateBeaconAndSend(beacon *old_beacon, uint16_t self_egress_if_no,
                                             uint16_t remote_ingress_if_no, Ptr<SCION_Node> remote_as,
                                             ld latency, ld bwd,
                                             bool immediate = false, ld latency_for_immediate = 0.0) {
        uint16_t dst_as;
        std::string key;

        uint16_t remote_as_no = remote_as->as_number;
#if IMMEDIATE_BEACONING
        bool immediate_dst = false;
        bool immediate_non_dst = false;
#endif

        // Even if the remote AS ends up ignoring the beacon later on, we update the interface values anyways since, in the real deployment,
        // we need to send the beacon before the remote AS can decide if it will be ignored.
        if (old_beacon == NULL) {
            node->bytes_sent_per_interface_per_period.at(node->now).at(self_egress_if_no) +=
                    (BEACON_HEADER_SIZE + BEACON_HOP_SIZE);
            dst_as = node->as_number;

#if IMMEDIATE_BEACONING
            if (remote_as->valid_beacons_count_per_dst_as.find (dst_as) ==
            remote_as->valid_beacons_count_per_dst_as.end () &&
            remote_as->next_round_valid_beacons_count_per_dst_as.find (dst_as) ==
            remote_as->next_round_valid_beacons_count_per_dst_as.end ())
        {
            immediate_dst = true;
        }
#endif
        } else {
            node->bytes_sent_per_interface_per_period.at(node->now).at(self_egress_if_no) +=
                    (BEACON_HEADER_SIZE + BEACON_HOP_SIZE +
                     BEACON_HOP_SIZE * old_beacon->the_path.size());
            dst_as = UPPER_16_BITS (old_beacon->the_path.at(0));
            key = old_beacon->key;
        }

#if IMMEDIATE_BEACONING
        if (immediate)
    { // Indicates that this is part of an immediate beacon dissemination (only set in processImmediateReceive)
        // dst_AS not found in next_round beacon store. Or less than 5 beacons in next round store from this AS.
        if (remote_as->next_round_valid_beacons_count_per_dst_as.find (dst_as) ==
            remote_as->next_round_valid_beacons_count_per_dst_as.end () ||
            remote_as->next_round_valid_beacons_count_per_dst_as.at (dst_as) <
            MAX_IMMEDIATE_BEACONS)
        {
            immediate_non_dst = true;
        }

    }
    // ***
#endif

        key = key + std::string((char *) &node->as_number, 2) +
              std::string((char *) &self_egress_if_no, 2);

        // If the beacon is already in the remote_ases beacon store
        if (remote_as->path_map_to_beacon.find(key) != remote_as->path_map_to_beacon.end()) {
            if (old_beacon == NULL) {
                remote_as->path_map_to_beacon.at(key)->next_initiation_time = node->now;
                remote_as->path_map_to_beacon.at(key)->next_expiration_time =
                        node->now + node->expiration_period;
            } else {
                remote_as->path_map_to_beacon.at(key)->next_initiation_time =
                        old_beacon->initiation_time;
                remote_as->path_map_to_beacon.at(key)->next_expiration_time =
                        old_beacon->expiration_time;
            }
            remote_as->path_map_to_beacon.at(key)->is_new = true;
            if (!remote_as->path_map_to_beacon.at(key)->is_valid) {
                remote_as->next_round_valid_beacons_count_per_dst_as.at(dst_as)++;
            }
            return;
        }

        if (remote_as->next_round_valid_beacons_count_per_dst_as.find(dst_as) !=
            remote_as->next_round_valid_beacons_count_per_dst_as.end()) {
            if (remote_as->next_round_valid_beacons_count_per_dst_as.at(dst_as) >= FIXED_BEACONS_NUMBER_TO_STORE) {
                remote_as->strategy->ReplacementPolicy(key, dst_as, old_beacon,
                                                       self_egress_if_no, remote_ingress_if_no,
                                                       remote_as, latency, bwd);
                return; // If the beacon store was full, we are done after this call.
            }
            remote_as->next_round_valid_beacons_count_per_dst_as.at(dst_as)++;
        } else {
            remote_as->next_round_valid_beacons_count_per_dst_as.insert(
                    std::make_pair(dst_as, 1));
        }

        uint64_t link_info;
        link_info = (((uint64_t) node->as_number) << 48) | (((uint64_t) self_egress_if_no) << 32) |
                    (((uint64_t) remote_as_no) << 16) | ((uint64_t) remote_ingress_if_no);

        path new_path;
        uint16_t next_initiation_time;
        uint16_t next_expiration_time;

        if (old_beacon == NULL) {
            next_initiation_time = node->now;
            next_expiration_time = node->now + node->expiration_period;
        } else {
            next_initiation_time = old_beacon->initiation_time;
            next_expiration_time = old_beacon->expiration_time;
            new_path = old_beacon->the_path;
        }
        new_path.push_back(link_info);
        uint16_t path_len = (uint16_t) new_path.size();

        beacon *new_beacon = new beacon((float) latency, (float) bwd, 0, 0, next_initiation_time,
                                        next_expiration_time, true, false, new_path, key);

        remote_as->path_map_to_beacon.insert(std::make_pair(key, new_beacon));

        if (remote_as->beacon_store.find(dst_as) != remote_as->beacon_store.end() &&
            remote_as->beacon_store.at(dst_as).find(path_len) !=
            remote_as->beacon_store.at(dst_as).end()) {
            remote_as->beacon_store.at(dst_as).at(path_len).insert(new_beacon);
        } else if (remote_as->beacon_store.find(dst_as) != remote_as->beacon_store.end() &&
                   remote_as->beacon_store.at(dst_as).find(path_len) ==
                   remote_as->beacon_store.at(dst_as).end()) {
            remote_as->beacon_store.at(dst_as).insert(
                    std::make_pair(path_len, beacons_with_equal_length()));
            remote_as->beacon_store.at(dst_as).at(path_len).insert(new_beacon);
        } else {
            remote_as->beacon_store.insert(std::make_pair(dst_as, beacons_with_same_dst_as()));
            remote_as->beacon_store.at(dst_as).insert(
                    std::make_pair(path_len, beacons_with_equal_length()));
            remote_as->beacon_store.at(dst_as).at(path_len).insert(new_beacon);
        }

#if IMMEDIATE_BEACONING
        if (immediate_dst)
    {
        // This is the processing delay of the receiving BR. Since this beacon can be generated at whichever border router (immediate_dst)
        // We don't need to consider the intra_as_latencies
        Simulator::Schedule (PROCESSING_DELAY, &SCION_Node::ProcessReceivedBeacons, remote_as,
                             dst_as, remote_ingress_if_no, new_beacon);
    }

    if (immediate_non_dst)
    {
        uint64_t delay = (uint64_t) (latency_for_immediate * 1000000);
        // Here the intra_as_latency is relevant and added to the processing delay.
        Simulator::Schedule (NanoSeconds (delay) + PROCESSING_DELAY,
                             &SCION_Node::ProcessReceivedBeacons, remote_as, dst_as,
                             remote_ingress_if_no, new_beacon);
        MetaDataUpdateAfterImmediateSend(new_beacon, self_egress_if_no, remote_as, dst_as);
    }
#endif
    }


/**
 * This function only executes if the source AS number at the origin of the beacon-path is unknown to the node.
 * Updates the nodes local time, adjusts the beacon validity, updates the beacons latency and bandwidth stat,
 * and sends the beacon to each neighbour over the interface with the smallest latency,
 * except the one it received the beacon from.
 *
 * @see AdjustBeaconValidity
 * @see GenerateBeaconAndSend
 *
 * @param dst_as The AS number of the AS that originated the beacon.
 * @param ingress_if The ingress interface over which the beacon was received.
 * @param the_beacon The received beacon.
 * @param valid_interfaces The valid interfaces over which the beacon can be disseminated.
 * @param node The node processing the beacon.
 */
    void
    BeaconingStrategy::processImmediateReceive(uint16_t dst_as, uint16_t ingress_if,
                                               beacon *the_beacon,
                                               SCION_Node::neighbour_relation relation) {
        if (node->valid_beacons_count_per_dst_as.find(dst_as) !=
            node->valid_beacons_count_per_dst_as.end()) {
            return; // only process unknown beacons immediately
        }

        UpdateBeaconState(the_beacon);

        for (auto const &remote_as_no_relation_pair : node->neighbors) {
            if (remote_as_no_relation_pair.second != relation) {
                continue;
            }
            uint16_t remote_as_no = remote_as_no_relation_pair.first;
            // Since the immediately disseminated beacons only propagate if the node does not yet have an entry
            // for AS at the beacon origin, loops are already prevented. Therefore this check is sufficient.
            if (remote_as_no == dst_as) {
                continue;
            }

            uint16_t min_egress_if = 0;
            ld min_latency = std::numeric_limits<double>::max();

            for (auto const &egress_if : node->interfaces_per_neighbor_as.at(remote_as_no)) {
                if (node->intra_as_latencies.at(ingress_if).at(egress_if) < min_latency) {
                    min_latency = node->intra_as_latencies.at(ingress_if).at(egress_if);
                    min_egress_if = egress_if;
                }
            }

            std::pair<uint16_t, Ptr<SCION_Node>>
                    remote_as_if_pair = node->GetRemoteAsInfo(min_egress_if);

            uint16_t remote_ingress_if_no = remote_as_if_pair.first;
            Ptr<SCION_Node> remote_as = remote_as_if_pair.second;

            ld latency = the_beacon->latency_stat +
                         node->intra_as_latencies.at(ingress_if).at(min_egress_if);
            ld bwd = the_beacon->bwd_stat > (ld) node->inter_as_bwds.at(min_egress_if)
                     ? (ld) node->inter_as_bwds.at(min_egress_if)
                     : the_beacon->bwd_stat;

            GenerateBeaconAndSend(the_beacon, min_egress_if, remote_ingress_if_no, remote_as,
                                  latency, bwd, true, min_latency);
        }
    }

/**
 * For reasons of scalability, we do not use ns3s native scheduler functions (e.g. send). Instead the beacons that have
 * been sent are directly written into the remote ASes beacon store with the 'new' bit set to true and the 'valid' bit set to false
 * such that they will not be disseminated in the same period. Before the next period starts, this functions responsibility
 * is to set the valid bit of the beacons received in the last period, such that they are disseminated in this period.
 * It also invalidates beacons that have expired.
 *
 * @see AdjustBeaconValidity
 * @param node The node on which to update the beacon store.
 */
    void
    BeaconingStrategy::UpdateStatePeriodic() {
        auto const &beacons = node->path_map_to_beacon;
        for (auto const &the_beacon_pair : beacons) {
            beacon *the_beacon = the_beacon_pair.second;
            UpdateBeaconState(the_beacon);
            MetaDataUpdatePeriodic(the_beacon);
        }
    }

/**
 * Updates the node time with the current simulator times.
 * - If the beacon is new: Sets the new property to false, increments the nodes valid beacon count for
 * the beacons source AS, sets the validity bit of the beacon to true and updates the beacons initiation
 * and expiration time.
 * - If the beacon is expired and valid: Sets the validity bit to false, and decreases the nodes
 * valid beacon counters as well as the counters for the next beaconing round.
 *
 * @param the_beacon The beacon for which to adjust the validity.
 * @param node The node holding the beacon.
 */
    void
    BeaconingStrategy::UpdateBeaconState(beacon *the_beacon) {
        uint16_t dst_as = UPPER_16_BITS (the_beacon->the_path.at(0));
        if (the_beacon->is_new) {
            the_beacon->is_new = false;
            if (the_beacon->next_expiration_time > node->now) {
                if (!the_beacon->is_valid) {
                    the_beacon->is_valid = true;
                    try {
                        node->valid_beacons_count_per_dst_as.at(dst_as)++;
                    } catch (const std::out_of_range &) {
                        node->valid_beacons_count_per_dst_as.insert(
                                std::make_pair(dst_as, 1));
                    }
                }
                the_beacon->initiation_time = the_beacon->next_initiation_time;
                the_beacon->expiration_time = the_beacon->next_expiration_time;
            }
        }

        if (the_beacon->expiration_time <= node->next_period && the_beacon->is_valid) {
            the_beacon->is_valid = false;

            if (node->valid_beacons_count_per_dst_as.find(dst_as) !=
                node->valid_beacons_count_per_dst_as.end()) {
                node->valid_beacons_count_per_dst_as.at(dst_as)--;
            }

            if (node->next_round_valid_beacons_count_per_dst_as.find(dst_as) !=
                node->next_round_valid_beacons_count_per_dst_as.end()) {
                node->next_round_valid_beacons_count_per_dst_as.at(dst_as)--;
            }
        }
    }

/**
 * @param the_beacon The beacon to be checked.
 * @param remote_as_no The remote as number against which to check for loops.
 * @return True if a loop is detected, false otherwise.
 */
    bool
    BeaconingStrategy::GeneratesLoop(beacon const *the_beacon, uint16_t remote_as_no) {
        for (auto const &link_info : the_beacon->the_path) { // remove loops
            if (UPPER_16_BITS (link_info) == remote_as_no) {
                return true;
            }
        }
        return false;
    }


} // namespace ns3