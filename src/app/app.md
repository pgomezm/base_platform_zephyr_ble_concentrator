# Application

@defgroup grp_app Application
@brief High-level logic and the device state machine

Orchestrates the whole firmware: brings the layers up in order, owns the state machine, and
coordinates the services. It contains no hardware access and no protocol knowledge of its own.

## Initialization order

HAL, then `device_table`, then the services, then the state machine. `device_table` comes before
`acquisition` because acquisition writes into it as soon as the first advertisement arrives, and a
service that fails to initialize stops the sequence rather than leaving the firmware half up.

## States

| State | Meaning | Corresponds to |
| --- | --- | --- |
| `STARTUP` | Services are up; waiting for the network join. | `STARTUP` / `CONFIGURED` in the status diagram |
| `LISTENING` | Scanning and collecting. The steady state. | `IDLE`, running the operation the 2022 diagram labels `HIRING` |
| `DISPATCHING` | An uplink is being built and sent. | `BUSY` |
| `SOFT_ERROR` | A recoverable failure. Retries, up to a limit. | `SOFT ERROR` |
| `HARD_ERROR` | Unrecoverable. The radio stops and the error LED stays lit. | `HARD ERROR` |

`LISTENING` is the state the 2022 `Concentrator.drawio` calls `HIRING`, which reads as a
transcription of "HEARING". It is named for what it does here, with the original noted so the
diagram is still traceable.

## Constraints

**The state machine holds the device state, and nothing else duplicates it.** `get_current_state_id()`
is what a status report serializes. There is no second copy of "what the device is doing" kept
alongside it to drift out of sync.

**States never transition each other.** A state asks `StateMachine::transition_to()`, so the entire
transition table stays readable in `state_machine.cpp` instead of being scattered across five files.

**Scanning keeps running during a dispatch.** The uplink is built from a snapshot taken when the
cycle starts, so readings that arrive mid-dispatch are picked up by the next cycle rather than
dropped.
