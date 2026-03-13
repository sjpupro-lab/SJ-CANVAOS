# SJ-STREAM Packet v1

- Packet size: 1024 bytes fixed
- Header: 64 bytes
- Payload: 896 bytes
- Tail: 64 bytes

## Core guarantees

1. Every packet is fixed-size for stable streaming.
2. Payload size records the true byte count of the last packet.
3. Packets are chained with `prev_packet_hash` for reassembly validation.
4. `packet_index` is the canonical reorder / restore index.
5. `visual_slot`, `route_id`, and `reorder_seed` are reserved for CanvasOS-side visualization and path engines.

## Build example

```bash
gcc -std=c11 -Wall -Wextra -Iinclude \
  src/sj_stream_packet.c tests/test_sj_stream_roundtrip.c \
  -o test_sj_stream_roundtrip
./test_sj_stream_roundtrip
```
