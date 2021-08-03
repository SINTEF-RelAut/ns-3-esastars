//
// Created by seyedali on 29.07.21.
//




#include "ns3/core-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/network-module.h"

#include "src/SCION/headers/scion_capable_node.h"
#include "src/SCION/headers/scion_as.h"

namespace ns3 {
    NS_LOG_COMPONENT_DEFINE("SCIONCapableDevice");
    void SCIONCapableNode::ScheduleReceive(uint16_t local_if, SCIONPacket* packet, Time propagation_delay) {
        bool in_the_same_as = std::get<2>(remote_nodes_info.at(local_if));
        if (in_the_same_as) {
            receive_scheduler_local_as->Schedule(propagation_delay, &SCIONCapableNode::receive, this, local_if, packet);
        } else {
            receive_scheduler_remote_as->Schedule(propagation_delay, &SCIONCapableNode::receive, this, local_if, packet);
        }
    }

    void SCIONCapableNode::receive (uint16_t local_if, SCIONPacket* packet) {
        processing_queue_length++;
        Time delay = processing_throughput_delay * processing_queue_length + processing_delay;
        process_scheduler->Schedule(delay, &SCIONCapableNode::process_received_packet, this, local_if, packet);
    }

    void SCIONCapableNode::process_received_packet(uint16_t local_if, SCIONPacket* packet) {
        processing_queue_length--;
        // Other tasks should be done in derived classes
    }

    void SCIONCapableNode::schedule_for_send(uint16_t local_if, SCIONPacket* packet) {
        NS_LOG_DEBUG(packet);
        transmission_queues_lengths.at(local_if) += packet->size;
        Time delay = transmission_delays.at(local_if) * transmission_queues_lengths.at(local_if) ;
        send_scheduler->Schedule(delay, &SCIONCapableNode::send,this, local_if, packet);

    }

    void SCIONCapableNode::send (uint16_t local_if, SCIONPacket* packet) {
        NS_LOG_DEBUG(packet);
        transmission_queues_lengths.at(local_if) -= packet->size;
        Ptr<SCIONCapableNode> remote_node = std::get<0>(remote_nodes_info.at(local_if));
        uint16_t remote_if = std::get<1>(remote_nodes_info.at(local_if));
        remote_node->ScheduleReceive(remote_if, packet, propagation_delays.at(local_if));
    }


    void SCIONCapableNode::AddToIFForwadingTable(uint16_t as_if, uint16_t local_if) {
        forwarding_table_to_other_AS_ifaces.insert(std::make_pair(as_if, local_if));
    }

    void SCIONCapableNode::AddToAddressForwardingTable(host_addr_t addr, uint16_t local_if) {
        forwarding_table_to_addresses_inside_as.insert(std::make_pair(addr, local_if));
    }

    host_addr_t SCIONCapableNode::GetLocalAddress () const {
        return local_address;
    }

    std::pair<LocalScheduler*, LocalScheduler*> SCIONCapableNode::GetReceiveSchedulers() {return std::make_pair(receive_scheduler_local_as, receive_scheduler_remote_as);}
    LocalScheduler* SCIONCapableNode::GetSendScheduler() {return send_scheduler;}
    LocalScheduler* SCIONCapableNode::GetProcessScheduler() {return process_scheduler;}

    double SCIONCapableNode::GetLatitude() const {return latitude;}
    double SCIONCapableNode::GetLogitude() const {return longitude;}

    void SCIONCapableNode::AddToPropagationDelays (Time delay) {propagation_delays.push_back(delay);}
    void SCIONCapableNode::AddToTransmissionDelays (Time delay) {transmission_delays.push_back(delay);}
    void SCIONCapableNode::SetProcessingDelay(Time delay, Time throughput_delay) {processing_delay = delay; processing_throughput_delay = throughput_delay;}

    void SCIONCapableNode::AddToRemoteNodesInfo (Ptr<SCIONCapableNode> remote_node, uint16_t remote_if, uint16_t remote_isd, uint16_t remote_as) {
        if (remote_isd == isd_number && remote_as == as_number) {
            remote_nodes_info.push_back(std::make_tuple(remote_node, remote_if, true));
        } else {
            remote_nodes_info.push_back(std::make_tuple(remote_node, remote_if, false));
        }
    }

    void SCIONCapableNode::InitializeTransmissionQueues() {
        transmission_queues_lengths.resize(GetNDevices());
    }

    void SCIONCapableNode::Drop(SCIONPacket* packet) {
        NS_ASSERT(packet == &on_the_flight_packets.at(packet->id));
        on_the_flight_packets.erase(packet->id);
    }

    void SCIONCapableNode::send_packet(SCIONPacket* packet) {
        NS_LOG_DEBUG("I am host " << isd_number << ":" << as_number << ":" << local_address << ". Packet sent to " << GET_ISDN(packet->dst_ia) << ":" << GET_ASN(packet->dst_ia) << ":" << packet->dst_host);

        uint16_t local_if_to_send;
        if (packet->dst_ia != ia_addr) {
            uint64_t hopf = packet->path.at(packet->curr_inf)->hops.at(packet->cur_hopf);
            NS_ASSERT(GET_HOP_ISD(hopf) == isd_number && GET_HOP_AS(hopf) == as_number);
            bool reverse = packet->path_reversed ^ packet->path.at(packet->curr_inf)->reverse;

            NS_LOG_DEBUG( reverse << " " << packet->path_reversed << " " << packet->path.at(packet->curr_inf)->reverse);

            uint16_t as_if_to_send;
            if (reverse) {
                as_if_to_send = GET_HOP_ING_IF(hopf);
            } else {
                as_if_to_send = GET_HOP_EG_IF(hopf);
            }

            NS_LOG_DEBUG(" first hop field: isd: " << GET_HOP_ISD(packet->path.at(packet->curr_inf)->hops.at(packet->cur_hopf))
            << ", as:" << GET_HOP_AS(packet->path.at(packet->curr_inf)->hops.at(packet->cur_hopf))
            << ", ing:" << GET_HOP_ING_IF(packet->path.at(packet->curr_inf)->hops.at(packet->cur_hopf))
            << ", eg:" << GET_HOP_EG_IF(packet->path.at(packet->curr_inf)->hops.at(packet->cur_hopf)));
            NS_LOG_DEBUG("as_if_to_send: " << as_if_to_send);

            local_if_to_send = forwarding_table_to_other_AS_ifaces.at(as_if_to_send);
        } else {
            local_if_to_send = forwarding_table_to_addresses_inside_as.at(packet->dst_host);
        }

        schedule_for_send(local_if_to_send, packet);
    }

    SCIONPacket* SCIONCapableNode::create_packet(Payload payload, payload_type_t payload_type, ia_t dst_ia, host_addr_t dst_host) {
        on_the_flight_packets.insert(std::make_pair(next_packet_id, SCIONPacket(next_packet_id,  Ptr<SCIONCapableNode>(this))));
        SCIONPacket* packet = &on_the_flight_packets.at(next_packet_id);
        next_packet_id++;

        packet->src_ia = ia_addr;
        packet->dst_ia = dst_ia;
        packet->src_host = local_address;
        packet->dst_host = dst_host;

        packet->path_reversed = false;
        packet->curr_inf = 0;
        packet->cur_hopf = 0;

        packet->payload_type = payload_type;
        packet->payload = payload;

        packet->timestamp = node->local_time;
        packet->size = 114;

        return packet;
    }

}