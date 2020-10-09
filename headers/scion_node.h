/**
 * @file scion_node.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @brief Defines SCION_Node and its associated constants and type-definitions.
 *
 */

#ifndef SCION_BEACONING_SIMMULATOR_SCION_NODE_H
#define SCION_BEACONING_SIMMULATOR_SCION_NODE_H
#include "beacon.h"
#include "ns3/network-module.h"
#include "ns3/node.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/point-to-point-channel.h"
#include "../headers/beaconing_strategy.h"
#include <unordered_set>
#include <unordered_map>

namespace ns3 {

/**
 * @brief Steers how many beacons are maximally disseminated to each neighbour per fixed destination AS.
 */
#define FIXED_BEACONS_NUMBER_TO_SEND 5
/**
 * @brief Steers how many beacons are maximally stored on each node per fixed destination AS.
 */
#define FIXED_BEACONS_NUMBER_TO_STORE 30

/**
 * @brief Time a router needs to process a beacon.
 *
 * This is only relevant for immediate beacons since the beaconing period for regular beacons
 * will typically be much greater than a few milliseconds.
 */
#define PROCESSING_DELAY MilliSeconds (1)

//TODO: Might achieve optimized cache use by making a const pass on things that are read only
//Also in beacon file

// Forward declaration because of circular dependency

/** @brief Holds a set of beacons with constant length.*/
typedef std::unordered_set<beacon *> beacons_with_equal_length;

/**
 * @brief Holds beacons originating at a fixed destination AS indexable by their hop count.
 */
typedef std::map<uint16_t, beacons_with_equal_length> beacons_with_same_dst_as;

/**
 * @brief Holds 0:beaconing_period, 1:expiration_period.
 *
 * Used to reduce the number of parameters we need to pass into the CreateObject constructor wrapper.
 *
 * Expected order:
 * - beaconing_period
 * - expiration_period
 */
typedef std::pair<Time, uint16_t> beaconign_timing_params;

/**
 * @brief Holds 0:latency_coef, 1:bandwidth_coef, 2:AS_level_diversity_coef, 3:link_level_diversity_coef
 *
 * Used to reduce the number of parameters we need to pass into the CreateObject constructor wrapper.
 *
 * Expected order:
 * - latency_coef
 * - bandwidth_coef
 * - AS_level_diversity_coef
 * - link_level_diversity_coef
 */
typedef std::tuple<ld, ld, ld, ld> coefficients;

class BeaconingStrategy;

/**
 * @brief This is the base definition of a SCION enabled Node. A SCION_Node models an Autonomous System.
 */
class SCION_Node : public Node
{


  public:
    //AS properties
    // TODO: Some cache optimisation might be achieved by reordering the members and grouping
    // the ones who are used together often (also in beacon.h file).
    /** @brief The autonomous system number of this node. */
    uint16_t as_number;
    /** @brief The current simulator time in minutes. */
    uint16_t now;
    /** @brief The next beaconing interval in minutes. */
    uint16_t next_period;
    /** @brief Periodicity of beaconing. */
    Time beaconing_period;
    /** @brief Expiration time of beacon. */
    uint16_t expiration_period;
    /** @brief AS latency preference coefficient. */
    ld latency_coef;
    /** @brief AS bandwidth preference coefficient. */
    ld bandwidth_coef;
    /** @brief AS AS-level path-diversity preference coefficient. */
    ld AS_level_diversity_coef;
    /** @brief AS link-level path-diversity preference coefficient. */
    ld link_level_diversity_coef;
    /** @brief Largest amount of bandwidth found on any border router link. */
    int32_t AS_max_bwd;

    // Interfaces Properties *****************************************************************************************************
    /**
     * @brief Different types of links.
     *
     * In typical BGP-enabled internet topologies, there are peer, and customer links. The Provider type was introduced
     * to model the reverse directionality of a customer link, the core type is found between core-ASes in SCION-topologies.
     */
    enum neighbour_relation { CORE = 0, PEER = 1, CUSTOMER = 2, PROVIDER = 3 };
    /** @brief Holds the AS numbers of all the neighbours of the node*/
    std::vector<std::pair<uint16_t, neighbour_relation> > neighbors;
    /** @brief Holds a mapping of AS numbers and their connected interfaces & relations to this node. */
    std::unordered_map<uint16_t, std::vector<uint16_t> > interfaces_per_neighbor_as;
    // TODO: Since interface_coordinates is only used for latency calculation, we could do this while initializing
    // and could save some space by not storing the interface_coordinates.
    /** @brief Maps all interfaces to their corresponding remote AS numbers
    */
    std::unordered_map<uint16_t, uint16_t> interface_to_neighbor_map;
    /** @brief Holds the coordinates of the border router locations between ASes.
     *
     * The pair of border routers are assumed to be in close proximity (same room) which is why we do not model
     * any latency between them.
     */
    std::vector<std::pair<ld, ld>> interfaces_coordinates;

    /** @brief Holds the estimated latencies between the border routers inside this AS.*/
    std::vector<std::vector<ld>> intra_as_latencies;
    /** @brief Holds the bandwidths of the links between border routers of ASes. */
    std::vector<int32_t> inter_as_bwds;

    // beacon store structures ***************************************************************************************************
    /** @brief Pointers to all the beacons indexable by their destination AS and their hop count.*/
    std::unordered_map<uint16_t, beacons_with_same_dst_as> beacon_store;
    /** @brief Pointers to all the beacons indexable by their key.
     *
     * This is done to allow efficient traversal & search of all the beacons.
     * @see key
     * */
    std::unordered_map<std::string, beacon *> path_map_to_beacon;


    // helper structures ********************************************************************************************************
    /**
     * @brief This structure counts how many valid beacons will be known per destination AS after the current beaconing
     * period is complete.
     *
     * It is the sum of the currently valid beacons and the beacons that will be valid in the next beaconing period. This structure
     * is used to decide if an AS would discard the a newly sent beacon or not.
     *
     * @see GenerateBeaconAndSend
     */
    std::unordered_map<uint16_t, uint16_t> next_round_valid_beacons_count_per_dst_as;

    // Beaconing Algorithm ***************************************************************************************************************
    /** @brief Pointer to the BeaconingStrategy used to execute the correct beaconing behaviour.*/
    BeaconingStrategy *strategy;

    // statistics ***************************************************************************************************************
    /** @brief Holds the number of beacons that are valid for each destination AS in the current beaconing period.*/
    std::unordered_map<uint16_t, uint16_t> valid_beacons_count_per_dst_as;
    /** @brief Collects how many bytes would have been sent over which interface for every beacon sent during one beaconing_period.
     *
     * The outer map structure is indexed by time. Then the vector index corresponds to the interface number.
     * */
    std::unordered_map<uint16_t, std::vector<uint32_t>> bytes_sent_per_interface_per_period;

    SCION_Node (uint16_t as_number, uint32_t system_id, coefficients coefs,
                const beaconign_timing_params &periods, BeaconingStrategy *strategy)
        : Node (system_id),
          as_number (as_number),
          beaconing_period (periods.first),
          expiration_period (periods.second),
          latency_coef (std::get<0> (coefs)),
          bandwidth_coef (std::get<1> (coefs)),
          AS_level_diversity_coef (std::get<2> (coefs)),
          link_level_diversity_coef (std::get<3> (coefs)),
          strategy (strategy)
    {
    }

    virtual ~SCION_Node ()
    {
    }

    /**
     * @brief Initializes the intra_as_latencies and the as_max_bw fields.
     */

    void DoInitializations();
    void DoInitializations (uint32_t all_nodes);


    std::pair<uint16_t, Ptr<SCION_Node>>
    GetRemoteAsInfo (uint16_t egress_interface_no);
    /**
     * @brief Called to initiate CoreBeaconing.
     *
     * Must be overwritten by descendants of SCION_Node.
     */
    virtual void CoreBeaconing () = 0;

    /**
     * @brief Called to initiate IntraISDBeaconing.
     *
     * Must be overwritten by descendants of SCION_Node.
     */
    virtual void IntraISDBeaconing () = 0;

    /**
     * @brief Called to initiate the processing of beacons received in the last beaconing period.
     *
     * Must be overwritten by descendants of SCION_Node.
     */
    virtual void ProcessReceivedBeacons (uint16_t dst_as_no, uint16_t ingress_if,
                                         beacon *the_beacon) = 0;

    /**
     * @brief Iterates over all the beacons and fills the provided structures with the distribution of achieved scores.
     */
    void FinalPathEvaluation (std::map<ld, uint64_t> &satisfaction_stat,
                              std::map<ld, uint64_t> &AS_level_diversity_stat,
                              std::map<ld, uint64_t> &link_level_diversity_stat);

  protected:
    /**
     * @brief Prints the number of discovered ASes so far, updates the node time and initializes some memory for bandwidth statistics.
     */
    void UpdateTimeAndStats ();

  private:
    /**
     *  @brief Returns the average as-level diversity and link-level diversity scores of the passed beacon compared to all other beacons the node has which
     *  originated at the same destination as number.
     */
    std::pair<ld, ld> calculate_final_diversity_scores (beacon *the_beacon);
};
}
#endif //SCION_BEACONING_SIMMULATOR_SCION_NODE_H
