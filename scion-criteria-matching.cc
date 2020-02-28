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

#define FIXED_BEACONS_NUMBER_TO_SEND 5
#define FIXED_BEACONS_NUMBER_TO_STORE 50
using namespace ns3;

typedef long double ld;

typedef uint16_t* link_information;
typedef std::vector<link_information> path;
struct beacon {
    int64_t initiation_time, expiration_time, next_initiation_time, next_expiration_time;
    double latency_stat, bwd_stat;
    path* the_path;
    std::string key;
    bool is_new, is_valid;
};

typedef std::vector <beacon*> beacons_from_same_if;
typedef std::unordered_map <uint16_t, beacons_from_same_if*> beacons_with_same_src_as;

int64_t now;
Time beaconing_period;
int64_t expiration_period;

  
ld calculate_latency(ld lat1_deg, ld long1_deg, ld lat2_deg, ld long2_deg) { 
    ld lat1 = lat1_deg * (M_PI) / 180; 
    ld long1 = long1_deg * (M_PI) / 180; 
    ld lat2 = lat2_deg * (M_PI) / 180; 
    ld long2 = long2_deg * (M_PI) / 180; 
      
    // Haversine Formula 
    ld dlong = long2 - long1; 
    ld dlat = lat2 - lat1; 
  
    ld distance = 6371 * 2 * asin(sqrt( pow(sin(dlat / 2), 2) +  cos(lat1) * cos(lat2) * pow(sin(dlong / 2), 2) ) ); 
  
    // 0.005 millisecods of latency per kilometer
    ld latency = distance * 0.005;

    return latency; 
}


namespace ns3 {

    class myNode : public Node {

    public:

        //AS properties
        uint16_t as_number;
        ld latency_coef, bandwidth_coef,  diversity_coef;
	int32_t AS_max_bwd;

	// Interfaces Properties *****************************************************************************************************
	std::vector<uint16_t> neighbors;
        std::unordered_map<uint16_t, std::vector<uint16_t> > interfaces_per_neighbor_as;

	std::vector<std::pair<ld, ld> > interfaces_coordinates;
        std::vector<std::vector<ld> > intra_as_latencies;
        std::vector<int32_t > inter_as_bwds;

        // beacon store structures ***************************************************************************************************
        std::unordered_map <uint16_t, beacons_with_same_src_as* > beacon_store;
        std::unordered_map <std::string, beacon*> beacon_existence_check_map;

        std::unordered_map <uint32_t, std::set<uint16_t> > ASes_on_advertised_paths;//key = Src AS | Dst AS, value = set of ASes on all advertised path to Dst AS from Src AS

        // helper structures ******************************************************************************************************** 
        std::unordered_map <uint16_t, uint64_t> next_round_valid_beacons_per_src_as_counters;

        // statistics ***************************************************************************************************************
        std::unordered_map <uint16_t, uint64_t> valid_beacons_per_src_as_counters;
        uint64_t *beacons_sent_per_link;
        std::list<uint64_t> beacons_sent_per_period;
        uint64_t sent_count = 0;


        myNode(uint16_t as_number, uint32_t system_id, ld latency_coef, ld bandwidth_coef, ld diversity_coef) : Node(system_id), as_number (as_number), latency_coef(latency_coef), bandwidth_coef(bandwidth_coef), diversity_coef(diversity_coef){}

        void do_initializations() {
            beacons_sent_per_link = new uint64_t[GetNDevices()];
            intra_as_latencies.resize(GetNDevices());

            for (uint64_t i = 0; i < GetNDevices(); ++i) {
                beacons_sent_per_link[i] = 0;
                intra_as_latencies.at(i).resize(GetNDevices());
            }

            for (uint32_t i = 0; i < GetNDevices(); ++i) {	    
                for (uint32_t j = i + 1; j < GetNDevices(); ++j) {
                        intra_as_latencies.at(i).at(j) = calculate_latency(interfaces_coordinates.at(i).first, interfaces_coordinates.at(i).second,
					                                   interfaces_coordinates.at(j).first, interfaces_coordinates.at(j).second);
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



        void UpdateBeaconStoreAndCountersBeforeBeaconing()
        {
            now = Simulator::Now().ToInteger(Time::NS);

            if (as_number == 0) {
                std::cout << now << std::endl;
            }
      

            for (auto const & pair:beacon_existence_check_map) {
    		beacon *the_beacon = pair.second;
                if (the_beacon->is_new) {
                    the_beacon->is_new = false;

                    if (the_beacon->next_expiration_time > now) {

                        if (!the_beacon->is_valid) {
                            if (valid_beacons_per_src_as_counters.find(*the_beacon->the_path->at(0)) != valid_beacons_per_src_as_counters.end()) {
                                valid_beacons_per_src_as_counters.at(*the_beacon->the_path->at(0)) = valid_beacons_per_src_as_counters.at(*the_beacon->the_path->at(0)) + 1;
                            } else {
                                valid_beacons_per_src_as_counters.insert(std::make_pair(*the_beacon->the_path->at(0), 1));
                            }
                        }

                        the_beacon->is_valid = true;
                        the_beacon->initiation_time = the_beacon->next_initiation_time;
                        the_beacon->expiration_time = the_beacon->next_expiration_time;
                    }
                }

                if (the_beacon->expiration_time <= now && the_beacon->is_valid) {
                    the_beacon->is_valid = false;
                    valid_beacons_per_src_as_counters.at(*the_beacon->the_path->at(0)) = valid_beacons_per_src_as_counters.at(*the_beacon->the_path->at(0)) - 1;
                    next_round_valid_beacons_per_src_as_counters.at(*the_beacon->the_path->at(0)) = next_round_valid_beacons_per_src_as_counters.at(*the_beacon->the_path->at(0)) - 1;
                }
            }

        }

        void UpdateCountersAfterBeaconing()
        {

            uint64_t tmp_sent_count = 0;
            for (uint64_t i = 0; i < GetNDevices(); ++i) {
                tmp_sent_count += beacons_sent_per_link[i];
            }

            beacons_sent_per_period.push_back(tmp_sent_count - sent_count);

            sent_count = tmp_sent_count;
        }

        void DoBeaconing()
        {

            std::cout << valid_beacons_per_src_as_counters.size() << std::endl;
            now = Simulator::Now().ToInteger(Time::NS);

#pragma omp parallel for
            for (uint32_t idx = 0; idx < neighbors.size(); ++idx) { // Per destination AS
		uint16_t dst_as_number = neighbors.at(idx);

                for (auto const & beacon_store_entry : beacon_store) { // Per source AS
                    uint16_t src_as_number = beacon_store_entry.first;
                    beacons_with_same_src_as* equal_src_as_beacons = beacon_store_entry.second;
                    if (dst_as_number == src_as_number) {
                        continue;
                    }


                    std::map <int64_t, std::vector<std::tuple<beacon*, uint16_t, uint16_t, Ptr<myNode>, ld, ld > > > beacons_ifaces_matchings_scores;
                    int32_t total_path = 0;

                    for (auto const& beacons_received_on_same_interface : *equal_src_as_beacons) { // each iface from which a beacon received
                        uint16_t from_if = beacons_received_on_same_interface.first;
			for (auto const& the_beacon : *beacons_received_on_same_interface.second) { // for each beacon from same source AS and interface
		            if (the_beacon->is_valid == false) {
			        continue;
			    }
		            bool generates_loop = false;
                            for (auto const & link_info : *the_beacon->the_path) { // remove loops
				if (*link_info == dst_as_number) {
                                    generates_loop = true;
                                    break;
                                }
                            }

                            if (generates_loop) {
                                continue;
                            }

                            for (auto const & src_if_index : interfaces_per_neighbor_as.at(dst_as_number)) {
				Ptr<PointToPointNetDevice> device_to_send_to =  DynamicCast<PointToPointNetDevice> (GetDevice(src_if_index));
                                //assert(src_if_index == (uint16_t) device_to_send_to->GetIfIndex());

                                Ptr<PointToPointChannel> channel = DynamicCast<PointToPointChannel> (device_to_send_to->GetChannel());
                                uint32_t wire = device_to_send_to == channel->GetSource (0) ? 0 : 1;
                                Ptr<PointToPointNetDevice> dst = channel->GetDestination (wire);

                                uint16_t dst_if_index = (uint16_t)  dst->GetIfIndex();
                                //assert(dst_as_number == (DynamicCast<myNode> (dst->GetNode ()))->as_number);
                                Ptr<myNode> dst_as = (DynamicCast<myNode> (dst->GetNode ()));

                                ld latency =  the_beacon->latency_stat + intra_as_latencies.at(from_if).at(src_if_index);

                                ld bwd = the_beacon->bwd_stat;

                                if (bwd > (double) inter_as_bwds.at(src_if_index)) {
                                    bwd = (double) inter_as_bwds.at(src_if_index);
                                }

                                double score = (1 - latency/1000) * dst_as->latency_coef + (bwd / 400) * dst_as->bandwidth_coef;

                                if (beacons_ifaces_matchings_scores.find(score) != beacons_ifaces_matchings_scores.end()) {
                                    beacons_ifaces_matchings_scores.at(score).push_back(std::make_tuple(the_beacon, src_if_index, dst_if_index, dst_as, latency, bwd));
                                } else {
                                    std::vector<std::tuple<beacon*, uint16_t, uint16_t, Ptr<myNode>, double, double > > v;
                                    v.push_back(std::make_tuple(the_beacon, src_if_index, dst_if_index, dst_as, latency, bwd));
                                    beacons_ifaces_matchings_scores.insert(std::make_pair(score, v));
                                }
                                total_path++;

                                if (total_path > FIXED_BEACONS_NUMBER_TO_SEND) {
			            beacons_ifaces_matchings_scores.begin()->second.pop_back();
                                    if (beacons_ifaces_matchings_scores.begin()->second.empty()) {
                                        beacons_ifaces_matchings_scores.erase(beacons_ifaces_matchings_scores.begin());
                                    }
                                    total_path--;
                                }
                            }
                        }
                    }


                    for (auto const & score_vector_pair : beacons_ifaces_matchings_scores) {
                        for (auto const & the_tuple : score_vector_pair.second) {
                            beacon* the_beacon;
                            uint16_t dst_if_index;
                            uint16_t src_if_index;
                            Ptr<myNode> dst_as;
                            double latency;
                            double bwd;


                            std::tie (the_beacon, src_if_index, dst_if_index, dst_as, latency, bwd) = the_tuple;

                            GenerateBeaconAndSend (the_beacon, src_if_index, dst_as_number, dst_if_index, dst_as, latency, bwd);
                        }
                    }
                }
            }

#pragma omp parallel for
            for (uint32_t i = 0; i < neighbors.size(); ++i) {
		uint16_t dst_as_number = neighbors.at(i);
                for (auto const & src_if_index : interfaces_per_neighbor_as.at(dst_as_number)) {
		    
		    Ptr<PointToPointNetDevice> device_to_send_to =  DynamicCast<PointToPointNetDevice> (GetDevice(src_if_index));
                    //uint16_t src_if_index = device_to_send_to->GetIfIndex();

                    Ptr<PointToPointChannel> channel = DynamicCast<PointToPointChannel> (device_to_send_to->GetChannel());
                    uint32_t wire = device_to_send_to == channel->GetSource (0) ? 0 : 1;
                    Ptr<PointToPointNetDevice> dst = channel->GetDestination (wire);

                    uint16_t dst_if_index = (uint16_t)  dst->GetIfIndex();
                    //uint16_t dst_as_number = (DynamicCast<myNode> (dst->GetNode ()))->as_number;
                    Ptr<myNode> dst_as = (DynamicCast<myNode> (dst->GetNode ()));

                    GenerateBeaconAndSend (NULL, src_if_index, dst_as_number, dst_if_index, dst_as, inter_as_latencies.at(src_if_index), inter_as_bwds.at(src_if_index));
                }
	    }



            UpdateCountersAfterBeaconing();

        }

        void GenerateBeaconAndSend (beacon* old_beacon, uint16_t src_if_index, uint16_t dst_as_number, uint16_t dst_if_index, Ptr<myNode> dst_as,
                                    double latency, double bwd) {
            beacons_sent_per_link[src_if_index]++;

            uint16_t src_as;
            std::string key;

            if (old_beacon == NULL){
                src_as = as_number;
            } else {
                key = old_beacon->key;
                src_as = *old_beacon->the_path->at(0);
            }

            key = key + std::string((char*) &as_number, 2) + std::string((char*) &src_if_index, 2);

            if (dst_as->beacon_existence_check_map.find(key) != dst_as->beacon_existence_check_map.end()) {
                if (old_beacon == NULL) {
                    dst_as->beacon_existence_check_map.at(key)->next_initiation_time = now;
                    dst_as->beacon_existence_check_map.at(key)->next_expiration_time = now + expiration_period;
                } else {
                    dst_as->beacon_existence_check_map.at(key)->next_initiation_time = old_beacon->initiation_time;
                    dst_as->beacon_existence_check_map.at(key)->next_expiration_time = old_beacon->expiration_time;
                }
                dst_as->beacon_existence_check_map.at(key)->is_new = true;
                return;
            }

            if (dst_as->next_round_valid_beacons_per_src_as_counters.find(src_as) != dst_as->next_round_valid_beacons_per_src_as_counters.end()) {
                if (dst_as->next_round_valid_beacons_per_src_as_counters.at(src_as) >= FIXED_BEACONS_NUMBER_TO_STORE) {
                    return;
                }

                dst_as->next_round_valid_beacons_per_src_as_counters.at(src_as) = dst_as->next_round_valid_beacons_per_src_as_counters.at(src_as) + 1;

            } else {
                dst_as->next_round_valid_beacons_per_src_as_counters.insert(std::make_pair(src_as, 1));
            }

            beacon* new_beacon = new beacon;
            path*   new_path = new path;
            new_beacon->the_path = new_path;
            new_beacon->bwd_stat = bwd;
            new_beacon->latency_stat = latency;

            uint16_t* link_info = new uint16_t[4];
            link_info[0] = as_number;
            link_info[1] = src_if_index;
            link_info[2] = dst_as_number;
            link_info[3] = dst_if_index;

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
                new_beacon->next_expiration_time = old_beacon->expiration_time;;

                *new_path = *(old_beacon->the_path);
            }

            new_path->push_back(link_info);
            dst_as->beacon_existence_check_map.insert(std::make_pair(key, new_beacon));

            if (dst_as->beacon_store.find(src_as) != dst_as->beacon_store.end() && dst_as->beacon_store.at(src_as)->find(dst_if_index) != dst_as->beacon_store.at(src_as)->end()) {
                dst_as->beacon_store.at(src_as)->at(dst_if_index)->push_back(new_beacon);
            } else if (dst_as->beacon_store.find(src_as) != dst_as->beacon_store.end() && dst_as->beacon_store.at(src_as)->find(dst_if_index) == dst_as->beacon_store.at(src_as)->end()) {
                dst_as->beacon_store.at(src_as)->insert(std::make_pair(dst_if_index, new beacons_from_same_if));
                dst_as->beacon_store.at(src_as)->at(dst_if_index)->push_back(new_beacon);
            } else {
                dst_as->beacon_store.insert(std::make_pair(src_as, new beacons_with_same_src_as));
                dst_as->beacon_store.at(src_as)->insert(std::make_pair(dst_if_index, new beacons_from_same_if));
                dst_as->beacon_store.at(src_as)->at(dst_if_index)->push_back(new_beacon);
            }
        }


	ld calculate_diversity_score(beacon* the_beacon) {
            int16_t src_as = *the_beacon->the_path->at(0);
            beacons_with_same_src_as *equal_scr_as_beacons = beacon_store.at(src_as);
            for(auto const & )

	}


	void EvaluatePaths(srd::map<ld, uint64_t>& satisfaction_stat) {
            for (auto const & pair:beacon_existence_check_map){
	        beacon *the_beacon = pair.second;
		if (the_beacon->is_valid) {
                  ld diversity_score = calculate_diversity_score (the_beacon);
		  ld latency_score = 1 - the_beacon->latency_stat / 1000;
		  ld bwd_score = the_beacon->bwd_stat / AS_max_bwd;
                  
                  ld score = (latency_score * latency_coef + bwd_score * bwd_coef + diversity_score * diversity_coef) / (latency_coef + bwd_coef + diversity_coef);		  
		  if (satisfaction_stat.find(score) != satisfaction_stat.end()) {
                      satisfaction_stat.at(score)++;
		  } else {
                      satisfaction_stat.insert(std::make_pair(score, 1));
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

        if(it != this->properties.end())
            return it->second;
        else
            exit(1);

    }


    void setProperty(const std::string &name, const std::string &value) {
        this->properties[name] = value;
    }


    bool hasProperty(const std::string &name) const {
        propertiesType::const_iterator it = this->properties.find(name);

        if(it == this->properties.end()) {
            return false;
        } else {
            return true;
        }

    }


private:
    typedef std::map <std::string, std::string> propertiesType;
    propertiesType properties;

};


std::string getAttribute(rapidxml::xml_node<>* node, const std::string &name) {
    rapidxml::xml_attribute<> *attr = node->first_attribute(name.c_str());
    if(attr) {
        return attr->value();
    } else {
        return std::string();
    }
}

PropertyContainer parseProperties(rapidxml::xml_node<>* node) {
    PropertyContainer p;
    rapidxml::xml_node<>* curNode = node->first_node("property");

    while(curNode) {
        std::string name = getAttribute(curNode, "name");
        if(name != "") {
            p.setProperty(name, curNode->value());
        }
        curNode = curNode->next_sibling("property");
    }

    return p;
}


void ProcessReceivedPacketsParallel (NodeContainer nodes) {
    uint32_t node_number = nodes.GetN();

#pragma omp parallel for
    for (uint32_t i = 0; i < node_number; ++i) {
        DynamicCast<myNode> (nodes.Get(i))->UpdateBeaconStoreAndCountersBeforeBeaconing();
    }
}

int
main (int argc, char *argv[])
{

    beaconing_period = Time(argv[1]);
    expiration_period = Time(argv[2]).ToInteger(Time::NS);
    std::string file = "/home/tabaeias/workspace/ns-3-allinone/ns-3-dev/topology/" + std::string(argv[4]) + ".xml";

    std::ifstream fin(file.c_str());
    std::ostringstream sstr;
    sstr << fin.rdbuf();

    std::string out_path = "/home/tabaeias/workspace/ns-3-allinone/ns-3-dev/results/criteria-matching_" + std::string(argv[4]) +  "_" + std::string(argv[1]) + "_" + std::string(argv[2]) + "_" + std::string(argv[3]) + ".txt";
    std::ofstream out(out_path);
    std::cout.rdbuf(out.rdbuf());

    sstr.flush();
    fin.close();

    std::string xmlData = sstr.str();
    rapidxml::xml_document<> doc;
    doc.parse<0> (&xmlData[0]);

    rapidxml::xml_node<> *rootNode = doc.first_node("topology");
    rapidxml::xml_node<> *curNode;

    if(!rootNode) {
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
	ld bandwidth_coef =  std::stod(p.getProperty("bandwidth_coef"));
	ld availability_coef =  std::stod(p.getProperty("availability_coef"));
	
	nodes.Add(CreateObject <myNode>(node_counter, 0, latency_coef, bandwidth_coef, availability_coef));

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

        Ptr<Node> fromNode;
        Ptr<Node> toNode;

        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            if ((DynamicCast<myNode> (nodes.Get(i)))->as_number == ASes.at(to)) {
                toNode = nodes.Get(i);
                break;
            }

        }

        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            if ((DynamicCast<myNode> (nodes.Get(i)))->as_number == ASes.at(from)) {
                fromNode = nodes.Get(i);
                break;
            }
        }

        PointToPointHelper helper;
        helper.Install(fromNode, toNode);

        Ptr<myNode> to_my_node = (DynamicCast<ns3::myNode> (toNode));
        Ptr<myNode> from_my_node = (DynamicCast<ns3::myNode> (fromNode));

        to_my_node->interfaces_coordinates.push_back(std::pair<ld, ld> (latitude, longitude));
	from_my_node->interfaces_coordinates.push_back(std::pair<ld, ld> (latitude, longitude));

        to_my_node->inter_as_bwds.push_back(bwd);
        from_my_node->inter_as_bwds.push_back(bwd);

        if (to_my_node->interfaces_per_neighbor_as.find(from_my_node->as_number) != to_my_node->interfaces_per_neighbor_as.end()) {
            to_my_node->interfaces_per_neighbor_as.at(from_my_node->as_number).push_back(to_my_node->GetNDevices() - 1);
        } else {
            std::vector<uint16_t> tmp;
            tmp.push_back((uint16_t)to_my_node->GetNDevices() - 1);
            to_my_node->interfaces_per_neighbor_as.insert(std::make_pair(from_my_node->as_number, tmp));
            to_my_node->neighbors.push_back(from_my_node->as_number);
	}


        if (from_my_node->interfaces_per_neighbor_as.find(to_my_node->as_number) != from_my_node->interfaces_per_neighbor_as.end()) {
            from_my_node->interfaces_per_neighbor_as.at(to_my_node->as_number).push_back(from_my_node->GetNDevices() - 1);
        } else {
            std::vector<uint16_t> tmp;
            tmp.push_back((uint16_t)from_my_node->GetNDevices() - 1);
            from_my_node->interfaces_per_neighbor_as.insert(std::make_pair(to_my_node->as_number, tmp));
            from_my_node->neighbors.push_back(to_my_node->as_number);
	}

        curNode = curNode->next_sibling("link");
    }

    
    for (uint64_t i = 0; i < nodes.GetN(); ++i) {
        DynamicCast<myNode> (nodes.Get(i))->do_initializations();
    }



    Time t = Seconds(0.0);
    while (t < Time(argv[3])) {
        Simulator::Schedule (t + Seconds(30.0), &ProcessReceivedPacketsParallel, nodes);

        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            Ptr<myNode> the_node = DynamicCast<myNode> (nodes.Get(i));
            Simulator::Schedule (t, &myNode::DoBeaconing, the_node);
        }
        t = t + beaconing_period;
    }



    Simulator::Stop (Time(argv[3]));
    Simulator::Run ();

    std::map <uint64_t, uint64_t> beacons_per_link_distribution;

    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        for (uint32_t j = 0; j < nodes.Get(i)->GetNDevices(); ++j) {
            uint64_t the_count = DynamicCast<myNode> (nodes.Get(i))->beacons_sent_per_link[j];
            if (beacons_per_link_distribution.find(the_count) == beacons_per_link_distribution.end()) {
                beacons_per_link_distribution.insert(std::make_pair(the_count, 1));
            } else {
                beacons_per_link_distribution.at(the_count) = beacons_per_link_distribution.at(the_count) + 1;
            }
        }
    }


    for (auto const & counter_pair:beacons_per_link_distribution) {
        std::cout << counter_pair.first << "\t" << counter_pair.second << std::endl;
    }

    Time t2 = Seconds(0.0);
    while (t2 < Time(argv[3])) {
        uint64_t counter_per_period = 0;
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            counter_per_period += DynamicCast<myNode> (nodes.Get(i))->beacons_sent_per_period.front();
            DynamicCast<myNode> (nodes.Get(i))->beacons_sent_per_period.pop_front();
        }

        std::cout << t2 << "\t" << counter_per_period << std::endl;

        t2 = t2 + beaconing_period;
    }

    std::map<uint64_t, uint64_t> hop_path_counters;
    for (int j = 1; j <= 3; ++j){
        std::cout << "######################################################################################################################################" << std::endl;
        hop_path_counters.clear();
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            for (auto const & src_as_beacons_pair:DynamicCast<myNode> (nodes.Get(i))->beacon_store) {
                int32_t hop_path = 0;
                if (src_as_beacons_pair.second->find(j) != src_as_beacons_pair.second->end()) {
                    hop_path = src_as_beacons_pair.second->at(j)->size();
                }

                if (hop_path_counters.find(hop_path) != hop_path_counters.end()) {
                    hop_path_counters.at(hop_path) = hop_path_counters.at(hop_path) + 1;
                } else {
                    hop_path_counters.insert(std::make_pair(hop_path, 1));
                }
            }
        }

        for (auto const & counters_pair : hop_path_counters) {
            std::cout << counters_pair.first << "\t" << counters_pair.second << std::endl;
        }
    }



    /*  for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        for (uint32_t j = 0; j < nodes.GetN(); ++j) {
        if (DynamicCast<myNode> (nodes.Get(i))->beacon_store.find(j) == DynamicCast<myNode> (nodes.Get(i))->beacon_store.end()) {
        std::cout << "0" << "\t";
        continue;
        }
        std::cout << DynamicCast<myNode> (nodes.Get(i))->beacon_store.at(j)->at(0)->size() << "\t";
        }

        std::cout << std::endl;
        }*/

    Simulator::Destroy ();
    return 0;
}
