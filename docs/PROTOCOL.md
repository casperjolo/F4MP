# Wire protocol (v2)

Transport: ENet, two channels (`0` reliable, `1` unreliable sequenced). Every packet starts with
a one-byte message id followed by the payload. All integers are little-endian; `str` is
`u16 length` + UTF-8 bytes (max 4096). Source of truth: `shared/include/f4mp/Protocol.hpp`.

## Common structures

```
Vec3        f32 x, f32 y, f32 z
PlayerState Vec3 position
            f32  yaw            radians (game Z angle)
            u32  worldspaceId   0 while in an interior
            u32  cellId
            u8   flags          bit0 sneaking, bit1 weapon drawn, bit2 dead, bit3 sprinting
            f32  health
            f32  maxHealth
```

## Client → server

| id | name | channel | payload |
|----|------|---------|---------|
| 1 | Hello | reliable | `u32 protocolVersion`, `u32 gameVersion` (major<<24 \| minor<<16 \| build), `str name` |
| 2 | PlayerState | unreliable | `PlayerState` |
| 3 | Chat | reliable | `str text` |
| 4 | SetName | reliable | `str name` (the character was named or renamed after connecting) |

## Server → client

| id | name | channel | payload |
|----|------|---------|---------|
| 64 | Welcome | reliable | `u32 yourId`, `u8 tickRate`, `str serverName`, `str motd` |
| 65 | Reject | reliable | `str reason` (connection is closed afterwards) |
| 66 | PlayerJoined | reliable | `u32 id`, `str name` |
| 67 | PlayerLeft | reliable | `u32 id` |
| 68 | PlayerStateUpdate | unreliable (reliable when replaying to a newcomer) | `u32 id`, `PlayerState` |
| 69 | ChatBroadcast | reliable | `u32 fromId` (0 = server), `str text` |
| 70 | PlayerRenamed | reliable | `u32 id`, `str name` (sent to everyone, including the renamed player) |

## Handshake

```
client                         server
  │── connect ───────────────────►│
  │── Hello ─────────────────────►│  protocol mismatch → Reject + disconnect
  │◄─ Welcome ────────────────────│
  │◄─ PlayerJoined × N ───────────│  everyone already here
  │◄─ PlayerStateUpdate × N ──────│  their last known state
  │── PlayerState (20 Hz) ───────►│
  │◄─ PlayerStateUpdate ──────────│  other players, relayed as received
```

Any message other than `Hello` before the handshake completes gets the sender kicked.

## Server-side policy (not part of the layout)

* Both sides set ENet peer timeouts to 60 s minimum / 180 s maximum so loading screens, which
  freeze the client's network pump, do not drop the connection.
* `PlayerState` with NaN/inf in any float is dropped.
* `Chat` is cut to 512 bytes, control characters are stripped, and a client gets at most 6
  messages per 5 seconds (extra ones are dropped, with one "Slow down." reply).
