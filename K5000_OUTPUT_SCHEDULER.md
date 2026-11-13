# K5000 output scheduler (design, not implemented)

Status: **specified, not implemented.** Reference prototype: `tools/schema/k5k_scheduler.py` (+ `test_scheduler.py`). C++ interface: `core/link/output_scheduler.h` (declarations only). Tags: **M** measured, **S** from the spec/manual, **I** inferred, **?** unknown/guess.

## 1. Purpose
When the plugin is connected to a real K5000 ("push mode"), every edit the model accepts must reach the unit as SysEx without overrunning its MIDI input. The scheduler sits between the model's outbox and the MIDI port:

```
UI / host / CC  ->  PatchModel.set  ->  outbox (coalesced per wire address)
                                         ->  OutputScheduler  ->  MIDI out port
unit  ->  MIDI in  ->  ack (0x40..)  ->  OutputScheduler.onAck
```

It is for **SysEx to hardware only**. It plays no part in sound generation, patch loading from disk, or host automation.

## 2. Why it is needed
- MIDI is 31.25 kbaud = 3.125 bytes/ms **[S]**. A parameter change is 14 bytes (4.5 ms); a single dump is ~3.4 kB (~1.1 s) **[M]** from codec output.
- A knob drag produces hundreds of changes per second; unpaced output queues up in the driver and the unit lags seconds behind.
- Dumps are acknowledged by the unit (0x40 write complete, other codes = error / protect / memory full, `sysex::ackText`). Data sent before the ack may be lost **[I]**.
- The unit's OS needs time to apply each message (queue at 0x36AC5E, see OS_INTERNALS). The per-parameter apply time is **not measured** **[?]**; the inter-message gap is a guess.

## 3. Behaviour (normative)
1. **Coalescing.** Parameter messages are keyed by wire address + word half (the same 64-bit key as `PatchModel::queueOut`). A newer message with the same key replaces the older one and moves to the back of the queue (order = last touched). Word parameters (wave msb/lsb) are one message carrying both halves.
2. **Pacing.** The wire is busy for `wireTime(len) + gap` after each message. Nothing is sent before `busyUntil`. `wireTime` uses 90 % of the nominal rate (10 % margin).
3. **Priorities.** Dumps go before parameter messages. A dump that covers the patch (`coversParams`) discards all pending parameter messages (they are inside the dump).
4. **Acks.** After sending a dump the scheduler sends nothing until the unit acknowledges or `ackTimeout` passes. Ack codes and timeouts are reported as events. A timeout releases the queue; it is reported, not retried.
5. **Bulk fallback.** When the number of distinct pending parameter keys reaches `bulkThreshold`, the pending parameters are dropped and replaced by one dump built by the `makeDump` callback.
6. **Burst limit.** At most `maxPerPoll` messages are returned by one `poll` so the MIDI driver is never handed a large burst.
7. **Dumps are replaced by tag.** A queued dump with the same tag is replaced by the newer one.
8. **Pure logic.** Time is passed in; nothing sleeps, allocates in `poll`, or touches a MIDI API. This makes it testable with scripted timelines and identical in Python and C++.

## 4. Parameters (defaults)
| Name | Default | Basis |
|---|---|---|
| bytes per ms | 3.125 x 0.9 = 2.8125 | **S** wire rate, **I** 10 % margin |
| gap between messages | 2 ms | **?** guess; replace with measured OS apply time |
| ack timeout | 1000 ms | **?** guess |
| bulk threshold | 24 distinct keys | **I** 24 x 14 B = 336 B vs ~3.4 kB dump: threshold from airtime would be ~240, 24 chosen to cut OS load. To be tuned with a real unit |
| max messages per poll | 4 | **I** |

## 5. Numeric rules for identical Python / C++ results
The prototype uses floating-point milliseconds. The port uses **integer microseconds** (`int64_t`) and the Python reference is changed to match before golden vectors are generated:
- `wireTimeUs(len) = ceil(len * 3200 / 9)`  (len / 2.8125 ms)
- `gapUs = 2000`, `ackTimeoutUs = 1'000'000`
- `busyUntil = max(now, busyUntil) + wireTimeUs(len) + gapUs`

## 6. Threading (plugin layer)
- Producers: the message thread (UI, host parameter changes, MIDI CC) calls `PatchModel::drainOutbox` and pushes the messages into a lock-free single-producer queue. The **audio thread never touches the scheduler**.
- Consumer: one timer/worker thread (about 1 ms period) pops the queue into the scheduler, calls `poll(now)`, and writes the returned messages to the JUCE `MidiOutput`. Incoming acks arrive on the MIDI-in thread and are forwarded through a second queue to the same worker, so the scheduler itself is single-threaded.
- Backpressure: the queue between threads is bounded; when full, the producer coalesces by key before dropping anything (parameter messages are idempotent, dumps are never dropped).

## 7. Test plan
1. `tools/gen_scheduler_golden.py` runs timelines (push / pushDump / ack / poll at given microsecond timestamps) against the integer-time Python reference and writes `tests/golden/scheduler.tsv`: one line per event (`push`, `dump`, `ack`, `poll`) and the expected output (`time`, hex of each message, events, stats).
2. `tests/scheduler_tests.cpp` replays the file and compares message by message.
3. Cases from `test_scheduler.py` carried over: sweep coalescing, pacing under 3.125 B/ms, ordering (newest value, last-touched position), bulk fallback, dump supersedes params, ack wait, ack timeout, 3.4 kB dump wire time.
4. New: tag replacement, `maxPerPoll`, ack that arrives before the dump's wire time has passed, timeout while params are queued, key collision between a word half and a scalar.
5. Soak: random timelines (seeded) comparing Python and C++, as the model golden does.

## 8. Open questions
- **Bulk target.** A single dump (`0x20`) with a bank and slot **stores** the patch (and the emulated OS also selected it); it is not an "edit buffer only" write. The fallback must not overwrite a stored patch the user cares about. Options: dump to a reserved scratch slot then program-change to it, or use the temporary-buffer form if the OS has one. Needs an OS probe. **?**
- **Real timings.** Per-parameter OS apply time and the ack latency of dumps are unmeasured; the gap and timeout are guesses. Needs a real unit or a measurement in the emulated OS (cycles per queue entry). **?**
- **Multi mode.** Multi parameter changes use a different wire space; the key and `makeDump` must be mode-aware. **I**
- **Echo.** The unit may echo changes made on its panel; those arrive as `SysexIn` and are never queued (the model already guarantees this).
- **Ordering constraints.** Some changes may need to arrive in order (source count before source parameters; wave across 512 before the additive block data). Coalescing by last-touched position keeps order only per key. **?**
