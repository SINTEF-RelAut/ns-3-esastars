/**
 * @file scion_as.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 */
#ifndef SCION_BEACONING_SIMULATOR_SCION_AS_H
#define SCION_BEACONING_SIMULATOR_SCION_AS_H

#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "ns3/network-module.h"
#include "ns3/node.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/map-scheduler.h"

#include "src/SCION/headers/beaconing/beacon.h"
#include "src/SCION/headers/scion_packet.h"
#include "src/SCION/headers/border_router.h"
#include "src/SCION/headers/scion_host.h"
#include "src/SCION/headers/path_server.h"


namespace ns3 {

    enum neighbour_relation {
        CORE = 0, PEER = 1, CUSTOMER = 2, PROVIDER = 3
    };

    class BeaconServer;

    class SCION_AS : public Node {
    public:

        SCION_AS(uint16_t isd_number, uint16_t as_number, uint32_t system_id, Time local_time)
                : Node(system_id),
                  isd_number(isd_number),
                  as_number(as_number),
                  local_time (local_time)
        {
            ia_addr = (((uint32_t) isd_number) << 16) | ((uint32_t) as_number);
        }

        virtual ~SCION_AS() {
        }

        uint16_t isd_number;
        uint16_t as_number;
        ia_t ia_addr;

        Time local_time;
        int32_t AS_max_bwd;

        std::vector<Time> latencies_between_hosts_and_path_server;
        std::vector<Time> latencies_between_interfaces_and_beacon_server;
        Time latency_between_path_server_and_beacon_server;

        std::vector<std::pair<uint16_t, neighbour_relation> > neighbors;
        std::unordered_map<uint16_t, std::vector<uint16_t> > interfaces_per_neighbor_as;
        std::unordered_map<uint16_t, uint16_t> interface_to_neighbor_map;
        std::vector<std::pair<ld, ld>> interfaces_coordinates;
        std::vector<std::vector<ld>> latencies_between_interfaces;
        std::vector<int32_t> inter_as_bwds;

        void DoInitializations(uint32_t num_ASes, bool only_propagation_delay, std::string border_routers_malicious_action, Time malicious_delay);

        void DoInitializations(uint32_t num_ASes);

        std::pair<uint16_t, SCION_AS*>
        GetRemoteAsInfo(uint16_t egress_interface_no);

        void ReceiveBeacon(Beacon &the_beacon, uint16_t sender_as, uint16_t remote_if, uint16_t local_if);

        void SetBeaconServer(BeaconServer* beaconServer);

        void SetPathServer(PathServer* pathServer);

        BeaconServer* GetBeaconServer();

        PathServer* GetPathServer();

        SCIONCapableNode * GetHost(host_addr_t host_addr);

        uint32_t GetNHosts();

        void AdvanceTime (ns3::Time advance);

        void AddHost(SCIONHost* host);

        BorderRouter* AddBR (double latitude, double longitude, Time processing_delay, Time processing_throughput_delay);

        void AddToRemoteASInfo (uint16_t remote_if, SCION_AS* remote_as);

    protected:
        BeaconServer* beaconServer;
        PathServer* pathServer;
        std::vector<SCIONHost*> hosts;
        std::vector<BorderRouter*> border_routers;

        std::vector<std::pair<uint16_t, SCION_AS*>> remote_as_info;

        std::pair<ld, ld> calculate_final_diversity_scores(Beacon *the_beacon);

        void connect_internal_nodes(bool only_propagation_delay, std::string border_routers_malicious_action, Time malicious_delay);
        void initialize_latencies(bool only_propagation_delay);

    };
}
#endif //SCION_BEACONING_SIMULATOR_SCION_AS_H
