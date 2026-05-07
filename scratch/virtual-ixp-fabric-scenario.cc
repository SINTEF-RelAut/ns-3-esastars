/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include <iostream>
#include <string>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/virtual-ixp-fabric.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("VirtualIxpFabricScenario");

class TestEndpointAdapter : public IxpEndpointAdapter
{
public:
  static TypeId
  GetTypeId (void)
  {
    static TypeId tid = TypeId ("ns3::TestEndpointAdapter")
                            .SetParent<IxpEndpointAdapter> ()
                            .AddConstructor<TestEndpointAdapter> ();
    return tid;
  }

  TestEndpointAdapter ()
      : m_isUp (false), m_lastPort (0), m_upEvents (0), m_downEvents (0), m_deliveries (0)
  {
  }

  void
  Bind (const std::string &name)
  {
    m_name = name;
  }

  void
  OnInterfaceUp (const IxpLinkId &link, uint32_t portId, Time now) override
  {
    (void) link;
    m_isUp = true;
    m_lastPort = portId;
    ++m_upEvents;
    NS_LOG_UNCOND ("[" << m_name << "] UP at t=" << now.GetSeconds () << "s on port " << portId);
  }

  void
  OnInterfaceDown (const IxpLinkId &link, uint32_t portId, Time now) override
  {
    (void) link;
    m_isUp = false;
    m_lastPort = portId;
    ++m_downEvents;
    NS_LOG_UNCOND ("[" << m_name << "] DOWN at t=" << now.GetSeconds () << "s from port "
                       << portId);
  }

  void
  DeliverFromFabric (const IxpLinkId &dst, Ptr<Packet> packet) override
  {
    (void) dst;
    (void) packet;
    ++m_deliveries;
    NS_LOG_UNCOND ("[" << m_name << "] deliver event #" << m_deliveries);
  }

  bool
  IsUp () const
  {
    return m_isUp;
  }

  uint32_t
  GetUpEvents () const
  {
    return m_upEvents;
  }

  uint32_t
  GetDownEvents () const
  {
    return m_downEvents;
  }

  uint32_t
  GetDeliveries () const
  {
    return m_deliveries;
  }

private:
  std::string m_name;
  bool m_isUp;
  uint32_t m_lastPort;
  uint32_t m_upEvents;
  uint32_t m_downEvents;
  uint32_t m_deliveries;
};

int
main (int argc, char *argv[])
{
  bool verbose = true;
  CommandLine cmd;
  cmd.AddValue ("verbose", "Print scenario logs", verbose);
  cmd.Parse (argc, argv);

  if (!verbose)
    {
      LogComponentDisable ("VirtualIxpFabricScenario", LOG_LEVEL_ALL);
    }

  Ptr<VirtualIxpFabric> fabric = CreateObject<VirtualIxpFabric> ();

  IxpConfig cfg;
  cfg.portCount = 2;
  cfg.rebalancePeriod = Seconds (0.5);
  cfg.holdDown = Seconds (0.0);
  fabric->Configure (cfg);

  const IxpLinkId linkA = {101, 1};
  const IxpLinkId linkB = {102, 1};
  const IxpLinkId linkC = {103, 1};

  Ptr<TestEndpointAdapter> epA = CreateObject<TestEndpointAdapter> ();
  Ptr<TestEndpointAdapter> epB = CreateObject<TestEndpointAdapter> ();
  Ptr<TestEndpointAdapter> epC = CreateObject<TestEndpointAdapter> ();
  epA->Bind ("AS101");
  epB->Bind ("AS102");
  epC->Bind ("AS103");

  fabric->RegisterEndpoint (linkA, epA);
  fabric->RegisterEndpoint (linkB, epB);
  fabric->RegisterEndpoint (linkC, epC);

  fabric->Start ();

  Simulator::Schedule (Seconds (0.0), &VirtualIxpFabric::SetFeasibleUp, fabric, linkA, 0.9);
  Simulator::Schedule (Seconds (0.0), &VirtualIxpFabric::SetFeasibleUp, fabric, linkB, 0.8);
  Simulator::Schedule (Seconds (0.2), &VirtualIxpFabric::InjectFromEndpoint, fabric, linkA,
                       Create<Packet> (64));

  Simulator::Schedule (Seconds (0.5), &VirtualIxpFabric::SetFeasibleUp, fabric, linkC, 1.0);
  Simulator::Schedule (Seconds (1.0), &VirtualIxpFabric::SetFeasibleDown, fabric, linkB);
  Simulator::Schedule (Seconds (1.2), &VirtualIxpFabric::InjectFromEndpoint, fabric, linkA,
                       Create<Packet> (64));

  Simulator::Schedule (Seconds (2.0), &VirtualIxpFabric::SetFeasibleDown, fabric, linkA);
  Simulator::Schedule (Seconds (2.3), &VirtualIxpFabric::Stop, fabric);
  Simulator::Stop (Seconds (2.5));

  Simulator::Run ();
  Simulator::Destroy ();

  bool pass = true;

  if (epA->GetUpEvents () < 1 || epA->GetDownEvents () < 1)
    {
      std::cerr << "FAIL: AS101 expected at least one up/down transition" << std::endl;
      pass = false;
    }

  if (epB->GetUpEvents () < 1 || epB->GetDownEvents () < 1)
    {
      std::cerr << "FAIL: AS102 expected at least one up/down transition" << std::endl;
      pass = false;
    }

  if (epC->GetUpEvents () < 1)
    {
      std::cerr << "FAIL: AS103 expected at least one attach event" << std::endl;
      pass = false;
    }

  if (epB->GetDeliveries () < 1)
    {
      std::cerr << "FAIL: AS102 expected at least one packet delivery before detach" << std::endl;
      pass = false;
    }

  if (epC->GetDeliveries () < 1)
    {
      std::cerr << "FAIL: AS103 expected at least one packet delivery after attach" << std::endl;
      pass = false;
    }

  if (!pass)
    {
      return 1;
    }

  std::cout << "PASS: virtual IXP scenario checks passed" << std::endl;
  return 0;
}
