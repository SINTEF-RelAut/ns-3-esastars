//
// Created by seyedali on 19.07.21.
//
#include <vector>

#include "ns3/ptr.h"

#include "src/SCION/headers/path_segment.h"
#include "src/SCION/headers/path_server.h"
#include "src/SCION/headers/scion_core_as.h"
#include "src/SCION/headers/scion_host.h"

namespace ns3 {
    NS_LOG_COMPONENT_DEFINE("SCIONHost");

    void SCIONHost::receive_registered_path_segments(path_segment_type seg_type, ia_t src_ia, ia_t dst_ia,
                                                     const reg_path_segs_to_one_as_t *path_segments) {
        NS_LOG_FUNCTION("I am host " << isd_number << ":" << as_number << ":" << local_address
                                     << ". Registered paths fetched: from " << src_ia << " " << GET_ISDN(src_ia) << ":"
                                     << GET_ASN(src_ia) << " to " << GET_ISDN(dst_ia) << ":" << GET_ASN(dst_ia)
                                     << " number of segments: " << path_segments->size());

        for (auto const &key_path_segment_pair : *path_segments) {
            PathSegment *path_segment = key_path_segment_pair.second;
            if (path_segment->expiration_time > local_time.GetMinutes()) {
                cache_path_segment(seg_type, src_ia, dst_ia, path_segment);
            }
        }
    }

    void SCIONHost::receive_cached_path_segments(path_segment_type seg_type, ia_t src_ia, ia_t dst_ia,
                                                 cached_path_segs_per_dst_t *path_seg) {}

    void SCIONHost::remove_expired_segments() {}

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

    void SCIONHost::cache_path_segment(path_segment_type seg_type, ia_t src_ia, ia_t dst_ia, PathSegment *path_seg) {
        cached_path_segs_dataset_t *cached_path_segs_data_set;

        if (seg_type == path_segment_type::CORE_SEG) {
            cached_path_segs_data_set = &cached_core_path_segments;
        } else if (seg_type == path_segment_type::UP_SEG) {
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

        cached_path_segs_data_set->at(dst_ia)->at(src_ia)->insert(std::make_pair(path_seg->hops.size(), path_seg));
    }

    void SCIONHost::send_request_for_path_segments(path_segment_type seg_type, ia_t src_ia, ia_t dst_ia) {
        payload_type_t payload_type = payload_type_t::PATH_REQ_FROM_HOST;

        Payload payload;
        payload.path_req_from_host.src_ia = src_ia;
        payload.path_req_from_host.dst_ia = dst_ia;
        payload.path_req_from_host.seg_type = seg_type;

        SCIONPacket *packet = create_scion_packet(payload, payload_type, ia_addr, 1, 0);
        send_scion_packet(packet);
    }

    void SCIONHost::search_in_cached_segments(ia_t dst_ia, std::vector<const PathSegment *> &the_path,
                                              std::vector<uint8_t> &shortcuts) {
        if (dst_ia == ia_addr) {
            return;
        }

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

        if (dst_in_which_cache == 0 && dynamic_cast<SCION_Core_AS *>(AS) != NULL &&
            cached_core_path_segments.at(dst_ia)->find(ia_addr) != cached_core_path_segments.at(dst_ia)->end()) {
            the_path.push_back(cached_core_path_segments.at(dst_ia)->at(ia_addr)->begin()->second);
            return;
        }

        if (dst_in_which_cache == 0 && dynamic_cast<SCION_Core_AS *>(AS) == NULL) {
            for (auto const &[core_seg_src_ia, core_path_segs] : *cached_core_path_segments.at(dst_ia)) {
                if (cached_up_path_segments.find(core_seg_src_ia) != cached_up_path_segments.end()) {
                    NS_ASSERT(cached_up_path_segments.at(core_seg_src_ia)->find(ia_addr) !=
                              cached_up_path_segments.at(dst_ia)->end());

                    the_path.push_back(cached_up_path_segments.at(core_seg_src_ia)->at(ia_addr)->begin()->second);
                    the_path.push_back(core_path_segs->begin()->second);

                    return;
                }
            }
            return;
        }

        if (dst_in_which_cache == 1 && dynamic_cast<SCION_Core_AS *>(AS) == NULL) {
            NS_ASSERT(cached_up_path_segments.at(dst_ia)->find(ia_addr) != cached_up_path_segments.at(dst_ia)->end());
            the_path.push_back(cached_up_path_segments.at(dst_ia)->at(ia_addr)->begin()->second);
            return;
        }

        if (dst_in_which_cache == 2 && dynamic_cast<SCION_Core_AS *>(AS) != NULL) {
            if (cached_down_path_segments.at(dst_ia)->find(ia_addr) != cached_down_path_segments.at(dst_ia)->end()) {
                the_path.push_back(cached_down_path_segments.find(dst_ia)->second->begin()->second->begin()->second);
                return;
            }

            for (auto const &[down_seg_src_ia, down_path_segs] : *cached_down_path_segments.at(dst_ia)) {
                if (cached_core_path_segments.find(down_seg_src_ia) != cached_up_path_segments.end()) {
                    the_path.push_back(cached_core_path_segments.at(down_seg_src_ia)->begin()->second->begin()->second);
                    the_path.push_back(down_path_segs->begin()->second);

                    return;
                }
            }
            return;
        }

        if (dst_in_which_cache == 2 && dynamic_cast<SCION_Core_AS *>(AS) == NULL) {
            for (auto const &[down_seg_src_ia, down_path_segs] : *cached_down_path_segments.at(dst_ia)) {
                if (cached_core_path_segments.find(down_seg_src_ia) != cached_core_path_segments.end()) {
                    for (auto const &[core_seg_src_ia, core_path_segs] :
                         *cached_core_path_segments.at(down_seg_src_ia)) {
                        if (cached_up_path_segments.find(core_seg_src_ia) != cached_up_path_segments.end()) {
                            the_path.push_back(
                                    cached_up_path_segments.at(core_seg_src_ia)->at(ia_addr)->begin()->second);
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

    void SCIONHost::process_received_packet(uint16_t local_if, SCIONPacket *packet, Time receive_time) {
        NS_ASSERT(packet->dst_ia == ia_addr && packet->dst_host == local_address);
        NS_LOG_FUNCTION("I am host " << isd_number << ":" << as_number << ":" << local_address
                                     << ". Packet received from " << GET_ISDN(packet->src_ia) << ":"
                                     << GET_ASN(packet->src_ia) << ":" << packet->src_host);

        SCIONCapableNode::process_received_packet(local_if, packet, receive_time);

        if (packet->payload_type == payload_type_t::REG_PATHS_FROM_LOCAL_PS) {
            RegPathsFromLocalPS registered_paths_from_local_ps = packet->payload.registered_paths_from_local_ps;
            receive_registered_path_segments(
                    registered_paths_from_local_ps.seg_type, registered_paths_from_local_ps.src_ia,
                    registered_paths_from_local_ps.dst_ia, registered_paths_from_local_ps.registered_path_segments);
            packet->packet_originator->DestroySCIONPacket(packet);
            return;
        }
        /*
        if (packet->packet_originator == this) {
            NS_ASSERT(on_the_flight_packets.find(packet->id) != on_the_flight_packets.end());
            NS_ASSERT(&on_the_flight_packets.at(packet->id) == packet);
            NS_LOG_FUNCTION("I am host " << isd_number << ":" << as_number << ":" << local_address << ". Response received from " << GET_ISDN(packet->src_ia) << ":" << GET_ASN(packet->src_ia) << ":" << packet->src_host);

            DestroySCIONPacket(packet);
            // The repose of a  previously-sent message has received; do whatever is necessary
        } else {
            return_scion_packet(packet);
        }
*/
    }

    void SCIONHost::SendArbitraryPacket(ia_t dst_ia, host_addr_t dst_host) {
        Payload payload;
        payload_type_t payload_type = payload_type_t::EMPTY;

        if (dst_ia == ia_addr) {
            SCIONPacket *packet = create_scion_packet(payload, payload_type, dst_ia, dst_host, 0);
            send_scion_packet(packet);
        } else {
            std::vector<const PathSegment *> the_path;
            std::vector<uint8_t> shortcuts;

            search_in_cached_segments(dst_ia, the_path, shortcuts);

            if (the_path.size() != 0) {
                SCIONPacket *packet =
                        create_scion_packet(payload, payload_type, dst_ia, dst_host, 0, the_path, shortcuts);
                send_scion_packet(packet);
            } else {
                request_for_path_segments(dst_ia);
                Simulator::Schedule(MilliSeconds(300), &SCIONHost::SendArbitraryPacket, this, dst_ia, dst_host);
            }
        }
    }

    void SCIONHost::modify_pkt_upon_send(SCIONPacket *packet) {}
} // namespace ns3
