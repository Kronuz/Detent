# Hosted reader fixture qualification

The prepared-reader candidate's hosted run [40306457470](https://github.com/Kronuz/Detent/actions/runs/40306457470) passes the four Linux jobs and fails two macOS fixtures. Neither failure exercises prepared application reads.

1. ARM macOS fails `test_multi_node_poll_source` at listener startup with `bind: Address already in use`. The fixture chooses a random port from a 6,000-port range without checking availability. Detent:742ffcd reserves a kernel-selected loopback port and retries a handoff collision within the existing three-second startup allowance. Twenty consecutive native repetitions pass in 4.40 seconds total.
2. Intel macOS fails `test_delivery_coordinator_subscription-relay-sessions-500000` at its 60-second feedback deadline. The update is acknowledged at 58,540 ms. Cleanup continues through 5,005 and 6,433 bounded turns, with maximum observed gaps of 12.72 and 2.78 ms; the latter input is still draining at the deadline. This trace establishes deadline exhaustion with continuing cleanup, but does not establish why ingestion takes longer on that host.

The 500K session fixture uses a 120-second source/feedback allowance and a 180-second outer CTest bound, matching the allowance already used by the comparable 500K HTTP fixture. All content, cursor, feedback and drain assertions remain. Smaller fixtures and production defaults retain their existing bounds. Native targeted validation and the subsequent hosted matrix are required before this candidate lands.
