/**
 * @file scion_core_as.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see scion_as.h
 * @brief Defines the SCION Core AS.
 *
 */

#ifndef SCION_SIMULATOR_SCION_CORE_AS_H
#define SCION_SIMULATOR_SCION_CORE_AS_H

#include "src/SCION/headers/scion_as.h"

namespace ns3 {

class ScionCoreAs : public ScionAs
{
public:
  ScionCoreAs (uint32_t systemId, bool parallelScheduler, uint16_t asNumber,
                 rapidxml::xml_node<> *xmlNode, const YAML::Node &config,
                 bool maliciousBorderRouters, Time localTime)
      : ScionAs (systemId, parallelScheduler, asNumber, xmlNode, config, maliciousBorderRouters,
                 localTime)
  {
  }
};
} // namespace ns3
#endif //SCION_SIMULATOR_SCION_CORE_AS_H
