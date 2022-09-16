/**
 * @file diversity_age_based.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 */

#ifndef SCION_SIMULATOR_CRITERIA_MATCHING_H
#define SCION_SIMULATOR_CRITERIA_MATCHING_H

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
  DiversityAgeBased (SCION_AS *AS, bool parallel_scheduler, rapidxml::xml_node<> *xml_node,
                     const YAML::Node &config)
      : BeaconServer (AS, parallel_scheduler, xml_node, config)
  {
  }

  void DoInitializations (uint32_t num_ASes, rapidxml::xml_node<> *xml_node,
                          const YAML::Node &config) override;

private:
  std::vector<std::unordered_map<Beacon *, std::pair<float, uint16_t>> *> sent_beacons;
  std::vector<std::unordered_map<uint16_t, uint16_t> *> sent_beacons_cnt;

  std::unordered_map<uint16_t, std::vector<std::unordered_map<uint32_t, uint32_t> *>>
      links_jointnesses_on_sent_paths;
  std::vector<std::unordered_map<uint32_t, uint32_t> *> links_jointnesses_on_received_paths;

  void disseminate_beacons (neighbour_relation relation) override;

  std::tuple<bool, bool, bool, Beacon *, ld>
  alg_specific_import_policy (Beacon &the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no,
                              uint16_t self_ingress_if_no, uint16_t now) override;

  void insert_to_algorithm_data_structures (Beacon *the_beacon, uint16_t sender_as,
                                            uint16_t remote_egress_if_no,
                                            uint16_t self_ingress_if_no) override;

  void delete_from_algorithm_data_structures (Beacon *the_beacon, ld replacement_key) override;

  void
  create_initial_static_info_extension (static_info_extension_t &static_info_extension,
                                        uint16_t self_egress_if_no,
                                        const optimization_target_t *optimization_target) override;

  void update_algorithm_data_structures_periodic (Beacon *the_beacon, bool invalidated) override;

  std::multimap<ld, std::tuple<Beacon *, uint16_t, uint16_t, SCION_AS *, static_info_extension_t>>
  select_beacons_to_disseminate_per_dst_per_nbr (
      uint16_t remote_as_no, uint16_t dst_as_no,
      const beacons_with_same_dst_as &beacons_to_the_dst_as);

  void update_sent_beacon_timer (uint16_t remote_as, uint16_t self_egress_if_no,
                                 Beacon *the_beacon);

  void inc_links_jointness_on_sent_paths (uint16_t dst_as_no, uint16_t remote_as_no,
                                          uint16_t self_egress_if_no, Beacon *the_beacon);

  void inc_links_jointness_on_received_paths (uint16_t dst_as_no, Beacon *the_beacon);

  void add_to_sent_beacons (uint16_t dst_as_no, uint16_t remote_as, uint16_t self_egress_if_no,
                            Beacon *the_beacon, float raw_score);

  ld calculate_link_diversity_score_for_dissemination (uint16_t remote_as, uint16_t dst_as,
                                                       uint16_t egress_if_no, Beacon *the_beacon);

  ld calculate_link_diversity_score_for_import (uint16_t dst_as, Beacon &the_beacon);

  bool path_not_sent_before (uint16_t remote_as, uint16_t self_egress_if_no, Beacon *the_beacon);

  void remove_invalid_sent_beacons (Beacon *the_beacon, uint16_t dst_as);

  void dec_links_jointnesses_on_sent_paths (Beacon *the_beacon, uint16_t dst_as,
                                            uint16_t remote_as_no, uint16_t self_egress_if);

  void dec_links_jointnesses_on_received_paths (Beacon *the_beacon, uint16_t dst_as);

  inline ld calculate_raw_score (Beacon *the_beacon, uint16_t dst_as_no, uint16_t self_egress_if_no,
                                 SCION_AS *remote_as);

  inline ld calculate_import_raw_score (Beacon &the_beacon);
};
} // namespace ns3
#endif //SCION_SIMULATOR_CRITERIA_MATCHING_H
