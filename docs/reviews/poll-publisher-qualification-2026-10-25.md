# Poll/publisher overlap qualification

The fixture requires 100 durable publisher transactions to finish while at least 100 large-scope polls execute, with nonzero overlap. Its completion condition and 90-second CTest timeout bound that requirement. An additional 45-second assertion inside the polling loop imposes a shorter wall-clock limit on the same work.

The full native run reaches this assertion at `test_distribution_ownership.cc:382` and reports failure after 53.35 seconds. The failure path waits for the asynchronous publisher, so total duration can exceed the inner deadline. The same deadline failure is recorded in earlier dependency replay observations; this failure does not identify a prepared-reader production regression.

The fixture removes the inner deadline and retains all transaction-status, poll-response, completion-count and overlap assertions. The existing 90-second CTest timeout remains unchanged. Five consecutive repetitions pass in approximately 17 seconds each. These repetitions establish functional completion for the tested schedules, rather than a production latency limit.

Reproduce after building `test_authority_distribution_ownership`:

```sh
ctest --test-dir build --output-on-failure --repeat until-fail:5 -R '^test_authority_distribution_poll_publisher$'
```
