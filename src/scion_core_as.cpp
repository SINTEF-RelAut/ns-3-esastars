/**
 * @file scion_core_as.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see scion_core_as.h
 *
 * @brief Implements the specialized functions on the scion core ASes.
 */

#include "src/SCION/headers/scion_core_as.h"
#include "src/SCION/headers/beaconing/beacon_server.h"

namespace ns3 {

    void
    SCION_AS::ScheduleBeaconing (ns3::Time beaconing_period, ns3::Time last_beaconing_event_time) {
        for (Time t = Seconds(0); t < last_beaconing_event_time; t += beaconing_period) {
            Simulator::Schedule(t, &BeaconServer::UpdateTimeAndStats, this->beaconServer);

            Simulator::Schedule(t, &BeaconServer::DisseminateBeacons, this->beaconServer, neighbour_relation::CORE);

            Simulator::Schedule(t, &BeaconServer::InitiateBeacons, this->beaconServer, neighbour_relation::CORE);
            Simulator::Schedule(t, &BeaconServer::InitiateBeacons, this->beaconServer, neighbour_relation::CUSTOMER);

            Simulator::Schedule(t + MilliSeconds(100), &BeaconServer::UpdateStatePeriodic, this->beaconServer);
        }
    }


}