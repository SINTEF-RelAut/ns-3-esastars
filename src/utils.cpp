/**
 * @file utils.cpp
 * @see utils.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 */

#include <set>
#include <cmath>
#include <random>

#include "src/SCION/headers/utils.h"
#include "src/SCION/headers/beaconing/beacon_server.h"

namespace ns3 {

    double GetMedian( std::multiset<int64_t>& data)
    {
        if (data.empty())
            throw std::length_error("Cannot calculate median value for empty dataset");

        const size_t n = data.size();
        double median = 0;

        auto iter = data.cbegin();
        std::advance(iter, n / 2);

        // Middle or average of two middle values
        if (n % 2 == 0) {
            const auto iter2 = iter--;
            median = double(*iter + *iter2) / 2;    // data[n/2 - 1] AND data[n/2]
        }
        else {
            median = *iter;
        }
        return median;
    }


    std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems) {
        std::stringstream ss(s);
        std::string item;
        while(std::getline(ss, item, delim)) {
            elems.push_back(item);
        }
        return elems;
    }


    ld link_level_jaccard_distance_between_two_paths(Beacon *beacon1, Beacon *beacon2) {
        std::set<uint32_t> set_of_links_on_path1;
        int32_t intersection = 0;

        for (auto const &link_info : beacon1->the_path) {
            set_of_links_on_path1.insert(UPPER_32_BITS(link_info));
        }

        for (auto const &link_info : beacon2->the_path) {
            if (set_of_links_on_path1.find(UPPER_32_BITS(link_info)) !=
                set_of_links_on_path1.end()) {
                intersection++;
            } else {
                set_of_links_on_path1.insert(UPPER_32_BITS(link_info));
            }
        }

        return 1 - 1.0 * intersection / set_of_links_on_path1.size();
    }

    ld AS_level_jaccard_distance_between_two_paths(Beacon *beacon1, Beacon *beacon2) {
        std::set<uint16_t> set_of_ASes_on_path1;
        int32_t intersection = 0;

        for (auto const &link_info : beacon1->the_path) {
            set_of_ASes_on_path1.insert(UPPER_16_BITS(link_info));
        }

        for (auto const &link_info : beacon2->the_path) {
            if (set_of_ASes_on_path1.find(UPPER_16_BITS(link_info)) != set_of_ASes_on_path1.end()) {
                intersection++;
            } else {
                set_of_ASes_on_path1.insert(UPPER_16_BITS(link_info));
            }
        }
        return 1 - 1.0 * intersection / set_of_ASes_on_path1.size();
    }

    ld calculate_great_circle_latency(ld lat1_deg, ld long1_deg, ld lat2_deg, ld long2_deg) {
        ld distance = calculate_great_circle_distance(lat1_deg, long1_deg, lat2_deg, long2_deg);
        // 0.005 millisecods of latency per kilometer
        ld latency = distance * 0.005;
        return latency;
    }


    ld calculate_great_circle_distance(ld lat1_deg, ld long1_deg, ld lat2_deg, ld long2_deg) {
        ld lat1 = lat1_deg * (M_PI) / 180;
        ld long1 = long1_deg * (M_PI) / 180;
        ld lat2 = lat2_deg * (M_PI) / 180;
        ld long2 = long2_deg * (M_PI) / 180;

        // Haversine Formula
        ld dlong = long2 - long1;
        ld dlat = lat2 - lat1;

        ld distance =
                6371 * 2 *
                asin(sqrt(pow(sin(dlat / 2), 2) + cos(lat1) * cos(lat2) * pow(sin(dlong / 2), 2)));

        return distance;
    }

    std::string getAttribute(rapidxml::xml_node<> *node, const std::string &name) {
        rapidxml::xml_attribute<> *attr = node->first_attribute(name.c_str());
        if (attr) {
            return attr->value();
        } else {
            return std::string();
        }
    }

    std::string PropertyContainer::getProperty(const std::string &name) const {
        propertiesType::const_iterator it;
        it = this->properties.find(name);

        if (it != this->properties.end())
            return it->second;
        else
            exit(1);
    }

    void PropertyContainer::setProperty(const std::string &name, const std::string &value) {
        this->properties[name] = value;
    }

    bool PropertyContainer::hasProperty(const std::string &name) const {
        propertiesType::const_iterator it = this->properties.find(name);
        if (it == this->properties.end()) {
            return false;
        } else {
            return true;
        }
    }

    PropertyContainer parseProperties(rapidxml::xml_node<> *node) {
        PropertyContainer p;
        rapidxml::xml_node<> *curNode = node->first_node("property");

        while (curNode) {
            std::string name = getAttribute(curNode, "name");
            if (name != "") {
                p.setProperty(name, curNode->value());
            }
            curNode = curNode->next_sibling("property");
        }

        return p;
    }

    void print_consumed_bw_structure(Ptr<SCION_AS> node, const std::map<uint16_t, int32_t> &index_to_AS_no) {
        for (auto const &el : node->GetBeaconServer()->bytes_sent_per_interface_per_period) {
            auto const &vector = el.second;
            std::cerr << "\nNode: " << index_to_AS_no.at(node->as_number) << " at time 0." << std::endl;
            for (auto const &element : vector) {
                std::cerr << element << " ";
            }
        }
    }

    void print_beacon_store(Ptr<SCION_AS> the_node, const std::map<uint16_t, int32_t> &index_to_AS_no) {
        std::cout << "From: " << index_to_AS_no.at(the_node->as_number) << std::endl;

        for (auto const &dst_as_beacons_pair : the_node->GetBeaconServer()->beacon_store) {
            uint16_t dst_as = dst_as_beacons_pair.first;
            auto const &same_dst_as_beacons = dst_as_beacons_pair.second;

            std::cout << "\t" << "To: " << index_to_AS_no.at(dst_as) << std::endl;

            for (auto const &beacons_from_same_nbr : same_dst_as_beacons) {
                for (auto const &the_beacon : beacons_from_same_nbr.second) {
                    if (!the_beacon->is_valid) {
                        continue;
                    }
                    std::cout << "\t" << "\t";
                    uint32_t hop_cnt = 0;
                    std::vector<link_information>::reverse_iterator hop = the_beacon->the_path.rbegin();
                    for (; hop != the_beacon->the_path.rend(); ++hop) {
                        if (hop_cnt != 0) {
                            std::cout << ", ";
                        }
                        std::cout << index_to_AS_no.at(SECOND_LOWER_16_BITS(*hop)) << ":" << LOWER_16_BITS(*hop) << ", "
                                  << index_to_AS_no.at(UPPER_16_BITS(*hop)) << ":" << SECOND_UPPER_16_BITS(*hop);
                        hop_cnt++;
                    }
                    std::cout << "; ";
                    std::cout << "latency = " << the_beacon->static_info_extension.at(static_info_type_t::LATENCY);
                    std::cout << "; ";
                    std::cout << "BWD = " << the_beacon->static_info_extension.at(static_info_type_t::BW);
                    std::cout << std::endl;
                }

            }
        }
    }

    void print_valid_beacon_counter(Ptr<SCION_AS> node, const std::map<uint16_t, int32_t> &index_to_AS_no,
                               const std::unordered_map<uint16_t, uint64_t> &counter) {
        std::cerr << "On Node: " << index_to_AS_no.at(node->as_number) << std::endl;
        std::cerr << "Src_AS:Count\n";
        for (auto const &src_as_count_pair : counter) {
            auto const &src_as = src_as_count_pair.first;
            auto const &count = src_as_count_pair.second;

            std::cerr << index_to_AS_no.at(src_as) << ":" << count << std::endl;
        }
    }

    void print_number_of_valid_beacon_entries_in_beacon_store(Ptr<SCION_AS> node,
                                                         const std::map<uint16_t, int32_t> &index_to_AS_no) {
        std::cerr << "Beacon Store on Node: " << index_to_AS_no.at(node->as_number) << std::endl;
        for (auto const &src_as_beacons_pair : node->GetBeaconServer()->beacon_store) {
            auto const &src_as = src_as_beacons_pair.first;
            auto const &beacons = src_as_beacons_pair.second;

            int count = 0;
            for (auto const &len_beacon_set_pair : beacons) {
                auto const &length = len_beacon_set_pair.first;
                auto const &beacon_set = len_beacon_set_pair.second;
                std::cout << length;
                for (auto b : beacon_set) {
                    if (b->is_valid) {
                        count++;
                    }
                }
            }
            std::cerr << "\t" << index_to_AS_no.at(src_as) << ":" << count << std::endl;
        }
        std::cerr << std::endl;
    }
}