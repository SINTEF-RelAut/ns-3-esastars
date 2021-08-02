//
// Created by seyedali on 28.07.21.
//

#include <cassert>

#include "src/SCION/headers/scion_as.h"
#include "src/SCION/headers/border_router.h"
#include "src/SCION/headers/scion_packet.h"

namespace ns3 {
    void BorderRouter::process_received_packet(uint16_t if_rcv, SCIONPacket& packet) {
        SCIONCapableNode::process_received_packet(if_rcv, packet);

        if (packet.src_ia == packet.dst_ia) {
            return;
        }

        if (packet.dst_ia == ia_addr) {
            uint64_t hopf = packet.path.at(packet.curr_inf)->hops.at(packet.cur_hopf);
            assert(GET_HOP_ISD(hopf) == isd_number && GET_HOP_AS(hopf) == as_number);

            if (forwarding_table_to_addresses_inside_as.find(packet.dst_host) == forwarding_table_to_addresses_inside_as.end()) {
                return;
            }

            uint16_t local_if_to_send = forwarding_table_to_addresses_inside_as.at(packet.dst_host);
            schedule_for_send(local_if_to_send, packet);

            return;
        }

        auto const & [remote_node, remote_if, received_from_local_as] = remote_nodes_info.at(if_rcv);

        if (!received_from_local_as) {
            if (packet.path_reversed && packet.cur_hopf == 0) {
                packet.curr_inf--;
                packet.cur_hopf = packet.path.at(packet.curr_inf)->hops.size() - 1;
            } else if (!packet.path_reversed && packet.cur_hopf == packet.path.at(packet.curr_inf)->hops.size() - 1) {
                packet.curr_inf++;
                packet.cur_hopf = 0;
            } else if (packet.shortcut_hopfs.size() == 2 && packet.path.size() == 2) {
                if (packet.shortcut_hopfs.at(packet.curr_inf) == packet.cur_hopf) {
                    if (packet.path_reversed) {
                        packet.curr_inf--;
                    } else {
                        packet.curr_inf++;
                    }
                }
                packet.cur_hopf = packet.shortcut_hopfs.at(packet.curr_inf);
            }
        }

        assert(packet.curr_inf > 0 && packet.curr_inf < packet.path.size());

        uint64_t hopf = packet.path.at(packet.curr_inf)->hops.at(packet.cur_hopf);
        assert(GET_HOP_ISD(hopf) == isd_number && GET_HOP_AS(hopf) == as_number);
        bool reverse = packet.path_reversed ^ packet.path.at(packet.curr_inf)->reverse;

        uint16_t as_if_to_send;
        if (reverse) {
            as_if_to_send = GET_HOP_ING_IF(hopf);
        } else {
            as_if_to_send = GET_HOP_EG_IF(hopf);
        }

        if (received_from_local_as) {
            if (packet.path_reversed) {
                packet.cur_hopf--;
            } else {
                packet.cur_hopf++;
            }
        }


        assert(packet.cur_hopf > 0 && packet.cur_hopf < packet.path.at(packet.curr_inf)->hops.size());

        uint16_t local_if_to_send = forwarding_table_to_other_AS_ifaces.at(as_if_to_send);
        schedule_for_send(local_if_to_send, packet);
    }


}