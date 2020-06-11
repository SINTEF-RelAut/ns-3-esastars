//
// Created by chrissy on 10.06.20.
//

#include "utils.h"
#include <set>
#include <cmath>

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

std::string getAttribute(rapidxml::xml_node<> *node, const std::string &name) {
    rapidxml::xml_attribute<> *attr = node->first_attribute(name.c_str());
    if (attr) {
        return attr->value();
    } else {
        return std::string();
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
