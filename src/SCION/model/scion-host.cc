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

#include <cstring>
#include <cstdlib>
#include <vector>

#include <iomanip>
#include <sstream>

#include "ns3/ptr.h"
#include "ns3/simulator.h"

#include "src/SCION/model/beaconing/beacon-server.h"
#include "externs.h"
#include "path-segment.h"
#include "path-server.h"
#include "scion-core-as.h"
#include "scion-host.h"

namespace ns3 {
NS_LOG_COMPONENT_DEFINE ("ScionHost");

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

bool
PruneExpiredFromCacheDataset (cached_path_segs_dataset_t &dataset, uint16_t now_minutes)
{
  bool removed_any = false;
  for (auto dst_it = dataset.begin (); dst_it != dataset.end ();)
    {
      cached_path_segs_per_dst_t *per_dst = dst_it->second;
      for (auto src_it = per_dst->begin (); src_it != per_dst->end ();)
        {
          cached_path_segs_per_src_dst_t *per_src_dst = src_it->second;
          for (auto seg_it = per_src_dst->begin (); seg_it != per_src_dst->end ();)
            {
              const PathSegment *segment = seg_it->second;
              if (segment == NULL || segment->expiration_time <= now_minutes)
                {
                  seg_it = per_src_dst->erase (seg_it);
                  removed_any = true;
                  continue;
                }

              ++seg_it;
            }

          if (per_src_dst->empty ())
            {
              delete per_src_dst;
              src_it = per_dst->erase (src_it);
              continue;
            }

          ++src_it;
        }

      if (per_dst->empty ())
        {
          delete per_dst;
          dst_it = dataset.erase (dst_it);
          continue;
        }

      ++dst_it;
    }

  return removed_any;
}

void
EraseDstFromCacheDataset (cached_path_segs_dataset_t &dataset, ia_t dst_ia)
{
  auto it = dataset.find (dst_ia);
  if (it == dataset.end ())
    {
      return;
    }

  cached_path_segs_per_dst_t *per_dst = it->second;
  for (auto src_it = per_dst->begin (); src_it != per_dst->end (); ++src_it)
    {
      delete src_it->second;
    }
  delete per_dst;
  dataset.erase (it);
}

void
ClearCacheDataset (cached_path_segs_dataset_t &dataset)
{
  for (auto dst_it = dataset.begin (); dst_it != dataset.end (); ++dst_it)
    {
      cached_path_segs_per_dst_t *per_dst = dst_it->second;
      for (auto src_it = per_dst->begin (); src_it != per_dst->end (); ++src_it)
        {
          delete src_it->second;
        }
      delete per_dst;
    }
  dataset.clear ();
}

} // namespace

void
ScionHost::ReceiveRegisteredPathSegments (PathSegmentType seg_type, ia_t src_ia, ia_t dst_ia,
                                          const reg_path_segs_to_one_as_t *path_segments)
{
  NS_LOG_FUNCTION ("I am host " << isd_number << ":" << as_number << ":" << local_address
                                << ". Registered paths fetched: from " << src_ia << " "
                                << GET_ISDN (src_ia) << ":" << GET_ASN (src_ia) << " to "
                                << GET_ISDN (dst_ia) << ":" << GET_ASN (dst_ia)
                                << " number of segments: " << path_segments->size ());

  const uint16_t now_minutes = Simulator::Now ().GetMinutes ();

  for (auto const &key_path_segment_pair : *path_segments)
    {
      PathSegment *path_segment = key_path_segment_pair.second;

      // Debug output: log AS107 segments for all destinations (not just 108)
      if (IsDebugAs107 (as_number) && path_segment != NULL)
        {
          std::cerr << "[AS107-SEG-RX] t=" << Simulator::Now ().GetSeconds ()
                    << " segType=" << SegTypeToString (seg_type) << " srcIA=" << GET_ASN (src_ia)
                    << " dstIA=" << GET_ASN (dst_ia) << " hops=" << path_segment->hops.size ()
                    << " originator=" << GET_ASN (path_segment->originator)
                    << " key=" << key_path_segment_pair.first << std::endl;
        }

      if (path_segment->expiration_time > now_minutes)
        {
          CachePathSegment (seg_type, src_ia, dst_ia, path_segment);
        }
    }
}

void
ScionHost::ReceiveCachedPathSegments (PathSegmentType seg_type, ia_t src_ia, ia_t dst_ia,
                                      cached_path_segs_per_dst_t *path_seg)
{
}

void
ScionHost::RemoveExpiredSegments ()
{
  // Probe retries can run while the host has not recently processed a packet, so
  // local_time may lag behind simulation time here. Expiry checks must therefore
  // use Simulator::Now() directly.
  uint16_t now_minutes = Simulator::Now ().GetMinutes ();
  PruneExpiredFromCacheDataset (cached_up_path_segments, now_minutes);
  PruneExpiredFromCacheDataset (cached_core_path_segments, now_minutes);
  PruneExpiredFromCacheDataset (cached_down_path_segments, now_minutes);
}

void
ScionHost::RequestForPathSegments (ia_t dst_ia)
{
  uint16_t dst_isd = GET_ISDN (dst_ia);

  // Local path servers may index UP segments by remote/core originators, so request all.
  SendRequestForPathSegments (PathSegmentType::UP_SEG, 0, 0);
  if (dst_isd == isd_number)
    {
      SendRequestForPathSegments (PathSegmentType::CORE_SEG, 0, 0);
      SendRequestForPathSegments (PathSegmentType::DOWN_SEG, 0, dst_ia);
    }
  else
    {
      SendRequestForPathSegments (PathSegmentType::CORE_SEG, 0, MAKE_IA (dst_isd, 0));
      SendRequestForPathSegments (PathSegmentType::DOWN_SEG, MAKE_IA (dst_isd, 0), dst_ia);
    }
}

void
ScionHost::RequestPathsToDestination (ia_t dst_ia)
{
  if (dst_ia == ia_addr)
    {
      return;
    }

  RequestForPathSegments (dst_ia);
}

void
ScionHost::ConfigureFirstHopPreference (uint16_t preferred_neighbor_real_as,
                                        uint16_t backup_neighbor_real_as)
{
  m_preferred_first_hop_real_as = preferred_neighbor_real_as;
  m_backup_first_hop_real_as = backup_neighbor_real_as;
}

void
ScionHost::InvalidateCachedPathsToDestination (ia_t dst_ia)
{
  EraseDstFromCacheDataset (cached_up_path_segments, dst_ia);
  EraseDstFromCacheDataset (cached_core_path_segments, dst_ia);
  EraseDstFromCacheDataset (cached_down_path_segments, dst_ia);
}

void
ScionHost::InvalidateAllCachedPathSegments ()
{
  ClearCacheDataset (cached_up_path_segments);
  ClearCacheDataset (cached_core_path_segments);
  ClearCacheDataset (cached_down_path_segments);
}

void
ScionHost::ClearProbeSessionBlacklists ()
{
  for (auto &session : probe_sessions)
    {
      session.timed_out_segments.clear ();
      session.last_sent_segment = nullptr;
    }
}

void
ScionHost::EvictCachedSegmentsUsingLink (uint16_t as_alias, uint16_t scion_if)
{
  auto prune_dataset = [&] (cached_path_segs_dataset_t &dataset) {
    for (auto &[dst_ia, per_dst] : dataset)
      {
        for (auto &[src_ia, per_src_dst] : *per_dst)
          {
            for (auto it = per_src_dst->begin (); it != per_src_dst->end ();)
              {
                const PathSegment *seg = it->second;
                bool uses_link = false;
                for (auto const &hop : seg->hops)
                  {
                    if (GET_HOP_AS (hop) == as_alias &&
                        (GET_HOP_ING_IF (hop) == scion_if || GET_HOP_EG_IF (hop) == scion_if))
                      {
                        uses_link = true;
                        break;
                      }
                  }
                if (uses_link)
                  it = per_src_dst->erase (it);
                else
                  ++it;
              }
          }
      }
  };
  prune_dataset (cached_up_path_segments);
  prune_dataset (cached_core_path_segments);
  prune_dataset (cached_down_path_segments);
}

void
ScionHost::CachePathSegment (PathSegmentType seg_type, ia_t src_ia, ia_t dst_ia,
                             PathSegment *path_seg)
{
  cached_path_segs_dataset_t *cached_path_segs_data_set;

  if (seg_type == PathSegmentType::CORE_SEG)
    {
      cached_path_segs_data_set = &cached_core_path_segments;
    }
  else if (seg_type == PathSegmentType::UP_SEG)
    {
      cached_path_segs_data_set = &cached_up_path_segments;
    }
  else
    {
      cached_path_segs_data_set = &cached_down_path_segments;
    }

  if (cached_path_segs_data_set->find (dst_ia) == cached_path_segs_data_set->end ())
    {
      cached_path_segs_data_set->insert (
          std::make_pair (dst_ia, new cached_path_segs_per_dst_t ()));
    }

  if (cached_path_segs_data_set->at (dst_ia)->find (src_ia) ==
      cached_path_segs_data_set->at (dst_ia)->end ())
    {
      cached_path_segs_data_set->at (dst_ia)->insert (
          std::make_pair (src_ia, new cached_path_segs_per_src_dst_t ()));
    }

  cached_path_segs_data_set->at (dst_ia)->at (src_ia)->insert (
      std::make_pair (path_seg->hops.size (), path_seg));
}

bool
ScionHost::MatchesFirstHopPreference (const PathSegment *segment, uint16_t real_as) const
{
  if (segment == NULL || segment->hops.empty () || real_as == 0)
    {
      return false;
    }

  link_information hop = segment->reverse ? *segment->hops.rbegin () : *segment->hops.begin ();

  const uint16_t left_alias = SECOND_LOWER_16_BITS (hop);
  const uint16_t right_alias = UPPER_16_BITS (hop);

  uint16_t remote_alias = 0;
  if (left_alias == as_number)
    {
      remote_alias = right_alias;
    }
  else if (right_alias == as_number)
    {
      remote_alias = left_alias;
    }
  else
    {
      return false;
    }

  uint16_t remote_real = remote_alias;
  if (alias_to_real_as_no.find (remote_alias) != alias_to_real_as_no.end ())
    {
      remote_real = static_cast<uint16_t> (alias_to_real_as_no.at (remote_alias));
    }

  return remote_real == real_as;
}

const PathSegment *
ScionHost::SelectPreferredSegment (const cached_path_segs_per_src_dst_t *segments) const
{
  if (segments == NULL || segments->empty ())
    {
      return NULL;
    }

  const PathSegment *fallback = segments->begin ()->second;
  const PathSegment *backup_match = NULL;

  for (auto const &[len, seg] : *segments)
    {
      (void) len;
      if (m_preferred_first_hop_real_as != 0 &&
          MatchesFirstHopPreference (seg, m_preferred_first_hop_real_as))
        {
          return seg;
        }

      if (m_backup_first_hop_real_as != 0 && backup_match == NULL &&
          MatchesFirstHopPreference (seg, m_backup_first_hop_real_as))
        {
          backup_match = seg;
        }
    }

  if (backup_match != NULL)
    {
      return backup_match;
    }

  return fallback;
}

void
ScionHost::SendRequestForPathSegments (PathSegmentType seg_type, ia_t src_ia, ia_t dst_ia)
{
  PayloadType payload_type = PayloadType::PATH_REQ_FROM_HOST;

  Payload payload;
  payload.path_req_from_host.src_ia = src_ia;
  payload.path_req_from_host.dst_ia = dst_ia;
  payload.path_req_from_host.seg_type = seg_type;

  ScionPacket *packet = CreateScionPacket (payload, payload_type, ia_addr, 1, 0);
  SendScionPacket (packet);
}

void
ScionHost::SearchInCachedSegments (ia_t dst_ia, std::vector<const PathSegment *> &path,
                                   std::vector<uint8_t> &shortcuts,
                                   const std::set<const PathSegment *> *skip_segments,
                                   const PathSegment **selected_segment_out)
{
  RemoveExpiredSegments ();

  if (dst_ia == ia_addr)
    {
      return;
    }

  if (IsDebugAs107 (as_number) && IsDebugDst108 (dst_ia))
    {
      std::cerr << "[AS107-CACHE-SEARCH] t=" << Simulator::Now ().GetSeconds ()
                << " dstIA=" << GET_ASN (dst_ia) << " upKeys=" << cached_up_path_segments.size ()
                << " coreKeys=" << cached_core_path_segments.size ()
                << " downKeys=" << cached_down_path_segments.size () << std::endl;
    }

  int16_t dst_in_which_cache = -1;

  if (cached_core_path_segments.find (dst_ia) != cached_core_path_segments.end ())
    {
      dst_in_which_cache = 0;
    }
  else if (cached_up_path_segments.find (dst_ia) != cached_up_path_segments.end ())
    {
      dst_in_which_cache = 1;
    }
  else if (cached_down_path_segments.find (dst_ia) != cached_down_path_segments.end ())
    {
      dst_in_which_cache = 2;
    }

  if (dst_in_which_cache == -1)
    {
      if (IsDebugAs107 (as_number) && IsDebugDst108 (dst_ia))
        {
          std::cerr << "[AS107-CACHE-SEARCH] miss-all-caches dstIA=" << GET_ASN (dst_ia)
                    << " downCacheEmpty=" << (cached_down_path_segments.empty () ? "1" : "0")
                    << " attempting beacon+fallback" << std::endl;
        }

      if (dynamic_cast<ScionCoreAs *> (as) == NULL &&
          TrySynthesizePathFromBeaconStore (dst_ia, path))
        {
          if (IsDebugAs107 (as_number) && IsDebugDst108 (dst_ia))
            {
              std::cerr << "[AS107-CACHE-SEARCH] synthesized-from-beacon-store pathSegments="
                        << path.size () << std::endl;
            }
          return;
        }

      // Some local path-server responses may cache usable DOWN segments under a
      // non-destination key. For non-core senders, attempt composition via any
      // available DOWN segment before giving up.
      if (dynamic_cast<ScionCoreAs *> (as) == NULL && !cached_down_path_segments.empty ())
        {
          if (IsDebugAs107 (as_number) && IsDebugDst108 (dst_ia))
            {
              std::cerr << "[AS107-CACHE-SEARCH] fallback-composition checking "
                        << cached_down_path_segments.size () << " down_dst entries" << std::endl;
            }

          for (auto const &[cached_dst_ia, down_dataset] : cached_down_path_segments)
            {
              (void) cached_dst_ia;
              for (auto const &[down_seg_src_ia, down_path_segs] : *down_dataset)
                {
                  if (IsDebugAs107 (as_number) && IsDebugDst108 (dst_ia))
                    {
                      std::cerr << "[AS107-CACHE-SEARCH] fallback checking down_seg_src_ia="
                                << GET_ASN (down_seg_src_ia)
                                << " coreKeys=" << cached_core_path_segments.size () << std::endl;
                    }

                  // Try to find CORE segments indexed by down_seg_src_ia (specific core AS)
                  cached_path_segs_dataset_t::iterator core_lookup =
                      cached_core_path_segments.find (down_seg_src_ia);

                  // If not found, fall back to checking dst_ia=0 for "global" CORE segments
                  if (core_lookup == cached_core_path_segments.end () && down_seg_src_ia != 0)
                    {
                      if (IsDebugAs107 (as_number) && IsDebugDst108 (dst_ia))
                        {
                          std::cerr << "[AS107-CACHE-SEARCH] core lookup failed for "
                                    << GET_ASN (down_seg_src_ia) << ", checking dst_ia=0"
                                    << std::endl;
                        }
                      core_lookup = cached_core_path_segments.find (0);
                    }

                  if (core_lookup != cached_core_path_segments.end ())
                    {
                      if (IsDebugAs107 (as_number) && IsDebugDst108 (dst_ia))
                        {
                          std::cerr
                              << "[AS107-CACHE-SEARCH] found CORE segments, checking composition..."
                              << std::endl;
                        }

                      for (auto const &[core_seg_src_ia, core_path_segs] : *core_lookup->second)
                        {
                          if (cached_up_path_segments.find (core_seg_src_ia) ==
                              cached_up_path_segments.end ())
                            {
                              if (IsDebugAs107 (as_number) && IsDebugDst108 (dst_ia))
                                {
                                  std::cerr
                                      << "[AS107-CACHE-SEARCH] no UP segment for core_seg_src_ia="
                                      << GET_ASN (core_seg_src_ia) << std::endl;
                                }
                              continue;
                            }

                          path.push_back (cached_up_path_segments.at (core_seg_src_ia)
                                              ->begin ()
                                              ->second->begin ()
                                              ->second);
                          path.push_back (core_path_segs->begin ()->second);
                          path.push_back (down_path_segs->begin ()->second);
                          if (IsDebugAs107 (as_number) && IsDebugDst108 (dst_ia))
                            {
                              std::cerr << "[AS107-CACHE-SEARCH] composed-via-fallback "
                                           "up+core+down pathSegments="
                                        << path.size () << std::endl;
                            }
                          return;
                        }
                    }
                }
            }

          if (IsDebugAs107 (as_number) && IsDebugDst108 (dst_ia))
            {
              std::cerr << "[AS107-CACHE-SEARCH] fallback-composition FAILED" << std::endl;
            }
        }

      return;
    }

  if (dst_in_which_cache == 0 && dynamic_cast<ScionCoreAs *> (as) != NULL &&
      cached_core_path_segments.at (dst_ia)->find (ia_addr) !=
          cached_core_path_segments.at (dst_ia)->end ())
    {
      const PathSegment *chosen = nullptr;
      for (auto const &[key, seg] : *cached_core_path_segments.at (dst_ia)->at (ia_addr))
        {
          (void) key;
          if (skip_segments != nullptr && skip_segments->count (seg) > 0)
            {
              continue;
            }
          chosen = seg;
          break;
        }
      if (chosen != nullptr)
        {
          path.push_back (chosen);
          if (selected_segment_out != nullptr)
            {
              *selected_segment_out = chosen;
            }
        }
      return;
    }

  if (dst_in_which_cache == 0 && dynamic_cast<ScionCoreAs *> (as) == NULL)
    {
      for (auto const &[core_seg_src_ia, core_path_segs] : *cached_core_path_segments.at (dst_ia))
        {
          if (cached_up_path_segments.find (core_seg_src_ia) != cached_up_path_segments.end ())
            {
              const PathSegment *up =
                  SelectPreferredSegment (cached_up_path_segments.at (core_seg_src_ia)
                                              ->begin ()->second);
              if (up == NULL)
                {
                  continue;
                }

              path.push_back (up);
              path.push_back (core_path_segs->begin ()->second);

              return;
            }
        }
      return;
    }

  if (dst_in_which_cache == 1 && dynamic_cast<ScionCoreAs *> (as) == NULL)
    {
      const PathSegment *up =
          SelectPreferredSegment (cached_up_path_segments.at (dst_ia)->begin ()->second);
      if (up != NULL)
        {
          path.push_back (up);
        }
      return;
    }

  if (dst_in_which_cache == 2 && dynamic_cast<ScionCoreAs *> (as) != NULL)
    {
      if (cached_down_path_segments.at (dst_ia)->find (ia_addr) !=
          cached_down_path_segments.at (dst_ia)->end ())
        {
          path.push_back (
              cached_down_path_segments.find (dst_ia)->second->begin ()->second->begin ()->second);
          return;
        }

      for (auto const &[down_seg_src_ia, down_path_segs] : *cached_down_path_segments.at (dst_ia))
        {
          if (cached_core_path_segments.find (down_seg_src_ia) != cached_core_path_segments.end ())
            {
              path.push_back (cached_core_path_segments.at (down_seg_src_ia)
                                  ->begin ()
                                  ->second->begin ()
                                  ->second);
              path.push_back (down_path_segs->begin ()->second);

              return;
            }
        }
      return;
    }

  if (dst_in_which_cache == 2 && dynamic_cast<ScionCoreAs *> (as) == NULL)
    {
      if (IsDebugAs107 (as_number) && IsDebugDst108 (dst_ia))
        {
          std::cerr << "[AS107-CACHE-SEARCH] dst_in_cache==2 (DOWN cache), dstIA="
                    << GET_ASN (dst_ia) << " coreKeys_IAs: ";
          for (auto const &[k, v] : cached_core_path_segments)
            {
              std::cerr << "(" << GET_ISDN (k) << ":" << GET_ASN (k) << ")=" << k << " ";
            }
          std::cerr << std::endl;
        }

      for (auto const &[down_seg_src_ia, down_path_segs] : *cached_down_path_segments.at (dst_ia))
        {
          if (skip_segments != nullptr && !down_path_segs->empty ()
              && skip_segments->count (down_path_segs->begin ()->second) > 0)
            {
              continue;
            }

          if (IsDebugAs107 (as_number) && IsDebugDst108 (dst_ia))
            {
              std::cerr << "[AS107-CACHE-SEARCH] down_seg_src_ia=" << GET_ASN (down_seg_src_ia)
                        << " coreKeys_size=" << cached_core_path_segments.size ()
                        << " looking for CORE..." << std::endl;
            }

          // Try to find CORE segments indexed by down_seg_src_ia (specific core AS)
          cached_path_segs_dataset_t::iterator core_lookup =
              cached_core_path_segments.find (down_seg_src_ia);

          bool found_core_at_key = (core_lookup != cached_core_path_segments.end ());

          // If not found, fall back to checking "global" CORE segments indexed by MAKE_IA(isd_number, 0)
          if (!found_core_at_key && down_seg_src_ia != MAKE_IA (isd_number, 0))
            {
              if (IsDebugAs107 (as_number) && IsDebugDst108 (dst_ia))
                {
                  std::cerr << "[AS107-CACHE-SEARCH] core not found at "
                            << GET_ASN (down_seg_src_ia) << ", checking global CORE key MAKE_IA("
                            << (int) isd_number << ",0)..." << std::endl;
                }
              core_lookup = cached_core_path_segments.find (MAKE_IA (isd_number, 0));

              if (IsDebugAs107 (as_number) && IsDebugDst108 (dst_ia))
                {
                  bool found = (core_lookup != cached_core_path_segments.end ());
                  std::cerr << "[AS107-CACHE-SEARCH] find(MAKE_IA(" << (int) isd_number
                            << ",0)) returned: " << (found ? "SUCCESS" : "FAIL") << std::endl;
                }
            }

          if (core_lookup != cached_core_path_segments.end ())
            {
              if (IsDebugAs107 (as_number) && IsDebugDst108 (dst_ia))
                {
                  std::cerr << "[AS107-CACHE-SEARCH] FOUND CORE segments at key "
                            << GET_ASN (core_lookup->first) << ", iterating..."
                            << " core_entries=" << core_lookup->second->size () << std::endl;
                }

              for (auto const &[core_seg_src_ia, core_path_segs] : *core_lookup->second)
                {
                  if (IsDebugAs107 (as_number) && IsDebugDst108 (dst_ia))
                    {
                      std::cerr << "[AS107-CACHE-SEARCH] core_seg_src_ia="
                                << GET_ASN (core_seg_src_ia)
                                << " upKeys_size=" << cached_up_path_segments.size ()
                                << " looking for UP..." << std::endl;
                    }

                  if (cached_up_path_segments.find (core_seg_src_ia) !=
                      cached_up_path_segments.end ())
                    {
                      if (IsDebugAs107 (as_number) && IsDebugDst108 (dst_ia))
                        {
                          std::cerr << "[AS107-CACHE-SEARCH] SUCCESS: composing UP+CORE+DOWN"
                                    << std::endl;
                        }

                      const PathSegment *up =
                          SelectPreferredSegment (cached_up_path_segments.at (core_seg_src_ia)
                                                      ->begin ()->second);
                      if (up == NULL)
                        {
                          continue;
                        }

                      path.push_back (up);
                      path.push_back (core_path_segs->begin ()->second);
                      path.push_back (down_path_segs->begin ()->second);

                      if (selected_segment_out != nullptr)
                        {
                          *selected_segment_out = down_path_segs->begin ()->second;
                        }

                      return;
                    }
                  else
                    {
                      if (IsDebugAs107 (as_number) && IsDebugDst108 (dst_ia))
                        {
                          std::cerr << "[AS107-CACHE-SEARCH] UP not found for "
                                    << GET_ASN (core_seg_src_ia) << std::endl;
                        }
                    }
                }
            }
          else
            {
              if (IsDebugAs107 (as_number) && IsDebugDst108 (dst_ia))
                {
                  std::cerr << "[AS107-CACHE-SEARCH] CORE NOT FOUND for "
                            << GET_ASN (down_seg_src_ia) << " (tried key and fallback to 0)"
                            << std::endl;
                }
            }
        }

      if (IsDebugAs107 (as_number) && IsDebugDst108 (dst_ia))
        {
          std::cerr << "[AS107-CACHE-SEARCH] DOWN cache exhausted, path composition FAILED"
                    << std::endl;
        }
    }

  return;
}

bool
ScionHost::TrySynthesizePathFromBeaconStore (ia_t dst_ia, std::vector<const PathSegment *> &path)
{
  if (as == NULL || as->GetBeaconServer () == NULL)
    {
      return false;
    }

  uint16_t dst_as = GET_ASN (dst_ia);
  auto const &beacon_store = as->GetBeaconServer ()->GetBeaconStore ();
  auto it = beacon_store.find (dst_as);
  if (it == beacon_store.end ())
    {
      return false;
    }

  const Beacon *selected_beacon = NULL;
  for (auto const &[len, beacons] : it->second)
    {
      (void) len;
      for (auto const &the_beacon : beacons)
        {
          if (the_beacon != NULL && the_beacon->is_valid)
            {
              selected_beacon = the_beacon;
              break;
            }
        }

      if (selected_beacon != NULL)
        {
          break;
        }
    }

  if (selected_beacon == NULL)
    {
      if (IsDebugAs107 (as_number) && IsDebugDst108 (dst_ia))
        {
          std::cerr << "[AS107-SYNTH] no-valid-beacon-for-dst dstIA=" << GET_ASN (dst_ia)
                    << std::endl;
        }
      return false;
    }

  PathSegment synthesized_segment;
  selected_beacon->ExtractPathSegmentFromPushBasedBeacon (synthesized_segment);
  synthesized_segment.reverse = true;

  synthesized_path_segments.push_back (
      std::unique_ptr<PathSegment> (new PathSegment (synthesized_segment)));
  path.push_back (synthesized_path_segments.back ().get ());
  if (IsDebugAs107 (as_number) && IsDebugDst108 (dst_ia))
    {
      std::cerr << "[AS107-SYNTH] built-direct-segment hops=" << synthesized_segment.hops.size ()
                << std::endl;
    }
  return true;
}

bool
ScionHost::TryGetCachedPath (ia_t dst_ia, std::vector<const PathSegment *> &path,
                             std::vector<uint8_t> &shortcuts)
{
  path.clear ();
  shortcuts.clear ();
  SearchInCachedSegments (dst_ia, path, shortcuts);
  return !path.empty ();
}

uint32_t
ScionHost::ConfigureDataPlaneProbe (ia_t dst_ia, host_addr_t dst_host, uint16_t src_as_real,
                                    uint16_t dst_as_real, uint32_t count, Time interval,
                                    Time timeout, Time start_time, const std::string &output_path)
{
  ProbeSession session;
  session.dst_ia = dst_ia;
  session.dst_host = dst_host;
  session.src_as_real = src_as_real;
  session.dst_as_real = dst_as_real;
  session.count = count;
  session.interval = interval;
  session.timeout = timeout;
  session.output_path = output_path;

  probe_sessions.push_back (std::move (session));
  uint32_t session_id = static_cast<uint32_t> (probe_sessions.size () - 1);

  Simulator::Schedule (start_time, &ScionHost::StartProbeSession, this, session_id);
  return session_id;
}

void
ScionHost::StartProbeSession (uint32_t session_id)
{
  if (session_id >= probe_sessions.size ())
    {
      return;
    }

  ProbeSession &session = probe_sessions.at (session_id);
  if (session.started)
    {
      return;
    }

  session.out.open (session.output_path.c_str (), std::ios::out | std::ios::trunc);
  if (!session.out.is_open ())
    {
      NS_LOG_WARN ("Unable to open probe output file: " << session.output_path);
      return;
    }

  session.out << "time_s,src_as,dst_as,seq,event,rtt_ms,path_used" << std::endl;
  session.started = true;
  SendProbeTick (session_id);
}

void
ScionHost::SendProbeTick (uint32_t session_id)
{
  if (session_id >= probe_sessions.size ())
    {
      return;
    }

  ProbeSession &session = probe_sessions.at (session_id);
  if (!session.started)
    {
      return;
    }

  if (session.next_seq >= session.count)
    {
      if (session.out.is_open ())
        {
          session.out.close ();
        }
      return;
    }

  const uint32_t seq = session.next_seq;
  session.next_seq++;
  TrySendProbe (session_id, seq, 0);

  if (session.next_seq < session.count)
    {
      session.send_event =
          Simulator::Schedule (session.interval, &ScionHost::SendProbeTick, this, session_id);
    }
}

void
ScionHost::TrySendProbe (uint32_t session_id, uint32_t seq, uint32_t retry_count)
{
  if (session_id >= probe_sessions.size ())
    {
      return;
    }

  ProbeSession &session = probe_sessions.at (session_id);
  if (!session.started)
    {
      return;
    }

  if (session.force_path_refresh && retry_count == 0)
    {
      // Force-refresh invalidates all cache datasets to avoid stale intermediates
      // surviving under non-destination keys.
      InvalidateAllCachedPathSegments ();
      RequestPathsToDestination (session.dst_ia);
      Simulator::Schedule (MilliSeconds (100), &ScionHost::TrySendProbe, this, session_id, seq,
                           retry_count + 1);
      return;
    }

  if (retry_count == 0)
    {
      session.last_sent_segment = nullptr;
    }

  std::vector<const PathSegment *> the_path;
  std::vector<uint8_t> shortcuts;
  const PathSegment *selected_segment = nullptr;
  SearchInCachedSegments (session.dst_ia, the_path, shortcuts, &session.timed_out_segments,
                          &selected_segment);

  // If all known paths are blocked by the skip set, fall back to unrestricted search
  if (the_path.empty () && !session.timed_out_segments.empty ())
    {
      session.timed_out_segments.clear ();
      SearchInCachedSegments (session.dst_ia, the_path, shortcuts, nullptr, &selected_segment);
    }

  if (the_path.empty () && dynamic_cast<ScionCoreAs *> (as) == NULL)
    {
      TrySynthesizePathFromBeaconStore (session.dst_ia, the_path);
    }

  if (IsDebugAs107 (as_number) && session.dst_as_real == 108)
    {
      std::cerr << "[AS107-PROBE-SEND] t=" << Simulator::Now ().GetSeconds () << " seq=" << seq
                << " retry=" << retry_count << " pathSegments=" << the_path.size () << std::endl;
    }

  if (the_path.empty ())
    {
      if (IsDebugAs107 (as_number) && session.dst_as_real == 108)
        {
          std::cerr << "[AS107-PROBE-SEND] requesting-paths dstIA=" << GET_ASN (session.dst_ia)
                    << std::endl;
        }
      RequestPathsToDestination (session.dst_ia);
      if (retry_count >= 20)
        {
          WriteProbeRow (session, seq, "timeout", "", "", Simulator::Now ());
          return;
        }

      Simulator::Schedule (MilliSeconds (100), &ScionHost::TrySendProbe, this, session_id, seq,
                           retry_count + 1);
      return;
    }

  Payload payload;
  payload.scion_probe_payload.seq = seq;
  payload.scion_probe_payload.send_time_ns = Simulator::Now ().GetNanoSeconds ();
  ScionPacket *packet =
      CreateScionPacket (payload, PayloadType::SCION_PROBE_REQ, session.dst_ia, session.dst_host,
                         sizeof (ScionProbePayload), the_path, shortcuts);

  const std::string path_used = FormatPacketPathForCsv (packet);
  SendScionPacket (packet);

  session.last_sent_segment = selected_segment;
  session.send_times.insert (std::make_pair (seq, Simulator::Now ()));

  EventId timeout_event =
      Simulator::Schedule (session.timeout, &ScionHost::OnProbeTimeout, this, session_id, seq);
  session.timeout_events.insert (std::make_pair (seq, timeout_event));

  WriteProbeRow (session, seq, "sent", "", path_used, Simulator::Now ());
}

void
ScionHost::OnProbeTimeout (uint32_t session_id, uint32_t seq)
{
  if (session_id >= probe_sessions.size ())
    {
      return;
    }

  ProbeSession &session = probe_sessions.at (session_id);
  auto send_it = session.send_times.find (seq);
  if (send_it == session.send_times.end ())
    {
      return;
    }

  session.send_times.erase (send_it);
  session.timeout_events.erase (seq);
  session.consecutive_timeouts++;
  session.force_path_refresh = true;

  if (session.last_sent_segment != nullptr)
    {
      session.timed_out_segments.insert (session.last_sent_segment);
    }

  WriteProbeRow (session, seq, "timeout", "", "", Simulator::Now ());
}

void
ScionHost::HandleProbeReply (ScionPacket *packet, Time receive_time)
{
  const uint32_t seq = packet->payload.scion_probe_payload.seq;
  for (auto &session : probe_sessions)
    {
      if (session.dst_ia != packet->src_ia)
        {
          continue;
        }

      auto send_it = session.send_times.find (seq);
      if (send_it == session.send_times.end ())
        {
          continue;
        }

      auto timeout_it = session.timeout_events.find (seq);
      if (timeout_it != session.timeout_events.end ())
        {
          timeout_it->second.Cancel ();
          session.timeout_events.erase (timeout_it);
        }

      Time rtt = receive_time - send_it->second;
      std::ostringstream rtt_ss;
      rtt_ss << std::fixed << std::setprecision (3) << rtt.GetSeconds () * 1000.0;
      WriteProbeRow (session, seq, "reply", rtt_ss.str (), FormatPacketPathForCsv (packet),
                     receive_time);

      session.consecutive_timeouts = 0;
      session.force_path_refresh = false;
      session.timed_out_segments.clear ();
      session.last_sent_segment = nullptr;
      session.send_times.erase (send_it);
      return;
    }

  return;
}

void
ScionHost::WriteProbeRow (ProbeSession &session, uint32_t seq, const std::string &event,
                          const std::string &rtt_ms, const std::string &path_used, Time event_time)
{
  if (!session.out.is_open ())
    {
      return;
    }

  session.out << std::fixed << std::setprecision (6) << event_time.GetSeconds () << ","
              << session.src_as_real << "," << session.dst_as_real << "," << seq << "," << event
              << "," << rtt_ms << "," << path_used << std::endl;
}

std::string
ScionHost::FormatPacketPathForCsv (const ScionPacket *packet) const
{
  std::ostringstream oss;
  bool first_hop = true;

  for (auto const *path_segment : packet->path)
    {
      if (path_segment->reverse)
        {
          for (auto hop_it = path_segment->hops.rbegin (); hop_it != path_segment->hops.rend ();
               ++hop_it)
            {
              if (!first_hop)
                {
                  oss << ";";
                }

              const uint16_t left_alias = SECOND_LOWER_16_BITS (*hop_it);
              const uint16_t right_alias = UPPER_16_BITS (*hop_it);
              const uint16_t left_if = LOWER_16_BITS (*hop_it);
              const uint16_t right_if = SECOND_UPPER_16_BITS (*hop_it);

              const int32_t left_real = alias_to_real_as_no.count (left_alias)
                                            ? alias_to_real_as_no.at (left_alias)
                                            : left_alias;
              const int32_t right_real = alias_to_real_as_no.count (right_alias)
                                             ? alias_to_real_as_no.at (right_alias)
                                             : right_alias;
              oss << left_real << ":" << left_if << "->" << right_real << ":" << right_if;
              first_hop = false;
            }
        }
      else
        {
          for (auto hop_it = path_segment->hops.begin (); hop_it != path_segment->hops.end ();
               ++hop_it)
            {
              if (!first_hop)
                {
                  oss << ";";
                }

              const uint16_t left_alias = SECOND_LOWER_16_BITS (*hop_it);
              const uint16_t right_alias = UPPER_16_BITS (*hop_it);
              const uint16_t left_if = LOWER_16_BITS (*hop_it);
              const uint16_t right_if = SECOND_UPPER_16_BITS (*hop_it);

              const int32_t left_real = alias_to_real_as_no.count (left_alias)
                                            ? alias_to_real_as_no.at (left_alias)
                                            : left_alias;
              const int32_t right_real = alias_to_real_as_no.count (right_alias)
                                             ? alias_to_real_as_no.at (right_alias)
                                             : right_alias;
              oss << left_real << ":" << left_if << "->" << right_real << ":" << right_if;
              first_hop = false;
            }
        }
    }

  return oss.str ();
}

void
ScionHost::ProcessReceivedPacket (uint16_t local_if, ScionPacket *packet, Time receive_time)
{
  NS_ASSERT (packet->dst_ia == ia_addr && packet->dst_host == local_address);
  NS_LOG_FUNCTION ("I am host " << isd_number << ":" << as_number << ":" << local_address
                                << ". Packet received from " << GET_ISDN (packet->src_ia) << ":"
                                << GET_ASN (packet->src_ia) << ":" << packet->src_host);

  ScionCapableNode::ProcessReceivedPacket (local_if, packet, receive_time);

  if (packet->payload_type == PayloadType::REG_PATHS_FROM_LOCAL_PS)
    {
      RegPathsFromLocalPs registered_paths_from_local_ps =
          packet->payload.registered_paths_from_local_ps;
      ReceiveRegisteredPathSegments (registered_paths_from_local_ps.seg_type,
                                     registered_paths_from_local_ps.src_ia,
                                     registered_paths_from_local_ps.dst_ia,
                                     registered_paths_from_local_ps.registered_path_segments);
      packet->packet_originator->DestroyScionPacket (packet);
      return;
    }

  if (packet->payload_type == PayloadType::SCION_PROBE_REQ)
    {
      Payload reply_payload;
      reply_payload.scion_probe_payload = packet->payload.scion_probe_payload;

      std::vector<const PathSegment *> the_path;
      std::vector<uint8_t> shortcuts;
      SearchInCachedSegments (packet->src_ia, the_path, shortcuts);
      if (the_path.empty () && dynamic_cast<ScionCoreAs *> (as) == NULL)
        {
          TrySynthesizePathFromBeaconStore (packet->src_ia, the_path);
        }
      if (the_path.empty ())
        {
          RequestPathsToDestination (packet->src_ia);
          packet->packet_originator->DestroyScionPacket (packet);
          return;
        }

      ScionPacket *reply =
          CreateScionPacket (reply_payload, PayloadType::SCION_PROBE_REPLY, packet->src_ia,
                             packet->src_host, sizeof (ScionProbePayload), the_path, shortcuts);
      SendScionPacket (reply);
      packet->packet_originator->DestroyScionPacket (packet);
      return;
    }

  if (packet->payload_type == PayloadType::SCION_PROBE_REPLY)
    {
      HandleProbeReply (packet, receive_time);
      packet->packet_originator->DestroyScionPacket (packet);
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
ScionHost::SendArbitraryPacket (ia_t dst_ia, host_addr_t dst_host)
{
  Payload payload;
  PayloadType payload_type = PayloadType::EMPTY;

  if (dst_ia == ia_addr)
    {
      ScionPacket *packet = CreateScionPacket (payload, payload_type, dst_ia, dst_host, 0);
      SendScionPacket (packet);
    }
  else
    {
      std::vector<const PathSegment *> the_path;
      std::vector<uint8_t> shortcuts;

      SearchInCachedSegments (dst_ia, the_path, shortcuts);

      if (the_path.size () != 0)
        {
          ScionPacket *packet =
              CreateScionPacket (payload, payload_type, dst_ia, dst_host, 0, the_path, shortcuts);
          SendScionPacket (packet);
        }
      else
        {
          RequestForPathSegments (dst_ia);
          Simulator::Schedule (MilliSeconds (300), &ScionHost::SendArbitraryPacket, this, dst_ia,
                               dst_host);
        }
    }
}

void
ScionHost::ModifyPktUponSend (ScionPacket *packet)
{
}
} // namespace ns3
