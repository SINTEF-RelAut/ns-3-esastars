/**
 * @file scion_as.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see scion_as.h
 * @brief Defines the SCION ASes which are not part of the core.
 *
 */
#ifndef SCION_BEACONING_SIMMULATOR_SCION_AS_H
#define SCION_BEACONING_SIMMULATOR_SCION_AS_H

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
#include "src/SCION/headers/local_scheduler.h"
#include "src/SCION/headers/scion_packet.h"

namespace ns3 {

    enum neighbour_relation {
        CORE = 0, PEER = 1, CUSTOMER = 2, PROVIDER = 3
    };

    class BeaconServer;
    class SCIONHost;
    class PathServer;

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
        /** @brief The autonomous system number of this node. */
        uint16_t as_number;

        ia_t ia_addr;

        Time local_time;
        /** @brief Largest amount of bandwidth found on any border router link. */
        int32_t AS_max_bwd;

        std::vector<LocalScheduler*> events; // a vector of (#interfaces +  #services + #hosts) schedulers

        std::vector<Time> latencies_between_hosts_and_path_server;
        std::vector<Time> latencies_between_interfaces_and_beacon_server;
        Time latency_between_path_server_and_beacon_server;




        // Interfaces Properties *****************************************************************************************************
        /**
         * @brief Different types of links.
         *
         * In typical BGP-enabled internet topologies, there are peer, and customer links. The Provider type was introduced
         * to model the reverse directionality of a customer link, the core type is found between core-ASes in SCION-topologies.
         */

        /** @brief Holds the AS numbers of all the neighbours of the node*/
        std::vector<std::pair<uint16_t, neighbour_relation> > neighbors;
        /** @brief Holds a mapping of AS numbers and their connected interfaces & relations to this node. */
        std::unordered_map<uint16_t, std::vector<uint16_t> > interfaces_per_neighbor_as;

        // and could save some space by not storing the interface_coordinates.
        /** @brief Maps all interfaces to their corresponding remote AS numbers
        */
        std::unordered_map<uint16_t, uint16_t> interface_to_neighbor_map;
        /** @brief Holds the coordinates of the border router locations between ASes.
         *
         * The pair of border routers are assumed to be in close proximity (same room) which is why we do not model
         * any latency between them.
         */
        std::vector<std::pair<ld, ld>> interfaces_coordinates;

        /** @brief Holds the estimated latencies between the border routers inside this AS.*/
        std::vector<std::vector<ld>> latencies_between_interfaces;
        /** @brief Holds the bandwidths of the links between border routers of ASes. */
        std::vector<int32_t> inter_as_bwds;



        /**
         * @brief Initializes the latencies_between_interfaces and the as_max_bw fields.
         */

        void DoInitializations();

        void DoInitializations(uint32_t all_nodes);

        std::pair<uint16_t, Ptr<SCION_AS>>
        GetRemoteAsInfo(uint16_t egress_interface_no);

        void ReceiveBeacon(Beacon &the_beacon, uint16_t sender_as, uint16_t remote_if, uint16_t local_if);

        void SetBeaconServer(BeaconServer* beaconServer);

        void SetPathServer(PathServer* pathServer);

        BeaconServer* GetBeaconServer();

        PathServer* GetPathServer();

        SCIONHost* GetHost(uint32_t host_addr);

        uint32_t GetPathServerSchedulerIdx();

        uint32_t GetBeaconServerSchedulerIdx();

        uint32_t GetHostSchedulerIdx(uint32_t host_addr);

        void AdvanceTime (ns3::Time advance);

        void ExecuteLocalScheduler();

        void AddHost(SCIONHost* host);


        uint64_t GetFirstEventTime ();

    protected:
        BeaconServer *beaconServer;
        PathServer *pathServer;
        std::vector<SCIONHost*> hosts;
    private:
        /**
         *  @brief Returns the average as-level diversity and link-level diversity scores of the passed beacon compared to all other beacons the node has which
         *  originated at the same destination as number.
         */
        std::pair<ld, ld> calculate_final_diversity_scores(Beacon *the_beacon);

    };
}
#endif //SCION_BEACONING_SIMMULATOR_SCION_AS_H
