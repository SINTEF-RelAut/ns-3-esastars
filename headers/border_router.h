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
  BorderRouter (uint32_t systemId, uint16_t isdNumber, uint16_t asNumber, uint32_t localAddress,
                double latitude, double longitude, ScionAs *as)
      : ScionCapableNode (systemId, isdNumber, asNumber, localAddress, latitude, longitude, as)
  {
  }

private:
  void ProcessReceivedPacket (uint16_t localIf, ScionPacket *packet, Time receiveTime) override;
};
} // namespace ns3
#endif //SCION_SIMULATOR_BORDER_ROUTER_H
