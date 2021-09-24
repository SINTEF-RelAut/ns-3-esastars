//
// Created by seyedali on 30.08.21.
//

#ifndef NS_3_BEACONING_SIMULATOR_TIME_SERVER_H
#define NS_3_BEACONING_SIMULATOR_TIME_SERVER_H

#include <algorithm>
#include <random>
#include <set>

#include "ns3/nstime.h"

#include "src/SCION/headers/scion_host.h"

namespace ns3 {
    class TimeServer : public SCIONHost {

    public:
        TimeServer(uint32_t system_id, uint16_t isd_number, uint16_t as_number, host_addr_t local_address,
        double latitude, double longitude, SCION_AS* AS, bool parallel_scheduler, Time max_initial_drift, Time max_drift_per_day, Time global_cut_off,
        Time first_event, Time last_event, Time list_of_ases_req_period, Time time_sync_period, uint32_t G,
                   uint32_t number_of_paths_to_use_for_global_sync, bool read_disjoint_paths, std::string set_of_disjoint_paths_directory) :
                SCIONHost(system_id, isd_number, as_number, local_address, latitude, longitude, AS),
                max_initial_drift(max_initial_drift), max_drift_per_day(max_drift_per_day), global_cut_off(global_cut_off),
                first_event(first_event), last_event(last_event), list_of_ases_req_period(list_of_ases_req_period),
                time_sync_period(time_sync_period), G(G), number_of_paths_to_use_for_global_sync(number_of_paths_to_use_for_global_sync),
        read_disjoint_paths(read_disjoint_paths){
            synchronization_round = 0;

            std::random_device rd;
            std::uniform_int_distribution<int64_t> dist (-std::abs(max_initial_drift.GetPicoSeconds()), std::abs(max_initial_drift.GetPicoSeconds()));
            int64_t random_drift_int = dist(rd);

            local_time = PicoSeconds(0);

            if (random_drift_int < 0) {
                local_time -= PicoSeconds(std::abs(random_drift_int));
            } else {
                local_time += PicoSeconds(std::abs(random_drift_int));
            }

            real_time_of_last_local_time_update = PicoSeconds(0);
            set_of_all_core_ases.insert(ia_addr);

            set_of_disjoint_paths_file = set_of_disjoint_paths_directory + "set_of_disjoint_path_TS_" + std::to_string(ia_addr) + ".json";
        }

        void ScheduleListOfAllASesRequest();
        void ScheduleTimeSync(ia_t  printer_ia);


        void AdvanceLocalTime() override;



    private:
        bool parallel_scheduler;
        Time max_initial_drift;
        Time max_drift_per_day;
        Time global_cut_off;
        Time first_event, last_event;
        Time list_of_ases_req_period;
        Time time_sync_period;

        uint32_t G;
        uint32_t number_of_paths_to_use_for_global_sync;
        bool read_disjoint_paths;
        uint32_t synchronization_round; // i in the Listing 2

        Time real_time_of_last_local_time_update;

        std::set<ia_t> set_of_all_core_ases;

        std::string  set_of_disjoint_paths_file;

        int64_t loff;
        std::unordered_map<ia_t, std::multiset<int64_t>> poff;

        std::unordered_map<ia_t, std::unordered_set<const PathSegment*>> set_of_most_disjoint_paths;

        Time get_reference_time();

        Time get_max_drift(Time duration);

        void request_set_of_all_core_ases_from_path_server();

        void request_for_paths_to_all_core_ases();

        void send_set_of_all_core_ases_to_neighbors();

        void send_ntp_req_to_peers();

        void receive_set_of_all_core_ases_from_path_server(SCIONPacket* packet);

        void construct_set_of_most_disjoint_paths();

        void read_set_of_disjoint_paths();

        void write_set_of_disjoint_paths();

        void receive_set_of_all_core_ases_from_other_time_server(SCIONPacket* packet);

        void receive_ntp_req_from_peer(SCIONPacket* packet, Time receive_time);

        void receive_ntp_res_from_peer(SCIONPacket* packet, Time receive_time);

        void trigger_core_time_sync_algo(ia_t printer_ia);

        void continue_global_time_sync();

        void correct_local_time (int64_t corr);

        void process_received_packet(uint16_t local_if, SCIONPacket *packet, Time receive_time) override;
    };
}

#endif //NS_3_BEACONING_SIMULATOR_TIME_SERVER_H
