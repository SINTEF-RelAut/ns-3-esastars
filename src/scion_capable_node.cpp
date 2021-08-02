//
// Created by seyedali on 29.07.21.
//




#include "ns3/core-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/network-module.h"

#include "src/SCION/headers/scion_capable_node.h"

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

    std::pair<uint16_t, Ptr<SCIONCapableNode>> SCIONCapableNode::get_remote_node(uint16_t local_if) {
        Ptr<PointToPointNetDevice> local_device = DynamicCast<PointToPointNetDevice>(GetDevice(local_if));

        Ptr<PointToPointChannel> channel = DynamicCast<PointToPointChannel>(local_device->GetChannel());
        uint32_t wire = local_device == channel->GetSource(0) ? 0 : 1;
        Ptr<PointToPointNetDevice> remote_device = channel->GetDestination(wire);

        uint16_t remote_if = (uint16_t) remote_device->GetIfIndex();

        Ptr<SCIONCapableNode> remote_node = (DynamicCast<SCIONCapableNode>(remote_device->GetNode()));

        return std::make_pair(remote_if, remote_node);
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

}