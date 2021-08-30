//
// Created by seyedali on 30.08.21.
//

#include "src/SCION/headers/time_server.h"

namespace ns3 {

    void TimeServer::SendSetOfAllCoreASesToNeighbors() {
        request_set_of_all_core_ases_from_path_server();
    }


    void TimeServer::request_set_of_all_core_ases_from_path_server () {
        payload_type_t payload_type = payload_type_t::LIST_OF_ALL_ASES_REQ;
        Payload payload;
        SCIONPacket* packet = create_packet(payload, payload_type, ia_addr, 1);

        send_packet(packet);
    }

    void TimeServer::process_received_packet(uint16_t local_if, SCIONPacket* packet) {
        SCIONCapableNode::process_received_packet(local_if, packet);

        if (packet->payload_type == payload_type_t::LIST_OF_ALL_ASES_RESP) {
            receive_set_of_all_core_ases_from_path_server();
        } else if (packet->payload_type == payload_type_t::LIST_OF_ASES_BROADCAST){

        } else if (packet->payload_type == payload_type_t::TIME_SYC_REQ) {

        } else if (packet->payload_type == payload_type_t::TIME_SYNC_RESP) {

        }

        packet->packet_originator->Drop(packet);
    }

    void TimeServer::receive_set_of_all_core_ases_from_path_server() {
        if (*packet->payload.list_of_all_ases.set_of_all_ases != set_of_all_core_ases) {
            set_of_all_core_ases = *packet->payload.list_of_all_ases.set_of_all_ases;
            send_set_of_all_core_ases_to_neighbors();
        }
    }

    void TimeServer::send_set_of_all_core_ases_to_neighbors() {

    }

    Time TimeServer::get_local_time() {
        return drift * (Simulator::Now().GetPicoSeconds() - the_real_time_of_last_sync.GetPicoSeconds()) / Time("24h").GetPicoSeconds();
    }

    Time TimeServer::get_reference_time() {
        return Simulator::Now() - Time("10ns");
    }
}
