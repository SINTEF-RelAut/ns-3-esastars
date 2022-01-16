/**
 * @file main.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
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
#include "src/SCION/headers/beaconing/diversity_age_based.h"
#include "src/SCION/headers/beaconing/latency_optimized_beaconing.h"
#include "src/SCION/headers/beaconing/green_beaconing.h"
#include "src/SCION/headers/scion_as.h"
#include "src/SCION/headers/scion_core_as.h"
#include "src/SCION/headers/global_scheduling.h"
#include "src/SCION/headers/path_server.h"
#include "src/SCION/headers/scion_host.h"
#include "src/SCION/headers/time_server.h"
#include "src/SCION/headers/externs.h"

void SetTimeResolution (std::string time_res_str);

//rapidxml::xml_node<>* SetupTopologyFile (std::string topology_name);

void InstantiateASesFromTopo(rapidxml::xml_node<>* xml_root,
                             std::map<int32_t, uint16_t>& real_to_alias_as_no,
                             std::map<uint16_t, int32_t>& alias_to_real_as_no,
                             ns3::NodeContainer& AS_nodes,
                             const YAML::Node & config);

void InstantiatePathServers(const YAML::Node& config,
                            const ns3::NodeContainer& AS_nodes);

void GetMaliciousTimeRefAndTimeServer (const ns3::NodeContainer& AS_nodes,
                                       const YAML::Node& config,
                                       std::vector<std::string>& time_reference_types,
                                       std::vector<std::string>& time_server_types);

void GetTimeServiceSnapShotTypes (const ns3::NodeContainer& AS_nodes,
                                  const YAML::Node& config,
                                  std::vector<std::string>& snapshot_types);

void GetTimeServiceAlgVersions (const ns3::NodeContainer& AS_nodes,
                                const YAML::Node& config,
                                std::vector<std::string>& alg_versions,
                                const std::vector<std::string>& snapshot_types);

void InstantiateTimeServers(const YAML::Node& config,
                            const ns3::NodeContainer& AS_nodes);

void GetASesWithMaliciousBRs (const ns3::NodeContainer& AS_nodes,
                              const YAML::Node& config,
                              std::vector<std::string>& border_routers_malicious_action);

void InstantiateLinksFromTopo (rapidxml::xml_node<>* xml_root,
                               ns3::NodeContainer& AS_nodes,
                               const std::map<int32_t, uint16_t>& real_to_alias_as_no,
                               const YAML::Node& config);

void InitializeASesAttributes(const ns3::NodeContainer& AS_nodes,
                              std::map<int32_t, uint16_t>& real_to_alias_as_no,
                              const YAML::Node& config);

bool OnlyPropagationDelay(const YAML::Node& config);

int main(int argc, char *argv[]) {
    std::map<int32_t, uint16_t> real_to_alias_as_no;
    std::map<uint16_t, int32_t> alias_to_real_as_no;

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

    if (!config["beacon_service"]
    && !(config["path_service"])
    && !(config["border_router"])) {
        std::cerr << "No simulation is possible." << std::endl;
        return 1;
    }

    SetTimeResolution(config["time_resolution"].as<std::string>());

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

    InstantiateASesFromTopo(xml_root, real_to_alias_as_no, alias_to_real_as_no, nodes, config);

    if (config["path_service"]) {
        InstantiatePathServers(config, nodes);
    }

    if(config["time_service"]) {
        InstantiateTimeServers(config,  nodes);
    }

    InstantiateLinksFromTopo(xml_root, nodes, real_to_alias_as_no, config);
    InitializeASesAttributes(nodes, real_to_alias_as_no, config);

    ns3::SchedulePeriodicEvents(config);
    ns3::Simulator::Stop(simulation_end_time);
    ns3::Simulator::Run();

    ns3::DoFinalEvaluations(config, nodes, real_to_alias_as_no, alias_to_real_as_no);

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

void InstantiateASesFromTopo(rapidxml::xml_node<>* xml_root,
                             std::map<int32_t, uint16_t>& real_to_alias_as_no,
                             std::map<uint16_t,int32_t>& alias_to_real_as_no,
                             ns3::NodeContainer& AS_nodes,
                             const YAML::Node& config) {

    ns3::Time beaconing_period = ns3::Time(config["beacon_service"]["period"].as<std::string>());
    ns3::Time last_beaconing_event_time = ns3::Time(config["beacon_service"]["last_beaconing"].as<std::string>());
    uint16_t expiration_period = ns3::Time(config["beacon_service"]["expiration_period"].as<std::string>()).ToInteger(ns3::Time::MIN);

    int16_t alias_as_no = 0;

    bool parallel_scheduler = true;

    rapidxml::xml_node<>* cur_xml_node = xml_root->first_node("node");
    while (cur_xml_node) {
        int32_t real_as_no = std::stoi(ns3::getAttribute(cur_xml_node, "id"));
        ns3::PropertyContainer p = ns3::parseProperties(cur_xml_node);

        uint16_t isd_number = 0;
        if (p.hasProperty("isd")) {
            isd_number = std::stoi(p.getProperty("isd"));
        }

        ns3::beaconing_timing_params timing_params = std::make_pair(beaconing_period, expiration_period);
        std::string type = "core"; //p.getProperty("type");

        std::string beaconing_policy_str = config["beacon_service"]["policy"].as<std::string>();
        ns3::BeaconServer* beaconing_policy;
        if (beaconing_policy_str == "baseline") {
            beaconing_policy = (ns3::BeaconServer*) new ns3::Baseline(parallel_scheduler, timing_params);
        } else if (beaconing_policy_str == "diversity_age_based") {
            beaconing_policy = (ns3::BeaconServer *) new ns3::DiversityAgeBased(parallel_scheduler, timing_params);
        } else if (beaconing_policy_str == "green_beaconing") {
            ns3::ld dirty_energy_ratio = std::stod(p.getProperty("dirty_energy_ratio"));
            ns3::ld sun_energy_ratio = std::stod(p.getProperty("sun_energy_ratio"));
            beaconing_policy = (ns3::BeaconServer *) new ns3::GreenBeaconing(parallel_scheduler, timing_params, dirty_energy_ratio, sun_energy_ratio);
        } else if (beaconing_policy_str == "latency_optimized") {
            beaconing_policy = (ns3::BeaconServer *) new ns3::LatencyOptimized(parallel_scheduler, timing_params);
        } else if (beaconing_policy_str == "scionlab") {
            beaconing_policy = (ns3::BeaconServer *) new ns3::SCIONLAB(parallel_scheduler, timing_params);
        } else {
            beaconing_policy = (ns3::BeaconServer*) new ns3::Baseline(parallel_scheduler, timing_params);
        }

        ns3::Ptr<ns3::SCION_AS> AS_node;
        if(type == "core"){
            AS_node = ns3::CreateObject<ns3::SCION_Core_AS>(isd_number, alias_as_no, 0, ns3::Time(0));
        } else if(type == "non-core"){
            AS_node = ns3::CreateObject<ns3::SCION_AS>(isd_number, alias_as_no, 0, ns3::Time(0));
        } else {
            std::cerr << "Incompatible AS_node type!" << std::endl;
            exit(1);
        }

        AS_node->SetBeaconServer(beaconing_policy);
        beaconing_policy->SetAS(PeekPointer(AS_node));

        AS_nodes.Add(AS_node);

        as_to_isd_map.insert(std::make_pair(alias_as_no, isd_number));

        real_to_alias_as_no.insert(std::make_pair(real_as_no, alias_as_no));
        alias_to_real_as_no.insert(std::make_pair(alias_as_no, real_as_no));

        alias_as_no++;

        cur_xml_node = cur_xml_node->next_sibling("node");
        parallel_scheduler = false;
    }
}

void InstantiatePathServers(const YAML::Node& config,
                            const ns3::NodeContainer& AS_nodes) {
    bool only_propagation_delay = OnlyPropagationDelay(config);

    for (uint32_t i = 0; i < AS_nodes.GetN(); ++i) {
        ns3::SCION_AS *AS_node = dynamic_cast<ns3::SCION_AS *>(PeekPointer(AS_nodes.Get(i)));
        ns3::PathServer* path_server = new ns3::PathServer( 0, AS_node->isd_number,
                                                            AS_node->as_number, 1,
                                                            0.0,  0.0, AS_node);
        AS_node->SetPathServer(path_server);

        if (only_propagation_delay) {
            path_server->SetProcessingDelay(ns3::Time(0), ns3::Time(0));
        } else {
            path_server->SetProcessingDelay(ns3::NanoSeconds(10), ns3::PicoSeconds(200));
        }
    }
}

void GetMaliciousTimeRefAndTimeServer (const ns3::NodeContainer& AS_nodes,
                                       const YAML::Node& config,
                                       std::vector<std::string>& time_reference_types,
                                       std::vector<std::string>& time_server_types) {

    std::vector<uint16_t> indices_time_references;
    std::vector<uint16_t> indices_time_servers;

    uint16_t number_of_ASes = AS_nodes.GetN();

    for (uint32_t i = 0; i < number_of_ASes; ++i) {
        indices_time_references.push_back(i);
        indices_time_servers.push_back(i);
    }

    time_reference_types.resize(number_of_ASes);
    time_server_types.resize(number_of_ASes);

    if (config["time_service"]["truly_random_malicious"].as<uint16_t>() == 1) {
        std::random_shuffle(indices_time_references.begin(), indices_time_references.end(), ns3::truly_random_generator);
        std::random_shuffle(indices_time_servers.begin(), indices_time_servers.end(), ns3::truly_random_generator);
    } else {
        std::random_shuffle(indices_time_references.begin(), indices_time_references.end(), ns3::random_generator);
        std::random_shuffle(indices_time_servers.begin(), indices_time_servers.end(), ns3::random_generator);
    }

    uint16_t number_of_malicious_time_references = (uint16_t) std::floor(
            ((double ) config["time_service"]["percent_of_malicious_time_references"].as<uint16_t>() * (double ) number_of_ASes) / 100.0);

    uint16_t number_of_malicious_time_servers = (uint16_t) std::floor(
            ((double ) config["time_service"]["percent_of_malicious_time_servers"].as<uint16_t>() * (double ) number_of_ASes) / 100.0);

    if (config["time_service"]["reference_clk"].as<std::string>() == "OFF") {
        for (uint16_t i = 0; i < number_of_ASes; ++i) {
            time_reference_types.at(i) = "OFF";
        }
    } else {
        for (uint16_t i = 0; i < number_of_malicious_time_references; ++i) {
            time_reference_types.at(indices_time_references.at(i)) = "MALICIOUS";
        }

        for (uint16_t i = number_of_malicious_time_references; i < number_of_ASes; ++i) {
            time_reference_types.at(indices_time_references.at(i)) = "ON";
        }

    }

    for (uint16_t i = 0; i < number_of_malicious_time_servers; ++i) {
        time_server_types.at(indices_time_servers.at(i)) = "MALICIOUS";
    }

    for (uint16_t i = number_of_malicious_time_servers; i < number_of_ASes; ++i) {
        time_server_types.at(indices_time_servers.at(i)) = "NORMAL";
    }
}

void GetTimeServiceSnapShotTypes(const ns3::NodeContainer& AS_nodes,
                                 const YAML::Node& config,
                                 std::vector<std::string>& snapshot_types,
                                 uint16_t& global_scheduler_and_printer) {
    global_scheduler_and_printer = 0;
    if (config["time_service"]["snapshot_type"].as<std::string>() == "PRINT_OFFSET_DIFF") {
    	std::random_device rd;
    	std::uniform_int_distribution<uint16_t> dist (0, AS_nodes.GetN() - 1);
    	global_scheduler_and_printer = dist(rd);
    }
    for (uint32_t i = 0; i < AS_nodes.GetN(); ++i) {
        if (config["time_service"]["snapshot_type"].as<std::string>() == "PRINT_OFFSET_DIFF") {
            if (i == global_scheduler_and_printer) {
                snapshot_types.push_back("PRINT_OFFSET_DIFF");
            } else {
                snapshot_types.push_back("OFF");
            }
        } else {
	    	
            snapshot_types.push_back(config["time_service"]["snapshot_type"].as<std::string>());
        }
    }
}

void GetTimeServiceAlgVersions (const ns3::NodeContainer& AS_nodes,
                                const YAML::Node& config,
                                std::vector<std::string>& alg_versions,
                                const std::vector<std::string>& snapshot_types) {
    for (uint32_t i = 0; i < AS_nodes.GetN(); ++i) {
        if (snapshot_types.at(i) == "OFF") {
            alg_versions.push_back(config["time_service"]["alg_version_non_printing_instances"].as<std::string>());
        } else {
            alg_versions.push_back(config["time_service"]["alg_version_printing_instances"].as<std::string>());
        }
    }
}

void InstantiateTimeServers(const YAML::Node& config,
                            const ns3::NodeContainer& AS_nodes) {

    std::vector<std::string> time_reference_types;
    std::vector<std::string> time_server_types;
    std::vector<std::string> snapshot_types;
    std::vector<std::string> alg_versions;
    uint16_t global_scheduler_and_printer;

    GetMaliciousTimeRefAndTimeServer (AS_nodes, config, time_reference_types, time_server_types);
    GetTimeServiceSnapShotTypes(AS_nodes, config, snapshot_types, global_scheduler_and_printer);
    GetTimeServiceAlgVersions(AS_nodes, config, alg_versions, snapshot_types);

    bool only_propagation_delay = OnlyPropagationDelay(config);

    for (uint32_t i = 0; i < AS_nodes.GetN(); ++i) {
        ns3::SCION_AS *AS_node = dynamic_cast<ns3::SCION_AS *>(PeekPointer(AS_nodes.Get(i)));
        uint16_t alias_as_no = AS_node->as_number;
        assert(alias_as_no == i);
        uint16_t isd_number = AS_node->isd_number;
        bool parallel_scheduler = (alias_as_no == global_scheduler_and_printer);

        ns3::SCIONHost* time_server =
                new ns3::TimeServer(0, isd_number, alias_as_no, 2,
                                    0.0, 0.0, AS_node,
                                    parallel_scheduler,
                                    ns3::Time(config["time_service"]["max_initial_drift"].as<std::string>()),
                                    ns3::Time(config["time_service"]["max_drift_per_day"].as<std::string>()),
                                    config["time_service"]["jitter_in_drift"].as<uint32_t>(),
                                    config["time_service"]["max_drift_coefficient"].as<uint32_t>(),
                                    ns3::Time(config["time_service"]["global_cut_off"].as<std::string>()),
                                    ns3::Time(config["time_service"]["first_event"].as<std::string>()),
                                    ns3::Time(config["time_service"]["last_event"].as<std::string>()),
                                    ns3::Time(config["time_service"]["snapshot_period"].as<std::string>()),
                                    ns3::Time(config["time_service"]["list_of_ases_req_period"].as<std::string>()),
                                    ns3::Time(config["time_service"]["time_sync_period"].as<std::string>()),
                                    config["time_service"]["G"].as<uint32_t>(),
                                    config["time_service"]["number_of_paths_to_use_for_global_sync"].as<uint32_t>(),
                                    config["time_service"]["read_disjoint_paths"].as<std::string>(),
                                    config["time_service"]["time_service_output_path"].as<std::string>(),
                                    time_reference_types.at(alias_as_no),
                                    time_server_types.at(alias_as_no),
                                    snapshot_types.at(alias_as_no),
                                    alg_versions.at(alias_as_no),
                                    ns3::Time(config["time_service"]["malcious_response_minimum_offset"].as<std::string>()),
                                    config["time_service"]["path_selection"].as<std::string>());

        AS_node->AddHost(time_server);

        if (only_propagation_delay) {
            time_server->SetProcessingDelay(ns3::Time(0), ns3::Time(0));
        } else {
            time_server->SetProcessingDelay(ns3::NanoSeconds(10), ns3::PicoSeconds(200));
        }
    }
}

void InstantiateLinksFromTopo (rapidxml::xml_node<>* xml_root,
                               ns3::NodeContainer& AS_nodes, const std::map<int32_t, uint16_t>& real_to_alias_as_no,
                               const YAML::Node& config){

    bool only_propagation_delay = OnlyPropagationDelay(config);

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

        uint16_t to_alias_as_no = real_to_alias_as_no.at(to);
        uint16_t from_alias_as_no = real_to_alias_as_no.at(from);

        to_AS = ns3::DynamicCast<ns3::SCION_AS>(AS_nodes.Get(to_alias_as_no));
        from_AS = ns3::DynamicCast<ns3::SCION_AS>(AS_nodes.Get(from_alias_as_no));

        assert(to_AS->as_number == to_alias_as_no);
        assert(from_AS->as_number == from_alias_as_no);

        ns3::PointToPointHelper helper;
        helper.Install(from_AS, to_AS);

        to_AS->AddToRemoteASInfo(from_AS->GetNDevices() - 1, ns3::PeekPointer(from_AS));
        to_AS->interfaces_coordinates.push_back(std::pair<ns3::ld, ns3::ld>(latitude, longitude));

        from_AS->AddToRemoteASInfo(to_AS->GetNDevices() - 1, ns3::PeekPointer(to_AS));
        from_AS->interfaces_coordinates.push_back(std::pair<ns3::ld, ns3::ld>(latitude, longitude));

        if (config["border_router"]) {
            ns3::Time to_propagation_delay, from_propagation_delay;
            ns3::Time to_transmission_delay, from_transmission_delay;
            ns3::Time to_processing_delay, from_processing_delay;
            ns3::Time to_processing_throughput_delay, from_processing_throughput_delay;

            to_propagation_delay = ns3::NanoSeconds(5); // Assuming 1m fiber optic between neighboring devices in the same location
            from_propagation_delay = ns3::NanoSeconds(5);

            if (only_propagation_delay) {
                to_transmission_delay = ns3::Time(0);
                from_transmission_delay = ns3::Time(0);

                to_processing_delay = ns3::Time(0);
                from_processing_delay = ns3::Time(0);

                to_processing_throughput_delay = ns3::Time(0);
                from_processing_throughput_delay = ns3::Time(0);
            } else {
                to_transmission_delay = ns3::PicoSeconds(20);//Per byte transmission delay assuming 400 Gbps link
                from_transmission_delay = ns3::PicoSeconds(20);

                to_processing_delay = ns3::NanoSeconds(10);
                from_processing_delay = ns3::NanoSeconds(10);

                to_processing_throughput_delay = ns3::PicoSeconds(200); // 5 Giga packets per second
                from_processing_throughput_delay = ns3::PicoSeconds(200);
            }

            ns3::BorderRouter *to_br = to_AS->AddBR(latitude, longitude, to_processing_delay,
                                                    to_processing_throughput_delay);
            ns3::BorderRouter *from_br = from_AS->AddBR(latitude, longitude, from_processing_delay,
                                                        from_processing_throughput_delay);

            to_br->AddToPropagationDelays(to_propagation_delay);
            to_br->AddToTransmissionDelays(to_transmission_delay);

            from_br->AddToPropagationDelays(from_propagation_delay);
            from_br->AddToTransmissionDelays(from_transmission_delay);

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


void GetASesWithMaliciousBRs (const ns3::NodeContainer& AS_nodes,
                              const YAML::Node& config,
                              std::vector<std::string>& border_routers_malicious_action) {
    if (!config["border_router"]) {
        return;
    }

    if (!config["border_router"]["percent_of_ASes_with_malicious_br"]) {
        return;
    }

    std::vector<uint16_t> indices;
    for (uint32_t i = 0; i < AS_nodes.GetN(); ++i) {
        indices.push_back(i);
    }

    if (config["border_router"]["truly_random_malicious"].as<uint16_t>() == 1) {
        std::random_shuffle(indices.begin(), indices.end(), ns3::truly_random_generator);
    } else {
        std::random_shuffle(indices.begin(), indices.end(), ns3::random_generator);
    }

    border_routers_malicious_action.resize(AS_nodes.GetN());

    uint16_t number_of_ASes_with_malicious_br = (uint16_t) std::floor(
            ((double ) config["border_router"]["percent_of_ASes_with_malicious_br"].as<uint16_t>() * (double ) AS_nodes.GetN()) / 100.0);


    for (uint16_t i = 0; i < number_of_ASes_with_malicious_br; ++i) {
        border_routers_malicious_action.at(indices.at(i)) = config["border_router"]["malicious_action"].as<std::string>();
    }

    for (uint16_t i = number_of_ASes_with_malicious_br; i < AS_nodes.GetN(); ++i) {
        border_routers_malicious_action.at(indices.at(i)) = "no";
    }

}


void InitializeASesAttributes(const ns3::NodeContainer& AS_nodes, std::map<int32_t, uint16_t>& real_to_alias_as_no, const YAML::Node& config ) {

    bool only_propagation_delay = OnlyPropagationDelay(config);

    if (config["border_router"]) {
        std::vector<std::string> border_routers_malicious_action;
        GetASesWithMaliciousBRs (AS_nodes, config, border_routers_malicious_action);
        ns3::Time malicious_delay = ns3::TimeStep(0);
        std::string malicious_action = config["border_router"]["malicious_action"].as<std::string>();
        if ((malicious_action == "symmetric_delay" || malicious_action == "asymmetric_delay") && !only_propagation_delay){
            malicious_delay = ns3::Time(config["border_router"]["delay"].as<std::string>());
        }

        for (uint64_t i = 0; i < AS_nodes.GetN(); ++i) {
            ns3::SCION_AS *AS_node = dynamic_cast<ns3::SCION_AS *>(PeekPointer(AS_nodes.Get(i)));
            AS_node->DoInitializations(AS_nodes.GetN(), only_propagation_delay,
                                       border_routers_malicious_action.at(i), malicious_delay);
        }
    } else {
        for (uint64_t i = 0; i < AS_nodes.GetN(); ++i) {
            ns3::SCION_AS *AS_node = dynamic_cast<ns3::SCION_AS *>(PeekPointer(AS_nodes.Get(i)));
            AS_node->DoInitializations(AS_nodes.GetN());
        }
    }

    if (config["beacon_service"]["policy"].as<std::string>() == "green_beaconing") {
        ns3::ReadBr2BrEnergy(AS_nodes, real_to_alias_as_no, config);
    }
}


bool OnlyPropagationDelay(const YAML::Node& config) {
    if (config["only_propagation_delay"] && config["only_propagation_delay"].as<int32_t>() != 0) {
        return true;
    }

    return false;
}
