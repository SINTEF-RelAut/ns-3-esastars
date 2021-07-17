//
// Created by seyedali on 17.07.21.
//

#include "src/SCION/headers/local_scheduler.h"
#include "ns3/make-event.h"

namespace ns3 {
    template <typename MEM, typename OBJ>
    EventId LocalScheduler::Schedule (Time const &delay, MEM mem_ptr, OBJ obj)
    {
        return Schedule (delay, MakeEvent (mem_ptr, obj));
    }


    template <typename MEM, typename OBJ,
            typename T1>
    EventId LocalScheduler::Schedule (Time const &delay, MEM mem_ptr, OBJ obj, T1 a1)
    {
        return Schedule (delay, MakeEvent (mem_ptr, obj, a1));
    }

    template <typename MEM, typename OBJ,
            typename T1, typename T2>
    EventId LocalScheduler::Schedule (Time const &delay, MEM mem_ptr, OBJ obj, T1 a1, T2 a2)
    {
        return Schedule (delay, MakeEvent (mem_ptr, obj, a1, a2));
    }

    template <typename MEM, typename OBJ,
            typename T1, typename T2, typename T3>
    EventId LocalScheduler::Schedule (Time const &delay, MEM mem_ptr, OBJ obj, T1 a1, T2 a2, T3 a3)
    {
        return Schedule (delay, MakeEvent (mem_ptr, obj, a1, a2, a3));
    }

    template <typename MEM, typename OBJ,
            typename T1, typename T2, typename T3, typename T4>
    EventId LocalScheduler::Schedule (Time const &delay, MEM mem_ptr, OBJ obj, T1 a1, T2 a2, T3 a3, T4 a4)
    {
        return Schedule (delay, MakeEvent (mem_ptr, obj, a1, a2, a3, a4));
    }

    template <typename MEM, typename OBJ,
            typename T1, typename T2, typename T3, typename T4, typename T5>
    EventId LocalScheduler::Schedule (Time const &delay, MEM mem_ptr, OBJ obj,
                                 T1 a1, T2 a2, T3 a3, T4 a4, T5 a5)
    {
        return Schedule (delay, MakeEvent (mem_ptr, obj, a1, a2, a3, a4, a5));
    }

    template <typename MEM, typename OBJ,
            typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
    EventId LocalScheduler::Schedule (Time const &time, MEM mem_ptr, OBJ obj,
                                 T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6)
    {
        return Schedule (time, MakeEvent (mem_ptr, obj, a1, a2, a3, a4, a5, a6));
    }

    EventId
    LocalScheduler::Schedule (Time const &delay, EventImpl *event)
    {
        Time tAbsolute = delay + *local_time;

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
        while (m_events->PeekNext().key.m_ts <= (uint64_t) local_time->GetTimeStep()) {
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
        return m_events->PeekNext().key.m_ts;
    }

}