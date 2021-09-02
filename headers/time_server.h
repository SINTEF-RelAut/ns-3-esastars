//
// Created by seyedali on 30.08.21.
//

#ifndef NS_3_BEACONING_SIMULATOR_TIME_SERVER_H
#define NS_3_BEACONING_SIMULATOR_TIME_SERVER_H

#include <set>

#include "ns3/nstime.h"

#include "src/SCION/headers/scion_host.h"

namespace ns3 {
    class TimeServer : public SCIONHost {

    public:
        TimeServer(uint32_t system_id, uint16_t isd_number, uint16_t as_number, host_addr_t local_address,
        double latitude, double longitude, SCION_AS* AS, Time max_drift_per_day, Time GlobalCutoff, Time first_event, Time last_event, Time list_of_ases_req_period, Time time_sync_period, uint32_t G) :
        SCIONHost(system_id, isd_number, as_number, local_address, latitude, longitude, AS),
        max_drift_per_day(max_drift_per_day), GlobalCutoff(GlobalCutoff),
        first_event(first_event), last_event(last_event), list_of_ases_req_period(list_of_ases_req_period),
        time_sync_period(time_sync_period), G(G){
            synchronization_round = 0;
            local_time = PicoSeconds(0);
            real_time_of_last_local_time_update = PicoSeconds(0);
        }

        void ScheduleListOfAllASesRequest();
        void ScheduleTimeSync();


        void AdvanceLocalTime() override;
    private:
        Time max_drift_per_day;
        Time GlobalCutoff;
        Time first_event, last_event;
        Time list_of_ases_req_period;
        Time time_sync_period;

        uint32_t G;
        uint32_t synchronization_round; // i in the Listing 2

        Time real_time_of_last_local_time_update;

        std::set<ia_t> set_of_all_core_ases;

        int64_t loff;
        std::map<ia_t, std::multiset<int64_t>> poff;

        Time get_reference_time();

        Time get_max_drift(Time duration);

        void request_set_of_all_core_ases_from_path_server();

        void request_for_paths_to_all_core_ases();

        void send_set_of_all_core_ases_to_neighbors();

        void send_ntp_req_to_peers();

        void get_the_most_disjoint_set_of_core_path_segs_to_as (ia_t dst_ia, std::set<const PathSegment*>& set_of_paths);

        void receive_set_of_all_core_ases_from_path_server(SCIONPacket* packet);

        void receive_set_of_all_core_ases_from_other_time_server(SCIONPacket* packet);

        void receive_ntp_req_from_peer(SCIONPacket* packet, Time receive_time);

        void receive_ntp_res_from_peer(SCIONPacket* packet, Time receive_time);

        void trigger_core_time_sync_algo();

        void continue_global_time_sync();

        void correct_local_time (int64_t corr);

        void process_received_packet(uint16_t local_if, SCIONPacket *packet, Time receive_time) override;
    };
}

#endif //NS_3_BEACONING_SIMULATOR_TIME_SERVER_H
