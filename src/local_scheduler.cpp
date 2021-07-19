//
// Created by seyedali on 17.07.21.
//
#include "ns3/core-module.h"
#include "src/SCION/headers/local_scheduler.h"
#include "ns3/make-event.h"
#include "src/SCION/headers/beaconing/beacon_server.h"

namespace ns3 {


    EventId
    LocalScheduler::Schedule (Time const &delay, EventImpl *event)
    {
        Time tAbsolute = delay + Simulator::Now();

        Scheduler::Event ev;
        ev.impl = event;
        ev.key.m_ts = (uint64_t) tAbsolute.GetTimeStep ();
        ev.key.m_uid = m_uid;
        m_uid++;
//        m_unscheduledEvents++;
        m_events->Insert (ev);
        return EventId (event, ev.key.m_ts, ev.key.m_context, ev.key.m_uid);
    }

    void
    LocalScheduler::ProcessEvents ()
    {
        while (!m_events->IsEmpty() && m_events->PeekNext().key.m_ts <= (uint64_t) Simulator::Now().GetTimeStep()) {
            Scheduler::Event next = m_events->RemoveNext ();

//            m_unscheduledEvents--;
            m_eventCount++;

//            m_currentContext = next.key.m_context;
//            m_currentUid = next.key.m_uid;
            next.impl->Invoke ();
            next.impl->Unref ();
        }
    }

    uint64_t LocalScheduler::GetFirstEventTime () {
        if (m_events->IsEmpty()) {
            return std::numeric_limits<uint64_t>::max();
        }

        return m_events->PeekNext().key.m_ts;
    }

}