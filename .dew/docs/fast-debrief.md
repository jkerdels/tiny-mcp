# Fast Debrief: backchannel

**Date**: 2026-03-11

## Verification Results

| Acceptance Test | Method | Result | Notes |
|----------------|--------|--------|-------|
| tools/list includes both backchannel tools after enable_backchannel | automated | PASS | |
| backchannel_event defers when queue is empty | automated | PASS | |
| emit with pending ID writes response and clears pending | automated | PASS | |
| emit with no pending ID enqueues; next call returns immediately | automated | PASS | |
| include_queued=false defers and discards existing queue | automated | PASS | Fixed during verify: original plan said "defers"; test now also asserts queue is cleared |
| immediate return clears the queue | automated | PASS | |
| ring-buffer overflow: oldest event is dropped | automated | PASS | |
| backchannel_usage returns non-empty string | automated | PASS | |
| backchannel_event without enable_backchannel returns isError | automated | PASS | |
| all existing tests pass | automated | PASS | 10/10 tool_set tests, 8/8 pre-existing McpToolServer tests |

## Edge Cases Checked

- **Queued items after include_queued=false + emit**: Previously, items queued before an `include_queued=false` call would survive in the queue and return on the next `include_queued=true` call. This was a semantic bug found during verify and fixed: `include_queued=false` now clears the queue.
- **Ring buffer at exactly max_queue_size**: Three items with `max_queue_size=3` retains all three — no spurious drop at the boundary. Confirmed.
- **emit before enable_backchannel**: Silent no-op, no crash. Confirmed.

## Deviations from Plan

1. **Supersede sentinel (build-phase addition)**: The plan described a single long-poll caller. During build, it became clear that a second `backchannel_event` call arriving while one is still pending needed handling. The implementation sends a `"backchannel_superseded"` sentinel to unblock the first caller before registering the second. This was not in the plan but is the correct behavior.

2. **include_queued=false queue semantics (verify-phase correction)**: The plan stated that `include_queued=false` "defers" — but was silent on what happens to existing queued events. During verify, the correct semantic was established: `include_queued=false` means "I am not interested in old messages," so the queue must be cleared on deferral. The implementation, description text, and test were all updated accordingly.

## Documentation Updated

- `README.md`: Added a **Backchannel notifications** section covering `enable_backchannel`, `emit_backchannel_event`, the agent-side long-poll pattern, and ring-buffer semantics.

## Lessons Learned

1. **Specify defer-path semantics for boolean parameters, not just the return-path**: The plan thoroughly described what `include_queued` does when the call returns immediately, but left its effect on the deferred path implicit. For any parameter that influences behavior on both the fast path and the slow path, document both explicitly during planning.

2. **Multi-caller scenarios surface naturally during implementation**: The supersede-sentinel behavior was not anticipated in the plan but became obvious the moment the implementation considered what happens when a second subagent calls `backchannel_event` before the first is unblocked. A brief "what if there are two concurrent callers?" question during planning would have surfaced this.
