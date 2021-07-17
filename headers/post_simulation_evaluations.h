//
// Created by seyedali on 17.07.21.
//

#ifndef NS_3_BEACONING_SIMULATOR_POST_SIMULATION_EVALUATIONS_H
#define NS_3_BEACONING_SIMULATOR_POST_SIMULATION_EVALUATIONS_H

void DoFinalEvaluations(ns3::NodeContainer& nodes, std::map<int32_t, uint16_t>& ASes, std::map<uint16_t, int32_t>& index_to_AS_no,
                        uint16_t expiration_period, ns3::Time beaconing_period, ns3::Time last_beaconing_event_time);

void PrintTrafficSentFromCollectorsPerDstPerPeriod(ns3::NodeContainer& nodes, std::map<int32_t, uint16_t>& ASes, std::map<uint16_t, int32_t>& index_to_AS_no,
                                                   uint16_t expiration_period, ns3::Time beaconing_period, ns3::Time last_beaconing_event_time);

void PrintAllDiscoveredPaths(ns3::NodeContainer& nodes, std::map<int32_t, uint16_t>& ASes, std::map<uint16_t, int32_t>& index_to_AS_no);

void PrintDistributionOfPathsWithSpecificHopCount(ns3::NodeContainer& nodes);

void PrintConsumedBWAtEachPeriod(ns3::NodeContainer& nodes, ns3::Time beaconing_period, ns3::Time last_beaconing_event_time);

void PrintPathQualities(ns3::NodeContainer& nodes);

void Evaluate_S_T_Connectivity(ns3::NodeContainer& nodes);

void PrintMinimumLatencyDist(ns3::NodeContainer& nodes);

void PrintPathNoDistribution (ns3::NodeContainer& nodes);

void FindMinLatencyToDNSRootServers(ns3::NodeContainer& nodes, std::map<int32_t, uint16_t>& ASes, std::map<uint16_t, int32_t>& index_to_AS_no);

#endif //NS_3_BEACONING_SIMULATOR_POST_SIMULATION_EVALUATIONS_H
