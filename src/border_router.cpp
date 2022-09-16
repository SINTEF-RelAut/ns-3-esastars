//
// Created by seyedali on 28.07.21.
//

#include "src/SCION/headers/border_router.h"
#include "src/SCION/headers/scion_packet.h"

namespace ns3 {
NS_LOG_COMPONENT_DEFINE ("BorderRouter");
void
BorderRouter::ProcessReceivedPacket (uint16_t localIf, ScionPacket *packet, Time receiveTime)
{
  NS_LOG_FUNCTION ("packet received " << packet);
  NS_LOG_FUNCTION (
      isdNumber
      << ":" << asNumber << " packet from " << GET_ISDN (packet->srcIa) << ":"
      << GET_ASN (packet->srcIa) << " to " << GET_ISDN (packet->dstIa) << ":"
      << GET_ASN (packet->dstIa) << ", currIF: " << packet->currInf
      << ", currHopF: " << packet->curHopf << ", path segments: " << packet->path.size ()
      << ", current hop field: isd: "
      << GET_HOP_ISD (packet->path.at (packet->currInf)->hops.at (packet->curHopf))
      << ", as:" << GET_HOP_AS (packet->path.at (packet->currInf)->hops.at (packet->curHopf))
      << ", ing:" << GET_HOP_ING_IF (packet->path.at (packet->currInf)->hops.at (packet->curHopf))
      << ", eg:" << GET_HOP_EG_IF (packet->path.at (packet->currInf)->hops.at (packet->curHopf)));

  ScionCapableNode::ProcessReceivedPacket (localIf, packet, Time ());

  if (packet->srcIa == packet->dstIa)
    {
      return;
    }

  if (packet->dstIa == iaAddr)
    {
      NS_ASSERT (GET_HOP_ISD (packet->path.at (packet->currInf)->hops.at (packet->curHopf)) ==
                 isdNumber);
      NS_ASSERT (GET_HOP_AS (packet->path.at (packet->currInf)->hops.at (packet->curHopf)) ==
                 asNumber);

      if (forwardingTableToAddressesInsideAs.find (packet->dstHost) ==
          forwardingTableToAddressesInsideAs.end ())
        {
          NS_LOG_FUNCTION ("Address not in the forwarding table");
          return;
        }

      uint16_t localIfToSend = forwardingTableToAddressesInsideAs.at (packet->dstHost);
      ScheduleForSend (localIfToSend, packet);

      return;
    }

  bool receivedFromLocalAs = std::get<2> (remoteNodesInfo.at (localIf));

  if (!receivedFromLocalAs)
    {
      if (packet->pathReversed && packet->curHopf == 0)
        {
          packet->currInf--;
          packet->curHopf = packet->path.at (packet->currInf)->hops.size () - 1;
        }
      else if (!packet->pathReversed &&
               packet->curHopf == packet->path.at (packet->currInf)->hops.size () - 1)
        {
          packet->currInf++;
          packet->curHopf = 0;
        }
      else if (packet->shortcutHopfs.size () == 2 && packet->path.size () == 2)
        {
          if (packet->shortcutHopfs.at (packet->currInf) == packet->curHopf)
            {
              if (packet->pathReversed)
                {
                  packet->currInf--;
                }
              else
                {
                  packet->currInf++;
                }
            }
          packet->curHopf = packet->shortcutHopfs.at (packet->currInf);
        }
    }

  NS_ASSERT (packet->currInf >= 0);
  NS_ASSERT (packet->currInf < packet->path.size ());

  uint64_t hopf = packet->path.at (packet->currInf)->hops.at (packet->curHopf);
  NS_ASSERT (GET_HOP_ISD (hopf) == isdNumber);
  NS_ASSERT (GET_HOP_AS (hopf) == asNumber);
  bool reverse = packet->pathReversed ^ packet->path.at (packet->currInf)->reverse;

  uint16_t asIfToSend;
  if (reverse)
    {
      asIfToSend = GET_HOP_ING_IF (hopf);
    }
  else
    {
      asIfToSend = GET_HOP_EG_IF (hopf);
    }

  if (receivedFromLocalAs)
    {
      if (packet->pathReversed)
        {
          packet->curHopf--;
        }
      else
        {
          packet->curHopf++;
        }
    }

  NS_ASSERT (packet->curHopf >= 0);
  NS_ASSERT (packet->curHopf < packet->path.at (packet->currInf)->hops.size ());

  uint16_t localIfToSend = forwardingTableToOtherAsIfaces.at (asIfToSend);
  ScheduleForSend (localIfToSend, packet);
}

} // namespace ns3