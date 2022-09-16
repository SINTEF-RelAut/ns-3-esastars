//
// Created by seyedali on 19.07.21.
//

#ifndef SCION_SIMULATOR_SCION_HOST_H
#define SCION_SIMULATOR_SCION_HOST_H

#include <unordered_map>

#include "ns3/nstime.h"

#include "src/SCION/headers/path_segment.h"
#include "src/SCION/headers/scion_capable_node.h"
#include "src/SCION/headers/scion_packet.h"

namespace ns3 {
class SCIONHost : public SCIONCapableNode
{
public:
  SCIONHost (uint32_t system_id, uint16_t isd_number, uint16_t as_number, host_addr_t local_address,
             double latitude, double longitude, SCION_AS *AS)
      : SCIONCapableNode (system_id, isd_number, as_number, local_address, latitude, longitude, AS)
  {
  }

  void SendArbitraryPacket (ia_t dst_ia, host_addr_t dst_host);

protected:
  cached_path_segs_dataset_t cached_up_path_segments;
  cached_path_segs_dataset_t cached_core_path_segments;
  cached_path_segs_dataset_t cached_down_path_segments;

  virtual void process_received_packet (uint16_t local_if, SCIONPacket *packet,
                                        Time receive_time) override;
  virtual void modify_pkt_upon_send (SCIONPacket *packet) override;
  void remove_expired_segments ();
  void search_in_cached_segments (ia_t dst_ia, std::vector<const PathSegment *> &path,
                                  std::vector<uint8_t> &shortcuts);
  void request_for_path_segments (ia_t dst_ia);
  void send_request_for_path_segments (path_segment_type seg_type, ia_t src_ia, ia_t dst_ia);

  void receive_registered_path_segments (path_segment_type seg_type, ia_t src_ia, ia_t dst_ia,
                                         const reg_path_segs_to_one_as_t *path_segments);
  void receive_cached_path_segments (path_segment_type seg_type, ia_t src_ia, ia_t dst_ia,
                                     cached_path_segs_per_dst_t *path_seg);

  void cache_path_segment (path_segment_type seg_type, ia_t src_ia, ia_t dst_ia,
                           PathSegment *path_seg);
};
} // namespace ns3

#endif //SCION_SIMULATOR_SCION_HOST_H
