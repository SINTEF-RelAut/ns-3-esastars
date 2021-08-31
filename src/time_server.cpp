//
// Created by seyedali on 30.08.21.
//

#include <algorithm>

#include "ns3/log.h"

#include "src/SCION/headers/time_server.h"
#include "src/SCION/headers/scion_core_as.h"

namespace ns3 {
    NS_LOG_COMPONENT_DEFINE("TimeServer");


    void TimeServer::request_set_of_all_core_ases_from_path_server () {
        NS_LOG_DEBUG("TimeSrv at " << isd_number << ":" << as_number << " sent req for all core ASes to PthSrv");

        payload_type_t payload_type = payload_type_t::REQ_FOR_LIST_OF_ALL_CORE_ASES;
        Payload payload;
        SCIONPacket* packet = create_packet(payload, payload_type, ia_addr, 1);

        send_packet(packet);
    }

    void TimeServer::process_received_packet(uint16_t local_if, SCIONPacket* packet) {
        SCIONHost::process_received_packet(local_if, packet);

        if (packet->payload_type == payload_type_t::LIST_OF_ALL_CORE_ASES) {
            NS_LOG_DEBUG("TimeSrv at " << isd_number << ":" << as_number << " rcv all core ASes from PthSrv");
            receive_set_of_all_core_ases_from_path_server(packet);
            packet->packet_originator->Drop(packet);
            return;
        }

        if (packet->payload_type == payload_type_t::BROADCAST_LIST_OF_ALL_CORE_ASES){
            NS_LOG_DEBUG("TimeSrv at" << isd_number << ":" << as_number << " rcv all core ASes from other TimeSrv " << GET_ISDN(packet->src_ia) << ":" << GET_ASN(packet->src_ia));
            receive_set_of_all_core_ases_from_other_time_server(packet);
            packet->packet_originator->Drop(packet);
            return;
        }

        if (packet->payload_type == payload_type_t::TIME_SYC_REQ) {

            packet->packet_originator->Drop(packet);
            return;
        }

        if (packet->payload_type == payload_type_t::TIME_SYNC_RESP) {

            packet->packet_originator->Drop(packet);
            return;
        }
    }

    void TimeServer::receive_set_of_all_core_ases_from_path_server(SCIONPacket* packet) {
        if (*packet->payload.list_of_all_ases.set_of_all_ases != set_of_all_core_ases) {
            std::vector<ia_t> v1(set_of_all_core_ases.begin(), set_of_all_core_ases.end());
            std::vector<ia_t> v2(packet->payload.list_of_all_ases.set_of_all_ases->begin(), packet->payload.list_of_all_ases.set_of_all_ases->end());
            std::vector<ia_t> result(v1.size() + v2.size());
            std::vector<ia_t>::iterator it = std::set_union(v1.begin(), v1.end(), v2.begin(), v2.end(), result.begin());
            std::set<ia_t> result_set(result.begin(), it);
            set_of_all_core_ases = result_set;

            request_for_paths_to_all_core_ases();
            process_scheduler->Schedule(MilliSeconds(300), &TimeServer::send_set_of_all_core_ases_to_neighbors, this);
        }
    }

    void TimeServer::receive_set_of_all_core_ases_from_other_time_server(SCIONPacket *packet) {
        if (*packet->payload.list_of_all_ases.set_of_all_ases != set_of_all_core_ases) {
            std::vector<ia_t> v1(set_of_all_core_ases.begin(), set_of_all_core_ases.end());
            std::vector<ia_t> v2(packet->payload.list_of_all_ases.set_of_all_ases->begin(), packet->payload.list_of_all_ases.set_of_all_ases->end());
            std::vector<ia_t> result(v1.size() + v2.size());
            std::vector<ia_t>::iterator it = std::set_union(v1.begin(), v1.end(), v2.begin(), v2.end(), result.begin());
            std::set<ia_t> result_set(result.begin(), it);
            set_of_all_core_ases = result_set;

            send_set_of_all_core_ases_to_neighbors();
        }
    }

    void TimeServer::request_for_paths_to_all_core_ases() {
        std::set<uint16_t> all_isds;

        for (auto const & isd_as : set_of_all_core_ases) {
            all_isds.insert(GET_ISDN(isd_as));
        }

        for (auto const & isd : all_isds) {
            NS_LOG_DEBUG("TimeSrv at " << isd_number << ":" << as_number << " send req for paths to isd " << isd);
            if (isd == isd_number) {
                send_request_for_path_segments(path_segment_type::CORE_SEG, 0, 0);
            } else {
                send_request_for_path_segments(path_segment_type::CORE_SEG, 0, MAKE_IA(isd, 0));
            }
        }
    }

    void TimeServer::send_set_of_all_core_ases_to_neighbors() {
        std::set<const PathSegment*> paths_to_neighbor_ases;

        for (auto const & dst_ia_cached_paths_pair : cached_core_path_segments) {
            for (auto const & [exp_time, path_seg] : *dst_ia_cached_paths_pair.second->at(ia_addr)) {
                if (exp_time > AS->local_time.GetMinutes()) {
                    if (path_seg->hops.size() == 2) {
                        paths_to_neighbor_ases.insert(path_seg);
                    }
                }
            }
        }

        for (auto const & path : paths_to_neighbor_ases) {
            NS_LOG_DEBUG("TimeSrv at " << isd_number << ":" << as_number << " sent list of all ases to " << GET_HOP_ISD(path->hops.back()) << ":" << GET_HOP_AS(path->hops.back()));
            payload_type_t payload_type = payload_type_t::BROADCAST_LIST_OF_ALL_CORE_ASES;

            Payload payload;
            payload.list_of_all_ases.set_of_all_ases = &set_of_all_core_ases;

            SCIONPacket* packet = create_packet(payload, payload_type, GET_HOP_IA(path->hops.back()), 2);

            packet->path.push_back(path);

            send_packet(packet);
        }
    }

    Time TimeServer::get_local_time() {
        return drift * ((Simulator::Now().GetPicoSeconds() - the_real_time_of_last_sync.GetPicoSeconds()) / Days(1).GetPicoSeconds());
    }

    Time TimeServer::get_reference_time() {
        return Simulator::Now() - Time("10ns");
    }

    void TimeServer::run_core_time_sync_algo() {
        get_local_time();
        get_reference_time();

        the_real_time_of_last_sync = Simulator::Now();
    }

    void TimeServer::ScheduleListOfAllASesRequest() {
        for (Time t = first_event; t < last_event; t += list_of_ases_req_period) {
            process_scheduler->Schedule(t, &TimeServer::request_set_of_all_core_ases_from_path_server, this);
        }
    }
    void TimeServer::ScheduleTimeSync() {
        for (Time t = first_event + Seconds(1); t < last_event + Seconds(1); t += time_sync_period) {
            process_scheduler->Schedule(t, &TimeServer::run_core_time_sync_algo, this);
        }
    }
}
