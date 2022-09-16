/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2022 ETH Zuerich
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Seyedali Tabaeiaghdaei seyedali.tabaeiaghdaei@inf.ethz.ch
 */

#ifndef SCION_SIMULATOR_PRE_SIMULATION_SETUP_H
#define SCION_SIMULATOR_PRE_SIMULATION_SETUP_H

#include <istream>
#include <omp.h>
#include <random>
#include <set>
#include <yaml-cpp/yaml.h>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/nstime.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/ptr.h"

#include "src/SCION/model/beaconing/baseline.h"
#include "src/SCION/model/beaconing/beacon_server.h"
#include "src/SCION/model/beaconing/diversity_age_based.h"
#include "src/SCION/model/beaconing/green_beaconing.h"
#include "src/SCION/model/beaconing/latency_optimized_beaconing.h"
#include "src/SCION/model/beaconing/scionlab_algo.h"
#include "externs.h"
#include "path_server.h"
#include "post_simulation_evaluations.h"
#include "schedule_periodic_events.h"
#include "scion_as.h"
#include "scion_core_as.h"
#include "scion_host.h"
#include "time_server.h"
#include "user_defined_events.h"
#include "utils.h"

namespace ns3 {
void SetTimeResolution (const std::string &time_res_str);

//rapidxml::xml_node<>* SetupTopologyFile (std::string topology_name);

void InstantiateASesFromTopo (rapidxml::xml_node<> *xml_root,
                              std::map<int32_t, uint16_t> &real_to_alias_as_no,
                              std::map<uint16_t, int32_t> &alias_to_real_as_no,
                              ns3::NodeContainer &AS_nodes, const YAML::Node &config);

void InstantiatePathServers (const YAML::Node &config, const ns3::NodeContainer &AS_nodes);

void GetMaliciousTimeRefAndTimeServer (const ns3::NodeContainer &AS_nodes, const YAML::Node &config,
                                       std::vector<std::string> &time_reference_types,
                                       std::vector<std::string> &time_server_types);

void GetTimeServiceSnapShotTypes (const ns3::NodeContainer &AS_nodes, const YAML::Node &config,
                                  std::vector<std::string> &snapshot_types);

void GetTimeServiceAlgVersions (const ns3::NodeContainer &AS_nodes, const YAML::Node &config,
                                std::vector<std::string> &alg_versions,
                                const std::vector<std::string> &snapshot_types);

void InstantiateTimeServers (const YAML::Node &config, const ns3::NodeContainer &AS_nodes);

void InstantiateLinksFromTopo (rapidxml::xml_node<> *xml_root, ns3::NodeContainer &AS_nodes,
                               const std::map<int32_t, uint16_t> &real_to_alias_as_no,
                               const YAML::Node &config);

void InitializeASesAttributes (const NodeContainer &AS_nodes,
                               std::map<int32_t, uint16_t> &real_to_alias_as_no,
                               rapidxml::xml_node<> *xml_node, const YAML::Node &config);

bool OnlyPropagationDelay (const YAML::Node &config);
} // namespace ns3
#endif //SCION_SIMULATOR_PRE_SIMULATION_SETUP_H
