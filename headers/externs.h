//
// Created by seyedali on 21.09.21.
//

#ifndef SCION_SIMULATOR_EXTERNS_H
#define SCION_SIMULATOR_EXTERNS_H

#include <map>

#include "ns3/node-container.h"

extern std::map<int32_t, uint16_t> real_to_alias_as_no;
extern std::map<uint16_t, int32_t> alias_to_real_as_no;
extern std::map<uint16_t, uint16_t> as_to_isd_map;
extern uint32_t NUM_CORE;
extern ns3::NodeContainer nodes;

#endif //SCION_SIMULATOR_EXTERNS_H
