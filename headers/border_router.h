//
// Created by seyedali on 28.07.21.
//

#ifndef SCION_SIMULATOR_BORDER_ROUTER_H
#define SCION_SIMULATOR_BORDER_ROUTER_H

#include "ns3/network-module.h"
#include "ns3/node.h"
#include "ns3/nstime.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"

#include "src/SCION/headers/scion_capable_node.h"

namespace ns3 {
class BorderRouter : public ScionCapableNode
{
public:
  BorderRouter (uint32_t system_id, uint16_t isd_number, uint16_t as_number, uint32_t local_address,
                double latitude, double longitude, ScionAs *AS)
      : ScionCapableNode (system_id, isd_number, as_number, local_address, latitude, longitude, AS)
  {
  }

private:
  void ProcessReceivedPacket (uint16_t local_if, ScionPacket *packet, Time receive_time) override;
};
} // namespace ns3
#endif //SCION_SIMULATOR_BORDER_ROUTER_H
