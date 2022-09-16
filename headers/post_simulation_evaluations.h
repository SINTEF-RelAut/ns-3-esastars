//
// Created by seyedali on 17.07.21.
//

#ifndef SCION_SIMULATOR_POST_SIMULATION_EVALUATIONS_H
#define SCION_SIMULATOR_POST_SIMULATION_EVALUATIONS_H

#include <yaml-cpp/yaml.h>

#include "src/SCION/headers/beaconing/beacon.h"
#include "src/SCION/headers/scion_as.h"

namespace ns3 {
#define REGISTER_FUN(FUNC_NAME)      \
  function_name_to_function.insert ( \
      std::make_pair (#FUNC_NAME, &PostSimulationEvaluations::FUNC_NAME));

class PostSimulationEvaluations
{
public:
  PostSimulationEvaluations (YAML::Node &config, NodeContainer &AS_nodes,
                             std::map<int32_t, uint16_t> &real_to_alias_as_no,
                             std::map<uint16_t, int32_t> &alias_to_real_as_no)
      : config (config),
        AS_nodes (AS_nodes),
        real_to_alias_as_no (real_to_alias_as_no),
        alias_to_real_as_no (alias_to_real_as_no)
  {
    beaconing_period = Time (config["beacon_service"]["period"].as<std::string> ());
    last_beaconing_event_time =
        Time (config["beacon_service"]["last_beaconing"].as<std::string> ());
    first_beaconing = Time (config["beacon_service"]["first_beaconing"].as<std::string> ());
    expiration_period = Time (config["beacon_service"]["expiration_period"].as<std::string> ())
                            .ToInteger (Time::MIN);
    beaconing_policy_str = config["beacon_service"]["policy"].as<std::string> ();

    REGISTER_FUN (PrintTrafficSentFromCollectorsPerDstPerPeriod)
    REGISTER_FUN (PrintAllDiscoveredPaths)
    REGISTER_FUN (PrintAllPathsAttributes)
    REGISTER_FUN (PrintDistributionOfPathsWithSpecificHopCount)
    REGISTER_FUN (PrintNoBeaconsPerInterface)
    REGISTER_FUN (PrintNoBeaconsPerInterfacePerDstOrOpt)
    REGISTER_FUN (PrintConsumedBWAtEachPeriod)
    //             REGISTER_FUN(PrintPathQualities)
    //             REGISTER_FUN(Evaluate_S_T_Connectivity)
    REGISTER_FUN (PrintMinimumLatencyDist)
    REGISTER_FUN (PrintPathNoDistribution)
    REGISTER_FUN (FindMinLatencyToDNSRootServers)
    REGISTER_FUN (PrintPathPollutionIndex)
    REGISTER_FUN (PrintLeastPollutingPaths)
    REGISTER_FUN (PrintBestPerHopPollutionIndexes)
    //             REGISTER_FUN(PrintTransitTrafficBaseline)
    REGISTER_FUN (InvestigateAffectedTimeServers)
    REGISTER_FUN (PrintBeaconStores)
    REGISTER_FUN (PrintConsumedBWForBeaconing)

    REGISTER_FUN (PrintNumberOfValidBeaconEntriesInBeaconStore)
  }

  void DoFinalEvaluations ();

  void PrintTrafficSentFromCollectorsPerDstPerPeriod ();

  void PrintAllDiscoveredPaths ();

  void PrintAllPathsAttributes ();

  void PrintDistributionOfPathsWithSpecificHopCount ();

  void PrintNoBeaconsPerInterface ();

  void PrintNoBeaconsPerInterfacePerDstOrOpt ();

  void PrintConsumedBWAtEachPeriod ();

  void PrintPathQualities ();

  void Evaluate_S_T_Connectivity ();

  void PrintMinimumLatencyDist ();

  void PrintPathNoDistribution ();

  void FindMinLatencyToDNSRootServers ();

  void PrintPathPollutionIndex ();

  void PrintLeastPollutingPaths ();

  void PrintBestPerHopPollutionIndexes ();

  void PrintTransitTrafficBaseline ();

  void InvestigateAffectedTimeServers ();

  void PrintConsumedBWForBeaconing ();

  void PrintBeaconStores ();

  void PrintNumberOfValidBeaconEntriesInBeaconStore ();

  friend void
  sort_beacons_by_pollution_by_latency (NodeContainer &AS_nodes, SCION_AS *AS1, SCION_AS *AS2,
                                        std::string beaconing_policy_str,
                                        std::map<double, std::map<double, std::set<Beacon *>>>
                                            &sorted_beacons_by_pollution_by_latency);

private:
  YAML::Node &config;
  NodeContainer &AS_nodes;
  std::map<int32_t, uint16_t> &real_to_alias_as_no;
  std::map<uint16_t, int32_t> &alias_to_real_as_no;

  Time beaconing_period;
  Time last_beaconing_event_time;
  Time first_beaconing;

  uint16_t expiration_period;
  std::string beaconing_policy_str;

  std::unordered_map<std::string, void (PostSimulationEvaluations::*) ()> function_name_to_function;
};

void sort_beacons_by_pollution_by_latency (
    NodeContainer &AS_nodes, SCION_AS *AS1, SCION_AS *AS2, std::string beaconing_policy_str,
    std::map<double, std::map<double, std::set<Beacon *>>> &sorted_beacons_by_pollution_by_latency);

} // namespace ns3
#endif //SCION_SIMULATOR_POST_SIMULATION_EVALUATIONS_H
