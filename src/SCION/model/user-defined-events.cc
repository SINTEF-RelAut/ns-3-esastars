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

#include "user-defined-events.h"
#include "time-server.h"

#include <algorithm>

#include "utils.h"

namespace ns3 {

void
UserDefinedEvents::SnapshotInitialLinkState ()
{
  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      ScionAs *scion_as = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
      if (scion_as == NULL)
        {
          continue;
        }

      original_interface_to_neighbor_map[scion_as->as_number] = scion_as->interface_to_neighbor_map;
      original_interfaces_per_neighbor_as[scion_as->as_number] =
          scion_as->interfaces_per_neighbor_as;
      std::vector<std::pair<uint16_t, int32_t>> neighbors_snapshot;
      for (auto const &neighbor_rel : scion_as->neighbors)
        {
          neighbors_snapshot.push_back (
              std::make_pair (neighbor_rel.first, (int32_t) neighbor_rel.second));
        }
      original_neighbors[scion_as->as_number] = neighbors_snapshot;
    }
}

void
UserDefinedEvents::BuildConfiguredIfIdMap ()
{
  if (!config["topology"])
    {
      return;
    }

  const std::string topology_file = config["topology"].as<std::string> ();
  std::ifstream input (topology_file);
  if (!input.is_open ())
    {
      std::cerr << "Warning: could not open topology file for IFID mapping: " << topology_file
                << std::endl;
      return;
    }

  std::string xml_text ((std::istreambuf_iterator<char> (input)),
                        std::istreambuf_iterator<char> ());
  input.close ();
  if (xml_text.empty ())
    {
      return;
    }

  std::vector<char> xml_buffer (xml_text.begin (), xml_text.end ());
  xml_buffer.push_back ('\0');

  rapidxml::xml_document<> doc;
  doc.parse<0> (&xml_buffer[0]);

  rapidxml::xml_node<> *root = doc.first_node ("topology");
  if (root == NULL)
    {
      return;
    }

  std::unordered_map<int32_t, uint16_t> next_scion_if;
  rapidxml::xml_node<> *link_node = root->first_node ("link");
  while (link_node)
    {
      int32_t from = std::stoi (link_node->first_node ("from")->value ());
      int32_t to = std::stoi (link_node->first_node ("to")->value ());
      PropertyContainer p = ParseProperties (link_node);

      next_scion_if[from]++;
      if (p.HasProperty ("from_if_id"))
        {
          uint32_t configured_if = (uint32_t) std::stoul (p.GetProperty ("from_if_id"));
          configured_if_to_scion_if[from][configured_if] = next_scion_if[from];
        }

      next_scion_if[to]++;
      if (p.HasProperty ("to_if_id"))
        {
          uint32_t configured_if = (uint32_t) std::stoul (p.GetProperty ("to_if_id"));
          configured_if_to_scion_if[to][configured_if] = next_scion_if[to];
        }

      link_node = link_node->next_sibling ("link");
    }
}

ScionAs *
UserDefinedEvents::GetAsByRealAsNo (int32_t real_as_no) const
{
  auto it = real_to_alias_as_no.find (real_as_no);
  if (it == real_to_alias_as_no.end ())
    {
      return NULL;
    }

  return dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (it->second)));
}

uint16_t
UserDefinedEvents::ResolveScionIfId (ScionAs *scion_as, int32_t real_as_no,
                                     uint32_t event_if_id) const
{
  if (scion_as == NULL)
    {
      return 0;
    }

  if (event_if_id >= 1 && event_if_id <= scion_as->GetNDevices ())
    {
      return (uint16_t) event_if_id;
    }

  auto as_it = configured_if_to_scion_if.find (real_as_no);
  if (as_it != configured_if_to_scion_if.end ())
    {
      auto if_it = as_it->second.find (event_if_id);
      if (if_it != as_it->second.end ())
        {
          return if_it->second;
        }
    }

  // Fallback for patterns like 1020001 where the suffix is a zero-based local IF index.
  if (event_if_id / 10000 == (uint32_t) real_as_no)
    {
      uint32_t suffix = event_if_id % 10000;
      if (suffix + 1 >= 1 && suffix + 1 <= scion_as->GetNDevices ())
        {
          return (uint16_t) (suffix + 1);
        }
    }

  return 0;
}

bool
UserDefinedEvents::HasNeighbor (const std::vector<std::pair<uint16_t, int32_t>> &neighbors,
                                uint16_t neighbor_as)
{
  for (auto const &neighbor_rel : neighbors)
    {
      if (neighbor_rel.first == neighbor_as)
        {
          return true;
        }
    }
  return false;
}

uint64_t
UserDefinedEvents::MakeAsIfKey (uint16_t alias_as_no, uint16_t scion_if_id) const
{
  return (((uint64_t) alias_as_no) << 32) | ((uint64_t) scion_if_id);
}

void
UserDefinedEvents::InitializeVirtualIxpIfConfigured ()
{
  if (!config["virtual_ixp"])
    {
      return;
    }

  YAML::Node ixp_cfg = config["virtual_ixp"];
  if (!ixp_cfg["enabled"] || !ixp_cfg["enabled"].as<bool> ())
    {
      return;
    }

  virtual_ixp_fabric = CreateObject<VirtualIxpFabric> ();
  IxpConfig fabric_cfg;

  if (ixp_cfg["port_count"])
    {
      fabric_cfg.portCount = ixp_cfg["port_count"].as<uint32_t> ();
    }
  if (ixp_cfg["rebalance_period"])
    {
      fabric_cfg.rebalancePeriod = Time (ixp_cfg["rebalance_period"].as<std::string> ());
    }
  if (ixp_cfg["hold_down"])
    {
      fabric_cfg.holdDown = Time (ixp_cfg["hold_down"].as<std::string> ());
    }

  virtual_ixp_fabric->Configure (fabric_cfg);

  if (!ixp_cfg["links"])
    {
      std::cerr << "Warning: virtual_ixp enabled but no links configured." << std::endl;
      return;
    }

  for (std::size_t i = 0; i < ixp_cfg["links"].size (); ++i)
    {
      YAML::Node link = ixp_cfg["links"][i];
      if (!link["real_as"] || !link["if_id"])
        {
          continue;
        }

      int32_t real_as_no = link["real_as"].as<int32_t> ();
      uint32_t configured_if_id = link["if_id"].as<uint32_t> ();
      double quality = link["quality"] ? link["quality"].as<double> () : 1.0;

      ScionAs *scion_as = GetAsByRealAsNo (real_as_no);
      if (scion_as == NULL)
        {
          std::cerr << "Warning: virtual_ixp link ignored, AS not found: " << real_as_no
                    << std::endl;
          continue;
        }

      uint16_t scion_if = ResolveScionIfId (scion_as, real_as_no, configured_if_id);
      if (scion_if == 0)
        {
          std::cerr << "Warning: virtual_ixp link ignored, IF not found for AS " << real_as_no
                    << " configured_if_id=" << configured_if_id << std::endl;
          continue;
        }

      Ptr<ScionIxpEndpointAdapter> endpoint = CreateObject<ScionIxpEndpointAdapter> ();
      endpoint->Bind (scion_as->as_number, as_nodes.Get (real_to_alias_as_no.at (real_as_no)),
                      scion_if);

      IxpLinkId link_id;
      link_id.asn = (uint32_t) real_as_no;
      link_id.linkIndex = virtual_ixp_next_link_index++;

      virtual_ixp_fabric->RegisterEndpoint (link_id, endpoint);

      uint64_t key = MakeAsIfKey (scion_as->as_number, scion_if);
      virtual_ixp_link_by_as_if[key] = link_id;
      virtual_ixp_quality_by_as_if[key] = quality;
    }

  if (virtual_ixp_link_by_as_if.empty ())
    {
      std::cerr << "Warning: virtual_ixp enabled but no valid links were registered." << std::endl;
      return;
    }

  virtual_ixp_enabled = true;
  virtual_ixp_fabric->Start ();

  for (std::unordered_map<uint64_t, IxpLinkId>::const_iterator it =
           virtual_ixp_link_by_as_if.begin ();
       it != virtual_ixp_link_by_as_if.end (); ++it)
    {
      double q = 1.0;
      std::unordered_map<uint64_t, double>::const_iterator qit =
          virtual_ixp_quality_by_as_if.find (it->first);
      if (qit != virtual_ixp_quality_by_as_if.end ())
        {
          q = qit->second;
        }
      virtual_ixp_fabric->SetFeasibleUp (it->second, q);
    }
}

bool
UserDefinedEvents::HandleVirtualIxpLinkEvent (uint16_t alias_as_no, uint16_t scion_if_id, bool up)
{
  if (!virtual_ixp_enabled || virtual_ixp_fabric == 0)
    {
      return false;
    }

  uint64_t key = MakeAsIfKey (alias_as_no, scion_if_id);
  std::unordered_map<uint64_t, IxpLinkId>::const_iterator it = virtual_ixp_link_by_as_if.find (key);
  if (it == virtual_ixp_link_by_as_if.end ())
    {
      return false;
    }

  if (up)
    {
      double q = 1.0;
      std::unordered_map<uint64_t, double>::const_iterator qit =
          virtual_ixp_quality_by_as_if.find (key);
      if (qit != virtual_ixp_quality_by_as_if.end ())
        {
          q = qit->second;
        }
      virtual_ixp_fabric->SetFeasibleUp (it->second, q);
      return true;
    }

  virtual_ixp_fabric->SetFeasibleDown (it->second);
  return true;
}

void
UserDefinedEvents::RunUserSpecifiedEvent (const std::string &func_name,
                                          std::vector<std::string> vec)
{
  std::unordered_map<std::string, AnyCallable>::const_iterator it =
      function_name_to_function.find (func_name);
  if (it == function_name_to_function.end ())
    {
      return;
    }

  it->second (vec);
}

void
UserDefinedEvents::ConstructFuncMap ()
{
  function_name_to_function["add_host"] = FunctionFactory (&UserDefinedEvents::AddAHost, this);
  function_name_to_function["link_down"] = FunctionFactory (&UserDefinedEvents::LinkDown, this);
  function_name_to_function["link_up"] = FunctionFactory (&UserDefinedEvents::LinkUp, this);
  function_name_to_function["send_packet"] =
      FunctionFactory (&UserDefinedEvents::SendAPacket, this);
  function_name_to_function["send_packet_batch"] =
      FunctionFactory (&UserDefinedEvents::SendPacketBatch, this);
  function_name_to_function["time_references_down"] =
      FunctionFactory (&UserDefinedEvents::TimeReferencesDown, this);
  function_name_to_function["time_references_up"] =
      FunctionFactory (&UserDefinedEvents::TimeReferencesUp, this);

  BuildConfiguredIfIdMap ();
  SnapshotInitialLinkState ();
  InitializeVirtualIxpIfConfigured ();
}

void
UserDefinedEvents::ReadAndScheduleUserDefinedEvents (const std::string &events_file_str)
{
  nlohmann::json events_json;
  std::ifstream events_file (events_file_str);
  events_file >> events_json;
  events_file.close ();

  for (auto const &event : events_json.at ("events"))
    {
      Time time = Time ((std::string) event["time"]);
      std::string func_name = (std::string) event["type"];
      std::vector<std::string> args_v;

      for (uint32_t i = 0; i < event["args"].size (); ++i)
        {
          args_v.push_back ((std::string) event["args"][i]);
        }

      Simulator::Schedule (time, &UserDefinedEvents::RunUserSpecifiedEvent, this, func_name,
                           args_v);
    }
}

void
UserDefinedEvents::AddAHost (std::string isd_number, std::string real_as_no,
                             std::string local_address)
{
}

void
UserDefinedEvents::LinkDown (std::string isd_number, std::string real_as_no, std::string if_id)
{
  uint16_t target_isd = (uint16_t) std::stoul (isd_number);
  int32_t target_real_as = (int32_t) std::stol (real_as_no);
  uint32_t requested_if = (uint32_t) std::stoul (if_id);

  ScionAs *scion_as = GetAsByRealAsNo (target_real_as);
  if (scion_as == NULL)
    {
      std::cerr << "Warning: link_down ignored, AS not found: " << real_as_no << std::endl;
      return;
    }

  if (scion_as->isd_number != target_isd)
    {
      std::cerr << "Warning: link_down ignored, ISD mismatch for AS " << real_as_no << std::endl;
      return;
    }

  uint16_t scion_if = ResolveScionIfId (scion_as, target_real_as, requested_if);
  if (scion_if == 0)
    {
      std::cerr << "Warning: link_down ignored, unknown interface id " << if_id << " on AS "
                << real_as_no << std::endl;
      return;
    }

  if (HandleVirtualIxpLinkEvent (scion_as->as_number, scion_if, false))
    {
      return;
    }

  auto if_to_neighbor_it = scion_as->interface_to_neighbor_map.find (scion_if);
  if (if_to_neighbor_it == scion_as->interface_to_neighbor_map.end ())
    {
      // Already down.
      return;
    }

  uint16_t remote_as = if_to_neighbor_it->second;
  scion_as->interface_to_neighbor_map.erase (if_to_neighbor_it);

  auto interfaces_it = scion_as->interfaces_per_neighbor_as.find (remote_as);
  if (interfaces_it != scion_as->interfaces_per_neighbor_as.end ())
    {
      auto &interfaces = interfaces_it->second;
      interfaces.erase (std::remove (interfaces.begin (), interfaces.end (), scion_if),
                        interfaces.end ());
      if (interfaces.empty ())
        {
          scion_as->interfaces_per_neighbor_as.erase (interfaces_it);

          scion_as->neighbors.erase (
              std::remove_if (scion_as->neighbors.begin (), scion_as->neighbors.end (),
                              [remote_as] (const std::pair<uint16_t, NeighbourRelation> &entry) {
                                return entry.first == remote_as;
                              }),
              scion_as->neighbors.end ());
        }
    }
  // Path revocation: selectively evict only the broken paths from host caches,
  // clear blacklists, then delete the revoked segments from each PS.
  // We iterate ALL ASes because any AS's PS or hosts may hold paths that
  // traverse the failed interface.
  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      ScionAs *target_as = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
      if (target_as == nullptr || target_as->path_server == nullptr)
        continue;
      for (ScionHost *host : target_as->hosts)
        {
          host->EvictCachedSegmentsUsingLink (scion_as->as_number, scion_if);
          host->ClearProbeSessionBlacklists ();
        }
      target_as->path_server->RevokeSegmentsContainingLink (scion_as->as_number, scion_if);
    }}
void
UserDefinedEvents::LinkUp (std::string isd_number, std::string real_as_no, std::string if_id)
{
  uint16_t target_isd = (uint16_t) std::stoul (isd_number);
  int32_t target_real_as = (int32_t) std::stol (real_as_no);
  uint32_t requested_if = (uint32_t) std::stoul (if_id);

  ScionAs *scion_as = GetAsByRealAsNo (target_real_as);
  if (scion_as == NULL)
    {
      std::cerr << "Warning: link_up ignored, AS not found: " << real_as_no << std::endl;
      return;
    }

  if (scion_as->isd_number != target_isd)
    {
      std::cerr << "Warning: link_up ignored, ISD mismatch for AS " << real_as_no << std::endl;
      return;
    }

  uint16_t scion_if = ResolveScionIfId (scion_as, target_real_as, requested_if);
  if (scion_if == 0)
    {
      std::cerr << "Warning: link_up ignored, unknown interface id " << if_id << " on AS "
                << real_as_no << std::endl;
      return;
    }

  if (HandleVirtualIxpLinkEvent (scion_as->as_number, scion_if, true))
    {
      return;
    }

  uint16_t alias_as = scion_as->as_number;
  auto original_if_map_it = original_interface_to_neighbor_map.find (alias_as);
  if (original_if_map_it == original_interface_to_neighbor_map.end () ||
      original_if_map_it->second.find (scion_if) == original_if_map_it->second.end ())
    {
      std::cerr << "Warning: link_up ignored, interface " << scion_if
                << " has no original neighbor mapping on AS " << real_as_no << std::endl;
      return;
    }

  uint16_t remote_as = original_if_map_it->second.at (scion_if);

  if (scion_as->interface_to_neighbor_map.find (scion_if) !=
      scion_as->interface_to_neighbor_map.end ())
    {
      // Already up.
      return;
    }

  scion_as->interface_to_neighbor_map.insert (std::make_pair (scion_if, remote_as));

  auto &interfaces = scion_as->interfaces_per_neighbor_as[remote_as];
  if (std::find (interfaces.begin (), interfaces.end (), scion_if) == interfaces.end ())
    {
      interfaces.push_back (scion_if);
      std::sort (interfaces.begin (), interfaces.end ());
    }

  std::vector<std::pair<uint16_t, int32_t>> current_neighbors;
  for (auto const &neighbor_rel : scion_as->neighbors)
    {
      current_neighbors.push_back (
          std::make_pair (neighbor_rel.first, (int32_t) neighbor_rel.second));
    }

  if (!HasNeighbor (current_neighbors, remote_as))
    {
      auto original_neighbors_it = original_neighbors.find (alias_as);
      if (original_neighbors_it != original_neighbors.end ())
        {
          for (auto const &neighbor_rel : original_neighbors_it->second)
            {
              if (neighbor_rel.first == remote_as)
                {
                  scion_as->neighbors.push_back (
                      std::make_pair (neighbor_rel.first, (NeighbourRelation) neighbor_rel.second));
                  break;
                }
            }
        }
    }
}

void
UserDefinedEvents::SendAPacket (std::string src_isd_number, std::string real_src_as_no,
                                std::string src_local_address, std::string dst_isd_number,
                                std::string real_dst_as_no, std::string dst_local_address,
                                std::string pyload_size)
{
}

void
UserDefinedEvents::SendPacketBatch (std::string src_isd_number, std::string real_src_as_no,
                                    std::string src_local_address, std::string dst_isd_number,
                                    std::string real_dst_as_no, std::string dst_local_address,
                                    std::string pyload_size, std::string no_pkts)
{
}

void
UserDefinedEvents::TimeReferencesDown ()
{
  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      ScionAs *scion_as = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
      TimeServer *time_server = dynamic_cast<TimeServer *> (scion_as->GetHost (2));

      time_server->reference_time_type = ReferenceTimeType::OFF;
    }
}

void
UserDefinedEvents::TimeReferencesUp ()
{
  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      ScionAs *scion_as = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
      TimeServer *time_server = dynamic_cast<TimeServer *> (scion_as->GetHost (2));

      time_server->reference_time_type = ReferenceTimeType::ON;
    }
}
} // namespace ns3
