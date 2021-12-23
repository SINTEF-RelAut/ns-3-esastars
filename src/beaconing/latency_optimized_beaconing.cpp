/**
 * @file latency_optimized_beaconing.cpp
 * @authors Seyedali Tabaeiaghdaei
 * @date 2021
 * @see latency_optimized_beaconing.h
 */

#include <cassert>
#include <omp.h>
#include "src/SCION/headers/beaconing/latency_optimized_beaconing.h"
#include "src/SCION/headers/utils.h"

namespace ns3 {
    void LatencyOptimized::DoInitializations(uint32_t num_ASes) {
        beacons_per_dst_per_ing_if_sorted_by_latency.resize(num_ASes);

        for (uint32_t i = 0;i < num_ASes; ++i ) {
            beacons_per_dst_per_ing_if_sorted_by_latency.at(i) = std::vector<std::multimap<ld, Beacon*>>();
            beacons_per_dst_per_ing_if_sorted_by_latency.at(i).resize(AS->GetNDevices());
            for (uint32_t j = 0; j < AS->GetNDevices(); ++j) {
                beacons_per_dst_per_ing_if_sorted_by_latency.at(i).at(j) = std::multimap<ld, Beacon*>();
            }
        }
    }

    void LatencyOptimized::create_initial_static_info_extension(static_info_extension_t& static_info_extension, uint16_t self_egress_if_no) {
        static_info_extension.insert(std::make_pair(static_info_type_t::LATENCY, 0));
    }

    std::tuple<bool, bool, bool, Beacon*>
    LatencyOptimized::ImportPolicy(Beacon &the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no,
                                   uint16_t self_ingress_if_no, uint16_t now)
    {
        uint16_t dst_as = UPPER_16_BITS(the_beacon.the_path.at(0));

        if (path_map_to_beacon.find(the_beacon.key) != path_map_to_beacon.end()) {
            Beacon* existing_beacon = path_map_to_beacon.at(the_beacon.key);
            if (!existing_beacon->is_valid){
                return std::tuple<bool, bool, bool, Beacon*>(true, true, false, existing_beacon);
            }
            return std::tuple<bool, bool, bool, Beacon*>(true, true, true, existing_beacon);
        }

        if (the_beacon.the_path.size() == 1) {
            return std::tuple<bool, bool, bool, Beacon*>(true, false, false, NULL);
        }

        if (beacons_per_dst_per_ing_if_sorted_by_latency.at(dst_as).at(self_ingress_if_no).size() < MAX_BEACONS_TO_STORE_PER_IFACE) {
            return std::tuple<bool, bool, bool, Beacon*>(true, false, false, NULL);
        }

        ld latency = the_beacon.static_info_extension.at(static_info_type_t::LATENCY);

        std::multimap<ld, Beacon*>::reverse_iterator highest_previous_latency_iterator = beacons_per_dst_per_ing_if_sorted_by_latency.at(dst_as).at(self_ingress_if_no).rbegin();
        ld highest_previous_latency = highest_previous_latency_iterator->first;
        if (highest_previous_latency > latency) {
            Beacon* to_be_removed_beacon = highest_previous_latency_iterator->second;
            return std::tuple<bool, bool, bool, Beacon*> (true, false, false, to_be_removed_beacon);
        }
        return std::tuple<bool, bool, bool, Beacon*> (false, false, false, NULL);
    }

    void
    LatencyOptimized::DeleteFromStrategyMetaData (Beacon* the_beacon)
    {
        uint16_t dst_as = UPPER_16_BITS(the_beacon->the_path.at(0));
        delete_from_beacons_per_dst_sorted_by_latency(dst_as, the_beacon);
    }

    void
    LatencyOptimized::InsertToStrategyMetaData (Beacon* the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no, uint16_t self_ingress_if_no)
    {
        uint16_t dst_as = UPPER_16_BITS(the_beacon->the_path.at(0));
        insert_to_beacons_per_dst_sorted_by_latency(dst_as,  the_beacon);
    }

    void
    LatencyOptimized::DisseminateBeacons(neighbour_relation relation) {
        uint32_t neighbors_cnt = AS->neighbors.size();
        omp_set_num_threads(NUM_CORE);
#pragma omp parallel for
        for (uint32_t i = 0; i < neighbors_cnt; ++i) { // Per neighbor AS

            if (AS->neighbors.at(i).second != relation) {
                continue;
            }

            uint16_t remote_as_no = AS->neighbors.at(i).first;
            for (auto const &dst_as_beacons_pair : beacon_store) { // Per destination AS
                uint16_t dst_as_no = dst_as_beacons_pair.first;
                const beacons_with_same_dst_as &beacons_to_the_dst_as = dst_as_beacons_pair.second;

                if (remote_as_no == dst_as_no) {
                    continue;
                }

                std::multimap<ld, std::tuple<Beacon *, uint16_t, uint16_t, SCION_AS*, static_info_extension_t> > selected_beacons =
                        select_beacons_to_disseminate_per_dst_per_nbr(remote_as_no, dst_as_no, beacons_to_the_dst_as);

                for (auto const &the_tuple_pair : selected_beacons) {
                    Beacon *the_beacon;
                    uint16_t remote_ingress_if_no;
                    uint16_t self_egress_if_no;
                    SCION_AS* remote_as;
                    static_info_extension_t static_info_extension;

                    std::tie(the_beacon, self_egress_if_no, remote_ingress_if_no, remote_as, static_info_extension) = the_tuple_pair.second;

                    GenerateBeaconAndSend(the_beacon, self_egress_if_no, remote_ingress_if_no, remote_as,
                                          static_info_extension);

                }
            }
        }
    }

    std::multimap<ld, std::tuple<Beacon *, uint16_t, uint16_t, SCION_AS*, static_info_extension_t> >
    LatencyOptimized::select_beacons_to_disseminate_per_dst_per_nbr(uint16_t remote_as_no, uint16_t dst_as_no,
                                                                  const beacons_with_same_dst_as &beacons_to_the_dst_as) {
        std::multimap<ld, std::tuple<Beacon *, uint16_t, uint16_t, SCION_AS*, static_info_extension_t> > latency_map_to_beacon_and_metadata;
        std::map<uint16_t, std::multimap<ld, Beacon*> > valid_candidates;

        int beacon_cnt = 0;
        for (auto const &len_beacons_pair : beacons_to_the_dst_as) {
            auto const &beacons = len_beacons_pair.second;
            for (auto const &the_beacon : beacons) {
                if (!the_beacon->is_valid) {
                    continue;
                }

                bool generates_loop = false;
                for (auto const &link_info : the_beacon->the_path) { // remove loops
                    if (UPPER_16_BITS(link_info) == remote_as_no) {
                        generates_loop = true;
                        break;
                    }
                }

                if (generates_loop) {
                    continue;
                }

                auto const &interfaces = AS->interfaces_per_neighbor_as.at(remote_as_no);
                for (auto const &self_egress_if_no : interfaces) {
                    ld  latency = the_beacon->static_info_extension.at(static_info_type_t::LATENCY) +
                                  AS->latencies_between_interfaces.at(LOWER_16_BITS(the_beacon->the_path.back())).at(self_egress_if_no);

                    if (beacon_cnt == 0) {
                        valid_candidates.insert(std::make_pair(self_egress_if_no, std::multimap<ld, Beacon*>()));
                    }

                    valid_candidates.at(self_egress_if_no).insert(std::make_pair(latency, the_beacon));

                }

                beacon_cnt++;
            }
        }


        for (auto const & iface_to_latency_beacon_pair : valid_candidates) {
            uint16_t self_egress_if_no = iface_to_latency_beacon_pair.first;

            int no_beacons_per_iface = 0;
            for (auto const &latency_beacon_pair : iface_to_latency_beacon_pair.second) {
                if (no_beacons_per_iface >= MAX_BEACONS_TO_SEND_PER_IFACE) {
                    break;
                }
                no_beacons_per_iface++;

                ld latency = latency_beacon_pair.first;
                Beacon *the_beacon = latency_beacon_pair.second;

                uint16_t remote_ingress_if_no = AS->GetRemoteAsInfo(self_egress_if_no).first;
                SCION_AS* remote_as = AS->GetRemoteAsInfo(self_egress_if_no).second;

                static_info_extension_t static_info_extension;
                static_info_extension.insert(std::make_pair(static_info_type_t::LATENCY, latency));

                latency_map_to_beacon_and_metadata.insert(
                        std::make_pair(latency, std::tuple<Beacon *, uint16_t, uint16_t, SCION_AS*, static_info_extension_t>
                                (the_beacon, self_egress_if_no, remote_ingress_if_no, remote_as,
                                 static_info_extension)));

            }
        }

        return latency_map_to_beacon_and_metadata;
    }

    void LatencyOptimized::insert_to_beacons_per_dst_sorted_by_latency(uint16_t dst_as, Beacon* the_beacon) {
        ld latency = the_beacon->static_info_extension.at(static_info_type_t::LATENCY);
        uint16_t self_ingress_if = LOWER_16_BITS(the_beacon->the_path.back());

        beacons_per_dst_per_ing_if_sorted_by_latency.at(dst_as).at(self_ingress_if).insert(std::make_pair(latency, the_beacon));

    }

    void LatencyOptimized::delete_from_beacons_per_dst_sorted_by_latency(uint16_t dst_as, Beacon* the_beacon) {
        ld latency = the_beacon->static_info_extension.at(static_info_type_t::LATENCY);
        uint16_t self_ingress_if = LOWER_16_BITS(the_beacon->the_path.back());

        std::multimap<ld, Beacon*>::iterator iterator = beacons_per_dst_per_ing_if_sorted_by_latency.at(dst_as).at(self_ingress_if).begin();

        for (; iterator != beacons_per_dst_per_ing_if_sorted_by_latency.at(dst_as).at(self_ingress_if).end(); ++iterator){
            if (iterator->first == latency && iterator->second == the_beacon) {
                beacons_per_dst_per_ing_if_sorted_by_latency.at(dst_as).at(self_ingress_if).erase(iterator);
            }
        }
    }

    void LatencyOptimized::MetaDataUpdatePeriodic(Beacon* the_beacon, bool invalidated) {
        uint16_t dst_as = UPPER_16_BITS(the_beacon->the_path.at(0));
        if (invalidated) {
            delete_from_beacons_per_dst_sorted_by_latency(dst_as, the_beacon);
        }
    }

}