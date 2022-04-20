/**
 * @file main.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 */

#include <istream>
#include <omp.h>
#include <set>
#include <yaml-cpp/yaml.h>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/nstime.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/ptr.h"

#include "src/SCION/headers/pre_simulation_setup.h"

#include "src/SCION/headers/externs.h"
#include "src/SCION/headers/post_simulation_evaluations.h"
#include "src/SCION/headers/schedule_periodic_events.h"
#include "src/SCION/headers/user_defined_events.h"
#include "src/SCION/headers/utils.h"

using namespace ns3;

int main(int argc, char *argv[]) {
    std::map<int32_t, uint16_t> real_to_alias_as_no;
    std::map<uint16_t, int32_t> alias_to_real_as_no;

    if (argc != 2) {
        std::cerr << "Please pass the config file location as the argument." << std::endl;
        return 1;
    }

    YAML::Node config = YAML::LoadFile(std::string(argv[1]));

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

    if (!config["beacon_service"] && !(config["path_service"]) && !(config["border_router"])) {
        std::cerr << "No simulation is possible." << std::endl;
        return 1;
    }

    SetTimeResolution(config["time_resolution"].as<std::string>());

    std::string topology_file = config["topology"].as<std::string>();

    std::string out_path = config["output"].as<std::string>();

    Time simulation_end_time = Time(config["simulation_duration"].as<std::string>());

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

    if (config["time_service"]) {
        InstantiateTimeServers(config, nodes);
    }

    InstantiateLinksFromTopo(xml_root, nodes, real_to_alias_as_no, config);
    InitializeASesAttributes(nodes, real_to_alias_as_no, xml_root, config);

    SchedulePeriodicEvents(config);
    UserDefinedEvents user_defined_events(config, nodes, real_to_alias_as_no, alias_to_real_as_no);

    Simulator::Stop(simulation_end_time);
    Simulator::Run();

    PostSimulationEvaluations *eval =
            new PostSimulationEvaluations(config, nodes, real_to_alias_as_no, alias_to_real_as_no);

    eval->DoFinalEvaluations();

    Simulator::Destroy();

    return 0;
}