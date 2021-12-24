/**
 * @file beacon_server.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see beacon_server.h
 */

#include <omp.h>

#include "ns3/point-to-point-net-device.h"

#include "src/SCION/headers/beaconing/beacon_server.h"
#include "src/SCION/headers/utils.h"
#include "src/SCION/headers/scion_core_as.h"
#include "src/SCION/headers/global_scheduling.h"
#include "src/SCION/headers/path_server.h"

namespace ns3 {
    void BeaconServer::SetAS(SCION_AS* AS) {
        this->AS = AS;
    }

    void BeaconServer::create_initial_static_info_extension(static_info_extension_t& static_info_extension, uint16_t self_egress_if_no) {}

    void
    BeaconServer::InitiateBeacons(neighbour_relation relation) {
        uint32_t neighbors_cnt = AS->neighbors.size();
        omp_set_num_threads(NUM_CORE);

#pragma omp parallel for
        for (uint32_t i = 0; i < neighbors_cnt; ++i) {
            if (AS->neighbors.at(i).second != relation) {
                continue;
            }

            uint16_t remote_as_no = AS->neighbors.at(i).first;
            const auto &interfaces = AS->interfaces_per_neighbor_as.at(remote_as_no);
            for (auto const &self_egress_if_no : interfaces) {
                std::pair<uint16_t, SCION_AS*>
                        remote_as_if_pair = AS->GetRemoteAsInfo(self_egress_if_no);

                uint16_t remote_ingress_if_no = remote_as_if_pair.first;
                SCION_AS* remote_as = remote_as_if_pair.second;

                static_info_extension_t static_info_extension;
                create_initial_static_info_extension(static_info_extension, self_egress_if_no);

                GenerateBeaconAndSend(
                        NULL, self_egress_if_no, remote_ingress_if_no, remote_as, static_info_extension);
            }
        }
    }

   void BeaconServer::GenerateBeaconAndSend(Beacon *selected_beacon, uint16_t self_egress_if_no,
                                            uint16_t remote_ingress_if_no, SCION_AS* remote_as,
                                            static_info_extension_t static_info_extension)
    {
        std::string key;
        uint16_t remote_as_no = remote_as->as_number;

        path new_path;
        isd_path new_isd_path;
        uint16_t next_initiation_time;
        uint16_t next_expiration_time;

        uint64_t link_info;
        link_info = (((uint64_t) AS->as_number) << 48) | (((uint64_t) self_egress_if_no) << 32) |
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

        key = key + std::string((char *) &AS->as_number, 2) +
              std::string((char *) &self_egress_if_no, 2);
        new_path.push_back(link_info);

        if (new_isd_path.size() == 0 || new_isd_path.back() != AS->isd_number) {
            new_isd_path.push_back(AS->isd_number);
        }

        Beacon to_disseminate_beacon(static_info_extension, 0, 0, next_initiation_time,
                                     next_expiration_time, true, false, new_path, key, new_isd_path);

        IncrementControlPlaneBytesSent(to_disseminate_beacon, self_egress_if_no);
        remote_as->ReceiveBeacon(to_disseminate_beacon, AS->as_number, self_egress_if_no, remote_ingress_if_no);
    }

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
                std::make_pair (now, std::vector<uint32_t> (AS->GetNDevices (), 0)));
    }

    const uint16_t
    BeaconServer::GetCurrentTime() const {
        return now;
    }

    void BeaconServer::ScheduleBeaconing(Time last_beaconing_event_time) {
        for (Time t = Seconds(0); t < last_beaconing_event_time; t += beaconing_period) {
            Simulator::Schedule(t, &BeaconServer::UpdateTimeAndStats, this);

            if (dynamic_cast<SCION_Core_AS*>(AS) != NULL) {
                Simulator::Schedule(t, &BeaconServer::DisseminateBeacons, this, neighbour_relation::CORE);

                Simulator::Schedule(t, &BeaconServer::InitiateBeacons, this, neighbour_relation::CORE);
                Simulator::Schedule(t, &BeaconServer::InitiateBeacons, this, neighbour_relation::CUSTOMER);
            } else {
                Simulator::Schedule(t, &BeaconServer::DisseminateBeacons, this, neighbour_relation::CUSTOMER);
            }

            if (parallel_scheduler) {
                if (AS->GetPathServer() != NULL) {
                    Simulator::Schedule(t + AS->latency_between_path_server_and_beacon_server,
                                        &RunParallelEvents<void (BeaconServer::*)()>,
                                        &BeaconServer::RegisterToLocalPathServer);
                }

                Simulator::Schedule(t + MilliSeconds(150),
                                    &RunParallelEvents<void (BeaconServer::*)()>,
                                    &BeaconServer::UpdateStatePeriodic);
            }
        }
    }

    void BeaconServer::RegisterToLocalPathServer() {
        for (auto const & [key, the_beacon] : path_map_to_beacon) {
            if (the_beacon->is_new) {
                PathSegment pathSegment;
                the_beacon->ExtractPathSegment(pathSegment);

                if (dynamic_cast<SCION_Core_AS*>(AS) != NULL) {
                    AS->GetPathServer()->RegisterCorePathSegment(pathSegment, key);
                } else {
                    AS->GetPathServer()->RegisterUpPathSegment(pathSegment, key);
                }
            }
        }
    }
} // namespace ns3