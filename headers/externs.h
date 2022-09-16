//
// Created by seyedali on 21.09.21.
//

#ifndef SCION_SIMULATOR_EXTERNS_H
#define SCION_SIMULATOR_EXTERNS_H

#include <map>

#include "ns3/node-container.h"

extern std::map<int32_t, uint16_t> g_realToAliasAsNo;
extern std::map<uint16_t, int32_t> g_aliasToRealAsNo;
extern std::map<uint16_t, uint16_t> g_asToIsdMap;
extern uint32_t g_numCore;
extern ns3::NodeContainer g_nodes;

#endif //SCION_SIMULATOR_EXTERNS_H
