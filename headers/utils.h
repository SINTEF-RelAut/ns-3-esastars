/**
 * @file utils.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 */

#ifndef SCION_BEACONING_SIMULATOR_UTILS_H
#define SCION_BEACONING_SIMULATOR_UTILS_H

#include <map>

#include "ns3/rapidxml.hpp"

#include "src/SCION/headers/beaconing/beacon.h"
#include "src/SCION/headers/scion_as.h"

namespace ns3 {

#define UPPER_16_BITS(input) ((uint16_t) ((input) >> 48))

#define LOWER_16_BITS(input) ((uint16_t) ((input) & 0x000000000000ffff))

#define SECOND_UPPER_16_BITS(input) ((uint16_t) (((input) & 0x0000ffff00000000) >> 32))

#define SECOND_LOWER_16_BITS(input) ((uint16_t) (((input) & 0x00000000ffff0000) >> 16))

#define UPPER_32_BITS(input) ((uint32_t) ((input) >> 32))

#define LOWER_32_BITS(input) ((uint32_t) ((input) & 0x00000000ffffffff))

uint64_t truly_random_generator(uint64_t i);

uint64_t random_generator(uint64_t i);

double GetMedian( std::multiset<int64_t>& data);

ld link_level_jaccard_distance_between_two_paths (Beacon *beacon1, Beacon *beacon2);

ld AS_level_jaccard_distance_between_two_paths (Beacon *beacon1, Beacon *beacon2);

ld calculate_great_circle_latency (ld lat1_deg, ld long1_deg, ld lat2_deg, ld long2_deg);

ld calculate_great_circle_distance (ld lat1_deg, ld long1_deg, ld lat2_deg, ld long2_deg);

std::string getAttribute (rapidxml::xml_node<> *node, const std::string &name);

class PropertyContainer
{
  public:

    std::string getProperty (const std::string &name) const;
    void setProperty (const std::string &name, const std::string &value);
    bool hasProperty (const std::string &name) const;

  private:
    typedef std::map<std::string, std::string> propertiesType;
    propertiesType properties;
};

PropertyContainer parseProperties (rapidxml::xml_node<> *node);

void print_consumed_bw_structure (Ptr<SCION_AS> node, const std::map<uint16_t, int32_t>& index_to_AS_no);

void print_beacon_store (Ptr<SCION_AS> the_node, const std::map<uint16_t, int32_t>& index_to_AS_no);

void print_valid_beacon_counter (Ptr<SCION_AS> node, const std::map<uint16_t, int32_t>& index_to_AS_no, std::unordered_map<uint16_t, uint64_t> counter);

void
print_number_of_valid_beacon_entries_in_beacon_store (Ptr<SCION_AS> node, const std::map<uint16_t, int32_t>& index_to_AS_no);
}
#endif //SCION_BEACONING_SIMMULATOR_UTILS_H