/**
 * @file utils.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 */

#ifndef SCION_SIMULATOR_UTILS_H
#define SCION_SIMULATOR_UTILS_H

#include <map>
#include <set>

#include "ns3/rapidxml.hpp"

#include "src/SCION/headers/beaconing/beacon.h"

namespace ns3 {

#define UPPER_16_BITS(input) ((uint16_t) ((input) >> 48))

#define LOWER_16_BITS(input) ((uint16_t) ((input) &0x000000000000ffff))

#define SECOND_UPPER_16_BITS(input) ((uint16_t) (((input) &0x0000ffff00000000) >> 32))

#define SECOND_LOWER_16_BITS(input) ((uint16_t) (((input) &0x00000000ffff0000) >> 16))

#define UPPER_32_BITS(input) ((uint32_t) ((input) >> 32))

#define LOWER_32_BITS(input) ((uint32_t) ((input) &0x00000000ffffffff))

double GetMedian (std::multiset<int64_t> &data);

std::vector<std::string> &split (const std::string &s, char delim, std::vector<std::string> &elems);

ld link_level_jaccard_distance_between_two_paths (Beacon *beacon1, Beacon *beacon2);

ld AS_level_jaccard_distance_between_two_paths (Beacon *beacon1, Beacon *beacon2);

ld calculate_great_circle_latency (ld lat1_deg, ld long1_deg, ld lat2_deg, ld long2_deg);

ld calculate_great_circle_distance (ld lat1_deg, ld long1_deg, ld lat2_deg, ld long2_deg);

std::string getAttribute (rapidxml::xml_node<> *node, const std::string &name);

class PropertyContainer
{
public:
  std::string GetProperty (const std::string &name) const;
  void setProperty (const std::string &name, const std::string &value);
  bool HasProperty (const std::string &name) const;

private:
  typedef std::map<std::string, std::string> propertiesType;
  propertiesType properties;
};

PropertyContainer parseProperties (rapidxml::xml_node<> *node);

} // namespace ns3
#endif //SCION_SIMULATOR_UTILS_H