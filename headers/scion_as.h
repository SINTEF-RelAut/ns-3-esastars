/**
 * @file scion_as.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see scion_node.h
 * @brief Defines the SCION ASes which are not part of the core.
 *
 */
#ifndef SCION_BEACONING_SIMMULATOR_SCION_AS_H
#define SCION_BEACONING_SIMMULATOR_SCION_AS_H
#include "scion_node.h"
class SCION_As : public SCION_Node{
public:
    SCION_As(uint16_t as_number, uint32_t system_id, coefficients coefs, const simulator_params &periods, BeaconingStrategy* strategy) :
            SCION_Node(as_number, system_id, coefs, periods, strategy) {}

    /**
     * @brief Does nothing. Leaf ASes do not participate in core-beaconing.
     */
    void CoreBeaconing() override;

    /**
    * @brief Starts the intra ISD beaconing process at the beginning of the beaconing period.
    */
    void IntraISDBeaconing() override;

    /**
     * @brief Starts the processing of beacons for source ASes that had previously not been seen. Gets
     * scheduled right after receiving such a beacon.
     */
    void ProcessReceivedBeacons(uint16_t beacon_origin_as_no, uint16_t ingress_if, beacon* the_beacon) override;

};
#endif //SCION_BEACONING_SIMMULATOR_SCION_AS_H
