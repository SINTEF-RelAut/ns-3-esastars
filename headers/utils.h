/**
 * @file utils.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @brief Defines functions and classes related to path quality, delay estimation,
 * parsing xml topologies, and helpers for iterating and printing node structures.
 */

#ifndef SCION_BEACONING_SIMMULATOR_UTILS_H
#define SCION_BEACONING_SIMMULATOR_UTILS_H

#include "beacon.h"
#include "scion_node.h"
#include "ns3/rapidxml.hpp"
#include <map>

/**
 * @brief Measures and returns the dissimilarity between the two paths in the passed beacons on the link-lvl.
 */
ld link_level_jaccard_distance_between_two_paths(beacon *beacon1, beacon *beacon2);

/**
 * @brief Measures and returns the dissimilarity between the two paths in the passed beacons on the AS-path-lvl.
 */
ld AS_level_jaccard_distance_between_two_paths(beacon *beacon1, beacon *beacon2);

/**
 * @brief Estimates the latency [milliseconds] between two routers given their coordinates using the great circle distance.
 */
ld calculate_great_circle_latency(ld lat1_deg, ld long1_deg, ld lat2_deg, ld long2_deg);

/**
 * @brief Searches the first attribute of the node for the value associated with name. Returns an empty string if not found.
 */
std::string getAttribute(rapidxml::xml_node<> *node, const std::string &name);

/**
 * @brief Helper class to parse properties in the provided topology xml files.
 */
class PropertyContainer {
    public:
        /**
         * @brief Returns the value of the property defined by name. If not found, *aborts the program*! Use hasProperty to check for existence first if you are unsure.
         * @see hasProperty
         */
        std::string getProperty(const std::string &name) const;

        void setProperty(const std::string &name, const std::string &value);

        /**
         * @brief Checks if a property is present.
         */
        bool hasProperty(const std::string &name) const;

    private:
        typedef std::map<std::string, std::string> propertiesType;
        propertiesType properties;
};
// TODO: Why not just make this the constructor?
 /**
   * @brief Iterates the xml-tree rooted at node and fills a propertyContainer with the node attributes.
  */
PropertyContainer parseProperties(rapidxml::xml_node<> *node);

// Debugg Helpers

// TODO: Let's change these things from std::cerr. Should probably Improve the output management a little anyways.
/**
 * @brief Prettyprints bytes_sent_per_interface_per_period on the passed SCION_Node. Currently works over std::cerr and prints only for time == 0.
 */
void print_consumed_bw_structure(SCION_Node* node);

/**
 * @brief Prettyprints the valid_intfs structure and some information on the passed node. Currently works with std::cerr.
 */
void print_valid_intfs(SCION_Node* node, std::unordered_map<uint16_t, std::vector<uint16_t>> valid_intfs);

/**
 * @brief Prettyprints the beacon store of the passed node. Currently works with std::cerr.
 */
void print_beacon_store(SCION_Node* node);

/**
 * @brief Prettyprints a valid beacon counter structure. Currently works with std::cerr.
 */
void print_valid_beacon_counter(SCION_Node* node, std::unordered_map<uint16_t, uint64_t> counter);

#endif //SCION_BEACONING_SIMMULATOR_UTILS_H
