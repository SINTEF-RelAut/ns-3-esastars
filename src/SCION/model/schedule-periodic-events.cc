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

#include <chrono>
#include <omp.h>
#include <sstream>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/nstime.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/ptr.h"
#include "ns3/simulator.h"

#include "src/SCION/model/beaconing/beacon-server.h"
#include "post-simulation-evaluations.h"
#include "schedule-periodic-events.h"
#include "scion-as.h"
#include "scion-host.h"
#include "time-server.h"

namespace ns3 {
NS_LOG_COMPONENT_DEFINE ("GlobalScheduling");

namespace {

void
ScheduleDataPlaneProbes (YAML::Node &config)
{
  if (!config["data_plane_probing"])
    {
      return;
    }

  YAML::Node probe_cfg = config["data_plane_probing"];
  if (!probe_cfg["pairs"])
    {
      NS_LOG_WARN ("data_plane_probing exists but no pairs are configured");
      return;
    }

  Time start_time = Seconds (0.0);
  Time period = Seconds (1.0);
  Time timeout = Seconds (2.0);
  uint32_t count = 0;

  if (probe_cfg["start"])
    {
      start_time = Time (probe_cfg["start"].as<std::string> ());
    }
  if (probe_cfg["period"])
    {
      period = Time (probe_cfg["period"].as<std::string> ());
    }
  if (probe_cfg["timeout"])
    {
      timeout = Time (probe_cfg["timeout"].as<std::string> ());
    }

  if (probe_cfg["count"])
    {
      count = probe_cfg["count"].as<uint32_t> ();
    }
  else if (config["simulation_duration"])
    {
      double sim_s = Time (config["simulation_duration"].as<std::string> ()).GetSeconds ();
      double period_s = period.GetSeconds ();
      count = (period_s > 0.0) ? static_cast<uint32_t> (sim_s / period_s) : 0;
    }

  if (probe_cfg["source_path_policy"])
    {
      for (auto policy_it = probe_cfg["source_path_policy"].begin ();
           policy_it != probe_cfg["source_path_policy"].end (); ++policy_it)
        {
          YAML::Node policy = *policy_it;
          if (!policy["src_as"] || !policy["preferred_first_hop_as"])
            {
              continue;
            }

          uint16_t src_real = policy["src_as"].as<uint16_t> ();
          host_addr_t src_host_addr = policy["src_host"].as<host_addr_t> (2);
          uint16_t preferred_real = policy["preferred_first_hop_as"].as<uint16_t> ();
          uint16_t backup_real = policy["backup_first_hop_as"].as<uint16_t> (0);

          if (real_to_alias_as_no.find (src_real) == real_to_alias_as_no.end ())
            {
              NS_LOG_WARN ("Skipping source_path_policy due to unknown AS " << src_real);
              continue;
            }

          uint16_t src_alias = real_to_alias_as_no.at (src_real);
          Ptr<ScionAs> src_as = DynamicCast<ScionAs> (nodes.Get (src_alias));
          if (src_as == NULL)
            {
              continue;
            }

          if (src_host_addr < 2 || (src_host_addr - 2) >= src_as->GetNHosts ())
            {
              NS_LOG_WARN ("Skipping source_path_policy due to invalid source host "
                           << src_host_addr << " in AS " << src_real);
              continue;
            }

          ScionHost *src_host = dynamic_cast<ScionHost *> (src_as->GetHost (src_host_addr));
          if (src_host == NULL)
            {
              NS_LOG_WARN ("Skipping source_path_policy due to missing source host "
                           << src_host_addr << " in AS " << src_real);
              continue;
            }

          src_host->ConfigureFirstHopPreference (preferred_real, backup_real);
        }
    }

  for (auto pair_it = probe_cfg["pairs"].begin (); pair_it != probe_cfg["pairs"].end (); ++pair_it)
    {
      YAML::Node pair = *pair_it;
      if (!pair["src_as"] || !pair["dst_as"])
        {
          continue;
        }

      uint16_t src_real = pair["src_as"].as<uint16_t> ();
      uint16_t dst_real = pair["dst_as"].as<uint16_t> ();
      host_addr_t src_host_addr = pair["src_host"].as<host_addr_t> (2);
      host_addr_t dst_host_addr = pair["dst_host"].as<host_addr_t> (2);

      if (real_to_alias_as_no.find (src_real) == real_to_alias_as_no.end () ||
          real_to_alias_as_no.find (dst_real) == real_to_alias_as_no.end ())
        {
          NS_LOG_WARN ("Skipping probe pair due to unknown AS in alias map: " << src_real << " -> "
                                                                              << dst_real);
          continue;
        }

      uint16_t src_alias = real_to_alias_as_no.at (src_real);
      uint16_t dst_alias = real_to_alias_as_no.at (dst_real);
      uint16_t dst_isd = 1;
      if (as_to_isd_map.find (dst_alias) != as_to_isd_map.end ())
        {
          dst_isd = as_to_isd_map.at (dst_alias);
        }

      Ptr<ScionAs> src_as = DynamicCast<ScionAs> (nodes.Get (src_alias));
      Ptr<ScionAs> dst_as = DynamicCast<ScionAs> (nodes.Get (dst_alias));
      if (src_as == NULL)
        {
          continue;
        }

      if (dst_as == NULL)
        {
          continue;
        }

      if (src_as->GetNHosts () == 0)
        {
          NS_LOG_WARN ("Skipping probe pair due to missing source hosts in AS " << src_real);
          continue;
        }

      if (dst_as->GetNHosts () == 0)
        {
          NS_LOG_WARN ("Skipping probe pair due to missing destination hosts in AS " << dst_real);
          continue;
        }

      if (src_host_addr < 2 || (src_host_addr - 2) >= src_as->GetNHosts ())
        {
          NS_LOG_WARN ("Skipping probe pair due to invalid source host address "
                       << src_host_addr << " in AS " << src_real);
          continue;
        }

      if (dst_host_addr < 2 || (dst_host_addr - 2) >= dst_as->GetNHosts ())
        {
          NS_LOG_WARN ("Skipping probe pair due to invalid destination host address "
                       << dst_host_addr << " in AS " << dst_real);
          continue;
        }

      ScionHost *src_host = dynamic_cast<ScionHost *> (src_as->GetHost (src_host_addr));
      if (src_host == NULL)
        {
          NS_LOG_WARN ("Skipping probe pair due to missing source host " << src_host_addr
                                                                         << " in AS " << src_real);
          continue;
        }

      std::string output_path;
      if (pair["output"])
        {
          output_path = pair["output"].as<std::string> ();
        }
      else
        {
          std::ostringstream oss;
          oss << "build/scion_probe_" << src_real << "_" << dst_real << ".csv";
          output_path = oss.str ();
        }

      src_host->ConfigureDataPlaneProbe (MAKE_IA (dst_isd, dst_alias), dst_host_addr, src_real,
                                         dst_real, count, period, timeout, start_time, output_path);
    }
}

} // namespace

void
SchedulePeriodicEvents (YAML::Node &config)
{
  for (uint32_t i = 0; i < nodes.GetN (); ++i)
    {
      Ptr<ScionAs> node = DynamicCast<ScionAs> (nodes.Get (i));

      if (config["beacon_service"])
        {
          node->GetBeaconServer ()->ScheduleBeaconing (
              Time (config["beacon_service"]["last_beaconing"].as<std::string> ()));
        }

      if (config["time_service"])
        {
          dynamic_cast<TimeServer *> (node->GetHost (2))->ScheduleListOfAllASesRequest ();
          dynamic_cast<TimeServer *> (node->GetHost (2))->ScheduleSnapShots ();
          dynamic_cast<TimeServer *> (node->GetHost (2))->ScheduleTimeSync ();
        }
    }

  if (config["beacon_service"])
    {
      Time logger_period = Time (config["beacon_service"]["period"].as<std::string> ());
      std::string logger_output_path = config["output"].as<std::string> () + ".paths.csv";
      uint32_t logger_max_paths_per_dst = 3;
      bool logger_discover_all_pairs = false;
      Time logger_request_lead_time = MilliSeconds (500);

      if (config["path_snapshot_logger"])
        {
          if (config["path_snapshot_logger"]["period"])
            {
              logger_period = Time (config["path_snapshot_logger"]["period"].as<std::string> ());
            }

          if (config["path_snapshot_logger"]["output"])
            {
              logger_output_path = config["path_snapshot_logger"]["output"].as<std::string> ();
            }

          if (config["path_snapshot_logger"]["max_paths_per_dst"])
            {
              logger_max_paths_per_dst =
                  config["path_snapshot_logger"]["max_paths_per_dst"].as<uint32_t> ();
            }

          if (config["path_snapshot_logger"]["discover_all_pairs"])
            {
              logger_discover_all_pairs =
                  config["path_snapshot_logger"]["discover_all_pairs"].as<bool> ();
            }

          if (config["path_snapshot_logger"]["request_lead_time"])
            {
              logger_request_lead_time =
                  Time (config["path_snapshot_logger"]["request_lead_time"].as<std::string> ());
            }
        }

      for (Time t = Seconds (0.0);
           t <= Time (config["beacon_service"]["last_beaconing"].as<std::string> ());
           t += Time (config["beacon_service"]["period"].as<std::string> ()))
        {
          Simulator::Schedule (t + TimeStep (2), &PeriodicBeaconingCheckPoint);
        }

      for (Time t = Seconds (0.0);
           t <= Time (config["beacon_service"]["last_beaconing"].as<std::string> ());
           t += logger_period)
        {
          if (logger_discover_all_pairs)
            {
              Time warmup_time =
                  (t > logger_request_lead_time) ? (t - logger_request_lead_time) : Seconds (0.0);
              Simulator::Schedule (warmup_time, &WarmPathRequestsForSnapshot);
            }

          Simulator::Schedule (t + TimeStep (3), &LogPathSnapshotCsv, logger_output_path,
                               logger_max_paths_per_dst, t.ToDouble (Time::S),
                               logger_discover_all_pairs);
        }
    }

  ScheduleDataPlaneProbes (config);
}

void
PeriodicBeaconingCheckPoint ()
{
  std::cout << "################################## "
            << DynamicCast<ScionAs> (nodes.Get (0))->GetBeaconServer ()->GetCurrentTime ()
            << " #########################################" << std::endl;
  uint32_t node_number = nodes.GetN ();

  // print number of connected pairs after each beaconing round
  uint32_t all_connected_pairs = 0;
  for (uint32_t i = 0; i < node_number; ++i)
    {
      all_connected_pairs += DynamicCast<ScionAs> (nodes.Get (i))
                                 ->GetBeaconServer ()
                                 ->GetValidBeaconsCountPerDstAs ()
                                 .size ();
    }
  std::cout << all_connected_pairs << std::endl;
}
} // namespace ns3
