//
// Created by chrissy on 10.06.20.
//

#ifndef SCION_BEACONING_SIMMULATOR_BEACONING_STRATEGY_H
#define SCION_BEACONING_SIMMULATOR_BEACONING_STRATEGY_H

#include "beacon.h"
#include "scion_node.h"

class BeaconingStrategy{
public:
    virtual void InitiateBeacons() = 0;
    virtual void DisseminateBeacons() = 0;
    virtual void processImmediateReceive() = 0;

private:
    void AdjustBeaconValidity (beacon* the_beacon, SCION_Node* node);
    void DoBeaconing(SCION_Node* node);

};
#endif //SCION_BEACONING_SIMMULATOR_BEACONING_STRATEGY_H
