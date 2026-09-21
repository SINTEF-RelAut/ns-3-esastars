/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include <arpa/inet.h>

#include <errno.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <random>
#include <regex>
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
#include "ns3/output-stream-wrapper.h"
#include "ns3/ipv4.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv4-interface-address.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/socket.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/v4traceroute-helper.h"
#include "ns3/virtual-ixp-fabric.h"

#include "ns3/bgp.h"

using namespace ns3;

namespace {

struct LinkSpec
{
  uint16_t as_a;
  uint16_t as_b;
  double latency_s;
  uint32_t capacity_mbps;
  uint32_t if_id_a;
  uint32_t if_id_b;
  bool ixp_managed;
};

struct LinkRuntime
{
  LinkSpec spec;
  Ptr<Node> node_a;
  Ptr<Node> node_b;
  Ptr<NetDevice> dev_a;
  Ptr<NetDevice> dev_b;
  uint32_t iface_a;
  uint32_t iface_b;
  Ipv4Address ip_a;
  Ipv4Address ip_b;
};

struct StochasticLatencyParams
{
  bool enabled = true;
  double R_eff_m = 1.4e6; // 1400 km
  double alpha = 1.3;
  double mu_link_s = 5.5e-3; // 5.5 ms
  double sigma_link_s = 1.2e-3; // 1.2 ms
  double delta_s = 0.4e-3; // 0.4 ms
  double beta = 0.08;
  double omega_rad_s = 2.0 * M_PI / 1500.0;
  double R_L_m = 600000.0;
  double R_M_m = 6000000.0;
  uint32_t P = 6;
  uint32_t N_p = 12;
};

uint32_t
GetIxpRankKForLink (const LinkSpec &spec)
{
  // k=1 when closest/preferred IXP link is used; k=2 for fallback IXP. The rank is a property
  // of the exchange *location*, not of the individual fabric AS: with --splitFabricPerIxp the
  // backup fabric AS123 sits on the same satellite as AS121, so it must draw from the same
  // latency distribution. Testing only for 121 would sample AS123 as a near link while its
  // primary sampled as far, making a backup cable look better than the main one.
  if (spec.as_a == 121 || spec.as_b == 121 || spec.as_a == 123 || spec.as_b == 123)
    {
      return 2;
    }
  return 1;
}

bool
IsDirectPeerLink (const LinkSpec &spec)
{
  return spec.as_a >= 102 && spec.as_a <= 105 && spec.as_b >= 102 && spec.as_b <= 105;
}

uint32_t
GetDirectRankKForLink (const LinkSpec &spec)
{
  // Direct-link IF IDs encode location in the hundreds digit of the last three digits:
  // loc A => 1 (preferred, k=1), loc B => 2 (fallback, k=2). The direct mesh is single-location
  // (see BuildTopologyLinksDirect), so this returns 1 for every cable it is currently asked
  // about; the loc-B arm is kept because the ID scheme still encodes it.
  auto RankFromIfId = [] (uint32_t if_id) -> uint32_t {
    uint32_t loc = (if_id / 100) % 10;
    return (loc == 2) ? 2 : 1;
  };

  return std::max (RankFromIfId (spec.if_id_a), RankFromIfId (spec.if_id_b));
}

uint32_t
GetStochasticRankKForLink (const LinkSpec &spec)
{
  if (spec.ixp_managed)
    {
      return GetIxpRankKForLink (spec);
    }
  if (IsDirectPeerLink (spec))
    {
      return GetDirectRankKForLink (spec);
    }
  return 1;
}

double
SampleIxpLatencySeconds (uint32_t k, double now_s, const StochasticLatencyParams &p)
{
  static std::mt19937 rng (4242);

  const double phi_max = M_PI / (2.0 * static_cast<double> (std::max<uint32_t> (1, p.P)));
  const double theta_min =
      (static_cast<double> (k) - 1.0) * M_PI / static_cast<double> (std::max<uint32_t> (1, p.N_p));
  const double theta_max =
      static_cast<double> (k) * M_PI / static_cast<double> (std::max<uint32_t> (1, p.N_p));

  std::uniform_real_distribution<double> uni_phi (0.0, phi_max);
  std::uniform_real_distribution<double> uni_theta (theta_min, theta_max);
  std::uniform_real_distribution<double> uni_phase (0.0, 2.0 * M_PI);

  const double phi_p = uni_phi (rng);
  const double theta_k = uni_theta (rng);
  const double delta_angle = std::sqrt (phi_p * phi_p + theta_k * theta_k);
  const double D = std::sqrt (p.R_L_m * p.R_L_m + p.R_M_m * p.R_M_m -
                              2.0 * p.R_L_m * p.R_M_m * std::cos (delta_angle));

  const double lambda = p.alpha * (D / p.R_eff_m);
  std::poisson_distribution<uint32_t> poisson_hops (std::max (0.0, lambda));
  uint32_t H = poisson_hops (rng);
  if (H == 0)
    {
      H = 1;
    }

  double total_latency = 0.0;
  for (uint32_t i = 0; i < H; ++i)
    {
      const double phase = uni_phase (rng);
      const double periodic = p.beta * std::sin (p.omega_rad_s * now_s + phase);
      const double link_mean = p.mu_link_s * (1.0 + periodic);
      std::normal_distribution<double> normal_link (link_mean, p.sigma_link_s);
      const double link_delay = std::max (0.0, normal_link (rng));
      total_latency += (link_delay + p.delta_s);
    }

  if (!std::isfinite (total_latency))
    {
      return std::max (1e-6, p.mu_link_s + p.delta_s);
    }
  return std::max (1e-6, total_latency);
}

uint16_t
NormalizeEdgeAs (uint16_t asn)
{
  if (asn >= 1020 && asn <= 1051 && (asn % 10 == 0 || asn % 10 == 1))
    {
      return static_cast<uint16_t> (asn / 10);
    }
  return asn;
}

bool
IsSplitEdgeAs (uint16_t asn)
{
  return asn >= 1020 && asn <= 1051 && (asn % 10 == 0 || asn % 10 == 1);
}

bool
Prefers120Location (uint16_t asn)
{
  if (IsSplitEdgeAs (asn))
    {
      return (asn % 10) == 0;
    }

  return true;
}

uint16_t
MakeSplitAsn (uint16_t base_as, bool prefer120)
{
  return static_cast<uint16_t> (base_as * 10 + (prefer120 ? 0 : 1));
}

// An IXP "location" is one exchange satellite. With --splitFabricPerIxp each location hosts two
// fabric ASes on the same satellite — a primary and a backup — so that a handover between the
// two cables of an adjacency changes the peer ASN and therefore the peer router ID. That is what
// makes the handover visible to BGP without a libbgp change: libbgp scopes its RIB by peer router
// ID, so two sessions to the *same* peer node share a scope and an ESTABLISHED exit on one wipes
// the other's routes.
const uint16_t kIxpLocationNone = 0;

// Ground/customer ASes. These attach to satellite ASes only; see the topology builders for why
// no two of them may be cabled together.
bool
IsGroundAs (uint16_t asn)
{
  return asn == 101 || asn == 106 || asn == 107 || asn == 108;
}

bool
IsIxpFabricAs (uint16_t asn)
{
  return asn >= 120 && asn <= 123;
}

bool
IsPrimaryFabricAs (uint16_t asn)
{
  return asn == 120 || asn == 121;
}

// Collapses a fabric ASN to the exchange satellite it sits on. AS110 is the single-IXP
// topology's only exchange and maps to itself, which is what keeps the standby-cable derivation
// below working unchanged for that topology.
uint16_t
IxpLocationOf (uint16_t ixp_as)
{
  if (ixp_as == 110)
    {
      return 110;
    }
  if (ixp_as == 120 || ixp_as == 122)
    {
      return 120;
    }
  if (ixp_as == 121 || ixp_as == 123)
    {
      return 121;
    }
  return kIxpLocationNone;
}

uint16_t
PrimaryFabricForLocation (bool location_a)
{
  return location_a ? 120 : 121;
}

uint16_t
BackupFabricForLocation (bool location_a)
{
  return location_a ? 122 : 123;
}

int32_t
GetDualIxpInterfaceWeight (uint16_t local_as, uint16_t peer_as, uint32_t local_if_id,
                           bool single_link_per_pair)
{
  uint16_t local_base = NormalizeEdgeAs (local_as);
  uint16_t peer_base = NormalizeEdgeAs (peer_as);

  if (!(IsIxpFabricAs (peer_as) && (local_base >= 102 && local_base <= 105)) &&
      !(IsIxpFabricAs (local_as) && (peer_base >= 102 && peer_base <= 105)))
    {
      return 0;
    }

  // In split-edge mode, enforce location affinity per split half, and — when the location hosts
  // two fabric ASes — primary fabric over backup fabric. Weight is compared before AS_PATH
  // length in libbgp's BgpRibEntry::operator> (bgp-rib.h:115-117), so this ordering decides the
  // path outright: the half uses its own location while any cable there is up, and only falls
  // back to the 20ms internal link to its sibling half when both local cables are down.
  if (IsSplitEdgeAs (local_as) && IsIxpFabricAs (peer_as))
    {
      const bool local_location_a = Prefers120Location (local_as);
      const bool peer_location_a = (IxpLocationOf (peer_as) == 120);
      if (local_location_a != peer_location_a)
        {
          // No topology builds such a cable; defensive only.
          return 50;
        }
      return IsPrimaryFabricAs (peer_as) ? 300 : 200;
    }

  uint32_t slot = local_if_id % 10;

  // With one cable per satellite-IXP pair the only surviving satellite-side slots are 1
  // (towards AS120) and 3 (towards AS121). The normal rule calls both "primary" and hands
  // them the same weight, which would leave no configured preference between the two IXPs.
  // Here AS120 is the nearer, preferred IXP and AS121 the backup carrying the path stretch,
  // so the handover has a defined direction.
  if (single_link_per_pair)
    {
      if (local_base >= 102 && local_base <= 105)
        {
          return (slot == 1) ? 200 : 100;
        }
      return (local_as == 120) ? 200 : 100;
    }

  bool primary = false;
  if (local_base >= 102 && local_base <= 105)
    {
      primary = (slot == 1 || slot == 3);
    }
  else
    {
      primary = (slot % 2 == 0);
    }

  return primary ? 200 : 100;
}

// Quality of a satellite-to-IXP cable, used by the virtual-fabric scenario to rank candidates.
// Derived from the peer fabric AS when the caller knows it, so the values stay correct when a
// location hosts two fabric ASes; the if_id digit rule is kept as the fallback because it is what
// every existing topology encodes (slot 1/2 = location A main/backup, 3/4 = location B).
double
GetDualIxpQuality (uint32_t if_id, uint16_t peer_fabric_as = 0)
{
  if (IsIxpFabricAs (peer_fabric_as))
    {
      const bool location_a = (IxpLocationOf (peer_fabric_as) == 120);
      if (IsPrimaryFabricAs (peer_fabric_as))
        {
          return location_a ? 1.0 : 0.95;
        }
      return location_a ? 0.8 : 0.75;
    }

  switch (if_id % 10)
    {
    case 1:
      return 1.0; // AS120 primary interface
    case 2:
      return 0.8; // AS120 backup interface
    case 3:
      return 0.95; // AS121 primary interface
    case 4:
      return 0.75; // AS121 backup interface
    default:
      return 1.0;
    }
}

// Weight for direct peer links between AS102-105 at two geographic locations.
// if_id encoding:  1XXYYZZ  — hundreds digit of last 3 = location (1=A, 2=B);
//                            units digit = 0 primary, 1 backup.
// Location preference: AS102/103 prefer loc A; AS104/105 prefer loc B.
int32_t
GetDirectLinkWeight (uint16_t local_as, uint16_t /*peer_as*/, uint32_t local_if_id)
{
  uint32_t loc = (local_if_id / 100) % 10; // 1 = loc A, 2 = loc B
  bool primary = (local_if_id % 2) == 0;
  bool prefers_loc_a = (local_as == 102 || local_as == 103);
  bool local_loc = (prefers_loc_a && loc == 1) || (!prefers_loc_a && loc == 2);

  if (local_loc && primary)
    return 300;
  if (local_loc && !primary)
    return 200;
  if (!local_loc && primary)
    return 150;
  return 50;
}

struct ExternalEvent
{
  double time_s;
  bool up;
  uint16_t asn;
  uint32_t if_id;
};

struct HiddenIxpLinkBinding
{
  Ptr<VirtualIxpFabric> fabric;
  IxpLinkId linkId;
  double quality;
  uint16_t remoteIxpAs;
};

class BgpHiddenIxpEndpointAdapter : public IxpEndpointAdapter
{
public:
  static TypeId
  GetTypeId (void)
  {
    static TypeId tid = TypeId ("ns3::BgpHiddenIxpEndpointAdapter")
                            .SetParent<IxpEndpointAdapter> ()
                            .AddConstructor<BgpHiddenIxpEndpointAdapter> ();
    return tid;
  }

  BgpHiddenIxpEndpointAdapter ()
  {
  }

  ~BgpHiddenIxpEndpointAdapter () override
  {
  }

  void
  OnInterfaceUp (const IxpLinkId &link, uint32_t portId, Time now) override
  {
    (void) link;
    (void) portId;
    (void) now;
    // Hidden-mode semantics for BGP: local IXP forwarding choice changes,
    // but control-plane interface state stays stable to avoid BGP churn.
  }

  void
  OnInterfaceDown (const IxpLinkId &link, uint32_t portId, Time now) override
  {
    (void) link;
    (void) portId;
    (void) now;
    // Hidden-mode semantics for BGP: no interface down on the node.
  }

  void
  DeliverFromFabric (const IxpLinkId &dst, Ptr<Packet> packet) override
  {
    (void) dst;
    (void) packet;
  }
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
             Time interval, Time timeout, const std::string &output_path,
             Ipv4Address src_ip = Ipv4Address::GetAny ())
  {
    m_src_as = src_as;
    m_dst_as = dst_as;
    m_dst_ip = dst_ip;
    m_src_ip = src_ip;
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
    // Binding to an explicit source address pins the probe's source IP to this AS's
    // loopback, so the reply is addressed to a prefix that only this AS originates. Left
    // unbound, the source would be whichever link address the route happened to pick, and
    // the return path would depend on that link's /30 having propagated.
    if (m_src_ip != Ipv4Address::GetAny ())
      {
        m_socket->Bind (InetSocketAddress (m_src_ip, 0));
      }
    else
      {
        m_socket->Bind ();
      }
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
  Ipv4Address m_src_ip;
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

// A per-AS service address, distinct from every link /30 (those live in 10.0.x.y) and
// originated by exactly one AS. Used as the probe endpoint so reachability does not depend
// on any single cable's prefix.
Ipv4Address
MakeLoopbackAddress (uint16_t asn)
{
  std::ostringstream addr;
  addr << "10.255." << ((asn >> 8) & 0xff) << "." << (asn & 0xff);
  return Ipv4Address (addr.str ().c_str ());
}

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
InstallBgpOnNode (uint32_t asn, Ptr<Node> node, Time clockInterval, Time mrai, Time errorHold,
                  const std::string &outDir)
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
  bgp->SetAttribute ("HoldTimer", TimeValue (Seconds (3.0 * clockInterval.GetSeconds ())));
  bgp->SetAttribute ("Mrai", TimeValue (mrai));
  bgp->SetAttribute ("ClockInterval", TimeValue (clockInterval));
  // Reconnect backoff after a session error. The 45s default is a blind wait; with a known
  // orbital schedule both ends know when the link returns, so a long backoff just adds dead
  // time to every handover.
  bgp->SetAttribute ("ErrorHold", TimeValue (errorHold));
  bgp->SetAttribute ("LibbgpLogLevel", EnumValue (libbgp::FATAL));
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

void
SetLinkState (LinkRuntime *link, bool up, std::ofstream *eventsOut)
{
  Ptr<Ipv4> ipv4_a = link->node_a->GetObject<Ipv4> ();
  Ptr<Ipv4> ipv4_b = link->node_b->GetObject<Ipv4> ();

  if (up)
    {
      ipv4_a->SetUp (link->iface_a);
      ipv4_b->SetUp (link->iface_b);
    }
  else
    {
      ipv4_a->SetDown (link->iface_a);
      ipv4_b->SetDown (link->iface_b);
    }

  if (eventsOut && eventsOut->is_open ())
    {
      (*eventsOut) << std::fixed << std::setprecision (6) << Simulator::Now ().GetSeconds () << ","
                   << (up ? "link_up" : "link_down") << "," << link->spec.as_a << ","
                   << link->spec.as_b << "," << link->spec.if_id_a << std::endl;
    }
}

void
SetSatelliteIxpInterfaceState (LinkRuntime *link, bool up, std::ofstream *eventsOut)
{
  Ptr<Ipv4> ipv4_a = link->node_a->GetObject<Ipv4> ();

  if (up)
    {
      ipv4_a->SetUp (link->iface_a);
    }
  else
    {
      ipv4_a->SetDown (link->iface_a);
    }

  if (eventsOut && eventsOut->is_open ())
    {
      (*eventsOut) << std::fixed << std::setprecision (6) << Simulator::Now ().GetSeconds () << ","
                   << (up ? "link_up" : "link_down") << "," << link->spec.as_a << ","
                   << link->spec.as_b << "," << link->spec.if_id_a << std::endl;
    }
}

void
SetVirtualIxpLinkState (HiddenIxpLinkBinding binding, bool up, uint16_t asn,
                        std::ofstream *eventsOut)
{
  if (binding.fabric == 0)
    {
      return;
    }

  if (up)
    {
      binding.fabric->SetFeasibleUp (binding.linkId, binding.quality);
    }
  else
    {
      binding.fabric->SetFeasibleDown (binding.linkId);
    }

  if (eventsOut && eventsOut->is_open ())
    {
      (*eventsOut) << std::fixed << std::setprecision (6) << Simulator::Now ().GetSeconds () << ","
                   << (up ? "link_up" : "link_down") << "," << asn << "," << binding.remoteIxpAs
                   << "," << binding.linkId.linkIndex << std::endl;
    }
}

// Calibration topology, transcribed link-for-link from the known-good reference
// src/bgp/examples/bgp-convergence-first.cc:430-439, which reaches 0.00% loss on 13 of its
// 16 probe pairs including the four-AS-hop 107->108 path. Running it through THIS file's
// setup code separates "the topology is the problem" from "this file's setup is the
// problem": if transit works here, the star/parallel-link topology is at fault; if it
// fails, the defect is in the code around it.
// 8 ASes, 10 links, one link and one subnet per AS pair, no IXP node.
std::vector<LinkSpec>
BuildTopologyLinksReference ()
{
  std::vector<LinkSpec> links;
  links.push_back ((LinkSpec) {101, 102, 0.0020, 300, 1010000, 1020000, false});
  links.push_back ((LinkSpec) {102, 104, 0.0020, 300, 1020001, 1040000, false});
  links.push_back ((LinkSpec) {104, 106, 0.0020, 300, 1040001, 1060000, false});
  links.push_back ((LinkSpec) {101, 103, 0.0022, 300, 1010001, 1030000, false});
  links.push_back ((LinkSpec) {103, 105, 0.0022, 300, 1030001, 1050000, false});
  links.push_back ((LinkSpec) {105, 106, 0.0022, 300, 1050001, 1060001, false});
  links.push_back ((LinkSpec) {102, 103, 0.0012, 200, 1020002, 1030002, false});
  links.push_back ((LinkSpec) {104, 105, 0.0012, 200, 1040002, 1050002, false});
  // EXCEPTION to the no-ground-ground rule that holds everywhere else in this file. This
  // builder is a calibration control transcribed edge-for-edge from
  // src/bgp/examples/bgp-convergence-first.cc, and its only job is to be an exact match to that
  // known-good example so a failure here means "this file's setup is broken" rather than "the
  // topology is broken". Removing these two links would break that correspondence and destroy
  // the control. Do not "fix" them.
  links.push_back ((LinkSpec) {101, 107, 0.0015, 150, 1010002, 1070000, false});
  links.push_back ((LinkSpec) {106, 108, 0.0015, 150, 1060002, 1080000, false});
  return links;
}

std::vector<LinkSpec>
BuildTopologyLinksSingle ()
{
  std::vector<LinkSpec> links;
  // Ground/customer AS attachments. Each ground AS attaches to exactly one satellite AS and
  // never to another ground AS: a customer does not provide transit between two others, and a
  // ground-ground link would give a satellite pair a path that bypasses the IXP entirely, which
  // is the very thing these scenarios measure. Link order and if_ids match
  // topology/ixp_as110_dual_links_topology.xml exactly, so BGP and SCION run the same graph.
  links.push_back ((LinkSpec) {101, 102, 0.0020, 300, 1010000, 1020000, false});
  links.push_back ((LinkSpec) {103, 107, 0.0020, 300, 1030000, 1070000, false});
  links.push_back ((LinkSpec) {104, 108, 0.0020, 300, 1040000, 1080000, false});
  links.push_back ((LinkSpec) {105, 106, 0.0020, 300, 1050000, 1060001, false});
  // IXP satellite AS110
  links.push_back ((LinkSpec) {102, 110, 0.0010, 300, 1020001, 1100000, true});
  links.push_back ((LinkSpec) {102, 110, 0.0010, 300, 1020002, 1100001, true});
  links.push_back ((LinkSpec) {103, 110, 0.0010, 300, 1030001, 1100002, true});
  links.push_back ((LinkSpec) {103, 110, 0.0010, 300, 1030002, 1100003, true});
  links.push_back ((LinkSpec) {104, 110, 0.0010, 300, 1040001, 1100004, true});
  links.push_back ((LinkSpec) {104, 110, 0.0010, 300, 1040002, 1100005, true});
  links.push_back ((LinkSpec) {105, 110, 0.0010, 300, 1050001, 1100006, true});
  links.push_back ((LinkSpec) {105, 110, 0.0010, 300, 1050002, 1100007, true});
  return links;
}

std::vector<LinkSpec>
BuildTopologyLinksDual (bool split_edge_as, bool single_link_per_pair,
                        bool split_fabric_per_ixp)
{
  std::vector<LinkSpec> links;
  NS_ABORT_MSG_IF (split_fabric_per_ixp && !split_edge_as,
                   "splitFabricPerIxp requires --splitEdgeAs=1");

  // Visible-handover topology: each satellite reaches AS120 and AS121 over exactly ONE
  // cable each, so every AS pair carries a single BGP session. The handover then moves
  // between two *different* peer ASes, which have different router IDs, so it is fully
  // visible to the control plane without hitting libbgp's per-router-ID RIB scoping. The
  // satellite-side if_ids are the ones the generated dual event traces address: X0001
  // towards AS120 and X0003 towards AS121.
  if (single_link_per_pair)
    {
      NS_ABORT_MSG_IF (split_edge_as, "singleLinkPerIxpPair is incompatible with --splitEdgeAs");
      links.push_back ((LinkSpec) {101, 102, 0.0020, 300, 1010000, 1020000, false});
      links.push_back ((LinkSpec) {105, 106, 0.0020, 300, 1050000, 1060001, false});
      links.push_back ((LinkSpec) {107, 103, 0.0015, 150, 1070000, 1030000, false});
      links.push_back ((LinkSpec) {104, 108, 0.0015, 150, 1040000, 1080001, false});
      // Primary orbit, AS120
      links.push_back ((LinkSpec) {102, 120, 0.0010, 300, 1020001, 1200000, true});
      links.push_back ((LinkSpec) {103, 120, 0.0010, 300, 1030001, 1200001, true});
      links.push_back ((LinkSpec) {104, 120, 0.0010, 300, 1040001, 1200002, true});
      links.push_back ((LinkSpec) {105, 120, 0.0010, 300, 1050001, 1200003, true});
      // Backup orbit, AS121
      links.push_back ((LinkSpec) {102, 121, 0.0010, 300, 1020003, 1210000, true});
      links.push_back ((LinkSpec) {103, 121, 0.0010, 300, 1030003, 1210001, true});
      links.push_back ((LinkSpec) {104, 121, 0.0010, 300, 1040003, 1210002, true});
      links.push_back ((LinkSpec) {105, 121, 0.0010, 300, 1050003, 1210003, true});
      // Inter-IXP link. Without it a satellite attached to AS120 cannot reach one attached
      // to AS121 at all, and because each satellite hands over independently the two are on
      // different IXPs roughly half the time. That turns the measurement into one of
      // topological disconnection rather than of BGP reconvergence. Two IXP satellites in a
      // constellation would carry an inter-satellite link, so this restores the invariant
      // that every satellite can always reach every other.
      links.push_back ((LinkSpec) {120, 121, 0.0010, 300, 1200004, 1210004, false});
      return links;
    }

  if (!split_edge_as)
    {
      links.push_back ((LinkSpec) {101, 102, 0.0020, 300, 1010000, 1020000, false});
      links.push_back ((LinkSpec) {105, 106, 0.0020, 300, 1050000, 1060001, false});
      links.push_back ((LinkSpec) {107, 103, 0.0015, 150, 1070000, 1030000, false});
      // Omitted deliberately: 108 and 106 are both ground/customer ASes and no topology in
      // this file connects two of them. topology/ixp_as110_dual_satellites_topology.xml has its
      // 108-106 block commented out for the same reason, so omitting it here keeps mode B an
      // exact 20-link edge-multiset match against that file.
      links.push_back ((LinkSpec) {104, 108, 0.0015, 150, 1040000, 1080001, false});
      // IXP satellite A (AS120)
      links.push_back ((LinkSpec) {102, 120, 0.0010, 300, 1020001, 1200000, true});
      links.push_back ((LinkSpec) {102, 120, 0.0010, 300, 1020002, 1200001, true});
      links.push_back ((LinkSpec) {103, 120, 0.0010, 300, 1030001, 1200002, true});
      links.push_back ((LinkSpec) {103, 120, 0.0010, 300, 1030002, 1200003, true});
      links.push_back ((LinkSpec) {104, 120, 0.0010, 300, 1040001, 1200004, true});
      links.push_back ((LinkSpec) {104, 120, 0.0010, 300, 1040002, 1200005, true});
      links.push_back ((LinkSpec) {105, 120, 0.0010, 300, 1050001, 1200006, true});
      links.push_back ((LinkSpec) {105, 120, 0.0010, 300, 1050002, 1200007, true});
      // IXP satellite B (AS121)
      links.push_back ((LinkSpec) {102, 121, 0.0010, 300, 1020003, 1210000, true});
      links.push_back ((LinkSpec) {102, 121, 0.0010, 300, 1020004, 1210001, true});
      links.push_back ((LinkSpec) {103, 121, 0.0010, 300, 1030003, 1210002, true});
      links.push_back ((LinkSpec) {103, 121, 0.0010, 300, 1030004, 1210003, true});
      links.push_back ((LinkSpec) {104, 121, 0.0010, 300, 1040003, 1210004, true});
      links.push_back ((LinkSpec) {104, 121, 0.0010, 300, 1040004, 1210005, true});
      links.push_back ((LinkSpec) {105, 121, 0.0010, 300, 1050003, 1210006, true});
      links.push_back ((LinkSpec) {105, 121, 0.0010, 300, 1050004, 1210007, true});
      return links;
    }

  const uint16_t as102_120 = MakeSplitAsn (102, true);
  const uint16_t as102_121 = MakeSplitAsn (102, false);
  const uint16_t as103_120 = MakeSplitAsn (103, true);
  const uint16_t as103_121 = MakeSplitAsn (103, false);
  const uint16_t as104_120 = MakeSplitAsn (104, true);
  const uint16_t as104_121 = MakeSplitAsn (104, false);
  const uint16_t as105_120 = MakeSplitAsn (105, true);
  const uint16_t as105_121 = MakeSplitAsn (105, false);

  // Ground/customer AS attachments. Each ground AS is dual-homed to the two halves of one
  // constellation, one attachment per IXP location, and never to another ground AS: a customer
  // does not provide transit between two others, and a ground-ground link would hand a satellite
  // pair a path bypassing the IXP fabric entirely. The slightly higher latency on the
  // location-B leg reflects AS101/AS106 sitting nearer location A and AS107/AS108 nearer B.
  links.push_back ((LinkSpec) {101, as102_120, 0.0020, 300, 1010000, 1020000, false});
  links.push_back ((LinkSpec) {101, as102_121, 0.0023, 300, 1010003, 1021000, false});
  links.push_back ((LinkSpec) {107, as103_121, 0.0015, 150, 1070000, 1031000, false});
  links.push_back ((LinkSpec) {107, as103_120, 0.0018, 150, 1070001, 1030000, false});
  links.push_back ((LinkSpec) {as105_120, 106, 0.0020, 300, 1050000, 1060001, false});
  links.push_back ((LinkSpec) {as105_121, 106, 0.0023, 300, 1051000, 1060003, false});
  links.push_back ((LinkSpec) {as104_121, 108, 0.0015, 150, 1041000, 1080001, false});
  links.push_back ((LinkSpec) {as104_120, 108, 0.0018, 150, 1040000, 1080002, false});

  // Internal split links (model inter-satellite path between IXP locations far apart).
  links.push_back ((LinkSpec) {as102_120, as102_121, 0.020, 1000, 1020010, 1021010, false});
  links.push_back ((LinkSpec) {as103_120, as103_121, 0.020, 1000, 1030010, 1031010, false});
  links.push_back ((LinkSpec) {as104_120, as104_121, 0.020, 1000, 1040010, 1041010, false});
  links.push_back ((LinkSpec) {as105_120, as105_121, 0.020, 1000, 1050010, 1051010, false});

  // Satellite-to-IXP cables. Each half has a MAIN and a BACKUP cable to its own exchange
  // location and the handover toggles between those two; a half never cables to the other
  // location. The satellite-side if_id slots are frozen — X0001/X0002 are location A main and
  // backup, X0003/X0004 location B — because every generated event trace addresses them.
  //
  // split_fabric_per_ixp decides where the BACKUP cable lands, and that single choice is the
  // whole difference between a handover hidden from BGP and one visible to it:
  //
  //   false  backup lands on the SAME fabric AS as the main. Both cables are one AS pair, so
  //          sharedPairSubnet gives them one /30 and the peering loop one session; the switch
  //          happens below IP and the session survives.
  //   true   backup lands on the location's second fabric AS (A: AS120 -> AS122,
  //          B: AS121 -> AS123), a separate node with its own router ID. Two AS pairs, two
  //          /30s, two sessions, so the handover changes peer ASN and is visible. Distinct
  //          router IDs are what keeps libbgp's per-peer RIB scoping from wiping the surviving
  //          session's routes.
  //
  // The fabric-side port index keeps its original parity rather than being renumbered densely,
  // so 1200001/1200003/1200005/1200007 simply do not exist when the backups move to AS122. A
  // trace written for the other variant is then skipped by the if_id guard instead of silently
  // toggling a different satellite's cable.
  const uint16_t loc_a_backup_as = split_fabric_per_ixp ? BackupFabricForLocation (true) : 120;
  const uint16_t loc_b_backup_as = split_fabric_per_ixp ? BackupFabricForLocation (false) : 121;
  const uint32_t loc_a_backup_port = split_fabric_per_ixp ? 1220000 : 1200000;
  const uint32_t loc_b_backup_port = split_fabric_per_ixp ? 1230000 : 1210000;

  const uint16_t loc_a_halves[4] = {as102_120, as103_120, as104_120, as105_120};
  const uint16_t loc_b_halves[4] = {as102_121, as103_121, as104_121, as105_121};
  const uint16_t base_as[4] = {102, 103, 104, 105};

  // IXP location A: main cables to AS120, backups to AS120 or AS122.
  for (uint32_t i = 0; i < 4; ++i)
    {
      const uint32_t sat_if = static_cast<uint32_t> (base_as[i]) * 10000;
      links.push_back ((LinkSpec) {loc_a_halves[i], 120, 0.0010, 300, sat_if + 1,
                                   1200000 + 2 * i, true});
      links.push_back ((LinkSpec) {loc_a_halves[i], loc_a_backup_as, 0.0010, 300, sat_if + 2,
                                   loc_a_backup_port + 2 * i + 1, true});
    }

  // IXP location B: main cables to AS121, backups to AS121 or AS123.
  for (uint32_t i = 0; i < 4; ++i)
    {
      const uint32_t sat_if = static_cast<uint32_t> (base_as[i]) * 10000;
      links.push_back ((LinkSpec) {loc_b_halves[i], 121, 0.0010, 300, sat_if + 3,
                                   1210000 + 2 * i, true});
      links.push_back ((LinkSpec) {loc_b_halves[i], loc_b_backup_as, 0.0010, 300, sat_if + 4,
                                   loc_b_backup_port + 2 * i + 1, true});
    }

  if (split_fabric_per_ixp)
    {
      // The two fabric ASes of one location sit on the same exchange satellite, so this is a
      // short intra-site cable, not an inter-satellite link. It is required: halves hand over
      // independently, so a half already on AS122 must still reach one still on AS120 without
      // detouring through the other location. ixp_managed is false so the stochastic
      // inter-satellite latency model leaves it alone, matching how the AS120-AS121 cable is
      // built in the single-link topology.
      links.push_back ((LinkSpec) {120, 122, 0.0005, 1000, 1200008, 1220008, false});
      links.push_back ((LinkSpec) {121, 123, 0.0005, 1000, 1210008, 1230008, false});
    }

  // NOTE: there is deliberately no cable between location A and location B. The two exchange
  // satellites are on opposite sides of Earth and are not interconnected; the only path between
  // them is the 20ms internal link inside each constellation, which is what supplies the path
  // stretch this scenario measures.

  return links;
}

// Direct peer-link topology: one full mesh among AS102-105, main + standby per pair.
// if_id scheme:  main units=0, standby units=1; hundreds digit of last 3 = location (1).
// Backbone links identical to dual-IXP (no IXP satellite nodes).
//
// SINGLE-LOCATION, deliberately. This mesh was built at two geographic locations, giving each
// AS pair four cables and two concurrent adjacencies. libbgp scopes its RIB by peer router ID
// (BgpRib4::insert/discard/lookup take a src_router_id) and router IDs are per node, so two
// concurrent sessions between the same AS pair share one scope and overwrite each other. The
// two-location mesh is therefore not representable here: peering both adjacencies blackholed
// AS102-AS105 and AS103-AS105 at 97-100% probe loss with no churn at all, while peering only
// one left every location-B handover invisible to BGP -- zero control-plane events for half
// the generated churn.
//
// One location gives each pair exactly one adjacency and one session, the same shape as the
// single-IXP family, and keeps the full six-pair mesh that makes this the control case for
// what an exchange satellite buys. Restoring a second location needs a router ID per location,
// which means a BGP speaker per location per node and redistribution between them.
std::vector<LinkSpec>
BuildTopologyLinksDirect ()
{
  std::vector<LinkSpec> links;

  // Backbone (same as dual-IXP non-split)
  links.push_back ((LinkSpec) {101, 102, 0.0020, 300, 1010000, 1020000, false});
  links.push_back ((LinkSpec) {105, 106, 0.0020, 300, 1050000, 1060001, false});
  links.push_back ((LinkSpec) {107, 103, 0.0015, 150, 1070000, 1030000, false});
  // Omitted deliberately: 108 and 106 are both ground/customer ASes, and no topology in this
  // file connects two ground ASes.
  links.push_back ((LinkSpec) {104, 108, 0.0015, 150, 1040000, 1080001, false});

  // Full mesh — 6 pairs × 2 cables (main + standby)
  links.push_back ((LinkSpec) {102, 103, 0.0010, 300, 1020100, 1030100, false}); // main
  links.push_back ((LinkSpec) {102, 103, 0.0010, 300, 1020101, 1030101, false}); // standby
  links.push_back ((LinkSpec) {102, 104, 0.0010, 300, 1020110, 1040100, false});
  links.push_back ((LinkSpec) {102, 104, 0.0010, 300, 1020111, 1040101, false});
  links.push_back ((LinkSpec) {102, 105, 0.0010, 300, 1020120, 1050100, false});
  links.push_back ((LinkSpec) {102, 105, 0.0010, 300, 1020121, 1050101, false});
  links.push_back ((LinkSpec) {103, 104, 0.0010, 300, 1030110, 1040110, false});
  links.push_back ((LinkSpec) {103, 104, 0.0010, 300, 1030111, 1040111, false});
  links.push_back ((LinkSpec) {103, 105, 0.0010, 300, 1030120, 1050110, false});
  links.push_back ((LinkSpec) {103, 105, 0.0010, 300, 1030121, 1050111, false});
  links.push_back ((LinkSpec) {104, 105, 0.0010, 300, 1040120, 1050120, false});
  links.push_back ((LinkSpec) {104, 105, 0.0010, 300, 1040121, 1050121, false});

  return links;
}

std::vector<LinkSpec>
BuildTopologyLinks ()
{
  // Default to single IXP satellite (AS110)
  return BuildTopologyLinksSingle ();
}

std::vector<ExternalEvent>
LoadExternalEvents (const std::string &jsonPath)
{
  std::ifstream in (jsonPath.c_str ());
  if (!in.is_open ())
    {
      NS_ABORT_MSG ("Failed to open event file: " << jsonPath);
    }

  std::vector<ExternalEvent> out;
  std::regex timeRegex ("\"time\"\\s*:\\s*\"([0-9]+(\\.[0-9]+)?)s\"");
  std::regex typeRegex ("\"type\"\\s*:\\s*\"(link_up|link_down)\"", std::regex::icase);
  std::regex numRegex ("\"([0-9]+)\"");

  bool hasTime = false;
  bool hasType = false;
  bool inArgs = false;
  double currTime = 0.0;
  std::string currType;
  std::vector<uint32_t> currNums;

  std::string line;
  while (std::getline (in, line))
    {
      std::smatch m;

      if (std::regex_search (line, m, timeRegex))
        {
          currTime = std::stod (m[1].str ());
          hasTime = true;
        }

      if (std::regex_search (line, m, typeRegex))
        {
          currType = m[1].str ();
          hasType = true;
        }

      if (line.find ("\"args\"") != std::string::npos)
        {
          inArgs = true;
          currNums.clear ();
        }

      if (inArgs)
        {
          std::sregex_iterator nit (line.begin (), line.end (), numRegex);
          std::sregex_iterator end;
          for (; nit != end; ++nit)
            {
              currNums.push_back (static_cast<uint32_t> (std::stoul ((*nit)[1].str ())));
            }

          if (line.find ("]") != std::string::npos)
            {
              inArgs = false;
              if (hasTime && hasType && currNums.size () >= 3)
                {
                  ExternalEvent ev;
                  ev.time_s = currTime;
                  ev.up = (currType == "link_up" || currType == "LINK_UP");
                  ev.asn = static_cast<uint16_t> (currNums[1]);
                  ev.if_id = currNums[2];
                  out.push_back (ev);
                }

              hasTime = false;
              hasType = false;
              currNums.clear ();
            }
        }
    }

  std::sort (out.begin (), out.end (),
             [] (const ExternalEvent &a, const ExternalEvent &b) { return a.time_s < b.time_s; });
  return out;
}

} // namespace

int
main (int argc, char *argv[])
{
  Time::SetResolution (Time::NS);

  std::string outDir = "build/bgp_ixp_scenarios";
  std::string scenario = "visible";
  double clock_interval_s = 1.0;
  double mrai_s = 0.0;
  double sim_time_s = 260.0;
  uint32_t virtual_ixp_port_count = 4;
  double virtual_ixp_rebalance_s = 1.0;
  double virtual_ixp_hold_down_s = 0.0;
  std::string event_file;
  bool verbose = true;
  bool dual_ixp = false;
  bool split_edge_as = false;
  bool direct_links = false;
  bool backup_links_down_at_start = true;
  bool loopback_probe_targets = false;
  uint32_t bgp_sessions_per_pair = 1;
  bool shared_pair_subnet = true;
  bool reference_topology = false;
  bool symmetric_link_events = true;
  bool single_link_per_ixp_pair = false;
  bool split_fabric_per_ixp = false;
  std::string dump_topology_path = "";
  double error_hold_s = 45.0;
  double dump_rib_at_s = 0.0;
  bool stochastic_latency_model = true;
  StochasticLatencyParams stochastic_params;

  CommandLine cmd;
  cmd.AddValue ("outDir", "Output directory for probe and event CSV files", outDir);
  cmd.AddValue ("scenario", "Scenario ID: visible or hidden", scenario);
  cmd.AddValue ("clockInterval", "BGP FSM clock interval in seconds", clock_interval_s);
  cmd.AddValue ("mrai", "Minimum Route Advertisement Interval for UPDATE pacing (seconds)", mrai_s);
  cmd.AddValue ("simTime", "Simulation duration in seconds", sim_time_s);
  cmd.AddValue ("virtualIxpPortCount", "Virtual IXP port count", virtual_ixp_port_count);
  cmd.AddValue ("virtualIxpRebalance", "Virtual IXP rebalance period (seconds)",
                virtual_ixp_rebalance_s);
  cmd.AddValue ("virtualIxpHoldDown", "Virtual IXP hold-down (seconds)", virtual_ixp_hold_down_s);
  cmd.AddValue ("eventFile",
                "Path to generated events JSON (entries with time/type/args=[isd,asn,if_id])",
                event_file);
  cmd.AddValue ("verbose", "Print scenario logs", verbose);
  cmd.AddValue ("dualIxp", "Use dual IXP satellites (AS120/121) instead of single (AS110)",
                dual_ixp);
  cmd.AddValue ("splitEdgeAs",
                "Split AS102-105 into two edge routers per AS (x0 prefers AS120, x1 prefers AS121)",
                split_edge_as);
  cmd.AddValue ("errorHold",
                "BGP reconnect backoff after a session error, in seconds (libbgp default 45)",
                error_hold_s);
  cmd.AddValue ("singleLinkPerIxpPair",
                "Dual-IXP visible-handover topology: one cable from each satellite to AS120 and "
                "one to AS121, so a handover moves between two distinct peer ASes and is visible "
                "to BGP. Requires --dualIxp=1.",
                single_link_per_ixp_pair);
  cmd.AddValue ("splitFabricPerIxp",
                "Split-edge topology with TWO fabric ASes per exchange satellite (location A: "
                "AS120 primary + AS122 backup; location B: AS121 + AS123), joined by a short "
                "intra-site link. Each satellite half's main cable lands on the primary fabric "
                "and its backup cable on the backup fabric, so a handover changes peer ASN and "
                "peer router ID and is visible to BGP. With 0, both cables land on the same "
                "fabric AS, share one /30 and one session, and the handover is hidden below IP. "
                "Requires --dualIxp=1 --splitEdgeAs=1.",
                split_fabric_per_ixp);
  cmd.AddValue ("dumpTopology",
                "Write the built link table to this CSV and exit without simulating. Used to "
                "check that the BGP topology and its SCION XML counterpart are the same graph, "
                "on interface IDs and latencies rather than just on edges.",
                dump_topology_path);
  cmd.AddValue ("symmetricLinkEvents",
                "Apply each link event to both endpoints. Set 0 for the old behaviour where "
                "only the satellite-side interface was toggled and the IXP end stayed up.",
                symmetric_link_events);
  cmd.AddValue ("referenceTopology",
                "Run the known-good bgp-convergence-first topology (8 ASes, 10 links, no IXP) "
                "through this program's setup code, as a calibration control",
                reference_topology);
  cmd.AddValue ("sharedPairSubnet",
                "Give both parallel cables of an AS pair one subnet and the same addresses, so "
                "only one prefix exists per adjacency (requires --backupLinksDownAtStart)",
                shared_pair_subnet);
  cmd.AddValue ("bgpSessionsPerPair",
                "BGP sessions per ASN pair. 1 peers once per pair (the second parallel cable is "
                "a physical standby); 2 restores the old one-session-per-cable behaviour.",
                bgp_sessions_per_pair);
  cmd.AddValue ("loopbackProbeTargets",
                "Probe a per-AS /32 service address (10.255.<asn>) instead of the first link "
                "address, and bind the probe source to it. Set 0 to reproduce older runs.",
                loopback_probe_targets);
  cmd.AddValue ("dumpRibAt",
                "If >0, write every node's routing table to <outDir>/rib.txt at this time",
                dump_rib_at_s);
  cmd.AddValue ("backupLinksDownAtStart",
                "Take each satellite's second (backup) IXP link fully down at t=0.1s, leaving one "
                "active link per satellite-IXP AS pair",
                backup_links_down_at_start);
  cmd.AddValue ("directLinks",
                "Use direct peer links between AS102-105 at two geographic locations (no IXP)",
                direct_links);
  cmd.AddValue ("stochasticLatencyModel", "Enable stochastic IXP/direct latency model",
                stochastic_latency_model);
  cmd.AddValue ("stochasticR_eff_m", "Effective ISL reach in metres", stochastic_params.R_eff_m);
  cmd.AddValue ("stochasticAlpha", "Routing inefficiency factor", stochastic_params.alpha);
  cmd.AddValue ("stochasticMuLink_s", "Mean per-hop link delay in seconds",
                stochastic_params.mu_link_s);
  cmd.AddValue ("stochasticSigmaLink_s", "Per-hop delay jitter in seconds",
                stochastic_params.sigma_link_s);
  cmd.AddValue ("stochasticDelta_s", "Per-hop processing overhead in seconds",
                stochastic_params.delta_s);
  cmd.AddValue ("stochasticBeta", "Sinusoidal variation amplitude", stochastic_params.beta);
  cmd.AddValue ("stochasticOmega_rad_s", "Sinusoidal angular frequency in rad/s",
                stochastic_params.omega_rad_s);
  cmd.AddValue ("stochasticR_L_m", "LEO orbital radius in metres", stochastic_params.R_L_m);
  cmd.AddValue ("stochasticR_M_m", "MEO orbital radius in metres", stochastic_params.R_M_m);
  cmd.AddValue ("stochasticP", "Number of orbital planes", stochastic_params.P);
  cmd.AddValue ("stochasticN_p", "Satellites per plane", stochastic_params.N_p);
  cmd.Parse (argc, argv);

  stochastic_params.enabled = stochastic_latency_model;

  if (!dual_ixp && split_edge_as)
    {
      NS_ABORT_MSG ("splitEdgeAs requires --dualIxp=1");
    }
  if (direct_links && dual_ixp)
    {
      NS_ABORT_MSG ("directLinks is incompatible with --dualIxp=1");
    }
  if (direct_links && scenario == "hidden")
    {
      NS_ABORT_MSG ("directLinks does not support the hidden scenario");
    }
  if (sim_time_s <= 10.0)
    {
      NS_ABORT_MSG ("simTime must be > 10s because routing stop time is simTime - 10s guard");
    }

  // The shared-subnet handover model assumes exactly two cables per adjacency, one live at a
  // time. The reference topology has a single cable per pair and the single-cable-per-IXP-pair
  // topology has one per fabric AS, so neither can use it.
  //
  // The direct-link topology used to be excluded here too, when it meshed AS102-105 at two
  // geographic locations and so had four cables per AS pair. It is now single-location with
  // exactly two, so it can use the model, and it must: four distinct /30s per pair, each
  // originated by both endpoints, is precisely the configuration that fails to propagate.
  if (reference_topology || single_link_per_ixp_pair)
    {
      shared_pair_subnet = false;
      backup_links_down_at_start = false;
    }
  // With a second fabric AS per location the two cables of an adjacency already terminate on
  // different ASes, so they are two AS pairs and get two /30s by construction. Sharing is not
  // just unnecessary here, it is what would hide the handover we are trying to expose.
  if (split_fabric_per_ixp)
    {
      shared_pair_subnet = false;
    }
  // The shared subnet gives both cables of an adjacency the same pair of addresses, which is
  // only safe while exactly one of them is live. The flag help has always said as much, but
  // nothing enforced it, and the failure is silent: two live interfaces with identical
  // addresses, which Ipv4AddressGenerator::TestMode stops ns-3 from even warning about. The
  // direct families now depend on this pairing, so assert it rather than documenting it.
  if (shared_pair_subnet && !backup_links_down_at_start)
    {
      NS_ABORT_MSG ("sharedPairSubnet requires --backupLinksDownAtStart=1: the shared /30 is "
                    "only safe while one cable per adjacency is live");
    }
  if (single_link_per_ixp_pair && !dual_ixp)
    {
      NS_ABORT_MSG ("singleLinkPerIxpPair requires --dualIxp=1");
    }
  if (split_fabric_per_ixp && !(dual_ixp && split_edge_as))
    {
      NS_ABORT_MSG ("splitFabricPerIxp requires --dualIxp=1 --splitEdgeAs=1");
    }
  if (split_fabric_per_ixp && single_link_per_ixp_pair)
    {
      NS_ABORT_MSG ("splitFabricPerIxp is incompatible with --singleLinkPerIxpPair");
    }
  if (split_fabric_per_ixp && scenario == "hidden")
    {
      // The virtual IXP fabric exists to hide a handover from the control plane; this topology
      // exists to expose one. Running both together is contradictory, and the per-AS active-link
      // caps the hidden scenario installs assume both cables of a half share one peer AS.
      NS_ABORT_MSG ("splitFabricPerIxp does not support --scenario=hidden");
    }

  if (!verbose)
    {
      LogComponentDisableAll (LOG_LEVEL_ALL);
    }

  std::vector<uint16_t> asIds;
  asIds.push_back (101);
  if (split_edge_as)
    {
      asIds.push_back (MakeSplitAsn (102, true));
      asIds.push_back (MakeSplitAsn (102, false));
      asIds.push_back (MakeSplitAsn (103, true));
      asIds.push_back (MakeSplitAsn (103, false));
      asIds.push_back (MakeSplitAsn (104, true));
      asIds.push_back (MakeSplitAsn (104, false));
      asIds.push_back (MakeSplitAsn (105, true));
      asIds.push_back (MakeSplitAsn (105, false));
    }
  else
    {
      asIds.push_back (102);
      asIds.push_back (103);
      asIds.push_back (104);
      asIds.push_back (105);
    }
  asIds.push_back (106);
  asIds.push_back (107);
  asIds.push_back (108);
  if (reference_topology)
    {
      outDir = outDir + "_reference_" + scenario;
    }
  else if (direct_links)
    {
      outDir = outDir + "_direct_" + scenario;
    }
  else if (dual_ixp)
    {
      asIds.push_back (120);
      asIds.push_back (121);
      if (split_fabric_per_ixp)
        {
          // Appended after 120/121 so every existing AS keeps its ns-3 node id, which is what
          // PrintRoutingTableAllAt labels its output by.
          asIds.push_back (122);
          asIds.push_back (123);
        }
      outDir = outDir + (split_fabric_per_ixp ? "_dual_ixp_fabric2_" : "_dual_ixp_") + scenario;
    }
  else
    {
      asIds.push_back (110);
      outDir = outDir + "_single_ixp_" + scenario;
    }

  EnsureDirectory (outDir);

  std::map<uint16_t, uint32_t> asToNodeIdx;
  std::map<uint16_t, Ptr<Node>> asNodes;
  NodeContainer nodes;
  for (uint32_t i = 0; i < asIds.size (); ++i)
    {
      nodes.Create (1);
      asToNodeIdx.insert (std::make_pair (asIds.at (i), i));
      asNodes.insert (std::make_pair (asIds.at (i), nodes.Get (i)));
    }

  InternetStackHelper internet;
  internet.Install (nodes);

  std::vector<LinkRuntime> runtimes;
  std::map<uint32_t, uint32_t> ifIdToRuntimeIdx;
  std::map<uint16_t, uint32_t> nextIxpLinkIndex;
  std::map<uint32_t, IxpLinkId> ixpLinkByIfId;
  std::map<uint32_t, double> ixpQualityByIfId;
  std::map<uint32_t, uint16_t> ixpRemoteAsByIfId;

  std::vector<LinkSpec> links =
      reference_topology
          ? BuildTopologyLinksReference ()
          : (direct_links ? BuildTopologyLinksDirect ()
                          : (dual_ixp ? BuildTopologyLinksDual (split_edge_as,
                                                                single_link_per_ixp_pair,
                                                                split_fabric_per_ixp)
                                      : BuildTopologyLinksSingle ()));

  // Structural invariants, asserted rather than assumed so a future edit cannot quietly
  // reintroduce either. Both are load-bearing for what these scenarios claim to measure.
  for (uint32_t i = 0; i < links.size (); ++i)
    {
      const LinkSpec &s = links.at (i);
      if (!reference_topology)
        {
          // A ground/customer AS never provides transit between two others, and a ground-ground
          // cable would hand a satellite pair a path that bypasses the IXP entirely.
          NS_ABORT_MSG_IF (IsGroundAs (s.as_a) && IsGroundAs (s.as_b),
                           "ground-ground link " << s.as_a << "-" << s.as_b
                                                 << " is not allowed in this topology");
        }
      // In the split topology the two exchange satellites are on opposite sides of Earth and
      // are never cabled to each other; the only path between them runs through a
      // constellation's internal link. The single-cable-per-pair topology is the deliberate
      // exception — there the handover moves between locations, so an AS120-AS121 link is
      // required to keep two satellites on different exchanges from being partitioned.
      NS_ABORT_MSG_IF (!single_link_per_ixp_pair && IsIxpFabricAs (s.as_a) &&
                           IsIxpFabricAs (s.as_b) &&
                           IxpLocationOf (s.as_a) != IxpLocationOf (s.as_b),
                       "IXP locations " << s.as_a << " and " << s.as_b
                                        << " must not be directly connected");
    }

  if (!dump_topology_path.empty ())
    {
      // Emitted in link order, because both this program and the SCION setup assign interface
      // indices by the order links are declared; a reordered XML resolves interface IDs
      // differently even when the edge multiset matches.
      std::ofstream dump (dump_topology_path.c_str ());
      NS_ABORT_MSG_IF (!dump.is_open (), "cannot open " << dump_topology_path);
      dump << "idx,as_a,as_b,latency_s,capacity_mbps,if_id_a,if_id_b,ixp_managed\n";
      for (uint32_t i = 0; i < links.size (); ++i)
        {
          const LinkSpec &s = links.at (i);
          dump << i << "," << s.as_a << "," << s.as_b << "," << std::fixed
               << std::setprecision (6) << s.latency_s << "," << s.capacity_mbps << ","
               << s.if_id_a << "," << s.if_id_b << "," << (s.ixp_managed ? 1 : 0) << "\n";
        }
      dump.close ();
      std::cout << "wrote " << links.size () << " links to " << dump_topology_path << std::endl;
      return 0;
    }
  // With --sharedPairSubnet, both cables of an AS pair get one subnet and the SAME pair of
  // addresses, so exactly one subnet exists per adjacency. Giving the standby cable its own
  // /30 means both endpoints originate a second prefix for the same adjacency, and that is
  // the configuration that fails to propagate: src/bgp/examples/bgp-convergence-first.cc has
  // one subnet per pair and reaches 0% loss on multi-hop transit, while the same code with a
  // second parallel subnet blackholes it. The standby interface is held down so the
  // duplicate address is never live on two interfaces at once.
  if (shared_pair_subnet)
    {
      Ipv4AddressGenerator::TestMode ();
    }

  std::map<std::pair<uint16_t, uint16_t>, uint32_t> subnetForPair;
  uint32_t subnetId = 0;
  for (uint32_t i = 0; i < links.size (); ++i)
    {
      const LinkSpec &spec = links.at (i);
      Ptr<Node> nodeA = nodes.Get (asToNodeIdx.at (spec.as_a));
      Ptr<Node> nodeB = nodes.Get (asToNodeIdx.at (spec.as_b));

      std::ostringstream rate;
      rate << spec.capacity_mbps << "Mbps";

      double link_delay_s = spec.latency_s;
      if (stochastic_params.enabled && (spec.ixp_managed || IsDirectPeerLink (spec)))
        {
          uint32_t k = GetStochasticRankKForLink (spec);
          link_delay_s =
              SampleIxpLatencySeconds (k, Simulator::Now ().GetSeconds (), stochastic_params);
        }
      if (!std::isfinite (link_delay_s) || link_delay_s <= 0.0)
        {
          link_delay_s = std::max (1e-6, spec.latency_s);
        }

      PointToPointHelper p2p;
      p2p.SetDeviceAttribute ("DataRate", StringValue (rate.str ()));
      p2p.SetChannelAttribute ("Delay", TimeValue (Seconds (link_delay_s)));

      NetDeviceContainer devs = p2p.Install (nodeA, nodeB);

      uint32_t thisSubnetId = subnetId;
      if (shared_pair_subnet)
        {
          std::pair<uint16_t, uint16_t> pairKey = std::make_pair (
              std::min (spec.as_a, spec.as_b), std::max (spec.as_a, spec.as_b));
          std::map<std::pair<uint16_t, uint16_t>, uint32_t>::const_iterator seen =
              subnetForPair.find (pairKey);
          if (seen != subnetForPair.end ())
            {
              thisSubnetId = seen->second;
            }
          else
            {
              subnetForPair[pairKey] = thisSubnetId;
            }
        }

      std::ostringstream base;
      base << "10." << (thisSubnetId / 256) << "." << (thisSubnetId % 256) << ".0";
      Ipv4AddressHelper ipv4;
      ipv4.SetBase (base.str ().c_str (), "255.255.255.252");
      Ipv4InterfaceContainer ifaces = ipv4.Assign (devs);

      Ptr<Ipv4> ipv4A = nodeA->GetObject<Ipv4> ();
      Ptr<Ipv4> ipv4B = nodeB->GetObject<Ipv4> ();

      LinkRuntime runtime;
      runtime.spec = spec;
      runtime.node_a = nodeA;
      runtime.node_b = nodeB;
      runtime.dev_a = devs.Get (0);
      runtime.dev_b = devs.Get (1);
      runtime.iface_a = ipv4A->GetInterfaceForDevice (devs.Get (0));
      runtime.iface_b = ipv4B->GetInterfaceForDevice (devs.Get (1));
      runtime.ip_a = ifaces.GetAddress (0);
      runtime.ip_b = ifaces.GetAddress (1);
      runtimes.push_back (runtime);

      ifIdToRuntimeIdx[spec.if_id_a] = i;
      ifIdToRuntimeIdx[spec.if_id_b] = i;

      if (spec.ixp_managed)
        {
          nextIxpLinkIndex[spec.as_a]++;
          IxpLinkId linkId = {spec.as_a, nextIxpLinkIndex[spec.as_a]};
          ixpLinkByIfId[spec.if_id_a] = linkId;
          ixpQualityByIfId[spec.if_id_a] =
              dual_ixp ? GetDualIxpQuality (spec.if_id_a, spec.as_b)
                       : ((spec.if_id_a % 10 == 1) ? 1.0 : 0.9);
          ixpRemoteAsByIfId[spec.if_id_a] = spec.as_b;
        }

      subnetId++;
    }

  std::map<uint16_t, Ptr<Bgp>> bgpApps;
  for (std::map<uint16_t, Ptr<Node>>::const_iterator it = asNodes.begin (); it != asNodes.end ();
       ++it)
    {
      Ptr<Bgp> bgp = InstallBgpOnNode (it->first, it->second, Seconds (clock_interval_s),
                                       Seconds (mrai_s), Seconds (error_hold_s), outDir);
      bgpApps[it->first] = bgp;
    }

  // libbgp scopes its RIB by peer router ID, which is per node and not per session. Two
  // sessions to the same peer node therefore share one RIB scope: they overwrite each
  // other's entries, and any ESTABLISHED exit on either one calls dropAllRoutes() ->
  // rib4->discard(peer_bgp_id), deleting the routes the OTHER session installed. With two
  // parallel cables per satellite-IXP AS pair that happens continuously, which blackholes
  // transit even with no churn at all. Peer once per adjacency; the second cable stays a
  // physical standby with no session of its own.
  //
  // Per ADJACENCY, not per ASN pair. An ASN-pair key is the same thing for every IXP topology,
  // where a pair meets at one location, but in the direct-peering mesh each pair meets at two
  // -- and an ASN key there peers only over whichever cable the builder declares first,
  // leaving the entire second location without a session. Gateway events that toggle cables at
  // that location then produce no control-plane activity whatsoever.
  std::set<std::pair<uint16_t, uint16_t>> peeredAsnPairs;
  for (uint32_t i = 0; i < runtimes.size (); ++i)
    {
      LinkRuntime &rt = runtimes.at (i);

      // Deduplicate only when the two cables of an adjacency share a subnet. They then carry
      // the same pair of addresses, one peer entry serves both, and that is exactly what lets
      // the session ride through a handover untouched.
      //
      // With a subnet per cable there is nothing to share: the standby's addresses appear in no
      // peer entry at all, so after a handover the session has nowhere to re-form and the
      // adjacency stays down for the rest of the run. Measured on a four-handover trace, that
      // left AS102-AS103 and AS103-AS105 at 100% probe loss with no recovery. Peer over every
      // cable instead. Only one cable of an adjacency is ever live -- the standby is held down
      // at t=0.1s and every generated trace is break-before-make -- so this still never puts
      // two sessions of one adjacency into ESTABLISHED at the same time.
      if (bgp_sessions_per_pair == 1 && shared_pair_subnet)
        {
          std::pair<uint16_t, uint16_t> pairKey =
              std::make_pair (std::min (rt.spec.as_a, rt.spec.as_b),
                              std::max (rt.spec.as_a, rt.spec.as_b));
          if (peeredAsnPairs.count (pairKey) > 0)
            {
              continue;
            }
          peeredAsnPairs.insert (pairKey);
        }

      Peer aToB;
      aToB.local_asn = rt.spec.as_a;
      aToB.peer_asn = rt.spec.as_b;
      aToB.peer_address = rt.ip_b;
      aToB.passive = false;

      if (dual_ixp && (rt.spec.as_b == 120 || rt.spec.as_b == 121))
        {
          aToB.weight = GetDualIxpInterfaceWeight (rt.spec.as_a, rt.spec.as_b, rt.spec.if_id_a,
                                                   single_link_per_ixp_pair);
        }
      else if (direct_links && rt.spec.as_a >= 102 && rt.spec.as_a <= 105 && rt.spec.as_b >= 102 &&
               rt.spec.as_b <= 105)
        {
          aToB.weight = GetDirectLinkWeight (rt.spec.as_a, rt.spec.as_b, rt.spec.if_id_a);
        }

      bgpApps.at (rt.spec.as_a)->AddPeer (aToB);

      Peer bToA;
      bToA.local_asn = rt.spec.as_b;
      bToA.peer_asn = rt.spec.as_a;
      bToA.peer_address = rt.ip_a;
      bToA.passive = false;

      if (dual_ixp && (rt.spec.as_a == 120 || rt.spec.as_a == 121))
        {
          bToA.weight = GetDualIxpInterfaceWeight (rt.spec.as_b, rt.spec.as_a, rt.spec.if_id_b,
                                                   single_link_per_ixp_pair);
        }
      else if (direct_links && rt.spec.as_a >= 102 && rt.spec.as_a <= 105 && rt.spec.as_b >= 102 &&
               rt.spec.as_b <= 105)
        {
          bToA.weight = GetDirectLinkWeight (rt.spec.as_b, rt.spec.as_a, rt.spec.if_id_b);
        }

      bgpApps.at (rt.spec.as_b)->AddPeer (bToA);
    }

  std::map<uint16_t, Ipv4Address> probeIpPerAs;
  for (std::map<uint16_t, Ptr<Node>>::const_iterator it = asNodes.begin (); it != asNodes.end ();
       ++it)
    {
      AddRoutesForAllInterfaces (bgpApps.at (it->first), it->second);

      Ptr<Ipv4> ipv4 = it->second->GetObject<Ipv4> ();

      if (loopback_probe_targets)
        {
          // Give the AS a service address of its own and originate it once. Targeting a
          // link's /30 instead makes the measurement depend on that link's prefix
          // propagating, and every link /30 here is originated by BOTH of its endpoints,
          // which is exactly the case that fails to propagate. A /32 with a single
          // originator removes that dependency and matches how the SCION probes address a
          // host in an AS rather than one end of a cable.
          Ipv4Address loopback = MakeLoopbackAddress (it->first);
          ipv4->AddAddress (0, Ipv4InterfaceAddress (loopback, Ipv4Mask ("255.255.255.255")));
          ipv4->SetUp (0);
          bgpApps.at (it->first)->AddRoute (loopback, Ipv4Mask ("255.255.255.255"), loopback);
          probeIpPerAs[it->first] = loopback;
          continue;
        }

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

  const double activityEndGuardS = 10.0;
  const double routingStopS = sim_time_s - activityEndGuardS;
  const double probeStartS = 10.0;
  const double probeIntervalS = 1.0;
  const double probeTimeoutS = 2.0;
  const double lastProbeSendS = routingStopS;
  const uint32_t probeCount =
      static_cast<uint32_t> ((lastProbeSendS - probeStartS) / probeIntervalS);

  for (std::map<uint16_t, Ptr<Bgp>>::iterator it = bgpApps.begin (); it != bgpApps.end (); ++it)
    {
      it->second->SetStartTime (Seconds (1.0));
      it->second->SetStopTime (Seconds (routingStopS));
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

  std::vector<std::pair<uint16_t, uint16_t>> probePairs;
  if (reference_topology)
    {
      // The same 16 pairs the reference example probes, so the two runs are directly
      // comparable number for number: every pair among 101-106, plus the stub pair 107-108
      // whose path crosses four AS hops.
      for (uint16_t a = 101; a <= 106; ++a)
        {
          for (uint16_t b = a + 1; b <= 106; ++b)
            {
              probePairs.push_back (std::make_pair (a, b));
            }
        }
      probePairs.push_back (std::make_pair (107, 108));
    }
  else if (split_edge_as)
    {
      probePairs.push_back (std::make_pair (101, MakeSplitAsn (102, true)));
      probePairs.push_back (std::make_pair (MakeSplitAsn (102, true), MakeSplitAsn (103, true)));
      probePairs.push_back (std::make_pair (MakeSplitAsn (102, false), MakeSplitAsn (103, false)));
      probePairs.push_back (std::make_pair (MakeSplitAsn (102, true), MakeSplitAsn (104, true)));
      probePairs.push_back (std::make_pair (MakeSplitAsn (102, false), MakeSplitAsn (104, false)));
      probePairs.push_back (std::make_pair (MakeSplitAsn (103, true), MakeSplitAsn (105, true)));
      probePairs.push_back (std::make_pair (MakeSplitAsn (103, false), MakeSplitAsn (105, false)));
      probePairs.push_back (std::make_pair (MakeSplitAsn (104, true), MakeSplitAsn (105, true)));
      probePairs.push_back (std::make_pair (MakeSplitAsn (104, false), MakeSplitAsn (105, false)));

      // Cross-location pairs. Every pair above is same-location, so none of them traverses a
      // constellation's 20ms internal link in steady state and a failure that isolated one
      // exchange from the other would be invisible to the measurement. The sibling pairs below
      // ride the internal link directly and must be insensitive to a fabric handover; the
      // diagonal pairs cross both an exchange and an internal link, so they are what actually
      // shows the path stretch this topology is built to produce.
      for (uint16_t base = 102; base <= 105; ++base)
        {
          probePairs.push_back (
              std::make_pair (MakeSplitAsn (base, true), MakeSplitAsn (base, false)));
        }
      probePairs.push_back (std::make_pair (MakeSplitAsn (102, true), MakeSplitAsn (103, false)));
      probePairs.push_back (std::make_pair (MakeSplitAsn (103, true), MakeSplitAsn (104, false)));
      probePairs.push_back (std::make_pair (MakeSplitAsn (104, true), MakeSplitAsn (105, false)));
      probePairs.push_back (std::make_pair (MakeSplitAsn (105, true), MakeSplitAsn (102, false)));
    }
  else
    {
      probePairs.push_back (std::make_pair (101, 102));
      probePairs.push_back (std::make_pair (102, 103));
      probePairs.push_back (std::make_pair (102, 104));
      probePairs.push_back (std::make_pair (102, 105));
      probePairs.push_back (std::make_pair (103, 104));
      probePairs.push_back (std::make_pair (103, 105));
      probePairs.push_back (std::make_pair (104, 105));
    }

  for (uint32_t i = 0; i < probePairs.size (); ++i)
    {
      uint16_t srcAs = probePairs.at (i).first;
      uint16_t dstAs = probePairs.at (i).second;

      Ptr<UdpProbeApp> probe = CreateObject<UdpProbeApp> ();
      std::ostringstream out;
      out << outDir << "/probe_" << srcAs << "_" << dstAs << ".csv";
      probe->Configure (srcAs, dstAs, probeIpPerAs.at (dstAs), probePort, probeCount,
                        Seconds (probeIntervalS), Seconds (probeTimeoutS), out.str (),
                        loopback_probe_targets ? probeIpPerAs.at (srcAs) : Ipv4Address::GetAny ());
      asNodes.at (srcAs)->AddApplication (probe);
      probe->SetStartTime (Seconds (probeStartS + static_cast<double> (i) * 0.001));
      probe->SetStopTime (Seconds (routingStopS));

      // Keep a traceroute-style path log alongside the probe RTT log so hidden and visible
      // BGP scenarios can be compared hop-by-hop.
      V4TraceRouteHelper traceHelper (probeIpPerAs.at (dstAs));
      traceHelper.SetAttribute ("Verbose", BooleanValue (false));
      traceHelper.SetAttribute ("Timeout", TimeValue (Seconds (probeTimeoutS)));
      traceHelper.SetAttribute ("ProbeNum", UintegerValue (3));
      traceHelper.SetAttribute ("MaxHop", UintegerValue (30));

      ApplicationContainer traceApps = traceHelper.Install (asNodes.at (srcAs));
      traceApps.Start (Seconds (probeStartS + static_cast<double> (i) * 0.001));
      traceApps.Stop (Seconds (routingStopS));

      std::ostringstream traceOut;
      traceOut << outDir << "/traceroute_" << scenario << "_" << srcAs << "_" << dstAs << ".txt";
      Ptr<OutputStreamWrapper> traceStream =
          Create<OutputStreamWrapper> (traceOut.str (), std::ios::out | std::ios::trunc);
      *traceStream->GetStream () << "# scenario=" << scenario << " src_as=" << srcAs
                                 << " dst_as=" << dstAs << std::endl;
      V4TraceRouteHelper::PrintTraceRouteAt (asNodes.at (srcAs), traceStream);
    }

  std::ofstream linkEvents;
  std::ostringstream linkEventsPath;
  linkEventsPath << outDir << "/link_events.csv";
  linkEvents.open (linkEventsPath.str ().c_str (), std::ios::out | std::ios::trunc);
  linkEvents << "time_s,event,as_a,as_b,if_id" << std::endl;

  Ptr<VirtualIxpFabric> fabric = 0;
  Ptr<VirtualIxpFabric> fabricA = 0;
  Ptr<VirtualIxpFabric> fabricB = 0;
  std::map<uint32_t, HiddenIxpLinkBinding> hiddenLinkByIfId;
  if (scenario == "hidden")
    {
      auto configureCaps = [&] (IxpConfig &cfg) {
        if (split_edge_as)
          {
            cfg.maxActiveLinksPerAs[MakeSplitAsn (102, true)] = 1;
            cfg.maxActiveLinksPerAs[MakeSplitAsn (102, false)] = 1;
            cfg.maxActiveLinksPerAs[MakeSplitAsn (103, true)] = 1;
            cfg.maxActiveLinksPerAs[MakeSplitAsn (103, false)] = 1;
            cfg.maxActiveLinksPerAs[MakeSplitAsn (104, true)] = 1;
            cfg.maxActiveLinksPerAs[MakeSplitAsn (104, false)] = 1;
            cfg.maxActiveLinksPerAs[MakeSplitAsn (105, true)] = 1;
            cfg.maxActiveLinksPerAs[MakeSplitAsn (105, false)] = 1;
          }
        else
          {
            // Hidden-mode policy: one active attachment per AS per virtual IXP.
            cfg.maxActiveLinksPerAs[102] = 1;
            cfg.maxActiveLinksPerAs[103] = 1;
            cfg.maxActiveLinksPerAs[104] = 1;
            cfg.maxActiveLinksPerAs[105] = 1;
          }
      };

      IxpConfig cfg;
      cfg.portCount = virtual_ixp_port_count;
      cfg.rebalancePeriod = Seconds (virtual_ixp_rebalance_s);
      cfg.holdDown = Seconds (virtual_ixp_hold_down_s);

      if (dual_ixp)
        {
          fabricA = CreateObject<VirtualIxpFabric> ();
          fabricB = CreateObject<VirtualIxpFabric> ();

          IxpConfig cfgA = cfg;
          IxpConfig cfgB = cfg;
          configureCaps (cfgA);
          configureCaps (cfgB);

          fabricA->Configure (cfgA);
          fabricB->Configure (cfgB);
        }
      else
        {
          fabric = CreateObject<VirtualIxpFabric> ();
          configureCaps (cfg);
          fabric->Configure (cfg);
        }

      for (uint32_t i = 0; i < runtimes.size (); ++i)
        {
          const LinkRuntime &rt = runtimes.at (i);
          if (!rt.spec.ixp_managed)
            {
              continue;
            }

          Ptr<BgpHiddenIxpEndpointAdapter> endpoint = CreateObject<BgpHiddenIxpEndpointAdapter> ();

          HiddenIxpLinkBinding binding;
          binding.linkId = ixpLinkByIfId.at (rt.spec.if_id_a);
          binding.quality = ixpQualityByIfId.at (rt.spec.if_id_a);
          binding.remoteIxpAs = ixpRemoteAsByIfId.at (rt.spec.if_id_a);

          if (dual_ixp)
            {
              Ptr<VirtualIxpFabric> targetFabric = (rt.spec.as_b == 120) ? fabricA : fabricB;
              binding.fabric = targetFabric;
              targetFabric->RegisterEndpoint (binding.linkId, endpoint);
            }
          else
            {
              binding.fabric = fabric;
              fabric->RegisterEndpoint (binding.linkId, endpoint);
            }

          hiddenLinkByIfId[rt.spec.if_id_a] = binding;
        }

      if (fabricA != 0)
        {
          fabricA->Start ();
        }
      if (fabricB != 0)
        {
          fabricB->Start ();
        }
      if (fabric != 0)
        {
          fabric->Start ();
        }

      for (std::map<uint32_t, HiddenIxpLinkBinding>::const_iterator it = hiddenLinkByIfId.begin ();
           it != hiddenLinkByIfId.end (); ++it)
        {
          if (it->second.fabric != 0)
            {
              it->second.fabric->SetFeasibleUp (it->second.linkId, it->second.quality);
            }
        }

      if (!event_file.empty ())
        {
          std::vector<ExternalEvent> extEvents = LoadExternalEvents (event_file);
          for (uint32_t i = 0; i < extEvents.size (); ++i)
            {
              const ExternalEvent &ev = extEvents.at (i);
              if (hiddenLinkByIfId.find (ev.if_id) == hiddenLinkByIfId.end ())
                {
                  continue;
                }

              const HiddenIxpLinkBinding &binding = hiddenLinkByIfId.at (ev.if_id);

              Simulator::Schedule (Seconds (ev.time_s), &SetVirtualIxpLinkState, binding, ev.up,
                                   ev.asn, &linkEvents);
            }
        }
      else
        {
          const uint32_t defaultIfIds[] = {1020001, 1030001, 1040001, 1050001};
          const double defaultTimes[] = {50.0, 77.0, 104.0, 131.0};
          const uint16_t defaultAsn[] = {102, 103, 104, 105};
          for (uint32_t k = 0; k < 4; ++k)
            {
              const uint32_t ifId = defaultIfIds[k];
              if (hiddenLinkByIfId.find (ifId) == hiddenLinkByIfId.end ())
                {
                  continue;
                }

              const HiddenIxpLinkBinding &binding = hiddenLinkByIfId.at (ifId);
              Simulator::Schedule (Seconds (defaultTimes[k]), &SetVirtualIxpLinkState, binding,
                                   false, defaultAsn[k], &linkEvents);
            }
        }
    }
  else if (scenario == "visible")
    {
      // Single/dual IXP: bring each satellite's backup link to the IXP fully down at startup,
      // so exactly one link per satellite-IXP AS pair is active and the other is a standby.
      // Both endpoints are taken down: downing only the satellite side leaves the IXP still
      // holding the backup /30, which keeps the duplicate parallel subnet in play.
      // Note the hardcoded schedules further below do this too, but they are in the
      // "no --eventFile" branch, so sweeps that supply an events file never reach them.
      if (backup_links_down_at_start)
        {
          // Derived from the built link list rather than enumerated. The previous hardcoded set
          // {1020002, 1030002, 1040002, 1050002} covered only the location-A backups, so in any
          // dual-IXP topology the location-B backups X0004 stayed up while sharing their
          // primary's /30 — two live interfaces with identical addresses, which is exactly what
          // the shared-subnet model exists to prevent and which Ipv4AddressGenerator::TestMode
          // stops ns-3 from complaining about.
          //
          // Keyed on the IXP *location*, not on the peer ASN: with --splitFabricPerIxp a half's
          // main and backup cables terminate on two different fabric ASes, so an ASN key would
          // classify both as "first seen" and neither as a standby.
          //
          // Direct peer cables join the same derivation, keyed on their ASN pair. The direct
          // mesh is single-location, so an ASN pair names exactly one adjacency of two cables
          // and the first one seen is its live main. This replaces a hardcoded
          // {1020101, ..., 1040221} array that only the direct topology used: deriving it
          // means a topology edit cannot silently leave a standby live, which is what the
          // shared /30 makes unsafe.
          std::set<std::pair<uint16_t, uint16_t>> seenMainCable;
          for (uint32_t i = 0; i < links.size (); ++i)
            {
              const LinkSpec &spec = links.at (i);
              const bool direct_peer = IsDirectPeerLink (spec);
              if (!spec.ixp_managed && !direct_peer)
                {
                  continue;
                }
              std::pair<uint16_t, uint16_t> key;
              if (direct_peer)
                {
                  key = std::make_pair (std::min (spec.as_a, spec.as_b),
                                        std::max (spec.as_a, spec.as_b));
                }
              else
                {
                  const uint16_t location = IxpLocationOf (spec.as_b);
                  if (location == kIxpLocationNone)
                    {
                      continue;
                    }
                  key = std::make_pair (spec.as_a, location);
                }
              if (seenMainCable.insert (key).second)
                {
                  continue; // first cable of this adjacency is the live main attachment
                }
              if (ifIdToRuntimeIdx.count (spec.if_id_a))
                {
                  Simulator::Schedule (Seconds (0.1), &SetLinkState,
                                       &runtimes.at (ifIdToRuntimeIdx.at (spec.if_id_a)), false,
                                       &linkEvents);
                }
            }
        }

      if (!event_file.empty ())
        {
          std::vector<ExternalEvent> extEvents = LoadExternalEvents (event_file);
          for (uint32_t i = 0; i < extEvents.size (); ++i)
            {
              const ExternalEvent &ev = extEvents.at (i);
              if (ifIdToRuntimeIdx.find (ev.if_id) == ifIdToRuntimeIdx.end ())
                {
                  continue;
                }

              if (direct_links)
                {
                  // Direct links: toggle both endpoints of the link symmetrically.
                  Simulator::Schedule (Seconds (ev.time_s), &SetLinkState,
                                       &runtimes.at (ifIdToRuntimeIdx.at (ev.if_id)), ev.up,
                                       &linkEvents);
                }
              else if (symmetric_link_events)
                {
                  // Both endpoints. A satellite and an IXP both know the orbital schedule
                  // in advance, so both know when a link goes down or comes up; modelling
                  // the event as one-sided leaves the far end forwarding into a black hole
                  // until its own timers expire, and makes a downed standby impossible to
                  // restore once it was taken down at both ends.
                  Simulator::Schedule (Seconds (ev.time_s), &SetLinkState,
                                       &runtimes.at (ifIdToRuntimeIdx.at (ev.if_id)), ev.up,
                                       &linkEvents);
                }
              else
                {
                  Simulator::Schedule (Seconds (ev.time_s), &SetSatelliteIxpInterfaceState,
                                       &runtimes.at (ifIdToRuntimeIdx.at (ev.if_id)), ev.up,
                                       &linkEvents);
                }
            }
        }
      else if (reference_topology)
        {
          // Calibration control: static topology, no scheduled events.
        }
      else
        {
          // Built-in demo schedules, reached only when no --eventFile is supplied. Every sweep
          // supplies one, so these exist for interactive runs.
          //
          // The interface IDs below are hardcoded and do not exist in every topology mode: for
          // example 1020002 is absent whenever --singleLinkPerIxpPair builds one cable per
          // pair. Looking them up with map::at threw std::out_of_range and aborted the run
          // before Simulator::Run, so those modes were simply unusable without an events file.
          // Skip a missing interface with a note instead, matching the guarded style used by
          // the --eventFile path above.
          auto scheduleIfPresent = [&] (double t, uint32_t ifId, bool up, bool bothEndpoints) {
            std::map<uint32_t, uint32_t>::const_iterator it = ifIdToRuntimeIdx.find (ifId);
            if (it == ifIdToRuntimeIdx.end ())
              {
                NS_LOG_UNCOND ("built-in schedule: if_id " << ifId
                                                           << " does not exist in this topology "
                                                              "mode, skipping");
                return;
              }
            if (bothEndpoints)
              {
                Simulator::Schedule (Seconds (t), &SetLinkState, &runtimes.at (it->second), up,
                                     &linkEvents);
              }
            else
              {
                Simulator::Schedule (Seconds (t), &SetSatelliteIxpInterfaceState,
                                     &runtimes.at (it->second), up, &linkEvents);
              }
          };

          if (dual_ixp)
            {
              // Hand each satellite half over from its main cable to its standby and back,
              // staggered so the handovers do not overlap. Derived from the built link list so
              // it covers every half in whichever dual mode is active, rather than only AS102
              // as the previous hardcoded schedule did.
              std::map<std::pair<uint16_t, uint16_t>, std::pair<uint32_t, uint32_t>> cables;
              std::vector<std::pair<uint16_t, uint16_t>> order;
              for (uint32_t i = 0; i < links.size (); ++i)
                {
                  const LinkSpec &spec = links.at (i);
                  const uint16_t location = IxpLocationOf (spec.as_b);
                  if (!spec.ixp_managed || location == kIxpLocationNone)
                    {
                      continue;
                    }
                  const std::pair<uint16_t, uint16_t> key (spec.as_a, location);
                  if (!cables.count (key))
                    {
                      cables[key] = std::make_pair (spec.if_id_a, 0u);
                      order.push_back (key);
                    }
                  else if (cables[key].second == 0u)
                    {
                      cables[key].second = spec.if_id_a;
                    }
                }
              double t0 = 50.0;
              for (uint32_t k = 0; k < order.size (); ++k)
                {
                  const std::pair<uint32_t, uint32_t> &pair = cables.at (order.at (k));
                  if (pair.second == 0u)
                    {
                      continue; // only one cable at this location, nothing to hand over to
                    }
                  scheduleIfPresent (t0, pair.first, false, true);
                  scheduleIfPresent (t0 + 0.001, pair.second, true, true);
                  scheduleIfPresent (t0 + 20.0, pair.second, false, true);
                  scheduleIfPresent (t0 + 20.001, pair.first, true, true);
                  t0 += 7.0;
                }
            }
          else
            {
              // Single-IXP visible scenario (original schedule).
              scheduleIfPresent (0.1, 1020002, false, false);
              scheduleIfPresent (0.1, 1030002, false, false);
              scheduleIfPresent (0.1, 1040002, false, false);
              scheduleIfPresent (0.1, 1050002, false, false);
              scheduleIfPresent (50.0, 1020001, false, false);
              scheduleIfPresent (55.0, 1020002, true, false);
              scheduleIfPresent (120.0, 1010000, false, true);
            }
        }
    }
  else
    {
      NS_FATAL_ERROR ("Unsupported scenario: " << scenario << " (expected hidden or visible)");
    }

  if (dump_rib_at_s > 0.0)
    {
      std::ostringstream ribPath;
      ribPath << outDir << "/rib.txt";
      Ptr<OutputStreamWrapper> ribStream =
          Create<OutputStreamWrapper> (ribPath.str (), std::ios::out);
      Ipv4RoutingHelper::PrintRoutingTableAllAt (Seconds (dump_rib_at_s), ribStream);
    }

  Simulator::Stop (Seconds (sim_time_s));
  Simulator::Run ();
  Simulator::Destroy ();
  linkEvents.close ();
  return 0;
}