/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include <iostream>
#include <map>
#include <sstream>
#include <string>

#include "ns3/bgp.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/virtual-ixp-fabric.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("BgpIxpIntegratedScenario");

namespace {

struct Point2d
{
  double x;
  double y;
};

struct SatelliteRuntime
{
  Ptr<Node> node;
  uint32_t ifIndex;
  Ipv4Address localAddr;
};

bool g_pass = true;

void
ExpectIfState (const std::string &label, Ptr<Node> node, uint32_t ifIndex, bool expectedUp)
{
  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
  if (ipv4 == 0)
    {
      g_pass = false;
      std::cerr << "FAIL: " << label << " missing Ipv4 stack" << std::endl;
      return;
    }

  const bool isUp = ipv4->IsUp (ifIndex);
  if (isUp != expectedUp)
    {
      g_pass = false;
      std::cerr << "FAIL: " << label << " expected IF " << ifIndex << (expectedUp ? " UP" : " DOWN")
                << " but observed " << (isUp ? "UP" : "DOWN") << std::endl;
      return;
    }

  NS_LOG_UNCOND ("[ASSERT] " << label << " IF " << ifIndex << " is " << (isUp ? "UP" : "DOWN")
                             << " at t=" << Simulator::Now ().GetSeconds () << "s");
}

void
InstallBgpOnNode (uint32_t asn, Ptr<Node> node, Time clockInterval)
{
  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();

  Ipv4Address routerId = Ipv4Address::GetAny ();
  for (uint32_t iface = 1; iface < ipv4->GetNInterfaces (); ++iface)
    {
      if (ipv4->GetNAddresses (iface) > 0)
        {
          routerId = ipv4->GetAddress (iface, 0).GetLocal ();
          break;
        }
    }

  Ptr<Bgp> bgp = CreateObject<Bgp> ();
  bgp->SetAttribute ("RouterID", Ipv4AddressValue (routerId));
  bgp->SetAttribute ("HoldTimer", TimeValue (Seconds (9.0)));
  bgp->SetAttribute ("ClockInterval", TimeValue (clockInterval));
  bgp->SetAttribute ("LibbgpLogLevel", EnumValue (libbgp::FATAL));

  node->AddApplication (bgp);

  for (uint32_t iface = 1; iface < ipv4->GetNInterfaces (); ++iface)
    {
      if (ipv4->GetNAddresses (iface) == 0)
        {
          continue;
        }

      Ipv4InterfaceAddress ifAddr = ipv4->GetAddress (iface, 0);
      Ipv4Address prefix = ifAddr.GetLocal ().CombineMask (ifAddr.GetMask ());
      bgp->AddRoute (prefix, ifAddr.GetMask (), ifAddr.GetLocal ());
    }

  bgp->SetStartTime (Seconds (0.2));
  bgp->SetStopTime (Seconds (18.0));

  NS_LOG_UNCOND ("Installed BGP app for AS" << asn << " router-id=" << routerId);
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
      LogComponentDisable ("BgpIxpIntegratedScenario", LOG_LEVEL_ALL);
    }

  const Point2d ixpCenter = {0.0, 0.0};
  const double radius = 1000.0;

  std::map<uint32_t, Point2d> satellites;
  satellites[301] = {ixpCenter.x + radius, ixpCenter.y};
  satellites[302] = {ixpCenter.x, ixpCenter.y + radius};
  satellites[303] = {ixpCenter.x - radius, ixpCenter.y};
  satellites[304] = {ixpCenter.x, ixpCenter.y - radius};

  NS_LOG_UNCOND ("IXP center: (" << ixpCenter.x << ", " << ixpCenter.y << ")");
  for (std::map<uint32_t, Point2d>::const_iterator it = satellites.begin ();
       it != satellites.end (); ++it)
    {
      NS_LOG_UNCOND ("Satellite AS" << it->first << " at (" << it->second.x << ", " << it->second.y
                                    << ")");
    }

  Ptr<Node> ixpNode = CreateObject<Node> ();

  std::map<uint32_t, Ptr<Node>> asNodes;
  NodeContainer allNodes;
  allNodes.Add (ixpNode);

  for (std::map<uint32_t, Point2d>::const_iterator it = satellites.begin ();
       it != satellites.end (); ++it)
    {
      Ptr<Node> n = CreateObject<Node> ();
      asNodes[it->first] = n;
      allNodes.Add (n);
    }

  InternetStackHelper internet;
  internet.Install (allNodes);

  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue ("200Mbps"));
  p2p.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (2)));

  std::map<uint32_t, SatelliteRuntime> runtime;
  std::map<uint32_t, Ipv4Address> ixpFacingAddr;

  uint32_t subnetId = 0;
  for (std::map<uint32_t, Point2d>::const_iterator it = satellites.begin ();
       it != satellites.end (); ++it)
    {
      const uint32_t asn = it->first;
      NetDeviceContainer devs = p2p.Install (ixpNode, asNodes[asn]);

      std::ostringstream base;
      base << "10." << (subnetId / 256) << "." << (subnetId % 256) << ".0";
      Ipv4AddressHelper ipv4;
      ipv4.SetBase (base.str ().c_str (), "255.255.255.252");
      Ipv4InterfaceContainer ifaces = ipv4.Assign (devs);

      Ptr<Ipv4> satIpv4 = asNodes[asn]->GetObject<Ipv4> ();
      const uint32_t satIf = satIpv4->GetInterfaceForDevice (devs.Get (1));

      SatelliteRuntime sr;
      sr.node = asNodes[asn];
      sr.ifIndex = satIf;
      sr.localAddr = ifaces.GetAddress (1);
      runtime[asn] = sr;
      ixpFacingAddr[asn] = ifaces.GetAddress (0);

      subnetId++;
    }

  const Time bgpClock = Seconds (0.5);
  InstallBgpOnNode (900, ixpNode, bgpClock);
  for (std::map<uint32_t, Ptr<Node>>::const_iterator it = asNodes.begin (); it != asNodes.end ();
       ++it)
    {
      InstallBgpOnNode (it->first, it->second, bgpClock);
    }

  Ptr<Bgp> bgpIxp = DynamicCast<Bgp> (ixpNode->GetApplication (0));
  for (std::map<uint32_t, Ptr<Node>>::const_iterator it = asNodes.begin (); it != asNodes.end ();
       ++it)
    {
      const uint32_t asn = it->first;

      Peer ixpToSat;
      ixpToSat.local_asn = 900;
      ixpToSat.peer_asn = asn;
      ixpToSat.peer_address = runtime[asn].localAddr;
      ixpToSat.passive = false;
      bgpIxp->AddPeer (ixpToSat);

      Ptr<Bgp> bgpSat = DynamicCast<Bgp> (it->second->GetApplication (0));
      Peer satToIxp;
      satToIxp.local_asn = asn;
      satToIxp.peer_asn = 900;
      satToIxp.peer_address = ixpFacingAddr[asn];
      satToIxp.passive = false;
      bgpSat->AddPeer (satToIxp);
    }

  Ptr<BgpIxpEndpointAdapter> ep301 = CreateObject<BgpIxpEndpointAdapter> ();
  Ptr<BgpIxpEndpointAdapter> ep302 = CreateObject<BgpIxpEndpointAdapter> ();
  Ptr<BgpIxpEndpointAdapter> ep303 = CreateObject<BgpIxpEndpointAdapter> ();
  Ptr<BgpIxpEndpointAdapter> ep304 = CreateObject<BgpIxpEndpointAdapter> ();

  ep301->Bind (301, runtime[301].node, runtime[301].ifIndex);
  ep302->Bind (302, runtime[302].node, runtime[302].ifIndex);
  ep303->Bind (303, runtime[303].node, runtime[303].ifIndex);
  ep304->Bind (304, runtime[304].node, runtime[304].ifIndex);

  Ptr<VirtualIxpFabric> fabric = CreateObject<VirtualIxpFabric> ();
  IxpConfig cfg;
  cfg.portCount = 2;
  cfg.rebalancePeriod = Seconds (0.25);
  cfg.holdDown = Seconds (0.0);
  fabric->Configure (cfg);

  const IxpLinkId l301 = {301, 1};
  const IxpLinkId l302 = {302, 1};
  const IxpLinkId l303 = {303, 1};
  const IxpLinkId l304 = {304, 1};

  fabric->RegisterEndpoint (l301, ep301);
  fabric->RegisterEndpoint (l302, ep302);
  fabric->RegisterEndpoint (l303, ep303);
  fabric->RegisterEndpoint (l304, ep304);

  fabric->Start ();

  // Longer event windows allow BGP to react to interface state transitions.
  const Time tInitialAssert = Seconds (2.0);
  const Time tDown302 = Seconds (6.0);
  const Time tAssert302Down = Seconds (7.0);
  const Time tDown303 = Seconds (14.0);
  const Time tAssert303Down = Seconds (15.0);
  const Time tUp302 = Seconds (16.0);
  const Time tAssert302Up = Seconds (17.0);

  Simulator::Schedule (Seconds (0.0), &VirtualIxpFabric::SetFeasibleUp, fabric, l301, 1.0);
  Simulator::Schedule (Seconds (0.0), &VirtualIxpFabric::SetFeasibleUp, fabric, l302, 0.95);
  Simulator::Schedule (Seconds (1.0), &VirtualIxpFabric::SetFeasibleUp, fabric, l303, 0.9);

  Simulator::Schedule (tDown302, &VirtualIxpFabric::SetFeasibleDown, fabric, l302);
  Simulator::Schedule (tDown303, &VirtualIxpFabric::SetFeasibleDown, fabric, l303);
  Simulator::Schedule (tUp302, &VirtualIxpFabric::SetFeasibleUp, fabric, l302, 0.96);

  Simulator::Schedule (tInitialAssert, &ExpectIfState, "AS301 initial attach", runtime[301].node,
                       runtime[301].ifIndex, true);
  Simulator::Schedule (tInitialAssert, &ExpectIfState, "AS302 initial attach", runtime[302].node,
                       runtime[302].ifIndex, true);

  Simulator::Schedule (tAssert302Down, &ExpectIfState, "AS302 detached after infeasible",
                       runtime[302].node, runtime[302].ifIndex, false);
  Simulator::Schedule (tAssert302Down, &ExpectIfState, "AS303 attached after AS302 down",
                       runtime[303].node, runtime[303].ifIndex, true);
  Simulator::Schedule (tAssert303Down, &ExpectIfState, "AS303 detached before AS302 returns",
                       runtime[303].node, runtime[303].ifIndex, false);
  Simulator::Schedule (tAssert302Up, &ExpectIfState, "AS302 reattached after long-down window",
                       runtime[302].node, runtime[302].ifIndex, true);

  Simulator::Schedule (Seconds (19.0), &VirtualIxpFabric::Stop, fabric);
  Simulator::Stop (Seconds (20.0));

  Simulator::Run ();
  Simulator::Destroy ();

  if (!g_pass)
    {
      return 1;
    }

  std::cout << "PASS: BGP-integrated IXP scenario checks passed" << std::endl;
  return 0;
}
