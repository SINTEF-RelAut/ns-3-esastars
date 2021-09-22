//
// Created by seyedali on 21.09.21.
//

#ifndef NS_3_BEACONING_SIMULATOR_EXTERNS_H
#define NS_3_BEACONING_SIMULATOR_EXTERNS_H

#include <map>

#include "ns3/node-container.h"


extern std::map<uint16_t, uint16_t> as_to_isd_map;
extern uint32_t NUM_CORE;
extern ns3::NodeContainer nodes;


#endif //NS_3_BEACONING_SIMULATOR_EXTERNS_H
