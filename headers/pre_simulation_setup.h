//
// Created by Seyedali Tabaeiaghdaei on 24.03.22.
//

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

#include "src/SCION/headers/beaconing/baseline.h"
#include "src/SCION/headers/beaconing/beacon_server.h"
#include "src/SCION/headers/beaconing/diversity_age_based.h"
#include "src/SCION/headers/beaconing/green_beaconing.h"
#include "src/SCION/headers/beaconing/latency_optimized_beaconing.h"
#include "src/SCION/headers/beaconing/scionlab_algo.h"
#include "src/SCION/headers/externs.h"
#include "src/SCION/headers/path_server.h"
#include "src/SCION/headers/post_simulation_evaluations.h"
#include "src/SCION/headers/schedule_periodic_events.h"
#include "src/SCION/headers/scion_as.h"
#include "src/SCION/headers/scion_core_as.h"
#include "src/SCION/headers/scion_host.h"
#include "src/SCION/headers/time_server.h"
#include "src/SCION/headers/user_defined_events.h"
#include "src/SCION/headers/utils.h"

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
