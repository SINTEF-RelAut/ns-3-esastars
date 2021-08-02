//
// Created by seyedali on 19.07.21.
//
#include <vector>
#include <cassert>

#include "ns3/ptr.h"

#include "src/SCION/headers/scion_core_as.h"
#include "src/SCION/headers/scion_host.h"
#include "src/SCION/headers/path_segment.h"
#include "src/SCION/headers/path_server.h"

namespace ns3 {
    NS_LOG_COMPONENT_DEFINE("SCIONHost");
    void SCIONHost::ReceiveRegisteredPathSegments (path_segment_type path_type, ia_t src_ia, ia_t dst_ia, reg_path_segs_to_one_as_t* path_segments) {
        NS_LOG_DEBUG(src_ia << " " << GET_ISDN(src_ia) << ":" << GET_ASN(src_ia) << " " << GET_ISDN(dst_ia) << ":" << GET_ASN(dst_ia) << " number of segments: " << path_segments->size());
        for (auto const & key_path_segment_pair : *path_segments) {
            PathSegment* path_segment = key_path_segment_pair.second;
            if (path_segment->expiration_time > node->local_time.GetMinutes()) {
                cache_path_segment ( path_type,  src_ia, dst_ia,  path_segment);
            }
        }
    }

    void SCIONHost::ReceiveCachedPathSegments (path_segment_type path_type, ia_t src_ia, ia_t dst_ia, cached_path_segs_per_dst_t* path_seg) {

    }

    void SCIONHost::remove_expired_segments() {

    }

    void SCIONHost::request_for_path_segments(ia_t dst_ia) {
        uint16_t dst_isd = GET_ISDN(dst_ia);

        send_request_for_path_segments(path_segment_type::UP_SEG, ia_addr, 0);
        if (dst_isd == isd_number) {
            send_request_for_path_segments(path_segment_type::CORE_SEG, 0, 0);
            send_request_for_path_segments(path_segment_type::DOWN_SEG, 0, dst_ia);
        } else {
            send_request_for_path_segments(path_segment_type::CORE_SEG, 0, MAKE_IA(dst_isd, 0));
            send_request_for_path_segments(path_segment_type::DOWN_SEG, MAKE_IA(dst_isd, 0), dst_ia);
        }
    }

    void SCIONHost::cache_path_segment (path_segment_type path_type, ia_t src_ia, ia_t dst_ia, PathSegment* path_seg){
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


    void SCIONHost::send_request_for_path_segments(path_segment_type path_type, ia_t src_ia, ia_t dst_ia) {
        node->events.at(node->GetPathServerSchedulerIdx())
        ->Schedule(node->latencies_between_hosts_and_path_server.at(local_address),
                   &PathServer::ReceiveRequestForPathSegmentFromHost,
                   node->GetPathServer(),
                   path_type, src_ia, dst_ia, local_address);
    }


    void SCIONHost::search_in_cached_segments(ia_t dst_ia, std::vector<const PathSegment*>& the_path, std::vector<uint8_t>& shortcuts) {
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
        && cached_core_path_segments.at(dst_ia)->find(ia_addr) != cached_core_path_segments.at(dst_ia)->end()) {
            the_path.push_back(cached_core_path_segments.at(dst_ia)->at(ia_addr)->begin()->second);
            return;
        }

        if (dst_in_which_cache == 0 && DynamicCast<SCION_Core_AS>(node) == NULL) {
            for (auto const & [core_seg_src_ia, core_path_segs] : *cached_core_path_segments.at(dst_ia)){
                if (cached_up_path_segments.find(core_seg_src_ia) != cached_up_path_segments.end()) {
                    assert(cached_up_path_segments.at(core_seg_src_ia)->find(ia_addr) != cached_up_path_segments.at(dst_ia)->end());

                    the_path.push_back(cached_up_path_segments.at(core_seg_src_ia)->at(ia_addr)->begin()->second);
                    the_path.push_back(core_path_segs->begin()->second);

                    return;
                }
            }
            return;
        }


        if (dst_in_which_cache == 1 && DynamicCast<SCION_Core_AS>(node) == NULL) {
            assert(cached_up_path_segments.at(dst_ia)->find(ia_addr) != cached_up_path_segments.at(dst_ia)->end());
            the_path.push_back(cached_up_path_segments.at(dst_ia)->at(ia_addr)->begin()->second);
            return;
        }

        if (dst_in_which_cache == 2 && DynamicCast<SCION_Core_AS>(node) != NULL){
            if (cached_down_path_segments.at(dst_ia)->find(ia_addr) != cached_down_path_segments.at(dst_ia)->end()) {
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
                            the_path.push_back(cached_up_path_segments.at(core_seg_src_ia)->at(ia_addr)->begin()->second);
                            the_path.push_back(core_path_segs->begin()->second);
                            the_path.push_back(down_path_segs->begin()->second);

                            return;
                        }
                    }
                }
            }
        }

        return;
    }

    void SCIONHost::process_received_packet(uint16_t local_if, SCIONPacket& packet) {
        NS_LOG_DEBUG("Message Received");
        SCIONCapableNode::process_received_packet(local_if, packet);
        if (on_the_flight_packets.find(packet.id) != on_the_flight_packets.end() && &packet == &on_the_flight_packets.at(packet.id)) {
            NS_LOG_DEBUG("Response Received");
            on_the_flight_packets.erase(packet.id);
            // The repose of a  previously-sent message has received; do whatever is necessary
        } else {
            packet.dst_host = packet.src_host;
            packet.dst_ia = packet.src_ia;
            packet.src_ia = ia_addr;
            packet.src_host = local_address;

            packet.path_reversed = !packet.path_reversed;
            packet.timestamp = node->local_time;
            send_packet(packet);
        }
    }

    void SCIONHost::SendArbitraryPacket(ia_t dst_ia, host_addr_t host_address) {
        SCIONPacket packet;
        packet.payload = NULL;
        packet.src_ia = ia_addr;
        packet.dst_ia = dst_ia;
        packet.dst_host = host_address;
        packet.src_host = local_address;
        packet.path_reversed = false;

        try_sending(packet, 0);
    }

    void SCIONHost::try_sending(SCIONPacket packet, uint16_t count) {
        std::vector<const PathSegment*> path;
        std::vector<uint8_t> shortcuts;
        search_in_cached_segments(packet.dst_ia, path, shortcuts);

        if (path.size() != 0) {
            packet.path = path;
            packet.shortcut_hopfs = shortcuts;
            packet.curr_inf = 0;
            packet.cur_hopf = 0;
            packet.timestamp = node->local_time;
            packet.size = 114;

            on_the_flight_packets.insert(std::make_pair(next_packet_id, packet));
            on_the_flight_packets.at(next_packet_id).id = next_packet_id;
            send_packet(on_the_flight_packets.at(next_packet_id));
            next_packet_id++;
        }

        if (path.size() == 0 && count == 0) {
            request_for_path_segments(packet.dst_ia);
        }

        if (path.size() == 0 && count < 3)  {
            node->events.at(node->GetHostSchedulerIdx(local_address))->
            Schedule(MilliSeconds(300), &SCIONHost::try_sending, this, packet, (count + 1));
        }
    }

    void SCIONHost::send_packet(SCIONPacket& packet) {
        NS_LOG_DEBUG("packet sent " << &packet << " " << &on_the_flight_packets.at(packet.id));

        uint64_t hopf = packet.path.at(packet.curr_inf)->hops.at(packet.cur_hopf);
        assert(GET_HOP_ISD(hopf) == isd_number && GET_HOP_AS(hopf) == as_number);
        bool reverse = packet.path_reversed ^ packet.path.at(packet.curr_inf)->reverse;

        NS_LOG_DEBUG( reverse << " " << packet.path_reversed << " " << packet.path.at(packet.curr_inf)->reverse);

        uint16_t as_if_to_send;
        if (reverse) {
            as_if_to_send = GET_HOP_ING_IF(hopf);
        } else {
            as_if_to_send = GET_HOP_EG_IF(hopf);
        }

        NS_LOG_DEBUG(" first hop field: isd: " << GET_HOP_ISD(packet.path.at(packet.curr_inf)->hops.at(packet.cur_hopf))
                        << ", as:" << GET_HOP_AS(packet.path.at(packet.curr_inf)->hops.at(packet.cur_hopf))
                        << ", ing:" << GET_HOP_ING_IF(packet.path.at(packet.curr_inf)->hops.at(packet.cur_hopf))
                        << ", eg:" << GET_HOP_EG_IF(packet.path.at(packet.curr_inf)->hops.at(packet.cur_hopf)));
        NS_LOG_DEBUG("as_if_to_send: " << as_if_to_send);

        uint16_t local_if_to_send = forwarding_table_to_other_AS_ifaces.at(as_if_to_send);
        NS_LOG_DEBUG("border router index: " << DynamicCast<BorderRouter>(std::get<0>(remote_nodes_info.at(local_if_to_send)))->GetIndex());
        schedule_for_send(local_if_to_send, packet);
    }


}