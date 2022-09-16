/**
 * @file scionlab_algo.h
 * @authors Seyedali Tabaeiaghdaei
 * @date 2020
 */

#ifndef SCION_SIMULATOR_SCIONLAB_ALGO_H
#define SCION_SIMULATOR_SCIONLAB_ALGO_H

#include "src/SCION/headers/beaconing/beacon_server.h"

namespace ns3 {
#define MAX_SET_SIZE 100

class Scionlab : public BeaconServer
{
public:
  Scionlab (ScionAs *as, bool parallelScheduler, rapidxml::xml_node<> *xmlNode,
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

  std::pair<Beacon *, int32_t> SelectMostDiverse (std::vector<Beacon *> &beacons,
                                                    Beacon *theBeacon);

  static int32_t CalcDiversity (Beacon *beacon1, Beacon *beacon2);
};
} // namespace ns3
#endif //SCION_SIMULATOR_SCIONLAB_ALGO_H
