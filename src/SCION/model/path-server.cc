/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2022 ETH Zuerich
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Seyedali Tabaeiaghdaei seyedali.tabaeiaghdaei@inf.ethz.ch
 */

#include <iostream>
#include <cstdlib>
#include <cstring>

#include "ns3/log.h"
#include "ns3/simulator.h"

#include "path-server.h"
#include "src/SCION/model/externs.h"
#include "scion-core-as.h"
#include "scion-host.h"

namespace ns3 {
NS_LOG_COMPONENT_DEFINE ("PathServer");

namespace {

int32_t
ResolveRealAsNo (uint16_t alias_as)
{
  if (alias_to_real_as_no.find (alias_as) != alias_to_real_as_no.end ())
    {
      return alias_to_real_as_no.at (alias_as);
    }

  return alias_as;
}

bool
IsDebugAs107 (uint16_t alias_as)
{
  const char *debug_env = std::getenv ("SCION_AS107_DEBUG");
  const bool debug_enabled = (debug_env != NULL && std::strcmp (debug_env, "1") == 0);
  return debug_enabled && ResolveRealAsNo (alias_as) == 107;
}

bool
IsDebugDst108 (ia_t dst_ia)
{
  return ResolveRealAsNo (GET_ASN (dst_ia)) == 108;
}

const char *
SegTypeToString (PathSegmentType seg_type)
{
  if (seg_type == PathSegmentType::UP_SEG)
    {
      return "UP";
    }
  if (seg_type == PathSegmentType::CORE_SEG)
    {
      return "CORE";
    }

  return "DOWN";
}

} // namespace

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
PathServer::RegisterCorePathSegment (PathSegment &path_segment, std::string key)
{
  path_segment.reverse = true;
  if (registered_core_segments.find (path_segment.originator) == registered_core_segments.end ())
    {
      registered_core_segments.insert (
          std::make_pair (path_segment.originator, new reg_path_segs_to_one_as_t ()));
      set_of_all_core_ases.insert (path_segment.originator);
    }

  if (registered_core_segments.at (path_segment.originator)->find (key) ==
      registered_core_segments.at (path_segment.originator)->end ())
    {
      registered_core_segments.at (path_segment.originator)
          ->insert (std::make_pair (key, new PathSegment (path_segment)));
      return;
    }

  registered_core_segments.at (path_segment.originator)->at (key)->initiation_time =
      path_segment.initiation_time;
  registered_core_segments.at (path_segment.originator)->at (key)->expiration_time =
      path_segment.expiration_time;
}

void
PathServer::RegisterUpPathSegment (PathSegment &path_segment, std::string key)
{
  path_segment.reverse = true;
  if (registered_up_segments.find (path_segment.originator) == registered_up_segments.end ())
    {
      registered_up_segments.insert (
          std::make_pair (path_segment.originator, new reg_path_segs_to_one_as_t ()));
    }

  if (registered_up_segments.at (path_segment.originator)->find (key) ==
      registered_up_segments.at (path_segment.originator)->end ())
    {
      registered_up_segments.at (path_segment.originator)
          ->insert (std::make_pair (key, new PathSegment (path_segment)));
      return;
    }

  registered_up_segments.at (path_segment.originator)->at (key)->initiation_time =
      path_segment.initiation_time;
  registered_up_segments.at (path_segment.originator)->at (key)->expiration_time =
      path_segment.expiration_time;
}
void
PathServer::RegisterDownPathSegment (PathSegment &path_segment, std::string key)
{
  path_segment.reverse = false;

  if (registered_down_segments.find (path_segment.originator) == registered_down_segments.end ())
    {
      registered_down_segments.insert (
          std::make_pair (path_segment.originator, new reg_path_segs_to_one_as_t ()));
    }

  if (registered_down_segments.at (path_segment.originator)->find (key) ==
      registered_down_segments.at (path_segment.originator)->end ())
    {
      registered_down_segments.at (path_segment.originator)
          ->insert (std::make_pair (key, new PathSegment (path_segment)));
      return;
    }

  registered_down_segments.at (path_segment.originator)->at (key)->initiation_time =
      path_segment.initiation_time;
  registered_down_segments.at (path_segment.originator)->at (key)->expiration_time =
      path_segment.expiration_time;
}

void
PathServer::ProcessLocalHostRequestForPath (PathSegmentType path_type, ia_t src_ia, ia_t dst_ia,
                                            host_addr_t host_addr)
{
  if (IsDebugAs107 (as_number))
    {
      std::cerr << "[AS107-PS-REQ] t=" << Simulator::Now ().GetSeconds () << " host=" << host_addr
                << " type=" << SegTypeToString (path_type) << " srcIA=" << GET_ASN (src_ia)
                << " dstIA=" << GET_ASN (dst_ia) << " upRegs=" << registered_up_segments.size ()
                << " coreRegs=" << registered_core_segments.size ()
                << " downRegs=" << registered_down_segments.size () << std::endl;
    }

  auto send_matching_registered_paths_to_host =
      [this, host_addr] (PathSegmentType seg_type, ia_t src_ia_filter, ia_t dst_ia_filter,
                         const registered_path_segs_dataset_t &registered_segments) {
        bool filter_by_dst = dst_ia_filter != 0;
        bool filter_by_src = src_ia_filter != 0;

        for (auto const &[registered_dst_ia, paths_to_dst_ia] : registered_segments)
          {
            if (filter_by_dst && registered_dst_ia != dst_ia_filter)
              {
                continue;
              }

            ia_t response_src_ia = src_ia_filter;
            if (filter_by_src)
              {
                if (paths_to_dst_ia->empty ())
                  {
                    continue;
                  }

                response_src_ia = paths_to_dst_ia->begin ()->second->originator;
                if (response_src_ia != src_ia_filter)
                  {
                    continue;
                  }
              }

            if (!filter_by_src)
              {
                response_src_ia = registered_dst_ia;
              }

            if (IsDebugAs107 (as_number))
              {
                std::cerr << "[AS107-PS-SEND] t=" << Simulator::Now ().GetSeconds ()
                          << " type=" << SegTypeToString (seg_type)
                          << " srcIA=" << GET_ASN (response_src_ia)
                          << " dstIA=" << GET_ASN (registered_dst_ia)
                          << " segCount=" << paths_to_dst_ia->size () << std::endl;
              }

            SendRegisteredPathToLocalHost (host_addr, seg_type, response_src_ia, registered_dst_ia,
                                           paths_to_dst_ia);
          }
      };

  if (path_type == PathSegmentType::UP_SEG)
    {
      NS_LOG_FUNCTION ("Received up path segment request from "
                       << isd_number << ":" << as_number << ":" << host_addr << " between "
                       << GET_ISDN (src_ia) << ":" << GET_ASN (src_ia) << " and "
                       << GET_ISDN (dst_ia) << ":" << GET_ASN (dst_ia));

      send_matching_registered_paths_to_host (PathSegmentType::UP_SEG, src_ia, dst_ia,
                                              registered_up_segments);
      return;
    }

  if (path_type == PathSegmentType::DOWN_SEG)
    {
      NS_LOG_FUNCTION ("Received down path segment request from "
                       << isd_number << ":" << as_number << ":" << host_addr << " between "
                       << GET_ISDN (src_ia) << ":" << GET_ASN (src_ia) << " and "
                       << GET_ISDN (dst_ia) << ":" << GET_ASN (dst_ia));

      auto get_down_src_ia = [] (const reg_path_segs_to_one_as_t *paths_to_dst_ia, ia_t fallback) {
        if (paths_to_dst_ia == NULL || paths_to_dst_ia->empty () ||
            paths_to_dst_ia->begin ()->second == NULL ||
            paths_to_dst_ia->begin ()->second->hops.empty ())
          {
            return fallback;
          }

        return GET_HOP_IA (paths_to_dst_ia->begin ()->second->hops.front ());
      };

      bool sent_matching = false;
      for (auto const &[registered_dst_ia, paths_to_dst_ia] : registered_down_segments)
        {
          if (dst_ia != 0 && registered_dst_ia != dst_ia)
            {
              continue;
            }

          SendRegisteredPathToLocalHost (host_addr, PathSegmentType::DOWN_SEG,
                                         get_down_src_ia (paths_to_dst_ia, registered_dst_ia),
                                         registered_dst_ia, paths_to_dst_ia);
          if (IsDebugAs107 (as_number))
            {
              std::cerr << "[AS107-PS-SEND] t=" << Simulator::Now ().GetSeconds () << " type=DOWN"
                        << " srcIA="
                        << GET_ASN (get_down_src_ia (paths_to_dst_ia, registered_dst_ia))
                        << " dstIA=" << GET_ASN (registered_dst_ia)
                        << " segCount=" << paths_to_dst_ia->size () << " exact=1" << std::endl;
            }
          sent_matching = true;
        }

      // Non-core local path servers may not always store down segments under
      // the destination IA key expected by host requests. If no exact match is
      // found, provide available down segments to seed destination cache.
      if (!sent_matching && dst_ia != 0 && dynamic_cast<ScionCoreAs *> (as) == NULL)
        {
          for (auto const &[registered_dst_ia, paths_to_dst_ia] : registered_down_segments)
            {
              SendRegisteredPathToLocalHost (host_addr, PathSegmentType::DOWN_SEG,
                                             get_down_src_ia (paths_to_dst_ia, registered_dst_ia),
                                             dst_ia, paths_to_dst_ia);
              if (IsDebugAs107 (as_number))
                {
                  std::cerr << "[AS107-PS-SEND] t=" << Simulator::Now ().GetSeconds ()
                            << " type=DOWN"
                            << " srcIA="
                            << GET_ASN (get_down_src_ia (paths_to_dst_ia, registered_dst_ia))
                            << " dstIA=" << GET_ASN (dst_ia)
                            << " segCount=" << paths_to_dst_ia->size () << " exact=0" << std::endl;
                }
            }
        }

      return;
    }

  if (path_type == PathSegmentType::CORE_SEG && dynamic_cast<ScionCoreAs *> (as) == NULL)
    {
      NS_LOG_FUNCTION ("non-core as received core path segment request from "
                       << isd_number << ":" << as_number << ":" << host_addr << " between "
                       << GET_ISDN (src_ia) << ":" << GET_ASN (src_ia) << " and "
                       << GET_ISDN (dst_ia) << ":" << GET_ASN (dst_ia));

      // For non-core hosts, send CORE segments based on ISD, not strict src/dst filtering
      // This allows path composition to use available CORE segments
      if (dst_ia == 0)
        {
          // Request for all CORE segments: send all available in any ISD
          for (auto const &[registered_dst_ia, paths_to_dst_ia] : registered_core_segments)
            {
              SendRegisteredPathToLocalHost (host_addr, PathSegmentType::CORE_SEG,
                                             registered_dst_ia, registered_dst_ia, paths_to_dst_ia);
              if (IsDebugAs107 (as_number))
                {
                  std::cerr << "[AS107-PS-SEND] t=" << Simulator::Now ().GetSeconds ()
                            << " type=CORE"
                            << " srcIA=" << GET_ASN (registered_dst_ia)
                            << " dstIA=" << GET_ASN (registered_dst_ia)
                            << " segCount=" << paths_to_dst_ia->size () << " allCore=1"
                            << std::endl;
                }
            }
        }
      else
        {
          // Request for CORE segments to specific destination ISD: send segments within that ISD
          for (auto const &[registered_dst_ia, paths_to_dst_ia] : registered_core_segments)
            {
              if (GET_ISDN (registered_dst_ia) == GET_ISDN (dst_ia))
                {
                  SendRegisteredPathToLocalHost (host_addr, PathSegmentType::CORE_SEG,
                                                 registered_dst_ia, registered_dst_ia,
                                                 paths_to_dst_ia);
                  if (IsDebugAs107 (as_number))
                    {
                      std::cerr << "[AS107-PS-SEND] t=" << Simulator::Now ().GetSeconds ()
                                << " type=CORE"
                                << " srcIA=" << GET_ASN (registered_dst_ia)
                                << " dstIA=" << GET_ASN (registered_dst_ia)
                                << " segCount=" << paths_to_dst_ia->size () << " isd_match=1"
                                << std::endl;
                    }
                }
            }
        }
      return;
    }

  if (path_type == PathSegmentType::CORE_SEG && dynamic_cast<ScionCoreAs *> (as) != NULL)
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

void
PathServer::RevokeSegmentsContainingLink (uint16_t as_alias, uint16_t scion_if)
{
  auto prune = [&] (registered_path_segs_dataset_t &dataset) {
    for (auto const &[originator_ia, per_orig] : dataset)
      {
        std::vector<std::string> to_erase;
        for (auto const &[key, seg] : *per_orig)
          {
            for (auto const &hop : seg->hops)
              {
                if (GET_HOP_AS (hop) == as_alias &&
                    (GET_HOP_ING_IF (hop) == scion_if || GET_HOP_EG_IF (hop) == scion_if))
                  {
                    to_erase.push_back (key);
                    break;
                  }
              }
          }
        for (auto const &key : to_erase)
          {
            delete per_orig->at (key);
            per_orig->erase (key);
          }
      }
  };

  prune (registered_core_segments);
  prune (registered_up_segments);
  prune (registered_down_segments);
}

} // namespace ns3
