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
#include <omp.h>
#include <random>
#include <set>
#include <yaml-cpp/yaml.h>

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
#include "src/SCION/headers/time_server.h"
#include "src/SCION/headers/externs.h"

void SetTimeResolution (std::string time_res_str);

//rapidxml::xml_node<>* SetupTopologyFile (std::string topology_name);

void InstantiateASesFromTopo( rapidxml::xml_node<>* xml_root,  std::map<int32_t, uint16_t>& AS_no_to_index,
                             std::map<uint16_t, int32_t>& index_to_AS_no, ns3::NodeContainer& AS_nodes, const YAML::Node & config);

void InstantiateLinksFromTopo ( rapidxml::xml_node<>* xml_root, ns3::NodeContainer& AS_nodes, std::map<int32_t, uint16_t>& AS_no_to_index, const YAML::Node& config);

void InitializeNodesAttributes(const ns3::NodeContainer& AS_nodes, const YAML::Node& config);



int main(int argc, char *argv[]) {
    std::map<int32_t, uint16_t> AS_no_to_index;
    std::map<uint16_t, int32_t> index_to_AS_no;

    if (argc != 2) {
        std::cerr << "Please pass the config file location as the argument." << std::endl;
        return 1;
    }

    YAML::Node config = YAML::LoadFile(std::string (argv[1]));

    if (!config["time_resolution"]) {
        std::cerr << "Please specify simulator's time resolution." << std::endl;
        return 1;
    }

    if (!config["topology"]) {
        std::cerr << "No topology file specified in the config file." << std::endl;
        return 1;
    }

    if (!config["output"]) {
        std::cerr << "Please specify output file's path." << std::endl;
        return 1;
    }


    if (!config["simulation_duration"]) {
        std::cerr << "Simulation duration is not specified in the config file." << std::endl;
        return 1;
    }

    if (!config["NUM_CORE"]) {
        std::cerr << "Please Specify number of cores to use." << std::endl;
        return 1;
    }

    if (!config["beacon_server"]
    && !(config["path_server"])
    && !(config["border_router"])) {
        std::cerr << "No simulation is possible." << std::endl;
        return 1;
    }

    SetTimeResolution(config["simulation_duration"].as<std::string>());

    std::string topology_file = config["topology"].as<std::string>();

    std::string out_path = config["output"].as<std::string>();

    ns3::Time simulation_end_time = ns3::Time(config["simulation_duration"].as<std::string>());

    NUM_CORE = config["NUM_CORE"].as<uint32_t>();

    std::ifstream fin(topology_file.c_str());
    std::ostringstream sstr;
    sstr << fin.rdbuf();

    sstr.flush();
    fin.close();

    std::string xml_data = sstr.str();
    rapidxml::xml_document<> doc;
    doc.parse<0>(&xml_data[0]);

    rapidxml::xml_node<> *xml_root = doc.first_node("topology");

    if (!xml_root) {
        std::cerr << "Empty topology!" << std::endl;
        exit(1);
    }

    std::ofstream out(out_path);
    std::cout.rdbuf(out.rdbuf());



    InstantiateASesFromTopo( xml_root, AS_no_to_index, index_to_AS_no, nodes, config);
    InstantiateLinksFromTopo( xml_root, nodes, AS_no_to_index, config);
    InitializeNodesAttributes( nodes, config);

    ns3::SchedulePeriodicEvents(config, nodes);
    ns3::Simulator::Stop(simulation_end_time);
    ns3::Simulator::Run();

    ns3::DoFinalEvaluations(config, nodes, AS_no_to_index, index_to_AS_no);

    ns3::Simulator::Destroy();

    return 0;
}

void SetTimeResolution(std::string time_res_str) {
    if (time_res_str == "FS") {
        ns3::Time::SetResolution(ns3::Time::FS);
    } else if (time_res_str == "PS") {
        ns3::Time::SetResolution(ns3::Time::PS);
    } else if (time_res_str == "NS") {
        ns3::Time::SetResolution(ns3::Time::NS);
    } else if (time_res_str == "US") {
        ns3::Time::SetResolution(ns3::Time::US);
    } else if (time_res_str == "MS") {
        ns3::Time::SetResolution(ns3::Time::MS);
    } else if (time_res_str == "S") {
        ns3::Time::SetResolution(ns3::Time::S);
    } else if (time_res_str == "MIN") {
        ns3::Time::SetResolution(ns3::Time::MIN);
    }
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

void InstantiateASesFromTopo(rapidxml::xml_node<>* xml_root, std::map<int32_t, uint16_t>& AS_no_to_index,
                             std::map<uint16_t,int32_t>& index_to_AS_no, ns3::NodeContainer& AS_nodes, const YAML::Node& config) {


    ns3::Time beaconing_period = ns3::Time(config["beacon_service"]["period"].as<std::string>());
    ns3::Time last_beaconing_event_time = ns3::Time(config["beacon_service"]["last_beaconing"].as<std::string>());
    uint16_t expiration_period = ns3::Time(config["beacon_service"]["expiration_period"].as<std::string>()).ToInteger(ns3::Time::MIN);

    int16_t node_counter = 0;

    rapidxml::xml_node<>* cur_xml_node = xml_root->first_node("node");
    while (cur_xml_node) {
        int32_t as_number = std::stoi(ns3::getAttribute(cur_xml_node, "id"));
        ns3::PropertyContainer p = ns3::parseProperties(cur_xml_node);

        uint16_t isd_number = 0;
        if (p.hasProperty("isd")) {
            isd_number = std::stoi(p.getProperty("isd"));
        }

        ns3::beaconing_timing_params params = std::make_pair(beaconing_period, expiration_period);
        std::string type = "core"; //p.getProperty("type");

        std::string beaconing_policy_str = config["beacon_service"]["policy"].as<std::string>();
        ns3::BeaconServer* beaconing_policy;
        if (beaconing_policy_str == "baseline") {
            beaconing_policy = (ns3::BeaconServer*) new ns3::Baseline(params);
        } else if (beaconing_policy_str == "criteria_matching") {
            ns3::ld latency_coef = 0.0; //std::stod(p.getProperty("latency_coef"));
            ns3::ld bandwidth_coef = 0.0; //std::stod(p.getProperty("bandwidth_coef"));
            ns3::ld AS_level_diversity_coef = 0.0; // std::stod(p.getProperty("AS_level_diversity_coef"));
            ns3::ld link_level_diversity_coef = 1.0; //std::stod(p.getProperty("link_level_diversity_coef"));

            ns3::coefficients coefs = std::make_tuple(latency_coef, bandwidth_coef, AS_level_diversity_coef,
                                                      link_level_diversity_coef);
            beaconing_policy = (ns3::BeaconServer *) new ns3::CriteriaMatching(params, coefs);
        } else if (beaconing_policy_str == "latency_optimized") {
            beaconing_policy = (ns3::BeaconServer *) new ns3::LatencyOptimized(params);
        } else if (beaconing_policy_str == "scionlab") {
            beaconing_policy = (ns3::BeaconServer *) new ns3::SCIONLAB(params);
        } else {
            beaconing_policy = (ns3::BeaconServer*) new ns3::Baseline(params);
        }

        ns3::Ptr<ns3::SCION_AS> AS_node;
        if(type == "core"){
            AS_node = ns3::CreateObject<ns3::SCION_Core_AS>(isd_number, node_counter, 0, ns3::Time(0));
        } else if(type =="non-core"){
            AS_node = ns3::CreateObject<ns3::SCION_AS>(isd_number, node_counter, 0, ns3::Time(0));
        } else {
            std::cerr << "Incompatible AS_node type!" << std::endl;
            exit(1);
        }

        AS_node->SetBeaconServer(beaconing_policy);
        beaconing_policy->SetNode(PeekPointer(AS_node));

        if (config["path_service"]) {
            ns3::PathServer* path_server = new ns3::PathServer( 0, isd_number, node_counter, 1,0.0,  0.0, PeekPointer(AS_node));
            AS_node->SetPathServer(path_server);
        }

        if (config["time_service"]) {
            ns3::SCIONHost* time_server = new ns3::TimeServer(0, isd_number, node_counter, 2,0.0, 0.0, PeekPointer(AS_node),
                                                             ns3::Time(config["time_service"]["max_initial_drift"].as<std::string>()),
                                                             ns3::Time(config["time_service"]["max_drift_per_day"].as<std::string>()),
                                                             ns3::Time(config["time_service"]["global_cut_off"].as<std::string>()),
                                                             ns3::Time(config["time_service"]["first_event"].as<std::string>()),
                                                             ns3::Time(config["time_service"]["last_event"].as<std::string>()),
                                                             ns3::Time(config["time_service"]["list_of_ases_req_period"].as<std::string>()),
                                                             ns3::Time(config["time_service"]["time_sync_period"].as<std::string>()),
                                                             config["time_service"]["G"].as<uint32_t>(),
                                                             config["time_service"]["number_of_paths_to_use_for_global_sync"].as<uint32_t>(),
                                                             config["time_service"]["read_disjoint_paths"].as<uint32_t>(),
                                                             config["time_service"]["set_of_disjoint_paths_directory"].as<std::string>());
            AS_node->AddHost(time_server);
        }



        AS_nodes.Add(AS_node);

        as_to_isd_map.insert(std::make_pair(node_counter, isd_number));

        AS_no_to_index.insert(std::make_pair(as_number, node_counter));
        index_to_AS_no.insert(std::make_pair(node_counter, as_number));

        node_counter++;

        cur_xml_node = cur_xml_node->next_sibling("node");
    }
}

void InstantiateLinksFromTopo (rapidxml::xml_node<>* xml_root, ns3::NodeContainer& AS_nodes, std::map<int32_t, uint16_t>& AS_no_to_index, const YAML::Node& config){
    rapidxml::xml_node<> *curr_xml_node = xml_root->first_node("link");
    while (curr_xml_node) {
        int32_t to = std::stoi(curr_xml_node->first_node("to")->value());
        int32_t from = std::stoi(curr_xml_node->first_node("from")->value());

        ns3::PropertyContainer p = ns3::parseProperties(curr_xml_node);

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

        ns3::Ptr<ns3::SCION_AS> from_AS;
        ns3::Ptr<ns3::SCION_AS> to_AS;

        for (uint32_t i = 0; i < AS_nodes.GetN(); ++i) {
            if ((ns3::DynamicCast<ns3::SCION_AS>(AS_nodes.Get(i)))->as_number == AS_no_to_index.at(to)) {
                to_AS = ns3::DynamicCast<ns3::SCION_AS>(AS_nodes.Get(i));
                break;
            }
        }

        for (uint32_t i = 0; i < AS_nodes.GetN(); ++i) {
            if ((ns3::DynamicCast<ns3::SCION_AS>(AS_nodes.Get(i)))->as_number == AS_no_to_index.at(from)) {
                from_AS = ns3::DynamicCast<ns3::SCION_AS>(AS_nodes.Get(i));
                break;
            }
        }

        ns3::PointToPointHelper helper;
        helper.Install(from_AS, to_AS);

        to_AS->AddToRemoteASInfo(from_AS->GetNDevices() - 1, ns3::PeekPointer(from_AS));
        from_AS->AddToRemoteASInfo(to_AS->GetNDevices() - 1, ns3::PeekPointer(to_AS));

        if (config["border_router"]) {
            ns3::Time to_processing_delay = ns3::NanoSeconds(10);
            ns3::Time from_processing_delay = ns3::NanoSeconds(10);

            ns3::Time to_processing_throughput_delay = ns3::PicoSeconds(200); // 5 Giga packets per second
            ns3::Time from_processing_throughput_delay = ns3::PicoSeconds(200);

            ns3::BorderRouter *to_br = to_AS->AddBR(latitude, longitude, to_processing_delay,
                                                    to_processing_throughput_delay);
            ns3::BorderRouter *from_br = from_AS->AddBR(latitude, longitude, from_processing_delay,
                                                        from_processing_throughput_delay);

            to_br->AddToPropagationDelays(
                    ns3::NanoSeconds(5)); // Assuming 1m fiber optic between neighboring devices in the same location
            to_br->AddToTransmissionDelays(ns3::PicoSeconds(20)); //Per byte transmission delay assuming 400 Gbps link

            from_br->AddToPropagationDelays(ns3::NanoSeconds(5));
            from_br->AddToTransmissionDelays(ns3::PicoSeconds(20));

            to_br->AddToIFForwadingTable(to_AS->GetNDevices() - 1, to_br->GetNDevices() - 1);
            from_br->AddToIFForwadingTable(from_AS->GetNDevices() - 1, from_br->GetNDevices() - 1);

            to_br->AddToRemoteNodesInfo(from_br, from_br->GetNDevices() - 1, from_AS->isd_number, from_AS->as_number);
            from_br->AddToRemoteNodesInfo(to_br, to_br->GetNDevices() - 1, to_AS->isd_number, to_AS->as_number);
        }

        to_AS->inter_as_bwds.push_back(bwd);
        from_AS->inter_as_bwds.push_back(bwd);

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

        to_AS->interface_to_neighbor_map.insert(std::make_pair(to_AS->GetNDevices() - 1, from_AS->as_number));
        if (to_AS->interfaces_per_neighbor_as.find(from_AS->as_number) !=
            to_AS->interfaces_per_neighbor_as.end()) {
            to_AS->interfaces_per_neighbor_as.at(from_AS->as_number).push_back(
                    (uint16_t) to_AS->GetNDevices() - 1);
        } else {
            std::vector<uint16_t> tmp;
            tmp.push_back((uint16_t) to_AS->GetNDevices() - 1);
            to_AS->interfaces_per_neighbor_as.insert(std::make_pair(from_AS->as_number, tmp));
            to_AS->neighbors.push_back(std::make_pair(from_AS->as_number, to_rel));
        }

        from_AS->interface_to_neighbor_map.insert(std::make_pair(from_AS->GetNDevices() - 1, to_AS->as_number));
        if (from_AS->interfaces_per_neighbor_as.find(to_AS->as_number) !=
            from_AS->interfaces_per_neighbor_as.end()) {
            from_AS->interfaces_per_neighbor_as.at(to_AS->as_number).push_back(
                    from_AS->GetNDevices() - 1);
        } else {
            std::vector<uint16_t> tmp;
            tmp.push_back((uint16_t) from_AS->GetNDevices() - 1);
            from_AS->interfaces_per_neighbor_as.insert(std::make_pair(to_AS->as_number, tmp));
            from_AS->neighbors.push_back(std::make_pair(to_AS->as_number, from_rel));
        }

        curr_xml_node = curr_xml_node->next_sibling("link");
    }
}

void InitializeNodesAttributes(const ns3::NodeContainer& AS_nodes, const YAML::Node& config ) {
    std::string beaconing_policy_str = config["beacon_service"]["policy"].as<std::string>();
//    omp_set_num_threads(NUM_CORE);
//#pragma omp parallel for
    for (uint64_t i = 0; i < AS_nodes.GetN(); ++i) {
        if (beaconing_policy_str == "baseline") {
            ns3::DynamicCast<ns3::SCION_AS>(AS_nodes.Get(i))->DoInitializations();
        } else if (beaconing_policy_str == "criteria_matching") {
            ns3::DynamicCast<ns3::SCION_AS>(AS_nodes.Get(i))->DoInitializations(AS_nodes.GetN());
        } else if (beaconing_policy_str == "latency_optimized") {
            ns3::DynamicCast<ns3::SCION_AS>(AS_nodes.Get(i))->DoInitializations(AS_nodes.GetN());
        } else if (beaconing_policy_str == "scionlab") {
            ns3::DynamicCast<ns3::SCION_AS>(AS_nodes.Get(i))->DoInitializations(AS_nodes.GetN());
        }
    }
}


