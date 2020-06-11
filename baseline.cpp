//
// Created by chrissy on 10.06.20.
//

#include "baseline.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/point-to-point-channel.h"

void Baseline::InitiateBeacons(std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces, SCION_Node *node){
    for (auto const& [remote_as_no, interfaces]: valid_interfaces){
        for (auto const & self_egress_if_no : interfaces) {
            ns3::Ptr<ns3::PointToPointNetDevice> self_egress_device = DynamicCast<ns3::PointToPointNetDevice>(
                    node->GetDevice(self_egress_if_no));
            ns3::Ptr<ns3::PointToPointChannel> channel = DynamicCast<ns3::PointToPointChannel>(
                    self_egress_device->GetChannel());
            uint32_t wire = self_egress_device == channel->GetSource(0) ? 0 : 1;
            ns3::Ptr<ns3::PointToPointNetDevice> remote_device = channel->GetDestination(wire);

            uint16_t remote_if_no = (uint16_t) remote_device->GetIfIndex();

            ns3::Ptr<SCION_Node> remote_as = (DynamicCast<SCION_Node>(remote_device->GetNode()));

            GenerateBeaconAndSend(NULL, self_egress_if_no, remote_as_no, remote_if_no, remote_as,
                                  0.0, node->inter_as_bwds.at(self_egress_if_no), false, 0.0);
        }

    }
}

