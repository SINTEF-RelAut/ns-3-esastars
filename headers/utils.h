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

std::vector<std::string> &Split (const std::string &s, char delim, std::vector<std::string> &elems);

Ld_t LinkLevelJaccardDistanceBetweenTwoPaths (Beacon *beacon1, Beacon *beacon2);

Ld_t AsLevelJaccardDistanceBetweenTwoPaths (Beacon *beacon1, Beacon *beacon2);

Ld_t CalculateGreatCircleLatency (Ld_t lat1Deg, Ld_t long1Deg, Ld_t lat2Deg, Ld_t long2Deg);

Ld_t CalculateGreatCircleDistance (Ld_t lat1Deg, Ld_t long1Deg, Ld_t lat2Deg, Ld_t long2Deg);

std::string GetAttribute (rapidxml::xml_node<> *node, const std::string &name);

class PropertyContainer
{
public:
  std::string GetProperty (const std::string &name) const;
  void SetProperty (const std::string &name, const std::string &value);
  bool HasProperty (const std::string &name) const;

private:
  typedef std::map<std::string, std::string> PropertiesType_t;
  PropertiesType_t properties;
};

PropertyContainer ParseProperties (rapidxml::xml_node<> *node);

} // namespace ns3
#endif //SCION_SIMULATOR_UTILS_H