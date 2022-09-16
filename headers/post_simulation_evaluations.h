//
// Created by seyedali on 17.07.21.
//

#ifndef SCION_SIMULATOR_POST_SIMULATION_EVALUATIONS_H
#define SCION_SIMULATOR_POST_SIMULATION_EVALUATIONS_H

#include <yaml-cpp/yaml.h>

#include "src/SCION/headers/beaconing/beacon.h"
#include "src/SCION/headers/scion_as.h"

namespace ns3 {
#define REGISTER_FUN(FUNC_NAME)   \
  functionNameToFunction.insert ( \
      std::make_pair (#FUNC_NAME, &PostSimulationEvaluations::FUNC_NAME));

class PostSimulationEvaluations
{
public:
  PostSimulationEvaluations (YAML::Node &config, NodeContainer &asNodes,
                             std::map<int32_t, uint16_t> &realToAliasAsNo,
                             std::map<uint16_t, int32_t> &aliasToRealAsNo)
      : config (config),
        asNodes (asNodes),
        realToAliasAsNo (realToAliasAsNo),
        aliasToRealAsNo (aliasToRealAsNo)
  {
    beaconingPeriod = Time (config["beacon_service"]["period"].as<std::string> ());
    lastBeaconingEventTime =
        Time (config["beacon_service"]["last_beaconing"].as<std::string> ());
    firstBeaconing = Time (config["beacon_service"]["first_beaconing"].as<std::string> ());
    expirationPeriod = Time (config["beacon_service"]["expiration_period"].as<std::string> ())
                            .ToInteger (Time::MIN);
    beaconingPolicyStr = config["beacon_service"]["policy"].as<std::string> ();

    REGISTER_FUN (PrintTrafficSentFromCollectorsPerDstPerPeriod)
    REGISTER_FUN (PrintAllDiscoveredPaths)
    REGISTER_FUN (PrintAllPathsAttributes)
    REGISTER_FUN (PrintDistributionOfPathsWithSpecificHopCount)
    REGISTER_FUN (PrintNoBeaconsPerInterface)
    REGISTER_FUN (PrintNoBeaconsPerInterfacePerDstOrOpt)
    REGISTER_FUN (PrintConsumedBwAtEachPeriod)
    //             REGISTER_FUN(PrintPathQualities)
    //             REGISTER_FUN(EvaluateStConnectivity)
    REGISTER_FUN (PrintMinimumLatencyDist)
    REGISTER_FUN (PrintPathNoDistribution)
    REGISTER_FUN (FindMinLatencyToDnsRootServers)
    REGISTER_FUN (PrintPathPollutionIndex)
    REGISTER_FUN (PrintLeastPollutingPaths)
    REGISTER_FUN (PrintBestPerHopPollutionIndexes)
    //             REGISTER_FUN(PrintTransitTrafficBaseline)
    REGISTER_FUN (InvestigateAffectedTimeServers)
    REGISTER_FUN (PrintBeaconStores)
    REGISTER_FUN (PrintConsumedBwForBeaconing)

    REGISTER_FUN (PrintNumberOfValidBeaconEntriesInBeaconStore)
  }

  void DoFinalEvaluations ();

  void PrintTrafficSentFromCollectorsPerDstPerPeriod ();

  void PrintAllDiscoveredPaths ();

  void PrintAllPathsAttributes ();

  void PrintDistributionOfPathsWithSpecificHopCount ();

  void PrintNoBeaconsPerInterface ();

  void PrintNoBeaconsPerInterfacePerDstOrOpt ();

  void PrintConsumedBwAtEachPeriod ();

  void PrintPathQualities ();

  void EvaluateStConnectivity ();

  void PrintMinimumLatencyDist ();

  void PrintPathNoDistribution ();

  void FindMinLatencyToDnsRootServers ();

  void PrintPathPollutionIndex ();

  void PrintLeastPollutingPaths ();

  void PrintBestPerHopPollutionIndexes ();

  void PrintTransitTrafficBaseline ();

  void InvestigateAffectedTimeServers ();

  void PrintConsumedBwForBeaconing ();

  void PrintBeaconStores ();

  void PrintNumberOfValidBeaconEntriesInBeaconStore ();

  friend void
  SortBeaconsByPollutionByLatency (NodeContainer &asNodes, ScionAs *as1, ScionAs *as2,
                                        std::string beaconingPolicyStr,
                                        std::map<double, std::map<double, std::set<Beacon *>>>
                                            &sortedBeaconsByPollutionByLatency);

private:
  YAML::Node &config;
  NodeContainer &asNodes;
  std::map<int32_t, uint16_t> &realToAliasAsNo;
  std::map<uint16_t, int32_t> &aliasToRealAsNo;

  Time beaconingPeriod;
  Time lastBeaconingEventTime;
  Time firstBeaconing;

  uint16_t expirationPeriod;
  std::string beaconingPolicyStr;

  std::unordered_map<std::string, void (PostSimulationEvaluations::*) ()> functionNameToFunction;
};

void SortBeaconsByPollutionByLatency (
    NodeContainer &asNodes, ScionAs *as1, ScionAs *as2, std::string beaconingPolicyStr,
    std::map<double, std::map<double, std::set<Beacon *>>> &sortedBeaconsByPollutionByLatency);

} // namespace ns3
#endif //SCION_SIMULATOR_POST_SIMULATION_EVALUATIONS_H
