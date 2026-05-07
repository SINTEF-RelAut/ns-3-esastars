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

#ifndef SCION_SIMULATOR_USER_DEFINED_EVENTS_H
#define SCION_SIMULATOR_USER_DEFINED_EVENTS_H

#include <cstdint>
#include <fstream>
#include <functional>
#include <unordered_map>
#include <vector>
#include <yaml-cpp/yaml.h>

#include "ns3/object.h"
#include "ns3/ptr.h"
#include "src/core/model/simulator.h"

#include "json.hpp"
#include "path-segment.h"
#include "scion-as.h"
#include "scion-packet.h"
#include "virtual-ixp-fabric.h"

namespace detail {

template <std::size_t... indices>
struct index_sequence
{
};

template <std::size_t N, std::size_t... indices>
struct make_index_sequence_impl : make_index_sequence_impl<N - 1, N - 1, indices...>
{
};

template <std::size_t... indices>
struct make_index_sequence_impl<0, indices...>
{
  typedef index_sequence<indices...> type;
};

template <std::size_t N>
struct make_index_sequence
{
  typedef typename make_index_sequence_impl<N>::type type;
};

} // namespace detail

struct AnyCallable
{
  AnyCallable ()
  {
  }

  explicit AnyCallable (std::function<void (const std::vector<std::string> &)> fun) : m_fun (fun)
  {
  }

  AnyCallable &
  operator= (std::function<void (const std::vector<std::string> &)> fun)
  {
    m_fun = fun;
    return *this;
  }

  void
  operator() (const std::vector<std::string> &vec) const
  {
    if (m_fun)
      {
        m_fun (vec);
      }
  }

  std::function<void (const std::vector<std::string> &)> m_fun;
};

template <class R, class U, class... Types, std::size_t... indices>
std::function<void (const std::vector<std::string> &)>
BindFactory (R (U::*f) (Types...), U *val, detail::index_sequence<indices...> /*seq*/)
{
  return [f, val] (const std::vector<std::string> &vec) {
    if (vec.size () != sizeof...(Types))
      {
        return;
      }

    (val->*f) (vec[indices]...);
  };
}

template <class R, class U, class... Types>
std::function<void (const std::vector<std::string> &)>
FunctionFactory (R (U::*f) (Types...), U *val)
{
  return BindFactory (f, val, typename detail::make_index_sequence<sizeof...(Types)>::type ());
}

namespace ns3 {

class UserDefinedEvents
{
public:
  UserDefinedEvents (YAML::Node &config, NodeContainer &as_nodes,
                     std::map<int32_t, uint16_t> &real_to_alias_as_no,
                     std::map<uint16_t, int32_t> &alias_to_real_as_no)
      : config (config),
        as_nodes (as_nodes),
        real_to_alias_as_no (real_to_alias_as_no),
        alias_to_real_as_no (alias_to_real_as_no)
  {
    if (!config["events_file"])
      {
        this->~UserDefinedEvents ();
        return;
      }

    ConstructFuncMap ();
    ReadAndScheduleUserDefinedEvents (config["events_file"].as<std::string> ());
  }

private:
  YAML::Node &config;
  NodeContainer &as_nodes;
  std::map<int32_t, uint16_t> &real_to_alias_as_no;
  std::map<uint16_t, int32_t> &alias_to_real_as_no;

  std::unordered_map<std::string, AnyCallable> function_name_to_function;

  // Per-AS snapshots of initial link state to support reversible LinkDown/LinkUp events.
  std::unordered_map<uint16_t, std::unordered_map<uint16_t, uint16_t>>
      original_interface_to_neighbor_map;
  std::unordered_map<uint16_t, std::unordered_map<uint16_t, std::vector<uint16_t>>>
      original_interfaces_per_neighbor_as;
  std::unordered_map<uint16_t, std::vector<std::pair<uint16_t, int32_t>>> original_neighbors;

  // Mapping from configured topology IFIDs (e.g., 1020001) to runtime SCION IFIDs (1..N).
  std::unordered_map<int32_t, std::unordered_map<uint32_t, uint16_t>> configured_if_to_scion_if;

  bool virtual_ixp_enabled = false;
  Ptr<VirtualIxpFabric> virtual_ixp_fabric;
  uint32_t virtual_ixp_next_link_index = 1;
  std::unordered_map<uint64_t, IxpLinkId> virtual_ixp_link_by_as_if;
  std::unordered_map<uint64_t, double> virtual_ixp_quality_by_as_if;

  void ConstructFuncMap ();

  void SnapshotInitialLinkState ();
  void BuildConfiguredIfIdMap ();
  void InitializeVirtualIxpIfConfigured ();
  uint64_t MakeAsIfKey (uint16_t alias_as_no, uint16_t scion_if_id) const;
  bool HandleVirtualIxpLinkEvent (uint16_t alias_as_no, uint16_t scion_if_id, bool up);

  ScionAs *GetAsByRealAsNo (int32_t real_as_no) const;
  uint16_t ResolveScionIfId (ScionAs *scion_as, int32_t real_as_no, uint32_t event_if_id) const;

  static bool HasNeighbor (const std::vector<std::pair<uint16_t, int32_t>> &neighbors,
                           uint16_t neighbor_as);

  void ReadAndScheduleUserDefinedEvents (const std::string &events_file_str);

  void RunUserSpecifiedEvent (const std::string &func_name, std::vector<std::string> vec);

  void AddAHost (std::string isd_number, std::string real_as_no, std::string local_address);

  void LinkDown (std::string isd_number, std::string real_as_no, std::string if_id);
  void LinkUp (std::string isd_number, std::string real_as_no, std::string if_id);

  void SendAPacket (std::string src_isd_number, std::string real_src_as_no,
                    std::string src_local_address, std::string dst_isd_number,
                    std::string real_dst_as_no, std::string dst_local_address,
                    std::string pyload_size);

  void SendPacketBatch (std::string src_isd_number, std::string real_src_as_no,
                        std::string src_local_address, std::string dst_isd_number,
                        std::string real_dst_as_no, std::string dst_local_address,
                        std::string pyload_size, std::string no_pkts);

  void TimeReferencesDown ();
  void TimeReferencesUp ();
};
} // namespace ns3
#endif //SCION_SIMULATOR_USER_DEFINED_EVENTS_H
