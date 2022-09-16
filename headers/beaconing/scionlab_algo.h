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
  Scionlab (ScionAs *AS, bool parallel_scheduler, rapidxml::xml_node<> *xml_node,
            const YAML::Node &config)
      : BeaconServer (AS, parallel_scheduler, xml_node, config)
  {
  }

  void DoInitializations (uint32_t num_ASes, rapidxml::xml_node<> *xml_node,
                          const YAML::Node &config) override;

private:
  void DisseminateBeacons (NeighbourRelation relation) override;

  std::tuple<bool, bool, bool, Beacon *, ld>
  AlgSpecificImportPolicy (Beacon &the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no,
                              uint16_t self_ingress_if_no, uint16_t now) override;

  void InsertToAlgorithmDataStructures (Beacon *the_beacon, uint16_t sender_as,
                                            uint16_t remote_egress_if_no,
                                            uint16_t self_ingress_if_no) override;

  void DeleteFromAlgorithmDataStructures (Beacon *the_beacon, ld replacement_key) override;

  void CreateInitialStaticInfoExtension (static_info_extension_t &static_info_extension,
                                        uint16_t self_egress_if_no,
                                        const OptimizationTarget *optimization_target) override;

  void UpdateAlgorithmDataStructuresPeriodic (Beacon *the_beacon, bool invalidated) override;

  std::pair<Beacon *, int32_t> SelectMostDiverse (std::vector<Beacon *> &beacons,
                                                    Beacon *the_beacon);

  static int32_t CalcDiversity (Beacon *beacon1, Beacon *beacon2);
};
} // namespace ns3
#endif //SCION_SIMULATOR_SCIONLAB_ALGO_H
