# http-drop-user-data: http: remove Request::user_data and DeferredReply, add Request::deadline()

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status http-drop-user-data`.

## Outcome

`docs/design/http-coroutines.md:513, 550` (follow-up 3, owner direction on Q4) commits http to removing `Request::user_data` (`http_message.h:135`) and `DeferredReply` (`http_deferred.h:251`) and adding `Request::deadline()`; neither old surface has a consumer. After this quest both are gone and handlers read their deadline from the request. It changes a shared public API, so it needs the Xapiand lead's agreement before landing.

## Acceptance criteria

- A1 `user_data` and `DeferredReply` are gone, and Detent and Xapiand build against the new http: jobs.
- A2 `Request::deadline()` returns the request's deadline: a test: jobs.
- A3 [decision] The Xapiand lead agrees to the removal: a PLAN.md Decisions id.
- A4 The suite and CI pass: jobs.

## Notes

Filed 2026-11-19 from the untracked-commitment audit (U14).
