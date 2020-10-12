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
#include "scion_node.h"

namespace ns3 {

class SCION_Core_As : public SCION_Node
{
  public:
    SCION_Core_As (uint16_t as_number, uint32_t system_id, coefficients coefs,
                   const beaconing_timing_params &periods, BeaconingStrategy *strategy)
        : SCION_Node (as_number, system_id, coefs, periods, strategy)
    {
    }

    /**
     * @brief Starts the core beaconing process at the beginning of the beaconing period.
     */
    void CoreBeaconing () override;

    /**
     * @brief Starts the intra ISD beaconing process at the beginning of the beaconing period.
     */
    void IntraISDBeaconing () override;

    /**
     * @brief Starts the processing of beacons for source ASes that had previously not been seen. Gets
     * scheduled right after receiving such a beacon.
     */
    void ProcessReceivedBeacons (uint16_t beacon_origin_as_no, uint16_t ingress_if,
                                 beacon *the_beacon) override;
};
}
#endif //SCION_BEACONING_SIMMULATOR_SCION_CORE_AS_H
