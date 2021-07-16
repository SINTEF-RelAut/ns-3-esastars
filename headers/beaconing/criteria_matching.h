/**
 * @file criteria_matching.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see beaconing_strategy.h
 * @brief Defines a specialized beaconing beaconServer that uses criteria matching
 * (related to bandwidth/latency/disjointness of paths) to choose which
 * beacons to disseminate.
 */

#ifndef SCION_BEACONING_SIMMULATOR_CRITERIA_MATCHING_H
#define SCION_BEACONING_SIMMULATOR_CRITERIA_MATCHING_H

#include "src/SCION/headers/beaconing/beacon_server.h"

namespace ns3 {
#define MAX_ACCEPTABLE_JOINTNESS 2.0
#define MAX_LAT 1000.0
#define MAX_BWD 400.0
#define ALPHA 12.0
#define BETA 6.0
#define GAMMA 11.0
#define SCALING_FACTOR 0.95
#define SCORE_THRESHOLD 0.9

#define MAX_BEACONS_TO_STORE 60
#define MAX_BEACONS_TO_SEND 5
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

    class CriteriaMatching : public BeaconServer {
    public:

        CriteriaMatching (beaconing_timing_params params, coefficients coefs) :
        BeaconServer(params){
            std::tie(latency_coef, bandwidth_coef, AS_level_diversity_coef, link_level_diversity_coef) = coefs;
        }

        /** @brief AS latency preference coefficient. */
        ld latency_coef;
        /** @brief AS bandwidth preference coefficient. */
        ld bandwidth_coef;
        /** @brief AS AS-level path-diversity preference coefficient. */
        ld AS_level_diversity_coef;
        /** @brief AS link-level path-diversity preference coefficient. */
        ld link_level_diversity_coef;

        void DoInitializations(uint32_t all_nodes) override;
        /**
         * @brief Disseminates highest scoring beacons towards multiple interfaces of the appropriate neighbours until the limit for
         * sending beacons with the same originating source AS to one neighbour is reached.
         */
        void
        DisseminateBeacons(neighbour_relation relation) override;

        /**
        * @brief Evicts the lowest scored beacon for the beacons originating AS if the score of the new beacon is larger
        * than the lowest scored matching beacon found in the remote ASes beacon store.
        */

        std::tuple<bool, bool, bool, beacon*>
        ImportPolicy (beacon& the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no, uint16_t self_ingress_if_no,
                      uint16_t now) override;

        void
        InsertToStrategyMetaData (beacon* the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no, uint16_t self_ingress_if_no) override;

        void
        DeleteFromStrategyMetaData (beacon* the_beacon) override;

    protected:


        void
        MetaDataUpdatePeriodic (beacon* the_beacon, bool invalidated) override;

    private:


        /** @brief
         * holds a history of sent beacons;
         * For each interface we keep a map from disseminated beacons pointers in self beacon store to the disseminated beacon's raw score and expiration time
         * */
        std::vector<std::unordered_map<beacon *, std::pair<float, uint16_t>> *> sent_beacons;

        std::vector<std::unordered_map<uint16_t, uint16_t>* > sent_beacons_cnt;
        /** @brief
        * holds repetition counter of every link on the path from a source AS to a destination AS
        * */
        std::unordered_map<uint16_t, std::vector<std::unordered_map<uint32_t, uint32_t> *>>
                links_jointnesses_on_sent_paths;

        std::vector<std::unordered_map<uint32_t, uint32_t> *> links_jointnesses_on_received_paths;

        void update_sent_beacon_timer(uint16_t remote_as, uint16_t self_egress_if_no, beacon *the_beacon);

        void inc_links_jointness_on_sent_paths(uint16_t dst_as_no, uint16_t remote_as_no, uint16_t self_egress_if_no,
                                               beacon *the_beacon);

        void
        inc_links_jointness_on_received_paths(uint16_t dst_as_no, beacon *the_beacon);

        void add_to_sent_beacons(uint16_t dst_as_no, uint16_t remote_as, uint16_t self_egress_if_no, beacon *the_beacon, float raw_score);

        ld calculate_link_diversity_score_for_dissemination(uint16_t remote_as, uint16_t dst_as, uint16_t egress_if_no,
                                                            beacon *the_beacon);

        ld  calculate_link_diversity_score_for_import(uint16_t dst_as, beacon& the_beacon);

        bool path_not_sent_before(uint16_t remote_as, uint16_t self_egress_if_no, beacon *the_beacon);

        std::multimap<ld, std::tuple<beacon *, uint16_t, uint16_t, Ptr<SCION_AS>, ld, ld> >
        select_beacons_to_disseminate_per_dst_per_nbr(uint16_t remote_as_no, uint16_t dst_as_no,
                                                      const beacons_with_same_dst_as &beacons_to_the_dst_as);

        void remove_invalid_sent_beacons(beacon* the_beacon, uint16_t dst_as);

        void dec_links_jointnesses_on_sent_paths(beacon* the_beacon, uint16_t  dst_as, uint16_t remote_as_no, uint16_t self_egress_if);

        void dec_links_jointnesses_on_received_paths(beacon* the_beacon, uint16_t  dst_as);

        inline ld calculate_raw_score (beacon* the_beacon, uint16_t dst_as_no, uint16_t self_egress_if_no, Ptr<SCION_AS> remote_as);

        inline ld
        calculate_import_raw_score (beacon& the_beacon);

    };
}
#endif //SCION_BEACONING_SIMMULATOR_CRITERIA_MATCHING_H
