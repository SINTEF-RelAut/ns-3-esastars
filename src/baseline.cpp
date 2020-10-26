/**
 * @file baseline.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see baseline.h
 *
 * @brief Implements the member functions of the baseline strategy.
 */
#include <omp.h>
#include "../headers/utils.h"
#include "../headers/baseline.h"
#include "ns3/point-to-point-channel.h"

namespace ns3 {

    void Baseline::DoInitializations(uint32_t all_nodes) {

    }

/**
 * Iterates over all the beacons for all the neighbours of the node. If the beacon is valid, its dissemination towards
 * this neighbour does not create a loop in the path and the neighbour is not the same AS which originated the beacon
 * it is sent over the interfaces towards this neighbour until the maximum number of beacons to send per neighbour has
 * been reached.
 *
 * @see GenerateBeaconAndSend
 * @param valid_interfaces The interfaces along which to disseminate beacons for this type of node.
 * @param node The node which is disseminating beacons.
 */
void
Baseline::DisseminateBeacons (SCION_Node::neighbour_relation relation)
{
    uint32_t neighbors_cnt = node->neighbors.size ();
    omp_set_num_threads (NUM_CORE);
#pragma omp parallel for
    for (uint32_t i = 0; i < neighbors_cnt; ++i)
    {
        if (node->neighbors.at(i).second != relation) {
            continue;
        }

        uint16_t &remote_as_no = node->neighbors.at(i).first;
        const std::vector<uint16_t> &interfaces = node->interfaces_per_neighbor_as.at(remote_as_no);

        for (auto const &dst_as_beacons_pair : node->beacon_store)
        {
            const uint16_t& dst_as_no  = dst_as_beacons_pair.first;
            const beacons_with_same_dst_as& equal_dst_as_beacons = dst_as_beacons_pair.second;

            int16_t sent_count = 0;

            if (remote_as_no == dst_as_no)
            {
                continue;
            }

            for (auto const &len_beacons_pair : equal_dst_as_beacons)
            { // for each length
                if (sent_count >= FIXED_BEACONS_NUMBER_TO_SEND)
                {
                    break;
                }

                auto const &beacons = len_beacons_pair.second;

                for (auto const &the_beacon : beacons)
                {
                    if (sent_count >= FIXED_BEACONS_NUMBER_TO_SEND)
                    {
                        break;
                    }

                    if (!the_beacon->is_valid) {
                        continue;
                    }

                    bool generates_loop = false;
                    for (auto const &link_info : the_beacon->the_path) { // remove loops
                        if (UPPER_16_BITS(link_info) == remote_as_no) {
                            generates_loop = true;
                            break;
                        }
                    }

                    if (generates_loop) {
                        continue;
                    }


                    sent_count++;

                    // Iterate over all the valid interfaces of this remote AS and send the beacons
                    for (auto const &egress_interface_no : interfaces)
                    {
                        std::pair<uint16_t, Ptr<SCION_Node>>
                            remote_as_if_pair = node->GetRemoteAsInfo (egress_interface_no);

                        uint16_t remote_ingress_if_no = remote_as_if_pair.first;
                        Ptr<SCION_Node> remote_as = remote_as_if_pair.second;

                        ld latency = the_beacon->latency_stat +
                                     node->intra_as_latencies
                                         .at (LOWER_16_BITS( the_beacon->the_path.back()))
                                         .at (egress_interface_no);
                        ld bwd =
                            the_beacon->bwd_stat > (ld) node->inter_as_bwds.at (
                                egress_interface_no)
                            ? (ld) node->inter_as_bwds.at (
                                egress_interface_no)
                            : the_beacon->bwd_stat;

                        GenerateBeaconAndSend (the_beacon, egress_interface_no,
                                               remote_ingress_if_no,
                                               remote_as, latency, bwd, 1, false,
                                               0);
                    }
                }
            }
        }
    }
}

/**
 * @param key Beacon key.
 * @param dst_as The AS number of the node which originated the beacon.
 * @param old_beacon The previous beacon.
 * @param self_egress_if_no The interface number on which to send the beacon.
 * @param remote_ingress_if_no The interface number where the beacon will be received on the remote_as.
 * @param node The node sending the beacon.
 * @param remote_as The node receiving the beacon.
 * @param latency The new beacon latency.
 * @param bwd The new beacon bandwidth stat.
 */
void
Baseline::ReplacementPolicy (std::string key, uint16_t src_as, beacon *old_beacon,
                             uint16_t self_egress_if_no, uint16_t remote_ingress_if_no,
                             Ptr<SCION_Node> remote_as, ld latency,
                             ld bwd)
{
}


void
Baseline::MetaDataUpdateAfterImmediateSend (beacon *the_beacon, uint16_t local_iface, Ptr<SCION_Node> remote_as,
                              uint16_t dst_as_no)
{
}
void
Baseline::MetaDataUpdatePeriodic (beacon* the_beacon)
{
}

} // namespace ns3