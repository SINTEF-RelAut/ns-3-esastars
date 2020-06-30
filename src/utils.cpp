/**
 * @file utils.cpp
 * @see utils.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 */

#include "../headers/utils.h"
#include "../headers/scion_node.h"
#include <set>
#include <cmath>

/**
 * The jaccard distance measures the dissimilarity between two sets. This function considers the AS number and the
 * egress interface number of each link on the path, since this is enough to uniquely identify the link.
 *
 * @param beacon1 A beacon containing a path. @see path
 * @param beacon2 A beacon containing a path. @see path
 * @return The link-level jaccard distance between the two paths.
 */
ld link_level_jaccard_distance_between_two_paths(beacon *beacon1, beacon *beacon2) {
    std::set<uint32_t> set_of_links_on_path1;
    int32_t intersection = 0;

    for (auto const &link_info : *beacon1->the_path) {
        set_of_links_on_path1.insert(((uint32_t) link_info[0]) << 16 | (uint32_t) link_info[1]);
    }

    for (auto const &link_info : *beacon2->the_path) {
        if (set_of_links_on_path1.find(((uint32_t) link_info[0]) << 16 | (uint32_t) link_info[1]) !=
            set_of_links_on_path1.end()) {
            intersection++;
        } else {
            set_of_links_on_path1.insert(((uint32_t) link_info[0]) << 16 | (uint32_t) link_info[1]);
        }
    }

    return 1 - 1.0 * intersection / set_of_links_on_path1.size();
}

/**
 * The jaccard distance measures the dissimilarity between two sets. This function considers only the AS number and
 * therefore the coarse grained AS-level paths.
 *
 * @param beacon1 A beacon containing a path. @see path
 * @param beacon2 A beacon containing a path. @see path
 * @return The AS-level jaccard distance between the two paths.
 */
ld AS_level_jaccard_distance_between_two_paths(beacon *beacon1, beacon *beacon2) {
    std::set<uint16_t> set_of_ASes_on_path1;
    int32_t intersection = 0;

    for (auto const &link_info : *beacon1->the_path) {
        set_of_ASes_on_path1.insert(link_info[0]);
    }

    for (auto const &link_info : *beacon2->the_path) {
        if (set_of_ASes_on_path1.find(link_info[0]) != set_of_ASes_on_path1.end()) {
            intersection++;
        } else {
            set_of_ASes_on_path1.insert(link_info[0]);
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
ld calculate_great_circle_latency(ld lat1_deg, ld long1_deg, ld lat2_deg, ld long2_deg) {
    ld lat1 = lat1_deg * (M_PI) / 180;
    ld long1 = long1_deg * (M_PI) / 180;
    ld lat2 = lat2_deg * (M_PI) / 180;
    ld long2 = long2_deg * (M_PI) / 180;

    // Haversine Formula
    ld dlong = long2 - long1;
    ld dlat = lat2 - lat1;

    ld distance = 6371 * 2 * asin(sqrt(pow(sin(dlat / 2), 2) + cos(lat1) * cos(lat2) * pow(sin(dlong / 2), 2)));

    // 0.005 millisecods of latency per kilometer
    ld latency = distance * 0.005;

    return latency;
}

/**
 * @param node The node to be searched.
 * @param name The attribute name.
 * @return The associated value or an empty string if not found.
 */
std::string getAttribute(rapidxml::xml_node<> *node, const std::string &name) {
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

/**
 * @param name Defines the property.
 * @return True if the property exists, false otherwise.
 */
bool PropertyContainer::hasProperty(const std::string &name) const {
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


// DEBUGG helpers
/**
 * @param node The node whose bandwith stats you want to print (at time==0).
 */
void print_consumed_bw_structure(SCION_Node* node){
    for(auto const&[time, vector]: node->bytes_sent_per_interface_per_period){
        if(time == 0){ // TODO: Maybe make more generic?

            std::cerr << "\nNode: " << node->as_number << " at time 0."<<std::endl;
            for(auto element: vector){
                std::cerr << element << " ";
            }
        }
    }
}

/**
 * @param node The node using these interfaces.
 * @param valid_intfs The valid interfaces returned by calling @see SCION_Node.GetValidInterfaces.
 */
void print_valid_intfs(SCION_Node* node, std::unordered_map<uint16_t, std::vector<uint16_t>> valid_intfs){
    std::cerr << "\n\nNode: " << node->as_number << " Works on the interfaces: " << std::endl;
    for(auto &[as_no, interface_rel_pairs]:valid_intfs){
        std::cerr << as_no << ": ";
        for(auto &intf_no: interface_rel_pairs){
            std::cerr << "[" << intf_no << "], ";
        }
    }
    std::cerr << std::endl;
}

/**
 * @param node The node holding the beacon_store to be printed.
 */
void print_beacon_store(SCION_Node* node){
    const std::string first_lvl_offset = "\t";
    const std::string second_lvl_offset = "\t\t";
    const std::string third_lvl_offset = "\t\t\t";
    std::cerr << "From: " << node->as_number << std::endl;
    for(auto const [dst_as_no, equal_as_beacons]:node->beacon_store){
        std::cerr << first_lvl_offset << "To: " << dst_as_no << std::endl;
        for(auto const [length, beacons]: *equal_as_beacons){
            std::cerr << second_lvl_offset << length << ":" <<std::endl;
            for(auto const beacon: *beacons){
                std::cerr << third_lvl_offset;
                for(auto const path:*beacon->the_path){
                    std::cerr  << "->" << path[0] << ":" << path[1] << "]->[" << path[2] << ":" << path[3];
                }
                std::cerr << std::endl;
            }
        }
    }

}