/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Virtual IXP fabric skeleton for protocol-agnostic inter-AS attachment control.
 */

#ifndef SCION_SIMULATOR_VIRTUAL_IXP_FABRIC_H
#define SCION_SIMULATOR_VIRTUAL_IXP_FABRIC_H

#include <map>
#include <vector>

#include "ns3/event-id.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"
#include "ns3/simulator.h"

namespace ns3 {

class Node;
class ScionAs;

struct IxpLinkId
{
  uint32_t asn;
  uint32_t linkIndex;

  bool
  operator< (const IxpLinkId &other) const
  {
    if (asn != other.asn)
      {
        return asn < other.asn;
      }
    return linkIndex < other.linkIndex;
  }
};

struct IxpLinkState
{
  IxpLinkId id;
  bool feasible;
  double quality;
  Time lastFeasibleUp;
  Time lastFeasibleDown;
  Time lastAttach;
  Time lastDetach;

  IxpLinkState ()
      : feasible (false),
        quality (1.0),
        lastFeasibleUp (Seconds (0.0)),
        lastFeasibleDown (Seconds (0.0)),
        lastAttach (Seconds (0.0)),
        lastDetach (Seconds (0.0))
  {
  }
};

struct IxpAssignment
{
  std::map<IxpLinkId, uint32_t> linkToPort;
  std::map<uint32_t, IxpLinkId> portToLink;
};

struct IxpConfig
{
  uint32_t portCount;
  Time rebalancePeriod;
  Time holdDown;
  bool allowPreemption;
  double preemptionMargin;

  double wConnectivity;
  double wQuality;
  double wStickiness;
  double wHandoverPenalty;

  std::map<uint32_t, uint32_t> maxActiveLinksPerAs;

  IxpConfig ()
      : portCount (5),
        rebalancePeriod (Seconds (1000.0)),
        holdDown (Seconds (50.0)),
        allowPreemption (true),
        preemptionMargin (0.2),
        wConnectivity (10.0),
        wQuality (1.0),
        wStickiness (0.5),
        wHandoverPenalty (1.0)
  {
  }
};

class IxpEndpointAdapter : public Object
{
public:
  static TypeId GetTypeId (void);
  virtual ~IxpEndpointAdapter ();

  virtual void OnInterfaceUp (const IxpLinkId &link, uint32_t portId, Time now) = 0;
  virtual void OnInterfaceDown (const IxpLinkId &link, uint32_t portId, Time now) = 0;
  virtual void DeliverFromFabric (const IxpLinkId &dst, Ptr<Packet> packet) = 0;
};

class IxpAllocator : public Object
{
public:
  static TypeId GetTypeId (void);
  virtual ~IxpAllocator ();

  virtual IxpAssignment Compute (const IxpConfig &cfg,
                                 const std::map<IxpLinkId, IxpLinkState> &links,
                                 const IxpAssignment &current, Time now) = 0;
};

class GreedyIxpAllocator : public IxpAllocator
{
public:
  static TypeId GetTypeId (void);

  IxpAssignment Compute (const IxpConfig &cfg, const std::map<IxpLinkId, IxpLinkState> &links,
                         const IxpAssignment &current, Time now) override;

private:
  double Score (const IxpConfig &cfg, const IxpLinkState &state, const IxpAssignment &partial,
                Time now) const;

  bool HasAsCapacity (const IxpConfig &cfg, const IxpAssignment &assignment, uint32_t asn) const;
};

class VirtualIxpFabric : public Object
{
public:
  static TypeId GetTypeId (void);

  VirtualIxpFabric ();
  ~VirtualIxpFabric () override;

  void Configure (const IxpConfig &cfg);
  void SetAllocator (Ptr<IxpAllocator> alloc);

  void RegisterLink (const IxpLinkId &link);
  void RegisterEndpoint (const IxpLinkId &link, Ptr<IxpEndpointAdapter> endpoint);

  void SetFeasibleUp (const IxpLinkId &link, double quality);
  void SetFeasibleDown (const IxpLinkId &link);

  void Start ();
  void Stop ();
  void RecomputeNow ();

  void InjectFromEndpoint (const IxpLinkId &ingress, Ptr<Packet> packet);

private:
  void Rebalance ();
  void ApplyDiff (const IxpAssignment &oldAssignment, const IxpAssignment &newAssignment);
  void ScheduleNextTick ();
  void OnTick ();

  IxpConfig m_cfg;
  Ptr<IxpAllocator> m_alloc;

  EventId m_tickEvent;
  bool m_running;

  std::map<IxpLinkId, IxpLinkState> m_links;
  std::map<IxpLinkId, Ptr<IxpEndpointAdapter>> m_endpoints;
  IxpAssignment m_assignment;
};

class ScionIxpEndpointAdapter : public IxpEndpointAdapter
{
public:
  static TypeId GetTypeId (void);

  ScionIxpEndpointAdapter ();
  ~ScionIxpEndpointAdapter () override;

  void Bind (uint32_t asn, Ptr<Node> asNode, uint16_t scionIfId);

  void OnInterfaceUp (const IxpLinkId &link, uint32_t portId, Time now) override;
  void OnInterfaceDown (const IxpLinkId &link, uint32_t portId, Time now) override;
  void DeliverFromFabric (const IxpLinkId &dst, Ptr<Packet> packet) override;

private:
  void SetScionIfState (bool up, Time now);

  uint32_t m_asn;
  Ptr<Node> m_asNode;
  uint16_t m_scionIfId;

  bool m_hasOriginalRemote;
  uint16_t m_originalRemoteAs;
  int32_t m_originalRelation;
};

class BgpIxpEndpointAdapter : public IxpEndpointAdapter
{
public:
  static TypeId GetTypeId (void);

  BgpIxpEndpointAdapter ();
  ~BgpIxpEndpointAdapter () override;

  void Bind (uint32_t asn, Ptr<Node> node, uint32_t ipv4IfIndex);

  void OnInterfaceUp (const IxpLinkId &link, uint32_t portId, Time now) override;
  void OnInterfaceDown (const IxpLinkId &link, uint32_t portId, Time now) override;
  void DeliverFromFabric (const IxpLinkId &dst, Ptr<Packet> packet) override;

private:
  void SetIpv4IfState (bool up);

  uint32_t m_asn;
  Ptr<Node> m_node;
  uint32_t m_ipv4IfIndex;
};

} // namespace ns3

#endif // SCION_SIMULATOR_VIRTUAL_IXP_FABRIC_H
