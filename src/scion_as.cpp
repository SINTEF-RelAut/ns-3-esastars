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
ScionAs::DoInitializations (uint32_t numASes, rapidxml::xml_node<> *xmlNode,
                             const YAML::Node &config)
{
  InitializeLatencies (true);

  asMaxBwd = 0;
  for (auto const currBwd : interAsBwds)
    {
      if (currBwd > asMaxBwd)
        {
          asMaxBwd = currBwd;
        }
    }

  beaconServer->DoInitializations (numASes, xmlNode, config);
}

void
ScionAs::DoInitializations (uint32_t numASes, rapidxml::xml_node<> *xmlNode,
                             const YAML::Node &config, bool onlyPropagationDelay)
{
  ConnectInternalNodes (onlyPropagationDelay);
  InitializeLatencies (onlyPropagationDelay);

  for (auto const &br : borderRouters)
    {
      br->InitializeTransmissionQueues ();
    }

  for (auto const &host : hosts)
    {
      host->InitializeTransmissionQueues ();
    }

  pathServer->InitializeTransmissionQueues ();

  asMaxBwd = 0;
  for (auto const currBwd : interAsBwds)
    {
      if (currBwd > asMaxBwd)
        {
          asMaxBwd = currBwd;
        }
    }

  beaconServer->DoInitializations (numASes, xmlNode, config);
}

std::pair<uint16_t, ScionAs *>
ScionAs::GetRemoteAsInfo (uint16_t egressInterfaceNo)
{
  return remoteAsInfo.at (egressInterfaceNo);
}

void
ScionAs::ReceiveBeacon (Beacon &theBeacon, uint16_t senderAs, uint16_t remoteIf,
                         uint16_t localIf)
{
  beaconServer->ReceiveBeacon (theBeacon, senderAs, remoteIf, localIf);
}

void
ScionAs::SetBeaconServer (BeaconServer *theBeaconServer)
{
  this->beaconServer = theBeaconServer;
}

void
ScionAs::AdvanceTime (ns3::Time advance)
{
  localTime += advance;
}

BeaconServer *
ScionAs::GetBeaconServer ()
{
  return this->beaconServer;
}

PathServer *
ScionAs::GetPathServer ()
{
  return this->pathServer;
}

void
ScionAs::SetPathServer (PathServer *thePathServer)
{
  this->pathServer = thePathServer;
}

ScionCapableNode *
ScionAs::GetHost (HostAddr_t hostAddr)
{
  if (hostAddr == 1)
    {
      return GetPathServer ();
    }

  return ((ScionCapableNode *) hosts.at (hostAddr - 2));
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
ScionAs::AddBr (double latitude, double longitude, Time processingDelay,
                 Time processingThroughputDelay)
{
  BorderRouter *theBr = new BorderRouter (0, isdNumber, asNumber, 0, latitude, longitude, this);

  theBr->SetProcessingDelay (processingDelay, processingThroughputDelay);

  borderRouters.push_back (theBr);

  return theBr;
}

void
ScionAs::ConnectInternalNodes (bool onlyPropagationDelay)
{
  Time maliciousDelay = TimeStep (0);
  if (maliciousBorderRouters &&
      (borderRoutersMaliciousAction == "symmetric_delay" ||
       borderRoutersMaliciousAction == "asymmetric_delay") &&
      !onlyPropagationDelay)
    {
      std::random_device rd;
      std::uniform_int_distribution<uint64_t> dist (
          50000, 300000); // random asymmetry between 50ms and 300ms
      uint64_t randomDelay = dist (rd);
      maliciousDelay = MicroSeconds (randomDelay);
    }

  std::map<BorderRouter *, std::set<uint16_t>> borderRouterToIf;

  for (uint16_t i = 0; i < GetNDevices (); ++i)
    {
      BorderRouter *br = borderRouters.at (i);
      if (borderRouterToIf.find (br) == borderRouterToIf.end ())
        {
          borderRouterToIf.insert (std::make_pair (br, std::set<uint16_t> ()));
        }
      borderRouterToIf.at (br).insert (i);
    }

  std::set<BorderRouter *> borderRoutersSet (borderRouters.begin (), borderRouters.end ());
  std::vector<BorderRouter *> borderRoutersVec (borderRoutersSet.begin (),
                                                  borderRoutersSet.end ());

  // Connect border routers to border routers
  for (uint32_t i = 0; i < borderRoutersVec.size () - 1; ++i)
    {
      BorderRouter *br1 = borderRoutersVec.at (i);
      for (uint32_t j = i + 1; j < borderRoutersVec.size (); ++j)
        {
          BorderRouter *br2 = borderRoutersVec.at (j);

          Time propagationDelay1 = NanoSeconds ((int64_t) floor (
              1e6 * CalculateGreatCircleLatency ((Ld_t) br1->GetLatitude (), (Ld_t) br1->GetLogitude (),
                                                 (Ld_t) br2->GetLatitude (),
                                                 (Ld_t) br2->GetLogitude ())));
          Time propagationDelay2 = propagationDelay1;

          if (borderRoutersMaliciousAction == "symmetric_delay")
            {
              propagationDelay1 += maliciousDelay;
              propagationDelay2 += maliciousDelay;
            }
          else if (borderRoutersMaliciousAction == "asymmetric_delay")
            {
              propagationDelay1 += maliciousDelay;
            }

          br1->AddToPropagationDelays (propagationDelay1);
          br2->AddToPropagationDelays (propagationDelay2);

          if (onlyPropagationDelay)
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

          br1->AddToRemoteNodesInfo (br2, br2->GetNDevices () - 1, isdNumber, asNumber);
          br2->AddToRemoteNodesInfo (br1, br1->GetNDevices () - 1, isdNumber, asNumber);

          for (uint16_t asIf : borderRouterToIf.at (br1))
            {
              br2->AddToIfForwadingTable (asIf, br2->GetNDevices () - 1);
            }

          for (uint16_t asIf : borderRouterToIf.at (br2))
            {
              br1->AddToIfForwadingTable (asIf, br1->GetNDevices () - 1);
            }
        }
    }

  // Connect border routers to hosts
  for (uint32_t i = 0; i < borderRoutersVec.size (); ++i)
    {
      BorderRouter *br = borderRoutersVec.at (i);
      for (uint32_t j = 0; j < hosts.size (); ++j)
        {
          ScionHost *host = hosts.at (j);

          Time propagationDelay = NanoSeconds (
              (int64_t) floor (1e6 * CalculateGreatCircleLatency ((Ld_t) br->GetLatitude (), (Ld_t) br->GetLogitude (),
                                                 (Ld_t) host->GetLatitude (),
                                                 (Ld_t) host->GetLogitude ())));

          br->AddToPropagationDelays (propagationDelay);
          host->AddToPropagationDelays (propagationDelay);

          if (onlyPropagationDelay)
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

          br->AddToRemoteNodesInfo (host, host->GetNDevices () - 1, isdNumber, asNumber);
          host->AddToRemoteNodesInfo (br, br->GetNDevices () - 1, isdNumber, asNumber);

          for (uint16_t asIf : borderRouterToIf.at (br))
            {
              host->AddToIfForwadingTable (asIf, host->GetNDevices () - 1);
            }

          br->AddToAddressForwardingTable (host->GetLocalAddress (), br->GetNDevices () - 1);
        }
    }

  // Connect border routers to path server
  for (uint32_t i = 0; i < borderRoutersVec.size (); ++i)
    {
      BorderRouter *br = borderRoutersVec.at (i);

      Time propagationDelay = NanoSeconds ((int64_t) floor (
          1e6 * CalculateGreatCircleLatency ((Ld_t) br->GetLatitude (), (Ld_t) br->GetLogitude (),
                                             (Ld_t) pathServer->GetLatitude (),
                                             (Ld_t) pathServer->GetLogitude ())));

      br->AddToPropagationDelays (propagationDelay);
      pathServer->AddToPropagationDelays (propagationDelay);

      if (onlyPropagationDelay)
        {
          br->AddToTransmissionDelays (
              Time (0)); // transmission delay for one byte assuming 10 Gbps link
          pathServer->AddToTransmissionDelays (Time (0));
        }
      else
        {
          br->AddToTransmissionDelays (
              PicoSeconds (800)); // transmission delay for one byte assuming 10 Gbps link
          pathServer->AddToTransmissionDelays (PicoSeconds (800));
        }

      br->AddToRemoteNodesInfo (pathServer, pathServer->GetNDevices () - 1, isdNumber, asNumber);
      pathServer->AddToRemoteNodesInfo (br, br->GetNDevices () - 1, isdNumber, asNumber);

      for (uint16_t asIf : borderRouterToIf.at (br))
        {
          pathServer->AddToIfForwadingTable (asIf, pathServer->GetNDevices () - 1);
        }

      br->AddToAddressForwardingTable (pathServer->GetLocalAddress (), br->GetNDevices () - 1);
    }

  // Connect hosts to local path server
  for (uint32_t i = 0; i < hosts.size (); ++i)
    {
      ScionHost *host = hosts.at (i);

      Time propagationDelay = NanoSeconds ((int64_t) floor (
          1e6 * CalculateGreatCircleLatency ((Ld_t) host->GetLatitude (), (Ld_t) host->GetLogitude (),
                                             (Ld_t) pathServer->GetLatitude (),
                                             (Ld_t) pathServer->GetLogitude ())));

      host->AddToPropagationDelays (propagationDelay);
      pathServer->AddToPropagationDelays (propagationDelay);

      if (onlyPropagationDelay)
        {
          host->AddToTransmissionDelays (
              Time (0)); // transmission delay for one byte assuming 1 Gbps link
          pathServer->AddToTransmissionDelays (Time (0));
        }
      else
        {
          host->AddToTransmissionDelays (
              NanoSeconds (8)); // transmission delay for one byte assuming 1 Gbps link
          pathServer->AddToTransmissionDelays (NanoSeconds (8));
        }

      host->AddToRemoteNodesInfo (pathServer, pathServer->GetNDevices () - 1, isdNumber,
                                  asNumber);
      pathServer->AddToRemoteNodesInfo (host, host->GetNDevices () - 1, isdNumber, asNumber);

      host->AddToAddressForwardingTable (pathServer->GetLocalAddress (), host->GetNDevices () - 1);
      pathServer->AddToAddressForwardingTable (host->GetLocalAddress (),
                                               pathServer->GetNDevices () - 1);
    }

  // Connect hosts to each other
  for (uint32_t i = 0; i < hosts.size () - 1; ++i)
    {
      ScionHost *h1 = hosts.at (i);
      for (uint32_t j = i + 1; j < hosts.size (); ++j)
        {
          ScionHost *h2 = hosts.at (j);

          Time propagationDelay = NanoSeconds ((int64_t) floor (
              1e6 * CalculateGreatCircleLatency ((Ld_t) h1->GetLatitude (), (Ld_t) h1->GetLogitude (),
                                                 (Ld_t) h2->GetLatitude (),
                                                 (Ld_t) h2->GetLogitude ())));

          h1->AddToPropagationDelays (propagationDelay);
          h2->AddToPropagationDelays (propagationDelay);

          if (onlyPropagationDelay)
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

          h1->AddToRemoteNodesInfo (h2, h2->GetNDevices () - 1, isdNumber, asNumber);
          h2->AddToRemoteNodesInfo (h1, h1->GetNDevices () - 1, isdNumber, asNumber);

          h1->AddToAddressForwardingTable (h2->GetLocalAddress (), h1->GetNDevices () - 1);
          h2->AddToAddressForwardingTable (h1->GetLocalAddress (), h2->GetNDevices () - 1);
        }
    }
}

void
ScionAs::InitializeLatencies (bool onlyPropagationDelay)
{
  latenciesBetweenInterfaces.resize (GetNDevices ());

  for (uint64_t i = 0; i < GetNDevices (); ++i)
    {
      latenciesBetweenInterfaces.at (i).resize (GetNDevices ());
    }

  for (uint32_t i = 0; i < GetNDevices (); ++i)
    {
      for (uint32_t j = i + 1; j < GetNDevices (); ++j)
        {
          latenciesBetweenInterfaces.at (i).at (j) = CalculateGreatCircleLatency (
              interfacesCoordinates.at (i).first, interfacesCoordinates.at (i).second,
              interfacesCoordinates.at (j).first, interfacesCoordinates.at (j).second);
          latenciesBetweenInterfaces.at (j).at (i) = latenciesBetweenInterfaces.at (i).at (j);
        }
    }

  latencyBetweenPathServerAndBeaconServer = MilliSeconds (100);
  // host_address == 0 ==> beacon server, host_address == 1 ==> path_server
  latenciesBetweenHostsAndPathServer.push_back (MilliSeconds (100));
  latenciesBetweenHostsAndPathServer.push_back (Time (0));

  for (uint32_t i = 0; i < hosts.size (); ++i)
    {
      latenciesBetweenHostsAndPathServer.push_back (MilliSeconds (20));
    }
}

void
ScionAs::AddToRemoteAsInfo (uint16_t remoteIf, ScionAs *remoteAs)
{
  remoteAsInfo.push_back (std::make_pair (remoteIf, remoteAs));
}

void
ScionAs::InstantiateBeaconServer (bool parallelScheduler, rapidxml::xml_node<> *xmlNode,
                                     const YAML::Node &config)
{
  std::string beaconingPolicyStr = config["beacon_service"]["policy"].as<std::string> ();

  if (beaconingPolicyStr == "baseline")
    {
      beaconServer = (BeaconServer *) new Baseline (this, parallelScheduler, xmlNode, config);
    }
  else if (beaconingPolicyStr == "diversity_age_based")
    {
      beaconServer =
          (BeaconServer *) new DiversityAgeBased (this, parallelScheduler, xmlNode, config);
    }
  else if (beaconingPolicyStr == "green_beaconing")
    {
      beaconServer =
          (BeaconServer *) new GreenBeaconing (this, parallelScheduler, xmlNode, config);
    }
  else if (beaconingPolicyStr == "latency_optimized")
    {
      beaconServer =
          (BeaconServer *) new LatencyOptimized (this, parallelScheduler, xmlNode, config);
    }
  else if (beaconingPolicyStr == "scionlab")
    {
      beaconServer = (BeaconServer *) new Scionlab (this, parallelScheduler, xmlNode, config);
    }
  else if (beaconingPolicyStr == "on_demand")
    {
      beaconServer =
          (BeaconServer *) new OnDemandOptimization (this, parallelScheduler, xmlNode, config);
    }
  else
    {
      beaconServer = (BeaconServer *) new Baseline (this, parallelScheduler, xmlNode, config);
    }
}
} // namespace ns3