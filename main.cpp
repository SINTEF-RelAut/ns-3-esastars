/**
 * @file baseline_sim.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 *
 * This script takes the beaconing_period, expiration_period, simulator_time and the topology
 * to use as command line arguments. It will automatically instantiate nodes as Core or Leaf ASes
 * depending on the "type" property given in the xml file. This, together with the "rel" property on the links,
 * is used to infer over which interfaces the node needs to propagate beacons. All the nodes will instantiate
 * the baseline beaconing strategy when using this script. For criteria matching use the other script.
 * @see criteria_matching_sim
 */

#include "headers/utils.h"
#include "headers/beaconing_strategy.h"
#include "headers/baseline.h"
#include "headers/scion_node.h"
#include "headers/scion_core_as.h"
#include "headers/scion_as.h"
#include "ns3/ptr.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-channel.h"
#include <ns3/nstime.h>
#include <istream>
#include <omp.h>
#include <src/SCION/headers/criteria_matching.h>
#include <random>


//rapidxml::xml_node<>* SetupTopologyFile (std::string topology_name);

void InstantiateASesFromTopo(rapidxml::xml_node<>* rootNode, std::string beaconing_policy_str, std::map<int32_t, uint16_t>& ASes, std::map<uint16_t,
                             int32_t>& index_to_AS_no, ns3::NodeContainer& nodes, uint16_t expiration_period, ns3::Time beaconing_period);

void InstantiateLinksFromTopo (rapidxml::xml_node<>* rootNode, ns3::NodeContainer& nodes, std::map<int32_t, uint16_t>& ASes);

void InitializeNodesAttributes(ns3::NodeContainer& nodes, std::string beaconing_policy_str);

void ScheduleBeaconingEvents(ns3::NodeContainer& nodes, ns3::Time beaconing_period, ns3::Time last_beaconing_event_time);

/**
 * @brief Called to process the beacons received in this beaconing period.
 *
 * Finalizes the beaconing period by calling UpdateBeaconStoreAndCountersBeforeBeaconing on each node.
 * Parallelized by distributing all the nodes on a few threads.
 *
 * @param nodes The ns3::NodeContainer hons3::lding all the nodes of this simulation.
 *
 * @see UpdateBeaconStoreAndCountersBeforeBeaconing
 */
void ActionsBetweenBeaconingIntervals(ns3::NodeContainer nodes);

void DoFinalEvaluations(ns3::NodeContainer& nodes, std::map<int32_t, uint16_t>& ASes, std::map<uint16_t, int32_t>& index_to_AS_no,
                        uint16_t expiration_period, ns3::Time beaconing_period, ns3::Time last_beaconing_event_time);

void PrintTrafficSentFromCollectorsPerDstPerPeriod(ns3::NodeContainer& nodes, std::map<int32_t, uint16_t>& ASes, std::map<uint16_t, int32_t>& index_to_AS_no,
uint16_t expiration_period, ns3::Time beaconing_period, ns3::Time last_beaconing_event_time);

void PrintAllDiscoveredPaths(ns3::NodeContainer& nodes, std::map<int32_t, uint16_t>& ASes, std::map<uint16_t, int32_t>& index_to_AS_no);

void PrintDistributionOfPathsWithSpecificHopCount(ns3::NodeContainer& nodes);

void PrintConsumedBWAtEachPeriod(ns3::NodeContainer& nodes, ns3::Time beaconing_period, ns3::Time last_beaconing_event_time);

void PrintPathQualities(ns3::NodeContainer& nodes);

void Evaluate_S_T_Connectivity(ns3::NodeContainer& nodes);

int main(int argc, char *argv[]) {
    ns3::NodeContainer nodes;
    std::map<int32_t, uint16_t> ASes;
    std::map<uint16_t, int32_t> index_to_AS_no;

    std::string beaconing_policy_str;
    std::string beaconing_period_str;
    std::string expiration_period_str;
    std::string last_beaconing_event_time_str;
    std::string topology_name;

    ns3::Time beaconing_period;
    ns3::Time last_beaconing_event_time;
    uint16_t expiration_period;

    ns3::Time simulation_end_time;

    if (argc == 6) {
        beaconing_policy_str = std::string (argv[1]);
        beaconing_period_str = std::string(argv[2]);
        expiration_period_str = std::string(argv[3]);
        last_beaconing_event_time_str = std::string(argv[4]);
        topology_name = std::string(argv[5]);
    } else {
        std::cerr << "Less arguments than expected!" << std::endl;
        return 1;
    }

    beaconing_period = ns3::Time(beaconing_period_str);
    last_beaconing_event_time = ns3::Time(last_beaconing_event_time_str);
    expiration_period = (uint16_t) ns3::Time(expiration_period_str).ToInteger(ns3::Time::MIN);

    // simulation_end_time can be something other than last_beaconing_event_time if we want to simulate other stuff as well
    simulation_end_time = last_beaconing_event_time;

    //rapidxml::xml_node<>* rootNode = SetupTopologyFile (topology_name);

    std::string file = "~/ns-3_beaconing_simulator/topology/" + std::string(topology_name) + ".xml";
    std::ifstream fin(file.c_str());
    std::ostringstream sstr;
    sstr << fin.rdbuf();

    sstr.flush();
    fin.close();

    std::string xmlData = sstr.str();
    rapidxml::xml_document<> doc;
    doc.parse<0>(&xmlData[0]);

    rapidxml::xml_node<> *rootNode = doc.first_node("topology");

    if (!rootNode) {
        std::cerr << "Empty topology!" << std::endl;
        exit(1);
    }

    InstantiateASesFromTopo(rootNode, beaconing_policy_str, ASes, index_to_AS_no, nodes, expiration_period, beaconing_period);
    InstantiateLinksFromTopo(rootNode, nodes, ASes);

    InitializeNodesAttributes(nodes, beaconing_policy_str);

    // TODO: Think about how to automatically set an appropriate name, maybe in conjunction with simulator configs?
    std::string out_path =
            "/cluster/scratch/tabaeias/" + beaconing_policy_str + "_" + topology_name + "_" +
            beaconing_period_str + "_" + expiration_period_str + "_" + last_beaconing_event_time_str + ".txt";
    std::ofstream out(out_path);
    std::cout.rdbuf(out.rdbuf());

    ScheduleBeaconingEvents(nodes, beaconing_period, last_beaconing_event_time);
    ns3::Simulator::Stop(simulation_end_time);
    ns3::Simulator::Run();

    DoFinalEvaluations(nodes, ASes, index_to_AS_no, expiration_period, beaconing_period, last_beaconing_event_time);

    ns3::Simulator::Destroy();

    return 0;
}

//rapidxml::xml_node<>* SetupTopologyFile (std::string topology_name) {
//    std::string file = "/home/tabaeias/ns-3_beaconing_simulator/topology/" + std::string(topology_name) + ".xml";
//    std::ifstream* fin = new std::ifstream(file.c_str());
//    std::ostringstream* sstr = new std::ostringstream();
//    *sstr << fin->rdbuf();
//
//    sstr->flush();
//    fin->close();
//
//    std::string xmlData = sstr->str();
//    rapidxml::xml_document<>* doc = new rapidxml::xml_document<>();
//    doc->parse<0>(&xmlData[0]);
//
//    rapidxml::xml_node<> *rootNode = doc->first_node("topology");
//
//    if (!rootNode) {
//        std::cerr << "Empty topology!" << std::endl;
//        exit(1);
//    }
//
//    return rootNode;
//}

void InstantiateASesFromTopo(rapidxml::xml_node<>* rootNode, std::string beaconing_policy_str, std::map<int32_t, uint16_t>& ASes, std::map<uint16_t,
        int32_t>& index_to_AS_no, ns3::NodeContainer& nodes, uint16_t expiration_period, ns3::Time beaconing_period) {
    int16_t node_counter = 0;

    rapidxml::xml_node<>* curNode = rootNode->first_node("node");
    while (curNode) {
        int32_t as_number = std::stoi(ns3::getAttribute(curNode, "id"));
        ns3::PropertyContainer p = ns3::parseProperties(curNode);

        ns3::ld latency_coef = 0.0; //std::stod(p.getProperty("latency_coef"));
        ns3::ld bandwidth_coef = 0.0; //std::stod(p.getProperty("bandwidth_coef"));
        ns3::ld AS_level_diversity_coef = 0.0; // std::stod(p.getProperty("AS_level_diversity_coef"));
        ns3::ld link_level_diversity_coef = 1.0; //std::stod(p.getProperty("link_level_diversity_coef"));
        std::string type = "core"; //p.getProperty("type");

        ns3::beaconing_timing_params params = std::make_pair(beaconing_period, expiration_period);
        ns3::coefficients coefs = std::make_tuple(latency_coef, bandwidth_coef, AS_level_diversity_coef,
                                                  link_level_diversity_coef);

        ns3::BeaconingStrategy* beaconing_policy;
        if (beaconing_policy_str == "baseline") {
            beaconing_policy = (ns3::BeaconingStrategy*) new ns3::Baseline();
        } else if (beaconing_policy_str == "criteria_matching") {
            beaconing_policy = (ns3::BeaconingStrategy*) new ns3::CriteriaMatching();
        } else {
            beaconing_policy = (ns3::BeaconingStrategy*) new ns3::Baseline();
        }

        ns3::Ptr<ns3::SCION_Node> node;
        if(type == "core"){
            node = ns3::CreateObject<ns3::SCION_Core_As>(node_counter, 0, coefs, params, beaconing_policy);
        } else if(type =="non-core"){
            node = ns3::CreateObject<ns3::SCION_As>(node_counter, 0, coefs, params, beaconing_policy);
        } else{
            std::cerr << "Incompatible node type!" << std::endl;
            exit(1);
        }
        nodes.Add(node);
        beaconing_policy->SetNode(node);

        ASes.insert(std::make_pair(as_number, node_counter));
        index_to_AS_no.insert(std::make_pair(node_counter, as_number));

        node_counter++;

        curNode = curNode->next_sibling("node");
    }
}

void InstantiateLinksFromTopo (rapidxml::xml_node<>* rootNode, ns3::NodeContainer& nodes, std::map<int32_t, uint16_t>& ASes){
    rapidxml::xml_node<> *curNode = rootNode->first_node("link");
    while (curNode) {
        int32_t to = std::stoi(curNode->first_node("to")->value());
        int32_t from = std::stoi(curNode->first_node("from")->value());

        ns3::PropertyContainer p = ns3::parseProperties(curNode);

        ns3::ld latitude = std::stod(p.getProperty("latitude"));
        ns3::ld longitude = std::stod(p.getProperty("longitude"));
        int32_t bwd = std::stoi(p.getProperty("capacity"));
        std::string rel = "core"; //p.getProperty("rel");
        ns3::SCION_Node::neighbour_relation relation;

// Check for the 3 possibilities in CAIDA topology
        if (rel == "peer") {
            relation = ns3::SCION_Node::neighbour_relation::PEER;
        } else if (rel == "core") {
            relation = ns3::SCION_Node::neighbour_relation::CORE;
        } else if (rel == "customer") {
            relation = ns3::SCION_Node::neighbour_relation::CUSTOMER;
        } else {
            relation = ns3::SCION_Node::neighbour_relation::CORE;
        }

        ns3::Ptr<ns3::SCION_Node> fromNode;
        ns3::Ptr<ns3::SCION_Node> toNode;

        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            if ((ns3::DynamicCast<ns3::SCION_Node>(nodes.Get(i)))->as_number == ASes.at(to)) {
                toNode = ns3::DynamicCast<ns3::SCION_Node>(nodes.Get(i));
                break;
            }
        }

        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            if ((ns3::DynamicCast<ns3::SCION_Node>(nodes.Get(i)))->as_number == ASes.at(from)) {
                fromNode = ns3::DynamicCast<ns3::SCION_Node>(nodes.Get(i));
                break;
            }
        }

        ns3::PointToPointHelper helper;
        helper.Install(fromNode, toNode);

        ns3::Ptr<ns3::SCION_Node> to_my_node = (ns3::DynamicCast<ns3::SCION_Node>(toNode));
        ns3::Ptr<ns3::SCION_Node> from_my_node = (ns3::DynamicCast<ns3::SCION_Node>(fromNode));

        to_my_node->interfaces_coordinates.push_back(std::pair<ns3::ld, ns3::ld>(latitude, longitude));
        from_my_node->interfaces_coordinates.push_back(std::pair<ns3::ld, ns3::ld>(latitude, longitude));

        to_my_node->inter_as_bwds.push_back(bwd);
        from_my_node->inter_as_bwds.push_back(bwd);

        ns3::SCION_Node::neighbour_relation to_rel;
        ns3::SCION_Node::neighbour_relation from_rel;

        switch (relation) {
            case ns3::SCION_Node::neighbour_relation::PEER:
                to_rel = ns3::SCION_Node::neighbour_relation::PEER;
                from_rel = ns3::SCION_Node::neighbour_relation::PEER;
                break;
            case ns3::SCION_Node::neighbour_relation::CORE:
                to_rel = ns3::SCION_Node::neighbour_relation::CORE;
                from_rel = ns3::SCION_Node::neighbour_relation::CORE;
                break;
            case ns3::SCION_Node::neighbour_relation::CUSTOMER:
                to_rel = ns3::SCION_Node::neighbour_relation::PROVIDER;
                from_rel = ns3::SCION_Node::neighbour_relation::CUSTOMER;
                break;
            case ns3::SCION_Node::neighbour_relation::PROVIDER:
// Shouns3::ld never happen, there is no "Provider" type in xml files
                to_rel = ns3::SCION_Node::neighbour_relation::CUSTOMER;
                from_rel = ns3::SCION_Node::neighbour_relation::PROVIDER;
                assert(false);
        }

        to_my_node->interface_to_neighbor_map.insert(std::make_pair(to_my_node->GetNDevices() - 1, from_my_node->as_number));
        if (to_my_node->interfaces_per_neighbor_as.find(from_my_node->as_number) !=
            to_my_node->interfaces_per_neighbor_as.end()) {
            to_my_node->interfaces_per_neighbor_as.at(from_my_node->as_number).push_back(
                    (uint16_t) to_my_node->GetNDevices() - 1);
        } else {
            std::vector<uint16_t> tmp;
            tmp.push_back((uint16_t) to_my_node->GetNDevices() - 1);
            to_my_node->interfaces_per_neighbor_as.insert(std::make_pair(from_my_node->as_number, tmp));
            to_my_node->neighbors.push_back(std::make_pair(from_my_node->as_number, to_rel));
        }

        from_my_node->interface_to_neighbor_map.insert(std::make_pair(from_my_node->GetNDevices() - 1, to_my_node->as_number));
        if (from_my_node->interfaces_per_neighbor_as.find(to_my_node->as_number) !=
            from_my_node->interfaces_per_neighbor_as.end()) {
            from_my_node->interfaces_per_neighbor_as.at(to_my_node->as_number).push_back(
                    from_my_node->GetNDevices() - 1);
        } else {
            std::vector<uint16_t> tmp;
            tmp.push_back((uint16_t) from_my_node->GetNDevices() - 1);
            from_my_node->interfaces_per_neighbor_as.insert(std::make_pair(to_my_node->as_number, tmp));
            from_my_node->neighbors.push_back(std::make_pair(to_my_node->as_number, from_rel));
        }

        curNode = curNode->next_sibling("link");
    }
}

void InitializeNodesAttributes(ns3::NodeContainer& nodes, std::string beaconing_policy_str) {
    for (uint64_t i = 0; i < nodes.GetN(); ++i) {
        if (beaconing_policy_str == "baseline") {
            ns3::DynamicCast<ns3::SCION_Node>(nodes.Get(i))->DoInitializations();
        } else if (beaconing_policy_str == "criteria_matching") {
            ns3::DynamicCast<ns3::SCION_Node>(nodes.Get(i))->DoInitializations(nodes.GetN());
        }
    }
}

void ScheduleBeaconingEvents(ns3::NodeContainer& nodes, ns3::Time beaconing_period, ns3::Time last_beaconing_event_time) {
    for (ns3::Time t = ns3::Seconds(0.0); t < last_beaconing_event_time; t += beaconing_period) {
        ns3::Simulator::Schedule(t + ns3::Seconds(1.0), &ActionsBetweenBeaconingIntervals, nodes);

        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            ns3::Ptr<ns3::SCION_Node> the_node = ns3::DynamicCast<ns3::SCION_Node>(nodes.Get(i));
            ns3::Simulator::Schedule(t, &ns3::SCION_Node::CoreBeaconing, the_node);
            //ns3::Simulator::Schedule(t, &ns3::SCION_Node::IntraISDBeaconing, the_node);
        }
    }
}

void ActionsBetweenBeaconingIntervals(ns3::NodeContainer nodes) {
    std::cout << "################################## " << ns3::DynamicCast<ns3::SCION_Node>(nodes.Get(0))->now << " #########################################" << std::endl;
    uint32_t node_number = nodes.GetN();
#pragma omp parallel for
    for (uint32_t i = 0; i < node_number; ++i) {
        ns3::Ptr<ns3::SCION_Node> node = ns3::DynamicCast<ns3::SCION_Node>(nodes.Get(i));
        node->strategy->UpdateStatePeriodic();
    }

    // print number of connected pairs after each beaconing round
    uint32_t all_connected_pairs = 0;
    for (uint32_t i = 0; i < node_number; ++i) {
        all_connected_pairs += ns3::DynamicCast<ns3::SCION_Node>(nodes.Get(i))->valid_beacons_count_per_dst_as.size();
    }
    std::cout << all_connected_pairs << std::endl;
}

void DoFinalEvaluations(ns3::NodeContainer& nodes, std::map<int32_t, uint16_t>& ASes, std::map<uint16_t, int32_t>& index_to_AS_no,
                        uint16_t expiration_period, ns3::Time beaconing_period, ns3::Time last_beaconing_event_time) {

    PrintTrafficSentFromCollectorsPerDstPerPeriod(nodes, ASes, index_to_AS_no, expiration_period,  beaconing_period,  last_beaconing_event_time);
    Evaluate_S_T_Connectivity(nodes);
//    PrintAllDiscoveredPaths(nodes, ASes, index_to_AS_no);

//    PrintConsumedBWAtEachPeriod(nodes, beaconing_period, last_beaconing_event_time);
//    PrintDistributionOfPathsWithSpecificHopCount(nodes);

//    PrintPathQualities(nodes);




}

void PrintTrafficSentFromCollectorsPerDstPerPeriod(ns3::NodeContainer& nodes, std::map<int32_t, uint16_t>& ASes, std::map<uint16_t, int32_t>& index_to_AS_no,
uint16_t expiration_period, ns3::Time beaconing_period, ns3::Time last_beaconing_event_time) {
    std::cout << "####################################### Traffic sent from each collector #######################################" << std::endl;
    std::list<int32_t> collectors({3303, 3130, 1239, 701, 5413, 34224, 7018, 53767, 3741, 31019, 22652, 2497, 57866, 37100,
                                   3130, 3257, 3549, 6939, 18106, 1299, 23673, 2914, 11537, 2152, 852, 8492, 34224, 11686});
    for (int32_t collector : collectors) {
        double_t consumed_bwd = 0.0;
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            ns3::Ptr<ns3::SCION_Node> the_node = ns3::DynamicCast<ns3::SCION_Node>(nodes.Get(i));
            if (index_to_AS_no.at(the_node->as_number) == collector) {
                double_t periods = 0.0;
                for (ns3::Time t = ns3::Time(0); t < last_beaconing_event_time; t += beaconing_period) {
                    periods += 1.0;
                    for (uint32_t if_index = 0; if_index < the_node->GetNDevices(); ++if_index) {
                        consumed_bwd += (double_t) the_node->bytes_sent_per_interface_per_period.at(
                                (uint16_t) t.ToInteger(ns3::Time::MIN)).at(if_index);
                    }
                }
                consumed_bwd = (double_t) consumed_bwd /
                               the_node->GetNDevices() /
                               periods;
                break;
            }
        }
        std::cout << collector << "\t" << consumed_bwd << std::endl;
    }
}

void PrintAllDiscoveredPaths(ns3::NodeContainer& nodes, std::map<int32_t, uint16_t>& ASes, std::map<uint16_t, int32_t>& index_to_AS_no) {
    std::cout << "################################################ Paths Information ##############################################################" << std::endl;

    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        ns3::Ptr<ns3::SCION_Node> the_node = ns3::DynamicCast<ns3::SCION_Node>(nodes.Get(i));

        std::cout << "From: " << index_to_AS_no.at(the_node->as_number) << std::endl;

        for (auto const & dst_as_beacons_pair : the_node->beacon_store) {
            uint16_t dst_as = dst_as_beacons_pair.first;
            const ns3::beacons_with_same_dst_as& same_dst_as_beacons = dst_as_beacons_pair.second;

            std::cout << "\t" << "To: " << index_to_AS_no.at(dst_as) << std::endl;

            for (auto const & beacons_from_same_nbr : same_dst_as_beacons) {
                for (auto const & the_beacon : beacons_from_same_nbr.second) {
                    if (!the_beacon->is_valid) {
                        continue;
                    }
                    std::cout << "\t" << "\t";
                    int hop_cnt = 0;
                    std::vector<ns3::link_information>::reverse_iterator hop = the_beacon->the_path.rbegin();
                    for (; hop!= the_beacon->the_path.rend(); ++hop) {
                        if (hop_cnt != 0) {
                            std::cout << ", ";
                        }
                        std::cout << index_to_AS_no.at(SECOND_LOWER_16_BITS(*hop)) << ":" << LOWER_16_BITS(*hop) << ", " << index_to_AS_no.at(UPPER_16_BITS(*hop)) << ":" << SECOND_UPPER_16_BITS(*hop);
                        hop_cnt++;
                    }
                    std::cout << "; ";
                    std::cout << "latency = " << the_beacon->latency_stat;
                    std::cout << "; ";
                    std::cout << "BWD = " << the_beacon->bwd_stat;
                    std::cout << std::endl;
                }

            }
        }

    }
}

void PrintConsumedBWAtEachPeriod(ns3::NodeContainer& nodes, ns3::Time beaconing_period, ns3::Time last_beaconing_event_time) {
    for (ns3::Time t = ns3::Seconds(0.0); t < last_beaconing_event_time; t += beaconing_period) {
        std::cout << "####################################### frequencies of consumed bandwidth at Time "
                  << t
                  << " #######################################" << std::endl;

        std::map<uint32_t, uint32_t> frequencies_of_consumed_bwd;
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            ns3::Ptr<ns3::SCION_Node> the_node = ns3::DynamicCast<ns3::SCION_Node>(nodes.Get(i));
            for (uint32_t if_index = 0; if_index < the_node->GetNDevices(); ++if_index) {
                uint32_t consumed_bwd = the_node->bytes_sent_per_interface_per_period.at(t.ToInteger(ns3::Time::NS)).at(if_index);

                if (frequencies_of_consumed_bwd.find(consumed_bwd) != frequencies_of_consumed_bwd.end()) {
                    frequencies_of_consumed_bwd.at(consumed_bwd)++;
                } else {
                    frequencies_of_consumed_bwd.insert(std::make_pair(consumed_bwd, 1));
                }
            }
        }

        std::cout << "consumed bandwidth on a link" << "\t" << "frequency" << std::endl;
        for (auto const & bwd_freq_pair : frequencies_of_consumed_bwd) {
            std::cout << bwd_freq_pair.first << "\t" << bwd_freq_pair.second << std::endl;
        }
    }
}

void PrintDistributionOfPathsWithSpecificHopCount(ns3::NodeContainer& nodes) {
    for (uint32_t path_length = 1; path_length <= 4; ++path_length) {
        std::cout
                << "######################################### frequencies of path counts per destination AS with hop count: "
                << path_length
                << "#########################################"
                << std::endl;
        std::map<uint64_t, uint64_t> frequencies_of_path_counts_per_dst_as_with_certain_length;
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            for (auto const &dst_as_beacons_pair : ns3::DynamicCast<ns3::SCION_Node>(nodes.Get(i))->beacon_store) {
                uint64_t number_of_paths_with_certain_length = 0;
                if(dst_as_beacons_pair.second.find(path_length) == dst_as_beacons_pair.second.end()){
                    continue;
                } else {
                    number_of_paths_with_certain_length = dst_as_beacons_pair.second.at(path_length).size();
                }

                if (frequencies_of_path_counts_per_dst_as_with_certain_length.find(
                        number_of_paths_with_certain_length) != frequencies_of_path_counts_per_dst_as_with_certain_length.end()) {
                    frequencies_of_path_counts_per_dst_as_with_certain_length.at(number_of_paths_with_certain_length)++;
                } else {
                    frequencies_of_path_counts_per_dst_as_with_certain_length.insert(
                            std::make_pair(number_of_paths_with_certain_length, 1));
                }
            }
        }

        std::cout << "path count per source AS" << "\t" << "frequency" << std::endl;
        for (auto const &count_freq_pair : frequencies_of_path_counts_per_dst_as_with_certain_length) {
            std::cout << count_freq_pair.first << "\t" << count_freq_pair.second << std::endl;
        }
    }
}

void PrintPathQualities(ns3::NodeContainer& nodes) {
    std::cout << "###################################################### PATH QUALITY #############################################################"
              << std::endl;
    std::cout << "##                                                                                                                             ##"
              << std::endl;

    std::map <ns3::ld, uint64_t> satisfaction_stat;
    std::map <ns3::ld, uint64_t> AS_level_diversity_stat;
    std::map <ns3::ld, uint64_t> link_level_diversity_stat;

    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        ns3::DynamicCast<ns3::SCION_Node>(nodes.Get(i))->FinalPathEvaluation(satisfaction_stat,
                                                                             AS_level_diversity_stat,
                                                                             link_level_diversity_stat);
    }

    std::cout << "###################################################### SATISFACTION #############################################################"
              << std::endl;
    std::cout << "satisfaction score" << "\t" << "frequency" << std::endl;

    for (auto const &satisfaction_pair : satisfaction_stat) {
        std::cout << satisfaction_pair.first << "\t" << satisfaction_pair.second << std::endl;
    }

    std::cout << "###################################################### LINK DIVERSITY ###########################################################"
              << std::endl;
    std::cout << "link diversity score" << "\t" << "frequency" << std::endl;

    for (auto const &diversity_pair : link_level_diversity_stat) {
        std::cout << diversity_pair.first << "\t" << diversity_pair.second << std::endl;
    }

    std::cout << "###################################################### AS DIVERSITY #############################################################"
              << std::endl;
    std::cout << "AS diversity score" << "\t" << "frequency" << std::endl;

    for (auto const &diversity_pair : AS_level_diversity_stat) {
        std::cout << diversity_pair.first << "\t" << diversity_pair.second << std::endl;
    }
}

void Evaluate_S_T_Connectivity(ns3::NodeContainer& nodes) {
    uint32_t NUMBER_OF_NODES = 50;

    uint32_t MAX_FAILURE_RATE = 100;

    uint32_t NUMBER_OF_TIME_SLICES = 100;

    std::vector<uint64_t > links;
    std::vector<uint64_t > links_reverse;

    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        ns3::Ptr<ns3::SCION_Node> node = ns3::DynamicCast<ns3::SCION_Node>(nodes.Get(i));
        for (uint16_t if_index = 0; if_index < node->GetNDevices(); ++if_index) {
            auto const & remote_info = node->GetRemoteAsInfo(if_index);

            uint64_t link = (((uint64_t) node->as_number) << 48) | (((uint64_t) if_index) << 32) |
                            (((uint64_t) remote_info.second->as_number) << 16) | ((uint64_t) remote_info.first);

            uint64_t link_reverse = (((uint64_t) remote_info.second->as_number) << 48) | (((uint64_t) remote_info.first) << 32) |
                                    (((uint64_t) node->as_number) << 16) | ((uint64_t) if_index);

            if (std::find(links_reverse.begin(), links_reverse.end(), link) == links_reverse.end()){
                links.push_back(link);
                links_reverse.push_back(link_reverse);
            }
        }
    }

    std::vector<uint32_t> st_nodes;
    std::random_device rd;
    std::uniform_int_distribution<uint32_t> distribution(0, nodes.GetN());
    while (st_nodes.size() < NUMBER_OF_NODES) {
        uint32_t node = distribution(rd);
        if (std::find(st_nodes.begin(), st_nodes.end(), node) == st_nodes.end()) {
            st_nodes.push_back(node);
        }
    }

    std::vector<std::unordered_map<uint32_t, uint32_t > > MMP_connectivity(MAX_FAILURE_RATE, std::unordered_map<uint32_t, uint32_t>());
    std::vector<std::unordered_map<uint32_t, uint32_t> > FMP_connectivity(MAX_FAILURE_RATE, std::unordered_map<uint32_t, uint32_t>());
    std::vector<std::unordered_map<uint32_t, uint32_t> > SP_connectivity(MAX_FAILURE_RATE, std::unordered_map<uint32_t, uint32_t>());

    for (uint32_t i = 0; i < st_nodes.size(); ++i) {
        uint32_t s_node = st_nodes.at(i);
        for (uint32_t j = i + 1; j < st_nodes.size(); ++j) {
            uint32_t t_node = st_nodes.at(j);

            uint32_t s_t_pair = (s_node << 16) | t_node;

            for (uint32_t p = 0; p < MAX_FAILURE_RATE; ++p) {
                MMP_connectivity.at(p).insert(std::make_pair(s_t_pair, 0));
                FMP_connectivity.at(p).insert(std::make_pair(s_t_pair, 0));
                SP_connectivity.at(p).insert(std::make_pair(s_t_pair, 0));
            }
        }
    }

    omp_set_num_threads(MAX_FAILURE_RATE > NUM_CORE ? NUM_CORE : MAX_FAILURE_RATE);
#pragma omp parallel for
    for (uint32_t p = 0; p < MAX_FAILURE_RATE; ++p) {

        for (uint32_t ts = 0; ts < NUMBER_OF_TIME_SLICES; ++ts) {
            std::vector<uint64_t> disabled_links;
            disabled_links.clear();

            std::random_device randomDevice;
            std::uniform_real_distribution<double> dist(0.0,MAX_FAILURE_RATE);
            for (uint32_t link_index = 0; link_index < links.size(); ++link_index) {
                double r = distribution(randomDevice);
                if (r < (double ) (p + 1)) {
                    disabled_links.push_back(links.at(link_index));
                    disabled_links.push_back(links_reverse.at(link_index));
                }
            }

            for (uint32_t i = 0; i < st_nodes.size(); ++i) {
                ns3::Ptr<ns3::SCION_Node> s_node = ns3::DynamicCast<ns3::SCION_Node>(nodes.Get(st_nodes.at(i)));
                for (uint32_t j = i + 1; j < st_nodes.size(); ++j) {
                    bool s_t_connected = false;

                    //ns3::Ptr<ns3::SCION_Node> t_node = ns3::DynamicCast<ns3::SCION_Node>(nodes.Get(st_nodes.at(j)));
                    uint32_t s_t_pair = (st_nodes.at(i) << 16) | st_nodes.at(j);

                    int path_no = -1;
                    for(auto const & len_beacons_set : s_node->beacon_store.at(st_nodes.at(j))) {
                        for (auto const & the_beacon : len_beacons_set.second) {
                            path_no++;
                            bool path_connected = true;
                            for (uint64_t link : the_beacon->the_path) {
                                if (std::find(disabled_links.begin(), disabled_links.end(), link) != disabled_links.end()) {
                                    path_connected = false;
                                    break;
                                }
                            }

                            if (path_connected) {
                                if (path_no == 0) {
                                    SP_connectivity.at(p).at(s_t_pair)++;
                                    FMP_connectivity.at(p).at(s_t_pair)++;
                                    MMP_connectivity.at(p).at(s_t_pair)++;
                                } else if (path_no == 1) {
                                    FMP_connectivity.at(p).at(s_t_pair)++;
                                    MMP_connectivity.at(p).at(s_t_pair)++;
                                } else if (path_no > 1) {
                                    MMP_connectivity.at(p).at(s_t_pair)++;
                                }

                                s_t_connected = true;
                                break;
                            }
                        }
                        if (s_t_connected) {
                            break;
                        }
                    }
                }
            }
        }
    }

    std::cout << "MMP" << std::endl;
    std::cout << "Snode" << "\t" << "Tnode" << "\t" << "h" << "\t" << "paths" << "\t" << "p" << "\t" << "c" << "\t" << "c/ts" << std::endl;

    for (uint32_t i = 0; i < st_nodes.size(); ++i) {
        ns3::Ptr<ns3::SCION_Node> s_node = ns3::DynamicCast<ns3::SCION_Node>(nodes.Get(st_nodes.at(i)));
        for (uint32_t j = i + 1; j < st_nodes.size(); ++j) {
//            ns3::Ptr<ns3::SCION_Node> t_node = ns3::DynamicCast<ns3::SCION_Node>(nodes.Get(st_nodes.at(j)));
            uint32_t s_t_pair = (st_nodes.at(i) << 16) | st_nodes.at(j);
            for (uint32_t p = 0; p < MAX_FAILURE_RATE; ++p) {
                int Snode = s_node->as_number;
                int Tnode = st_nodes.at(j);
                int h = s_node->beacon_store.at(Tnode).begin()->first;
                int paths = s_node->valid_beacons_count_per_dst_as.at(Tnode);
                int c = MMP_connectivity.at(p).at(s_t_pair);
                double c_ts = c / NUMBER_OF_TIME_SLICES;
                std::cout << Snode << "\t" << Tnode << "\t" << h << "\t" << paths << "\t" << p << "\t" << c << "\t" << c_ts << std::endl;
            }

        }
    }

    std::cout << "*************************************" << std::endl;
    std::cout << "FMP" << std::endl;
    std::cout << "Snode" << "\t" << "Tnode" << "\t" << "h" << "\t" << "paths" << "\t" << "p" << "\t" << "c" << "\t" << "c/ts" << std::endl;

    for (uint32_t i = 0; i < st_nodes.size(); ++i) {
        ns3::Ptr<ns3::SCION_Node> s_node = ns3::DynamicCast<ns3::SCION_Node>(nodes.Get(st_nodes.at(i)));
        for (uint32_t j = i + 1; j < st_nodes.size(); ++j) {
//            ns3::Ptr<ns3::SCION_Node> t_node = ns3::DynamicCast<ns3::SCION_Node>(nodes.Get(st_nodes.at(j)));
            uint32_t s_t_pair = (st_nodes.at(i) << 16) | st_nodes.at(j);
            for (uint32_t p = 0; p < MAX_FAILURE_RATE; ++p) {
                int Snode = s_node->as_number;
                int Tnode = st_nodes.at(j);
                int h = s_node->beacon_store.at(Tnode).begin()->first;
                int paths = s_node->valid_beacons_count_per_dst_as.at(Tnode) > 2 ? 2 : s_node->valid_beacons_count_per_dst_as.at(Tnode);
                int c = FMP_connectivity.at(p).at(s_t_pair);
                double c_ts = c / NUMBER_OF_TIME_SLICES;
                std::cout << Snode << "\t" << Tnode << "\t" << h << "\t" << paths << "\t" << p << "\t" << c << "\t" << c_ts << std::endl;
            }

        }
    }

    std::cout << "*************************************" << std::endl;
    std::cout << "SP" << std::endl;
    std::cout << "Snode" << "\t" << "Tnode" << "\t" << "h" << "\t" << "paths" << "\t" << "p" << "\t" << "c" << "\t" << "c/ts" << std::endl;

    for (uint32_t i = 0; i < st_nodes.size(); ++i) {
        ns3::Ptr<ns3::SCION_Node> s_node = ns3::DynamicCast<ns3::SCION_Node>(nodes.Get(st_nodes.at(i)));
        for (uint32_t j = i + 1; j < st_nodes.size(); ++j) {
//            ns3::Ptr<ns3::SCION_Node> t_node = ns3::DynamicCast<ns3::SCION_Node>(nodes.Get(st_nodes.at(j)));
            uint32_t s_t_pair = (st_nodes.at(i) << 16) | st_nodes.at(j);
            for (uint32_t p = 0; p < MAX_FAILURE_RATE; ++p) {
                int Snode = s_node->as_number;
                int Tnode = st_nodes.at(j);
                int h = s_node->beacon_store.at(Tnode).begin()->first;
                int paths = s_node->valid_beacons_count_per_dst_as.at(Tnode) > 1 ? 1 : s_node->valid_beacons_count_per_dst_as.at(Tnode);
                int c = FMP_connectivity.at(p).at(s_t_pair);
                double c_ts = c / NUMBER_OF_TIME_SLICES;
                std::cout << Snode << "\t" << Tnode << "\t" << h << "\t" << paths << "\t" << p << "\t" << c << "\t" << c_ts << std::endl;
            }

        }
    }
}
