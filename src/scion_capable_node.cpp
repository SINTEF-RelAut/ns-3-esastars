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
    void SCIONCapableNode::ScheduleReceive(uint16_t local_if, SCIONPacket& packet, Time propagation_delay) {
        Time delay = propagation_delay;
        receive_scheduler->Schedule(delay, &SCIONCapableNode::receive, this, local_if, packet);
    }

    void SCIONCapableNode::receive (uint16_t local_if, SCIONPacket& packet) {
        Time delay = process_scheduler->GetFirstAvailableSlotAssumingThroughput(processing_delay);
        process_scheduler->Schedule(delay, &SCIONCapableNode::process_received_packet, this, local_if, packet);
    }

    void SCIONCapableNode::schedule_for_send(uint16_t local_if, SCIONPacket& packet) {
        Time delay = send_scheduler->GetFirstAvailableSlotAssumingThroughput(transmission_delays.at(local_if) * packet.size * 8);
        send_scheduler->Schedule(delay, &SCIONCapableNode::send,this, local_if, packet);
    }

    void SCIONCapableNode::send (uint16_t local_if, SCIONPacket& packet) {
        std::pair<uint16_t, Ptr<SCIONCapableNode>> remote_if_node_pair = get_remote_node(local_if);
        remote_if_node_pair.second->ScheduleReceive(remote_if_node_pair.first, packet, propagation_delays.at(local_if));
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

    LocalScheduler* SCIONCapableNode::GetReceiveScheduler() {return receive_scheduler;}
    LocalScheduler* SCIONCapableNode::GetSendScheduler() {return send_scheduler;}
    LocalScheduler* SCIONCapableNode::GetProcessScheduler() {return process_scheduler;}

    double SCIONCapableNode::GetLatitude() const {return latitude;}
    double SCIONCapableNode::GetLogitude() const {return longitude;}

    void SCIONCapableNode::AddToPropagationDelays (Time delay) {propagation_delays.push_back(delay);}
    void SCIONCapableNode::AddToTransmissionDelays (Time delay) {transmission_delays.push_back(delay);}
    void SCIONCapableNode::SetProcessingDelay(Time delay) {processing_delay = delay;}

}