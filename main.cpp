/**
 * @file main.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 *
 * This script takes the beaconing_period, expiration_period, simulator_time and the topology
 * to use as command line arguments. It will automatically instantiate nodes as Core or Leaf ASes
 * depending on the "type" property given in the xml file. This, together with the "rel" property on the links,
 * is used to infer over which interfaces the node needs to propagate beacons. All the nodes will instantiate
 * the baseline beaconing beaconServer when using this script. For criteria matching use the other script.
 * @see criteria_matching_sim
 */

#include <istream>

#include <random>
#include <set>

#include "ns3/ptr.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/nstime.h"

#include "src/SCION/headers/post_simulation_evaluations.h"
#include "src/SCION/headers/utils.h"
#include "src/SCION/headers/beaconing/beacon_server.h"
#include "src/SCION/headers/beaconing/baseline.h"
#include "src/SCION/headers/beaconing/scionlab_algo.h"
#include "src/SCION/headers/beaconing/criteria_matching.h"
#include "src/SCION/headers/beaconing/latency_optimized_beaconing.h"
#include "src/SCION/headers/scion_as.h"
#include "src/SCION/headers/scion_core_as.h"
#include "src/SCION/headers/global_scheduling.h"
#include "src/SCION/headers/path_server.h"
#include "src/SCION/headers/scion_host.h"

//rapidxml::xml_node<>* SetupTopologyFile (std::string topology_name);

void InstantiateASesFromTopo(rapidxml::xml_node<>* rootNode, std::string beaconing_policy_str, std::map<int32_t, uint16_t>& ASes, std::map<uint16_t,
                             int32_t>& index_to_AS_no, ns3::NodeContainer& nodes, uint16_t expiration_period, ns3::Time beaconing_period);

void InstantiateLinksFromTopo (rapidxml::xml_node<>* rootNode, ns3::NodeContainer& nodes, std::map<int32_t, uint16_t>& ASes);

void InitializeNodesAttributes(ns3::NodeContainer& nodes, std::string beaconing_policy_str);


int main(int argc, char *argv[]) {
    ns3::NodeContainer nodes;
    std::map<int32_t, uint16_t> ASes;
    std::map<uint16_t, int32_t> index_to_AS_no;

    std::string beaconing_policy_str;
    std::string beaconing_period_str;
    std::string expiration_period_str;
    std::string last_beaconing_event_time_str;
    std::string simulation_end_time_str;
    std::string topology_name;

    ns3::Time beaconing_period;
    ns3::Time last_beaconing_event_time;
    uint16_t expiration_period;

    ns3::Time simulation_end_time;

    if (argc == 7) {
        beaconing_policy_str = std::string (argv[1]);
        beaconing_period_str = std::string(argv[2]);
        expiration_period_str = std::string(argv[3]);
        last_beaconing_event_time_str = std::string(argv[4]);
        simulation_end_time_str = std::string(argv[5]);
        topology_name = std::string(argv[6]);
    } else {
        std::cerr << "Less arguments than expected!" << std::endl;
        return 1;
    }

    beaconing_period = ns3::Time(beaconing_period_str);
    last_beaconing_event_time = ns3::Time(last_beaconing_event_time_str);
    expiration_period = (uint16_t) ns3::Time(expiration_period_str).ToInteger(ns3::Time::MIN);

    // simulation_end_time can be something other than last_beaconing_event_time if we want to simulate other stuff as well
    simulation_end_time = ns3::Time(simulation_end_time_str);

    //rapidxml::xml_node<>* rootNode = SetupTopologyFile (topology_name);

    //std::string file = "/cluster/home/tabaeias/ns-3_beaconing_simulator/topology/" + std::string(topology_name) + ".xml";
    std::string file = "/home/tabaeias/ns-3_beaconing_simulator/topology/" + std::string(topology_name) + ".xml";
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

//    std::string out_path =
//            "/cluster/scratch/tabaeias/" + beaconing_policy_str + "_" + topology_name + "_" +
//            beaconing_period_str + "_" + expiration_period_str + "_" + last_beaconing_event_time_str + ".txt";

        std::string out_path =
            "/home/tabaeias/ns-3_beaconing_simulator/results/" + beaconing_policy_str + "_" + topology_name + "_" +
            beaconing_period_str + "_" + expiration_period_str + "_" + last_beaconing_event_time_str + ".txt";

    std::ofstream out(out_path);
    std::cout.rdbuf(out.rdbuf());


    ns3::SchedulePeriodicEvents(nodes, beaconing_period, last_beaconing_event_time, simulation_end_time);
    ns3::Simulator::Stop(simulation_end_time);
    ns3::Simulator::Run();

    ns3::DoFinalEvaluations(nodes, ASes, index_to_AS_no, expiration_period, beaconing_period, last_beaconing_event_time);

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

        uint16_t isd_number = 0;
        if (p.hasProperty("isd")) {
            isd_number = std::stoi(p.getProperty("isd"));
        }

        ns3::ld latency_coef = 0.0; //std::stod(p.getProperty("latency_coef"));
        ns3::ld bandwidth_coef = 0.0; //std::stod(p.getProperty("bandwidth_coef"));
        ns3::ld AS_level_diversity_coef = 0.0; // std::stod(p.getProperty("AS_level_diversity_coef"));
        ns3::ld link_level_diversity_coef = 1.0; //std::stod(p.getProperty("link_level_diversity_coef"));
        std::string type = "core"; //p.getProperty("type");

        ns3::beaconing_timing_params params = std::make_pair(beaconing_period, expiration_period);
        ns3::coefficients coefs = std::make_tuple(latency_coef, bandwidth_coef, AS_level_diversity_coef,
                                                  link_level_diversity_coef);

        ns3::BeaconServer* beaconing_policy;
        if (beaconing_policy_str == "baseline") {
            beaconing_policy = (ns3::BeaconServer*) new ns3::Baseline(params);
        } else if (beaconing_policy_str == "criteria_matching") {
            beaconing_policy = (ns3::BeaconServer *) new ns3::CriteriaMatching(params, coefs);
        } else if (beaconing_policy_str == "latency_optimized") {
            beaconing_policy = (ns3::BeaconServer *) new ns3::LatencyOptimized(params);
        } else if (beaconing_policy_str == "scionlab") {
            beaconing_policy = (ns3::BeaconServer *) new ns3::SCIONLAB(params);
        } else {
            beaconing_policy = (ns3::BeaconServer*) new ns3::Baseline(params);
        }

        ns3::PathServer* pathServer = new ns3::PathServer();

        ns3::Ptr<ns3::SCION_AS> node;
        if(type == "core"){
            node = ns3::CreateObject<ns3::SCION_Core_AS>(isd_number, node_counter, 0, ns3::Time(0));
        } else if(type =="non-core"){
            node = ns3::CreateObject<ns3::SCION_AS>(isd_number, node_counter, 0, ns3::Time(0));
        } else {
            std::cerr << "Incompatible node type!" << std::endl;
            exit(1);
        }
        node->SetBeaconServer(beaconing_policy);
        node->SetPathServer(pathServer);

        ns3::SCIONHost* scionHost = new ns3::SCIONHost(node, 0, 0, 0);
        node->AddHost(scionHost);

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
        ns3::neighbour_relation relation;

// Check for the 3 possibilities in CAIDA topology
        if (rel == "peer") {
            relation = ns3::neighbour_relation::PEER;
        } else if (rel == "core") {
            relation = ns3::neighbour_relation::CORE;
        } else if (rel == "customer") {
            relation = ns3::neighbour_relation::CUSTOMER;
        } else {
            relation = ns3::neighbour_relation::CORE;
        }

        ns3::Ptr<ns3::SCION_AS> fromNode;
        ns3::Ptr<ns3::SCION_AS> toNode;

        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            if ((ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i)))->as_number == ASes.at(to)) {
                toNode = ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i));
                break;
            }
        }

        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            if ((ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i)))->as_number == ASes.at(from)) {
                fromNode = ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i));
                break;
            }
        }

        ns3::PointToPointHelper helper;
        helper.Install(fromNode, toNode);

        ns3::Ptr<ns3::SCION_AS> to_my_node = (ns3::DynamicCast<ns3::SCION_AS>(toNode));
        ns3::Ptr<ns3::SCION_AS> from_my_node = (ns3::DynamicCast<ns3::SCION_AS>(fromNode));

        to_my_node->interfaces_coordinates.push_back(std::pair<ns3::ld, ns3::ld>(latitude, longitude));
        from_my_node->interfaces_coordinates.push_back(std::pair<ns3::ld, ns3::ld>(latitude, longitude));

        to_my_node->inter_as_bwds.push_back(bwd);
        from_my_node->inter_as_bwds.push_back(bwd);

        ns3::neighbour_relation to_rel;
        ns3::neighbour_relation from_rel;

        switch (relation) {
            case ns3::neighbour_relation::PEER:
                to_rel = ns3::neighbour_relation::PEER;
                from_rel = ns3::neighbour_relation::PEER;
                break;
            case ns3::neighbour_relation::CORE:
                to_rel = ns3::neighbour_relation::CORE;
                from_rel = ns3::neighbour_relation::CORE;
                break;
            case ns3::neighbour_relation::CUSTOMER:
                to_rel = ns3::neighbour_relation::PROVIDER;
                from_rel = ns3::neighbour_relation::CUSTOMER;
                break;
            case ns3::neighbour_relation::PROVIDER:
// Shouns3::ld never happen, there is no "Provider" type in xml files
                to_rel = ns3::neighbour_relation::CUSTOMER;
                from_rel = ns3::neighbour_relation::PROVIDER;
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
            ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i))->DoInitializations();
        } else if (beaconing_policy_str == "criteria_matching") {
            ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i))->DoInitializations(nodes.GetN());
        } else if (beaconing_policy_str == "latency_optimized") {
            ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i))->DoInitializations(nodes.GetN());
        } else if (beaconing_policy_str == "scionlab") {
            ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i))->DoInitializations(nodes.GetN());
        }
    }
}


