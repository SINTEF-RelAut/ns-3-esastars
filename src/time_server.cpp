//
// Created by seyedali on 30.08.21.
//


#include <omp.h>

#include "ns3/log.h"

#include "src/SCION/headers/global_scheduling.h"
#include "src/SCION/headers/time_server.h"
#include "src/SCION/headers/scion_core_as.h"
#include "src/SCION/headers/utils.h"
#include "src/SCION/headers/externs.h"

namespace ns3 {
    NS_LOG_COMPONENT_DEFINE("TimeServer");


    void TimeServer::request_set_of_all_core_ases_from_path_server () {
        AdvanceLocalTime();

        NS_LOG_DEBUG("TimeSrv at " << isd_number << ":" << as_number << " sent req for all core ASes to PthSrv");

        payload_type_t payload_type = payload_type_t::REQ_FOR_LIST_OF_ALL_CORE_ASES;
        Payload payload;
        SCIONPacket* packet = create_scion_packet(payload, payload_type, ia_addr, 1, 0);

        send_scion_packet(packet);
    }

    void TimeServer::process_received_packet(uint16_t local_if, SCIONPacket *packet, Time receive_time) {
        SCIONHost::process_received_packet(local_if, packet, receive_time);

        if (packet->payload_type == payload_type_t::LIST_OF_ALL_CORE_ASES) {
            NS_LOG_DEBUG("TimeSrv at " << isd_number << ":" << as_number << " rcv all core ASes from PthSrv");
            receive_set_of_all_core_ases_from_path_server(packet);
            packet->packet_originator->DestroySCIONPacket(packet);
            return;
        }

        if (packet->payload_type == payload_type_t::BROADCAST_LIST_OF_ALL_CORE_ASES){
            NS_LOG_DEBUG("TimeSrv at " << isd_number << ":" << as_number << " rcv all core ASes from other TimeSrv " << GET_ISDN(packet->src_ia) << ":" << GET_ASN(packet->src_ia));
            receive_set_of_all_core_ases_from_other_time_server(packet);
            packet->packet_originator->DestroySCIONPacket(packet);
            return;
        }

        if (packet->payload_type == payload_type_t::NTP_REQ) {
            NS_LOG_DEBUG("TimeSrv at " << isd_number << ":" << as_number << " rcv ntp req from " << GET_ISDN(packet->src_ia) << ":" << GET_ASN(packet->src_ia));
            receive_ntp_req_from_peer(packet, receive_time);
            return;
        }

        if (packet->payload_type == payload_type_t::NTP_RESP) {
            NS_LOG_DEBUG("TimeSrv at " << isd_number << ":" << as_number << " rcv ntp resp from " << GET_ISDN(packet->src_ia) << ":" << GET_ASN(packet->src_ia));
            receive_ntp_res_from_peer(packet, receive_time);
            DestroySCIONPacket(packet);
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

            if (as_number == 0) {
                Simulator::Schedule(MilliSeconds(300), &RunParallelEvents, local_address);
            }

            Simulator::Schedule(MilliSeconds(350), &TimeServer::send_set_of_all_core_ases_to_neighbors, this);
        }
    }

    void TimeServer::ConstructSetOfMostDisjointPaths () {
        NS_LOG_DEBUG("TimeSrv at " << isd_number << ":" << as_number << " constructing disjoint paths");
        std::set<ia_t> tmp_set_of_all_core_ases = set_of_all_core_ases;
        tmp_set_of_all_core_ases.erase(ia_addr);

        std::vector<ia_t> vector_of_all_core_ases(tmp_set_of_all_core_ases.begin(), tmp_set_of_all_core_ases.end());
        uint32_t size = vector_of_all_core_ases.size();

        for (uint32_t i = 0; i < size; ++i) {
            ia_t dst_ia = vector_of_all_core_ases.at(i);
            set_of_most_disjoint_paths.insert(std::make_pair(dst_ia, std::set<const PathSegment*>()));
        }
//        omp_set_num_threads(NUM_CORE);
//#pragma omp parallel for schedule(dynamic, 1)
        for (uint32_t i = 0; i < size; ++i) {
            ia_t dst_ia = vector_of_all_core_ases.at(i);
            std::unordered_map<uint32_t, uint32_t> number_of_paths_per_link_selected_paths;

            while (set_of_most_disjoint_paths.at(dst_ia).size() < number_of_paths_to_use_for_global_sync
                   && set_of_most_disjoint_paths.at(dst_ia).size() < cached_core_path_segments.at(dst_ia)->at(ia_addr)->size()) {

                const PathSegment* best_path;

                uint64_t best_path_score = std::numeric_limits<uint64_t>::max();
                for (auto const &path_seg: *cached_core_path_segments.at(dst_ia)->at(ia_addr)) {
                    uint64_t path_seg_score = 1;
                    for (uint32_t  j = 0; j < path_seg.second->hops.size() - 1; ++j) {
                        uint64_t hop = path_seg.second->hops.at(j);
                        uint32_t link = GET_HOP_AS_ING(hop);
                        if (number_of_paths_per_link_selected_paths.find(link) != number_of_paths_per_link_selected_paths.end()) {
                            path_seg_score += (number_of_paths_per_link_selected_paths.at(link) + 1);
                        }
                    }

                    path_seg_score /= (path_seg.second->hops.size() - 1);

                    if (path_seg_score < best_path_score) {
                        best_path = path_seg.second;
                        best_path_score = path_seg_score;
                    }
                }

                set_of_most_disjoint_paths.at(dst_ia).insert(best_path);

                for (uint32_t  j = 0; j < best_path->hops.size() - 1; ++j) {
                    uint64_t hop = best_path->hops.at(j);
                    uint32_t link = GET_HOP_AS_ING(hop);
                    if (number_of_paths_per_link_selected_paths.find(link) == number_of_paths_per_link_selected_paths.end()) {
                        number_of_paths_per_link_selected_paths.insert(std::make_pair(link, 0));
                    }
                    number_of_paths_per_link_selected_paths.at(link)++;
                }
            }
        }

        NS_LOG_DEBUG("TimeSrv at " << isd_number << ":" << as_number << " FINISHED constructing disjoint paths");
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
            if (isd_as == ia_addr) {
                continue;
            }

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
                if (exp_time > local_time.GetMinutes()) {
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

            SCIONPacket* packet = create_scion_packet(payload, payload_type, GET_HOP_IA(path->hops.back()), 2, set_of_all_core_ases.size() * 8);

            packet->path.push_back(path);

            send_scion_packet(packet);
        }
    }

    void TimeServer::AdvanceLocalTime() {
        Time advance = Simulator::Now() - real_time_of_last_local_time_update;

        Time max_drift = get_max_drift(advance);
        std::random_device rd;
        std::uniform_int_distribution<int64_t> dist (-std::abs(max_drift.GetPicoSeconds()), std::abs(max_drift.GetPicoSeconds()));
        int64_t random_drift_int = dist(rd); //std::abs(max_drift.GetPicoSeconds());

        //local_time += advance + PicoSeconds(random_drift_int);
        if (random_drift_int < 0) {
            local_time -= PicoSeconds(std::abs(random_drift_int));
        } else {
            local_time += PicoSeconds(std::abs(random_drift_int));
        }

        real_time_of_last_local_time_update = Simulator::Now();
    }

    Time TimeServer::get_reference_time() {
        return Simulator::Now() - NanoSeconds(10);
    }

    Time TimeServer::get_max_drift(Time duration) {
        return max_drift_per_day * (duration.GetPicoSeconds() / Days(1).GetPicoSeconds());
    }

    void TimeServer::trigger_core_time_sync_algo(ia_t  printer_ia) {
        AdvanceLocalTime();

        if (ia_addr == printer_ia) {
            if (synchronization_round == 0) {
                std::cout << "##################################### Global Time Sync at " << Simulator::Now().GetPicoSeconds() << "##################################" << std::endl;
            } else {
                std::cout << "##################################### Local Time Sync at " << Simulator::Now().GetPicoSeconds() << "##################################" << std::endl;
            }
        }

        loff = get_reference_time().GetPicoSeconds() - local_time.GetPicoSeconds();

        if (synchronization_round == 0) {
            std::cout << "AS " << isd_number << "-" << as_number << ": " << local_time << std::endl;
            send_ntp_req_to_peers();
            Simulator::Schedule(Seconds(60), &TimeServer::continue_global_time_sync, this);
        } else {
            correct_local_time(loff);
        }

        synchronization_round = (synchronization_round + 1) % G;
    }

    void TimeServer::continue_global_time_sync() {
        AdvanceLocalTime();

        int32_t N = set_of_all_core_ases.size();
        int32_t F = std::floor((N - 1) / 3);
        int64_t corr = loff;

        std::multiset<int64_t> off;
        off.insert(loff);

        for (auto const & peer_ia : set_of_all_core_ases) {
            if (peer_ia == ia_addr) {
                continue;
            }

            if (poff.find(peer_ia) == poff.end()) {
                off.insert(get_reference_time().GetPicoSeconds() - local_time.GetPicoSeconds());
            } else {
                int64_t median_off = (int64_t) std::round(GetMedian(poff.at(peer_ia)));
                off.insert(median_off);
            }
        }

        auto iter1 = off.cbegin();
        auto iter2 = off.cbegin();
        std::advance(iter1, F);
        std::advance(iter2, N - 1 - F);

        int64_t goff = std::floor((*iter1 + *iter2) / 2);
        int64_t doff = loff - goff;

        if (std::abs(doff) > std::abs(global_cut_off.GetPicoSeconds())) {
            doff = doff > 0 ? std::abs(global_cut_off.GetPicoSeconds()) : -std::abs(global_cut_off.GetPicoSeconds());
            corr = goff + doff;
        }

        correct_local_time(corr);

        poff.clear();
    }

    void TimeServer::correct_local_time (int64_t corr) {
        Time max_drift = get_max_drift(time_sync_period);

        int64_t final_corr_abs = std::abs(corr) < std::abs(max_drift.GetPicoSeconds()) ? std::abs(corr) : std::abs(max_drift.GetPicoSeconds());

        if (corr > 0) {
            local_time += PicoSeconds(final_corr_abs);
        } else {
            local_time -= PicoSeconds(final_corr_abs);
        }
    }

    void TimeServer::send_ntp_req_to_peers() {
        for (auto const & peer_ia : set_of_all_core_ases) {
            if (peer_ia == ia_addr) {
                continue;
            }

            for (auto const & path_seg : set_of_most_disjoint_paths.at(peer_ia)) {
                NS_LOG_DEBUG("TimeSrv at " << isd_number << ":" << as_number << " sent ntp req to " << GET_ISDN(peer_ia) << ":" << GET_ASN(peer_ia));
                payload_type_t payload_type = payload_type_t::NTP_REQ;

                Payload payload;
                payload.ntp_req_or_resp.t0 = local_time.GetPicoSeconds();

                SCIONPacket* packet = create_scion_packet(payload, payload_type, peer_ia, 2, 8 + 48 /* udp + ntp*/);

                packet->path.push_back(path_seg);

                send_scion_packet(packet);
            }
        }
    }

    void TimeServer::receive_ntp_req_from_peer(SCIONPacket *packet, Time receive_time) {
        packet->payload_type = payload_type_t::NTP_RESP;
        packet->payload.ntp_req_or_resp.t1 = receive_time.GetPicoSeconds();
        packet->payload.ntp_req_or_resp.t2 = local_time.GetPicoSeconds();
        return_scion_packet(packet);
    }

    void TimeServer::receive_ntp_res_from_peer(SCIONPacket* packet, Time receive_time) {
        NS_LOG_DEBUG("TimeServ at " << isd_number << ":" << as_number <<
        " RCV NTP resp from peer " << GET_ISDN(packet->src_ia) << ":" << GET_ASN(packet->src_ia));

        int64_t poff_tmp = std::abs(
                            ((packet->payload.ntp_req_or_resp.t1 - packet->payload.ntp_req_or_resp.t0) +
                            (packet->payload.ntp_req_or_resp.t2 - receive_time.GetPicoSeconds())) / 2);

        if (poff.find(packet->src_ia) == poff.end()) {
            poff.insert(std::make_pair(packet->src_ia, std::multiset<int64_t>()));
        }

        poff.at(packet->src_ia).insert(poff_tmp);

    }




    void TimeServer::ScheduleListOfAllASesRequest() {
        for (Time t = first_event; t < last_event; t += list_of_ases_req_period) {
            //process_scheduler->Schedule(t, &TimeServer::request_set_of_all_core_ases_from_path_server, this);
            Simulator::Schedule(t, &TimeServer::request_set_of_all_core_ases_from_path_server, this);
        }
    }

    void TimeServer::ScheduleTimeSync(ia_t printer_ia) {
        for (Time t = first_event + Seconds(1); t < last_event + Seconds(1); t += time_sync_period) {
            //process_scheduler->Schedule(t, &TimeServer::trigger_core_time_sync_algo, this, printer_ia);
            Simulator::Schedule(t, &TimeServer::trigger_core_time_sync_algo, this, printer_ia);
        }
    }
}
