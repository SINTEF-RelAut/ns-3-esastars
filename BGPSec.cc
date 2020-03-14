//
// Created by seyedali on 05.03.20.
//

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/rapidxml.hpp"
#include <vector>
#include <stdio.h>
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

using namespace ns3;

#define SELF_CUSTOMER_OF_REMOTE 0
#define SELF_PROVIDER_OF_REMOTE 1
#define PEER -1

typedef uint32_t prefix_t;
typedef std::vector<uint16_t> path_t;

typedef uint16_t  as_number_t;
typedef int8_t relation_t;
typedef uint16_t interface_idx_t;

struct update_message_t {
    path_t *path;
    prefix_t prefix;
    Time initiation_time, expiration_time;
};


typedef std::vector<update_message_t *> beacons_with_equal_length;
typedef std::unordered_map<uint16_t, beacons_with_equal_length *> beacons_with_same_src_as;


Time advertisement_period;
Time expiration_period;

double_t calculate_great_circle_latency(double_t lat1_deg, double_t long1_deg, double_t lat2_deg, double_t long2_deg) {
    double_t lat1 = lat1_deg * (M_PI) / 180;
    double_t long1 = long1_deg * (M_PI) / 180;
    double_t lat2 = lat2_deg * (M_PI) / 180;
    double_t long2 = long2_deg * (M_PI) / 180;

    // Haversine Formula
    double_t dlong = long2 - long1;
    double_t dlat = lat2 - lat1;

    double_t distance = 6371 * 2 * asin(sqrt(pow(sin(dlat / 2), 2) + cos(lat1) * cos(lat2) * pow(sin(dlong / 2), 2)));

    // 5000 nanoseconds of latency per kilometer
    double_t latency = distance * 5000;

    return latency;
}

namespace ns3 {

    class myNode : public Node {

    public:

        //AS properties
        as_number_t as_number;
        std::vector<prefix_t> own_prefixes;

        // Interfaces Properties *****************************************************************************************************
        std::vector<as_number_t> neighbors;
        std::unordered_map<as_number_t, relation_t> relations;
        std::unordered_map<as_number_t, std::vector<interface_idx_t> > interfaces_per_neighbor_as;

        std::vector<std::pair<double_t, double_t > > interfaces_coordinates;
        std::vector<std::vector<double_t > > intra_as_latencies;
        // update_message_t store structures ***************************************************************************************************
        std::unordered_map<prefix_t, update_message_t*> discovered_prefixes;

        // statistics ***************************************************************************************************************
        std::unordered_map<int64_t, std::vector<uint64_t> > bytes_sent_per_interface_per_period;

        myNode(as_number_t as_number, uint32_t system_id, uint32_t* counter) : Node(system_id), as_number(as_number) {
            for (int i = 0; i < 100; ++i) {
                own_prefixes.push_back(*counter);
                (*counter)++;
            }
        }

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
                                                                                    interfaces_coordinates.at(
                                                                                            j).second);
                    intra_as_latencies.at(j).at(i) = intra_as_latencies.at(i).at(j);
                }
            }
        }

        void advertise_prefixes() {
            update_message_t new_update_message;
            path_t new_path;
            new_update_message.path = &new_path;
            new_update_message.initiation_time = Simulator::Now();
            new_update_message.expiration_time = Simulator::Now() + expiration_period;

            for (auto const & prefix : own_prefixes) {
                new_update_message.prefix = prefix;

                for (uint32_t i = 0; i < neighbors.size(); ++i) {
                    as_number_t remote_as_no = neighbors.at(i);
                    for (auto const & self_egress_if_no : interfaces_per_neighbor_as.at(remote_as_no)) {
                        Ptr<PointToPointNetDevice> self_egress_device = DynamicCast<PointToPointNetDevice>(
                            GetDevice(self_egress_if_no));

                        Ptr<PointToPointChannel> channel = DynamicCast<PointToPointChannel>(
                            self_egress_device->GetChannel());
                        uint32_t wire = self_egress_device == channel->GetSource(0) ? 0 : 1;
                        Ptr<PointToPointNetDevice> remote_device = channel->GetDestination(wire);

                        interface_idx_t remote_if_no = (uint16_t) remote_device->GetIfIndex();

                        Ptr<myNode> remote_as = (DynamicCast<myNode>(remote_device->GetNode()));

                        remote_as->receive_update_message(&new_update_message, as_number, remote_if_no);

                    }
                }
            }
        }

        void receive_update_message (update_message_t* update_message, as_number_t previous_as_no, interface_idx_t self_ingress_if_idx) {

	    if (Simulator::Now() > 0) {
		    std::cout << Simulator::Now() << std::endl;
	    }
//   	    try {
//                bytes_sent_per_interface_per_period.at(Simulator::Now().ToInteger(Time::S)).at(self_ingress_if_idx) += 400;
//            } catch (std::out_of_range) {
//                bytes_sent_per_interface_per_period.insert(std::make_pair(Simulator::Now().ToInteger(Time::S), std::vector <uint64_t >(GetNDevices(), 0)));
//                bytes_sent_per_interface_per_period.at(Simulator::Now().ToInteger(Time::S)).at(self_ingress_if_idx) += 400;
//            }

            prefix_t prefix = update_message->prefix;

            if (discovered_prefixes.find(prefix) != discovered_prefixes.end()) {
                if (discovered_prefixes.at(prefix)->expiration_time <= Simulator::Now()) {
                    if (update_message->expiration_time > Simulator::Now()) {
                        discovered_prefixes.at(prefix)->initiation_time = update_message->initiation_time;
                        discovered_prefixes.at(prefix)->expiration_time = update_message->expiration_time;
                        discovered_prefixes.at(prefix)->path->assign(update_message->path->begin(), update_message->path->end());
                        discovered_prefixes.at(prefix)->path->push_back(previous_as_no);
                        disseminate_prefix(discovered_prefixes.at(prefix), previous_as_no, self_ingress_if_idx);
                        return;
                    }

                    update_message_t *tmp = discovered_prefixes.at(prefix);
                    discovered_prefixes.erase(prefix);
                    free(tmp->path);
		            free(tmp);
                    return;

                }

                if (update_message->expiration_time <= Simulator::Now()) {
                    return;
                }

                if (update_message->path->size() < discovered_prefixes.at(prefix)->path->size()) {
                    discovered_prefixes.at(prefix)->initiation_time = update_message->initiation_time;
                    discovered_prefixes.at(prefix)->expiration_time = update_message->expiration_time;
                    discovered_prefixes.at(prefix)->path->assign(update_message->path->begin(), update_message->path->end());
                    discovered_prefixes.at(prefix)->path->push_back(previous_as_no);
                    disseminate_prefix(discovered_prefixes.at(prefix), previous_as_no, self_ingress_if_idx);
                    return;
                }

                if (discovered_prefixes.at(prefix)->path->size() == update_message->path->size()
                    && discovered_prefixes.at(prefix)->initiation_time < update_message->initiation_time) {
                    discovered_prefixes.at(prefix)->initiation_time = update_message->initiation_time;
                    discovered_prefixes.at(prefix)->expiration_time = update_message->expiration_time;
                    discovered_prefixes.at(prefix)->path->assign(update_message->path->begin(), update_message->path->end());
                    discovered_prefixes.at(prefix)->path->push_back(previous_as_no);
                    disseminate_prefix(discovered_prefixes.at(prefix), previous_as_no, self_ingress_if_idx);
                    return;
                }

                return;
            } else {

                discovered_prefixes.insert(std::make_pair(prefix, new update_message_t));
                discovered_prefixes.at(prefix)->path = new path_t(update_message->path->begin(), update_message->path->end());
                discovered_prefixes.at(prefix)->initiation_time = update_message->initiation_time;
                discovered_prefixes.at(prefix)->expiration_time = update_message->expiration_time;
                discovered_prefixes.at(prefix)->path->push_back(previous_as_no);
                disseminate_prefix(discovered_prefixes.at(prefix), previous_as_no, self_ingress_if_idx);
                return;
            }
        }

        void disseminate_prefix (update_message_t* update_message, as_number_t previous_as_no, interface_idx_t self_ingress_if_idx) {
            relation_t relation_with_previous_as = relations.at(previous_as_no);
            for (uint32_t i = 0; i < neighbors.size(); ++i) {
                as_number_t next_as_no = neighbors.at(i);
                if (next_as_no == previous_as_no) {
                    continue;
                }

                relation_t relation_with_next_as = relations.at(next_as_no);
                if (relation_with_previous_as == PEER && relation_with_next_as != SELF_PROVIDER_OF_REMOTE) {
                    continue;
                }

                if (relation_with_previous_as == SELF_CUSTOMER_OF_REMOTE && relation_with_next_as != SELF_PROVIDER_OF_REMOTE) {
                    continue;
                }

                bool generates_loop = false;
                for (auto const &as_on_path : *update_message->path) { // remove loops
                    if (as_on_path == next_as_no) {
                        generates_loop = true;
                        break;
                    }
                }

                if (generates_loop) {
                    continue;
                }

                this->send_to_all_interfaces_with_same_remote_as (update_message, self_ingress_if_idx, next_as_no);
            }
        }

        void send_to_all_interfaces_with_same_remote_as(update_message_t* update_message, interface_idx_t self_ingress_if_idx,
                                                        as_number_t next_as_no) {
            for (auto const &self_egress_if_no : interfaces_per_neighbor_as.at(next_as_no)) {
                Ptr<PointToPointNetDevice> self_egress_device = DynamicCast<PointToPointNetDevice>(
                        GetDevice(self_egress_if_no));

                Ptr<PointToPointChannel> channel = DynamicCast<PointToPointChannel>(
                        self_egress_device->GetChannel());
                uint32_t wire = self_egress_device == channel->GetSource(0) ? 0 : 1;
                Ptr<PointToPointNetDevice> remote_device = channel->GetDestination(wire);

                uint16_t remote_ingress_if_no = (uint16_t) remote_device->GetIfIndex();
                Ptr<myNode> remote_as = (DynamicCast<myNode>(remote_device->GetNode()));

                Simulator::Schedule(NanoSeconds(intra_as_latencies.at(self_ingress_if_idx).at(self_egress_if_no)) ,
                                    &ns3::myNode::receive_update_message,
                                    remote_as,
                                    update_message, as_number, remote_ingress_if_no);
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


int
main(int argc, char *argv[]) {

    advertisement_period = Time(argv[1]);
    expiration_period = Time(argv[2]);
    std::string file = "/home/tabaeias/workspace/ns-3-allinone/ns-3-dev/topology/" + std::string(argv[4]) + ".xml";

    std::ifstream fin(file.c_str());
    std::ostringstream sstr;
    sstr << fin.rdbuf();

    std::string out_path =
            "/home/tabaeias/workspace/ns-3-allinone/ns-3-dev/results/BGPSec_" + std::string(argv[4]) + "_" +
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
    uint32_t prefix_counter = 0;
    while (curNode) {
        int32_t as_number = std::stoi(getAttribute(curNode, "id"));

        nodes.Add(CreateObject<myNode>(node_counter, 0, &prefix_counter));
        ASes.insert(std::make_pair(as_number, node_counter));
        node_counter++;
        curNode = curNode->next_sibling("node");
    }

    curNode = rootNode->first_node("link");
    while (curNode) {
        int32_t from = std::stoi(curNode->first_node("from")->value());
        int32_t to = std::stoi(curNode->first_node("to")->value());
        
        PropertyContainer p = parseProperties(curNode);
        std::string relation = p.getProperty("rel");
        double_t latitude = std::stod(p.getProperty("latitude"));
        double_t longitude = std::stod(p.getProperty("longitude"));

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

        Ptr<myNode> from_my_node = (DynamicCast<ns3::myNode>(fromNode));
        Ptr<myNode> to_my_node = (DynamicCast<ns3::myNode>(toNode));

        from_my_node->interfaces_coordinates.push_back(std::pair<double_t , double_t >(latitude, longitude));
        to_my_node->interfaces_coordinates.push_back(std::pair<double_t , double_t >(latitude, longitude));

        if (from_my_node->interfaces_per_neighbor_as.find(to_my_node->as_number) !=
            from_my_node->interfaces_per_neighbor_as.end()) {
            from_my_node->interfaces_per_neighbor_as.at(to_my_node->as_number).push_back(
                    from_my_node->GetNDevices() - 1);
        } else {
            std::vector<uint16_t> tmp;
            tmp.push_back((uint16_t) from_my_node->GetNDevices() - 1);
            from_my_node->interfaces_per_neighbor_as.insert(std::make_pair(to_my_node->as_number, tmp));
            from_my_node->neighbors.push_back(to_my_node->as_number);
            if (relation == "peer") {
                from_my_node->relations.insert(std::make_pair(to_my_node->as_number, PEER));
            } else{
                from_my_node->relations.insert(std::make_pair(to_my_node->as_number, SELF_PROVIDER_OF_REMOTE));
            }

        if (to_my_node->interfaces_per_neighbor_as.find(from_my_node->as_number) !=
            to_my_node->interfaces_per_neighbor_as.end()) {
            to_my_node->interfaces_per_neighbor_as.at(from_my_node->as_number).push_back(to_my_node->GetNDevices() - 1);
        } else {
            std::vector<uint16_t> tmp;
            tmp.push_back((uint16_t) to_my_node->GetNDevices() - 1);
            to_my_node->interfaces_per_neighbor_as.insert(std::make_pair(from_my_node->as_number, tmp));
            to_my_node->neighbors.push_back(from_my_node->as_number);
            if (relation == "peer") {
                to_my_node->relations.insert(std::make_pair(from_my_node->as_number, PEER));
            } else{
                to_my_node->relations.insert(std::make_pair(from_my_node->as_number, SELF_CUSTOMER_OF_REMOTE));
            }

        }



        }

        curNode = curNode->next_sibling("link");
    }

    for (uint64_t i = 0; i < nodes.GetN(); ++i) {
        DynamicCast<myNode>(nodes.Get(i))->DoInitializations();
    }

    Simulator::SetScheduler(ns3::ObjectFactory(MapScheduler::GetTypeId().GetName()));

    std::random_device generator;
    std::uniform_int_distribution<int64_t> distribution(0, 500000000);
    for (Time t = Seconds(0.0); t < Time(argv[3]); t += advertisement_period) {
	    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            Ptr<myNode> the_node = DynamicCast<myNode>(nodes.Get(i));
            Simulator::Schedule(t + NanoSeconds(distribution(generator)), &myNode::advertise_prefixes, the_node);
//            Simulator::Schedule(t , &myNode::advertise_prefixes, the_node);
	    }
    }

    Simulator::Stop(Time(argv[3]));
    Simulator::Run();

    //############################################################################################################################################################
    for (Time t = Seconds(0.0); t < Time(argv[3]); t += advertisement_period) {
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

    Simulator::Destroy();
    return 0;
}
