//
// Created by seyedali on 19.07.21.
//

#include "src/SCION/headers/path_server.h"
#include "src/SCION/headers/scion_as.h"
#include "src/SCION/headers/scion_core_as.h"
#include "src/SCION/headers/host.h"
#include <iostream>

namespace ns3 {
    void PathServer::RegisterCorePathSegment (PathSegment& pathSegment, std::string key) {
        pathSegment.reverse = true;
        if (registered_core_segments.find(pathSegment.originator) == registered_core_segments.end()) {
            registered_core_segments.insert(std::make_pair(pathSegment.originator, new reg_path_segs_to_one_as_t ()));
        }

        if (registered_core_segments.at(pathSegment.originator)->find(key) == registered_core_segments.at(pathSegment.originator)->end()) {
            registered_core_segments.at(pathSegment.originator)->insert(std::make_pair(key, new PathSegment(pathSegment)));
            return;
        }

        registered_core_segments.at(pathSegment.originator)->at(key)->initiation_time = pathSegment.initiation_time;
        registered_core_segments.at(pathSegment.originator)->at(key)->expiration_time = pathSegment.expiration_time;
    }

    void PathServer::RegisterUpPathSegment (PathSegment& pathSegment, std::string key) {
        pathSegment.reverse = true;
        if (registered_up_segments.find(pathSegment.originator) == registered_up_segments.end()) {
            registered_up_segments.insert(std::make_pair(pathSegment.originator, new reg_path_segs_to_one_as_t ()));
        }

        if (registered_up_segments.at(pathSegment.originator)->find(key) == registered_up_segments.at(pathSegment.originator)->end()) {
            registered_up_segments.at(pathSegment.originator)->insert(std::make_pair(key, new PathSegment(pathSegment)));
            return;
        }

        registered_up_segments.at(pathSegment.originator)->at(key)->initiation_time = pathSegment.initiation_time;
        registered_up_segments.at(pathSegment.originator)->at(key)->expiration_time = pathSegment.expiration_time;
    }
    void PathServer::RegisterDownPathSegment (PathSegment& pathSegment, std::string key) {
        pathSegment.reverse = false;
    }

    void PathServer::ReceiveRequestForPathSegmentFromHost (path_segment_type seg_type, ia_t src_ia, ia_t dst_ia, uint32_t host_addr, uint32_t req_id) {
        if (seg_type == path_segment_type::UP_SEG) {
            return; // TODO
        }

        if (seg_type == path_segment_type::DOWN_SEG) {
            return; // TODO
        }

        if (seg_type == path_segment_type::CORE_SEG && DynamicCast<SCION_Core_AS>(node) == NULL) {
            if (dst_ia == 0) {
                return; //TODO
            } else {
                return; // TODO
            }
        }

        if (seg_type == path_segment_type::CORE_SEG && DynamicCast<SCION_Core_AS>(node) != NULL) {
            if (dst_ia == 0) {
                for (auto const & [registered_dst_ia, registered_path_segs] : registered_core_segments) {
                    if (GET_ISDN(registered_dst_ia) == node->isd_number) {
                        node->events.at(node->GetHostSchedulerIdx(host_addr))
                        ->Schedule(request_processing_delay + node->latencies_between_hosts_and_path_server.at(host_addr),
                                   &Host::ReceiveRegisteredPathSegments,
                                   node->GetHost(host_addr),
                                   path_segment_type::CORE_SEG, node->ia_addr, dst_ia, req_id, registered_core_segments.at(dst_ia));
                    }
                }
            } else {
                for (auto const & [registered_dst_ia, registered_path_segs] : registered_core_segments) {
                    if (GET_ISDN(registered_dst_ia) == dst_ia) {
                        node->events.at(node->GetHostSchedulerIdx(host_addr))
                                ->Schedule(request_processing_delay + node->latencies_between_hosts_and_path_server.at(host_addr),
                                           &Host::ReceiveRegisteredPathSegments,
                                           node->GetHost(host_addr),
                                           path_segment_type::CORE_SEG, node->ia_addr, dst_ia, req_id, registered_core_segments.at(dst_ia));
                    }
                }
            }
        }

    }
}
