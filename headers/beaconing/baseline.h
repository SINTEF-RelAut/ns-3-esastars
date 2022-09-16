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
  Baseline (SCION_AS *AS, bool parallel_scheduler, rapidxml::xml_node<> *xml_node,
            const YAML::Node &config)
      : BeaconServer (AS, parallel_scheduler, xml_node, config)
  {
  }

  void DoInitializations (uint32_t num_ASes, rapidxml::xml_node<> *xml_node,
                          const YAML::Node &config) override;

private:
  void DisseminateBeacons (neighbour_relation relation) override;

  std::tuple<bool, bool, bool, Beacon *, ld>
  AlgSpecificImportPolicy (Beacon &the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no,
                              uint16_t self_ingress_if_no, uint16_t now) override;

  void InsertToAlgorithmDataStructures (Beacon *the_beacon, uint16_t sender_as,
                                            uint16_t remote_egress_if_no,
                                            uint16_t self_ingress_if_no) override;

  void DeleteFromAlgorithmDataStructures (Beacon *the_beacon, ld replacement_key) override;

  void CreateInitialStaticInfoExtension (static_info_extension_t &static_info_extension,
                                        uint16_t self_egress_if_no,
                                        const optimization_target_t *optimization_target) override;

  void UpdateAlgorithmDataStructuresPeriodic (Beacon *the_beacon, bool invalidated) override;
};
} // namespace ns3
#endif //SCION_SIMULATOR_BASELINE_H
