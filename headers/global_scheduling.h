//
// Created by seyedali on 17.07.21.
//

#ifndef NS_3_BEACONING_SIMULATOR_GLOBAL_SCHEDULING_H
#define NS_3_BEACONING_SIMULATOR_GLOBAL_SCHEDULING_H

namespace ns3 {
    void ExecuteNonPeriodicEvents (ns3::NodeContainer nodes, ns3::Time advance);

    void SchedulePeriodicEvents(ns3::NodeContainer& nodes, ns3::Time beaconing_period, ns3::Time last_beaconing_event_time, ns3::Time simulation_end);

    void PeriodicCheckPoint(ns3::NodeContainer nodes);
}

#endif //NS_3_BEACONING_SIMULATOR_GLOBAL_SCHEDULING_H
