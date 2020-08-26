#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/rapidxml.hpp"
#include <vector>
#include <stdio.h>
#include <omp.h>
#include <map>
#include <unordered_map>
#include <fstream>
#include <istream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <random>
#include <cmath>
#include <set>
#include <list>
#include <iostream>
#include <unordered_set>


#define FIXED_BEACONS_NUMBER_TO_SEND 5
#define FIXED_BEACONS_NUMBER_TO_STORE 30
using namespace ns3;
using namespace std;

typedef long double ld;

typedef uint16_t *link_information;
typedef std::vector<link_information> path;

struct beacon {
    int64_t initiation_time, expiration_time, next_initiation_time, next_expiration_time;
    ld latency_stat, bwd_stat;
    path *the_path;
    std::string key;
    bool is_new, is_valid;
};

ld link_level_jaccard_distance_between_two_paths(beacon *beacon1, beacon *beacon2) {
    std::set<uint32_t> set_of_links_on_path1;
    int32_t intersection = 0;

    for (auto const &link_info : *beacon1->the_path) {
        set_of_links_on_path1.insert(((uint32_t) link_info[0]) << 16 | (uint32_t) link_info[1]);
    }

    for (auto const &link_info : *beacon2->the_path) {
        if (set_of_links_on_path1.find(((uint32_t) link_info[0]) << 16 | (uint32_t) link_info[1]) !=
            set_of_links_on_path1.end()) {
            intersection++;
        } else {
            set_of_links_on_path1.insert(((uint32_t) link_info[0]) << 16 | (uint32_t) link_info[1]);
        }
    }

    return 1 - 1.0 * intersection / set_of_links_on_path1.size();
}

ld AS_level_jaccard_distance_between_two_paths(beacon *beacon1, beacon *beacon2) {
    std::set<uint16_t> set_of_ASes_on_path1;
    int32_t intersection = 0;

    for (auto const &link_info : *beacon1->the_path) {
        set_of_ASes_on_path1.insert(link_info[0]);
    }

    for (auto const &link_info : *beacon2->the_path) {
        if (set_of_ASes_on_path1.find(link_info[0]) != set_of_ASes_on_path1.end()) {
            intersection++;
        } else {
            set_of_ASes_on_path1.insert(link_info[0]);
        }
    }

    return 1 - 1.0 * intersection / set_of_ASes_on_path1.size();

}

typedef std::unordered_set<beacon *> beacons_received_from_same_as;
typedef std::unordered_map<uint16_t, beacons_received_from_same_as *> beacons_with_same_src_as;


Time beaconing_period;
int64_t expiration_period;

ld calculate_great_circle_latency(ld lat1_deg, ld long1_deg, ld lat2_deg, ld long2_deg) {
    ld lat1 = lat1_deg * (M_PI) / 180;
    ld long1 = long1_deg * (M_PI) / 180;
    ld lat2 = lat2_deg * (M_PI) / 180;
    ld long2 = long2_deg * (M_PI) / 180;

    // Haversine Formula
    ld dlong = long2 - long1;
    ld dlat = lat2 - lat1;

    ld distance = 6371 * 2 * asin(sqrt(pow(sin(dlat / 2), 2) + cos(lat1) * cos(lat2) * pow(sin(dlong / 2), 2)));

    // 0.005 millisecods of latency per kilometer
    ld latency = distance * 0.005;

    return latency;
}

namespace ns3 {

    class myNode : public Node {

    public:

        //AS properties
        uint16_t as_number;
        int64_t now;
        ld latency_coef, bandwidth_coef, AS_level_diversity_coef, link_level_diversity_coef;
        int32_t AS_max_bwd;
        bool core;

        // Interfaces Properties *****************************************************************************************************
        std::vector<uint16_t> neighbors;
        std::unordered_map<uint16_t, std::vector<uint16_t> > interfaces_per_neighbor_as;

        std::vector<std::pair<ld, ld> > interfaces_coordinates;
        std::vector<std::vector<ld> > intra_as_latencies;
        std::vector<int32_t> inter_as_bwds;

        // beacon store structures ***************************************************************************************************
        std::unordered_map<uint16_t, beacons_with_same_src_as *> beacon_store;
        std::unordered_map<uint16_t, std::multimap <ld, beacon* >* > beacons_sorted_by_score;
        std::unordered_map<std::string, beacon*> path_map_to_beacon;
        // helper structures ********************************************************************************************************
        std::unordered_map<uint16_t, uint64_t> next_round_valid_beacons_count_per_src_as;
        // statistics ***************************************************************************************************************
        std::unordered_map<uint16_t, uint64_t> valid_beacons_count_per_src_as;
        std::unordered_map<int64_t, std::vector<uint32_t> > bytes_sent_per_interface_per_period;

        myNode(uint16_t as_number, uint32_t system_id, ld latency_coef, ld bandwidth_coef, ld AS_level_diversity_coef,
               ld link_level_diversity_coef, bool core) : Node(
                system_id), as_number(as_number), latency_coef(latency_coef), bandwidth_coef(bandwidth_coef),
                                               AS_level_diversity_coef(AS_level_diversity_coef),
                                               link_level_diversity_coef(link_level_diversity_coef),
                                               core(core) {}

        void DoInitializations() {
            intra_as_latencies.resize(GetNDevices());
            for (uint64_t i = 0; i < GetNDevices(); ++i) {
                intra_as_latencies.at(i).resize(GetNDevices());
            }

            for (uint32_t i = 0; i < GetNDevices(); ++i) {
                for (uint32_t j = i + 1; j < GetNDevices(); ++j) {
                    intra_as_latencies.at(i).at(j) = calculate_great_circle_latency(interfaces_coordinates.at(i).first,
                                                                                    interfaces_coordinates.at(i).second,
                                                                                    interfaces_coordinates.at(j).first,
                                                                                    interfaces_coordinates.at(j).second);
                    intra_as_latencies.at(j).at(i) = intra_as_latencies.at(i).at(j);
                }
            }

            AS_max_bwd = 0;
            for (auto const curr_bwd:inter_as_bwds) {
                if (curr_bwd > AS_max_bwd) {
                    AS_max_bwd = curr_bwd;
                }
            }
        }

        void UpdateBeaconStoreAndCountersBeforeBeaconing() {
            now = Simulator::Now().ToInteger(Time::NS);

            if (as_number == 0) {
                std::cout << "################################## " << now << " #########################################" << std::endl;
            }


            for (auto const &the_beacon_pair:path_map_to_beacon) {
                beacon* the_beacon = the_beacon_pair.second;
                uint16_t src_as = the_beacon->the_path->at(0)[0];
                if (the_beacon->is_new) {
                    the_beacon->is_new = false;

                    if (the_beacon->next_expiration_time > now) {
                        if (!the_beacon->is_valid) {
                            try {
                                valid_beacons_count_per_src_as.at(src_as)++;
                            } catch (std::out_of_range){
                                valid_beacons_count_per_src_as.insert(std::make_pair(src_as, 1));
                            }
                        }

                        the_beacon->is_valid = true;
                        the_beacon->initiation_time = the_beacon->next_initiation_time;
                        the_beacon->expiration_time = the_beacon->next_expiration_time;
                    }
                }

                if (the_beacon->expiration_time <= now && the_beacon->is_valid) {
                    the_beacon->is_valid = false;
                    valid_beacons_count_per_src_as.at(src_as)--;
                    next_round_valid_beacons_count_per_src_as.at(src_as)--;
                }
            }

            std::cout << as_number << "\t" <<valid_beacons_count_per_src_as.size() << std::endl; // Print number of source ASes

        }

        void DisseminateBeacons() {
#pragma omp parallel for
            for (uint32_t i = 0; i < neighbors.size(); ++i) { // Per destination AS
                uint16_t remote_as_no = neighbors.at(i);
                for (auto const & src_as_beacons_pair : beacon_store) { // Per source AS
                    uint16_t src_as_no = src_as_beacons_pair.first;
                    beacons_with_same_src_as* equal_src_as_beacons = src_as_beacons_pair.second;
                    if (remote_as_no == src_as_no) {
                        continue;
                    }

                    std::multimap<int64_t, std::tuple<beacon*, uint16_t, uint16_t, Ptr<myNode>, ld , ld> > beacons_ifaces_matchings_scores;

                    for (auto const &sender_as_beacons_pair : *equal_src_as_beacons) {
                        for (auto const &the_beacon : *sender_as_beacons_pair.second) {
                            if (!the_beacon->is_valid ) {
                                continue;
                            }

                            bool generates_loop = false;
                            for (auto const &link_info : *the_beacon->the_path) { // remove loops
                                if (link_info[0] == remote_as_no) {
                                    generates_loop = true;
                                    break;
                                }
                            }

                            if (generates_loop) {
                                continue;
                            }

                            for (auto const &self_egress_if_no : interfaces_per_neighbor_as.at(remote_as_no)) {
                                Ptr<PointToPointNetDevice> self_egress_device = DynamicCast<PointToPointNetDevice>(
                                        GetDevice(self_egress_if_no));

                                Ptr<PointToPointChannel> channel = DynamicCast<PointToPointChannel>(
                                        self_egress_device->GetChannel());
                                uint32_t wire = self_egress_device == channel->GetSource(0) ? 0 : 1;
                                Ptr<PointToPointNetDevice> remote_device = channel->GetDestination(wire);

                                uint16_t remote_ingress_if_no = (uint16_t) remote_device->GetIfIndex();

                                Ptr<myNode> remote_as = (DynamicCast<myNode>(remote_device->GetNode()));

                                ld latency = the_beacon->latency_stat + intra_as_latencies.at(the_beacon->the_path->back()[3]).at(self_egress_if_no);
                                ld bwd  = the_beacon->bwd_stat > (ld) inter_as_bwds.at(self_egress_if_no)
                                          ? (ld) inter_as_bwds.at(self_egress_if_no)
                                          : the_beacon->bwd_stat;


                                ld score = ((1 - latency / 1000) * remote_as->latency_coef +
                                            (bwd / 400) * remote_as->bandwidth_coef)
                                           /
                                           (remote_as->latency_coef + remote_as->bandwidth_coef);

                                if (beacons_ifaces_matchings_scores.size() >= FIXED_BEACONS_NUMBER_TO_SEND
                                    && score <= beacons_ifaces_matchings_scores.begin()->first) {
                                    continue;
                                }

                                beacons_ifaces_matchings_scores.insert(std::make_pair(score, std::tuple<beacon *, uint16_t, uint16_t, Ptr<myNode>, ld,
                                        ld>(the_beacon, self_egress_if_no, remote_ingress_if_no, remote_as, latency, bwd)));

                                if (beacons_ifaces_matchings_scores.size() > FIXED_BEACONS_NUMBER_TO_SEND) {
                                    beacons_ifaces_matchings_scores.erase(beacons_ifaces_matchings_scores.begin());
                                }
                            }
                        }
                    }

                    for (auto const &the_tuple_pair : beacons_ifaces_matchings_scores) {
                        beacon *the_beacon;
                        uint16_t remote_ingress_if_no;
                        uint16_t self_egress_if_no;
                        Ptr<myNode> remote_as;
                        ld latency;
                        ld bwd;

                        std::tie(the_beacon, self_egress_if_no, remote_ingress_if_no, remote_as, latency, bwd) = the_tuple_pair.second;

                        GenerateBeaconAndSend(the_beacon, self_egress_if_no, remote_as_no, remote_ingress_if_no, remote_as,
                                              latency, bwd);

                    }
                }
            }
        }

        void InitiateBeacons() {
#pragma omp parallel for
            for (uint32_t i = 0; i < neighbors.size(); ++i) {
                uint16_t remote_as_no = neighbors.at(i);
                for (auto const & self_egress_if_no : interfaces_per_neighbor_as.at(remote_as_no)) {
                    Ptr<PointToPointNetDevice> self_egress_device = DynamicCast<PointToPointNetDevice>(
                            GetDevice(self_egress_if_no));

                    Ptr<PointToPointChannel> channel = DynamicCast<PointToPointChannel>(
                            self_egress_device->GetChannel());
                    uint32_t wire = self_egress_device == channel->GetSource(0) ? 0 : 1;
                    Ptr<PointToPointNetDevice> remote_device = channel->GetDestination(wire);

                    uint16_t remote_if_no = (uint16_t) remote_device->GetIfIndex();

                    Ptr<myNode> remote_as = (DynamicCast<myNode>(remote_device->GetNode()));

                    GenerateBeaconAndSend(NULL, self_egress_if_no, remote_as_no, remote_if_no, remote_as,
                                          0.0, inter_as_bwds.at(self_egress_if_no));
                }
            }
        }

        void DoBeaconing() {
            now = Simulator::Now().ToInteger(Time::NS);

            bytes_sent_per_interface_per_period.insert(std::make_pair(now, std::vector<uint32_t > (GetNDevices(), 0)));

            if (!this->core) {
                this->DisseminateBeacons();
            }
            if (this->core) {
                this->InitiateBeacons();
            }

        }

        void
        GenerateBeaconAndSend(beacon *old_beacon, uint16_t self_egress_if_no, uint16_t remote_as_no, uint16_t remote_ingress_if_no,
                              Ptr<myNode> remote_as,
                              ld latency, ld bwd) {
            uint16_t src_as;
            std::string key;
            if (old_beacon == NULL) {
                bytes_sent_per_interface_per_period.at(now).at(self_egress_if_no) += (70 + 330);
                src_as = as_number;
            } else {
                bytes_sent_per_interface_per_period.at(now).at(self_egress_if_no) += (70 + 330 + 330 * old_beacon->the_path->size());
                src_as = *old_beacon->the_path->at(0);
                key = old_beacon->key;
            }

            key = key + std::string((char *) &as_number, 2) + std::string((char *) &self_egress_if_no, 2);

            if (remote_as->path_map_to_beacon.find(key) != remote_as->path_map_to_beacon.end()) {
                if (old_beacon == NULL) {
                    remote_as->path_map_to_beacon.at(key)->next_initiation_time = now;
                    remote_as->path_map_to_beacon.at(key)->next_expiration_time = now + expiration_period;
                } else {
                    remote_as->path_map_to_beacon.at(key)->next_initiation_time = old_beacon->initiation_time;
                    remote_as->path_map_to_beacon.at(key)->next_expiration_time = old_beacon->expiration_time;
                }
                remote_as->path_map_to_beacon.at(key)->is_new = true;
                return;
            }

            ld score = ((1 - latency / 1000) * remote_as->latency_coef + (bwd / 400) * remote_as->bandwidth_coef)
                       /
                       (remote_as->latency_coef + remote_as->bandwidth_coef);

            if (remote_as->next_round_valid_beacons_count_per_src_as.find(src_as) != remote_as->next_round_valid_beacons_count_per_src_as.end()) {
                if (remote_as->next_round_valid_beacons_count_per_src_as.at(src_as) >= FIXED_BEACONS_NUMBER_TO_STORE) {
                    std::multimap <ld, beacon* >::iterator it = remote_as->beacons_sorted_by_score.at(src_as)->begin();
                    if (it->first < score) {
                        beacon* lower_score_beacon = it->second;
                        remote_as->beacons_sorted_by_score.at(src_as)->erase(it);
                        remote_as->path_map_to_beacon.erase(lower_score_beacon->key);
                        remote_as->beacon_store.at(src_as)->at(lower_score_beacon->the_path->back()[0])->erase(lower_score_beacon);
                        if (remote_as->beacon_store.at(src_as)->at(lower_score_beacon->the_path->back()[0])->empty()) {
                            remote_as->beacon_store.at(src_as)->erase(lower_score_beacon->the_path->back()[0]);
                        }

                        if (lower_score_beacon->is_valid) {
                            remote_as->valid_beacons_count_per_src_as.at(src_as)--;
                        }

                        *lower_score_beacon->the_path = *old_beacon->the_path;

                        uint16_t *link_info = new uint16_t[4];
                        link_info[0] = as_number;
                        link_info[1] = self_egress_if_no;
                        link_info[2] = remote_as_no;
                        link_info[3] = remote_ingress_if_no;

                        lower_score_beacon->the_path->push_back(link_info);
                        lower_score_beacon->key = key;
                        lower_score_beacon->initiation_time = -1;
                        lower_score_beacon->expiration_time = -1;
                        lower_score_beacon->next_initiation_time = old_beacon->initiation_time;
                        lower_score_beacon->next_expiration_time = old_beacon->expiration_time;
                        lower_score_beacon->is_new = true;
                        lower_score_beacon->is_valid = false;
                        lower_score_beacon->bwd_stat = bwd;
                        lower_score_beacon->latency_stat = latency;

                        remote_as->beacons_sorted_by_score.at(src_as)->insert(std::make_pair(score, lower_score_beacon));
                        remote_as->path_map_to_beacon.insert(std::make_pair(key, lower_score_beacon));

                        if (remote_as->beacon_store.at(src_as)->find(as_number) != remote_as->beacon_store.at(src_as)->end()) {
                            remote_as->beacon_store.at(src_as)->at(as_number)->insert(lower_score_beacon);
                        } else {
                            remote_as->beacon_store.at(src_as)->insert(std::make_pair(as_number, new beacons_received_from_same_as()));
                            remote_as->beacon_store.at(src_as)->at(as_number)->insert(lower_score_beacon);
                        }
                        return;
                    }
                    return;
                }
                remote_as->next_round_valid_beacons_count_per_src_as.at(src_as)++;
            } else {
                remote_as->next_round_valid_beacons_count_per_src_as.insert(std::make_pair(src_as, 1));
            }

            beacon *new_beacon = new beacon;
            path *new_path = new path;
            new_beacon->the_path = new_path;
            new_beacon->bwd_stat = bwd;
            new_beacon->latency_stat = latency;

            uint16_t *link_info = new uint16_t[4];
            link_info[0] = as_number;
            link_info[1] = self_egress_if_no;
            link_info[2] = remote_as_no;
            link_info[3] = remote_ingress_if_no;

            new_beacon->initiation_time = -1;
            new_beacon->expiration_time = -1;
            new_beacon->key = key;
            new_beacon->is_new = true;
            new_beacon->is_valid = false;

            if (old_beacon == NULL) {
                new_beacon->next_initiation_time = now;
                new_beacon->next_expiration_time = now + expiration_period;
            } else {
                new_beacon->next_initiation_time = old_beacon->initiation_time;
                new_beacon->next_expiration_time = old_beacon->expiration_time;

                *new_path = *(old_beacon->the_path);
            }

            new_path->push_back(link_info);
            remote_as->path_map_to_beacon.insert(std::make_pair(key, new_beacon));

            if (remote_as->beacon_store.find(src_as) != remote_as->beacon_store.end() &&
                remote_as->beacon_store.at(src_as)->find(as_number) != remote_as->beacon_store.at(src_as)->end()) {
                remote_as->beacon_store.at(src_as)->at(as_number)->insert(new_beacon);
            } else if (remote_as->beacon_store.find(src_as) != remote_as->beacon_store.end() &&
                       remote_as->beacon_store.at(src_as)->find(as_number) == remote_as->beacon_store.at(src_as)->end()) {
                remote_as->beacon_store.at(src_as)->insert(std::make_pair(as_number, new beacons_received_from_same_as ()));
                remote_as->beacon_store.at(src_as)->at(as_number)->insert(new_beacon);
            } else {
                remote_as->beacon_store.insert(std::make_pair(src_as, new beacons_with_same_src_as));
                remote_as->beacon_store.at(src_as)->insert(std::make_pair(as_number, new beacons_received_from_same_as()));
                remote_as->beacon_store.at(src_as)->at(as_number)->insert(new_beacon);
            }

            if (remote_as->beacons_sorted_by_score.find(src_as) != remote_as->beacons_sorted_by_score.end()) {
                remote_as->beacons_sorted_by_score.at(src_as)->insert(std::make_pair(score, new_beacon));
            } else {
                remote_as->beacons_sorted_by_score.insert(std::make_pair(src_as, new std::multimap<ld, beacon*> ()));
                remote_as->beacons_sorted_by_score.at(src_as)->insert(std::make_pair(score, new_beacon));
            }
        }

        std::pair<ld, ld> calculate_final_diversity_scores(beacon *the_beacon) {
            ld AS_level_diversity_score = 0;
            ld link_level_diversity_score = 0;
            int32_t counter = 0;
            uint16_t src_as = *the_beacon->the_path->at(0);
            beacons_with_same_src_as *equal_scr_as_beacons = beacon_store.at(src_as);
            for (auto const &received_if_beacon_vector_pair : *equal_scr_as_beacons) {
                for (auto const &curr_beacon : *received_if_beacon_vector_pair.second) {
                    if (curr_beacon != the_beacon) {
                        AS_level_diversity_score += AS_level_jaccard_distance_between_two_paths(the_beacon,
                                                                                                curr_beacon);
                        link_level_diversity_score += link_level_jaccard_distance_between_two_paths(the_beacon,
                                                                                                    curr_beacon);
                        counter++;
                    }
                }
            }

            return (std::make_pair(AS_level_diversity_score / counter, link_level_diversity_score / counter));
        }

        void FinalPathEvaluation(std::map<ld, uint64_t> &satisfaction_stat,
                                 std::map<ld, uint64_t> &AS_level_diversity_stat,
                                 std::map<ld, uint64_t> &link_level_diversity_stat) {
            for (auto const &the_beacon_pair:path_map_to_beacon) {
                beacon* the_beacon = the_beacon_pair.second;
                if (the_beacon->is_valid) {
                    std::pair<ld, ld> diversity_scores = this->calculate_final_diversity_scores(the_beacon);
                    ld AS_level_diversity_score = diversity_scores.first;
                    ld link_level_diversity_score = diversity_scores.second;
                    ld latency_score = 1 - the_beacon->latency_stat / 1000;
                    ld bwd_score = the_beacon->bwd_stat / AS_max_bwd;

                    ld score =
                            (latency_score * latency_coef + bwd_score * bandwidth_coef +
                             AS_level_diversity_score * AS_level_diversity_coef +
                             link_level_diversity_score * link_level_diversity_coef) /
                            (latency_coef + bandwidth_coef + AS_level_diversity_coef + link_level_diversity_coef);

                    score = roundl(score * 1000) / 1000;
                    AS_level_diversity_score = roundl(AS_level_diversity_score * 1000) / 1000;
                    link_level_diversity_score = roundl(link_level_diversity_score * 1000) / 1000;


                    if (satisfaction_stat.find(score) != satisfaction_stat.end()) {
                        satisfaction_stat.at(score)++;
                    } else {
                        satisfaction_stat.insert(std::make_pair(score, 1));
                    }

                    if (AS_level_diversity_stat.find(AS_level_diversity_score) != AS_level_diversity_stat.end()) {
                        AS_level_diversity_stat.at(AS_level_diversity_score)++;
                    } else {
                        AS_level_diversity_stat.insert(std::make_pair(AS_level_diversity_score, 1));
                    }

                    if (link_level_diversity_stat.find(link_level_diversity_score) != link_level_diversity_stat.end()) {
                        link_level_diversity_stat.at(link_level_diversity_score)++;
                    } else {
                        link_level_diversity_stat.insert(std::make_pair(link_level_diversity_score, 1));
                    }
                }
            }
        }
    };
}

class PropertyContainer {
public:

    std::string getProperty(const std::string &name) const {
        propertiesType::const_iterator it;
        it = this->properties.find(name);

        if (it != this->properties.end())
            return it->second;
        else
            exit(1);

    }


    void setProperty(const std::string &name, const std::string &value) {
        this->properties[name] = value;
    }


    bool hasProperty(const std::string &name) const {
        propertiesType::const_iterator it = this->properties.find(name);

        if (it == this->properties.end()) {
            return false;
        } else {
            return true;
        }

    }


private:
    typedef std::map<std::string, std::string> propertiesType;
    propertiesType properties;

};


std::string getAttribute(rapidxml::xml_node<> *node, const std::string &name) {
    rapidxml::xml_attribute<> *attr = node->first_attribute(name.c_str());
    if (attr) {
        return attr->value();
    } else {
        return std::string();
    }
}

PropertyContainer parseProperties(rapidxml::xml_node<> *node) {
    PropertyContainer p;
    rapidxml::xml_node<> *curNode = node->first_node("property");

    while (curNode) {
        std::string name = getAttribute(curNode, "name");
        if (name != "") {
            p.setProperty(name, curNode->value());
        }
        curNode = curNode->next_sibling("property");
    }

    return p;
}


void ProcessReceivedPacketsParallel(NodeContainer nodes) {
    uint32_t node_number = nodes.GetN();

#pragma omp parallel for
    for (uint32_t i = 0; i < node_number; ++i) {
        DynamicCast<myNode>(nodes.Get(i))->UpdateBeaconStoreAndCountersBeforeBeaconing();
    }
}

int
main(int argc, char *argv[]) {

    beaconing_period = Time(argv[1]);
    expiration_period = Time(argv[2]).ToInteger(Time::NS);
    std::string file = "./topology/" + std::string(argv[4]) + ".xml";

    std::ifstream fin(file.c_str());
    std::ostringstream sstr;
    sstr << fin.rdbuf();

    std::string out_path =
            "./results/intra_isd_criteria_matching_" + std::string(argv[4]) + "_" +
            std::string(argv[1]) + "_" + std::string(argv[2]) + "_" + std::string(argv[3]) + ".txt";
    std::ofstream out(out_path);
    std::cout.rdbuf(out.rdbuf());

    sstr.flush();
    fin.close();

    std::string xmlData = sstr.str();
    rapidxml::xml_document<> doc;
    doc.parse<0>(&xmlData[0]);

    rapidxml::xml_node<> *rootNode = doc.first_node("topology");
    rapidxml::xml_node<> *curNode;
/**/
    if (!rootNode) {
        std::cerr << "Empty topology!" << std::endl;
        return 1;
    }


    NodeContainer nodes;
    int16_t node_counter = 0;
    std::map<int32_t, uint16_t> ASes;


    curNode = rootNode->first_node("node");
    while (curNode) {
        int32_t as_number = std::stoi(getAttribute(curNode, "id"));
        PropertyContainer p = parseProperties(curNode);

        ld latency_coef = std::stod(p.getProperty("latency_coef"));
        ld bandwidth_coef = std::stod(p.getProperty("bandwidth_coef"));
        ld AS_level_diversity_coef = std::stod(p.getProperty("AS_level_diversity_coef"));
        ld link_level_diversity_coef = std::stod(p.getProperty("link_level_diversity_coef"));
        std::string type = p.getProperty("type");

        bool isCore = type.compare("core") == 0;

        nodes.Add(CreateObject<myNode>(node_counter, 0, latency_coef, bandwidth_coef, AS_level_diversity_coef,
                                       link_level_diversity_coef, isCore));

        ASes.insert(std::make_pair(as_number, node_counter));

        node_counter++;


        curNode = curNode->next_sibling("node");
    }


    curNode = rootNode->first_node("link");
    while (curNode) {
        int32_t to = std::stoi(curNode->first_node("to")->value());
        int32_t from = std::stoi(curNode->first_node("from")->value());

        PropertyContainer p = parseProperties(curNode);

        ld latitude = std::stod(p.getProperty("latitude"));
        ld longitude = std::stod(p.getProperty("longitude"));
        int32_t bwd = std::stoi(p.getProperty("capacity"));
        std::string rel = p.getProperty("rel");

        Ptr<Node> fromNode;
        Ptr<Node> toNode;

        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            if ((DynamicCast<myNode>(nodes.Get(i)))->as_number == ASes.at(to)) {
                toNode = nodes.Get(i);
                break;
            }
        }

        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            if ((DynamicCast<myNode>(nodes.Get(i)))->as_number == ASes.at(from)) {
                fromNode = nodes.Get(i);
                break;
            }
        }

        PointToPointHelper helper;
        helper.Install(fromNode, toNode);

        Ptr<myNode> to_my_node = (DynamicCast<ns3::myNode>(toNode));
        Ptr<myNode> from_my_node = (DynamicCast<ns3::myNode>(fromNode));

        to_my_node->interfaces_coordinates.push_back(std::pair<ld, ld>(latitude, longitude));
        from_my_node->interfaces_coordinates.push_back(std::pair<ld, ld>(latitude, longitude));

        to_my_node->inter_as_bwds.push_back(bwd);
        from_my_node->inter_as_bwds.push_back(bwd);

        if (to_my_node->interfaces_per_neighbor_as.find(from_my_node->as_number) !=
            to_my_node->interfaces_per_neighbor_as.end()) {
            to_my_node->interfaces_per_neighbor_as.at(from_my_node->as_number).push_back(to_my_node->GetNDevices() - 1);
        } else {
            std::vector<uint16_t> tmp;
            tmp.push_back((uint16_t) to_my_node->GetNDevices() - 1);
            to_my_node->interfaces_per_neighbor_as.insert(std::make_pair(from_my_node->as_number, tmp));
        }


        if (from_my_node->interfaces_per_neighbor_as.find(to_my_node->as_number) !=
            from_my_node->interfaces_per_neighbor_as.end()) {
            from_my_node->interfaces_per_neighbor_as.at(to_my_node->as_number).push_back(
                    from_my_node->GetNDevices() - 1);
        } else {
            std::vector<uint16_t> tmp;
            tmp.push_back((uint16_t) from_my_node->GetNDevices() - 1);
            from_my_node->interfaces_per_neighbor_as.insert(std::make_pair(to_my_node->as_number, tmp));
            if (rel.compare("customer") == 0) {
                from_my_node->neighbors.push_back(to_my_node->as_number);
            }
        }

        curNode = curNode->next_sibling("link");
    }


    for (uint64_t i = 0; i < nodes.GetN(); ++i) {
        DynamicCast<myNode>(nodes.Get(i))->DoInitializations();
    }

    for (Time t = Seconds(0.0); t < Time(argv[3]); t += beaconing_period) {
        Simulator::Schedule(t + Seconds(30.0), &ProcessReceivedPacketsParallel, nodes);

        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            Ptr<myNode> the_node = DynamicCast<myNode>(nodes.Get(i));
            Simulator::Schedule(t, &myNode::DoBeaconing, the_node);
        }
    }

    Simulator::Stop(Time(argv[3]));
    Simulator::Run();

    //############################################################################################################################################################
    for (Time t = Seconds(0.0); t < Time(argv[3]); t += beaconing_period) {
        std::cout << "####################################### frequencies of consumed bandwidth at Time "
                  << t
                  << "#######################################" << std::endl;

        std::map<uint32_t, uint32_t> frequencies_of_consumed_bwd;
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            Ptr<myNode> the_node = DynamicCast<myNode>(nodes.Get(i));
            for (uint32_t if_index = 0; if_index < the_node->GetNDevices(); ++if_index) {
                uint32_t consumed_bwd = the_node->bytes_sent_per_interface_per_period.at(t.ToInteger(Time::NS)).at(if_index);

                if (frequencies_of_consumed_bwd.find(consumed_bwd) != frequencies_of_consumed_bwd.end()) {
                    frequencies_of_consumed_bwd.at(consumed_bwd)++;
                } else {
                    frequencies_of_consumed_bwd.insert(std::make_pair(consumed_bwd, 1));
                }
            }
        }

        std::cout << "consumed bandwidth on a link" << "\t" << "frequency" << std::endl;
        for (auto const & bwd_freq_pair : frequencies_of_consumed_bwd) {
            std::cout << bwd_freq_pair.first << "\t" << bwd_freq_pair.second << std::endl;
        }
    }

    //############################################################################################################################################################
    for (uint32_t path_length = 1; path_length <= 4; ++path_length) {
        std::cout
                << "######################################### frequencies of path counts per source AS with length "
                << path_length - 1
                << "#########################################"
                << std::endl;
        std::map<uint64_t, uint64_t> frequencies_of_path_counts_per_src_as_with_certain_length;
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            for (auto const &src_as_beacons_pair : DynamicCast<myNode>(nodes.Get(i))->beacon_store) {
                uint64_t number_of_paths_with_certain_length = 0;

                for (auto const &ingress_if_beacons_pair : *src_as_beacons_pair.second) {
                    for (auto const &the_beacon : *ingress_if_beacons_pair.second) {
                        if (the_beacon->the_path->size() == path_length) {
                            number_of_paths_with_certain_length++;
                        }
                    }
                }

                if (frequencies_of_path_counts_per_src_as_with_certain_length.find(
                        number_of_paths_with_certain_length) != frequencies_of_path_counts_per_src_as_with_certain_length.end()) {
                    frequencies_of_path_counts_per_src_as_with_certain_length.at(number_of_paths_with_certain_length)++;
                } else {
                    frequencies_of_path_counts_per_src_as_with_certain_length.insert(
                            std::make_pair(number_of_paths_with_certain_length, 1));
                }
            }
        }

        std::cout << "path count per source AS" << "\t" << "frequency" << std::endl;
        for (auto const &count_freq_pair : frequencies_of_path_counts_per_src_as_with_certain_length) {
            std::cout << count_freq_pair.first << "\t" << count_freq_pair.second << std::endl;
        }
    }

    std::cout << "###################################################### PATH QUALITY #############################################################"
              << std::endl;
    std::cout << "##                                                                                                                             ##"
              << std::endl;

    std::map <ld, uint64_t> satisfaction_stat;
    std::map <ld, uint64_t> AS_level_diversity_stat;
    std::map <ld, uint64_t> link_level_diversity_stat;

    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        DynamicCast<myNode>(nodes.Get(i))->FinalPathEvaluation(satisfaction_stat,
                                                               AS_level_diversity_stat,
                                                               link_level_diversity_stat);
    }


    std::cout << "###################################################### SATISFACTION #############################################################"
              << std::endl;
    std::cout << "satisfaction score" << "\t" << "frequency" << std::endl;

    for (auto const &satisfaction_pair : satisfaction_stat) {
        std::cout << satisfaction_pair.first << "\t" << satisfaction_pair.second << std::endl;
    }

    std::cout << "###################################################### LINK DIVERSITY ###########################################################"
              << std::endl;
    std::cout << "link diversity score" << "\t" << "frequency" << std::endl;

    for (auto const &diversity_pair : link_level_diversity_stat) {
        std::cout << diversity_pair.first << "\t" << diversity_pair.second << std::endl;
    }

    std::cout << "###################################################### AS DIVERSITY #############################################################"
              << std::endl;
    std::cout << "AS diversity score" << "\t" << "frequency" << std::endl;

    for (auto const &diversity_pair : AS_level_diversity_stat) {
        std::cout << diversity_pair.first << "\t" << diversity_pair.second << std::endl;
    }

    Simulator::Destroy();
    return 0;
}
