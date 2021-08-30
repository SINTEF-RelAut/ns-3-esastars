//
// Created by seyedali on 30.08.21.
//

#ifndef NS_3_BEACONING_SIMULATOR_TIME_SERVER_H
#define NS_3_BEACONING_SIMULATOR_TIME_SERVER_H

#include "src/SCION/headers/scion_as.h"


namespace ns3 {
    class TimeServer : public SCIONHost {
    public:
        void SendSetOfAllCoreASesToNeighbors();
        void ReceiveNeighborsCoreASesSet();
        void RunCoreTimeSyncAlgo();
    private:
        Time the_real_time_of_last_sync;
        Time drift;
        std::set<ia_t> set_of_all_core_ases;

        Time get_local_time();
        Time get_reference_time();

        void request_set_of_all_core_ases_from_path_server();

        void send_set_of_all_core_ases_to_neighbors();

        void receive_set_of_all_core_ases_from_path_server();

        void process_received_packet(uint16_t local_if, SCIONPacket* packet) override;
    };
}

#endif //NS_3_BEACONING_SIMULATOR_TIME_SERVER_H
