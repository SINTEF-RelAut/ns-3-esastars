/**
 * @file main.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 *
 * This script takes the beaconing_period, expiration_period, simulator_time and the topology
 * to use as command line arguments. It will automatically instantiate nodes as Core or Leaf ASes
 * depending on the "type" property given in the xml file. This, together with the "rel" property on the links,
 * is used to infer over which interfaces the node needs to propagate beacons. All the nodes will instantiate
 * the baseline beaconing beaconServer when using this script. For criteria matching use the other script.
 * @see criteria_matching_sim
 */

#include <istream>
#include <omp.h>
#include <random>
#include <set>

#include "ns3/ptr.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-channel.h"
#include <ns3/nstime.h>

#include "src/SCION/headers/utils.h"
#include "src/SCION/headers/beaconing/beacon_server.h"
#include "src/SCION/headers/beaconing/baseline.h"
#include "src/SCION/headers/beaconing/scionlab_algo.h"
#include "src/SCION/headers/scion_as.h"
#include "src/SCION/headers/scion_core_as.h"
#include "src/SCION/headers/beaconing/criteria_matching.h"
#include "src/SCION/headers/beaconing/latency_optimized_beaconing.h"


//rapidxml::xml_node<>* SetupTopologyFile (std::string topology_name);

void InstantiateASesFromTopo(rapidxml::xml_node<>* rootNode, std::string beaconing_policy_str, std::map<int32_t, uint16_t>& ASes, std::map<uint16_t,
                             int32_t>& index_to_AS_no, ns3::NodeContainer& nodes, uint16_t expiration_period, ns3::Time beaconing_period);

void InstantiateLinksFromTopo (rapidxml::xml_node<>* rootNode, ns3::NodeContainer& nodes, std::map<int32_t, uint16_t>& ASes);

void InitializeNodesAttributes(ns3::NodeContainer& nodes, std::string beaconing_policy_str);

void ScheduleEvents(ns3::NodeContainer& nodes, ns3::Time beaconing_period, ns3::Time last_beaconing_event_time);

/**
 * @brief Called to process the beacons received in this beaconing period.
 *
 * Finalizes the beaconing period by calling UpdateBeaconStoreAndCountersBeforeBeaconing on each node.
 * Parallelized by distributing all the nodes on a few threads.
 *
 * @param nodes The ns3::NodeContainer hons3::lding all the nodes of this simulation.
 *
 * @see UpdateBeaconStoreAndCountersBeforeBeaconing
 */
void PeriodicPrints(ns3::NodeContainer nodes);

void DoFinalEvaluations(ns3::NodeContainer& nodes, std::map<int32_t, uint16_t>& ASes, std::map<uint16_t, int32_t>& index_to_AS_no,
                        uint16_t expiration_period, ns3::Time beaconing_period, ns3::Time last_beaconing_event_time);

void PrintTrafficSentFromCollectorsPerDstPerPeriod(ns3::NodeContainer& nodes, std::map<int32_t, uint16_t>& ASes, std::map<uint16_t, int32_t>& index_to_AS_no,
uint16_t expiration_period, ns3::Time beaconing_period, ns3::Time last_beaconing_event_time);

void PrintAllDiscoveredPaths(ns3::NodeContainer& nodes, std::map<int32_t, uint16_t>& ASes, std::map<uint16_t, int32_t>& index_to_AS_no);

void PrintDistributionOfPathsWithSpecificHopCount(ns3::NodeContainer& nodes);

void PrintConsumedBWAtEachPeriod(ns3::NodeContainer& nodes, ns3::Time beaconing_period, ns3::Time last_beaconing_event_time);

void PrintPathQualities(ns3::NodeContainer& nodes);

void Evaluate_S_T_Connectivity(ns3::NodeContainer& nodes);

void PrintMinimumLatencyDist(ns3::NodeContainer& nodes);

void PrintPathNoDistribution (ns3::NodeContainer& nodes);

void FindMinLatencyToDNSRootServers(ns3::NodeContainer& nodes, std::map<int32_t, uint16_t>& ASes, std::map<uint16_t, int32_t>& index_to_AS_no);

int main(int argc, char *argv[]) {
    ns3::NodeContainer nodes;
    std::map<int32_t, uint16_t> ASes;
    std::map<uint16_t, int32_t> index_to_AS_no;

    std::string beaconing_policy_str;
    std::string beaconing_period_str;
    std::string expiration_period_str;
    std::string last_beaconing_event_time_str;
    std::string topology_name;

    ns3::Time beaconing_period;
    ns3::Time last_beaconing_event_time;
    uint16_t expiration_period;

    ns3::Time simulation_end_time;

    if (argc == 6) {
        beaconing_policy_str = std::string (argv[1]);
        beaconing_period_str = std::string(argv[2]);
        expiration_period_str = std::string(argv[3]);
        last_beaconing_event_time_str = std::string(argv[4]);
        topology_name = std::string(argv[5]);
    } else {
        std::cerr << "Less arguments than expected!" << std::endl;
        return 1;
    }

    beaconing_period = ns3::Time(beaconing_period_str);
    last_beaconing_event_time = ns3::Time(last_beaconing_event_time_str);
    expiration_period = (uint16_t) ns3::Time(expiration_period_str).ToInteger(ns3::Time::MIN);

    // simulation_end_time can be something other than last_beaconing_event_time if we want to simulate other stuff as well
    simulation_end_time = last_beaconing_event_time;

    //rapidxml::xml_node<>* rootNode = SetupTopologyFile (topology_name);

    std::string file = "/cluster/home/tabaeias/ns-3_beaconing_simulator/topology/" + std::string(topology_name) + ".xml";
    std::ifstream fin(file.c_str());
    std::ostringstream sstr;
    sstr << fin.rdbuf();

    sstr.flush();
    fin.close();

    std::string xmlData = sstr.str();
    rapidxml::xml_document<> doc;
    doc.parse<0>(&xmlData[0]);

    rapidxml::xml_node<> *rootNode = doc.first_node("topology");

    if (!rootNode) {
        std::cerr << "Empty topology!" << std::endl;
        exit(1);
    }

    InstantiateASesFromTopo(rootNode, beaconing_policy_str, ASes, index_to_AS_no, nodes, expiration_period, beaconing_period);
    InstantiateLinksFromTopo(rootNode, nodes, ASes);

    InitializeNodesAttributes(nodes, beaconing_policy_str);

    // TODO: Think about how to automatically set an appropriate name, maybe in conjunction with simulator configs?
    std::string out_path =
            "/cluster/scratch/tabaeias/" + beaconing_policy_str + "_" + topology_name + "_" +
            beaconing_period_str + "_" + expiration_period_str + "_" + last_beaconing_event_time_str + ".txt";
    std::ofstream out(out_path);
    std::cout.rdbuf(out.rdbuf());

    ScheduleEvents(nodes, beaconing_period, last_beaconing_event_time);
    ns3::Simulator::Stop(simulation_end_time);
    ns3::Simulator::Run();

    DoFinalEvaluations(nodes, ASes, index_to_AS_no, expiration_period, beaconing_period, last_beaconing_event_time);

    ns3::Simulator::Destroy();

    return 0;
}

//rapidxml::xml_node<>* SetupTopologyFile (std::string topology_name) {
//    std::string file = "/home/tabaeias/ns-3_beaconing_simulator/topology/" + std::string(topology_name) + ".xml";
//    std::ifstream* fin = new std::ifstream(file.c_str());
//    std::ostringstream* sstr = new std::ostringstream();
//    *sstr << fin->rdbuf();
//
//    sstr->flush();
//    fin->close();
//
//    std::string xmlData = sstr->str();
//    rapidxml::xml_document<>* doc = new rapidxml::xml_document<>();
//    doc->parse<0>(&xmlData[0]);
//
//    rapidxml::xml_node<> *rootNode = doc->first_node("topology");
//
//    if (!rootNode) {
//        std::cerr << "Empty topology!" << std::endl;
//        exit(1);
//    }
//
//    return rootNode;
//}

void InstantiateASesFromTopo(rapidxml::xml_node<>* rootNode, std::string beaconing_policy_str, std::map<int32_t, uint16_t>& ASes, std::map<uint16_t,
        int32_t>& index_to_AS_no, ns3::NodeContainer& nodes, uint16_t expiration_period, ns3::Time beaconing_period) {
    int16_t node_counter = 0;

    rapidxml::xml_node<>* curNode = rootNode->first_node("node");
    while (curNode) {
        int32_t as_number = std::stoi(ns3::getAttribute(curNode, "id"));
        ns3::PropertyContainer p = ns3::parseProperties(curNode);

        uint16_t isd_number = 0;
        if (p.hasProperty("isd")) {
            isd_number = std::stoi(p.getProperty("isd"));
        }

        ns3::ld latency_coef = 0.0; //std::stod(p.getProperty("latency_coef"));
        ns3::ld bandwidth_coef = 0.0; //std::stod(p.getProperty("bandwidth_coef"));
        ns3::ld AS_level_diversity_coef = 0.0; // std::stod(p.getProperty("AS_level_diversity_coef"));
        ns3::ld link_level_diversity_coef = 1.0; //std::stod(p.getProperty("link_level_diversity_coef"));
        std::string type = "core"; //p.getProperty("type");

        ns3::beaconing_timing_params params = std::make_pair(beaconing_period, expiration_period);
        ns3::coefficients coefs = std::make_tuple(latency_coef, bandwidth_coef, AS_level_diversity_coef,
                                                  link_level_diversity_coef);

        ns3::BeaconServer* beaconing_policy;
        if (beaconing_policy_str == "baseline") {
            beaconing_policy = (ns3::BeaconServer*) new ns3::Baseline(params);
        } else if (beaconing_policy_str == "criteria_matching") {
            beaconing_policy = (ns3::BeaconServer *) new ns3::CriteriaMatching(params, coefs);
        } else if (beaconing_policy_str == "latency_optimized") {
            beaconing_policy = (ns3::BeaconServer *) new ns3::LatencyOptimized(params);
        } else if (beaconing_policy_str == "scionlab") {
            beaconing_policy = (ns3::BeaconServer *) new ns3::SCIONLAB(params);
        } else {
            beaconing_policy = (ns3::BeaconServer*) new ns3::Baseline(params);
        }

        ns3::Ptr<ns3::SCION_AS> node;
        if(type == "core"){
            node = ns3::CreateObject<ns3::SCION_Core_AS>(isd_number, node_counter, 0,  beaconing_policy, ns3::Time(0));
        } else if(type =="non-core"){
            node = ns3::CreateObject<ns3::SCION_AS>(isd_number, node_counter, 0,  beaconing_policy, ns3::Time(0));
        } else{
            std::cerr << "Incompatible node type!" << std::endl;
            exit(1);
        }
        nodes.Add(node);
        beaconing_policy->SetNode(node);

        ASes.insert(std::make_pair(as_number, node_counter));
        index_to_AS_no.insert(std::make_pair(node_counter, as_number));

        node_counter++;

        curNode = curNode->next_sibling("node");
    }
}

void InstantiateLinksFromTopo (rapidxml::xml_node<>* rootNode, ns3::NodeContainer& nodes, std::map<int32_t, uint16_t>& ASes){
    rapidxml::xml_node<> *curNode = rootNode->first_node("link");
    while (curNode) {
        int32_t to = std::stoi(curNode->first_node("to")->value());
        int32_t from = std::stoi(curNode->first_node("from")->value());

        ns3::PropertyContainer p = ns3::parseProperties(curNode);

        ns3::ld latitude = std::stod(p.getProperty("latitude"));
        ns3::ld longitude = std::stod(p.getProperty("longitude"));
        int32_t bwd = std::stoi(p.getProperty("capacity"));
        std::string rel = "core"; //p.getProperty("rel");
        ns3::neighbour_relation relation;

// Check for the 3 possibilities in CAIDA topology
        if (rel == "peer") {
            relation = ns3::neighbour_relation::PEER;
        } else if (rel == "core") {
            relation = ns3::neighbour_relation::CORE;
        } else if (rel == "customer") {
            relation = ns3::neighbour_relation::CUSTOMER;
        } else {
            relation = ns3::neighbour_relation::CORE;
        }

        ns3::Ptr<ns3::SCION_AS> fromNode;
        ns3::Ptr<ns3::SCION_AS> toNode;

        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            if ((ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i)))->as_number == ASes.at(to)) {
                toNode = ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i));
                break;
            }
        }

        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            if ((ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i)))->as_number == ASes.at(from)) {
                fromNode = ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i));
                break;
            }
        }

        ns3::PointToPointHelper helper;
        helper.Install(fromNode, toNode);

        ns3::Ptr<ns3::SCION_AS> to_my_node = (ns3::DynamicCast<ns3::SCION_AS>(toNode));
        ns3::Ptr<ns3::SCION_AS> from_my_node = (ns3::DynamicCast<ns3::SCION_AS>(fromNode));

        to_my_node->interfaces_coordinates.push_back(std::pair<ns3::ld, ns3::ld>(latitude, longitude));
        from_my_node->interfaces_coordinates.push_back(std::pair<ns3::ld, ns3::ld>(latitude, longitude));

        to_my_node->inter_as_bwds.push_back(bwd);
        from_my_node->inter_as_bwds.push_back(bwd);

        ns3::neighbour_relation to_rel;
        ns3::neighbour_relation from_rel;

        switch (relation) {
            case ns3::neighbour_relation::PEER:
                to_rel = ns3::neighbour_relation::PEER;
                from_rel = ns3::neighbour_relation::PEER;
                break;
            case ns3::neighbour_relation::CORE:
                to_rel = ns3::neighbour_relation::CORE;
                from_rel = ns3::neighbour_relation::CORE;
                break;
            case ns3::neighbour_relation::CUSTOMER:
                to_rel = ns3::neighbour_relation::PROVIDER;
                from_rel = ns3::neighbour_relation::CUSTOMER;
                break;
            case ns3::neighbour_relation::PROVIDER:
// Shouns3::ld never happen, there is no "Provider" type in xml files
                to_rel = ns3::neighbour_relation::CUSTOMER;
                from_rel = ns3::neighbour_relation::PROVIDER;
                assert(false);
        }

        to_my_node->interface_to_neighbor_map.insert(std::make_pair(to_my_node->GetNDevices() - 1, from_my_node->as_number));
        if (to_my_node->interfaces_per_neighbor_as.find(from_my_node->as_number) !=
            to_my_node->interfaces_per_neighbor_as.end()) {
            to_my_node->interfaces_per_neighbor_as.at(from_my_node->as_number).push_back(
                    (uint16_t) to_my_node->GetNDevices() - 1);
        } else {
            std::vector<uint16_t> tmp;
            tmp.push_back((uint16_t) to_my_node->GetNDevices() - 1);
            to_my_node->interfaces_per_neighbor_as.insert(std::make_pair(from_my_node->as_number, tmp));
            to_my_node->neighbors.push_back(std::make_pair(from_my_node->as_number, to_rel));
        }

        from_my_node->interface_to_neighbor_map.insert(std::make_pair(from_my_node->GetNDevices() - 1, to_my_node->as_number));
        if (from_my_node->interfaces_per_neighbor_as.find(to_my_node->as_number) !=
            from_my_node->interfaces_per_neighbor_as.end()) {
            from_my_node->interfaces_per_neighbor_as.at(to_my_node->as_number).push_back(
                    from_my_node->GetNDevices() - 1);
        } else {
            std::vector<uint16_t> tmp;
            tmp.push_back((uint16_t) from_my_node->GetNDevices() - 1);
            from_my_node->interfaces_per_neighbor_as.insert(std::make_pair(to_my_node->as_number, tmp));
            from_my_node->neighbors.push_back(std::make_pair(to_my_node->as_number, from_rel));
        }

        curNode = curNode->next_sibling("link");
    }
}

void InitializeNodesAttributes(ns3::NodeContainer& nodes, std::string beaconing_policy_str) {
    for (uint64_t i = 0; i < nodes.GetN(); ++i) {
        if (beaconing_policy_str == "baseline") {
            ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i))->DoInitializations();
        } else if (beaconing_policy_str == "criteria_matching") {
            ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i))->DoInitializations(nodes.GetN());
        } else if (beaconing_policy_str == "latency_optimized") {
            ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i))->DoInitializations(nodes.GetN());
        } else if (beaconing_policy_str == "scionlab") {
            ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i))->DoInitializations(nodes.GetN());
        }
    }
}

void ScheduleEvents(ns3::NodeContainer& nodes, ns3::Time beaconing_period, ns3::Time last_beaconing_event_time) {
    for (ns3::Time t = ns3::Seconds(0.0); t < last_beaconing_event_time; t += beaconing_period) {
        ns3::Simulator::Schedule(t + ns3::Seconds(1.0), &PeriodicPrints, nodes);
    }

    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        ns3::Ptr<ns3::SCION_AS> node = ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i));
        node->ScheduleBeaconing(beaconing_period, last_beaconing_event_time);
    }
}

void PeriodicPrints(ns3::NodeContainer nodes) {
    std::cout << "################################## " << ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(0))->GetBeaconServer()->GetCurrentTime() << " #########################################" << std::endl;
    uint32_t node_number = nodes.GetN();

    // print number of connected pairs after each beaconing round
    uint32_t all_connected_pairs = 0;
    for (uint32_t i = 0; i < node_number; ++i) {
        all_connected_pairs += ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i))->GetBeaconServer()->valid_beacons_count_per_dst_as.size();
    }
    std::cout << all_connected_pairs << std::endl;

}

void DoFinalEvaluations(ns3::NodeContainer& nodes, std::map<int32_t, uint16_t>& ASes, std::map<uint16_t, int32_t>& index_to_AS_no,
                        uint16_t expiration_period, ns3::Time beaconing_period, ns3::Time last_beaconing_event_time) {

//    PrintTrafficSentFromCollectorsPerDstPerPeriod(nodes, ASes, index_to_AS_no, expiration_period,  beaconing_period,  last_beaconing_event_time);
//    PrintPathNoDistribution (nodes);
//    PrintMinimumLatencyDist(nodes);
//    Evaluate_S_T_Connectivity(nodes);
//    PrintAllDiscoveredPaths(nodes, ASes, index_to_AS_no);
     FindMinLatencyToDNSRootServers(nodes, ASes, index_to_AS_no);
//    PrintConsumedBWAtEachPeriod(nodes, beaconing_period, last_beaconing_event_time);
//    PrintDistributionOfPathsWithSpecificHopCount(nodes);

//    PrintPathQualities(nodes);




}

void PrintTrafficSentFromCollectorsPerDstPerPeriod(ns3::NodeContainer& nodes, std::map<int32_t, uint16_t>& ASes, std::map<uint16_t, int32_t>& index_to_AS_no,
uint16_t expiration_period, ns3::Time beaconing_period, ns3::Time last_beaconing_event_time) {
    std::cout << "####################################### Traffic sent from each collector #######################################" << std::endl;
    std::list<int32_t> collectors({3303, 3130, 1239, 701, 5413, 34224, 7018, 53767, 3741, 31019, 22652, 2497, 57866, 37100,
                                   3130, 3257, 3549, 6939, 18106, 1299, 23673, 2914, 11537, 2152, 852, 8492, 34224, 11686});
    for (int32_t collector : collectors) {
        double_t consumed_bwd = 0.0;
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            ns3::Ptr<ns3::SCION_AS> the_node = ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i));
            if (index_to_AS_no.at(the_node->as_number) == collector) {
                double_t periods = 0.0;
                for (ns3::Time t = ns3::Time(0); t < last_beaconing_event_time; t += beaconing_period) {
                    periods += 1.0;
                    for (uint32_t if_index = 0; if_index < the_node->GetNDevices(); ++if_index) {
                        consumed_bwd += (double_t) the_node->GetBeaconServer()->bytes_sent_per_interface_per_period.at(
                                (uint16_t) t.ToInteger(ns3::Time::MIN)).at(if_index);
                    }
                }
                consumed_bwd = (double_t) consumed_bwd /
                               the_node->GetNDevices() /
                               periods;
                break;
            }
        }
        std::cout << collector << "\t" << consumed_bwd << std::endl;
    }
}

void FindMinLatencyToDNSRootServers(ns3::NodeContainer& nodes, std::map<int32_t, uint16_t>& ASes, std::map<uint16_t, int32_t>& index_to_AS_no) {
    std::string probes_file = "/cluster/scratch/tabaeias/atlas_probes.xml";
    std::ifstream fin_probes(probes_file.c_str());
    std::ostringstream probes_sstr;
    probes_sstr << fin_probes.rdbuf();
    probes_sstr.flush();
    fin_probes.close();

    std::string xmlProbesData = probes_sstr.str();
    rapidxml::xml_document<> probes_doc;
    probes_doc.parse<0>(&xmlProbesData[0]);

    rapidxml::xml_node<> *probesRootNode = probes_doc.first_node("root");
    rapidxml::xml_node<> *probesNode = probesRootNode->first_node("Probes");

    std::list<std::string> root_server_names ({"a-root", "b-root", "c-root", "d-root", "e-root", "f-root", "h-root", "j-root", "k-root", "l-root", "m-root"});
    for (auto const & root_server_name : root_server_names) {
        std::cout << "################################################## " << root_server_name << " #########################################################" << std::endl;

        std::string dns_root_file = "/cluster/scratch/tabaeias/" + root_server_name + ".xml";
        std::ifstream fin_dns_root(dns_root_file.c_str());
        std::ostringstream dns_root_sstr;
        dns_root_sstr << fin_dns_root.rdbuf();
        dns_root_sstr.flush();
        fin_dns_root.close();

        std::set<int32_t> set_of_src_ases;

        std::string xmlDNSRootData = dns_root_sstr.str();
        rapidxml::xml_document<> dns_root_doc;
        dns_root_doc.parse<0>(&xmlDNSRootData[0]);

        rapidxml::xml_node<> *dnsRootNode = dns_root_doc.first_node("root");
        int32_t dst_as_no = std::stoi(dnsRootNode->first_node("ASN")->value());

        if (ASes.find(dst_as_no) == ASes.end()) {
            std::cout << root_server_name << ": " << dst_as_no << " The root DNS server's AS is not among the top 2000 ASes" << std::endl;
            continue;
        }
        std::cout << "Probe|ASN|Latency|Distance|Probe coordinates|Path Coordinates|Instance Coordinates|ASes on Path" << std::endl;

        uint16_t dst_index = ASes.at(dst_as_no);
        ns3::Ptr<ns3::SCION_AS> dst_node = ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(dst_index));

        rapidxml::xml_node<> *currProbe = probesNode->first_node("item");
        while(currProbe) {
            int32_t src_as_no = std::stoi(currProbe->first_node("ASN")->value());
            double probe_lat = std::stod(currProbe->first_node("Latitude")->value());
            double probe_long = std::stod(currProbe->first_node("Longitude")->value());

            if (ASes.find(src_as_no) == ASes.end()) {
                currProbe = currProbe->next_sibling("item");
                continue;
            }

            set_of_src_ases.insert(src_as_no);

            ns3::Ptr<ns3::SCION_AS> src_node = ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(ASes.at(src_as_no)));

            uint16_t last_br;
            double min_latency_to_dst_as = std::numeric_limits<double>::max();
            ns3::path* selected_path = NULL;

            auto const & beacons_to_dns_root_as = src_node->GetBeaconServer()->beacon_store.at(dst_index);

            for (auto const & len_beacons_pair : beacons_to_dns_root_as) {
                auto const & same_len_beacons = len_beacons_pair.second;

                for (auto const & the_beacon : same_len_beacons) {
                    if (the_beacon->is_valid) {
                        assert(UPPER_16_BITS(the_beacon->the_path.at(0)) == dst_index);

                        uint16_t first_br = LOWER_16_BITS(the_beacon->the_path.back());
                        std::pair<double, double> first_br_coordinates = src_node->interfaces_coordinates.at(first_br);
                        double latency_from_probe_to_first_hop = ns3::calculate_great_circle_latency(probe_lat, probe_long, first_br_coordinates.first, first_br_coordinates.second);
                        if (the_beacon->latency_stat + latency_from_probe_to_first_hop < min_latency_to_dst_as) {
                            min_latency_to_dst_as = the_beacon->latency_stat + latency_from_probe_to_first_hop;
                            last_br = SECOND_UPPER_16_BITS(the_beacon->the_path.at(0));
                            selected_path = &the_beacon->the_path;
                        }
                    }
                }
            }

            double min_overall_latency = std::numeric_limits<double>::max();
            std::pair<double, double> selected_instance_coordinates;

            rapidxml::xml_node<> *sitesNode = dnsRootNode->first_node("Sites");
            rapidxml::xml_node<> *currSite = sitesNode->first_node("item");
            while(currSite) {
                double instance_lat = std::stod(currSite->first_node("Latitude")->value());
                double instance_long = std::stod(currSite->first_node("Longitude")->value());
                std::pair<double, double> last_br_coordinates = dst_node->interfaces_coordinates.at(last_br);
                double overall_latency = min_latency_to_dst_as + ns3::calculate_great_circle_latency(instance_lat, instance_long, last_br_coordinates.first, last_br_coordinates.second);
                if (overall_latency < min_overall_latency) {
                    min_overall_latency = overall_latency;
                    selected_instance_coordinates = std::pair<double, double> (instance_lat, instance_long);
                }
                currSite = currSite->next_sibling("item");
            }

            std::cout << currProbe->first_node("ID")->value() << "|" << src_as_no << "|"
           // << "(" << selected_instance_coordinates.first << ", " << selected_instance_coordinates.second << ")" << "|"
            << min_overall_latency << "|"
            << ns3::calculate_great_circle_distance(probe_lat, probe_long, selected_instance_coordinates.first, selected_instance_coordinates.second) << "|"
            << "(" << probe_lat << ", " << probe_long << ")" << "|";

            int hop_cnt = 0;
            std::vector<ns3::link_information>::reverse_iterator hop = selected_path->rbegin();
            for (; hop!= selected_path->rend(); ++hop) {
                if (hop_cnt != 0) {
                    std::cout << " ";
                }

                ns3::Ptr<ns3::SCION_AS> AS = ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(UPPER_16_BITS(*hop)));
                std::pair<double, double> br_coordinates = AS->interfaces_coordinates.at(SECOND_UPPER_16_BITS(*hop));
                std::cout << "(" << br_coordinates.first << ", " << br_coordinates.second << ")";
                hop_cnt++;
            }

            std::cout << "|" << "(" << selected_instance_coordinates.first << ", " << selected_instance_coordinates.second << ")" << "|";

            hop_cnt = 0;
            hop = selected_path->rbegin();
            for (; hop!= selected_path->rend(); ++hop) {
                if (hop_cnt != 0) {
                    std::cout << " ";
                }
                std::cout << index_to_AS_no.at(SECOND_LOWER_16_BITS(*hop));
                hop_cnt++;
            }

            std::cout << " " << index_to_AS_no.at(UPPER_16_BITS(selected_path->at(0)));

            std::cout << std::endl;


            currProbe = currProbe->next_sibling("item");

        }

        for (auto const & src_as_no : set_of_src_ases) {
            ns3::Ptr<ns3::SCION_AS> src_node = ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(ASes.at(src_as_no)));
            auto const & beacons_to_dns_root_as = src_node->GetBeaconServer()->beacon_store.at(dst_index);
            for (auto const & len_beacons_pair : beacons_to_dns_root_as) {
                auto const & same_len_beacons = len_beacons_pair.second;
                for (auto const & the_beacon : same_len_beacons) {
                    if (!the_beacon->is_valid) {
                        continue;
                    }

                    std::cout << src_as_no << "|" << dst_as_no << "|";
                    ns3::path* the_path = &the_beacon->the_path;

                    int hop_cnt = 0;
                    std::vector<ns3::link_information>::reverse_iterator hop = the_path->rbegin();
                    for (; hop!= the_path->rend(); ++hop) {
                        if (hop_cnt != 0) {
                            std::cout << " ";
                        }

                        ns3::Ptr<ns3::SCION_AS> AS = ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(UPPER_16_BITS(*hop)));
                        std::pair<double, double> br_coordinates = AS->interfaces_coordinates.at(SECOND_UPPER_16_BITS(*hop));
                        std::cout << "(" << br_coordinates.first << ", " << br_coordinates.second << ")";
                        hop_cnt++;
                    }

                    std::cout << "|";

                    hop_cnt = 0;
                    hop = the_path->rbegin();
                    for (; hop!= the_path->rend(); ++hop) {
                        if (hop_cnt != 0) {
                            std::cout << " ";
                        }
                        std::cout << index_to_AS_no.at(SECOND_LOWER_16_BITS(*hop));
                        hop_cnt++;
                    }

                    std::cout << " " << index_to_AS_no.at(UPPER_16_BITS(the_path->at(0)));

                    std::cout << std::endl;

                }
            }
        }

    }
}

void PrintAllDiscoveredPaths(ns3::NodeContainer& nodes, std::map<int32_t, uint16_t>& ASes, std::map<uint16_t, int32_t>& index_to_AS_no) {
    std::cout << "################################################ Paths Information ##############################################################" << std::endl;

    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        ns3::Ptr<ns3::SCION_AS> the_node = ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i));

        std::cout << "From: " << index_to_AS_no.at(the_node->as_number) << std::endl;

        for (auto const & dst_as_beacons_pair : the_node->GetBeaconServer()->beacon_store) {
            uint16_t dst_as = dst_as_beacons_pair.first;
            auto const& same_dst_as_beacons = dst_as_beacons_pair.second;

            std::cout << "\t" << "To: " << index_to_AS_no.at(dst_as) << std::endl;

            for (auto const & beacons_from_same_nbr : same_dst_as_beacons) {
                for (auto const & the_beacon : beacons_from_same_nbr.second) {
                    if (!the_beacon->is_valid) {
                        continue;
                    }
                    std::cout << "\t" << "\t";
                    int hop_cnt = 0;
                    std::vector<ns3::link_information>::reverse_iterator hop = the_beacon->the_path.rbegin();
                    for (; hop!= the_beacon->the_path.rend(); ++hop) {
                        if (hop_cnt != 0) {
                            std::cout << ", ";
                        }
                        std::cout << index_to_AS_no.at(SECOND_LOWER_16_BITS(*hop)) << ":" << LOWER_16_BITS(*hop) << ", " << index_to_AS_no.at(UPPER_16_BITS(*hop)) << ":" << SECOND_UPPER_16_BITS(*hop);
                        hop_cnt++;
                    }
                    std::cout << "; ";
                    std::cout << "latency = " << the_beacon->latency_stat;
                    std::cout << "; ";
                    std::cout << "BWD = " << the_beacon->bwd_stat;
                    std::cout << std::endl;
                }

            }
        }

    }
}

void PrintConsumedBWAtEachPeriod(ns3::NodeContainer& nodes, ns3::Time beaconing_period, ns3::Time last_beaconing_event_time) {
    for (ns3::Time t = ns3::Seconds(0.0); t < last_beaconing_event_time; t += beaconing_period) {
        std::cout << "####################################### frequencies of consumed bandwidth at Time "
                  << t
                  << " #######################################" << std::endl;

        std::map<uint32_t, uint32_t> frequencies_of_consumed_bwd;
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            ns3::Ptr<ns3::SCION_AS> the_node = ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i));
            for (uint32_t if_index = 0; if_index < the_node->GetNDevices(); ++if_index) {
                uint32_t consumed_bwd = the_node->GetBeaconServer()->bytes_sent_per_interface_per_period.at(t.ToInteger(ns3::Time::MIN)).at(if_index);

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
}

void PrintDistributionOfPathsWithSpecificHopCount(ns3::NodeContainer& nodes) {
    for (uint32_t path_length = 1; path_length <= 4; ++path_length) {
        std::cout
                << "######################################### frequencies of path counts per destination AS with hop count: "
                << path_length
                << "#########################################"
                << std::endl;
        std::map<uint64_t, uint64_t> frequencies_of_path_counts_per_dst_as_with_certain_length;
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            for (auto const &dst_as_beacons_pair : ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i))->GetBeaconServer()->beacon_store) {
                uint64_t number_of_paths_with_certain_length = 0;
                if(dst_as_beacons_pair.second.find(path_length) == dst_as_beacons_pair.second.end()){
                    continue;
                } else {
                    number_of_paths_with_certain_length = dst_as_beacons_pair.second.at(path_length).size();
                }

                if (frequencies_of_path_counts_per_dst_as_with_certain_length.find(
                        number_of_paths_with_certain_length) != frequencies_of_path_counts_per_dst_as_with_certain_length.end()) {
                    frequencies_of_path_counts_per_dst_as_with_certain_length.at(number_of_paths_with_certain_length)++;
                } else {
                    frequencies_of_path_counts_per_dst_as_with_certain_length.insert(
                            std::make_pair(number_of_paths_with_certain_length, 1));
                }
            }
        }

        std::cout << "path count per source AS" << "\t" << "frequency" << std::endl;
        for (auto const &count_freq_pair : frequencies_of_path_counts_per_dst_as_with_certain_length) {
            std::cout << count_freq_pair.first << "\t" << count_freq_pair.second << std::endl;
        }
    }
}

void PrintMinimumLatencyDist(ns3::NodeContainer& nodes) {
    std::cout << "###################################################### MINIMUM LATENCY####################################" << std::endl;

    std::map<float, int> distribution;
    for (uint32_t i = 0; i < nodes.GetN(); i++) {
        ns3::Ptr<ns3::SCION_AS> the_node = ns3::DynamicCast<ns3::SCION_AS> (nodes.Get(i));
        for (uint32_t j = 0; j < nodes.GetN(); ++j) {
            if (i == j) continue;
            float min_latency = std::numeric_limits<float>::max();
            for (auto const & len_beacons_pair : the_node->GetBeaconServer()->beacon_store.at(j)) {
                for (auto const & the_beacon : len_beacons_pair.second) {
                    assert(the_beacon->the_path.size() != 1 || the_beacon->latency_stat == (float) 0);
                    if (the_beacon->latency_stat < min_latency) {
                        min_latency = the_beacon->latency_stat;
                    }
                }
            }

            if (distribution.find(min_latency) == distribution.end()) {
                distribution.insert(std::make_pair(min_latency, 0));
            }
            distribution.at(min_latency)++;
        }
    }

    int cumulative_counter = 0;
    for (auto const & entry : distribution) {
        float latency = entry.first;
        distribution.at(latency) += cumulative_counter;
        cumulative_counter = distribution.at(latency);
    }

    for (auto const & entry : distribution) {
        std::cout << entry.first << "\t" << (double) entry.second / cumulative_counter << std::endl;
    }
}

void PrintPathNoDistribution (ns3::NodeContainer& nodes) {
    std::cout << "############################################# Path No Distribution ##################################" << std::endl;
    std::map<uint32_t, uint32_t> distribution;
    for (uint32_t  i = 0; i < nodes.GetN (); ++i)
        {
            ns3::Ptr<ns3::SCION_AS> node = ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i));
            for (auto const & dst_count_pair : node->GetBeaconServer()->valid_beacons_count_per_dst_as) {
                    if (distribution.find(dst_count_pair.second) == distribution.end()) {
                        distribution.insert(std::make_pair(dst_count_pair.second,  0));
                        }

                distribution.at(dst_count_pair.second) = distribution.at(dst_count_pair.second) + 1;
                }
        }

    uint32_t cumulative_counter = 0;
    for (auto const & path_cnt_cnt_pair:distribution) {
        distribution.at(path_cnt_cnt_pair.first) = distribution.at(path_cnt_cnt_pair.first) + cumulative_counter;
        cumulative_counter = distribution.at(path_cnt_cnt_pair.first);
    }


    for (auto const & entry : distribution) {
        std::cout << entry.first << "\t" << (double) entry.second / cumulative_counter << std::endl;
    }


}
