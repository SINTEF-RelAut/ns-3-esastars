/**
 * @file diversity_age_based.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 */

#ifndef SCION_SIMULATOR_DIVERSITY_AGE_BASED_H
#define SCION_SIMULATOR_DIVERSITY_AGE_BASED_H

#include "src/SCION/headers/beaconing/beacon_server.h"

namespace ns3 {
#define MAX_ACCEPTABLE_JOINTNESS 2.0
#define MAX_LAT 1000.0
#define MAX_BWD 400.0
#define ALPHA 12.0
#define BETA 6.0
#define GAMMA 11.0
#define SCALING_FACTOR 0.95
#define SCORE_THRESHOLD 0.9

class DiversityAgeBased : public BeaconServer
{
public:
  DiversityAgeBased (ScionAs *as, bool parallelScheduler, rapidxml::xml_node<> *xmlNode,
                     const YAML::Node &config)
      : BeaconServer (as, parallelScheduler, xmlNode, config)
  {
  }

  void DoInitializations (uint32_t numASes, rapidxml::xml_node<> *xmlNode,
                          const YAML::Node &config) override;

private:
  std::vector<std::unordered_map<Beacon *, std::pair<float, uint16_t>> *> sentBeacons;
  std::vector<std::unordered_map<uint16_t, uint16_t> *> sentBeaconsCnt;

  std::unordered_map<uint16_t, std::vector<std::unordered_map<uint32_t, uint32_t> *>>
      linksJointnessesOnSentPaths;
  std::vector<std::unordered_map<uint32_t, uint32_t> *> linksJointnessesOnReceivedPaths;

  void DisseminateBeacons (NeighbourRelation relation) override;

  std::tuple<bool, bool, bool, Beacon *, Ld_t>
  AlgSpecificImportPolicy (Beacon &theBeacon, uint16_t senderAs, uint16_t remoteEgressIfNo,
                              uint16_t selfIngressIfNo, uint16_t now) override;

  void InsertToAlgorithmDataStructures (Beacon *theBeacon, uint16_t senderAs,
                                            uint16_t remoteEgressIfNo,
                                            uint16_t selfIngressIfNo) override;

  void DeleteFromAlgorithmDataStructures (Beacon *theBeacon, Ld_t replacementKey) override;

  void CreateInitialStaticInfoExtension (StaticInfoExtension_t &staticInfoExtension,
                                        uint16_t selfEgressIfNo,
                                        const OptimizationTarget *optimizationTarget) override;

  void UpdateAlgorithmDataStructuresPeriodic (Beacon *theBeacon, bool invalidated) override;

  std::multimap<Ld_t, std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, StaticInfoExtension_t>>
  SelectBeaconsToDisseminatePerDstPerNbr (
      uint16_t remoteAsNo, uint16_t dstAsNo,
      const BeaconsWithSameDstAs_t &beaconsToTheDstAs);

  void UpdateSentBeaconTimer (uint16_t remoteAs, uint16_t selfEgressIfNo,
                                 Beacon *theBeacon);

  void IncLinksJointnessOnSentPaths (uint16_t dstAsNo, uint16_t remoteAsNo,
                                          uint16_t selfEgressIfNo, Beacon *theBeacon);

  void IncLinksJointnessOnReceivedPaths (uint16_t dstAsNo, Beacon *theBeacon);

  void AddToSentBeacons (uint16_t dstAsNo, uint16_t remoteAs, uint16_t selfEgressIfNo,
                            Beacon *theBeacon, float rawScore);

  Ld_t CalculateLinkDiversityScoreForDissemination (uint16_t remoteAs, uint16_t dstAs,
                                                       uint16_t egressIfNo, Beacon *theBeacon);

  Ld_t CalculateLinkDiversityScoreForImport (uint16_t dstAs, Beacon &theBeacon);

  bool PathNotSentBefore (uint16_t remoteAs, uint16_t selfEgressIfNo, Beacon *theBeacon);

  void RemoveInvalidSentBeacons (Beacon *theBeacon, uint16_t dstAs);

  void DecLinksJointnessesOnSentPaths (Beacon *theBeacon, uint16_t dstAs,
                                            uint16_t remoteAsNo, uint16_t selfEgressIf);

  void DecLinksJointnessesOnReceivedPaths (Beacon *theBeacon, uint16_t dstAs);

  inline Ld_t CalculateRawScore (Beacon *theBeacon, uint16_t dstAsNo, uint16_t selfEgressIfNo,
                                 ScionAs *remoteAs);

  inline Ld_t CalculateImportRawScore (Beacon &theBeacon);
};
} // namespace ns3
#endif //SCION_SIMULATOR_DIVERSITY_AGE_BASED_H
