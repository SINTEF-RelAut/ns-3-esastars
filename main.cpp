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

int
main (int argc, char *argv[])
{
  if (argc != 2)
    {
      std::cerr << "Please pass the config file location as the argument." << std::endl;
      return 1;
    }

  YAML::Node config = YAML::LoadFile (std::string (argv[1]));

  if (!config["time_resolution"])
    {
      std::cerr << "Please specify simulator's time resolution." << std::endl;
      return 1;
    }

  if (!config["topology"])
    {
      std::cerr << "No topology file specified in the config file." << std::endl;
      return 1;
    }

  if (!config["output"])
    {
      std::cerr << "Please specify output file's path." << std::endl;
      return 1;
    }

  if (!config["simulation_duration"])
    {
      std::cerr << "Simulation duration is not specified in the config file." << std::endl;
      return 1;
    }

  if (!config["g_numCore"])
    {
      std::cerr << "Please Specify number of cores to use." << std::endl;
      return 1;
    }

  if (!config["beacon_service"] && !(config["path_service"]) && !(config["border_router"]))
    {
      std::cerr << "No simulation is possible." << std::endl;
      return 1;
    }

  SetTimeResolution (config["time_resolution"].as<std::string> ());

  std::string topologyFile = config["topology"].as<std::string> ();

  std::string outPath = config["output"].as<std::string> ();

  Time simulationEndTime = Time (config["simulation_duration"].as<std::string> ());

  g_numCore = config["NUM_CORE"].as<uint32_t> ();

  std::ifstream fin (topologyFile.c_str ());
  std::ostringstream sstr;
  sstr << fin.rdbuf ();

  sstr.flush ();
  fin.close ();

  std::string xmlData = sstr.str ();
  rapidxml::xml_document<> doc;
  doc.parse<0> (&xmlData[0]);

  rapidxml::xml_node<> *xmlRoot = doc.first_node ("topology");

  if (!xmlRoot)
    {
      std::cerr << "Empty topology!" << std::endl;
      exit (1);
    }

  std::ofstream out (outPath);
  std::cout.rdbuf (out.rdbuf ());

  InstantiateASesFromTopo (xmlRoot, g_realToAliasAsNo, g_aliasToRealAsNo, g_nodes, config);

  if (config["path_service"])
    {
      InstantiatePathServers (config, g_nodes);
    }

  if (config["time_service"])
    {
      InstantiateTimeServers (config, g_nodes);
    }

  InstantiateLinksFromTopo (xmlRoot, g_nodes, g_realToAliasAsNo, config);
  InitializeASesAttributes (g_nodes, g_realToAliasAsNo, xmlRoot, config);

  SchedulePeriodicEvents (config);
  UserDefinedEvents userDefinedEvents (config, g_nodes, g_realToAliasAsNo, g_aliasToRealAsNo);

  Simulator::Stop (simulationEndTime);
  Simulator::Run ();

  PostSimulationEvaluations *eval =
      new PostSimulationEvaluations (config, g_nodes, g_realToAliasAsNo, g_aliasToRealAsNo);

  eval->DoFinalEvaluations ();

  Simulator::Destroy ();

  return 0;
}