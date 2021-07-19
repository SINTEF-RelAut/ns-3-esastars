//
// Created by seyedali on 19.07.21.
//

#include "src/SCION/headers/path_server.h"
#include <iostream>

namespace ns3 {
    void PathServer::RegisterCorePathSegment (PathSegment& pathSegment, std::string key) {
        std::cout << "Registering Core Path Segments " << std::endl;
        if (registered_core_segments.find(pathSegment.originator) == registered_core_segments.end()) {
            registered_core_segments.insert(std::make_pair(pathSegment.originator, path_segments_to_one_as()));
        }

        if (registered_core_segments.at(pathSegment.originator).find(key) == registered_core_segments.at(pathSegment.originator).end()) {
            registered_core_segments.at(pathSegment.originator).insert(std::make_pair(key, new PathSegment(pathSegment)));
            return;
        }

        registered_core_segments.at(pathSegment.originator).at(key)->initiation_time = pathSegment.initiation_time;
        registered_core_segments.at(pathSegment.originator).at(key)->expiration_time = pathSegment.expiration_time;
    }

    void PathServer::RegisterUpPathSegment (PathSegment& pathSegment, std::string key) {
        if (registered_up_segments.find(pathSegment.originator) == registered_up_segments.end()) {
            registered_up_segments.insert(std::make_pair(pathSegment.originator, path_segments_to_one_as()));
        }

        if (registered_up_segments.at(pathSegment.originator).find(key) == registered_up_segments.at(pathSegment.originator).end()) {
            registered_up_segments.at(pathSegment.originator).insert(std::make_pair(key, new PathSegment(pathSegment)));
            return;
        }

        registered_up_segments.at(pathSegment.originator).at(key)->initiation_time = pathSegment.initiation_time;
        registered_up_segments.at(pathSegment.originator).at(key)->expiration_time = pathSegment.expiration_time;
    }
    void PathServer::RegisterDownPathSegment (PathSegment& pathSegment, std::string key) {}

    void PathServer::ReceiveRequestForPathSegment (PathSegment& pathSegment, std::string key) {}
}
