/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "virtual-ixp-fabric.h"

#include <algorithm>

#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/ipv4.h"

#include "scion-as.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("VirtualIxpFabric");

namespace {

uint32_t
CountActiveForAs (const IxpAssignment &assignment, uint32_t asn)
{
  uint32_t cnt = 0;
  for (std::map<IxpLinkId, uint32_t>::const_iterator it = assignment.linkToPort.begin ();
       it != assignment.linkToPort.end (); ++it)
    {
      if (it->first.asn == asn)
        {
          cnt++;
        }
    }
  return cnt;
}

bool
HasMapping (const IxpAssignment &assignment, const IxpLinkId &link)
{
  return assignment.linkToPort.find (link) != assignment.linkToPort.end ();
}

uint32_t
FindFreePort (const IxpConfig &cfg, const IxpAssignment &assignment)
{
  for (uint32_t p = 1; p <= cfg.portCount; ++p)
    {
      if (assignment.portToLink.find (p) == assignment.portToLink.end ())
        {
          return p;
        }
    }
  return 0;
}

void
Attach (IxpAssignment &assignment, const IxpLinkId &link, uint32_t port)
{
  assignment.linkToPort[link] = port;
  assignment.portToLink[port] = link;
}

} // namespace

TypeId
IxpEndpointAdapter::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::IxpEndpointAdapter").SetParent<Object> ();
  return tid;
}

IxpEndpointAdapter::~IxpEndpointAdapter ()
{
}

TypeId
IxpAllocator::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::IxpAllocator").SetParent<Object> ();
  return tid;
}

IxpAllocator::~IxpAllocator ()
{
}

TypeId
GreedyIxpAllocator::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::GreedyIxpAllocator")
                          .SetParent<IxpAllocator> ()
                          .AddConstructor<GreedyIxpAllocator> ();
  return tid;
}

bool
GreedyIxpAllocator::HasAsCapacity (const IxpConfig &cfg, const IxpAssignment &assignment,
                                   uint32_t asn) const
{
  std::map<uint32_t, uint32_t>::const_iterator capIt = cfg.maxActiveLinksPerAs.find (asn);
  if (capIt == cfg.maxActiveLinksPerAs.end ())
    {
      return true;
    }

  return CountActiveForAs (assignment, asn) < capIt->second;
}

double
GreedyIxpAllocator::Score (const IxpConfig &cfg, const IxpLinkState &state,
                           const IxpAssignment &partial, Time now) const
{
  (void) now;

  const double disconnectedPriority = (CountActiveForAs (partial, state.id.asn) == 0) ? 1.0 : 0.0;
  const double quality = state.quality;
  const double stickiness = (state.lastAttach > state.lastDetach) ? 1.0 : 0.0;
  const double handoverPenalty = (Simulator::Now () - state.lastDetach < cfg.holdDown) ? 1.0 : 0.0;

  return cfg.wConnectivity * disconnectedPriority + cfg.wQuality * quality +
         cfg.wStickiness * stickiness - cfg.wHandoverPenalty * handoverPenalty;
}

IxpAssignment
GreedyIxpAllocator::Compute (const IxpConfig &cfg, const std::map<IxpLinkId, IxpLinkState> &links,
                             const IxpAssignment &current, Time now)
{
  IxpAssignment out = current;

  for (std::map<IxpLinkId, uint32_t>::iterator it = out.linkToPort.begin ();
       it != out.linkToPort.end ();)
    {
      std::map<IxpLinkId, IxpLinkState>::const_iterator ls = links.find (it->first);
      if (ls == links.end () || !ls->second.feasible)
        {
          out.portToLink.erase (it->second);
          it = out.linkToPort.erase (it);
          continue;
        }
      ++it;
    }

  struct Candidate
  {
    double score;
    IxpLinkId link;
  };

  std::vector<Candidate> candidates;
  for (std::map<IxpLinkId, IxpLinkState>::const_iterator it = links.begin (); it != links.end ();
       ++it)
    {
      const IxpLinkState &ls = it->second;
      if (!ls.feasible)
        {
          continue;
        }
      if (HasMapping (out, ls.id))
        {
          continue;
        }
      if (now - ls.lastDetach < cfg.holdDown)
        {
          continue;
        }

      Candidate c;
      c.score = Score (cfg, ls, out, now);
      c.link = ls.id;
      candidates.push_back (c);
    }

  std::sort (candidates.begin (), candidates.end (),
             [] (const Candidate &a, const Candidate &b) { return a.score > b.score; });

  for (std::vector<Candidate>::const_iterator it = candidates.begin (); it != candidates.end ();
       ++it)
    {
      if (!HasAsCapacity (cfg, out, it->link.asn))
        {
          continue;
        }

      uint32_t freePort = FindFreePort (cfg, out);
      if (freePort == 0)
        {
          break;
        }

      Attach (out, it->link, freePort);
    }

  return out;
}

TypeId
VirtualIxpFabric::GetTypeId (void)
{
  static TypeId tid =
      TypeId ("ns3::VirtualIxpFabric").SetParent<Object> ().AddConstructor<VirtualIxpFabric> ();
  return tid;
}

VirtualIxpFabric::VirtualIxpFabric () : m_running (false)
{
  m_alloc = CreateObject<GreedyIxpAllocator> ();
}

VirtualIxpFabric::~VirtualIxpFabric ()
{
  Stop ();
}

void
VirtualIxpFabric::Configure (const IxpConfig &cfg)
{
  m_cfg = cfg;
}

void
VirtualIxpFabric::SetAllocator (Ptr<IxpAllocator> alloc)
{
  m_alloc = alloc;
}

void
VirtualIxpFabric::RegisterLink (const IxpLinkId &link)
{
  if (m_links.find (link) != m_links.end ())
    {
      return;
    }

  IxpLinkState st;
  st.id = link;
  st.lastDetach = Time (0);
  m_links.insert (std::make_pair (link, st));
}

void
VirtualIxpFabric::RegisterEndpoint (const IxpLinkId &link, Ptr<IxpEndpointAdapter> endpoint)
{
  RegisterLink (link);
  m_endpoints[link] = endpoint;
}

void
VirtualIxpFabric::SetFeasibleUp (const IxpLinkId &link, double quality)
{
  RegisterLink (link);

  IxpLinkState &st = m_links[link];
  st.feasible = true;
  st.quality = quality;
  st.lastFeasibleUp = Simulator::Now ();

  Rebalance ();
}

void
VirtualIxpFabric::SetFeasibleDown (const IxpLinkId &link)
{
  std::map<IxpLinkId, IxpLinkState>::iterator it = m_links.find (link);
  if (it == m_links.end ())
    {
      return;
    }

  it->second.feasible = false;
  it->second.lastFeasibleDown = Simulator::Now ();

  Rebalance ();
}

void
VirtualIxpFabric::Start ()
{
  if (m_running)
    {
      return;
    }
  m_running = true;
  ScheduleNextTick ();
}

void
VirtualIxpFabric::Stop ()
{
  m_running = false;
  if (m_tickEvent.IsRunning ())
    {
      Simulator::Cancel (m_tickEvent);
    }
}

void
VirtualIxpFabric::RecomputeNow ()
{
  Rebalance ();
}

void
VirtualIxpFabric::InjectFromEndpoint (const IxpLinkId &ingress, Ptr<Packet> packet)
{
  std::map<IxpLinkId, uint32_t>::const_iterator ingressIt = m_assignment.linkToPort.find (ingress);
  if (ingressIt == m_assignment.linkToPort.end ())
    {
      return;
    }

  const uint32_t ingressPort = ingressIt->second;

  for (std::map<uint32_t, IxpLinkId>::const_iterator it = m_assignment.portToLink.begin ();
       it != m_assignment.portToLink.end (); ++it)
    {
      if (it->first == ingressPort)
        {
          continue;
        }

      std::map<IxpLinkId, Ptr<IxpEndpointAdapter>>::const_iterator epIt =
          m_endpoints.find (it->second);
      if (epIt == m_endpoints.end ())
        {
          continue;
        }

      epIt->second->DeliverFromFabric (it->second, packet->Copy ());
    }
}

void
VirtualIxpFabric::ScheduleNextTick ()
{
  if (!m_running)
    {
      return;
    }

  m_tickEvent = Simulator::Schedule (m_cfg.rebalancePeriod, &VirtualIxpFabric::OnTick, this);
}

void
VirtualIxpFabric::OnTick ()
{
  Rebalance ();
  ScheduleNextTick ();
}

void
VirtualIxpFabric::Rebalance ()
{
  if (m_alloc == 0)
    {
      return;
    }

  const IxpAssignment oldAssignment = m_assignment;
  const IxpAssignment newAssignment =
      m_alloc->Compute (m_cfg, m_links, m_assignment, Simulator::Now ());

  ApplyDiff (oldAssignment, newAssignment);
  m_assignment = newAssignment;
}

void
VirtualIxpFabric::ApplyDiff (const IxpAssignment &oldAssignment, const IxpAssignment &newAssignment)
{
  if (oldAssignment.linkToPort.empty ())
    {
      for (std::map<IxpLinkId, Ptr<IxpEndpointAdapter>>::iterator ep = m_endpoints.begin ();
           ep != m_endpoints.end (); ++ep)
        {
          if (newAssignment.linkToPort.find (ep->first) != newAssignment.linkToPort.end ())
            {
              continue;
            }

          ep->second->OnInterfaceDown (ep->first, 0, Simulator::Now ());
          m_links[ep->first].lastDetach = Simulator::Now ();
        }
    }

  for (std::map<IxpLinkId, uint32_t>::const_iterator it = oldAssignment.linkToPort.begin ();
       it != oldAssignment.linkToPort.end (); ++it)
    {
      if (newAssignment.linkToPort.find (it->first) != newAssignment.linkToPort.end ())
        {
          continue;
        }

      std::map<IxpLinkId, Ptr<IxpEndpointAdapter>>::iterator ep = m_endpoints.find (it->first);
      if (ep != m_endpoints.end ())
        {
          ep->second->OnInterfaceDown (it->first, it->second, Simulator::Now ());
        }
      m_links[it->first].lastDetach = Simulator::Now ();
    }

  for (std::map<IxpLinkId, uint32_t>::const_iterator it = newAssignment.linkToPort.begin ();
       it != newAssignment.linkToPort.end (); ++it)
    {
      if (oldAssignment.linkToPort.find (it->first) != oldAssignment.linkToPort.end ())
        {
          continue;
        }

      std::map<IxpLinkId, Ptr<IxpEndpointAdapter>>::iterator ep = m_endpoints.find (it->first);
      if (ep != m_endpoints.end ())
        {
          ep->second->OnInterfaceUp (it->first, it->second, Simulator::Now ());
        }
      m_links[it->first].lastAttach = Simulator::Now ();
    }
}

TypeId
ScionIxpEndpointAdapter::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::ScionIxpEndpointAdapter")
                          .SetParent<IxpEndpointAdapter> ()
                          .AddConstructor<ScionIxpEndpointAdapter> ();
  return tid;
}

ScionIxpEndpointAdapter::ScionIxpEndpointAdapter ()
    : m_asn (0),
      m_scionIfId (0),
      m_hasOriginalRemote (false),
      m_originalRemoteAs (0),
      m_originalRelation ((int32_t) NeighbourRelation::PEER)
{
}

ScionIxpEndpointAdapter::~ScionIxpEndpointAdapter ()
{
}

void
ScionIxpEndpointAdapter::Bind (uint32_t asn, Ptr<Node> asNode, uint16_t scionIfId)
{
  m_asn = asn;
  m_asNode = asNode;
  m_scionIfId = scionIfId;

  ScionAs *scionAs = dynamic_cast<ScionAs *> (PeekPointer (m_asNode));
  if (scionAs == NULL)
    {
      NS_LOG_WARN ("ScionIxpEndpointAdapter::Bind: node is not ScionAs for AS " << asn);
      return;
    }

  std::unordered_map<uint16_t, uint16_t>::const_iterator ifIt =
      scionAs->interface_to_neighbor_map.find (m_scionIfId);
  if (ifIt == scionAs->interface_to_neighbor_map.end ())
    {
      return;
    }

  m_hasOriginalRemote = true;
  m_originalRemoteAs = ifIt->second;

  for (std::vector<std::pair<uint16_t, NeighbourRelation>>::const_iterator it =
           scionAs->neighbors.begin ();
       it != scionAs->neighbors.end (); ++it)
    {
      if (it->first == m_originalRemoteAs)
        {
          m_originalRelation = (int32_t) it->second;
          break;
        }
    }
}

void
ScionIxpEndpointAdapter::OnInterfaceUp (const IxpLinkId &link, uint32_t portId, Time now)
{
  (void) link;
  (void) portId;
  SetScionIfState (true, now);
}

void
ScionIxpEndpointAdapter::OnInterfaceDown (const IxpLinkId &link, uint32_t portId, Time now)
{
  (void) link;
  (void) portId;
  SetScionIfState (false, now);
}

void
ScionIxpEndpointAdapter::DeliverFromFabric (const IxpLinkId &dst, Ptr<Packet> packet)
{
  (void) dst;
  (void) packet;

  static bool warned = false;
  if (!warned)
    {
      NS_LOG_WARN ("ScionIxpEndpointAdapter::DeliverFromFabric invoked, but SCION data-plane "
                   "forwarding over VIXP fabric is disabled by design");
      warned = true;
    }
}

void
ScionIxpEndpointAdapter::SetScionIfState (bool up, Time now)
{
  (void) now;

  ScionAs *scionAs = dynamic_cast<ScionAs *> (PeekPointer (m_asNode));
  if (scionAs == NULL)
    {
      NS_LOG_WARN ("ScionIxpEndpointAdapter::SetScionIfState: node is not ScionAs for AS "
                   << m_asn);
      return;
    }

  if (up)
    {
      if (scionAs->interface_to_neighbor_map.find (m_scionIfId) !=
          scionAs->interface_to_neighbor_map.end ())
        {
          return;
        }

      if (!m_hasOriginalRemote)
        {
          NS_LOG_WARN ("ScionIxpEndpointAdapter: cannot bring IF up without known remote mapping "
                       << "AS=" << m_asn << " IF=" << m_scionIfId);
          return;
        }

      scionAs->interface_to_neighbor_map.insert (std::make_pair (m_scionIfId, m_originalRemoteAs));

      std::vector<uint16_t> &interfaces = scionAs->interfaces_per_neighbor_as[m_originalRemoteAs];
      if (std::find (interfaces.begin (), interfaces.end (), m_scionIfId) == interfaces.end ())
        {
          interfaces.push_back (m_scionIfId);
          std::sort (interfaces.begin (), interfaces.end ());
        }

      bool hasNeighbor = false;
      for (std::vector<std::pair<uint16_t, NeighbourRelation>>::const_iterator it =
               scionAs->neighbors.begin ();
           it != scionAs->neighbors.end (); ++it)
        {
          if (it->first == m_originalRemoteAs)
            {
              hasNeighbor = true;
              break;
            }
        }

      if (!hasNeighbor)
        {
          scionAs->neighbors.push_back (
              std::make_pair (m_originalRemoteAs, (NeighbourRelation) m_originalRelation));
        }

      return;
    }

  std::unordered_map<uint16_t, uint16_t>::iterator ifIt =
      scionAs->interface_to_neighbor_map.find (m_scionIfId);
  if (ifIt == scionAs->interface_to_neighbor_map.end ())
    {
      return;
    }

  const uint16_t remoteAs = ifIt->second;
  m_hasOriginalRemote = true;
  m_originalRemoteAs = remoteAs;

  for (std::vector<std::pair<uint16_t, NeighbourRelation>>::const_iterator it =
           scionAs->neighbors.begin ();
       it != scionAs->neighbors.end (); ++it)
    {
      if (it->first == remoteAs)
        {
          m_originalRelation = (int32_t) it->second;
          break;
        }
    }

  scionAs->interface_to_neighbor_map.erase (ifIt);

  std::unordered_map<uint16_t, std::vector<uint16_t>>::iterator listIt =
      scionAs->interfaces_per_neighbor_as.find (remoteAs);
  if (listIt != scionAs->interfaces_per_neighbor_as.end ())
    {
      std::vector<uint16_t> &interfaces = listIt->second;
      interfaces.erase (std::remove (interfaces.begin (), interfaces.end (), m_scionIfId),
                        interfaces.end ());

      if (interfaces.empty ())
        {
          scionAs->interfaces_per_neighbor_as.erase (listIt);
          scionAs->neighbors.erase (
              std::remove_if (scionAs->neighbors.begin (), scionAs->neighbors.end (),
                              [remoteAs] (const std::pair<uint16_t, NeighbourRelation> &entry) {
                                return entry.first == remoteAs;
                              }),
              scionAs->neighbors.end ());
        }
    }
}

TypeId
BgpIxpEndpointAdapter::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::BgpIxpEndpointAdapter")
                          .SetParent<IxpEndpointAdapter> ()
                          .AddConstructor<BgpIxpEndpointAdapter> ();
  return tid;
}

BgpIxpEndpointAdapter::BgpIxpEndpointAdapter () : m_asn (0), m_ipv4IfIndex (0)
{
}

BgpIxpEndpointAdapter::~BgpIxpEndpointAdapter ()
{
}

void
BgpIxpEndpointAdapter::Bind (uint32_t asn, Ptr<Node> node, uint32_t ipv4IfIndex)
{
  m_asn = asn;
  m_node = node;
  m_ipv4IfIndex = ipv4IfIndex;
}

void
BgpIxpEndpointAdapter::OnInterfaceUp (const IxpLinkId &link, uint32_t portId, Time now)
{
  (void) link;
  (void) portId;
  (void) now;
  SetIpv4IfState (true);
}

void
BgpIxpEndpointAdapter::OnInterfaceDown (const IxpLinkId &link, uint32_t portId, Time now)
{
  (void) link;
  (void) portId;
  (void) now;
  SetIpv4IfState (false);
}

void
BgpIxpEndpointAdapter::DeliverFromFabric (const IxpLinkId &dst, Ptr<Packet> packet)
{
  (void) dst;
  (void) packet;

  // TODO: Optional packet injection path if a synthetic L2 forwarding path is required for BGP tests.
}

void
BgpIxpEndpointAdapter::SetIpv4IfState (bool up)
{
  if (m_node == 0)
    {
      NS_LOG_WARN ("BgpIxpEndpointAdapter::SetIpv4IfState: node not bound for AS " << m_asn);
      return;
    }

  Ptr<Ipv4> ipv4 = m_node->GetObject<Ipv4> ();
  if (ipv4 == 0)
    {
      NS_LOG_WARN ("BgpIxpEndpointAdapter::SetIpv4IfState: Ipv4 stack missing on node for AS "
                   << m_asn);
      return;
    }

  if (m_ipv4IfIndex >= ipv4->GetNInterfaces ())
    {
      NS_LOG_WARN ("BgpIxpEndpointAdapter::SetIpv4IfState: interface index out of range for AS "
                   << m_asn << ", ifIndex=" << m_ipv4IfIndex
                   << ", nIfs=" << ipv4->GetNInterfaces ());
      return;
    }

  const bool currentlyUp = ipv4->IsUp (m_ipv4IfIndex);
  if (up == currentlyUp)
    {
      return;
    }

  if (up)
    {
      ipv4->SetUp (m_ipv4IfIndex);
      return;
    }

  ipv4->SetDown (m_ipv4IfIndex);
}

} // namespace ns3
