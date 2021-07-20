//
// Created by seyedali on 19.07.21.
//
#include <vector>
#include <src/SCION/headers/scion_core_as.h>
#include <cassert>
#include "src/SCION/headers/host.h"
#include "src/SCION/headers/scion_as.h"
#include "src/SCION/headers/path_server.h"


namespace ns3 {

    void Host::remove_expired_segments() {

    }

    void Host::search_in_cached_segments(ia_t dst_ia, std::vector<const PathSegment*>& the_path) {
        int16_t dst_in_which_cache = -1;

        if (cached_core_path_segments.find(dst_ia) != cached_core_path_segments.end()) {
            dst_in_which_cache = 0;
        } else if (cached_up_path_segments.find(dst_ia) != cached_up_path_segments.end()) {
            dst_in_which_cache = 1;
        } else if (cached_down_path_segments.find(dst_ia) != cached_down_path_segments.end()) {
            dst_in_which_cache = 2;
        }

        if (dst_in_which_cache == -1) {
            return;
        }

        if (dst_in_which_cache == 0 && DynamicCast<SCION_Core_AS>(node) != NULL
            && cached_core_path_segments.at(dst_ia)->find(node->ia_addr) != cached_core_path_segments.at(dst_ia)->end()) {
            the_path.push_back(cached_core_path_segments.at(dst_ia)->at(node->ia_addr)->begin()->second);
            return;
        }

        if (dst_in_which_cache == 0 && DynamicCast<SCION_Core_AS>(node) == NULL) {
            for (auto const & [core_seg_src_ia, core_path_segs] : *cached_core_path_segments.at(dst_ia)){
                if (cached_up_path_segments.find(core_seg_src_ia) != cached_up_path_segments.end()) {
                    assert(cached_up_path_segments.at(core_seg_src_ia)->find(node->ia_addr) != cached_up_path_segments.at(dst_ia)->end());

                    the_path.push_back(cached_up_path_segments.at(core_seg_src_ia)->at(node->ia_addr)->begin()->second);
                    the_path.push_back(core_path_segs->begin()->second);
                    return;
                }
            }
            return;
        }


        if (dst_in_which_cache == 1 && DynamicCast<SCION_Core_AS>(node) == NULL) {
            assert(cached_up_path_segments.at(dst_ia)->find(node->ia_addr) != cached_up_path_segments.at(dst_ia)->end());
            the_path.push_back(cached_up_path_segments.at(dst_ia)->at(node->ia_addr)->begin()->second);
            return;
        }

        if (dst_in_which_cache == 2 && DynamicCast<SCION_Core_AS>(node) != NULL){
            if (cached_down_path_segments.at(dst_ia)->find(node->ia_addr) != cached_down_path_segments.at(dst_ia)->end()) {
                the_path.push_back(cached_down_path_segments.find(dst_ia)->second->begin()->second->begin()->second);
                return;
            }

            for (auto const & [down_seg_src_ia, down_path_segs] : *cached_down_path_segments.at(dst_ia)){
                if (cached_core_path_segments.find(down_seg_src_ia) != cached_up_path_segments.end()) {
                    the_path.push_back(cached_core_path_segments.at(down_seg_src_ia)->begin()->second->begin()->second);
                    the_path.push_back(down_path_segs->begin()->second);
                    return;
                }
            }
            return;
        }

        if (dst_in_which_cache == 2 && DynamicCast<SCION_Core_AS>(node) == NULL) {
            for (auto const & [down_seg_src_ia, down_path_segs] : *cached_down_path_segments.at(dst_ia)){
                if (cached_core_path_segments.find(down_seg_src_ia) != cached_core_path_segments.end()) {
                    for (auto const & [core_seg_src_ia, core_path_segs] : *cached_core_path_segments.at(down_seg_src_ia)){
                        if (cached_up_path_segments.find(core_seg_src_ia) != cached_up_path_segments.end()) {
                            the_path.push_back(cached_up_path_segments.at(core_seg_src_ia)->at(node->ia_addr)->begin()->second);
                            the_path.push_back(cached_core_path_segments.at(down_seg_src_ia)->at(core_seg_src_ia)->begin()->second);
                            the_path.push_back(cached_down_path_segments.at(dst_ia)->at(down_seg_src_ia)->begin()->second);
                            return;
                        }
                    }
                }
            }
        }

        return;
    }

    void Host::SendArbitraryPacket(ia_t dst_ia, uint32_t host_address) {
        SCIONPacket packet;
        packet.payload = NULL;
        packet.src_ia = node->ia_addr;
        packet.dst_ia = dst_ia;
        packet.dst_host = host_address;
        packet.src_host = local_address;
        packet.path_reversed = false;

        try_sending(packet, 0);


    }

    void Host::try_sending(SCIONPacket packet, uint16_t count) {
        std::vector<const PathSegment*> path;
        search_in_cached_segments(packet.dst_ia, path);

        if (path.size() != 0) {
            packet.path = path;
            packet.timestamp = node->local_time;
            send_packet(packet);
        }

        if (path.size() == 0 && count == 0) {
            request_for_path_segments(packet.dst_ia);
        } else if (path.size() == 0 && count < 3)  {
            node->events.at(node->GetHostSchedulerIdx(local_address))->
            Schedule(MilliSeconds(300), &Host::try_sending, this, packet, (count + 1));
        }
    }

    void Host::send_packet(SCIONPacket packet) {

    }

    void Host::request_for_path_segments(ia_t dst_ia) {
        uint16_t dst_as = GET_ASN(dst_ia);
        uint16_t dst_isd = GET_ISDN(dst_ia);

        send_request_for_path_segments(path_segment_type::UP_SEG, node->ia_addr, 0);
        if (dst_isd == node->isd_number) {
            send_request_for_path_segments(path_segment_type::CORE_SEG, 0, 0);
            send_request_for_path_segments(path_segment_type::DOWN_SEG, 0, dst_ia);
        } else {
            send_request_for_path_segments(path_segment_type::CORE_SEG, 0, MAKE_IA(dst_isd, 0));
            send_request_for_path_segments(path_segment_type::DOWN_SEG, MAKE_IA(dst_isd, 0), dst_ia);
        }

    }


    void Host::cache_path_segment (path_segment_type path_type, ia_t src_ia, ia_t dst_ia, const PathSegment* path_seg){
        cached_path_segs_dataset_t* cached_path_segs_data_set;

        if (path_type == path_segment_type::CORE_SEG) {
            cached_path_segs_data_set = &cached_core_path_segments;
        } else if (path_type == path_segment_type::UP_SEG) {
            cached_path_segs_data_set = &cached_up_path_segments;
        } else {
            cached_path_segs_data_set = &cached_down_path_segments;
        }

        if (cached_path_segs_data_set->find(dst_ia) == cached_path_segs_data_set->end()) {
            cached_path_segs_data_set->insert(std::make_pair(dst_ia, new cached_path_segs_per_dst_t()));
        }

        if (cached_path_segs_data_set->at(dst_ia)->find(src_ia) == cached_path_segs_data_set->at(dst_ia)->end()) {
            cached_path_segs_data_set->at(dst_ia)->insert(std::make_pair(src_ia, new cached_path_segs_per_src_dst_t()));
        }

        if (cached_path_segs_data_set->at(dst_ia)->at(src_ia)->find(path_seg->expiration_time) == cached_path_segs_data_set->at(dst_ia)->at(src_ia)->end()){
            cached_path_segs_data_set->at(dst_ia)->at(src_ia)->insert(std::make_pair(path_seg->expiration_time, path_seg));
        }
    }

    void Host::ReceiveRegisteredPathSegments (path_segment_type path_type, ia_t src_ia, ia_t dst_ia, uint32_t req_id, const reg_path_segs_to_one_as_t& path_segments) {
        for (auto const & [key, path_segment] : path_segments) {
            if (path_segment->expiration_time > node->local_time.GetMinutes()) {
                cache_path_segment ( path_type,  src_ia, dst_ia,  path_segment);
            }
        }
    }


    void Host::ReceiveCachedPathSegments (path_segment_type path_type, ia_t src_ia, ia_t dst_ia, uint32_t req_id, const cached_path_segs_per_src_dst_t& path_seg) {

    }

    void Host::send_request_for_path_segments(path_segment_type path_type, ia_t src_ia, ia_t dst_ia) {
        unique_path_req_id++;

        node->events.at(node->GetPathServerSchedulerIdx())
        ->Schedule(node->latencies_between_hosts_and_path_server.at(local_address),
                   &ns3::PathServer::ReceiveRequestForPathSegmentFromHost,
                   node->GetPathServer(),
                   path_type, src_ia, dst_ia, local_address, unique_path_req_id);
    }
}