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

#ifndef SCION_SIMULATOR_SCION_HOST_H
#define SCION_SIMULATOR_SCION_HOST_H

#include <fstream>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

#include "ns3/event-id.h"
#include "ns3/nstime.h"

#include "path-segment.h"
#include "scion-capable-node.h"
#include "scion-packet.h"

namespace ns3 {
class ScionHost : public ScionCapableNode
{
public:
  ScionHost (uint32_t system_id, uint16_t isd_number, uint16_t as_number, host_addr_t local_address,
             double latitude, double longitude, ScionAs *as)
      : ScionCapableNode (system_id, isd_number, as_number, local_address, latitude, longitude, as)
  {
  }

  void SendArbitraryPacket (ia_t dst_ia, host_addr_t dst_host);
  void RequestPathsToDestination (ia_t dst_ia);
  bool TryGetCachedPath (ia_t dst_ia, std::vector<const PathSegment *> &path,
                         std::vector<uint8_t> &shortcuts);
  uint32_t ConfigureDataPlaneProbe (ia_t dst_ia, host_addr_t dst_host, uint16_t src_as_real,
                                    uint16_t dst_as_real, uint32_t count, Time interval,
                                    Time timeout, Time start_time, const std::string &output_path);
  void ConfigureFirstHopPreference (uint16_t preferred_neighbor_real_as,
                                    uint16_t backup_neighbor_real_as = 0);
  void InvalidateAllCachedPathSegments ();
  void ClearProbeSessionBlacklists ();
  void EvictCachedSegmentsUsingLink (uint16_t as_alias, uint16_t scion_if);

protected:
  struct ProbeSession
  {
    ia_t dst_ia;
    host_addr_t dst_host;
    uint16_t src_as_real;
    uint16_t dst_as_real;
    uint32_t count;
    uint32_t next_seq;
    Time interval;
    Time timeout;
    std::string output_path;
    std::ofstream out;
    EventId send_event;
    std::unordered_map<uint32_t, Time> send_times;
    std::unordered_map<uint32_t, EventId> timeout_events;
    uint32_t consecutive_timeouts;
    bool force_path_refresh;
    bool started;
    const PathSegment *last_sent_segment;
    std::set<const PathSegment *> timed_out_segments;

    ProbeSession ()
        : dst_ia (0),
          dst_host (0),
          src_as_real (0),
          dst_as_real (0),
          count (0),
          next_seq (0),
          interval (Seconds (1.0)),
          timeout (Seconds (2.0)),
          consecutive_timeouts (0),
          force_path_refresh (false),
          started (false),
          last_sent_segment (nullptr)
    {
    }
  };

  cached_path_segs_dataset_t cached_up_path_segments;
  cached_path_segs_dataset_t cached_core_path_segments;
  cached_path_segs_dataset_t cached_down_path_segments;

  std::vector<ProbeSession> probe_sessions;
  std::vector<std::unique_ptr<PathSegment>> synthesized_path_segments;
  uint16_t m_preferred_first_hop_real_as = 0;
  uint16_t m_backup_first_hop_real_as = 0;

  virtual void ProcessReceivedPacket (uint16_t local_if, ScionPacket *packet,
                                      Time receive_time) override;
  virtual void ModifyPktUponSend (ScionPacket *packet) override;
  void RemoveExpiredSegments ();
  void SearchInCachedSegments (ia_t dst_ia, std::vector<const PathSegment *> &path,
                               std::vector<uint8_t> &shortcuts,
                               const std::set<const PathSegment *> *skip_segments = nullptr,
                               const PathSegment **selected_segment_out = nullptr);
  const PathSegment *SelectPreferredSegment (const cached_path_segs_per_src_dst_t *segments) const;
  bool MatchesFirstHopPreference (const PathSegment *segment, uint16_t real_as) const;
  bool TrySynthesizePathFromBeaconStore (ia_t dst_ia, std::vector<const PathSegment *> &path);
  void RequestForPathSegments (ia_t dst_ia);
  void SendRequestForPathSegments (PathSegmentType seg_type, ia_t src_ia, ia_t dst_ia);
  void InvalidateCachedPathsToDestination (ia_t dst_ia);

  void ReceiveRegisteredPathSegments (PathSegmentType seg_type, ia_t src_ia, ia_t dst_ia,
                                      const reg_path_segs_to_one_as_t *path_segments);
  void ReceiveCachedPathSegments (PathSegmentType seg_type, ia_t src_ia, ia_t dst_ia,
                                  cached_path_segs_per_dst_t *path_seg);

  void CachePathSegment (PathSegmentType seg_type, ia_t src_ia, ia_t dst_ia, PathSegment *path_seg);

  void StartProbeSession (uint32_t session_id);
  void SendProbeTick (uint32_t session_id);
  void TrySendProbe (uint32_t session_id, uint32_t seq, uint32_t retry_count);
  void OnProbeTimeout (uint32_t session_id, uint32_t seq);
  void TrySendProbeReply (Payload reply_payload, ia_t dst_ia, host_addr_t dst_host,
                          uint32_t retry_count);
  void HandleProbeReply (ScionPacket *packet, Time receive_time);
  void WriteProbeRow (ProbeSession &session, uint32_t seq, const std::string &event,
                      const std::string &rtt_ms, const std::string &path_used, Time event_time);
  std::string FormatPacketPathForCsv (const ScionPacket *packet) const;
};
} // namespace ns3

#endif //SCION_SIMULATOR_SCION_HOST_H
