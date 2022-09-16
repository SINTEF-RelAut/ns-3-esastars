//
// Created by seyedali on 21.09.21.
//

#include "src/SCION/headers/externs.h"

std::map<int32_t, uint16_t> real_to_alias_as_no;
std::map<uint16_t, int32_t> alias_to_real_as_no;
std::map<uint16_t, uint16_t> as_to_isd_map;
uint32_t NUM_CORE;
ns3::NodeContainer nodes;
