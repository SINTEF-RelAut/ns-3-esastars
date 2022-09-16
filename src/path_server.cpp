//
// Created by seyedali on 19.07.21.
//

#include <iostream>

#include "ns3/log.h"

#include "src/SCION/headers/path_server.h"
#include "src/SCION/headers/scion_core_as.h"
#include "src/SCION/headers/scion_host.h"

namespace ns3 {
NS_LOG_COMPONENT_DEFINE ("PathServer");

void
PathServer::ProcessReceivedPacket (uint16_t localIf, ScionPacket *packet, Time receiveTime)
{
  NS_ASSERT (packet->dstIa == iaAddr && packet->dstHost == localAddress);
  ScionCapableNode::ProcessReceivedPacket (localIf, packet, receiveTime);

  if (packet->payloadType == PayloadType::pathReqFromHost && packet->srcIa == iaAddr)
    {
      PathReqFromHost pathReqFromHost = packet->payload.pathReqFromHost;
      ProcessLocalHostRequestForPath (pathReqFromHost.segType, pathReqFromHost.srcIa,
                                      pathReqFromHost.dstIa, packet->srcHost);

      packet->packetOriginator->DestroyScionPacket (packet);
      return;
    }

  if (packet->payloadType == PayloadType::reqForListOfAllCoreAses &&
      packet->srcIa == iaAddr)
    {
      NS_LOG_FUNCTION ("PthSrv rcv REQ_FOR_LIST_OF_ALL_CORE_ASES from " << packet->srcHost);
      ReturnListOfAllCoreAses (packet->srcHost);
      packet->packetOriginator->DestroyScionPacket (packet);
      return;
    }
}

void
PathServer::RegisterCorePathSegment (PathSegment &pathSegment, std::string key)
{
  pathSegment.reverse = true;
  if (registeredCoreSegments.find (pathSegment.originator) == registeredCoreSegments.end ())
    {
      registeredCoreSegments.insert (
          std::make_pair (pathSegment.originator, new RegPathSegsToOneAs_t ()));
      setOfAllCoreAses.insert (pathSegment.originator);
    }

  if (registeredCoreSegments.at (pathSegment.originator)->find (key) ==
      registeredCoreSegments.at (pathSegment.originator)->end ())
    {
      registeredCoreSegments.at (pathSegment.originator)
          ->insert (std::make_pair (key, new PathSegment (pathSegment)));
      return;
    }

  registeredCoreSegments.at (pathSegment.originator)->at (key)->initiationTime =
      pathSegment.initiationTime;
  registeredCoreSegments.at (pathSegment.originator)->at (key)->expirationTime =
      pathSegment.expirationTime;
}

void
PathServer::RegisterUpPathSegment (PathSegment &pathSegment, std::string key)
{
  pathSegment.reverse = true;
  if (registeredUpSegments.find (pathSegment.originator) == registeredUpSegments.end ())
    {
      registeredUpSegments.insert (
          std::make_pair (pathSegment.originator, new RegPathSegsToOneAs_t ()));
    }

  if (registeredUpSegments.at (pathSegment.originator)->find (key) ==
      registeredUpSegments.at (pathSegment.originator)->end ())
    {
      registeredUpSegments.at (pathSegment.originator)
          ->insert (std::make_pair (key, new PathSegment (pathSegment)));
      return;
    }

  registeredUpSegments.at (pathSegment.originator)->at (key)->initiationTime =
      pathSegment.initiationTime;
  registeredUpSegments.at (pathSegment.originator)->at (key)->expirationTime =
      pathSegment.expirationTime;
}
void
PathServer::RegisterDownPathSegment (PathSegment &pathSegment, std::string key)
{
  pathSegment.reverse = false;
}

void
PathServer::ProcessLocalHostRequestForPath (PathSegmentType pathType, Ia_t srcIa, Ia_t dstIa,
                                            HostAddr_t hostAddr)
{
  if (pathType == PathSegmentType::upSeg)
    {
      NS_LOG_FUNCTION ("Received up path segment request from "
                       << isdNumber << ":" << asNumber << ":" << hostAddr << " between "
                       << GET_ISDN (srcIa) << ":" << GET_ASN (srcIa) << " and "
                       << GET_ISDN (dstIa) << ":" << GET_ASN (dstIa));
      return; // TODO
    }

  if (pathType == PathSegmentType::downSeg)
    {
      NS_LOG_FUNCTION ("Received down path segment request from "
                       << isdNumber << ":" << asNumber << ":" << hostAddr << " between "
                       << GET_ISDN (srcIa) << ":" << GET_ASN (srcIa) << " and "
                       << GET_ISDN (dstIa) << ":" << GET_ASN (dstIa));
      return; // TODO
    }

  if (pathType == PathSegmentType::coreSeg && dynamic_cast<ScionCoreAs *> (as) == NULL)
    {
      NS_LOG_FUNCTION ("non-core as received core path segment request from "
                       << isdNumber << ":" << asNumber << ":" << hostAddr << " between "
                       << GET_ISDN (srcIa) << ":" << GET_ASN (srcIa) << " and "
                       << GET_ISDN (dstIa) << ":" << GET_ASN (dstIa));
      if (dstIa == 0)
        {
          return; //TODO
        }
      else
        {
          return; // TODO
        }
    }

  if (pathType == PathSegmentType::coreSeg && dynamic_cast<ScionCoreAs *> (as) != NULL)
    {
      NS_LOG_FUNCTION ("Core AS received core path segment request from "
                       << isdNumber << ":" << asNumber << ":" << hostAddr << " between "
                       << GET_ISDN (srcIa) << ":" << GET_ASN (srcIa) << " and "
                       << GET_ISDN (dstIa) << ":" << GET_ASN (dstIa));
      if (dstIa == 0)
        {
          for (auto const &[registeredDstIa, pathsToDstIa] : registeredCoreSegments)
            {
              NS_LOG_FUNCTION (GET_ISDN (registeredDstIa)
                               << ":" << GET_ASN (registeredDstIa) << " " << GET_ISDN (iaAddr)
                               << ":" << GET_ASN (iaAddr));
              if (GET_ISDN (registeredDstIa) == isdNumber)
                {
                  SendRegisteredPathToLocalHost (hostAddr, PathSegmentType::coreSeg, iaAddr,
                                                 registeredDstIa, pathsToDstIa);
                }
            }
        }
      else
        {
          for (auto const &[registeredDstIa, pathsToDstIa] : registeredCoreSegments)
            {
              NS_LOG_FUNCTION (GET_ISDN (registeredDstIa)
                               << ":" << GET_ASN (registeredDstIa) << " " << GET_ISDN (dstIa)
                               << ":" << GET_ASN (dstIa));
              if (GET_ISDN (registeredDstIa) == GET_ISDN (dstIa))
                {
                  SendRegisteredPathToLocalHost (hostAddr, PathSegmentType::coreSeg, iaAddr,
                                                 registeredDstIa, pathsToDstIa);
                }
            }
        }
    }
}

void
PathServer::SendRegisteredPathToLocalHost (HostAddr_t hostAddr, PathSegmentType pathType,
                                           Ia_t srcIa, Ia_t dstIa,
                                                const RegPathSegsToOneAs_t *pathsToDstIa)
{
  PayloadType payloadType = PayloadType::regPathsFromLocalPs;
  Payload payload;
  payload.registeredPathsFromLocalPs.segType = pathType;
  payload.registeredPathsFromLocalPs.srcIa = srcIa;
  payload.registeredPathsFromLocalPs.dstIa = dstIa;
  payload.registeredPathsFromLocalPs.registeredPathSegments = pathsToDstIa;

  ScionPacket *packet = CreateScionPacket (payload, payloadType, iaAddr, hostAddr, 0);
  SendScionPacket (packet);
}

void
PathServer::ReturnListOfAllCoreAses (HostAddr_t hostAddr)
{
  NS_LOG_FUNCTION ("PthSrv snd LIST_OF_ALL_CORE_ASES to " << hostAddr);
  PayloadType payloadType = PayloadType::listOfAllCoreAses;
  Payload payload;
  payload.listOfAllAses.setOfAllAses = &setOfAllCoreAses;

  ScionPacket *packet = CreateScionPacket (payload, payloadType, iaAddr, hostAddr, 0);
  SendScionPacket (packet);
}
} // namespace ns3
