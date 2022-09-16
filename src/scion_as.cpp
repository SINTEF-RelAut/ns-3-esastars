/**
 * @file scion_as.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see scion_as.h
 *
 * @brief Implements the specialized functions on the scion leaf ASes.
 */

#include "ns3/core-module.h"
#include <random>

#include "src/SCION/headers/beaconing/baseline.h"
#include "src/SCION/headers/beaconing/diversity_age_based.h"
#include "src/SCION/headers/beaconing/green_beaconing.h"
#include "src/SCION/headers/beaconing/latency_optimized_beaconing.h"
#include "src/SCION/headers/beaconing/on_demand_optimization.h"
#include "src/SCION/headers/beaconing/scionlab_algo.h"
#include "src/SCION/headers/beaconing/beacon_server.h"
#include "src/SCION/headers/path_server.h"
#include "src/SCION/headers/scion_as.h"
#include "src/SCION/headers/scion_host.h"
#include "src/SCION/headers/utils.h"

namespace ns3 {

void
ScionAs::DoInitializations (uint32_t num_ASes, rapidxml::xml_node<> *xml_node,
                             const YAML::Node &config)
{
  InitializeLatencies (true);

  AS_max_bwd = 0;
  for (auto const curr_bwd : inter_as_bwds)
    {
      if (curr_bwd > AS_max_bwd)
        {
          AS_max_bwd = curr_bwd;
        }
    }

  beacon_server->DoInitializations (num_ASes, xml_node, config);
}

void
ScionAs::DoInitializations (uint32_t num_ASes, rapidxml::xml_node<> *xml_node,
                             const YAML::Node &config, bool only_propagation_delay)
{
  ConnectInternalNodes (only_propagation_delay);
  InitializeLatencies (only_propagation_delay);

  for (auto const &br : border_routers)
    {
      br->InitializeTransmissionQueues ();
    }

  for (auto const &host : hosts)
    {
      host->InitializeTransmissionQueues ();
    }

  path_server->InitializeTransmissionQueues ();

  AS_max_bwd = 0;
  for (auto const curr_bwd : inter_as_bwds)
    {
      if (curr_bwd > AS_max_bwd)
        {
          AS_max_bwd = curr_bwd;
        }
    }

  beacon_server->DoInitializations (num_ASes, xml_node, config);
}

std::pair<uint16_t, ScionAs *>
ScionAs::GetRemoteAsInfo (uint16_t egress_interface_no)
{
  return remote_as_info.at (egress_interface_no);
}

void
ScionAs::ReceiveBeacon (Beacon &received_beacon, uint16_t sender_as, uint16_t remote_if,
                         uint16_t local_if)
{
  beacon_server->ReceiveBeacon (received_beacon, sender_as, remote_if, local_if);
}

void
ScionAs::SetBeaconServer (BeaconServer *the_beaconServer)
{
  this->beacon_server = the_beaconServer;
}

void
ScionAs::AdvanceTime (ns3::Time advance)
{
  local_time += advance;
}

BeaconServer *
ScionAs::GetBeaconServer ()
{
  return this->beacon_server;
}

PathServer *
ScionAs::GetPathServer ()
{
  return this->path_server;
}

void
ScionAs::SetPathServer (PathServer *the_pathServer)
{
  this->path_server = the_pathServer;
}

ScionCapableNode *
ScionAs::GetHost (host_addr_t host_addr)
{
  if (host_addr == 1)
    {
      return GetPathServer ();
    }

  return ((ScionCapableNode *) hosts.at (host_addr - 2));
}

uint32_t
ScionAs::GetNHosts ()
{
  return hosts.size ();
}

void
ScionAs::AddHost (ScionHost *host)
{
  hosts.push_back (host);
}

BorderRouter *
ScionAs::AddBr (double latitude, double longitude, Time processing_delay,
                 Time processing_throughput_delay)
{
  BorderRouter *the_br = new BorderRouter (0, isd_number, as_number, 0, latitude, longitude, this);

  the_br->SetProcessingDelay (processing_delay, processing_throughput_delay);

  border_routers.push_back (the_br);

  return the_br;
}

void
ScionAs::ConnectInternalNodes (bool only_propagation_delay)
{
  Time malicious_delay = TimeStep (0);
  if (malicious_border_routers &&
      (border_routers_malicious_action == "symmetric_delay" ||
       border_routers_malicious_action == "asymmetric_delay") &&
      !only_propagation_delay)
    {
      std::random_device rd;
      std::uniform_int_distribution<uint64_t> dist (
          50000, 300000); // random asymmetry between 50ms and 300ms
      uint64_t random_delay = dist (rd);
      malicious_delay = MicroSeconds (random_delay);
    }

  std::map<BorderRouter *, std::set<uint16_t>> border_router_to_if;

  for (uint16_t i = 0; i < GetNDevices (); ++i)
    {
      BorderRouter *br = border_routers.at (i);
      if (border_router_to_if.find (br) == border_router_to_if.end ())
        {
          border_router_to_if.insert (std::make_pair (br, std::set<uint16_t> ()));
        }
      border_router_to_if.at (br).insert (i);
    }

  std::set<BorderRouter *> border_routers_set (border_routers.begin (), border_routers.end ());
  std::vector<BorderRouter *> border_routers_vec (border_routers_set.begin (),
                                                  border_routers_set.end ());

  // Connect border routers to border routers
  for (uint32_t i = 0; i < border_routers_vec.size () - 1; ++i)
    {
      BorderRouter *br1 = border_routers_vec.at (i);
      for (uint32_t j = i + 1; j < border_routers_vec.size (); ++j)
        {
          BorderRouter *br2 = border_routers_vec.at (j);

          Time propagation_delay1 = NanoSeconds ((int64_t) floor (
              1e6 *
              calculate_great_circle_latency ((ld) br1->GetLatitude (), (ld) br1->GetLogitude (),
                                              (ld) br2->GetLatitude (), (ld) br2->GetLogitude ())));
          Time propagation_delay2 = propagation_delay1;

          if (border_routers_malicious_action == "symmetric_delay")
            {
              propagation_delay1 += malicious_delay;
              propagation_delay2 += malicious_delay;
            }
          else if (border_routers_malicious_action == "asymmetric_delay")
            {
              propagation_delay1 += malicious_delay;
            }

          br1->AddToPropagationDelays (propagation_delay1);
          br2->AddToPropagationDelays (propagation_delay2);

          if (only_propagation_delay)
            {
              br1->AddToTransmissionDelays (
                  Time (0)); // transmission delay for one byte assuming 400 Gbps link
              br2->AddToTransmissionDelays (Time (0));
            }
          else
            {
              br1->AddToTransmissionDelays (
                  PicoSeconds (20)); // transmission delay for one byte assuming 400 Gbps link
              br2->AddToTransmissionDelays (PicoSeconds (20));
            }

          br1->AddToRemoteNodesInfo (br2, br2->GetNDevices () - 1, isd_number, as_number);
          br2->AddToRemoteNodesInfo (br1, br1->GetNDevices () - 1, isd_number, as_number);

          for (uint16_t as_if : border_router_to_if.at (br1))
            {
              br2->AddToIfForwadingTable (as_if, br2->GetNDevices () - 1);
            }

          for (uint16_t as_if : border_router_to_if.at (br2))
            {
              br1->AddToIfForwadingTable (as_if, br1->GetNDevices () - 1);
            }
        }
    }

  // Connect border routers to hosts
  for (uint32_t i = 0; i < border_routers_vec.size (); ++i)
    {
      BorderRouter *br = border_routers_vec.at (i);
      for (uint32_t j = 0; j < hosts.size (); ++j)
        {
          ScionHost *host = hosts.at (j);

          Time propagation_delay = NanoSeconds (
              (int64_t) floor (1e6 * calculate_great_circle_latency (
                                         (ld) br->GetLatitude (), (ld) br->GetLogitude (),
                                         (ld) host->GetLatitude (), (ld) host->GetLogitude ())));

          br->AddToPropagationDelays (propagation_delay);
          host->AddToPropagationDelays (propagation_delay);

          if (only_propagation_delay)
            {
              br->AddToTransmissionDelays (
                  Time (0)); // transmission delay for one byte assuming 1 Gbps link
              host->AddToTransmissionDelays (Time (0));
            }
          else
            {
              br->AddToTransmissionDelays (
                  NanoSeconds (8)); // transmission delay for one byte assuming 1 Gbps link
              host->AddToTransmissionDelays (NanoSeconds (8));
            }

          br->AddToRemoteNodesInfo (host, host->GetNDevices () - 1, isd_number, as_number);
          host->AddToRemoteNodesInfo (br, br->GetNDevices () - 1, isd_number, as_number);

          for (uint16_t as_if : border_router_to_if.at (br))
            {
              host->AddToIfForwadingTable (as_if, host->GetNDevices () - 1);
            }

          br->AddToAddressForwardingTable (host->GetLocalAddress (), br->GetNDevices () - 1);
        }
    }

  // Connect border routers to path server
  for (uint32_t i = 0; i < border_routers_vec.size (); ++i)
    {
      BorderRouter *br = border_routers_vec.at (i);

      Time propagation_delay = NanoSeconds ((int64_t) floor (
          1e6 * calculate_great_circle_latency ((ld) br->GetLatitude (), (ld) br->GetLogitude (),
                                                (ld) path_server->GetLatitude (),
                                                (ld) path_server->GetLogitude ())));

      br->AddToPropagationDelays (propagation_delay);
      path_server->AddToPropagationDelays (propagation_delay);

      if (only_propagation_delay)
        {
          br->AddToTransmissionDelays (
              Time (0)); // transmission delay for one byte assuming 10 Gbps link
          path_server->AddToTransmissionDelays (Time (0));
        }
      else
        {
          br->AddToTransmissionDelays (
              PicoSeconds (800)); // transmission delay for one byte assuming 10 Gbps link
          path_server->AddToTransmissionDelays (PicoSeconds (800));
        }

      br->AddToRemoteNodesInfo (path_server, path_server->GetNDevices () - 1, isd_number,
                                as_number);
      path_server->AddToRemoteNodesInfo (br, br->GetNDevices () - 1, isd_number, as_number);

      for (uint16_t as_if : border_router_to_if.at (br))
        {
          path_server->AddToIfForwadingTable (as_if, path_server->GetNDevices () - 1);
        }

      br->AddToAddressForwardingTable (path_server->GetLocalAddress (), br->GetNDevices () - 1);
    }

  // Connect hosts to local path server
  for (uint32_t i = 0; i < hosts.size (); ++i)
    {
      ScionHost *host = hosts.at (i);

      Time propagation_delay = NanoSeconds ((int64_t) floor (
          1e6 * calculate_great_circle_latency (
                    (ld) host->GetLatitude (), (ld) host->GetLogitude (),
                    (ld) path_server->GetLatitude (), (ld) path_server->GetLogitude ())));

      host->AddToPropagationDelays (propagation_delay);
      path_server->AddToPropagationDelays (propagation_delay);

      if (only_propagation_delay)
        {
          host->AddToTransmissionDelays (
              Time (0)); // transmission delay for one byte assuming 1 Gbps link
          path_server->AddToTransmissionDelays (Time (0));
        }
      else
        {
          host->AddToTransmissionDelays (
              NanoSeconds (8)); // transmission delay for one byte assuming 1 Gbps link
          path_server->AddToTransmissionDelays (NanoSeconds (8));
        }

      host->AddToRemoteNodesInfo (path_server, path_server->GetNDevices () - 1, isd_number,
                                  as_number);
      path_server->AddToRemoteNodesInfo (host, host->GetNDevices () - 1, isd_number, as_number);

      host->AddToAddressForwardingTable (path_server->GetLocalAddress (), host->GetNDevices () - 1);
      path_server->AddToAddressForwardingTable (host->GetLocalAddress (),
                                                path_server->GetNDevices () - 1);
    }

  // Connect hosts to each other
  for (uint32_t i = 0; i < hosts.size () - 1; ++i)
    {
      ScionHost *h1 = hosts.at (i);
      for (uint32_t j = i + 1; j < hosts.size (); ++j)
        {
          ScionHost *h2 = hosts.at (j);

          Time propagation_delay = NanoSeconds ((int64_t) floor (
              1e6 *
              calculate_great_circle_latency ((ld) h1->GetLatitude (), (ld) h1->GetLogitude (),
                                              (ld) h2->GetLatitude (), (ld) h2->GetLogitude ())));

          h1->AddToPropagationDelays (propagation_delay);
          h2->AddToPropagationDelays (propagation_delay);

          if (only_propagation_delay)
            {
              h1->AddToTransmissionDelays (
                  Time (0)); // transmission delay for one byte assuming 1 Gbps link
              h2->AddToTransmissionDelays (Time (0));
            }
          else
            {
              h1->AddToTransmissionDelays (
                  NanoSeconds (8)); // transmission delay for one byte assuming 1 Gbps link
              h2->AddToTransmissionDelays (NanoSeconds (8));
            }

          h1->AddToRemoteNodesInfo (h2, h2->GetNDevices () - 1, isd_number, as_number);
          h2->AddToRemoteNodesInfo (h1, h1->GetNDevices () - 1, isd_number, as_number);

          h1->AddToAddressForwardingTable (h2->GetLocalAddress (), h1->GetNDevices () - 1);
          h2->AddToAddressForwardingTable (h1->GetLocalAddress (), h2->GetNDevices () - 1);
        }
    }
}

void
ScionAs::InitializeLatencies (bool only_propagation_delay)
{
  latencies_between_interfaces.resize (GetNDevices ());

  for (uint64_t i = 0; i < GetNDevices (); ++i)
    {
      latencies_between_interfaces.at (i).resize (GetNDevices ());
    }

  for (uint32_t i = 0; i < GetNDevices (); ++i)
    {
      for (uint32_t j = i + 1; j < GetNDevices (); ++j)
        {
          latencies_between_interfaces.at (i).at (j) = calculate_great_circle_latency (
              interfaces_coordinates.at (i).first, interfaces_coordinates.at (i).second,
              interfaces_coordinates.at (j).first, interfaces_coordinates.at (j).second);
          latencies_between_interfaces.at (j).at (i) = latencies_between_interfaces.at (i).at (j);
        }
    }

  latency_between_path_server_and_beacon_server = MilliSeconds (100);
  // host_address == 0 ==> beacon server, host_address == 1 ==> path_server
  latencies_between_hosts_and_path_server.push_back (MilliSeconds (100));
  latencies_between_hosts_and_path_server.push_back (Time (0));

  for (uint32_t i = 0; i < hosts.size (); ++i)
    {
      latencies_between_hosts_and_path_server.push_back (MilliSeconds (20));
    }
}

void
ScionAs::AddToRemoteAsInfo (uint16_t remote_if, ScionAs *remote_as)
{
  remote_as_info.push_back (std::make_pair (remote_if, remote_as));
}

void
ScionAs::InstantiateBeaconServer (bool parallel_scheduler, rapidxml::xml_node<> *xml_node,
                                     const YAML::Node &config)
{
  std::string beaconing_policy_str = config["beacon_service"]["policy"].as<std::string> ();

  if (beaconing_policy_str == "baseline")
    {
      beacon_server = (BeaconServer *) new Baseline (this, parallel_scheduler, xml_node, config);
    }
  else if (beaconing_policy_str == "diversity_age_based")
    {
      beacon_server =
          (BeaconServer *) new DiversityAgeBased (this, parallel_scheduler, xml_node, config);
    }
  else if (beaconing_policy_str == "green_beaconing")
    {
      beacon_server =
          (BeaconServer *) new GreenBeaconing (this, parallel_scheduler, xml_node, config);
    }
  else if (beaconing_policy_str == "latency_optimized")
    {
      beacon_server =
          (BeaconServer *) new LatencyOptimized (this, parallel_scheduler, xml_node, config);
    }
  else if (beaconing_policy_str == "scionlab")
    {
      beacon_server = (BeaconServer *) new Scionlab (this, parallel_scheduler, xml_node, config);
    }
  else if (beaconing_policy_str == "on_demand")
    {
      beacon_server =
          (BeaconServer *) new OnDemandOptimization (this, parallel_scheduler, xml_node, config);
    }
  else
    {
      beacon_server = (BeaconServer *) new Baseline (this, parallel_scheduler, xml_node, config);
    }
}
} // namespace ns3