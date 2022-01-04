/**
 * @file green_beaconing.cpp
 * @authors Seyedali Tabaeiaghdaei
 * @date 2021
 * @see green_beaconing.h
 */

#include <omp.h>


#include "ns3/point-to-point-channel.h"

#include "src/SCION/headers/beaconing/green_beaconing.h"
#include "src/SCION/headers/utils.h"


namespace ns3 {
    void GreenBeaconing::DoInitializations(uint32_t num_ASes) {
        beacons_per_dst_per_ing_if_sorted_by_pollution.resize(num_ASes);

        for (uint32_t i = 0; i < num_ASes; ++i ) {
            beacons_per_dst_per_ing_if_sorted_by_pollution.at(i) = std::vector<std::multimap<ld, Beacon*>>();
            beacons_per_dst_per_ing_if_sorted_by_pollution.at(i).resize(AS->GetNDevices());
            for (uint32_t j = 0; j < AS->GetNDevices(); ++j) {
                beacons_per_dst_per_ing_if_sorted_by_pollution.at(i).at(j) = std::multimap<ld, Beacon*>();
            }
        }
    }

    void GreenBeaconing::create_initial_static_info_extension(static_info_extension_t& static_info_extension, uint16_t self_egress_if_no) {
        static_info_extension.insert(std::make_pair(static_info_type_t::LATENCY, 0));
        static_info_extension.insert(std::make_pair(static_info_type_t::CO2, 0));
    }

    std::tuple<bool, bool, bool, Beacon*>
    GreenBeaconing::ImportPolicy (Beacon &the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no,
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

        if (beacons_per_dst_per_ing_if_sorted_by_pollution.at(dst_as).at(self_ingress_if_no).size() < MAX_BEACONS_TO_STORE_PER_IFACE) {
            return std::tuple<bool, bool, bool, Beacon*>(true, false, false, NULL);
        }

        ld  pollution_index = the_beacon.static_info_extension.at(static_info_type_t::CO2);

        std::multimap<ld, Beacon*>::reverse_iterator highest_previous_pollution_iterator = beacons_per_dst_per_ing_if_sorted_by_pollution.at(dst_as).at(self_ingress_if_no).rbegin();
        ld highest_previous_pollution = highest_previous_pollution_iterator->first;
        if (highest_previous_pollution > pollution_index) {
            Beacon* to_be_removed_beacon = highest_previous_pollution_iterator->second;
            return std::tuple<bool, bool, bool, Beacon*> (true, false, false, to_be_removed_beacon);
        }
        return std::tuple<bool, bool, bool, Beacon*> (false, false, false, NULL);
    }

    void
    GreenBeaconing::DeleteFromStrategyMetaData (Beacon* the_beacon)
    {
        uint16_t dst_as = UPPER_16_BITS(the_beacon->the_path.at(0));
        delete_from_beacons_per_dst_sorted_by_pollution(dst_as, the_beacon);
    }

    void
    GreenBeaconing::InsertToStrategyMetaData (Beacon* the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no, uint16_t self_ingress_if_no)
    {
        uint16_t dst_as = UPPER_16_BITS(the_beacon->the_path.at(0));
        insert_to_beacons_per_dst_sorted_by_pollution(dst_as,  the_beacon);
    }

    void
    GreenBeaconing::DisseminateBeacons(neighbour_relation relation) {
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

                std::multimap<ld, std::tuple<Beacon *, uint16_t, uint16_t, SCION_AS*, static_info_extension_t> > selected_beacons;
                select_beacons_to_disseminate_per_dst_per_nbr(remote_as_no, dst_as_no, beacons_to_the_dst_as, selected_beacons);

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

    void
    GreenBeaconing::select_beacons_to_disseminate_per_dst_per_nbr(uint16_t remote_as_no, uint16_t dst_as_no,
                                                                  const beacons_with_same_dst_as &beacons_to_the_dst_as,
								  std::multimap<ld, std::tuple<Beacon *, uint16_t, uint16_t, SCION_AS*, static_info_extension_t> >& pollution_index_map_to_beacon_and_metadata) {
        std::map<uint16_t, std::multimap<ld, Beacon*> > valid_candidates;


        auto const &interfaces = AS->interfaces_per_neighbor_as.at(remote_as_no);
        for (auto const &self_egress_if_no : interfaces) {
           valid_candidates.insert(std::make_pair(self_egress_if_no, std::multimap<ld, Beacon*>()));
        }

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
                    ld pollution_index = the_beacon->static_info_extension.at(static_info_type_t::CO2) +
                                          calculate_pollution_between_border_routers (LOWER_16_BITS(the_beacon->the_path.back()),
                                                                                      self_egress_if_no);


                    valid_candidates.at(self_egress_if_no).insert(std::make_pair(pollution_index, the_beacon));
                }
            }
        }

        for (auto const & iface_to_pollution_beacon_pair : valid_candidates) {
            uint16_t self_egress_if_no = iface_to_pollution_beacon_pair.first;
            int no_beacons_per_iface = 0;
            for (auto const &pollution_beacon_pair : iface_to_pollution_beacon_pair.second) {
                if (no_beacons_per_iface >= MAX_BEACONS_TO_SEND_PER_IFACE) {
                    break;
                }
                no_beacons_per_iface++;

                ld pollution_index = pollution_beacon_pair.first;
                Beacon *the_beacon = pollution_beacon_pair.second;

                uint16_t remote_ingress_if_no = AS->GetRemoteAsInfo(self_egress_if_no).first;
                SCION_AS* remote_as = AS->GetRemoteAsInfo(self_egress_if_no).second;

                ld latency = the_beacon->static_info_extension.at(static_info_type_t::LATENCY) +
                        AS->latencies_between_interfaces.at(LOWER_16_BITS(the_beacon->the_path.back())).at(
                                     self_egress_if_no);

                static_info_extension_t static_info_extension;
                static_info_extension.insert(std::make_pair(static_info_type_t::LATENCY, latency));
                static_info_extension.insert(std::make_pair(static_info_type_t::CO2, pollution_index));

                pollution_index_map_to_beacon_and_metadata.insert(std::make_pair(pollution_index,
                                                                                 std::tuple<Beacon *, uint16_t, uint16_t, SCION_AS *, static_info_extension_t>
                                                                                         (the_beacon, self_egress_if_no,
                                                                                          remote_ingress_if_no,
                                                                                          remote_as,
                                                                                          static_info_extension)));
            }

        }

    }

    void GreenBeaconing::insert_to_beacons_per_dst_sorted_by_pollution(uint16_t dst_as, Beacon* the_beacon) {
        ld pollution_index = the_beacon->static_info_extension.at(static_info_type_t::CO2);
        uint16_t self_ingress_if = LOWER_16_BITS(the_beacon->the_path.back());

        beacons_per_dst_per_ing_if_sorted_by_pollution.at(dst_as).at(self_ingress_if).insert(std::make_pair(pollution_index, the_beacon));
    }

    void GreenBeaconing::delete_from_beacons_per_dst_sorted_by_pollution(uint16_t dst_as, Beacon* the_beacon) {
        ld pollution_index = the_beacon->static_info_extension.at(static_info_type_t::CO2);
        uint16_t self_ingress_if = LOWER_16_BITS(the_beacon->the_path.back());

        std::multimap<ld, Beacon*>::iterator iterator = beacons_per_dst_per_ing_if_sorted_by_pollution.at(dst_as).at(self_ingress_if).begin();

        for (; iterator != beacons_per_dst_per_ing_if_sorted_by_pollution.at(dst_as).at(self_ingress_if).end(); ++iterator){
            if (iterator->first == pollution_index && iterator->second == the_beacon) {
                beacons_per_dst_per_ing_if_sorted_by_pollution.at(dst_as).at(self_ingress_if).erase(iterator);
            }
        }
    }

    ld GreenBeaconing::calculate_pollution_between_border_routers(uint16_t ingress_if, uint16_t egress_if) {
        ld energy_resource_carbon_intensity = dirty_energy_ratio * 700 / 3.6e6; // gr/Joule
        ld  path_energy_intensity = intra_as_energies.at(ingress_if).at(egress_if);

        ld  pollution = path_energy_intensity * energy_resource_carbon_intensity;

        return pollution;
    }


    void GreenBeaconing::MetaDataUpdatePeriodic(Beacon* the_beacon, bool invalidated) {
        uint16_t dst_as = UPPER_16_BITS(the_beacon->the_path.at(0));
        if (invalidated) {
            delete_from_beacons_per_dst_sorted_by_pollution(dst_as, the_beacon);
        }
    }

    void ReadBr2BrEnergy(NodeContainer AS_nodes, std::map<int32_t, uint16_t> real_to_alias_as_no, const YAML::Node& config) {
        std::ifstream energy_file(config["beacon_service"]["br_br_energy_file"].as<std::string>());
        std::string  line;

        int counter = 0;
        while(getline(energy_file, line)){
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
            SCION_AS* as = dynamic_cast<SCION_AS*>(PeekPointer(AS_nodes.Get(index)));
            assert(as->as_number == index);

            GreenBeaconing* green_beaconing_policy = dynamic_cast<GreenBeaconing*>(as->GetBeaconServer());

            if (green_beaconing_policy->intra_as_energies.size() == 0){
                green_beaconing_policy->intra_as_energies.resize(as->GetNDevices());
                for (uint32_t i = 0; i < as->GetNDevices(); ++i) {
                    green_beaconing_policy->intra_as_energies.at(i).resize(as->GetNDevices());
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
                            green_beaconing_policy->intra_as_energies.at(i).at(j) = energy;
                        }
                    }
                }
            }
        }
        energy_file.close();
        std::cout << counter << std::endl;

        for (uint32_t i = 0; i < AS_nodes.GetN(); ++i) {
            SCION_AS* as = dynamic_cast<SCION_AS*>(PeekPointer(AS_nodes.Get(i)));
            for (uint32_t j = 0; j < dynamic_cast<GreenBeaconing*>(as->GetBeaconServer())->intra_as_energies.size(); ++j) {
                for (uint32_t k = 0; k < dynamic_cast<GreenBeaconing*>(as->GetBeaconServer())->intra_as_energies.at(j).size(); ++k) {
                    assert(dynamic_cast<GreenBeaconing*>(as->GetBeaconServer())->intra_as_energies.at(j).at(k) != 0);
                }
            }
        }
    }
}
