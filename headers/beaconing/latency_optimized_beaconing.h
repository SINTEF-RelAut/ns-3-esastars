/**
 * @file latency_optimized_beaconing.h
 * @authors Seyedali Tabaeiaghdaei
 * @date 2021
 */

#ifndef SCION_SIMULATOR_LATENCY_OPTIMIZED_BEACONING_H
#define SCION_SIMULATOR_LATENCY_OPTIMIZED_BEACONING_H

#include "src/SCION/headers/beaconing/beacon_server.h"

namespace ns3 {

#define MAX_BEACONS_TO_SEND_PER_IFACE 1
#define MAX_BEACONS_TO_STORE_PER_IFACE 1

class LatencyOptimized : public BeaconServer
{
public:
  LatencyOptimized (ScionAs *as, bool parallelScheduler, rapidxml::xml_node<> *xmlNode,
                    const YAML::Node &config)
      : BeaconServer (as, parallelScheduler, xmlNode, config)
  {
  }

  void DoInitializations (uint32_t numASes, rapidxml::xml_node<> *xmlNode,
                          const YAML::Node &config) override;

private:
  std::vector<std::vector<std::multimap<Ld_t, Beacon *>>> beaconsPerDstPerIngIfSortedByLatency;

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
};
} // namespace ns3
#endif //SCION_SIMULATOR_LATENCY_OPTIMIZED_BEACONING_H
