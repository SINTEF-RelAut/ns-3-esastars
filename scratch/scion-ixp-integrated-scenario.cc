/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include <cmath>
#include <iostream>
#include <map>
#include <string>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/scion-as.h"
#include "ns3/virtual-ixp-fabric.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("ScionIxpIntegratedScenario");

namespace {

struct Point2d
{
  double x;
  double y;
};

bool g_pass = true;

void
ExpectInterfaceState (Ptr<ScionAs> as, uint16_t scionIf, bool expectedUp, const std::string &label)
{
  const bool isUp =
      as->interface_to_neighbor_map.find (scionIf) != as->interface_to_neighbor_map.end ();
  if (isUp != expectedUp)
    {
      g_pass = false;
      std::cerr << "FAIL: " << label << " expected IF " << scionIf << (expectedUp ? " UP" : " DOWN")
                << " but observed " << (isUp ? "UP" : "DOWN") << std::endl;
      return;
    }

  NS_LOG_UNCOND ("[ASSERT] " << label << " IF " << scionIf << " is " << (isUp ? "UP" : "DOWN")
                             << " at t=" << Simulator::Now ().GetSeconds () << "s");
}

void
SeedScionInterfaceState (Ptr<ScionAs> as, uint16_t scionIf, uint16_t remoteAs)
{
  as->interface_to_neighbor_map[scionIf] = remoteAs;
  as->interfaces_per_neighbor_as[remoteAs].push_back (scionIf);
  as->neighbors.push_back (std::make_pair (remoteAs, PEER));
}

} // namespace

int
main (int argc, char *argv[])
{
  bool verbose = true;
  CommandLine cmd;
  cmd.AddValue ("verbose", "Print scenario logs", verbose);
  cmd.Parse (argc, argv);

  if (!verbose)
    {
      LogComponentDisable ("ScionIxpIntegratedScenario", LOG_LEVEL_ALL);
    }

  const uint16_t scionIf = 1;
  const double radius = 1000.0;
  const Point2d ixpCenter = {0.0, 0.0};

  std::map<uint32_t, Point2d> topology;
  topology[201] = {ixpCenter.x + radius, ixpCenter.y};
  topology[202] = {ixpCenter.x, ixpCenter.y + radius};
  topology[203] = {ixpCenter.x - radius, ixpCenter.y};
  topology[204] = {ixpCenter.x, ixpCenter.y - radius};

  NS_LOG_UNCOND ("IXP center: (" << ixpCenter.x << ", " << ixpCenter.y << ")");
  for (std::map<uint32_t, Point2d>::const_iterator it = topology.begin (); it != topology.end ();
       ++it)
    {
      NS_LOG_UNCOND ("Satellite AS" << it->first << " at (" << it->second.x << ", " << it->second.y
                                    << ")");
    }

  Ptr<Node> ixpNode = CreateObject<Node> ();
  (void) ixpNode;

  Ptr<ScionAs> as201 = CreateObject<ScionAs> (1, 201);
  Ptr<ScionAs> as202 = CreateObject<ScionAs> (2, 202);
  Ptr<ScionAs> as203 = CreateObject<ScionAs> (3, 203);
  Ptr<ScionAs> as204 = CreateObject<ScionAs> (4, 204);

  SeedScionInterfaceState (as201, scionIf, 9201);
  SeedScionInterfaceState (as202, scionIf, 9202);
  SeedScionInterfaceState (as203, scionIf, 9203);
  SeedScionInterfaceState (as204, scionIf, 9204);

  Ptr<ScionIxpEndpointAdapter> ep201 = CreateObject<ScionIxpEndpointAdapter> ();
  Ptr<ScionIxpEndpointAdapter> ep202 = CreateObject<ScionIxpEndpointAdapter> ();
  Ptr<ScionIxpEndpointAdapter> ep203 = CreateObject<ScionIxpEndpointAdapter> ();
  Ptr<ScionIxpEndpointAdapter> ep204 = CreateObject<ScionIxpEndpointAdapter> ();

  ep201->Bind (201, as201, scionIf);
  ep202->Bind (202, as202, scionIf);
  ep203->Bind (203, as203, scionIf);
  ep204->Bind (204, as204, scionIf);

  Ptr<VirtualIxpFabric> fabric = CreateObject<VirtualIxpFabric> ();
  IxpConfig cfg;
  cfg.portCount = 2;
  cfg.rebalancePeriod = Seconds (0.25);
  cfg.holdDown = Seconds (0.0);
  fabric->Configure (cfg);

  const IxpLinkId l201 = {201, 1};
  const IxpLinkId l202 = {202, 1};
  const IxpLinkId l203 = {203, 1};
  const IxpLinkId l204 = {204, 1};

  fabric->RegisterEndpoint (l201, ep201);
  fabric->RegisterEndpoint (l202, ep202);
  fabric->RegisterEndpoint (l203, ep203);
  fabric->RegisterEndpoint (l204, ep204);

  fabric->Start ();

  // Longer event windows allow protocol reactions to settle between changes.
  const Time tInitialAssert = Seconds (2.0);
  const Time tDown202 = Seconds (6.0);
  const Time tAssert202Down = Seconds (7.0);
  const Time tDown203 = Seconds (14.0);
  const Time tAssert203Down = Seconds (15.0);
  const Time tUp202 = Seconds (16.0);
  const Time tAssert202Up = Seconds (17.0);
  const Time tDown201 = Seconds (20.0);
  const Time tAssert201Down = Seconds (21.0);
  const Time tUp204 = Seconds (22.0);
  const Time tAssert204Up = Seconds (23.0);
  const Time tDown204 = Seconds (28.0);
  const Time tAssert204Down = Seconds (29.0);
  const Time tUp201 = Seconds (30.0);
  const Time tAssert201Up = Seconds (31.0);

  // Start with two feasible links attached; others rotate in later.
  Simulator::Schedule (Seconds (0.0), &VirtualIxpFabric::SetFeasibleUp, fabric, l201, 1.0);
  Simulator::Schedule (Seconds (0.0), &VirtualIxpFabric::SetFeasibleUp, fabric, l202, 0.9);
  Simulator::Schedule (Seconds (1.0), &VirtualIxpFabric::SetFeasibleUp, fabric, l203, 0.95);

  // Explicit down/up cycles with larger gaps to let SCION react.
  Simulator::Schedule (tDown202, &VirtualIxpFabric::SetFeasibleDown, fabric, l202);
  Simulator::Schedule (tDown203, &VirtualIxpFabric::SetFeasibleDown, fabric, l203);
  Simulator::Schedule (tUp202, &VirtualIxpFabric::SetFeasibleUp, fabric, l202, 0.92);
  Simulator::Schedule (tDown201, &VirtualIxpFabric::SetFeasibleDown, fabric, l201);
  Simulator::Schedule (tUp204, &VirtualIxpFabric::SetFeasibleUp, fabric, l204, 1.1);
  Simulator::Schedule (tDown204, &VirtualIxpFabric::SetFeasibleDown, fabric, l204);
  Simulator::Schedule (tUp201, &VirtualIxpFabric::SetFeasibleUp, fabric, l201, 1.0);

  // Timed assertions for the SCION interface mapping state.
  Simulator::Schedule (tInitialAssert, &ExpectInterfaceState, as201, scionIf, true,
                       "AS201 initial attach");
  Simulator::Schedule (tInitialAssert, &ExpectInterfaceState, as202, scionIf, true,
                       "AS202 initial attach");

  Simulator::Schedule (tAssert202Down, &ExpectInterfaceState, as202, scionIf, false,
                       "AS202 detached after infeasible");
  Simulator::Schedule (tAssert202Down, &ExpectInterfaceState, as203, scionIf, true,
                       "AS203 attached after AS202 down");
  Simulator::Schedule (tAssert203Down, &ExpectInterfaceState, as203, scionIf, false,
                       "AS203 detached before AS202 returns");
  Simulator::Schedule (tAssert202Up, &ExpectInterfaceState, as202, scionIf, true,
                       "AS202 reattached after long-down window");

  Simulator::Schedule (tAssert201Down, &ExpectInterfaceState, as201, scionIf, false,
                       "AS201 detached after infeasible");
  Simulator::Schedule (tAssert204Up, &ExpectInterfaceState, as204, scionIf, true,
                       "AS204 attached near end");
  Simulator::Schedule (tAssert204Down, &ExpectInterfaceState, as204, scionIf, false,
                       "AS204 detached before AS201 returns");
  Simulator::Schedule (tAssert201Up, &ExpectInterfaceState, as201, scionIf, true,
                       "AS201 reattached after long-down window");

  Simulator::Schedule (Seconds (34.0), &VirtualIxpFabric::Stop, fabric);
  Simulator::Stop (Seconds (35.0));

  Simulator::Run ();
  Simulator::Destroy ();

  if (!g_pass)
    {
      return 1;
    }

  std::cout << "PASS: SCION-integrated IXP scenario checks passed" << std::endl;
  return 0;
}
