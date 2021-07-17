/**
 * @file scion_core_as.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see scion_as.h
 * @brief Defines the SCION Core AS.
 *
 */

#ifndef SCION_BEACONING_SIMMULATOR_SCION_CORE_AS_H
#define SCION_BEACONING_SIMMULATOR_SCION_CORE_AS_H
#include "src/SCION/headers/scion_as.h"

namespace ns3 {

class SCION_Core_AS : public SCION_AS
{
  public:
    SCION_Core_AS (uint16_t isd_number, uint16_t as_number, uint32_t system_id,  Time local_time)
        : SCION_AS (isd_number, as_number, system_id, local_time){}

    void ScheduleBeaconing (ns3::Time beaconing_period, ns3::Time last_beaconing_event_time) override;

};
}
#endif //SCION_BEACONING_SIMMULATOR_SCION_CORE_AS_H
