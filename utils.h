//
// Created by chrissy on 10.06.20.
//

#ifndef SCION_BEACONING_SIMMULATOR_UTILS_H
#define SCION_BEACONING_SIMMULATOR_UTILS_H

#include "beacon.h"
#include "ns3/rapidxml.hpp"
#include <map>

ld link_level_jaccard_distance_between_two_paths(beacon *beacon1, beacon *beacon2);

ld AS_level_jaccard_distance_between_two_paths(beacon *beacon1, beacon *beacon2);

ld calculate_great_circle_latency(ld lat1_deg, ld long1_deg, ld lat2_deg, ld long2_deg);

std::string getAttribute(rapidxml::xml_node<> *node, const std::string &name);

class PropertyContainer {
    public:

        std::string getProperty(const std::string &name) const;

        void setProperty(const std::string &name, const std::string &value);

        bool hasProperty(const std::string &name) const;

    private:
        typedef std::map<std::string, std::string> propertiesType;
        propertiesType properties;
};

PropertyContainer parseProperties(rapidxml::xml_node<> *node);

#endif //SCION_BEACONING_SIMMULATOR_UTILS_H
