//
// Created by seyedali on 17.07.21.
//

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
#include "src/SCION/headers/global_scheduling.h"
#include "src/SCION/headers/post_simulation_evaluations.h"

namespace ns3 {

    void DoFinalEvaluations(ns3::NodeContainer &nodes, std::map<int32_t, uint16_t> &ASes,
                            std::map<uint16_t, int32_t> &index_to_AS_no,
                            uint16_t expiration_period, ns3::Time beaconing_period,
                            ns3::Time last_beaconing_event_time) {

//    PrintTrafficSentFromCollectorsPerDstPerPeriod(nodes, ASes, index_to_AS_no, expiration_period,  beaconing_period,  last_beaconing_event_time);
//    PrintPathNoDistribution (nodes);
//    PrintMinimumLatencyDist(nodes);
//    Evaluate_S_T_Connectivity(nodes);
//    PrintAllDiscoveredPaths(nodes, ASes, index_to_AS_no);
//     FindMinLatencyToDNSRootServers(nodes, ASes, index_to_AS_no);
        PrintConsumedBWAtEachPeriod(nodes, beaconing_period, last_beaconing_event_time);
//    PrintDistributionOfPathsWithSpecificHopCount(nodes);

//    PrintPathQualities(nodes);
    }

    void PrintTrafficSentFromCollectorsPerDstPerPeriod(ns3::NodeContainer &nodes, std::map<int32_t, uint16_t> &ASes,
                                                       std::map<uint16_t, int32_t> &index_to_AS_no,
                                                       uint16_t expiration_period, ns3::Time beaconing_period,
                                                       ns3::Time last_beaconing_event_time) {
        std::cout
                << "####################################### Traffic sent from each collector #######################################"
                << std::endl;
        std::list<int32_t> collectors(
                {3303, 3130, 1239, 701, 5413, 34224, 7018, 53767, 3741, 31019, 22652, 2497, 57866, 37100,
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

    void FindMinLatencyToDNSRootServers(ns3::NodeContainer &nodes, std::map<int32_t, uint16_t> &ASes,
                                        std::map<uint16_t, int32_t> &index_to_AS_no) {
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

        std::list<std::string> root_server_names(
                {"a-root", "b-root", "c-root", "d-root", "e-root", "f-root", "h-root", "j-root", "k-root", "l-root",
                 "m-root"});
        for (auto const &root_server_name : root_server_names) {
            std::cout << "################################################## " << root_server_name
                      << " #########################################################" << std::endl;

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
                std::cout << root_server_name << ": " << dst_as_no
                          << " The root DNS server's AS is not among the top 2000 ASes" << std::endl;
                continue;
            }
            std::cout
                    << "Probe|ASN|Latency|Distance|Probe coordinates|Path Coordinates|Instance Coordinates|ASes on Path"
                    << std::endl;

            uint16_t dst_index = ASes.at(dst_as_no);
            ns3::Ptr<ns3::SCION_AS> dst_node = ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(dst_index));

            rapidxml::xml_node<> *currProbe = probesNode->first_node("item");
            while (currProbe) {
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
                ns3::path *selected_path = NULL;

                auto const &beacons_to_dns_root_as = src_node->GetBeaconServer()->beacon_store.at(dst_index);

                for (auto const &len_beacons_pair : beacons_to_dns_root_as) {
                    auto const &same_len_beacons = len_beacons_pair.second;

                    for (auto const &the_beacon : same_len_beacons) {
                        if (the_beacon->is_valid) {
                            assert(UPPER_16_BITS(the_beacon->the_path.at(0)) == dst_index);

                            uint16_t first_br = LOWER_16_BITS(the_beacon->the_path.back());
                            std::pair<double, double> first_br_coordinates = src_node->interfaces_coordinates.at(
                                    first_br);
                            double latency_from_probe_to_first_hop = ns3::calculate_great_circle_latency(probe_lat,
                                                                                                         probe_long,
                                                                                                         first_br_coordinates.first,
                                                                                                         first_br_coordinates.second);
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
                while (currSite) {
                    double instance_lat = std::stod(currSite->first_node("Latitude")->value());
                    double instance_long = std::stod(currSite->first_node("Longitude")->value());
                    std::pair<double, double> last_br_coordinates = dst_node->interfaces_coordinates.at(last_br);
                    double overall_latency = min_latency_to_dst_as +
                                             ns3::calculate_great_circle_latency(instance_lat, instance_long,
                                                                                 last_br_coordinates.first,
                                                                                 last_br_coordinates.second);
                    if (overall_latency < min_overall_latency) {
                        min_overall_latency = overall_latency;
                        selected_instance_coordinates = std::pair<double, double>(instance_lat, instance_long);
                    }
                    currSite = currSite->next_sibling("item");
                }

                std::cout << currProbe->first_node("ID")->value() << "|" << src_as_no << "|"
                          // << "(" << selected_instance_coordinates.first << ", " << selected_instance_coordinates.second << ")" << "|"
                          << min_overall_latency << "|"
                          << ns3::calculate_great_circle_distance(probe_lat, probe_long,
                                                                  selected_instance_coordinates.first,
                                                                  selected_instance_coordinates.second) << "|"
                          << "(" << probe_lat << ", " << probe_long << ")" << "|";

                int hop_cnt = 0;
                std::vector<ns3::link_information>::reverse_iterator hop = selected_path->rbegin();
                for (; hop != selected_path->rend(); ++hop) {
                    if (hop_cnt != 0) {
                        std::cout << " ";
                    }

                    ns3::Ptr<ns3::SCION_AS> AS = ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(UPPER_16_BITS(*hop)));
                    std::pair<double, double> br_coordinates = AS->interfaces_coordinates.at(
                            SECOND_UPPER_16_BITS(*hop));
                    std::cout << "(" << br_coordinates.first << ", " << br_coordinates.second << ")";
                    hop_cnt++;
                }

                std::cout << "|" << "(" << selected_instance_coordinates.first << ", "
                          << selected_instance_coordinates.second << ")" << "|";

                hop_cnt = 0;
                hop = selected_path->rbegin();
                for (; hop != selected_path->rend(); ++hop) {
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

            for (auto const &src_as_no : set_of_src_ases) {
                ns3::Ptr<ns3::SCION_AS> src_node = ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(ASes.at(src_as_no)));
                auto const &beacons_to_dns_root_as = src_node->GetBeaconServer()->beacon_store.at(dst_index);
                for (auto const &len_beacons_pair : beacons_to_dns_root_as) {
                    auto const &same_len_beacons = len_beacons_pair.second;
                    for (auto const &the_beacon : same_len_beacons) {
                        if (!the_beacon->is_valid) {
                            continue;
                        }

                        std::cout << src_as_no << "|" << dst_as_no << "|";
                        ns3::path *the_path = &the_beacon->the_path;

                        int hop_cnt = 0;
                        std::vector<ns3::link_information>::reverse_iterator hop = the_path->rbegin();
                        for (; hop != the_path->rend(); ++hop) {
                            if (hop_cnt != 0) {
                                std::cout << " ";
                            }

                            ns3::Ptr<ns3::SCION_AS> AS = ns3::DynamicCast<ns3::SCION_AS>(
                                    nodes.Get(UPPER_16_BITS(*hop)));
                            std::pair<double, double> br_coordinates = AS->interfaces_coordinates.at(
                                    SECOND_UPPER_16_BITS(*hop));
                            std::cout << "(" << br_coordinates.first << ", " << br_coordinates.second << ")";
                            hop_cnt++;
                        }

                        std::cout << "|";

                        hop_cnt = 0;
                        hop = the_path->rbegin();
                        for (; hop != the_path->rend(); ++hop) {
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

    void PrintAllDiscoveredPaths(ns3::NodeContainer &nodes, std::map<int32_t, uint16_t> &ASes,
                                 std::map<uint16_t, int32_t> &index_to_AS_no) {
        std::cout
                << "################################################ Paths Information ##############################################################"
                << std::endl;

        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            ns3::Ptr<ns3::SCION_AS> the_node = ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i));

            std::cout << "From: " << index_to_AS_no.at(the_node->as_number) << std::endl;

            for (auto const &dst_as_beacons_pair : the_node->GetBeaconServer()->beacon_store) {
                uint16_t dst_as = dst_as_beacons_pair.first;
                auto const &same_dst_as_beacons = dst_as_beacons_pair.second;

                std::cout << "\t" << "To: " << index_to_AS_no.at(dst_as) << std::endl;

                for (auto const &beacons_from_same_nbr : same_dst_as_beacons) {
                    for (auto const &the_beacon : beacons_from_same_nbr.second) {
                        if (!the_beacon->is_valid) {
                            continue;
                        }
                        std::cout << "\t" << "\t";
                        int hop_cnt = 0;
                        std::vector<ns3::link_information>::reverse_iterator hop = the_beacon->the_path.rbegin();
                        for (; hop != the_beacon->the_path.rend(); ++hop) {
                            if (hop_cnt != 0) {
                                std::cout << ", ";
                            }
                            std::cout << index_to_AS_no.at(SECOND_LOWER_16_BITS(*hop)) << ":" << LOWER_16_BITS(*hop)
                                      << ", " << index_to_AS_no.at(UPPER_16_BITS(*hop)) << ":"
                                      << SECOND_UPPER_16_BITS(*hop);
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

    void PrintConsumedBWAtEachPeriod(ns3::NodeContainer &nodes, ns3::Time beaconing_period,
                                     ns3::Time last_beaconing_event_time) {
        for (ns3::Time t = ns3::Seconds(0.0); t < last_beaconing_event_time; t += beaconing_period) {
            std::cout << "####################################### frequencies of consumed bandwidth at Time "
                      << t
                      << " #######################################" << std::endl;

            std::map<uint32_t, uint32_t> frequencies_of_consumed_bwd;
            for (uint32_t i = 0; i < nodes.GetN(); ++i) {
                ns3::Ptr<ns3::SCION_AS> the_node = ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i));
                for (uint32_t if_index = 0; if_index < the_node->GetNDevices(); ++if_index) {
                    uint32_t consumed_bwd = the_node->GetBeaconServer()->bytes_sent_per_interface_per_period.at(
                            t.ToInteger(ns3::Time::MIN)).at(if_index);

                    if (frequencies_of_consumed_bwd.find(consumed_bwd) != frequencies_of_consumed_bwd.end()) {
                        frequencies_of_consumed_bwd.at(consumed_bwd)++;
                    } else {
                        frequencies_of_consumed_bwd.insert(std::make_pair(consumed_bwd, 1));
                    }
                }
            }

            std::cout << "consumed bandwidth on a link" << "\t" << "frequency" << std::endl;
            for (auto const &bwd_freq_pair : frequencies_of_consumed_bwd) {
                std::cout << bwd_freq_pair.first << "\t" << bwd_freq_pair.second << std::endl;
            }
        }
    }

    void PrintDistributionOfPathsWithSpecificHopCount(ns3::NodeContainer &nodes) {
        for (uint32_t path_length = 1; path_length <= 4; ++path_length) {
            std::cout
                    << "######################################### frequencies of path counts per destination AS with hop count: "
                    << path_length
                    << "#########################################"
                    << std::endl;
            std::map<uint64_t, uint64_t> frequencies_of_path_counts_per_dst_as_with_certain_length;
            for (uint32_t i = 0; i < nodes.GetN(); ++i) {
                for (auto const &dst_as_beacons_pair : ns3::DynamicCast<ns3::SCION_AS>(
                        nodes.Get(i))->GetBeaconServer()->beacon_store) {
                    uint64_t number_of_paths_with_certain_length = 0;
                    if (dst_as_beacons_pair.second.find(path_length) == dst_as_beacons_pair.second.end()) {
                        continue;
                    } else {
                        number_of_paths_with_certain_length = dst_as_beacons_pair.second.at(path_length).size();
                    }

                    if (frequencies_of_path_counts_per_dst_as_with_certain_length.find(
                            number_of_paths_with_certain_length) !=
                        frequencies_of_path_counts_per_dst_as_with_certain_length.end()) {
                        frequencies_of_path_counts_per_dst_as_with_certain_length.at(
                                number_of_paths_with_certain_length)++;
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

    void PrintMinimumLatencyDist(ns3::NodeContainer &nodes) {
        std::cout
                << "###################################################### MINIMUM LATENCY####################################"
                << std::endl;

        std::map<float, int> distribution;
        for (uint32_t i = 0; i < nodes.GetN(); i++) {
            ns3::Ptr<ns3::SCION_AS> the_node = ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i));
            for (uint32_t j = 0; j < nodes.GetN(); ++j) {
                if (i == j) continue;
                float min_latency = std::numeric_limits<float>::max();
                for (auto const &len_beacons_pair : the_node->GetBeaconServer()->beacon_store.at(j)) {
                    for (auto const &the_beacon : len_beacons_pair.second) {
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
        for (auto const &entry : distribution) {
            float latency = entry.first;
            distribution.at(latency) += cumulative_counter;
            cumulative_counter = distribution.at(latency);
        }

        for (auto const &entry : distribution) {
            std::cout << entry.first << "\t" << (double) entry.second / cumulative_counter << std::endl;
        }
    }

    void PrintPathNoDistribution(ns3::NodeContainer &nodes) {
        std::cout
                << "############################################# Path No Distribution ##################################"
                << std::endl;
        std::map<uint32_t, uint32_t> distribution;
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            ns3::Ptr<ns3::SCION_AS> node = ns3::DynamicCast<ns3::SCION_AS>(nodes.Get(i));
            for (auto const &dst_count_pair : node->GetBeaconServer()->valid_beacons_count_per_dst_as) {
                if (distribution.find(dst_count_pair.second) == distribution.end()) {
                    distribution.insert(std::make_pair(dst_count_pair.second, 0));
                }

                distribution.at(dst_count_pair.second) = distribution.at(dst_count_pair.second) + 1;
            }
        }

        uint32_t cumulative_counter = 0;
        for (auto const &path_cnt_cnt_pair:distribution) {
            distribution.at(path_cnt_cnt_pair.first) = distribution.at(path_cnt_cnt_pair.first) + cumulative_counter;
            cumulative_counter = distribution.at(path_cnt_cnt_pair.first);
        }


        for (auto const &entry : distribution) {
            std::cout << entry.first << "\t" << (double) entry.second / cumulative_counter << std::endl;
        }


    }

}