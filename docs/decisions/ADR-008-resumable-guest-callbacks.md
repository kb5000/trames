# ADR-008 — Guest-owned continuations for native callbacks

Accepted by owner direction, 2026-10-13. ADR-003's one-interpreter/no-JIT decision
and the bounded pure-memory authorization of ADR-007 remain in force.

Use a new typed NativeCall module for resumable sorting/search. Store operation
progress in a versioned guest stack record. A precompiled native step returns
when it needs a comparator; ordinary guest wrapper code calls it and resumes the
step. Do not cast guest callbacks to host code or recursively drive the complete
scheduler under a suspended host qsort stack.

Release every page borrow before guest execution. Bound native work, transport
only mutable state, reuse the current instruction quantum for immediate returns,
and retain bounded control/signal polls. Never restart original sorting after
partial native changes. Keep original pinned sources for pre-mutation fallback.

Default routing follows measured cost: qsort/qsort_r use the supported native
subset; the implemented generic bsearch driver is explicit because its small
control loop did not justify native entry overhead. This is not a second backend.

Guest ownership supplies natural nesting, fork and nonlocal-exit behavior without
host operation handles. Stateful library layouts and other callback signatures
need separate contracts; this decision does not authorize arbitrary native code.
See [protocol, limits and tests](../architecture/native-callbacks.md).
