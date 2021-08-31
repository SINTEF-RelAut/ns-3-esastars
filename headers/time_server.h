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
        double latitude, double longitude, SCION_AS* AS) :
        SCIONHost(system_id, isd_number, as_number, local_address, latitude, longitude, AS){}

        void ScheduleListOfAllASesRequest();
        void ScheduleTimeSync();


    private:
        Time the_real_time_of_last_sync;
        Time drift;

        Time first_event = Minutes(60), last_event = Minutes(200);
        Time list_of_ases_req_period = Minutes(60);
        Time time_sync_period = Minutes(10);

        std::set<ia_t> set_of_all_core_ases;

        Time get_local_time();
        Time get_reference_time();

        void request_set_of_all_core_ases_from_path_server();

        void request_for_paths_to_all_core_ases();

        void send_set_of_all_core_ases_to_neighbors();

        void receive_set_of_all_core_ases_from_path_server(SCIONPacket* packet);

        void receive_set_of_all_core_ases_from_other_time_server(SCIONPacket* packet);

        void run_core_time_sync_algo();

        void process_received_packet(uint16_t local_if, SCIONPacket* packet) override;
    };
}

#endif //NS_3_BEACONING_SIMULATOR_TIME_SERVER_H
