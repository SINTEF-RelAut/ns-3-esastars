/**
 * @file scion_core_as.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see scion_node.h
 * @brief Defines the SCION Core AS.
 *
 */

#ifndef SCION_BEACONING_SIMMULATOR_SCION_CORE_AS_H
#define SCION_BEACONING_SIMMULATOR_SCION_CORE_AS_H
#include "scion_as.h"

namespace ns3 {

class SCION_Core_AS : public SCION_AS
{
  public:
    SCION_Core_AS (uint16_t isd_number, uint16_t as_number, uint32_t system_id, BeaconServer *beaconServer)
        : SCION_AS (isd_number, as_number, system_id, beaconServer)
    {
    }

    /**
     * @brief Starts the core beaconing process at the beginning of the beaconing period.
     */
    void CoreBeaconing () ;

    /**
     * @brief Starts the intra ISD beaconing process at the beginning of the beaconing period.
     */
    void IntraISDBeaconing () ;

};
}
#endif //SCION_BEACONING_SIMMULATOR_SCION_CORE_AS_H
