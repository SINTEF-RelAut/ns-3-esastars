//
// Created by seyedali on 19.07.21.
//

#ifndef SCION_SIMULATOR_PATH_SERVER_H
#define SCION_SIMULATOR_PATH_SERVER_H

#include <map>
#include <unordered_map>
#include <vector>

#include "ns3/nstime.h"

#include "src/SCION/headers/path_segment.h"
#include "src/SCION/headers/scion_capable_node.h"
#include "src/SCION/headers/scion_packet.h"

namespace ns3 {
class PathServer : public SCIONCapableNode
{
public:
  PathServer (uint32_t system_id, uint16_t isd_number, uint16_t as_number,
              host_addr_t local_address, double latitude, double longitude, SCION_AS *AS)
      : SCIONCapableNode (system_id, isd_number, as_number, local_address, latitude, longitude, AS)
  {
    set_of_all_core_ases.insert (ia_addr);
  }

  void RegisterCorePathSegment (PathSegment &pathSegment, std::string key);
  void RegisterUpPathSegment (PathSegment &pathSegment, std::string key);
  void RegisterDownPathSegment (PathSegment &pathSegment, std::string key);

private:
  std::set<ia_t> set_of_all_core_ases;

  registered_path_segs_dataset_t registered_core_segments;
  registered_path_segs_dataset_t registered_up_segments;
  registered_path_segs_dataset_t registered_down_segments;

  cached_path_segs_dataset_t cached_core_segments;
  cached_path_segs_dataset_t cached_up_segments;
  cached_path_segs_dataset_t cached_down_segments;

  void process_received_packet (uint16_t local_if, SCIONPacket *packet, Time receive_time) override;

  void process_local_host_request_for_path (path_segment_type path_type, ia_t src_ia, ia_t dst_ia,
                                            host_addr_t host_addr);

  void send_registered_path_to_local_host (host_addr_t host_addr, path_segment_type path_type,
                                           ia_t src_ia, ia_t dst_ia,
                                           const reg_path_segs_to_one_as_t *);

  void return_list_of_all_core_ases (host_addr_t host_addr);
};
} // namespace ns3

#endif //SCION_SIMULATOR_PATH_SERVER_H
