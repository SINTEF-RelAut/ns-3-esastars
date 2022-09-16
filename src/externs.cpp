//
// Created by seyedali on 21.09.21.
//

#include "src/SCION/headers/externs.h"

std::map<int32_t, uint16_t> g_realToAliasAsNo;
std::map<uint16_t, int32_t> g_aliasToRealAsNo;
std::map<uint16_t, uint16_t> g_asToIsdMap;
uint32_t g_numCore;
ns3::NodeContainer g_nodes;
