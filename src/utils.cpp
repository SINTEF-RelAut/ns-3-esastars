/**
 * @file utils.cpp
 * @see utils.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @brief Implements the functions related to path quality, delay estimation, parsing xml topologies,
 * and helpers for iterating and printing various structures on the nodes.
 */

#include "src/SCION/headers/utils.h"
#include "../headers/beaconing/beacon_server.h"

#include <set>
#include <cmath>

/**
 * The jaccard distance measures the dissimilarity between two sets. This function considers the AS number and the
 * egress interface number of each link on the path, since this is enough to uniquely identify the link.
 *
 * @see path
 * @param beacon1 A beacon containing a path.
 * @param beacon2 A beacon containing a path.
 * @return The link-level jaccard distance between the two paths.
 */
namespace ns3 {
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


/**
 * The jaccard distance measures the dissimilarity between two sets. This function considers only the AS number and
 * therefore the coarse grained AS-level paths.
 *
 * @see path
 * @param beacon1 A beacon containing a path.
 * @param beacon2 A beacon containing a path.
 * @return The AS-level jaccard distance between the two paths.
 */
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


/**
 * Calculates the great circle distance between the two coordinate pairs. Uses this distance and the assumption of
 * 0.005 milliseconds of latency per kilometer to return a latency estimation between the two routers.
 * @param lat1_deg Latitude of first router.
 * @param long1_deg Longitude of first router.
 * @param lat2_deg Latitude of second router.
 * @param long2_deg Longitude of second router.
 * @return An estimated latency between the two routers.
 */
    ld
    calculate_great_circle_latency(ld lat1_deg, ld long1_deg, ld lat2_deg, ld long2_deg) {


        ld distance = calculate_great_circle_distance(lat1_deg, long1_deg, lat2_deg, long2_deg);
        // 0.005 millisecods of latency per kilometer
        ld latency = distance * 0.005;
        return latency;
    }


    ld
    calculate_great_circle_distance(ld lat1_deg, ld long1_deg, ld lat2_deg, ld long2_deg) {
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

/**
 * @param node The node to be searched.
 * @param name The attribute name.
 * @return The associated value or an empty string if not found.
 */
    std::string
    getAttribute(rapidxml::xml_node<> *node, const std::string &name) {
        rapidxml::xml_attribute<> *attr = node->first_attribute(name.c_str());
        if (attr) {
            return attr->value();
        } else {
            return std::string();
        }
    }

/**
 * @param name Defines the property we want to get.
 * @return The associated value to name. Aborts the Program if not found.
 */
    std::string
    PropertyContainer::getProperty(const std::string &name) const {
        propertiesType::const_iterator it;
        it = this->properties.find(name);

        if (it != this->properties.end())
            return it->second;
        else
            exit(1);
    }

/**
 * @param name Defines the property.
 * @param value The value to set this property to.
 */
    void
    PropertyContainer::setProperty(const std::string &name, const std::string &value) {
        this->properties[name] = value;
    }

/**
 * @param name Defines the property.
 * @return True if the property exists, false otherwise.
 */
    bool
    PropertyContainer::hasProperty(const std::string &name) const {
        propertiesType::const_iterator it = this->properties.find(name);

        if (it == this->properties.end()) {
            return false;
        } else {
            return true;
        }
    }

/**
 * @param node The root of the xml-tree you would like to traverse.
 * @return A property container containing all the node attributes in the tree.
 */
    PropertyContainer
    parseProperties(rapidxml::xml_node<> *node) {
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

// DEBUG helpers

/**
 * @param node The node whose bandwidth stats you want to print.
 */
    void
    print_consumed_bw_structure(Ptr<SCION_AS> node, const std::map<uint16_t, int32_t> &index_to_AS_no) {
        for (auto const &el : node->GetBeaconServer()->bytes_sent_per_interface_per_period) {
            auto const &vector = el.second;
            std::cerr << "\nNode: " << index_to_AS_no.at(node->as_number) << " at time 0." << std::endl;
            for (auto const &element : vector) {
                std::cerr << element << " ";
            }
        }
    }


/**
 * @param node The node holding the beacon_store to be printed.
 * @param out Where to print the beacon store. 
 */
    void
    print_beacon_store(Ptr<SCION_AS> the_node, const std::map<uint16_t, int32_t> &index_to_AS_no) {
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
                    int hop_cnt = 0;
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
                    std::cout << "latency = " << the_beacon->latency_stat;
                    std::cout << "; ";
                    std::cout << "BWD = " << the_beacon->bwd_stat;
                    std::cout << std::endl;
                }

            }
        }
    }

/**
 * @param node The node who owns the counters.
 * @param counter The counter structure you want to print.
 */
    void
    print_valid_beacon_counter(Ptr<SCION_AS> node, const std::map<uint16_t, int32_t> &index_to_AS_no,
                               const std::unordered_map<uint16_t, uint64_t> &counter) {
        std::cerr << "On Node: " << index_to_AS_no.at(node->as_number) << std::endl;
        std::cerr << "Src_AS:Count\n";
        for (auto const &src_as_count_pair : counter) {
            auto const &src_as = src_as_count_pair.first;
            auto const &count = src_as_count_pair.second;

            std::cerr << index_to_AS_no.at(src_as) << ":" << count << std::endl;
        }
    }

/**
 * @param node The node whose beacon store you want to analyze.
 */
    void
    print_number_of_valid_beacon_entries_in_beacon_store(Ptr<SCION_AS> node,
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