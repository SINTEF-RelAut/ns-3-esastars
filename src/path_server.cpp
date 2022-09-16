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
PathServer::ProcessReceivedPacket (uint16_t local_if, ScionPacket *packet, Time receive_time)
{
  NS_ASSERT (packet->dst_ia == ia_addr && packet->dst_host == local_address);
  ScionCapableNode::ProcessReceivedPacket (local_if, packet, receive_time);

  if (packet->payload_type == PayloadType::PATH_REQ_FROM_HOST && packet->src_ia == ia_addr)
    {
      PathReqFromHost path_req_from_host = packet->payload.path_req_from_host;
      ProcessLocalHostRequestForPath (path_req_from_host.seg_type, path_req_from_host.src_ia,
                                      path_req_from_host.dst_ia, packet->src_host);

      packet->packet_originator->DestroyScionPacket (packet);
      return;
    }

  if (packet->payload_type == PayloadType::REQ_FOR_LIST_OF_ALL_CORE_ASES &&
      packet->src_ia == ia_addr)
    {
      NS_LOG_FUNCTION ("PthSrv rcv REQ_FOR_LIST_OF_ALL_CORE_ASES from " << packet->src_host);
      ReturnListOfAllCoreAses (packet->src_host);
      packet->packet_originator->DestroyScionPacket (packet);
      return;
    }
}

void
PathServer::RegisterCorePathSegment (PathSegment &pathSegment, std::string key)
{
  pathSegment.reverse = true;
  if (registered_core_segments.find (pathSegment.originator) == registered_core_segments.end ())
    {
      registered_core_segments.insert (
          std::make_pair (pathSegment.originator, new reg_path_segs_to_one_as_t ()));
      set_of_all_core_ases.insert (pathSegment.originator);
    }

  if (registered_core_segments.at (pathSegment.originator)->find (key) ==
      registered_core_segments.at (pathSegment.originator)->end ())
    {
      registered_core_segments.at (pathSegment.originator)
          ->insert (std::make_pair (key, new PathSegment (pathSegment)));
      return;
    }

  registered_core_segments.at (pathSegment.originator)->at (key)->initiation_time =
      pathSegment.initiation_time;
  registered_core_segments.at (pathSegment.originator)->at (key)->expiration_time =
      pathSegment.expiration_time;
}

void
PathServer::RegisterUpPathSegment (PathSegment &pathSegment, std::string key)
{
  pathSegment.reverse = true;
  if (registered_up_segments.find (pathSegment.originator) == registered_up_segments.end ())
    {
      registered_up_segments.insert (
          std::make_pair (pathSegment.originator, new reg_path_segs_to_one_as_t ()));
    }

  if (registered_up_segments.at (pathSegment.originator)->find (key) ==
      registered_up_segments.at (pathSegment.originator)->end ())
    {
      registered_up_segments.at (pathSegment.originator)
          ->insert (std::make_pair (key, new PathSegment (pathSegment)));
      return;
    }

  registered_up_segments.at (pathSegment.originator)->at (key)->initiation_time =
      pathSegment.initiation_time;
  registered_up_segments.at (pathSegment.originator)->at (key)->expiration_time =
      pathSegment.expiration_time;
}
void
PathServer::RegisterDownPathSegment (PathSegment &pathSegment, std::string key)
{
  pathSegment.reverse = false;
}

void
PathServer::ProcessLocalHostRequestForPath (PathSegmentType path_type, ia_t src_ia,
                                                 ia_t dst_ia, host_addr_t host_addr)
{
  if (path_type == PathSegmentType::UP_SEG)
    {
      NS_LOG_FUNCTION ("Received up path segment request from "
                       << isd_number << ":" << as_number << ":" << host_addr << " between "
                       << GET_ISDN (src_ia) << ":" << GET_ASN (src_ia) << " and "
                       << GET_ISDN (dst_ia) << ":" << GET_ASN (dst_ia));
      return; // TODO
    }

  if (path_type == PathSegmentType::DOWN_SEG)
    {
      NS_LOG_FUNCTION ("Received down path segment request from "
                       << isd_number << ":" << as_number << ":" << host_addr << " between "
                       << GET_ISDN (src_ia) << ":" << GET_ASN (src_ia) << " and "
                       << GET_ISDN (dst_ia) << ":" << GET_ASN (dst_ia));
      return; // TODO
    }

  if (path_type == PathSegmentType::CORE_SEG && dynamic_cast<ScionCoreAs *> (AS) == NULL)
    {
      NS_LOG_FUNCTION ("non-core as received core path segment request from "
                       << isd_number << ":" << as_number << ":" << host_addr << " between "
                       << GET_ISDN (src_ia) << ":" << GET_ASN (src_ia) << " and "
                       << GET_ISDN (dst_ia) << ":" << GET_ASN (dst_ia));
      if (dst_ia == 0)
        {
          return; //TODO
        }
      else
        {
          return; // TODO
        }
    }

  if (path_type == PathSegmentType::CORE_SEG && dynamic_cast<ScionCoreAs *> (AS) != NULL)
    {
      NS_LOG_FUNCTION ("Core AS received core path segment request from "
                       << isd_number << ":" << as_number << ":" << host_addr << " between "
                       << GET_ISDN (src_ia) << ":" << GET_ASN (src_ia) << " and "
                       << GET_ISDN (dst_ia) << ":" << GET_ASN (dst_ia));
      if (dst_ia == 0)
        {
          for (auto const &[registered_dst_ia, paths_to_dst_ia] : registered_core_segments)
            {
              NS_LOG_FUNCTION (GET_ISDN (registered_dst_ia)
                               << ":" << GET_ASN (registered_dst_ia) << " " << GET_ISDN (ia_addr)
                               << ":" << GET_ASN (ia_addr));
              if (GET_ISDN (registered_dst_ia) == isd_number)
                {
                  SendRegisteredPathToLocalHost (host_addr, PathSegmentType::CORE_SEG, ia_addr,
                                                 registered_dst_ia, paths_to_dst_ia);
                }
            }
        }
      else
        {
          for (auto const &[registered_dst_ia, paths_to_dst_ia] : registered_core_segments)
            {
              NS_LOG_FUNCTION (GET_ISDN (registered_dst_ia)
                               << ":" << GET_ASN (registered_dst_ia) << " " << GET_ISDN (dst_ia)
                               << ":" << GET_ASN (dst_ia));
              if (GET_ISDN (registered_dst_ia) == GET_ISDN (dst_ia))
                {
                  SendRegisteredPathToLocalHost (host_addr, PathSegmentType::CORE_SEG, ia_addr,
                                                 registered_dst_ia, paths_to_dst_ia);
                }
            }
        }
    }
}

void
PathServer::SendRegisteredPathToLocalHost (host_addr_t host_addr, PathSegmentType path_type,
                                                ia_t src_ia, ia_t dst_ia,
                                                const reg_path_segs_to_one_as_t *paths_to_dst_ia)
{
  PayloadType payload_type = PayloadType::REG_PATHS_FROM_LOCAL_PS;
  Payload payload;
  payload.registered_paths_from_local_ps.seg_type = path_type;
  payload.registered_paths_from_local_ps.src_ia = src_ia;
  payload.registered_paths_from_local_ps.dst_ia = dst_ia;
  payload.registered_paths_from_local_ps.registered_path_segments = paths_to_dst_ia;

  ScionPacket *packet = CreateScionPacket (payload, payload_type, ia_addr, host_addr, 0);
  SendScionPacket (packet);
}

void
PathServer::ReturnListOfAllCoreAses (host_addr_t host_addr)
{
  NS_LOG_FUNCTION ("PthSrv snd LIST_OF_ALL_CORE_ASES to " << host_addr);
  PayloadType payload_type = PayloadType::LIST_OF_ALL_CORE_ASES;
  Payload payload;
  payload.list_of_all_ases.set_of_all_ases = &set_of_all_core_ases;

  ScionPacket *packet = CreateScionPacket (payload, payload_type, ia_addr, host_addr, 0);
  SendScionPacket (packet);
}
} // namespace ns3
