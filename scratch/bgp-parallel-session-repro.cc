/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

// Minimal reproduction for the BGP transit blackhole seen in the IXP scenarios.
//
// Topology is a three-AS transit chain, with no churn events at all:
//
//     AS201 ---- AS210 (IXP) ---- AS202
//
// Two probe pairs run over it:
//   201 -> 210  directly connected. Control: must be 0% loss in every configuration.
//   201 -> 202  transit through AS210. Subject: this is what blackholes.
//
// The variable under test is how many BGP sessions exist between one pair of ASNs.
// libbgp scopes its RIB by peer *router ID*, which is per node rather than per session,
// so two sessions to the same peer node share one RIB scope: they overwrite each other's
// entries, and any ESTABLISHED exit on either one calls dropAllRoutes() ->
// rib4->discard(peer_bgp_id), which deletes the routes belonging to the *other*, still
// established session. BgpRouting looks the RIB up live on every packet, so that is an
// immediate blackhole.
//
//   --linksPerPair=1                    one cable, one session      -> expect 0% loss
//   --linksPerPair=2 --sharedSubnet=0   two cables, two sessions    -> expect blackhole
//   --linksPerPair=2 --sharedSubnet=1   two cables, one session     -> expect 0% loss
//
// The scaffolding (probe app, echo responder, route origination) is deliberately copied
// from src/bgp/examples/bgp-convergence-first.cc and scratch/bgp-ixp-scenarios.cc so that
// any behavioural difference is attributable to the topology and not to the harness.

#include <errno.h>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <utility>
#include <vector>

#include "ns3/application-container.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv4-interface-address.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/socket.h"
#include "ns3/output-stream-wrapper.h"
#include "ns3/udp-socket-factory.h"

#include "ns3/bgp.h"

using namespace ns3;

namespace {

struct LinkSpec
{
  uint16_t as_a;
  uint16_t as_b;
  double latency_s;
  uint32_t capacity_mbps;
  bool is_standby; // second cable of a pair; downed at t=0 when running shared-subnet
};

class UdpEchoResponder : public Application
{
public:
  explicit UdpEchoResponder (uint16_t port) : m_port (port)
  {
  }

private:
  void
  StartApplication () override
  {
    m_socket = Socket::CreateSocket (GetNode (), UdpSocketFactory::GetTypeId ());
    m_socket->Bind (InetSocketAddress (Ipv4Address::GetAny (), m_port));
    m_socket->SetRecvCallback (MakeCallback (&UdpEchoResponder::HandleRead, this));
  }

  void
  StopApplication () override
  {
    if (m_socket)
      {
        m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket>> ());
        m_socket->Close ();
        m_socket = 0;
      }
  }

  void
  HandleRead (Ptr<Socket> socket)
  {
    Address from;
    Ptr<Packet> packet = socket->RecvFrom (from);
    if (!packet)
      {
        return;
      }
    socket->SendTo (packet, 0, from);
  }

  Ptr<Socket> m_socket;
  uint16_t m_port;
};

class UdpProbeApp : public Application
{
public:
  UdpProbeApp ()
      : m_src_as (0),
        m_dst_as (0),
        m_port (9000),
        m_seq (0),
        m_count (0),
        m_timeout (Seconds (2.0)),
        m_interval (Seconds (1.0))
  {
  }

  void
  Configure (uint16_t src_as, uint16_t dst_as, Ipv4Address dst_ip, uint16_t port, uint32_t count,
             Time interval, Time timeout, const std::string &output_path)
  {
    m_src_as = src_as;
    m_dst_as = dst_as;
    m_dst_ip = dst_ip;
    m_port = port;
    m_count = count;
    m_interval = interval;
    m_timeout = timeout;
    m_output_path = output_path;
  }

private:
  struct ProbePayload
  {
    uint32_t seq;
    uint64_t send_time_ns;
  };

  static uint64_t
  ToNs (Time t)
  {
    return static_cast<uint64_t> (t.GetNanoSeconds ());
  }

  void
  StartApplication () override
  {
    m_socket = Socket::CreateSocket (GetNode (), UdpSocketFactory::GetTypeId ());
    m_socket->Bind ();
    m_socket->Connect (InetSocketAddress (m_dst_ip, m_port));
    m_socket->SetRecvCallback (MakeCallback (&UdpProbeApp::HandleRead, this));

    m_out.open (m_output_path.c_str (), std::ios::out | std::ios::trunc);
    m_out << "time_s,src_as,dst_as,seq,event,rtt_ms" << std::endl;

    SendOne ();
  }

  void
  StopApplication () override
  {
    if (m_send_event.IsRunning ())
      {
        m_send_event.Cancel ();
      }

    for (std::map<uint32_t, EventId>::iterator it = m_timeouts.begin (); it != m_timeouts.end ();
         ++it)
      {
        it->second.Cancel ();
      }

    if (m_socket)
      {
        m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket>> ());
        m_socket->Close ();
        m_socket = 0;
      }

    if (m_out.is_open ())
      {
        m_out.close ();
      }
  }

  void
  SendOne ()
  {
    if (m_seq >= m_count)
      {
        return;
      }

    ProbePayload payload;
    payload.seq = m_seq;
    payload.send_time_ns = ToNs (Simulator::Now ());

    Ptr<Packet> p = Create<Packet> (reinterpret_cast<const uint8_t *> (&payload), sizeof (payload));
    m_socket->Send (p);

    m_send_times.insert (std::make_pair (m_seq, Simulator::Now ()));
    m_out << std::fixed << std::setprecision (6) << Simulator::Now ().GetSeconds () << ","
          << m_src_as << "," << m_dst_as << "," << m_seq << ",sent," << "" << std::endl;

    EventId timeout_event = Simulator::Schedule (m_timeout, &UdpProbeApp::OnTimeout, this, m_seq);
    m_timeouts.insert (std::make_pair (m_seq, timeout_event));

    m_seq++;
    m_send_event = Simulator::Schedule (m_interval, &UdpProbeApp::SendOne, this);
  }

  void
  HandleRead (Ptr<Socket> socket)
  {
    Ptr<Packet> p = socket->Recv ();
    if (!p || p->GetSize () < sizeof (ProbePayload))
      {
        return;
      }

    ProbePayload payload;
    p->CopyData (reinterpret_cast<uint8_t *> (&payload), sizeof (payload));

    std::map<uint32_t, Time>::iterator send_it = m_send_times.find (payload.seq);
    if (send_it == m_send_times.end ())
      {
        return;
      }

    std::map<uint32_t, EventId>::iterator timeout_it = m_timeouts.find (payload.seq);
    if (timeout_it != m_timeouts.end ())
      {
        timeout_it->second.Cancel ();
        m_timeouts.erase (timeout_it);
      }

    Time rtt = Simulator::Now () - send_it->second;
    m_send_times.erase (send_it);

    m_out << std::fixed << std::setprecision (6) << Simulator::Now ().GetSeconds () << ","
          << m_src_as << "," << m_dst_as << "," << payload.seq << ",reply," << std::setprecision (3)
          << (rtt.GetSeconds () * 1000.0) << std::endl;
  }

  void
  OnTimeout (uint32_t seq)
  {
    std::map<uint32_t, Time>::iterator it = m_send_times.find (seq);
    if (it == m_send_times.end ())
      {
        return;
      }

    m_send_times.erase (it);
    m_timeouts.erase (seq);

    m_out << std::fixed << std::setprecision (6) << Simulator::Now ().GetSeconds () << ","
          << m_src_as << "," << m_dst_as << "," << seq << ",timeout," << "" << std::endl;
  }

  Ptr<Socket> m_socket;
  EventId m_send_event;

  uint16_t m_src_as;
  uint16_t m_dst_as;
  Ipv4Address m_dst_ip;
  uint16_t m_port;

  uint32_t m_seq;
  uint32_t m_count;
  Time m_timeout;
  Time m_interval;

  std::string m_output_path;
  std::ofstream m_out;
  std::map<uint32_t, Time> m_send_times;
  std::map<uint32_t, EventId> m_timeouts;
};

bool
EnsureDirectory (const std::string &path)
{
  if (path.empty ())
    {
      return false;
    }

  int rc = mkdir (path.c_str (), 0775);
  return rc == 0 || errno == EEXIST;
}

Ptr<Bgp>
InstallBgpOnNode (uint32_t asn, Ptr<Node> node, Time clockInterval, Time holdTimer, Time mrai,
                  Time errorHold, const std::string &outDir, uint32_t libbgpLog)
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
  // HoldTimer is passed explicitly rather than derived from clockInterval, so that
  // failure-detection latency can be varied independently of the FSM tick rate.
  bgp->SetAttribute ("HoldTimer", TimeValue (holdTimer));
  bgp->SetAttribute ("Mrai", TimeValue (mrai));
  bgp->SetAttribute ("ClockInterval", TimeValue (clockInterval));
  bgp->SetAttribute ("ErrorHold", TimeValue (errorHold));
  const libbgp::LogLevel levels[] = {libbgp::FATAL, libbgp::ERROR, libbgp::WARN, libbgp::INFO,
                                     libbgp::DEBUG};
  bgp->SetAttribute ("LibbgpLogLevel", EnumValue (levels[libbgpLog > 4 ? 4 : libbgpLog]));
  node->AddApplication (bgp);

  std::ostringstream cpOut;
  cpOut << outDir << "/bgp_cp_as" << asn << ".csv";
  bgp->SetControlPlaneOutput (cpOut.str ());

  return bgp;
}

void
AddRoutesForAllInterfaces (Ptr<Bgp> bgp, Ptr<Node> node)
{
  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
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
}

} // namespace

int
main (int argc, char *argv[])
{
  uint32_t links_per_pair = 1;
  uint32_t sessions_per_pair = 2;
  bool shared_subnet = false;
  bool peer_weights = false;
  double sim_time_s = 150.0;
  double clock_interval_s = 1.0;
  double hold_timer_s = 3.0;
  double mrai_s = 1.0;
  double error_hold_s = 45.0;
  std::string outDir = "build/bgp_parallel_repro";
  uint32_t libbgp_log = 0; // 0=FATAL, 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG
  double dump_rib_at_s = 0.0;

  CommandLine cmd;
  cmd.AddValue ("linksPerPair", "Physical links between each AS pair (1 or 2)", links_per_pair);
  cmd.AddValue ("sessionsPerPair", "BGP sessions per ASN pair (1 or 2)", sessions_per_pair);
  cmd.AddValue ("sharedSubnet",
                "Give both links of a pair one subnet and the same addresses, standby down",
                shared_subnet);
  cmd.AddValue ("peerWeights", "Apply differing Peer::weight to the two links of a pair",
                peer_weights);
  cmd.AddValue ("simTime", "Simulation duration in seconds", sim_time_s);
  cmd.AddValue ("clockInterval", "BGP FSM clock interval in seconds", clock_interval_s);
  cmd.AddValue ("holdTimer", "BGP hold timer in seconds", hold_timer_s);
  cmd.AddValue ("mrai", "Minimum Route Advertisement Interval in seconds", mrai_s);
  cmd.AddValue ("errorHold", "BGP reconnect back-off after an error, in seconds", error_hold_s);
  cmd.AddValue ("outDir", "Output directory for probe and control-plane CSV files", outDir);
  cmd.AddValue ("libbgpLog", "libbgp log verbosity for diagnostics (0=FATAL..4=DEBUG)", libbgp_log);
  cmd.AddValue ("dumpRibAt", "If >0, write every node's routing table to <outDir>/rib.txt at this time",
                dump_rib_at_s);
  cmd.Parse (argc, argv);

  NS_ABORT_MSG_IF (links_per_pair < 1 || links_per_pair > 2,
                   "--linksPerPair must be 1 or 2");
  NS_ABORT_MSG_IF (sessions_per_pair < 1 || sessions_per_pair > 2,
                   "--sessionsPerPair must be 1 or 2");
  NS_ABORT_MSG_IF (shared_subnet && links_per_pair != 2,
                   "--sharedSubnet only means anything with --linksPerPair=2");
  NS_ABORT_MSG_IF (shared_subnet && sessions_per_pair != 1,
                   "--sharedSubnet requires --sessionsPerPair=1: both cables carry the same "
                   "addresses, so a second session would target the same peer address");

  // Both cables of a pair intentionally carry the same addresses under --sharedSubnet, so
  // the global address generator's duplicate check has to be relaxed.
  if (shared_subnet)
    {
      Ipv4AddressGenerator::TestMode ();
    }
  NS_ABORT_MSG_IF (!EnsureDirectory (outDir), "could not create output directory " << outDir);

  const uint16_t kAsSrc = 201;
  const uint16_t kAsIxp = 210;
  const uint16_t kAsDst = 202;
  const std::vector<uint16_t> asIds = {kAsSrc, kAsIxp, kAsDst};

  std::vector<LinkSpec> links;
  for (uint32_t i = 0; i < links_per_pair; ++i)
    {
      links.push_back ((LinkSpec){kAsSrc, kAsIxp, 0.0010, 300, i > 0});
    }
  for (uint32_t i = 0; i < links_per_pair; ++i)
    {
      links.push_back ((LinkSpec){kAsIxp, kAsDst, 0.0010, 300, i > 0});
    }

  NodeContainer nodes;
  nodes.Create (asIds.size ());

  std::map<uint16_t, Ptr<Node>> asNodes;
  std::map<uint16_t, uint32_t> asToNodeIdx;
  for (uint32_t i = 0; i < asIds.size (); ++i)
    {
      asToNodeIdx[asIds.at (i)] = i;
      asNodes.insert (std::make_pair (asIds.at (i), nodes.Get (i)));
    }

  InternetStackHelper internet;
  internet.Install (nodes);

  struct LinkRuntime
  {
    LinkSpec spec;
    Ptr<Ipv4> ipv4_a;
    Ptr<Ipv4> ipv4_b;
    uint32_t iface_a;
    uint32_t iface_b;
    Ipv4Address ip_a;
    Ipv4Address ip_b;
  };

  std::vector<LinkRuntime> runtimes;
  std::map<std::pair<uint16_t, uint16_t>, uint32_t> subnetForPair;
  uint32_t nextSubnetId = 0;

  for (uint32_t i = 0; i < links.size (); ++i)
    {
      const LinkSpec &spec = links.at (i);
      Ptr<Node> nodeA = nodes.Get (asToNodeIdx.at (spec.as_a));
      Ptr<Node> nodeB = nodes.Get (asToNodeIdx.at (spec.as_b));

      std::ostringstream rate;
      rate << spec.capacity_mbps << "Mbps";

      PointToPointHelper p2p;
      p2p.SetDeviceAttribute ("DataRate", StringValue (rate.str ()));
      p2p.SetChannelAttribute ("Delay", TimeValue (Seconds (spec.latency_s)));

      NetDeviceContainer devs = p2p.Install (nodeA, nodeB);

      // With --sharedSubnet the two cables of a pair carry the same addresses, so the
      // BGP session's peer address is unchanged across a handover. Otherwise every cable
      // gets its own /30, which is what produces two distinct sessions per ASN pair.
      std::pair<uint16_t, uint16_t> pairKey = std::make_pair (std::min (spec.as_a, spec.as_b),
                                                              std::max (spec.as_a, spec.as_b));

      uint32_t subnetId;
      if (shared_subnet && subnetForPair.count (pairKey) > 0)
        {
          subnetId = subnetForPair.at (pairKey);
        }
      else
        {
          subnetId = nextSubnetId++;
          subnetForPair[pairKey] = subnetId;
        }

      std::ostringstream base;
      base << "10." << (subnetId / 256) << "." << (subnetId % 256) << ".0";
      Ipv4AddressHelper ipv4;
      ipv4.SetBase (base.str ().c_str (), "255.255.255.252");
      Ipv4InterfaceContainer ifaces = ipv4.Assign (devs);

      LinkRuntime runtime;
      runtime.spec = spec;
      runtime.ipv4_a = nodeA->GetObject<Ipv4> ();
      runtime.ipv4_b = nodeB->GetObject<Ipv4> ();
      runtime.iface_a = runtime.ipv4_a->GetInterfaceForDevice (devs.Get (0));
      runtime.iface_b = runtime.ipv4_b->GetInterfaceForDevice (devs.Get (1));
      runtime.ip_a = ifaces.GetAddress (0);
      runtime.ip_b = ifaces.GetAddress (1);
      runtimes.push_back (runtime);
    }

  // Two up interfaces carrying the same address on one node is not a valid state, so the
  // standby cable is taken down before anything starts.
  if (shared_subnet)
    {
      for (uint32_t i = 0; i < runtimes.size (); ++i)
        {
          if (!runtimes.at (i).spec.is_standby)
            {
              continue;
            }
          runtimes.at (i).ipv4_a->SetDown (runtimes.at (i).iface_a);
          runtimes.at (i).ipv4_b->SetDown (runtimes.at (i).iface_b);
        }
    }

  std::map<uint16_t, Ptr<Bgp>> bgpApps;
  for (std::map<uint16_t, Ptr<Node>>::const_iterator it = asNodes.begin (); it != asNodes.end ();
       ++it)
    {
      bgpApps[it->first] =
          InstallBgpOnNode (it->first, it->second, Seconds (clock_interval_s),
                            Seconds (hold_timer_s), Seconds (mrai_s), Seconds (error_hold_s),
                            outDir, libbgp_log);
    }

  // One session per ASN pair when the pair shares a subnet; otherwise one per cable,
  // which is the configuration under test.
  std::set<std::pair<uint16_t, uint16_t>> peeredAsnPairs;
  for (uint32_t i = 0; i < runtimes.size (); ++i)
    {
      const LinkRuntime &rt = runtimes.at (i);
      std::pair<uint16_t, uint16_t> pairKey =
          std::make_pair (std::min (rt.spec.as_a, rt.spec.as_b),
                          std::max (rt.spec.as_a, rt.spec.as_b));

      if (sessions_per_pair == 1 && peeredAsnPairs.count (pairKey) > 0)
        {
          continue;
        }
      peeredAsnPairs.insert (pairKey);

      Peer aToB;
      aToB.local_asn = rt.spec.as_a;
      aToB.peer_asn = rt.spec.as_b;
      aToB.peer_address = rt.ip_b;
      aToB.passive = false;
      if (peer_weights)
        {
          aToB.weight = rt.spec.is_standby ? 50 : 300;
        }
      bgpApps.at (rt.spec.as_a)->AddPeer (aToB);

      Peer bToA;
      bToA.local_asn = rt.spec.as_b;
      bToA.peer_asn = rt.spec.as_a;
      bToA.peer_address = rt.ip_a;
      bToA.passive = false;
      if (peer_weights)
        {
          bToA.weight = rt.spec.is_standby ? 50 : 300;
        }
      bgpApps.at (rt.spec.as_b)->AddPeer (bToA);
    }

  // Stop BGP (and the probes) before the simulation itself ends. Bgp declares _sessions
  // ahead of _bus (src/bgp/model/bgp.h:130 vs :134), so at destruction _bus goes first and
  // ~BgpFsm then unsubscribes from an already-destroyed bus. Letting StopApplication drop
  // the sessions while the bus is still alive avoids that. scratch/bgp-ixp-scenarios.cc
  // carries the same guard as activityEndGuardS.
  const double activityEndGuardS = 5.0;
  const double routingStopS = sim_time_s - activityEndGuardS;
  for (std::map<uint16_t, Ptr<Bgp>>::iterator it = bgpApps.begin (); it != bgpApps.end (); ++it)
    {
      it->second->SetStartTime (Seconds (1.0));
      it->second->SetStopTime (Seconds (routingStopS));
    }

  std::map<uint16_t, Ptr<Bgp>>::const_iterator bgpIt;
  for (bgpIt = bgpApps.begin (); bgpIt != bgpApps.end (); ++bgpIt)
    {
      AddRoutesForAllInterfaces (bgpIt->second, asNodes.at (bgpIt->first));
    }

  // The probe destination is the first address on each node, matching the convention in
  // scratch/bgp-ixp-scenarios.cc.
  std::map<uint16_t, Ipv4Address> probeIpPerAs;
  for (std::map<uint16_t, Ptr<Node>>::const_iterator it = asNodes.begin (); it != asNodes.end ();
       ++it)
    {
      Ptr<Ipv4> ipv4 = it->second->GetObject<Ipv4> ();
      Ipv4Address firstAddr = Ipv4Address::GetAny ();
      for (uint32_t iface = 1; iface < ipv4->GetNInterfaces (); ++iface)
        {
          if (ipv4->GetNAddresses (iface) == 0)
            {
              continue;
            }
          firstAddr = ipv4->GetAddress (iface, 0).GetLocal ();
          break;
        }
      probeIpPerAs[it->first] = firstAddr;
    }

  const uint16_t probePort = 9000;
  for (std::map<uint16_t, Ptr<Node>>::const_iterator it = asNodes.begin (); it != asNodes.end ();
       ++it)
    {
      Ptr<UdpEchoResponder> responder = CreateObject<UdpEchoResponder> (probePort);
      it->second->AddApplication (responder);
      responder->SetStartTime (Seconds (5.0));
      responder->SetStopTime (Seconds (routingStopS));
    }

  const double probeStartS = 10.0;
  const double probeIntervalS = 1.0;
  const double probeTimeoutS = 2.0;
  const uint32_t probeCount =
      static_cast<uint32_t> ((routingStopS - probeStartS) / probeIntervalS);

  std::vector<std::pair<uint16_t, uint16_t>> probePairs;
  probePairs.push_back (std::make_pair (kAsSrc, kAsIxp)); // control, directly connected
  probePairs.push_back (std::make_pair (kAsSrc, kAsDst)); // subject, transit via the IXP

  for (uint32_t i = 0; i < probePairs.size (); ++i)
    {
      uint16_t srcAs = probePairs.at (i).first;
      uint16_t dstAs = probePairs.at (i).second;

      Ptr<UdpProbeApp> probe = CreateObject<UdpProbeApp> ();
      std::ostringstream out;
      out << outDir << "/probe_" << srcAs << "_" << dstAs << ".csv";
      probe->Configure (srcAs, dstAs, probeIpPerAs.at (dstAs), probePort, probeCount,
                        Seconds (probeIntervalS), Seconds (probeTimeoutS), out.str ());
      asNodes.at (srcAs)->AddApplication (probe);
      probe->SetStartTime (Seconds (probeStartS + static_cast<double> (i) * 0.001));
      probe->SetStopTime (Seconds (routingStopS));
    }

  if (dump_rib_at_s > 0.0)
    {
      std::ostringstream ribPath;
      ribPath << outDir << "/rib.txt";
      Ptr<OutputStreamWrapper> ribStream = Create<OutputStreamWrapper> (ribPath.str (), std::ios::out);
      Ipv4RoutingHelper::PrintRoutingTableAllAt (Seconds (dump_rib_at_s), ribStream);
    }

  Simulator::Stop (Seconds (sim_time_s));
  Simulator::Run ();

  // Release the local handles on the BGP applications before Simulator::Destroy(), so the
  // libbgp session objects are not torn down a second time when this scope exits.
  bgpApps.clear ();
  asNodes.clear ();
  runtimes.clear ();

  Simulator::Destroy ();

  return 0;
}
