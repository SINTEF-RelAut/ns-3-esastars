//
// Created by Seyedali Tabaeiaghdaei on 06.04.22.
//

#ifndef SCION_SIMULATOR_ON_DEMAND_OPTIMIZATION_H
#define SCION_SIMULATOR_ON_DEMAND_OPTIMIZATION_H

#include "src/SCION/headers/beaconing/beacon_server.h"

namespace ns3 {
typedef std::multimap<Ld_t, Beacon *, std::greater<Ld_t>>
    BeaconsWithTheSameOptTargetAndIngressIfGroup_t;
typedef std::map<uint16_t, BeaconsWithTheSameOptTargetAndIngressIfGroup_t>
    BeaconsWithTheSameOptTarget_t;
typedef std::unordered_map<const OptimizationTarget *, BeaconsWithTheSameOptTarget_t>
    BeaconsGroupedByOptimizationTargetsAndIngressIfGroup_t;

class OnDemandOptimization : public BeaconServer
{
public:
  OnDemandOptimization (ScionAs *as, bool parallelScheduler, rapidxml::xml_node<> *xmlNode,
                        const YAML::Node &config)
      : BeaconServer (as, parallelScheduler, xmlNode, config)
  {
    pullBasedBeaconsGroupedByOptimizationTargetsAndIngressIfGroup.resize (2);
    firstPullBasedInterval =
        Time (config["beacon_service"]["first_pull_based_interval"].as<std::string> ())
            .ToInteger (Time::MIN);
    pullBasedDisseminationToInitiationFrequency =
        stoi (config["beacon_service"]["pull_based_dissemination_to_initiation_frequency"]
                  .as<std::string> ());
    desiredMaxTolerableLinkFailures =
        stoi (config["beacon_service"]["desired_max_tolerable_link_failures"].as<std::string> ());
    rapidxml::xml_node<> *curTarget = xmlNode->first_node ("target");
    while (curTarget)
      {
        uint16_t targetId = std::stoi (curTarget->first_node ("target_id")->value ());

        OptimizationCriteria_t optimizationCriteria;
        PropertyContainer p = ParseProperties (curTarget);
        if (p.HasProperty ("bw") && std::stof (p.GetProperty ("bw")) > 0.001)
          {
            optimizationCriteria.insert (
                std::make_pair (StaticInfoType::bw, std::stof (p.GetProperty ("bw"))));
          }

        if (p.HasProperty ("latency") && std::stof (p.GetProperty ("latency")) > 0.001)
          {
            optimizationCriteria.insert (std::make_pair (StaticInfoType::latency,
                                                          std::stof (p.GetProperty ("latency"))));
          }

        std::string direction = curTarget->first_node ("direction")->value ();
        OptimizationDirection optimizationDirection = OptimizationDirection::symmetric;
        if (direction == "forward")
          {
            optimizationDirection = OptimizationDirection::forward;
          }
        else if (direction == "backward")
          {
            optimizationDirection = OptimizationDirection::backward;
          }

        uint16_t groupId = std::stoi (curTarget->first_node ("group_id")->value ());
        uint16_t noBeacons = std::stoi (curTarget->first_node ("no_beacons")->value ());

        setOfOptimizationTargetsOriginatedFromThisAs.insert (
            std::make_pair (
            targetId, OptimizationTarget (targetId, optimizationCriteria, optimizationDirection, as->asNumber, groupId, noBeacons, NULL)));
        curTarget = curTarget->next_sibling ("target");
      }
  }

  void DoInitializations (uint32_t numASes, rapidxml::xml_node<> *xmlNode,
                          const YAML::Node &config) override;

  void PerLinkInitializations (rapidxml::xml_node<> *xmlNode, const YAML::Node &config) override;

private:
  BeaconsGroupedByOptimizationTargetsAndIngressIfGroup_t
      pushBasedBeaconsGroupedByOptimizationTargetsAndIngressIfGroup; // permanent until beacons expiration

  std::vector<BeaconsGroupedByOptimizationTargetsAndIngressIfGroup_t>
      pullBasedBeaconsGroupedByOptimizationTargetsAndIngressIfGroup;

  std::unordered_map<uint16_t, const OptimizationTarget>
      setOfOptimizationTargetsOriginatedFromThisAs;
  std::multimap<uint16_t, const OptimizationTarget *> ifToPushBasedOptimizationTargetsMap;

  std::unordered_map<uint16_t, uint16_t> ifToIfGroup;

  std::unordered_map<uint16_t, std::unordered_map<uint16_t, std::vector<uint16_t>>>
      interfaceGroupsConnectedPerNeighbor; // key1: neighbor AS, key2: interface_group, values in the vector: interface ids

  uint16_t firstPullBasedInterval;
  uint16_t pullBasedDisseminationToInitiationFrequency;
  uint16_t desiredMaxTolerableLinkFailures;
  std::set<std::pair<uint16_t, uint16_t>> visitedPullBasedSrcDstPair;

  std::multimap<uint16_t, const OptimizationTarget *> ifToPullBasedOptimizationTargetsMap;
  std::unordered_map<uint16_t, std::unordered_map<uint32_t, uint16_t> *> repetitionOfEdges;
  std::unordered_map<uint16_t, std::unordered_map<uint32_t, std::unordered_set<const Beacon *>> *>
      edgeToBeacon;
  std::unordered_map<uint16_t, std::unordered_map<uint16_t, std::unordered_set<uint16_t> *> *>
      setOfForbiddenEdgesPerDestinationAs;
  std::unordered_set<Beacon *> newRequestedPullBasedBeacons;

  void InitiateBeaconsPerInterface (uint16_t selfEgressIfNo, ScionAs *remoteAs,
                                       uint16_t remoteIngressIfNo) override;

  void CreateInitialStaticInfoExtension (StaticInfoExtension_t &staticInfoExtension,
                                        uint16_t selfEgressIfNo,
                                        const OptimizationTarget *optimizationTarget) override;

  void DisseminateBeacons (NeighbourRelation relation) override;

  std::tuple<bool, bool, bool, Beacon *, Ld_t>
  AlgSpecificImportPolicy (Beacon &theBeacon, uint16_t senderAs, uint16_t remoteEgressIfNo,
                              uint16_t selfIngressIfNo, uint16_t now) override;

  void InsertToAlgorithmDataStructures (Beacon *theBeacon, uint16_t senderAs,
                                            uint16_t remoteEgressIfNo,
                                            uint16_t selfIngressIfNo) override;

  void DeleteFromAlgorithmDataStructures (Beacon *theBeacon, Ld_t replacementKey) override;

  static Ld_t CalculateScore (const OptimizationTarget *optimizationTarget,
                             const StaticInfoExtension_t &staticInfoExtension);

  void SelectBeaconsToDisseminatePerTargetPerNbr (
      uint16_t remoteAsNo,
      const BeaconsWithTheSameOptTarget_t &beaconsWithTheSameOptTarget,
      const OptimizationTarget *optimizationTarget,
      std::unordered_map<uint16_t, std::multimap<Ld_t,
                                                 std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, StaticInfoExtension_t>,
                                                 std::greater<Ld_t>>> &selectedBeacons);

  void SendSelectedBeaconsPerTargetPerNbr (
      const std::unordered_map<
          uint16_t,
          std::multimap<Ld_t, std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, StaticInfoExtension_t>,
              std::greater<Ld_t>>> &selectedBeacons);

  void ExtendStaticInfoExtension (const Beacon *theBeacon, uint16_t beaconIngressIfNo,
                                     uint16_t candidateEgressIfNo,
                                  StaticInfoExtension_t &propagationStaticInfo);

  void UpdateStateBeforeBeaconing () override;

  void UpdateAlgorithmDataStructuresPeriodic (Beacon *theBeacon, bool invalidated) override;

  void DeleteFromForbiddenEdges (Beacon *theBeacon);

  void InsertToForbiddenEdges (Beacon *theBeacon);

  void CheckMaxTolerableLinkFailures ();

  uint16_t CheckMaxTolerableLinkFailuresPerDst (
      std::unordered_map<uint32_t, std::unordered_set<const Beacon *>> &perDstEdgeToBeacon,
      uint16_t maxTolerableLinkFailure);

  void CreateOptimizationTargetsForForbiddenEdges (uint16_t dstAs);

  void RemoveOptimizationTargetsForForbiddenEdges (uint16_t dstAs);
};
} // namespace ns3
#endif //SCION_SIMULATOR_ON_DEMAND_OPTIMIZATION_H
