//
// Created by seyedali on 17.07.21.
//

#ifndef NS_3_BEACONING_SIMULATOR_SCHEDULE_PERIODIC_EVENTS_H
#define NS_3_BEACONING_SIMULATOR_SCHEDULE_PERIODIC_EVENTS_H

#include <yaml-cpp/yaml.h>

#include "src/SCION/headers/externs.h"
#include "src/SCION/headers/scion_as.h"
#include "src/SCION/headers/scion_packet.h"

namespace ns3 {

    void SchedulePeriodicEvents(YAML::Node &config);

    void PeriodicBeaconingCheckPoint();
} // namespace ns3

#endif //NS_3_BEACONING_SIMULATOR_SCHEDULE_PERIODIC_EVENTS_H
