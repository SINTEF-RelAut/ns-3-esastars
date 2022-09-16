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
  ScionCoreAs (uint32_t system_id, bool parallel_scheduler, uint16_t as_number,
                 rapidxml::xml_node<> *xml_node, const YAML::Node &config,
                 bool malicious_border_routers, Time local_time)
      : ScionAs (system_id, parallel_scheduler, as_number, xml_node, config,
                  malicious_border_routers, local_time)
  {
  }
};
} // namespace ns3
#endif //SCION_SIMULATOR_SCION_CORE_AS_H
