//
// Created by Seyedali Tabaeiaghdaei on 20.04.22.
//

#ifndef SCION_SIMULATOR_SCION_AS_H
#define SCION_SIMULATOR_SCION_AS_H
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ns3/map-scheduler.h"
#include "ns3/network-module.h"
#include "ns3/node.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"

#include "src/SCION/headers/beaconing/beacon.h"
#include "src/SCION/headers/border_router.h"
#include "src/SCION/headers/path_server.h"
#include "src/SCION/headers/scion_host.h"
#include "src/SCION/headers/scion_packet.h"
#include "src/SCION/headers/user_defined_events.h"
#include "src/SCION/headers/utils.h"

namespace ns3 {

enum neighbour_relation { CORE = 0, PEER = 1, CUSTOMER = 2, PROVIDER = 3 };

class BeaconServer;

class SCION_AS : public Node
{
public:
  SCION_AS (uint32_t system_id, bool parallel_scheduler, uint16_t as_number,
            rapidxml::xml_node<> *xml_node, const YAML::Node &config, bool malicious_border_routers,
            Time local_time)
      : Node (system_id)
  {
    PropertyContainer p = parseProperties (xml_node);

    if (p.hasProperty ("isd"))
      {
        isd_number = std::stoi (p.getProperty ("isd"));
      }
    else
      {
        isd_number = 0;
      }

    this->as_number = as_number;
    ia_addr = (((uint32_t) isd_number) << 16) | ((uint32_t) as_number);

    this->local_time = local_time;

    this->malicious_border_routers = malicious_border_routers;

    if (malicious_border_routers)
      {
        border_routers_malicious_action =
            config["border_router"]["malicious_action"].as<std::string> ();
      }
    else
      {
        border_routers_malicious_action = "no";
      }

    instantiate_beacon_server (parallel_scheduler, xml_node, config);
  }

  virtual ~SCION_AS ()
  {
  }

  uint16_t isd_number;
  uint16_t as_number;
  ia_t ia_addr;

  Time local_time;
  int32_t AS_max_bwd;

  std::vector<Time> latencies_between_hosts_and_path_server;
  std::vector<Time> latencies_between_interfaces_and_beacon_server;
  Time latency_between_path_server_and_beacon_server;

  std::vector<std::pair<uint16_t, neighbour_relation>> neighbors;
  std::unordered_map<uint16_t, std::vector<uint16_t>> interfaces_per_neighbor_as;
  std::unordered_map<uint16_t, uint16_t> interface_to_neighbor_map;
  std::vector<std::pair<ld, ld>> interfaces_coordinates;
  std::multimap<std::pair<ld, ld>, uint16_t> coordinates_to_interfaces;
  std::vector<std::vector<ld>> latencies_between_interfaces;
  std::vector<int32_t> inter_as_bwds;

  void DoInitializations (uint32_t num_ASes, rapidxml::xml_node<> *xml_node,
                          const YAML::Node &config, bool only_propagation_delay);

  void DoInitializations (uint32_t num_ASes, rapidxml::xml_node<> *xml_node,
                          const YAML::Node &config);

  std::pair<uint16_t, SCION_AS *> GetRemoteAsInfo (uint16_t egress_interface_no);

  void ReceiveBeacon (Beacon &the_beacon, uint16_t sender_as, uint16_t remote_if,
                      uint16_t local_if);

  void SetBeaconServer (BeaconServer *beaconServer);

  void SetPathServer (PathServer *pathServer);

  BeaconServer *GetBeaconServer ();

  PathServer *GetPathServer ();

  SCIONCapableNode *GetHost (host_addr_t host_addr);

  uint32_t GetNHosts ();

  void AdvanceTime (ns3::Time advance);

  void AddHost (SCIONHost *host);

  BorderRouter *AddBR (double latitude, double longitude, Time processing_delay,
                       Time processing_throughput_delay);

  void AddToRemoteASInfo (uint16_t remote_if, SCION_AS *remote_as);

  friend class UserDefinedEvents;

protected:
  bool malicious_border_routers;
  std::string border_routers_malicious_action;

  BeaconServer *beacon_server;
  PathServer *path_server = NULL;
  std::vector<SCIONHost *> hosts;
  std::vector<BorderRouter *> border_routers;

  std::vector<std::pair<uint16_t, SCION_AS *>> remote_as_info;

  void connect_internal_nodes (bool only_propagation_delay);
  void initialize_latencies (bool only_propagation_delay);

  void instantiate_beacon_server (bool parallel_scheduler, rapidxml::xml_node<> *xml_node,
                                  const YAML::Node &config);
};
} // namespace ns3
#endif //SCION_SIMULATOR_SCION_AS_H
