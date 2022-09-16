//
// Created by seyedali on 28.07.21.
//

#ifndef SCION_SIMULATOR_SCION_CAPABLE_NODE_H
#define SCION_SIMULATOR_SCION_CAPABLE_NODE_H

#include "ns3/node.h"

#include "src/SCION/headers/scion_packet.h"

namespace ns3 {
class ScionAs;

class ScionCapableNode : public Node
{
public:
  ScionCapableNode (uint32_t systemId, uint16_t isdNumber, uint16_t asNumber,
                    HostAddr_t localAddress, double latitude, double longitude, ScionAs *as)
      : Node (systemId),
        isdNumber (isdNumber),
        asNumber (asNumber),
        localAddress (localAddress),
        latitude (latitude),
        longitude (longitude),
        as (as)
  {
    iaAddr = (((uint32_t) isdNumber) << 16) | ((uint32_t) asNumber);
    nextPacketId = 0;
    processingQueueLength = 0;
    localTime = TimeStep (0);
  }

  void AddToIfForwadingTable (uint16_t asIf, uint16_t localIf);
  void AddToAddressForwardingTable (HostAddr_t addr, uint16_t localIf);
  void ScheduleReceive (uint16_t localIf, ScionPacket *packet, Time propagationDelay);

  HostAddr_t GetLocalAddress () const;

  double GetLatitude () const;
  double GetLogitude () const;

  void AddToPropagationDelays (Time delay);
  void AddToTransmissionDelays (Time delay);
  void SetProcessingDelay (Time delay, Time throughputDelay);
  void AddToRemoteNodesInfo (ScionCapableNode *remoteNode, uint16_t remoteIf, uint16_t remoteIsd,
                             uint16_t remoteAs);
  void InitializeTransmissionQueues ();
  void DestroyScionPacket (ScionPacket *packet);
  uint32_t GetNDevices (void) const;
  Time GetLocalTime (void) const;

  virtual void AdvanceLocalTime ();

protected:
  uint16_t isdNumber;
  uint16_t asNumber;

  Ia_t iaAddr;

  HostAddr_t localAddress;

  double latitude;
  double longitude;

  ScionAs *as;

  // Queueing delay is modeled by the processing and send scheduling queues, but no drop function is implemented yet
  std::vector<Time> propagationDelays;
  std::vector<Time> transmissionDelays; // In picoseconds/byte
  Time processingDelay, processingThroughputDelay;

  std::vector<uint32_t> transmissionQueuesLengths; // In bytes
  uint32_t processingQueueLength; // In packets

  PacketId_t nextPacketId;

  Time localTime;

  std::unordered_map<uint16_t, uint16_t> forwardingTableToOtherAsIfaces;
  std::unordered_map<HostAddr_t, uint16_t> forwardingTableToAddressesInsideAs;

  std::unordered_map<PacketId_t, ScionPacket> onTheFlightPackets;

  std::vector<std::tuple<ScionCapableNode *, uint16_t, bool>> remoteNodesInfo;

  void Receive (uint16_t localIf, ScionPacket *packet);
  void Send (uint16_t localIf, ScionPacket *packet);
  virtual void ModifyPktUponSend (ScionPacket *packet);
  virtual void ProcessReceivedPacket (uint16_t localIf, ScionPacket *packet, Time receiveTime);
  void ScheduleForSend (uint16_t localIf, ScionPacket *packet);

  void SendScionPacket (ScionPacket *packet);
  ScionPacket *CreateScionPacket (
      const Payload &payload, PayloadType payloadType, Ia_t dstIa, HostAddr_t dstHost,
      int32_t payloadSize,
      const std::vector<const PathSegment *> &thePath = std::vector<const PathSegment *> (),
      const std::vector<uint8_t> &shortcutHopfs = std::vector<uint8_t> ());

  void ReturnScionPacket (ScionPacket *packet);
};
} // namespace ns3
#endif //SCION_SIMULATOR_SCION_CAPABLE_NODE_H
