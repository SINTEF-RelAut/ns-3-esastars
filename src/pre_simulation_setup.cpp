//
// Created by Seyedali Tabaeiaghdaei on 24.03.22.
//

#include "src/SCION/headers/pre_simulation_setup.h"

namespace ns3 {
void
SetTimeResolution (const std::string &timeResStr)
{
  if (timeResStr == "FS")
    {
      Time::SetResolution (Time::FS);
    }
  else if (timeResStr == "PS")
    {
      Time::SetResolution (Time::PS);
    }
  else if (timeResStr == "NS")
    {
      Time::SetResolution (Time::NS);
    }
  else if (timeResStr == "US")
    {
      Time::SetResolution (Time::US);
    }
  else if (timeResStr == "MS")
    {
      Time::SetResolution (Time::MS);
    }
  else if (timeResStr == "S")
    {
      Time::SetResolution (Time::S);
    }
  else if (timeResStr == "MIN")
    {
      Time::SetResolution (Time::MIN);
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

void
InstantiateASesFromTopo (rapidxml::xml_node<> *xmlRoot,
                         std::map<int32_t, uint16_t> &realToAliasAsNo,
                         std::map<uint16_t, int32_t> &aliasToRealAsNo, NodeContainer &asNodes,
                         const YAML::Node &config)
{
  uint16_t aliasAsNo = 0;
  rapidxml::xml_node<> *curXmlNode = xmlRoot->first_node ("node");

  while (curXmlNode)
    {
      PropertyContainer p = ParseProperties (curXmlNode);
      std::string type;

      if (p.HasProperty ("type"))
        {
          type = p.GetProperty ("type");
        }
      else
        {
          type = "core";
        }

      bool maliciousBorderRouters = false;
      if (config["border_router"] && config["border_router"]["malicious_action"])
        {
          assert (p.HasProperty ("malicious"));
          if (p.GetProperty ("malicious") == "True")
            {
              maliciousBorderRouters = true;
            }
        }

      Ptr<ScionAs> asNode;
      if (type == "core")
        {
          asNode = CreateObject<ScionCoreAs> (0, (aliasAsNo == 0), aliasAsNo, curXmlNode,
                                                 config,
                                              maliciousBorderRouters, Time (0));
        }
      else if (type == "non-core")
        {
          asNode = CreateObject<ScionAs> (0, (aliasAsNo == 0), aliasAsNo, curXmlNode,
                                            config,
                                          maliciousBorderRouters, Time (0));
        }
      else
        {
          std::cerr << "Incompatible AS_node type!" << std::endl;
          exit (1);
        }
      asNodes.Add (asNode);

      int32_t realAsNo = std::stoi (GetAttribute (curXmlNode, "id"));
      uint16_t isdNumber = 0;
      if (p.HasProperty ("isd"))
        {
          isdNumber = std::stoi (p.GetProperty ("isd"));
        }

      g_asToIsdMap.insert (std::make_pair (aliasAsNo, isdNumber));

      realToAliasAsNo.insert (std::make_pair (realAsNo, aliasAsNo));
      aliasToRealAsNo.insert (std::make_pair (aliasAsNo, realAsNo));

      aliasAsNo++;

      curXmlNode = curXmlNode->next_sibling ("node");
    }
}

void
InstantiatePathServers (const YAML::Node &config, const NodeContainer &asNodes)
{
  bool onlyPropagationDelay = OnlyPropagationDelay (config);

  for (uint32_t i = 0; i < asNodes.GetN (); ++i)
    {
      ScionAs *asNode = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));
      PathServer *pathServer =
          new PathServer (0, asNode->isdNumber, asNode->asNumber, 1, 0.0, 0.0, asNode);
      asNode->SetPathServer (pathServer);

      if (onlyPropagationDelay)
        {
          pathServer->SetProcessingDelay (Time (0), Time (0));
        }
      else
        {
          pathServer->SetProcessingDelay (NanoSeconds (10), PicoSeconds (200));
        }
    }
}

void
GetMaliciousTimeRefAndTimeServer (const NodeContainer &asNodes, const YAML::Node &config,
                                  std::vector<std::string> &timeReferenceTypes,
                                  std::vector<std::string> &timeServerTypes)
{
  std::vector<uint16_t> indicesTimeReferences;
  std::vector<uint16_t> indicesTimeServers;

  uint16_t numberOfASes = asNodes.GetN ();

  for (uint32_t i = 0; i < numberOfASes; ++i)
    {
      indicesTimeReferences.push_back (i);
      indicesTimeServers.push_back (i);
    }

  timeReferenceTypes.resize (numberOfASes);
  timeServerTypes.resize (numberOfASes);

  if (config["time_service"]["truly_random_malicious"].as<uint16_t> () == 1)
    {
      std::shuffle (indicesTimeReferences.begin (), indicesTimeReferences.end (),
                    std::random_device{});
      std::shuffle (indicesTimeServers.begin (), indicesTimeServers.end (),
                    std::random_device{});
    }
  else
    {
      std::shuffle (indicesTimeReferences.begin (), indicesTimeReferences.end (),
                    std::mt19937{});
      std::shuffle (indicesTimeServers.begin (), indicesTimeServers.end (), std::mt19937{});
    }

  uint16_t numberOfMaliciousTimeReferences = (uint16_t) std::floor (
      ((double) config["time_service"]["percent_of_malicious_time_references"].as<uint16_t> () *
       (double) numberOfASes) /
      100.0);

  uint16_t numberOfMaliciousTimeServers = (uint16_t) std::floor (
      ((double) config["time_service"]["percent_of_malicious_time_servers"].as<uint16_t> () *
       (double) numberOfASes) /
      100.0);

  if (config["time_service"]["reference_clk"].as<std::string> () == "OFF")
    {
      for (uint16_t i = 0; i < numberOfASes; ++i)
        {
          timeReferenceTypes.at (i) = "OFF";
        }
    }
  else
    {
      for (uint16_t i = 0; i < numberOfMaliciousTimeReferences; ++i)
        {
          timeReferenceTypes.at (indicesTimeReferences.at (i)) = "MALICIOUS";
        }

      for (uint16_t i = numberOfMaliciousTimeReferences; i < numberOfASes; ++i)
        {
          timeReferenceTypes.at (indicesTimeReferences.at (i)) = "ON";
        }
    }

  for (uint16_t i = 0; i < numberOfMaliciousTimeServers; ++i)
    {
      timeServerTypes.at (indicesTimeServers.at (i)) = "MALICIOUS";
    }

  for (uint16_t i = numberOfMaliciousTimeServers; i < numberOfASes; ++i)
    {
      timeServerTypes.at (indicesTimeServers.at (i)) = "NORMAL";
    }
}

void
GetTimeServiceSnapShotTypes (const NodeContainer &asNodes, const YAML::Node &config,
                             std::vector<std::string> &snapshotTypes,
                             uint16_t &globalSchedulerAndPrinter)
{
  globalSchedulerAndPrinter = 0;
  if (config["time_service"]["snapshot_type"].as<std::string> () == "PRINT_OFFSET_DIFF")
    {
      std::random_device rd;
      std::uniform_int_distribution<uint16_t> dist (0, asNodes.GetN () - 1);
      globalSchedulerAndPrinter = dist (rd);
    }
  for (uint32_t i = 0; i < asNodes.GetN (); ++i)
    {
      if (config["time_service"]["snapshot_type"].as<std::string> () == "PRINT_OFFSET_DIFF")
        {
          if (i == globalSchedulerAndPrinter)
            {
              snapshotTypes.push_back ("PRINT_OFFSET_DIFF");
            }
          else
            {
              snapshotTypes.push_back ("OFF");
            }
        }
      else
        {
          snapshotTypes.push_back (config["time_service"]["snapshot_type"].as<std::string> ());
        }
    }
}

void
GetTimeServiceAlgVersions (const NodeContainer &asNodes, const YAML::Node &config,
                           std::vector<std::string> &algVersions,
                           const std::vector<std::string> &snapshotTypes)
{
  for (uint32_t i = 0; i < asNodes.GetN (); ++i)
    {
      if (snapshotTypes.at (i) == "OFF")
        {
          algVersions.push_back (
              config["time_service"]["alg_version_non_printing_instances"].as<std::string> ());
        }
      else
        {
          algVersions.push_back (
              config["time_service"]["alg_version_printing_instances"].as<std::string> ());
        }
    }
}

void
InstantiateTimeServers (const YAML::Node &config, const NodeContainer &asNodes)
{
  std::vector<std::string> timeReferenceTypes;
  std::vector<std::string> timeServerTypes;
  std::vector<std::string> snapshotTypes;
  std::vector<std::string> algVersions;
  uint16_t globalSchedulerAndPrinter;

  GetMaliciousTimeRefAndTimeServer (asNodes, config, timeReferenceTypes, timeServerTypes);
  GetTimeServiceSnapShotTypes (asNodes, config, snapshotTypes, globalSchedulerAndPrinter);
  GetTimeServiceAlgVersions (asNodes, config, algVersions, snapshotTypes);

  bool onlyPropagationDelay = OnlyPropagationDelay (config);

  for (uint32_t i = 0; i < asNodes.GetN (); ++i)
    {
      ScionAs *asNode = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));
      uint16_t aliasAsNo = asNode->asNumber;
      assert (aliasAsNo == i);
      uint16_t isdNumber = asNode->isdNumber;
      bool parallelScheduler = (aliasAsNo == globalSchedulerAndPrinter);

      ScionHost *timeServer = new TimeServer (
          0, isdNumber, aliasAsNo, 2, 0.0, 0.0, asNode, parallelScheduler,
          Time (config["time_service"]["max_initial_drift"].as<std::string> ()),
          Time (config["time_service"]["max_drift_per_day"].as<std::string> ()),
          config["time_service"]["jitter_in_drift"].as<uint32_t> (),
          config["time_service"]["max_drift_coefficient"].as<uint32_t> (),
          Time (config["time_service"]["global_cut_off"].as<std::string> ()),
          Time (config["time_service"]["first_event"].as<std::string> ()),
          Time (config["time_service"]["last_event"].as<std::string> ()),
          Time (config["time_service"]["snapshot_period"].as<std::string> ()),
          Time (config["time_service"]["list_of_ases_req_period"].as<std::string> ()),
          Time (config["time_service"]["time_sync_period"].as<std::string> ()),
          config["time_service"]["G"].as<uint32_t> (),
          config["time_service"]["number_of_paths_to_use_for_global_sync"].as<uint32_t> (),
          config["time_service"]["read_disjoint_paths"].as<std::string> (),
          config["time_service"]["time_service_output_path"].as<std::string> (),
          timeReferenceTypes.at (aliasAsNo), timeServerTypes.at (aliasAsNo),
          snapshotTypes.at (aliasAsNo), algVersions.at (aliasAsNo),
          Time (config["time_service"]["malcious_response_minimum_offset"].as<std::string> ()),
          config["time_service"]["path_selection"].as<std::string> ());

      asNode->AddHost (timeServer);

      if (onlyPropagationDelay)
        {
          timeServer->SetProcessingDelay (Time (0), Time (0));
        }
      else
        {
          timeServer->SetProcessingDelay (NanoSeconds (10), PicoSeconds (200));
        }
    }
}

void
InstantiateLinksFromTopo (rapidxml::xml_node<> *xmlRoot, NodeContainer &asNodes,
                          const std::map<int32_t, uint16_t> &realToAliasAsNo,
                          const YAML::Node &config)
{
  bool onlyPropagationDelay = OnlyPropagationDelay (config);

  rapidxml::xml_node<> *currXmlNode = xmlRoot->first_node ("link");
  while (currXmlNode)
    {
      int32_t to = std::stoi (currXmlNode->first_node ("to")->value ());
      int32_t from = std::stoi (currXmlNode->first_node ("from")->value ());

      PropertyContainer p = ParseProperties (currXmlNode);

      Ld_t latitude = std::stod (p.GetProperty ("latitude"));
      Ld_t longitude = std::stod (p.GetProperty ("longitude"));
      int32_t bwd = std::stoi (p.GetProperty ("capacity"));
      std::string rel = "core"; //p.GetProperty("rel");
      NeighbourRelation relation;

      // Check for the 3 possibilities in CAIDA topology
      if (rel == "peer")
        {
          relation = NeighbourRelation::peer;
        }
      else if (rel == "core")
        {
          relation = NeighbourRelation::core;
        }
      else if (rel == "customer")
        {
          relation = NeighbourRelation::customer;
        }
      else
        {
          relation = NeighbourRelation::core;
        }

      Ptr<ScionAs> fromAs;
      Ptr<ScionAs> toAs;

      uint16_t toAliasAsNo = realToAliasAsNo.at (to);
      uint16_t fromAliasAsNo = realToAliasAsNo.at (from);

      toAs = DynamicCast<ScionAs> (asNodes.Get (toAliasAsNo));
      fromAs = DynamicCast<ScionAs> (asNodes.Get (fromAliasAsNo));

      assert (toAs->asNumber == toAliasAsNo);
      assert (fromAs->asNumber == fromAliasAsNo);

      PointToPointHelper helper;
      helper.Install (fromAs, toAs);

      toAs->AddToRemoteAsInfo (fromAs->GetNDevices () - 1, PeekPointer (fromAs));
      toAs->interfacesCoordinates.push_back (std::pair<Ld_t, Ld_t> (latitude, longitude));
      toAs->coordinatesToInterfaces.insert (std::make_pair (
          std::pair<Ld_t, Ld_t> (latitude, longitude), toAs->interfacesCoordinates.size () - 1));

      if (p.HasProperty ("to_if_id"))
        {
          assert ((uint32_t) std::stoi (p.GetProperty ("to_if_id")) == toAs->GetNDevices () - 1);
        }

      fromAs->AddToRemoteAsInfo (toAs->GetNDevices () - 1, PeekPointer (toAs));
      fromAs->interfacesCoordinates.push_back (std::pair<Ld_t, Ld_t> (latitude, longitude));
      fromAs->coordinatesToInterfaces.insert (std::make_pair (
          std::pair<Ld_t, Ld_t> (latitude, longitude), fromAs->interfacesCoordinates.size () - 1));

      if (p.HasProperty ("from_if_id"))
        {
          assert ((uint32_t) std::stoi (p.GetProperty ("from_if_id")) ==
                  fromAs->GetNDevices () - 1);
        }

      if (config["border_router"])
        {
          Time toPropagationDelay, fromPropagationDelay;
          Time toTransmissionDelay, fromTransmissionDelay;
          Time toProcessingDelay, fromProcessingDelay;
          Time toProcessingThroughputDelay, fromProcessingThroughputDelay;

          toPropagationDelay = NanoSeconds (
              5); // Assuming 1m fiber optic between neighboring devices in the same location
          fromPropagationDelay = NanoSeconds (5);

          if (onlyPropagationDelay)
            {
              toTransmissionDelay = Time (0);
              fromTransmissionDelay = Time (0);

              toProcessingDelay = Time (0);
              fromProcessingDelay = Time (0);

              toProcessingThroughputDelay = Time (0);
              fromProcessingThroughputDelay = Time (0);
            }
          else
            {
              toTransmissionDelay =
                  PicoSeconds (20); //Per byte transmission delay assuming 400 Gbps link
              fromTransmissionDelay = PicoSeconds (20);

              toProcessingDelay = NanoSeconds (10);
              fromProcessingDelay = NanoSeconds (10);

              toProcessingThroughputDelay = PicoSeconds (200); // 5 Giga packets per second
              fromProcessingThroughputDelay = PicoSeconds (200);
            }

          BorderRouter *toBr = toAs->AddBr (latitude, longitude, toProcessingDelay, toProcessingThroughputDelay);
          BorderRouter *fromBr = fromAs->AddBr (latitude, longitude, fromProcessingDelay,
                                                 fromProcessingThroughputDelay);

          toBr->AddToPropagationDelays (toPropagationDelay);
          toBr->AddToTransmissionDelays (toTransmissionDelay);

          fromBr->AddToPropagationDelays (fromPropagationDelay);
          fromBr->AddToTransmissionDelays (fromTransmissionDelay);

          toBr->AddToIfForwadingTable (toAs->GetNDevices () - 1, toBr->GetNDevices () - 1);
          fromBr->AddToIfForwadingTable (fromAs->GetNDevices () - 1, fromBr->GetNDevices () - 1);

          toBr->AddToRemoteNodesInfo (fromBr, fromBr->GetNDevices () - 1, fromAs->isdNumber,
                                       fromAs->asNumber);
          fromBr->AddToRemoteNodesInfo (toBr, toBr->GetNDevices () - 1, toAs->isdNumber,
                                         toAs->asNumber);
        }

      toAs->interAsBwds.push_back (bwd);
      fromAs->interAsBwds.push_back (bwd);

      NeighbourRelation toRel;
      NeighbourRelation fromRel;

      switch (relation)
        {
        case NeighbourRelation::peer:
          toRel = NeighbourRelation::peer;
          fromRel = NeighbourRelation::peer;
          break;
        case NeighbourRelation::core:
          toRel = NeighbourRelation::core;
          fromRel = NeighbourRelation::core;
          break;
        case NeighbourRelation::customer:
          toRel = NeighbourRelation::provider;
          fromRel = NeighbourRelation::customer;
          break;
        case NeighbourRelation::provider:
          // Should never happen, there is no "Provider" type in xml files
          toRel = NeighbourRelation::customer;
          fromRel = NeighbourRelation::provider;
          assert (false);
        }

      toAs->interfaceToNeighborMap.insert (
          std::make_pair (toAs->GetNDevices () - 1, fromAs->asNumber));
      if (toAs->interfacesPerNeighborAs.find (fromAs->asNumber) !=
          toAs->interfacesPerNeighborAs.end ())
        {
          toAs->interfacesPerNeighborAs.at (fromAs->asNumber)
              .push_back ((uint16_t) toAs->GetNDevices () - 1);
        }
      else
        {
          std::vector<uint16_t> tmp;
          tmp.push_back ((uint16_t) toAs->GetNDevices () - 1);
          toAs->interfacesPerNeighborAs.insert (std::make_pair (fromAs->asNumber, tmp));
          toAs->neighbors.push_back (std::make_pair (fromAs->asNumber, toRel));
        }

      fromAs->interfaceToNeighborMap.insert (
          std::make_pair (fromAs->GetNDevices () - 1, toAs->asNumber));
      if (fromAs->interfacesPerNeighborAs.find (toAs->asNumber) !=
          fromAs->interfacesPerNeighborAs.end ())
        {
          fromAs->interfacesPerNeighborAs.at (toAs->asNumber)
              .push_back (fromAs->GetNDevices () - 1);
        }
      else
        {
          std::vector<uint16_t> tmp;
          tmp.push_back ((uint16_t) fromAs->GetNDevices () - 1);
          fromAs->interfacesPerNeighborAs.insert (std::make_pair (toAs->asNumber, tmp));
          fromAs->neighbors.push_back (std::make_pair (toAs->asNumber, fromRel));
        }

      toAs->GetBeaconServer ()->PerLinkInitializations (currXmlNode, config);
      fromAs->GetBeaconServer ()->PerLinkInitializations (currXmlNode, config);

      currXmlNode = currXmlNode->next_sibling ("link");
    }
}

void
InitializeASesAttributes (const NodeContainer &asNodes,
                          std::map<int32_t, uint16_t> &realToAliasAsNo,
                          rapidxml::xml_node<> *xmlNode, const YAML::Node &config)
{
  bool onlyPropagationDelay = OnlyPropagationDelay (config);

  if (config["border_router"])
    {
      for (uint64_t i = 0; i < asNodes.GetN (); ++i)
        {
          ScionAs *asNode = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));
          asNode->DoInitializations (asNodes.GetN (), xmlNode, config, onlyPropagationDelay);
        }
    }
  else
    {
      for (uint64_t i = 0; i < asNodes.GetN (); ++i)
        {
          ScionAs *asNode = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));
          asNode->DoInitializations (asNodes.GetN (), xmlNode, config);
        }
    }

  if (config["beacon_service"]["br_br_energy_file"])
    {
      ReadBr2BrEnergy (asNodes, realToAliasAsNo, config);
    }
}

bool
OnlyPropagationDelay (const YAML::Node &config)
{
  if (config["only_propagation_delay"] && config["only_propagation_delay"].as<int32_t> () != 0)
    {
      return true;
    }

  return false;
}
} // namespace ns3