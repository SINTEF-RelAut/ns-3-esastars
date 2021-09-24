//
// Created by seyedali on 17.07.21.
//

#ifndef NS_3_BEACONING_SIMULATOR_GLOBAL_SCHEDULING_H
#define NS_3_BEACONING_SIMULATOR_GLOBAL_SCHEDULING_H

#include <yaml-cpp/yaml.h>

#include "src/SCION/headers/utils.h"

namespace ns3 {

    void RunParallelEvents (host_addr_t host_addr);

//    template <typename MEM, typename OBJ>
//    void RunParallelEvents (host_addr_t host_addr, MEM mem_ptr, OBJ obj);
//

    void ScheduleNextEvent (NodeContainer& nodes);

    void ExecuteNonPeriodicEvents (ns3::NodeContainer& nodes, ns3::Time advance);

    void SchedulePeriodicEvents(YAML::Node& config, NodeContainer& nodes);

    void PeriodicBeaconingCheckPoint(ns3::NodeContainer& nodes);
}

#endif //NS_3_BEACONING_SIMULATOR_GLOBAL_SCHEDULING_H
