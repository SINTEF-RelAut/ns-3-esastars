//
// Created by seyedali on 17.07.21.
//

#ifndef NS_3_BEACONING_SIMULATOR_LOCAL_SCHEDULER_H
#define NS_3_BEACONING_SIMULATOR_LOCAL_SCHEDULER_H

#include "ns3/event-impl.h"
#include "ns3/event-id.h"
#include "ns3/nstime.h"

#include "ns3/map-scheduler.h"
#include "ns3/scheduler.h"



namespace ns3 {
    class BeaconServer;
    class LocalScheduler {
    public:
        LocalScheduler (Time* local_time) : local_time(local_time) {
            m_events = new MapScheduler();
        }

        template <typename MEM, typename OBJ>
        EventId Schedule (Time const &delay, MEM mem_ptr, OBJ obj)
        {
            return Schedule (delay, MakeEvent (mem_ptr, obj));
        }


        template <typename MEM, typename OBJ,
                typename T1>
        EventId Schedule (Time const &delay, MEM mem_ptr, OBJ obj, T1 a1)
        {
            return Schedule (delay, MakeEvent (mem_ptr, obj, a1));
        }

        template <typename MEM, typename OBJ,
                typename T1, typename T2>
        EventId Schedule (Time const &delay, MEM mem_ptr, OBJ obj, T1 a1, T2 a2)
        {
            return Schedule (delay, MakeEvent (mem_ptr, obj, a1, a2));
        }

        template <typename MEM, typename OBJ,
                typename T1, typename T2, typename T3>
        EventId Schedule (Time const &delay, MEM mem_ptr, OBJ obj, T1 a1, T2 a2, T3 a3)
        {
            return Schedule (delay, MakeEvent (mem_ptr, obj, a1, a2, a3));
        }

        template <typename MEM, typename OBJ,
                typename T1, typename T2, typename T3, typename T4>
        EventId Schedule (Time const &delay, MEM mem_ptr, OBJ obj, T1 a1, T2 a2, T3 a3, T4 a4)
        {
            return Schedule (delay, MakeEvent (mem_ptr, obj, a1, a2, a3, a4));
        }

        template <typename MEM, typename OBJ,
                typename T1, typename T2, typename T3, typename T4, typename T5>
        EventId Schedule (Time const &delay, MEM mem_ptr, OBJ obj,
                                          T1 a1, T2 a2, T3 a3, T4 a4, T5 a5)
        {
            return Schedule (delay, MakeEvent (mem_ptr, obj, a1, a2, a3, a4, a5));
        }

        template <typename MEM, typename OBJ,
                typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
        EventId Schedule (Time const &time, MEM mem_ptr, OBJ obj,
                                          T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6)
        {
            return Schedule (time, MakeEvent (mem_ptr, obj, a1, a2, a3, a4, a5, a6));
        }




        void
        ProcessEvents ();

        uint64_t GetFirstEventTime ();

    private:

        EventId Schedule (const Time &delay, EventImpl *event);

        Ptr<Scheduler> m_events;

        Time* local_time;

        /** Next event unique id. */
        uint32_t m_uid;
//        /** Unique id of the current event. */
//        uint32_t m_currentUid;
//        /** Execution context of the current event. */
//        uint32_t m_currentContext;
        /** The event count. */
        uint64_t m_eventCount;
        /**
         * Number of events that have been inserted but not yet scheduled,
         *  not counting the Destroy events; this is used for validation
         */
//        int m_unscheduledEvents;




    };
}
#endif //NS_3_BEACONING_SIMULATOR_LOCAL_SCHEDULER_H
