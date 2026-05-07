# SCION Link Failure Study Checklist

This checklist is scoped to the first experiment based on:
- configs/manual_link_fail_6as.yaml
- topology/manual_link_fail_6as_topology.xml
- user_defined_events/manual_link_fail_6as_events.json

## Phase 1: Baseline Sanity

1. Build and run once with no code changes to confirm scenario parsing works.
2. Verify results file is created and contains path output sections.
3. Confirm at least one path from AS 101 to AS 106 is printed with interface IDs.

## Phase 2: Implement Event Effects

1. Implement link_down in UserDefinedEvents::LinkDown.
2. Implement link_up in UserDefinedEvents::LinkUp.
3. Introduce interface up/down state on ScionAs or BeaconServer side.
4. Enforce interface state in beacon dissemination and reception.
5. Re-run scenario and verify path changes after 120s and 220s events.

## Phase 3: Time-Indexed Observability

1. Add a periodic path snapshot evaluator (per beacon period).
2. Record valid paths for pair 101->106 across time.
3. Include path interface sequence in each snapshot row.

## Phase 4: Convergence Metrics

1. Compute alternate-path switchover time after the 120s failure.
2. Compute time until the original branch reappears after 220s recovery.
3. Compute per-AS recovery completion times for ASes 101, 102, 103.

## Suggested Run Command

./waf --run "scion configs/manual_link_fail_6as.yaml"

## Expected Initial Behavior

Before LinkDown/LinkUp are implemented, the scenario should parse and run, but link events will not change behavior because the event handlers are currently stubs.
