//
// Created by seyedali on 29.07.21.
//

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"

#include "src/SCION/headers/scion_as.h"
#include "src/SCION/headers/scion_capable_node.h"

namespace ns3 {
NS_LOG_COMPONENT_DEFINE ("SCIONCapableDevice");

void
ScionCapableNode::ScheduleReceive (uint16_t localIf, ScionPacket *packet, Time propagationDelay)
{
  AdvanceLocalTime ();
  Simulator::Schedule (propagationDelay, &ScionCapableNode::Receive, this, localIf, packet);
}

void
ScionCapableNode::Receive (uint16_t localIf, ScionPacket *packet)
{
  AdvanceLocalTime ();
  processingQueueLength++;
  Time delay = processingThroughputDelay * processingQueueLength + processingDelay;
  Simulator::Schedule (delay, &ScionCapableNode::ProcessReceivedPacket, this, localIf, packet,
                       localTime);
}

void
ScionCapableNode::ProcessReceivedPacket (uint16_t localIf, ScionPacket *packet,
                                           Time receiveTime)
{
  AdvanceLocalTime ();
  processingQueueLength--;
  // Other tasks should be done in derived classes
}

void
ScionCapableNode::ScheduleForSend (uint16_t localIf, ScionPacket *packet)
{
  NS_LOG_FUNCTION (packet);
  transmissionQueuesLengths.at (localIf) += packet->size;
  Time delay = transmissionDelays.at (localIf) * transmissionQueuesLengths.at (localIf);
  Simulator::Schedule (delay, &ScionCapableNode::Send, this, localIf, packet);
}

void
ScionCapableNode::Send (uint16_t localIf, ScionPacket *packet)
{
  AdvanceLocalTime ();
  NS_LOG_FUNCTION (packet);
  transmissionQueuesLengths.at (localIf) -= packet->size;
  ScionCapableNode *remoteNode = std::get<0> (remoteNodesInfo.at (localIf));
  uint16_t remoteIf = std::get<1> (remoteNodesInfo.at (localIf));
  ModifyPktUponSend (packet);
  remoteNode->ScheduleReceive (remoteIf, packet, propagationDelays.at (localIf));
}

void
ScionCapableNode::AddToIfForwadingTable (uint16_t asIf, uint16_t localIf)
{
  forwardingTableToOtherAsIfaces.insert (std::make_pair (asIf, localIf));
}

void
ScionCapableNode::AddToAddressForwardingTable (HostAddr_t addr, uint16_t localIf)
{
  forwardingTableToAddressesInsideAs.insert (std::make_pair (addr, localIf));
}

HostAddr_t
ScionCapableNode::GetLocalAddress () const
{
  return localAddress;
}

double
ScionCapableNode::GetLatitude () const
{
  return latitude;
}
double
ScionCapableNode::GetLogitude () const
{
  return longitude;
}

void
ScionCapableNode::AddToPropagationDelays (Time delay)
{
  propagationDelays.push_back (delay);
}
void
ScionCapableNode::AddToTransmissionDelays (Time delay)
{
  transmissionDelays.push_back (delay);
}
void
ScionCapableNode::SetProcessingDelay (Time delay, Time throughputDelay)
{
  processingDelay = delay;
  processingThroughputDelay = throughputDelay;
}

void
ScionCapableNode::AddToRemoteNodesInfo (ScionCapableNode *remoteNode, uint16_t remoteIf,
                                        uint16_t remoteIsd, uint16_t remoteAs)
{
  if (remoteIsd == isdNumber && remoteAs == asNumber)
    {
      remoteNodesInfo.push_back (std::make_tuple (remoteNode, remoteIf, true));
    }
  else
    {
      remoteNodesInfo.push_back (std::make_tuple (remoteNode, remoteIf, false));
    }
}

void
ScionCapableNode::InitializeTransmissionQueues ()
{
  transmissionQueuesLengths.resize (GetNDevices ());
}

void
ScionCapableNode::DestroyScionPacket (ScionPacket *packet)
{
  NS_ASSERT (packet == &onTheFlightPackets.at (packet->id));
  onTheFlightPackets.erase (packet->id);
}

void
ScionCapableNode::SendScionPacket (ScionPacket *packet)
{
  NS_LOG_FUNCTION ("I am host " << isdNumber << ":" << asNumber << ":" << localAddress
                                << ". Packet sent to " << GET_ISDN (packet->dstIa) << ":"
                                << GET_ASN (packet->dstIa) << ":" << packet->dstHost);

  uint16_t localIfToSend;
  if (packet->dstIa != iaAddr)
    {
      uint64_t hopf = packet->path.at (packet->currInf)->hops.at (packet->curHopf);
      NS_ASSERT (GET_HOP_ISD (hopf) == isdNumber && GET_HOP_AS (hopf) == asNumber);
      bool reverse = packet->pathReversed ^ packet->path.at (packet->currInf)->reverse;

      NS_LOG_FUNCTION (reverse << " " << packet->pathReversed << " "
                               << packet->path.at (packet->currInf)->reverse);

      uint16_t asIfToSend;
      if (reverse)
        {
          asIfToSend = GET_HOP_ING_IF (hopf);
        }
      else
        {
          asIfToSend = GET_HOP_EG_IF (hopf);
        }

      NS_LOG_FUNCTION (
          " first hop field: isd: "
          << GET_HOP_ISD (packet->path.at (packet->currInf)->hops.at (packet->curHopf)) << ", as:"
          << GET_HOP_AS (packet->path.at (packet->currInf)->hops.at (packet->curHopf)) << ", ing:"
          << GET_HOP_ING_IF (packet->path.at (packet->currInf)->hops.at (packet->curHopf))
          << ", eg:"
          << GET_HOP_EG_IF (packet->path.at (packet->currInf)->hops.at (packet->curHopf)));
      NS_LOG_FUNCTION ("asIfToSend: " << asIfToSend);

      localIfToSend = forwardingTableToOtherAsIfaces.at (asIfToSend);
    }
  else
    {
      localIfToSend = forwardingTableToAddressesInsideAs.at (packet->dstHost);
    }

  ScheduleForSend (localIfToSend, packet);
}

ScionPacket *
ScionCapableNode::CreateScionPacket (const Payload &payload, PayloadType payloadType, Ia_t dstIa,
                                     HostAddr_t dstHost, int32_t payloadSize,
                                       const std::vector<const PathSegment *> &thePath,
                                       const std::vector<uint8_t> &shortcutHopfs)
{
  onTheFlightPackets.insert (
      std::make_pair (nextPacketId, ScionPacket (this, nextPacketId)));
  ScionPacket *packet = &onTheFlightPackets.at (nextPacketId);
  nextPacketId++;

  packet->srcIa = iaAddr;
  packet->dstIa = dstIa;
  packet->srcHost = localAddress;
  packet->dstHost = dstHost;

  packet->path = thePath;
  packet->shortcutHopfs = shortcutHopfs;
  packet->pathReversed = false;
  packet->currInf = 0;
  packet->curHopf = 0;

  packet->payloadType = payloadType;
  packet->payload = payload;

  packet->timestamp = localTime;
  packet->size = 14 + 12 + 24 + payloadSize; // MAC + Common Header + Address Header + payload size

  if (packet->path.size () > 0)
    {
      packet->size += 4 + packet->path.size () * 8; //Path Meta Hdr + Info fields
      for (auto const &pathSeg : packet->path)
        {
          packet->size += pathSeg->hops.size () * 12; // Hop Fields
        }
    }

  return packet;
}

void
ScionCapableNode::ReturnScionPacket (ScionPacket *packet)
{
  packet->dstHost = packet->srcHost;
  packet->dstIa = packet->srcIa;
  packet->srcIa = iaAddr;
  packet->srcHost = localAddress;

  packet->pathReversed = !packet->pathReversed;
  packet->timestamp = localTime;

  SendScionPacket (packet);
}

uint32_t
ScionCapableNode::GetNDevices (void) const
{
  return propagationDelays.size ();
}

void
ScionCapableNode::AdvanceLocalTime ()
{
  localTime = Simulator::Now ();
}

void
ScionCapableNode::ModifyPktUponSend (ScionPacket *packet)
{
}

Time
ScionCapableNode::GetLocalTime (void) const
{
  return localTime;
}
} // namespace ns3