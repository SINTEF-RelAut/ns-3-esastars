/**
 * @file baseline.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 */

#ifndef SCION_SIMULATOR_BASELINE_H
#define SCION_SIMULATOR_BASELINE_H

#include "src/SCION/headers/beaconing/beacon_server.h"

namespace ns3 {

class Baseline : public BeaconServer
{
public:
  Baseline (ScionAs *as, bool parallelScheduler, rapidxml::xml_node<> *xmlNode,
            const YAML::Node &config)
      : BeaconServer (as, parallelScheduler, xmlNode, config)
  {
  }

  void DoInitializations (uint32_t numASes, rapidxml::xml_node<> *xmlNode,
                          const YAML::Node &config) override;

private:
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
};
} // namespace ns3
#endif //SCION_SIMULATOR_BASELINE_H
