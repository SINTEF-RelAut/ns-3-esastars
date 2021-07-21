/**
 * @file beaconing_strategy.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see beaconing_strategy.h
 * @brief Implements the member functions of the BeaconServer.
 */

#include <omp.h>

#include "ns3/point-to-point-net-device.h"
#include "ns3/ptr.h"

#include "src/SCION/headers/beaconing/beacon_server.h"
#include "src/SCION/headers/utils.h"
#include "src/SCION/headers/scion_core_as.h"
#include "src/SCION/headers/local_scheduler.h"
#include "src/SCION/headers/path_server.h"

namespace ns3 {
    void BeaconServer::SetNode(Ptr<SCION_AS> the_node) {
        this->node = the_node;
    }

/**
 * @see GenerateBeaconAndSend
 * @param valid_interfaces The interfaces along to initiate the beacons.
 * @param node The node from where to initiate the beacons
 */
    void
    BeaconServer::InitiateBeacons(neighbour_relation relation) {
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
                std::pair<uint16_t, Ptr<SCION_AS>>
                        remote_as_if_pair = node->GetRemoteAsInfo(self_egress_if_no);

                uint16_t remote_ingress_if_no = remote_as_if_pair.first;
                Ptr<SCION_AS> remote_as = remote_as_if_pair.second;

                GenerateBeaconAndSend(
                        NULL, self_egress_if_no, remote_ingress_if_no, remote_as, 0,
                        node->inter_as_bwds.at(self_egress_if_no));
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
 * the update of any additional beacon store structure a specialized beaconServer might need.
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

   void BeaconServer::GenerateBeaconAndSend(Beacon *selected_beacon, uint16_t self_egress_if_no,
                                            uint16_t remote_ingress_if_no, Ptr<SCION_AS> remote_as,
                                            ld latency, ld bwd)
    {
        std::string key;
        uint16_t remote_as_no = remote_as->as_number;

        path new_path;
        isd_path new_isd_path;
        uint16_t next_initiation_time;
        uint16_t next_expiration_time;

        uint64_t link_info;
        link_info = (((uint64_t) node->as_number) << 48) | (((uint64_t) self_egress_if_no) << 32) |
                    (((uint64_t) remote_as_no) << 16) | ((uint64_t) remote_ingress_if_no);

        if (selected_beacon == NULL) {
            next_initiation_time = now;
            next_expiration_time = now + expiration_period;
        } else {
            next_initiation_time = selected_beacon->initiation_time;
            next_expiration_time = selected_beacon->expiration_time;
            new_path = selected_beacon->the_path;
            key = selected_beacon->key;
            new_isd_path = selected_beacon->the_isd_path;
        }

        key = key + std::string((char *) &node->as_number, 2) +
              std::string((char *) &self_egress_if_no, 2);
        new_path.push_back(link_info);

        if (new_isd_path.size() == 0 || new_isd_path.back() != node->isd_number) {
            new_isd_path.push_back(node->isd_number);
        }

        Beacon to_disseminate_beacon((float) latency, (float) bwd, 0, 0, next_initiation_time,
                                     next_expiration_time, true, false, new_path, key, new_isd_path);

        IncrementControlPlaneBytesSent(to_disseminate_beacon, self_egress_if_no);
        remote_as->ReceiveBeacon(to_disseminate_beacon, node->as_number, self_egress_if_no, remote_ingress_if_no);
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
    BeaconServer::UpdateStatePeriodic() {
        auto const &beacons = path_map_to_beacon;
        for (auto const &the_beacon_pair : beacons) {
            Beacon *the_beacon = the_beacon_pair.second;

            bool was_valid = the_beacon->is_valid;
            UpdateBeaconState(the_beacon);
            bool is_valid = the_beacon->is_valid;
            bool invalidated = was_valid && (!is_valid);

            MetaDataUpdatePeriodic(the_beacon, invalidated);
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
    BeaconServer::UpdateBeaconState(Beacon *the_beacon) {
        uint16_t dst_as = UPPER_16_BITS (the_beacon->the_path.at(0));
        if (the_beacon->is_new) {
            the_beacon->is_new = false;
            if (the_beacon->next_expiration_time > now) {
                if (!the_beacon->is_valid) {
                    the_beacon->is_valid = true;
                    try {
                        valid_beacons_count_per_dst_as.at(dst_as)++;
                    } catch (const std::out_of_range &) {
                        valid_beacons_count_per_dst_as.insert(
                                std::make_pair(dst_as, 1));
                    }
                }
                the_beacon->initiation_time = the_beacon->next_initiation_time;
                the_beacon->expiration_time = the_beacon->next_expiration_time;
            }
        }

        if (the_beacon->expiration_time <= next_period && the_beacon->is_valid) {
            the_beacon->is_valid = false;

            if (valid_beacons_count_per_dst_as.find(dst_as) !=
                valid_beacons_count_per_dst_as.end()) {
                valid_beacons_count_per_dst_as.at(dst_as)--;
            }

            if (next_round_valid_beacons_count_per_dst_as.find(dst_as) !=
                next_round_valid_beacons_count_per_dst_as.end()) {
                next_round_valid_beacons_count_per_dst_as.at(dst_as)--;
            }
        }
    }



    void BeaconServer::InsertBeacon (Beacon& received_beacon, uint16_t dst_as, uint16_t sender_as, uint16_t remote_egress_if, uint16_t local_ingress_if, bool path_exists, bool existing_path_valid, Beacon* beacon_to_replace)
    {

        if (next_round_valid_beacons_count_per_dst_as.find(dst_as) == next_round_valid_beacons_count_per_dst_as.end()) {
            next_round_valid_beacons_count_per_dst_as.insert(std::make_pair(dst_as, 0));
        }

        if (path_exists) {
            beacon_to_replace->next_initiation_time = received_beacon.next_initiation_time;
            beacon_to_replace->next_expiration_time = received_beacon.next_expiration_time;
            beacon_to_replace->is_new = true;

            if (!existing_path_valid) {
                next_round_valid_beacons_count_per_dst_as.at(dst_as)++;
                InsertToStrategyMetaData(beacon_to_replace, sender_as, remote_egress_if, local_ingress_if);
            }
            return;
        } else {
            Beacon* to_insert_beacon;

            if (beacon_to_replace == NULL) {
                to_insert_beacon = new Beacon(received_beacon);
            } else {
                to_insert_beacon = beacon_to_replace;
                *to_insert_beacon = received_beacon;
            }

            next_round_valid_beacons_count_per_dst_as.at(dst_as)++;

            path_map_to_beacon.insert(std::make_pair(to_insert_beacon->key, to_insert_beacon));
            uint16_t path_len = (uint16_t) to_insert_beacon->the_path.size();

            if (beacon_store.find(dst_as) != beacon_store.end() &&
                beacon_store.at(dst_as).find(path_len) != beacon_store.at(dst_as).end()) {
                beacon_store.at(dst_as).at(path_len).insert(to_insert_beacon);
            } else if (beacon_store.find(dst_as) != beacon_store.end() &&
                       beacon_store.at(dst_as).find(path_len) == beacon_store.at(dst_as).end()) {
                beacon_store.at(dst_as).insert(std::make_pair(path_len, beacons_with_equal_length()));
                beacon_store.at(dst_as).at(path_len).insert(to_insert_beacon);
            } else {
                beacon_store.insert(std::make_pair(dst_as, beacons_with_same_dst_as()));
                beacon_store.at(dst_as).insert(
                        std::make_pair(path_len, beacons_with_equal_length()));
                beacon_store.at(dst_as).at(path_len).insert(to_insert_beacon);
            }

            InsertToStrategyMetaData(to_insert_beacon, sender_as, remote_egress_if, local_ingress_if);
        }
    }

    void BeaconServer::DeleteBeacon (Beacon* to_be_removed_beacon, uint16_t dst_as) {
        assert(beacon_store.find(dst_as) != beacon_store.end());
        assert(beacon_store.at(dst_as).find(to_be_removed_beacon->the_path.size()) != beacon_store.at(dst_as).end());
        assert(beacon_store.at(dst_as).at(to_be_removed_beacon->the_path.size()).find(to_be_removed_beacon) != beacon_store.at(dst_as).at(to_be_removed_beacon->the_path.size()).end());

        beacon_store.at(dst_as).at(to_be_removed_beacon->the_path.size()).erase(to_be_removed_beacon);
        if (beacon_store.at(dst_as).at(to_be_removed_beacon->the_path.size()).empty()) {
            beacon_store.at(dst_as).erase(to_be_removed_beacon->the_path.size());
        }
        if (beacon_store.at(dst_as).empty()) {
            beacon_store.erase(dst_as);
        }

        path_map_to_beacon.erase(to_be_removed_beacon->key);

        if (to_be_removed_beacon->is_valid && !to_be_removed_beacon->is_new){
            valid_beacons_count_per_dst_as.at(dst_as)--;
        }

        if (to_be_removed_beacon->is_new) {
            next_round_valid_beacons_count_per_dst_as.at(dst_as)--;
        }

        DeleteFromStrategyMetaData(to_be_removed_beacon);

    }

    void
    BeaconServer::IncrementControlPlaneBytesSent(Beacon &the_beacon, uint16_t interface) {
        bytes_sent_per_interface_per_period.at(now).at(interface) +=
                (BEACON_HEADER_SIZE + BEACON_HOP_SIZE * the_beacon.the_path.size());
    }

    void
    BeaconServer::ReceiveBeacon (Beacon &received_beacon, uint16_t sender_as, uint16_t remote_if, uint16_t local_if) {
        uint16_t dst_as = UPPER_16_BITS(received_beacon.the_path.at(0));

        bool to_import;
        bool path_exists;
        bool existing_path_valid;
        Beacon* beacon_to_replace;

        std::tie(to_import, path_exists, existing_path_valid, beacon_to_replace)
                = ImportPolicy(received_beacon, sender_as, remote_if, local_if, now);

        if (!to_import){
            return;
        }

        if (!path_exists && beacon_to_replace != NULL) {
            DeleteBeacon(beacon_to_replace, dst_as);
        }

        InsertBeacon(received_beacon, dst_as, sender_as, remote_if, local_if, path_exists, existing_path_valid, beacon_to_replace);
    }

    void
    BeaconServer::UpdateTimeAndStats ()
    {
        now = (uint16_t) Simulator::Now().ToInteger(Time::MIN);
        next_period = now + (uint16_t) beaconing_period.ToInteger (Time::MIN);


        bytes_sent_per_interface_per_period.insert (
                std::make_pair (now, std::vector<uint32_t> (node->GetNDevices (), 0)));
    }

    const uint16_t
    BeaconServer::GetCurrentTime() const {
        return now;
    }

    void BeaconServer::ScheduleBeaconing(Time last_beaconing_event_time) {
        for (Time t = Seconds(0); t < last_beaconing_event_time; t += beaconing_period) {
            Simulator::Schedule(t - node->local_time, &BeaconServer::UpdateTimeAndStats, this);

            if (DynamicCast<SCION_Core_AS>(node) != NULL) {
                Simulator::Schedule(t - node->local_time, &BeaconServer::DisseminateBeacons, this, neighbour_relation::CORE);

                Simulator::Schedule(t - node->local_time, &BeaconServer::InitiateBeacons, this, neighbour_relation::CORE);
                Simulator::Schedule(t - node->local_time, &BeaconServer::InitiateBeacons, this, neighbour_relation::CUSTOMER);
            } else {
                Simulator::Schedule(t - node->local_time, &BeaconServer::DisseminateBeacons, this, neighbour_relation::CUSTOMER);
            }

            if (node->GetPathServer() != NULL) {
                node->events.at(node->GetBeaconServerSchedulerIdx())
                ->Schedule(t - node->local_time + node->latency_between_path_server_and_beacon_server,
                           &BeaconServer::RegisterToLocalPathServer,
                           this);
            }

            node->events.at(node->GetBeaconServerSchedulerIdx())
            ->Schedule(t - node->local_time + MilliSeconds(150),
                       &BeaconServer::UpdateStatePeriodic,
                       this);
        }
    }

    void BeaconServer::RegisterToLocalPathServer() {
        for (auto const & [key, the_beacon] : path_map_to_beacon) {
            if (the_beacon->is_new) {
                PathSegment pathSegment;
                the_beacon->ExtractPathSegment(pathSegment);

                if (DynamicCast<SCION_Core_AS>(node) != NULL) {
                    node->GetPathServer()->RegisterCorePathSegment(pathSegment, key);
                } else {
                    node->GetPathServer()->RegisterUpPathSegment(pathSegment, key);
                }
            }
        }


    }
} // namespace ns3