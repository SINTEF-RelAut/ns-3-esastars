//
// Created by seyedali on 19.07.21.
//
#include <vector>

#include "ns3/ptr.h"

#include "src/SCION/headers/path_segment.h"
#include "src/SCION/headers/path_server.h"
#include "src/SCION/headers/scion_core_as.h"
#include "src/SCION/headers/scion_host.h"

namespace ns3 {
NS_LOG_COMPONENT_DEFINE ("SCIONHost");

void
ScionHost::ReceiveRegisteredPathSegments (PathSegmentType segType, Ia_t srcIa, Ia_t dstIa,
                                             const RegPathSegsToOneAs_t *pathSegments)
{
  NS_LOG_FUNCTION ("I am host " << isdNumber << ":" << asNumber << ":" << localAddress
                                << ". Registered paths fetched: from " << srcIa << " "
                                << GET_ISDN (srcIa) << ":" << GET_ASN (srcIa) << " to "
                                << GET_ISDN (dstIa) << ":" << GET_ASN (dstIa)
                                << " number of segments: " << pathSegments->size ());

  for (auto const &keyPathSegmentPair : *pathSegments)
    {
      PathSegment *pathSegment = keyPathSegmentPair.second;
      if (pathSegment->expirationTime > localTime.GetMinutes ())
        {
          CachePathSegment (segType, srcIa, dstIa, pathSegment);
        }
    }
}

void
ScionHost::ReceiveCachedPathSegments (PathSegmentType segType, Ia_t srcIa, Ia_t dstIa,
                                      CachedPathSegsPerDst_t *pathSeg)
{
}

void
ScionHost::RemoveExpiredSegments ()
{
}

void
ScionHost::RequestForPathSegments (Ia_t dstIa)
{
  uint16_t dstIsd = GET_ISDN (dstIa);

  SendRequestForPathSegments (PathSegmentType::upSeg, iaAddr, 0);
  if (dstIsd == isdNumber)
    {
      SendRequestForPathSegments (PathSegmentType::coreSeg, 0, 0);
      SendRequestForPathSegments (PathSegmentType::downSeg, 0, dstIa);
    }
  else
    {
      SendRequestForPathSegments (PathSegmentType::coreSeg, 0, MAKE_IA (dstIsd, 0));
      SendRequestForPathSegments (PathSegmentType::downSeg, MAKE_IA (dstIsd, 0), dstIa);
    }
}

void
ScionHost::CachePathSegment (PathSegmentType segType, Ia_t srcIa, Ia_t dstIa,
                               PathSegment *pathSeg)
{
  CachedPathSegsDataset_t *cachedPathSegsDataSet;

  if (segType == PathSegmentType::coreSeg)
    {
      cachedPathSegsDataSet = &cachedCorePathSegments;
    }
  else if (segType == PathSegmentType::upSeg)
    {
      cachedPathSegsDataSet = &cachedUpPathSegments;
    }
  else
    {
      cachedPathSegsDataSet = &cachedDownPathSegments;
    }

  if (cachedPathSegsDataSet->find (dstIa) == cachedPathSegsDataSet->end ())
    {
      cachedPathSegsDataSet->insert (
          std::make_pair (dstIa, new CachedPathSegsPerDst_t ()));
    }

  if (cachedPathSegsDataSet->at (dstIa)->find (srcIa) == cachedPathSegsDataSet->at (dstIa)->end ())
    {
      cachedPathSegsDataSet->at (dstIa)->insert (
          std::make_pair (srcIa, new CachedPathSegsPerSrcDst_t ()));
    }

  cachedPathSegsDataSet->at (dstIa)->at (srcIa)->insert (
      std::make_pair (pathSeg->hops.size (), pathSeg));
}

void
ScionHost::SendRequestForPathSegments (PathSegmentType segType, Ia_t srcIa, Ia_t dstIa)
{
  PayloadType payloadType = PayloadType::pathReqFromHost;

  Payload payload;
  payload.pathReqFromHost.srcIa = srcIa;
  payload.pathReqFromHost.dstIa = dstIa;
  payload.pathReqFromHost.segType = segType;

  ScionPacket *packet = CreateScionPacket (payload, payloadType, iaAddr, 1, 0);
  SendScionPacket (packet);
}

void
ScionHost::SearchInCachedSegments (Ia_t dstIa, std::vector<const PathSegment *> &path,
                                      std::vector<uint8_t> &shortcuts)
{
  if (dstIa == iaAddr)
    {
      return;
    }

  int16_t dstInWhichCache = -1;

  if (cachedCorePathSegments.find (dstIa) != cachedCorePathSegments.end ())
    {
      dstInWhichCache = 0;
    }
  else if (cachedUpPathSegments.find (dstIa) != cachedUpPathSegments.end ())
    {
      dstInWhichCache = 1;
    }
  else if (cachedDownPathSegments.find (dstIa) != cachedDownPathSegments.end ())
    {
      dstInWhichCache = 2;
    }

  if (dstInWhichCache == -1)
    {
      return;
    }

  if (dstInWhichCache == 0 && dynamic_cast<ScionCoreAs *> (as) != NULL &&
      cachedCorePathSegments.at (dstIa)->find (iaAddr) != cachedCorePathSegments.at (dstIa)->end ())
    {
      path.push_back (cachedCorePathSegments.at (dstIa)->at (iaAddr)->begin ()->second);
      return;
    }

  if (dstInWhichCache == 0 && dynamic_cast<ScionCoreAs *> (as) == NULL)
    {
      for (auto const &[coreSegSrcIa, corePathSegs] : *cachedCorePathSegments.at (dstIa))
        {
          if (cachedUpPathSegments.find (coreSegSrcIa) != cachedUpPathSegments.end ())
            {
              NS_ASSERT (cachedUpPathSegments.at (coreSegSrcIa)->find (iaAddr) !=
                         cachedUpPathSegments.at (dstIa)->end ());

              path.push_back (
                  cachedUpPathSegments.at (coreSegSrcIa)->at (iaAddr)->begin ()->second);
              path.push_back (corePathSegs->begin ()->second);

              return;
            }
        }
      return;
    }

  if (dstInWhichCache == 1 && dynamic_cast<ScionCoreAs *> (as) == NULL)
    {
      NS_ASSERT (cachedUpPathSegments.at (dstIa)->find (iaAddr) !=
                 cachedUpPathSegments.at (dstIa)->end ());
      path.push_back (cachedUpPathSegments.at (dstIa)->at (iaAddr)->begin ()->second);
      return;
    }

  if (dstInWhichCache == 2 && dynamic_cast<ScionCoreAs *> (as) != NULL)
    {
      if (cachedDownPathSegments.at (dstIa)->find (iaAddr) !=
          cachedDownPathSegments.at (dstIa)->end ())
        {
          path.push_back (
              cachedDownPathSegments.find (dstIa)->second->begin ()->second->begin ()->second);
          return;
        }

      for (auto const &[downSegSrcIa, downPathSegs] : *cachedDownPathSegments.at (dstIa))
        {
          if (cachedCorePathSegments.find (downSegSrcIa) != cachedUpPathSegments.end ())
            {
              path.push_back (
                  cachedCorePathSegments.at (downSegSrcIa)
                                      ->begin ()
                                      ->second->begin ()
                                      ->second);
              path.push_back (downPathSegs->begin ()->second);

              return;
            }
        }
      return;
    }

  if (dstInWhichCache == 2 && dynamic_cast<ScionCoreAs *> (as) == NULL)
    {
      for (auto const &[downSegSrcIa, downPathSegs] : *cachedDownPathSegments.at (dstIa))
        {
          if (cachedCorePathSegments.find (downSegSrcIa) != cachedCorePathSegments.end ())
            {
              for (auto const &[coreSegSrcIa, corePathSegs] :
                   *cachedCorePathSegments.at (downSegSrcIa))
                {
                  if (cachedUpPathSegments.find (coreSegSrcIa) != cachedUpPathSegments.end ())
                    {
                      path.push_back (
                          cachedUpPathSegments.at (coreSegSrcIa)
                                              ->at (iaAddr)
                                              ->begin ()
                                              ->second);
                      path.push_back (corePathSegs->begin ()->second);
                      path.push_back (downPathSegs->begin ()->second);

                      return;
                    }
                }
            }
        }
    }

  return;
}

void
ScionHost::ProcessReceivedPacket (uint16_t localIf, ScionPacket *packet, Time receiveTime)
{
  NS_ASSERT (packet->dstIa == iaAddr && packet->dstHost == localAddress);
  NS_LOG_FUNCTION ("I am host " << isdNumber << ":" << asNumber << ":" << localAddress
                                << ". Packet received from " << GET_ISDN (packet->srcIa) << ":"
                                << GET_ASN (packet->srcIa) << ":" << packet->srcHost);

  ScionCapableNode::ProcessReceivedPacket (localIf, packet, receiveTime);

  if (packet->payloadType == PayloadType::regPathsFromLocalPs)
    {
      RegPathsFromLocalPs registeredPathsFromLocalPs =
          packet->payload.registeredPathsFromLocalPs;
      ReceiveRegisteredPathSegments (
          registeredPathsFromLocalPs.segType, registeredPathsFromLocalPs.srcIa,
          registeredPathsFromLocalPs.dstIa, registeredPathsFromLocalPs.registeredPathSegments);
      packet->packetOriginator->DestroyScionPacket (packet);
      return;
    }
  /*
        if (packet->packet_originator == this) {
            NS_ASSERT(on_the_flight_packets.find(packet->id) != on_the_flight_packets.end());
            NS_ASSERT(&on_the_flight_packets.at(packet->id) == packet);
            NS_LOG_FUNCTION("I am host " << isd_number << ":" << as_number << ":" << local_address << ". Response received from " << GET_ISDN(packet->src_ia) << ":" << GET_ASN(packet->src_ia) << ":" << packet->src_host);

            DestroyScionPacket(packet);
            // The repose of a  previously-sent message has received; do whatever is necessary
        } else {
            ReturnScionPacket(packet);
        }
*/
}

void
ScionHost::SendArbitraryPacket (Ia_t dstIa, HostAddr_t dstHost)
{
  Payload payload;
  PayloadType payloadType = PayloadType::empty;

  if (dstIa == iaAddr)
    {
      ScionPacket *packet = CreateScionPacket (payload, payloadType, dstIa, dstHost, 0);
      SendScionPacket (packet);
    }
  else
    {
      std::vector<const PathSegment *> thePath;
      std::vector<uint8_t> shortcuts;

      SearchInCachedSegments (dstIa, thePath, shortcuts);

      if (thePath.size () != 0)
        {
          ScionPacket *packet =
              CreateScionPacket (payload, payloadType, dstIa, dstHost, 0, thePath, shortcuts);
          SendScionPacket (packet);
        }
      else
        {
          RequestForPathSegments (dstIa);
          Simulator::Schedule (MilliSeconds (300), &ScionHost::SendArbitraryPacket, this, dstIa,
                               dstHost);
        }
    }
}

void
ScionHost::ModifyPktUponSend (ScionPacket *packet)
{
}
} // namespace ns3
