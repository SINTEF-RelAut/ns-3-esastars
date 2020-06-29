//
// Created by chrissy on 10.06.20.
//

#ifndef SCION_BEACONING_SIMMULATOR_BEACON_H
#define SCION_BEACONING_SIMMULATOR_BEACON_H
#include <string>
#include <vector>
typedef long double ld;
//link_info[0] = node->as_number;
//    link_info[1] = self_egress_if_no;
//    link_info[2] = remote_as_no;
//    link_info[3] = remote_ingress_if_no;
typedef uint16_t *link_information;
typedef std::vector<link_information> path;

struct beacon {
    int64_t initiation_time, expiration_time, next_initiation_time, next_expiration_time;
    ld latency_stat, bwd_stat;
    path *the_path;
    std::string key; // TODO: Maybe this should be a member function instead?
    bool is_new, is_valid;
};
#endif //SCION_BEACONING_SIMMULATOR_BEACON_H
