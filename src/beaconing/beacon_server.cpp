/**
 * @file beacon_server.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see beacon_server.h
 */

#include <omp.h>

#include "ns3/point-to-point-net-device.h"

#include "src/SCION/headers/beaconing/beacon_server.h"
#include "src/SCION/headers/path_server.h"
#include "src/SCION/headers/run_parallel_events.h"
#include "src/SCION/headers/scion_core_as.h"
#include "src/SCION/headers/utils.h"

namespace ns3 {
    void BeaconServer::PerLinkInitializations(rapidxml::xml_node<> *xml_node, const YAML::Node &config) {};

    void BeaconServer::SetAS(SCION_AS *AS) { this->AS = AS; }

    void BeaconServer::ScheduleBeaconing(Time last_beaconing_event_time) {
        for (Time t = Seconds(0); t < last_beaconing_event_time; t += beaconing_period) {
            Simulator::Schedule(t, &BeaconServer::update_time_and_stats, this);

            if (dynamic_cast<SCION_Core_AS *>(AS) != NULL) {
                Simulator::Schedule(t, &BeaconServer::disseminate_beacons, this, neighbour_relation::CORE);

                Simulator::Schedule(t, &BeaconServer::initiate_beacons, this, neighbour_relation::CORE);
                Simulator::Schedule(t, &BeaconServer::initiate_beacons, this, neighbour_relation::CUSTOMER);
            } else {
                Simulator::Schedule(t, &BeaconServer::disseminate_beacons, this, neighbour_relation::CUSTOMER);
            }

            if (parallel_scheduler) {
                if (AS->GetPathServer() != NULL) {
                    Simulator::Schedule(t + AS->latency_between_path_server_and_beacon_server,
                                        &RunParallelEvents<void (BeaconServer::*)()>,
                                        &BeaconServer::register_to_local_path_server);
                }

                Simulator::Schedule(t + MilliSeconds(150), &RunParallelEvents<void (BeaconServer::*)()>,
                                    &BeaconServer::update_state_periodic);
            }
        }
    }

    void BeaconServer::update_beacon_state(Beacon *the_beacon) {
        uint16_t dst_as = UPPER_16_BITS(the_beacon->the_path.at(0));
        if (the_beacon->is_new) {
            the_beacon->is_new = false;
            if (the_beacon->next_expiration_time > now) {
                if (!the_beacon->is_valid) {
                    the_beacon->is_valid = true;
                    try {
                        valid_beacons_count_per_dst_as.at(dst_as)++;
                    } catch (const std::out_of_range &) {
                        valid_beacons_count_per_dst_as.insert(std::make_pair(dst_as, 1));
                    }
                }
                the_beacon->initiation_time = the_beacon->next_initiation_time;
                the_beacon->expiration_time = the_beacon->next_expiration_time;
            }
        }

        if (the_beacon->expiration_time <= next_period && the_beacon->is_valid) {
            the_beacon->is_valid = false;

            if (valid_beacons_count_per_dst_as.find(dst_as) != valid_beacons_count_per_dst_as.end()) {
                valid_beacons_count_per_dst_as.at(dst_as)--;
            }

            if (next_round_valid_beacons_count_per_dst_as.find(dst_as) !=
                next_round_valid_beacons_count_per_dst_as.end()) {
                next_round_valid_beacons_count_per_dst_as.at(dst_as)--;
            }
        }
    }

    void BeaconServer::create_initial_static_info_extension(static_info_extension_t &static_info_extension,
                                                            uint16_t self_egress_if_no,
                                                            const optimization_target_t *optimization_target) {}

    void BeaconServer::initiate_beacons(neighbour_relation relation) {
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
                std::pair<uint16_t, SCION_AS *> remote_as_if_pair = AS->GetRemoteAsInfo(self_egress_if_no);

                uint16_t remote_ingress_if_no = remote_as_if_pair.first;
                SCION_AS *remote_as = remote_as_if_pair.second;

                initiate_beacons_per_interface(self_egress_if_no, remote_as, remote_ingress_if_no);
            }
        }
    }

    void BeaconServer::initiate_beacons_per_interface(uint16_t self_egress_if_no, SCION_AS *remote_as,
                                                      uint16_t remote_ingress_if_no) {
        static_info_extension_t static_info_extension;
        create_initial_static_info_extension(static_info_extension, self_egress_if_no, NULL);

        generate_beacon_and_send(NULL, self_egress_if_no, remote_ingress_if_no, remote_as, static_info_extension);
    }

    void BeaconServer::generate_beacon_and_send(Beacon *selected_beacon, uint16_t self_egress_if_no,
                                                uint16_t remote_ingress_if_no, SCION_AS *remote_as,
                                                static_info_extension_t &static_info_extension,
                                                const optimization_target_t *optimization_target,
                                                beacon_direction_t beacon_direction) {
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

        key = key + std::string((char *) &AS->as_number, 2) + std::string((char *) &self_egress_if_no, 2);
        new_path.push_back(link_info);

        if (new_isd_path.size() == 0 || new_isd_path.back() != AS->isd_number) {
            new_isd_path.push_back(AS->isd_number);
        }

        bool valid = (beacon_direction == beacon_direction_t::PULL_BASED);
        uint16_t initiation_time = (beacon_direction == beacon_direction_t::PULL_BASED) ? next_initiation_time : 0;
        uint16_t expiration_time = (beacon_direction == beacon_direction_t::PULL_BASED) ? next_expiration_time : 0;

        Beacon to_disseminate_beacon(static_info_extension, optimization_target, beacon_direction, initiation_time,
                                     expiration_time, next_initiation_time, next_expiration_time, true, valid, new_path,
                                     key, new_isd_path);

        increment_control_plane_bytes_sent(to_disseminate_beacon, self_egress_if_no);
        remote_as->ReceiveBeacon(to_disseminate_beacon, AS->as_number, self_egress_if_no, remote_ingress_if_no);
    }

    void BeaconServer::update_state_periodic() {
        auto const &beacons = path_map_to_beacon;
        for (auto const &the_beacon_pair : beacons) {
            Beacon *the_beacon = the_beacon_pair.second;

            bool was_valid = the_beacon->is_valid;
            update_beacon_state(the_beacon);
            bool is_valid = the_beacon->is_valid;
            bool invalidated = was_valid && (!is_valid);

            update_algorithm_data_structures_periodic(the_beacon, invalidated);
        }
    }

    void BeaconServer::insert_beacon(Beacon &the_beacon, uint16_t dst_as, uint16_t sender_as, uint16_t remote_egress_if,
                                     uint16_t local_ingress_if, bool path_exists, bool existing_path_valid,
                                     Beacon *beacon_to_replace) {
        if (next_round_valid_beacons_count_per_dst_as.find(dst_as) == next_round_valid_beacons_count_per_dst_as.end()) {
            next_round_valid_beacons_count_per_dst_as.insert(std::make_pair(dst_as, 0));
        }

        if (path_exists) {
            beacon_to_replace->next_initiation_time = the_beacon.next_initiation_time;
            beacon_to_replace->next_expiration_time = the_beacon.next_expiration_time;
            beacon_to_replace->is_new = true;

            if (!existing_path_valid) {
                next_round_valid_beacons_count_per_dst_as.at(dst_as)++;
                insert_to_algorithm_data_structures(beacon_to_replace, sender_as, remote_egress_if, local_ingress_if);
            }
            return;
        } else {
            Beacon *to_insert_beacon;

            if (beacon_to_replace == NULL) {
                to_insert_beacon = new Beacon(the_beacon);
            } else {
                to_insert_beacon = beacon_to_replace;
                *to_insert_beacon = the_beacon;
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
                beacon_store.at(dst_as).insert(std::make_pair(path_len, beacons_with_equal_length()));
                beacon_store.at(dst_as).at(path_len).insert(to_insert_beacon);
            }

            insert_to_algorithm_data_structures(to_insert_beacon, sender_as, remote_egress_if, local_ingress_if);
        }
    }

    void BeaconServer::delete_beacon(Beacon *to_be_removed_beacon, ld replacement_key, uint16_t dst_as) {
        if (to_be_removed_beacon->beacon_direction == beacon_direction_t::PUSH_BASED) {
            assert(beacon_store.find(dst_as) != beacon_store.end());
            assert(beacon_store.at(dst_as).find(to_be_removed_beacon->the_path.size()) !=
                   beacon_store.at(dst_as).end());
            assert(beacon_store.at(dst_as).at(to_be_removed_beacon->the_path.size()).find(to_be_removed_beacon) !=
                   beacon_store.at(dst_as).at(to_be_removed_beacon->the_path.size()).end());

            beacon_store.at(dst_as).at(to_be_removed_beacon->the_path.size()).erase(to_be_removed_beacon);
            if (beacon_store.at(dst_as).at(to_be_removed_beacon->the_path.size()).empty()) {
                beacon_store.at(dst_as).erase(to_be_removed_beacon->the_path.size());
            }
            if (beacon_store.at(dst_as).empty()) {
                beacon_store.erase(dst_as);
            }

            path_map_to_beacon.erase(to_be_removed_beacon->key);

            if (to_be_removed_beacon->is_valid && !to_be_removed_beacon->is_new) {
                valid_beacons_count_per_dst_as.at(dst_as)--;
            }

            if (to_be_removed_beacon->is_new) {
                next_round_valid_beacons_count_per_dst_as.at(dst_as)--;
            }
        }

        delete_from_algorithm_data_structures(to_be_removed_beacon, replacement_key);
    }

    void BeaconServer::increment_control_plane_bytes_sent(Beacon &the_beacon, uint16_t interface) {
        beacons_sent_per_interface_per_period.at(now).at(interface)++;
        bytes_sent_per_interface_per_period.at(now).at(interface) +=
                (BEACON_HEADER_SIZE + BEACON_HOP_SIZE * the_beacon.the_path.size());
    }

    void BeaconServer::ReceiveBeacon(Beacon &received_beacon, uint16_t sender_as, uint16_t remote_if,
                                     uint16_t local_if) {
        uint16_t dst_as = UPPER_16_BITS(received_beacon.the_path.at(0));

        bool to_import;
        bool path_exists;
        bool existing_path_valid;
        Beacon *beacon_to_replace;
        ld replacement_key;

        std::tie(to_import, path_exists, existing_path_valid, beacon_to_replace, replacement_key) =
                import_policy(received_beacon, sender_as, remote_if, local_if, now);

        if (!to_import) {
            return;
        }

        if (!path_exists && beacon_to_replace != NULL) {
            delete_beacon(beacon_to_replace, replacement_key, dst_as);
        }

        insert_beacon(received_beacon, dst_as, sender_as, remote_if, local_if, path_exists, existing_path_valid,
                      beacon_to_replace);
    }

    std::tuple<bool, bool, bool, Beacon *, ld> BeaconServer::import_policy(Beacon &the_beacon, uint16_t sender_as,
                                                                           uint16_t remote_egress_if_no,
                                                                           uint16_t self_ingress_if_no, uint16_t now) {
        if (the_beacon.beacon_direction == beacon_direction_t::PUSH_BASED) {
            if (path_map_to_beacon.find(the_beacon.key) != path_map_to_beacon.end()) {
                Beacon *existing_beacon = path_map_to_beacon.at(the_beacon.key);
                if (!existing_beacon->is_valid) {
                    return std::tuple<bool, bool, bool, Beacon *, ld>(true, true, false, existing_beacon, 0);
                }
                return std::tuple<bool, bool, bool, Beacon *, ld>(true, true, true, existing_beacon, 0);
            }

            if (the_beacon.the_path.size() == 1) {
                return std::tuple<bool, bool, bool, Beacon *, ld>(true, false, false, NULL, 0);
            }
        }

        return alg_specific_import_policy(the_beacon, sender_as, remote_egress_if_no, self_ingress_if_no, now);
    }

    void BeaconServer::update_time_and_stats() {
        now = (uint16_t) Simulator::Now().ToInteger(Time::MIN);
        next_period = now + (uint16_t) beaconing_period.ToInteger(Time::MIN);
        bytes_sent_per_interface_per_period.insert(std::make_pair(now, std::vector<uint32_t>(AS->GetNDevices(), 0)));
        beacons_sent_per_interface_per_period.insert(std::make_pair(now, std::vector<uint32_t>(AS->GetNDevices(), 0)));
    }

    const uint16_t BeaconServer::GetCurrentTime() const { return now; }

    void BeaconServer::register_to_local_path_server() {
        for (auto const &[key, the_beacon] : path_map_to_beacon) {
            if (the_beacon->is_new) {
                PathSegment pathSegment;
                the_beacon->ExtractPathSegment(pathSegment);

                if (dynamic_cast<SCION_Core_AS *>(AS) != NULL) {
                    AS->GetPathServer()->RegisterCorePathSegment(pathSegment, key);
                } else {
                    AS->GetPathServer()->RegisterUpPathSegment(pathSegment, key);
                }
            }
        }
    }

    std::pair<ld, ld> BeaconServer::calculate_final_diversity_scores(Beacon *the_beacon) {
        ld AS_level_diversity_score = 0;
        ld link_level_diversity_score = 0;
        int32_t counter = 0;

        uint16_t dst_as = UPPER_16_BITS(the_beacon->the_path.at(0));
        auto const &equal_dst_as_beacons = beacon_store.at(dst_as);
        for (auto const &len_beacons_pair : equal_dst_as_beacons) {
            auto const &beacons = len_beacons_pair.second;
            for (auto const &curr_beacon : beacons) {
                if (curr_beacon != the_beacon) {
                    AS_level_diversity_score += AS_level_jaccard_distance_between_two_paths(the_beacon, curr_beacon);
                    link_level_diversity_score +=
                            link_level_jaccard_distance_between_two_paths(the_beacon, curr_beacon);
                    counter++;
                }
            }
        }
        return (std::make_pair(AS_level_diversity_score / counter, link_level_diversity_score / counter));
    }

    const std::vector<std::vector<ld>> &BeaconServer::GetIntraASEnergies() const { return intra_as_energies; }

    float BeaconServer::GetDirtyEnergyRatio() const { return dirty_energy_ratio; }

    float BeaconServer::GetSunEnergyRatio() const { return sun_energy_ratio; }

    const std::unordered_map<uint16_t, beacons_with_same_dst_as> &BeaconServer::GetBeaconStore() const {
        return beacon_store;
    }

    const std::unordered_map<std::string, Beacon *> &BeaconServer::GetPathMapToBeacon() const {
        return path_map_to_beacon;
    }

    const std::unordered_map<uint16_t, uint16_t> &BeaconServer::GetValidBeaconsCountPerDstAS() const {
        return valid_beacons_count_per_dst_as;
    }

    const std::unordered_map<uint16_t, uint16_t> &BeaconServer::GetNextRoundValidBeaconsCountPerDstAS() const {
        return next_round_valid_beacons_count_per_dst_as;
    }

    const std::unordered_map<uint16_t, std::vector<uint32_t>> &BeaconServer::GetBytesSentPerInterfacePerPeriod() const {
        return bytes_sent_per_interface_per_period;
    }

    const std::unordered_map<uint16_t, std::vector<uint32_t>> &BeaconServer::GetBeaconsSentPerInterfacePerPeriod() const {
        return beacons_sent_per_interface_per_period;
    }

    void ReadBr2BrEnergy(NodeContainer AS_nodes, std::map<int32_t, uint16_t> real_to_alias_as_no,
                         const YAML::Node &config) {
        std::ifstream energy_file(config["beacon_service"]["br_br_energy_file"].as<std::string>());
        std::string line;

        int counter = 0;
        while (getline(energy_file, line)) {
            std::vector<std::string> fields;
            fields = split(line, '\t', fields);

            int as_no = std::stoi(fields[0]);

            double lat1 = std::stod(fields[1]);
            double long1 = std::stod(fields[2]);

            double lat2 = std::stod(fields[3]);
            double long2 = std::stod(fields[4]);

            double energy = std::stod(fields[5]);

            if (real_to_alias_as_no.find(as_no) == real_to_alias_as_no.end()) {
                counter++;
                continue;
            }

            uint16_t index = real_to_alias_as_no.at(as_no);
            SCION_AS *as = dynamic_cast<SCION_AS *>(PeekPointer(AS_nodes.Get(index)));
            assert(as->as_number == index);

            BeaconServer *beacon_server = as->GetBeaconServer();

            if (beacon_server->intra_as_energies.size() == 0) {
                beacon_server->intra_as_energies.resize(as->GetNDevices());
                for (uint32_t i = 0; i < as->GetNDevices(); ++i) {
                    beacon_server->intra_as_energies.at(i).resize(as->GetNDevices());
                }
            }

            for (uint32_t i = 0; i < as->interfaces_coordinates.size(); ++i) {
                std::pair<double, double> coordinates1 = as->interfaces_coordinates.at(i);
                double if1_lat = coordinates1.first;
                double if1_long = coordinates1.second;

                if (std::abs(if1_lat - lat1) < 0.001 && std::abs(if1_long - long1) < 0.001) {
                    for (uint32_t j = 0; j < as->interfaces_coordinates.size(); ++j) {
                        std::pair<double, double> coordinates2 = as->interfaces_coordinates.at(j);
                        double if2_lat = coordinates2.first;
                        double if2_long = coordinates2.second;

                        if (std::abs(if2_lat - lat2) < 0.001 && std::abs(if2_long - long2) < 0.001) {
                            beacon_server->intra_as_energies.at(i).at(j) = energy;
                        }
                    }
                }
            }
        }
        energy_file.close();
        std::cout << counter << std::endl;

        for (uint32_t i = 0; i < AS_nodes.GetN(); ++i) {
            SCION_AS *as = dynamic_cast<SCION_AS *>(PeekPointer(AS_nodes.Get(i)));
            for (uint32_t j = 0; j < as->GetBeaconServer()->intra_as_energies.size(); ++j) {
                for (uint32_t k = 0; k < as->GetBeaconServer()->intra_as_energies.at(j).size(); ++k) {
                    assert(as->GetBeaconServer()->intra_as_energies.at(j).at(k) != 0);
                }
            }
        }
    }
} // namespace ns3
