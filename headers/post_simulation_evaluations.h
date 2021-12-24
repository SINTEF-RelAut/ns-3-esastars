//
// Created by seyedali on 17.07.21.
//

#ifndef NS_3_BEACONING_SIMULATOR_POST_SIMULATION_EVALUATIONS_H
#define NS_3_BEACONING_SIMULATOR_POST_SIMULATION_EVALUATIONS_H

#include <yaml-cpp/yaml.h>

namespace ns3 {

    void DoFinalEvaluations(YAML::Node& config, ns3::NodeContainer &AS_nodes, std::map<int32_t, uint16_t> &real_to_alias_as_no,
                            std::map<uint16_t, int32_t> &alias_to_real_as_no);

    void PrintTrafficSentFromCollectorsPerDstPerPeriod(ns3::NodeContainer &AS_nodes, std::map<int32_t, uint16_t> &real_to_alias_as_no,
                                                       std::map<uint16_t, int32_t> &alias_to_real_as_no,
                                                       uint16_t expiration_period, ns3::Time beaconing_period,
                                                       ns3::Time last_beaconing_event_time);

    void PrintAllDiscoveredPaths(ns3::NodeContainer &AS_nodes, std::map<int32_t, uint16_t> &real_to_alias_as_no,
                                 std::map<uint16_t, int32_t> &alias_to_real_as_no);

    void PrintDistributionOfPathsWithSpecificHopCount(ns3::NodeContainer &AS_nodes);

    void PrintConsumedBWAtEachPeriod(ns3::NodeContainer &AS_nodes, ns3::Time beaconing_period,
                                     ns3::Time last_beaconing_event_time);

    void PrintPathQualities(ns3::NodeContainer &AS_nodes);

    void Evaluate_S_T_Connectivity(ns3::NodeContainer &AS_nodes);

    void PrintMinimumLatencyDist(ns3::NodeContainer &AS_nodes);

    void PrintPathNoDistribution(ns3::NodeContainer &AS_nodes);

    void FindMinLatencyToDNSRootServers(ns3::NodeContainer &AS_nodes, std::map<int32_t, uint16_t> &real_to_alias_as_no,
                                        std::map<uint16_t, int32_t> &alias_to_real_as_no);

    void PrintPathPollutionIndex(ns3::NodeContainer& AS_nodes, std::map<uint16_t, int32_t>& alias_to_real_as_no);

    void PrintLeastPollutingPaths(ns3::NodeContainer& AS_nodes, std::map<uint16_t, int32_t>& alias_to_real_as_no, std::string beaconing_policy_str);

    void PrintBestPerHopPollutionIndexes(ns3::NodeContainer& AS_nodes, std::map<uint16_t, int32_t>& alias_to_real_as_no);

    void PrintTransitTrafficBaseline(ns3::NodeContainer& AS_nodes, std::map<uint16_t, int32_t>& alias_to_real_as_no);
}
#endif //NS_3_BEACONING_SIMULATOR_POST_SIMULATION_EVALUATIONS_H
