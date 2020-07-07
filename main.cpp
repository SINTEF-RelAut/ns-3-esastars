//
// Created by chrissy on 23.06.20.
//

#include "headers/utils.h"
#include "headers/beaconing_strategy.h"
#include "headers/criteria_matching.h"
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

// TODO: Document
void ProcessReceivedPacketsParallel(ns3::NodeContainer nodes) {
    // Just do this once instead of checking for node_no == 0 every time one lvl down.
    std::cout << "################################## " << ns3::DynamicCast<SCION_Node>(nodes.Get(0))->now << " #########################################" << std::endl;
    uint32_t node_number = nodes.GetN();
    #pragma omp parallel for
    for (uint32_t i = 0; i < node_number; ++i) {
        ns3::Ptr<SCION_Node> ns3_ptr_to_node = ns3::DynamicCast<SCION_Node>(nodes.Get(i));
        // Don't wanna pass around their smart pointer, seems to lead to race conditions regarding Uref.
        SCION_Node* node = ns3::GetPointer(ns3_ptr_to_node);
        node->strategy->UpdateBeaconStoreAndCountersBeforeBeaconing(node);
        ns3_ptr_to_node->Unref();
    }
}

int main(int argc, char *argv[]) {

    std::string beaconing_period_str;
    std::string expiration_period_str;
    std::string simulator_time_str;
    std::string topology_str;

    // Debug
    // TODO: Remove after testing
    if (argc >=4) {
        beaconing_period_str = argv[1];
        expiration_period_str = argv[2];
        simulator_time_str = argv[3];
        topology_str = argv[4];
    } else {
        // Initialize with dummy values in case you are invocing it with GDB
        beaconing_period_str = "30min";
        expiration_period_str = "2h";
        simulator_time_str = "5h";
        topology_str = "15_geo_rel_annotated";
    }

    ns3::Time beaconing_period = ns3::Time(beaconing_period_str);
    int64_t  expiration_period = ns3::Time(expiration_period_str).ToInteger(ns3::Time::NS);
    std::string file = "./topology/" + std::string(topology_str) + ".xml";

    std::ifstream fin(file.c_str());
    std::ostringstream sstr;
    sstr << fin.rdbuf();

    // TODO: Think about how to automatically set an appropriate name, maybe in conjunction with simulator configs?
    std::string out_path =
            "./results/main_crit_" + std::string(topology_str) + "_" +
            std::string(beaconing_period_str) + "_" + std::string(expiration_period_str) + "_" + std::string(simulator_time_str) + ".txt";
    std::ofstream out(out_path);
    std::cout.rdbuf(out.rdbuf());

    sstr.flush();
    fin.close();

    std::string xmlData = sstr.str();
    rapidxml::xml_document<> doc;
    doc.parse<0>(&xmlData[0]);

    rapidxml::xml_node<> *rootNode = doc.first_node("topology");
    rapidxml::xml_node<> *curNode;
/**/
    if (!rootNode) {
        std::cerr << "Empty topology!" << std::endl;
        return 1;
    }


    ns3::NodeContainer nodes;
    int16_t node_counter = 0;
    std::map<int32_t, uint16_t> ASes;


    curNode = rootNode->first_node("node");
    while (curNode) {
        int32_t as_number = std::stoi(getAttribute(curNode, "id"));
        PropertyContainer p = parseProperties(curNode);

        // TODO: Add the node type once you have an example from Seyedali

        ld latency_coef = std::stod(p.getProperty("latency_coef"));
        ld bandwidth_coef = std::stod(p.getProperty("bandwidth_coef"));
        ld AS_level_diversity_coef = std::stod(p.getProperty("AS_level_diversity_coef"));
        ld link_level_diversity_coef = std::stod(p.getProperty("link_level_diversity_coef"));
        std::string type = p.getProperty("type");

        simulator_params periods = std::make_pair(beaconing_period, expiration_period);
        coefficients coefs = std::make_tuple(latency_coef, bandwidth_coef, AS_level_diversity_coef,
                                             link_level_diversity_coef);
        // TODO: Better way to do this.
        BeaconingStrategy* strat = new Baseline();
        if(type == "core"){
            nodes.Add(ns3::CreateObject<SCION_Core_As>(node_counter, 0, coefs, periods, strat));
        } else if(type =="non-core"){
            nodes.Add(ns3::CreateObject<SCION_As>(node_counter, 0, coefs, periods, strat));
        } else{
            std::cerr << "Incompatible node type!" << std::endl;
            exit(1);
        }
        ASes.insert(std::make_pair(as_number, node_counter));

        node_counter++;

        curNode = curNode->next_sibling("node");
    }

    curNode = rootNode->first_node("link");
    while (curNode) {
        int32_t to = std::stoi(curNode->first_node("to")->value());
        int32_t from = std::stoi(curNode->first_node("from")->value());

        PropertyContainer p = parseProperties(curNode);

        ld latitude = std::stod(p.getProperty("latitude"));
        ld longitude = std::stod(p.getProperty("longitude"));
        int32_t bwd = std::stoi(p.getProperty("capacity"));
        std::string rel = p.getProperty("rel");
        SCION_Node::neighbour_relation relation;

        // Check for the 3 possibilities in CAIDA topology
        if(rel == "peer"){
            relation = SCION_Node::neighbour_relation::PEER;
        } else if(rel == "core"){
            relation = SCION_Node::neighbour_relation::CORE;
        } else if(rel == "customer"){
            relation = SCION_Node::neighbour_relation::CUSTOMER;
        }

        ns3::Ptr<SCION_Node> fromNode;
        ns3::Ptr<SCION_Node> toNode;

        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            if ((ns3::DynamicCast<SCION_Node>(nodes.Get(i)))->as_number == ASes.at(to)) {
                toNode = ns3::DynamicCast<SCION_Node>(nodes.Get(i));
                break;
            }
        }

        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            if ((ns3::DynamicCast<SCION_Node>(nodes.Get(i)))->as_number == ASes.at(from)) {
                fromNode = ns3::DynamicCast<SCION_Node>(nodes.Get(i));
                break;
            }
        }

        ns3::PointToPointHelper helper;
        helper.Install(fromNode, toNode);

        ns3::Ptr<SCION_Node> to_my_node = (ns3::DynamicCast<SCION_Node>(toNode));
        ns3::Ptr<SCION_Node> from_my_node = (ns3::DynamicCast<SCION_Node>(fromNode));

        // TODO: Guess we could save some space by computing the intra AS latencies now and only storing one value
        // Check if we need the lat & long for anything else?
        to_my_node->interfaces_coordinates.push_back(std::pair<ld, ld>(latitude, longitude));
        from_my_node->interfaces_coordinates.push_back(std::pair<ld, ld>(latitude, longitude));

        to_my_node->inter_as_bwds.push_back(bwd);
        from_my_node->inter_as_bwds.push_back(bwd);

        SCION_Node::neighbour_relation to_rel;
        SCION_Node::neighbour_relation from_rel;

        // TODO: Also think about how you will test against indiscriminate original Code that does not care about neighbour relations
        switch(relation){
            case SCION_Node::neighbour_relation::PEER:
                to_rel = SCION_Node::neighbour_relation::PEER;
                from_rel = SCION_Node::neighbour_relation::PEER;
                break;
            case SCION_Node::neighbour_relation::CORE:
                to_rel = SCION_Node::neighbour_relation::CORE;
                from_rel = SCION_Node::neighbour_relation::CORE;
                break;
            case SCION_Node::neighbour_relation::CUSTOMER:
                to_rel = SCION_Node::neighbour_relation::PROVIDER;
                from_rel = SCION_Node::neighbour_relation::CUSTOMER;
                break;
            case SCION_Node::neighbour_relation::PROVIDER:
                // Should never happen, there is no "Provider" type in xml files
                to_rel = SCION_Node::neighbour_relation::CUSTOMER;
                from_rel = SCION_Node::neighbour_relation::PROVIDER;
                assert(false);
                break;
        }

        if (to_my_node->interfaces_per_neighbor_as.find(from_my_node->as_number) !=
            to_my_node->interfaces_per_neighbor_as.end()) {
            to_my_node->interfaces_per_neighbor_as.at(from_my_node->as_number).push_back(std::make_pair((uint16_t) to_my_node->GetNDevices() - 1, to_rel));
        } else {
            std::vector<std::pair<uint16_t, SCION_Node::neighbour_relation>> tmp;
            tmp.push_back(std::make_pair((uint16_t) to_my_node->GetNDevices() - 1, to_rel));
            to_my_node->interfaces_per_neighbor_as.insert(std::make_pair(from_my_node->as_number, tmp));
            to_my_node->neighbors.push_back(from_my_node->as_number);
        }

        if (from_my_node->interfaces_per_neighbor_as.find(to_my_node->as_number) !=
            from_my_node->interfaces_per_neighbor_as.end()) {
            from_my_node->interfaces_per_neighbor_as.at(to_my_node->as_number).push_back(std::make_pair(from_my_node->GetNDevices() - 1, from_rel));
        } else {
            std::vector<std::pair<uint16_t, SCION_Node::neighbour_relation>> tmp;
            tmp.push_back(std::make_pair((uint16_t) from_my_node->GetNDevices() - 1, from_rel));
            from_my_node->interfaces_per_neighbor_as.insert(std::make_pair(to_my_node->as_number, tmp));
            from_my_node->neighbors.push_back(to_my_node->as_number);
        }

        curNode = curNode->next_sibling("link");
    }

    for (uint64_t i = 0; i < nodes.GetN(); ++i) {
        ns3::DynamicCast<SCION_Node>(nodes.Get(i))->DoInitializations();
    }

    ns3::Time scheduling_delay = ns3::Time(beaconing_period.ns3::Time::GetSeconds() / 2);
    for (ns3::Time t = ns3::Seconds(0.0); t < ns3::Time(simulator_time_str); t += beaconing_period) {
        ns3::Simulator::Schedule(t + ns3::Seconds(scheduling_delay), &ProcessReceivedPacketsParallel, nodes);

        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            ns3::Ptr<SCION_Node> the_node = ns3::DynamicCast<SCION_Node>(nodes.Get(i));
            ns3::Simulator::Schedule(t, &SCION_Node::CoreBeaconing, the_node);
            ns3::Simulator::Schedule(t, &SCION_Node::IntraISDBeaconing, the_node);
        }
    }

    ns3::Simulator::Stop(ns3::Time(simulator_time_str));
    ns3::Simulator::Run();

    //############################################################################################################################################################
    for (ns3::Time t = ns3::Seconds(0.0); t < ns3::Time(simulator_time_str); t += beaconing_period) {
        std::cout << "####################################### frequencies of consumed bandwidth at Time "
                  << t
                  << " #######################################" << std::endl;

        std::map<uint32_t, uint32_t> frequencies_of_consumed_bwd;
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            ns3::Ptr<SCION_Node> the_node = ns3::DynamicCast<SCION_Node>(nodes.Get(i));
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

    //############################################################################################################################################################
    for (uint32_t path_length = 1; path_length <= 4; ++path_length) {
        // TODO: Correct this? Not sure anymore how it should be..
        std::cout
                << "######################################### frequencies of path counts per source AS with length "
                << path_length - 1
                << "#########################################"
                << std::endl;
        std::map<uint64_t, uint64_t> frequencies_of_path_counts_per_src_as_with_certain_length;
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            for (auto const &src_as_beacons_pair : ns3::DynamicCast<SCION_Node>(nodes.Get(i))->beacon_store) {
                uint64_t number_of_paths_with_certain_length = 0;
                if(src_as_beacons_pair.second->find(path_length) == src_as_beacons_pair.second->end()){
                    continue;
                } else {
                    number_of_paths_with_certain_length = src_as_beacons_pair.second->at(path_length)->size();
                }

                if (frequencies_of_path_counts_per_src_as_with_certain_length.find(
                        number_of_paths_with_certain_length) != frequencies_of_path_counts_per_src_as_with_certain_length.end()) {
                    frequencies_of_path_counts_per_src_as_with_certain_length.at(number_of_paths_with_certain_length)++;
                } else {
                    frequencies_of_path_counts_per_src_as_with_certain_length.insert(
                            std::make_pair(number_of_paths_with_certain_length, 1));
                }
            }
        }

        std::cout << "path count per source AS" << "\t" << "frequency" << std::endl;
        for (auto const &count_freq_pair : frequencies_of_path_counts_per_src_as_with_certain_length) {
            std::cout << count_freq_pair.first << "\t" << count_freq_pair.second << std::endl;
        }
    }

    std::cout << "###################################################### PATH QUALITY #############################################################"
              << std::endl;
    std::cout << "##                                                                                                                             ##"
              << std::endl;

    std::map <ld, uint64_t> satisfaction_stat;
    std::map <ld, uint64_t> AS_level_diversity_stat;
    std::map <ld, uint64_t> link_level_diversity_stat;

    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        ns3::DynamicCast<SCION_Node>(nodes.Get(i))->FinalPathEvaluation(satisfaction_stat,
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

    ns3::Simulator::Destroy();
    return 0;
}