/**
 * @file beacon_server.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 */

#ifndef SCION_SIMULATOR_BEACON_SERVER_H
#define SCION_SIMULATOR_BEACON_SERVER_H

#include <map>
#include <unordered_map>
#include <unordered_set>

#include "ns3/nstime.h"
#include "ns3/rapidxml.hpp"

#include "src/SCION/headers/beaconing/beacon.h"
#include "src/SCION/headers/externs.h"
#include "src/SCION/headers/scion_as.h"

namespace ns3 {

#define MAX_BEACONS_TO_STORE 60
#define MAX_BEACONS_TO_SEND 5

    class SCION_AS;

    typedef std::unordered_set<Beacon *> beacons_with_equal_length;
    typedef std::map<uint16_t, beacons_with_equal_length> beacons_with_same_dst_as;
    typedef std::pair<Time, uint16_t> beaconing_timing_params;

    class BeaconServer {
    public:
        BeaconServer(SCION_AS *AS, bool parallel_scheduler, rapidxml::xml_node<> *xml_node, const YAML::Node &config)
            : AS(AS), parallel_scheduler(parallel_scheduler),
              beaconing_period(Time(config["beacon_service"]["period"].as<std::string>())),
              expiration_period(
                      Time(config["beacon_service"]["expiration_period"].as<std::string>()).ToInteger(Time::MIN)),
              last_beaconing_event_time(Time(config["beacon_service"]["last_beaconing"].as<std::string>())) {
            PropertyContainer p = parseProperties(xml_node);
            if (p.hasProperty("dirty_energy_ratio")) {
                dirty_energy_ratio = std::stod(p.getProperty("dirty_energy_ratio"));
            }

            if (p.hasProperty("sun_energy_ratio")) {
                sun_energy_ratio = std::stod(p.getProperty("sun_energy_ratio"));
            }
        }

        virtual void DoInitializations(uint32_t num_ASes, rapidxml::xml_node<> *xml_node, const YAML::Node &config) = 0;

        void SetAS(SCION_AS *AS);

        void ReceiveBeacon(Beacon &received_beacon, uint16_t sender_as, uint16_t remote_if, uint16_t local_if);

        void ScheduleBeaconing(Time last_beaconing_event_time);

        const uint16_t GetCurrentTime() const;

        const std::vector<std::vector<ld>> &GetIntraASEnergies() const;

        float GetDirtyEnergyRatio() const;

        float GetSunEnergyRatio() const;

        const std::unordered_map<uint16_t, beacons_with_same_dst_as> &GetBeaconStore() const;

        const std::unordered_map<std::string, Beacon *> &GetPathMapToBeacon() const;

        const std::unordered_map<uint16_t, uint16_t> &GetValidBeaconsCountPerDstAS() const;

        const std::unordered_map<uint16_t, uint16_t> &GetNextRoundValidBeaconsCountPerDstAS() const;

        const std::unordered_map<uint16_t, std::vector<uint32_t>> &GetBytesSentPerInterfacePerPeriod() const;

    protected:
        SCION_AS *AS;

        const bool parallel_scheduler;

        const Time beaconing_period;
        const uint16_t expiration_period;
        const Time last_beaconing_event_time;

        float dirty_energy_ratio;
        float sun_energy_ratio;

        uint16_t now;
        uint16_t next_period;

        std::vector<std::vector<ld>> intra_as_energies;

        std::unordered_map<uint16_t, beacons_with_same_dst_as> beacon_store;
        std::unordered_map<std::string, Beacon *> path_map_to_beacon;

        std::unordered_map<uint16_t, uint16_t> valid_beacons_count_per_dst_as;
        std::unordered_map<uint16_t, uint16_t> next_round_valid_beacons_count_per_dst_as;

        std::unordered_map<uint16_t, std::vector<uint32_t>> bytes_sent_per_interface_per_period;

        void initiate_beacons(neighbour_relation relation);

        virtual void initiate_beacons_per_interface(uint16_t self_egress_if_no, SCION_AS *remote_as,
                                                    uint16_t remote_ingress_if_no);

        virtual void create_initial_static_info_extension(static_info_extension_t &static_info_extension,
                                                          uint16_t self_egress_if_no,
                                                          const optimization_target_t *optimization_target);

        virtual void disseminate_beacons(neighbour_relation relation) = 0;

        void generate_beacon_and_send(Beacon *selected_beacon, uint16_t self_egress_if_no,
                                      uint16_t remote_ingress_if_no, SCION_AS *remote_as,
                                      static_info_extension_t &static_info_extension,
                                      const optimization_target_t *optimization_target = NULL,
                                      beacon_direction_t beacon_direction = beacon_direction_t::PUSH_BASED);

        std::tuple<bool, bool, bool, Beacon *, ld> import_policy(Beacon &the_beacon, uint16_t sender_as,
                                                                 uint16_t remote_egress_if_no,
                                                                 uint16_t self_ingress_if_no, uint16_t now);

        void insert_beacon(Beacon &the_beacon, uint16_t dst_as, uint16_t sender_as, uint16_t remote_egress_if,
                           uint16_t local_ingress_if, bool path_exists, bool existing_path_valid,
                           Beacon *beacon_to_replace);

        void delete_beacon(Beacon *to_be_removed_beacon, ld replacement_key, uint16_t dst_as);

        virtual std::tuple<bool, bool, bool, Beacon *, ld>
        alg_specific_import_policy(Beacon &the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no,
                                   uint16_t self_ingress_if_no, uint16_t now) = 0;

        virtual void insert_to_algorithm_data_structures(Beacon *the_beacon, uint16_t sender_as,
                                                         uint16_t remote_egress_if_no, uint16_t self_ingress_if_no) = 0;

        virtual void delete_from_algorithm_data_structures(Beacon *the_beacon, ld replacement_key) = 0;

        void update_state_periodic();

        void update_time_and_stats();

        void update_beacon_state(Beacon *the_beacon);

        virtual void update_algorithm_data_structures_periodic(Beacon *the_beacon, bool invalidated) = 0;

        void register_to_local_path_server();

        void increment_control_plane_bytes_sent(Beacon &the_beacon, uint16_t interface);

        std::pair<ld, ld> calculate_final_diversity_scores(Beacon *the_beacon);

        friend void ReadBr2BrEnergy(ns3::NodeContainer AS_nodes, std::map<int32_t, uint16_t> real_to_alias_as_no,
                                    const YAML::Node &config);
    };

    void ReadBr2BrEnergy(NodeContainer AS_nodes, std::map<int32_t, uint16_t> real_to_alias_as_no,
                         const YAML::Node &config);

} // namespace ns3
#endif //SCION_SIMULATOR_BEACON_SERVER_H
