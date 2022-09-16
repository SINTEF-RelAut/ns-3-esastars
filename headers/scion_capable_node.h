//
// Created by seyedali on 28.07.21.
//

#ifndef SCION_SIMULATOR_SCION_CAPABLE_NODE_H
#define SCION_SIMULATOR_SCION_CAPABLE_NODE_H

#include "ns3/node.h"

#include "src/SCION/headers/scion_packet.h"

namespace ns3 {
class SCION_AS;

class SCIONCapableNode : public Node
{
public:
  SCIONCapableNode (uint32_t system_id, uint16_t isd_number, uint16_t as_number,
                    host_addr_t local_address, double latitude, double longitude, SCION_AS *AS)
      : Node (system_id),
        isd_number (isd_number),
        as_number (as_number),
        local_address (local_address),
        latitude (latitude),
        longitude (longitude),
        AS (AS)
  {
    ia_addr = (((uint32_t) isd_number) << 16) | ((uint32_t) as_number);
    next_packet_id = 0;
    processing_queue_length = 0;
    local_time = TimeStep (0);
  }

  void AddToIFForwadingTable (uint16_t as_if, uint16_t local_if);
  void AddToAddressForwardingTable (host_addr_t addr, uint16_t local_if);
  void ScheduleReceive (uint16_t local_if, SCIONPacket *packet, Time propagation_delay);

  host_addr_t GetLocalAddress () const;

  double GetLatitude () const;
  double GetLogitude () const;

  void AddToPropagationDelays (Time delay);
  void AddToTransmissionDelays (Time delay);
  void SetProcessingDelay (Time delay, Time throughput_delay);
  void AddToRemoteNodesInfo (SCIONCapableNode *remote_node, uint16_t remote_if, uint16_t remote_isd,
                             uint16_t remote_as);
  void InitializeTransmissionQueues ();
  void DestroySCIONPacket (SCIONPacket *packet);
  uint32_t GetNDevices (void) const;
  Time GetLocalTime (void) const;

  virtual void AdvanceLocalTime ();

protected:
  uint16_t isd_number;
  uint16_t as_number;

  ia_t ia_addr;

  host_addr_t local_address;

  double latitude;
  double longitude;

  SCION_AS *AS;

  // Queueing delay is modeled by the processing and send scheduling queues, but no drop function is implemented yet
  std::vector<Time> propagation_delays;
  std::vector<Time> transmission_delays; // In picoseconds/byte
  Time processing_delay, processing_throughput_delay;

  std::vector<uint32_t> transmission_queues_lengths; // In bytes
  uint32_t processing_queue_length; // In packets

  packet_id_t next_packet_id;

  Time local_time;

  std::unordered_map<uint16_t, uint16_t> forwarding_table_to_other_AS_ifaces;
  std::unordered_map<host_addr_t, uint16_t> forwarding_table_to_addresses_inside_as;

  std::unordered_map<packet_id_t, SCIONPacket> on_the_flight_packets;

  std::vector<std::tuple<SCIONCapableNode *, uint16_t, bool>> remote_nodes_info;

  void receive (uint16_t local_if, SCIONPacket *packet);
  void send (uint16_t local_if, SCIONPacket *packet);
  virtual void modify_pkt_upon_send (SCIONPacket *packet);
  virtual void process_received_packet (uint16_t local_if, SCIONPacket *packet, Time receive_time);
  void schedule_for_send (uint16_t local_if, SCIONPacket *packet);

  void send_scion_packet (SCIONPacket *packet);
  SCIONPacket *create_scion_packet (
      const Payload &payload, payload_type_t payload_type, ia_t dst_ia, host_addr_t dst_host,
      int32_t payload_size,
      const std::vector<const PathSegment *> &the_path = std::vector<const PathSegment *> (),
      const std::vector<uint8_t> &shortcut_hopfs = std::vector<uint8_t> ());

  void return_scion_packet (SCIONPacket *packet);
};
} // namespace ns3
#endif //SCION_SIMULATOR_SCION_CAPABLE_NODE_H
