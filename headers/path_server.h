//
// Created by seyedali on 19.07.21.
//

#ifndef NS_3_BEACONING_SIMULATOR_PATH_SERVER_H
#define NS_3_BEACONING_SIMULATOR_PATH_SERVER_H

#include <unordered_map>
#include <vector>

namespace ns3 {

#define GET_ISDN(input) ((uint16_t) ((input) >> 16))
#define GET_ASN(input)  ((uint16_t) ((input) & 0x0000ffff))

    typedef uint32_t ISD_AS_pair;

    struct PathSegment {
        ISD_AS_pair originator;
        uint16_t initiation_time;
        uint16_t expiration_time;

        std::vector<uint64_t> hops;

        PathSegment(){}

        PathSegment (PathSegment& pathSegment) :
        originator(pathSegment.originator),
        initiation_time(pathSegment.initiation_time),
        expiration_time(pathSegment.expiration_time),
        hops(pathSegment.hops) {}
    };

    typedef std::unordered_map<std::string, PathSegment*> path_segments_to_one_as;

    class PathServer {
    public:
        void RegisterCorePathSegment (PathSegment& pathSegment, std::string key);
        void RegisterUpPathSegment (PathSegment& pathSegment, std::string key);
        void RegisterDownPathSegment (PathSegment& pathSegment, std::string key);

        void ReceiveRequestForPathSegment (PathSegment& pathSegment, std::string key);
    private:
        std::unordered_map<ISD_AS_pair, path_segments_to_one_as> registered_core_segments;
        std::unordered_map<ISD_AS_pair, path_segments_to_one_as> registered_up_segments;
        std::unordered_map<ISD_AS_pair, path_segments_to_one_as> registered_down_segments;

        std::unordered_map<ISD_AS_pair, path_segments_to_one_as> cached_core_segments;
        std::unordered_map<ISD_AS_pair, path_segments_to_one_as> cached_up_segments;
        std::unordered_map<ISD_AS_pair, path_segments_to_one_as> cached_down_segments;
    };
}

#endif //NS_3_BEACONING_SIMULATOR_PATH_SERVER_H
