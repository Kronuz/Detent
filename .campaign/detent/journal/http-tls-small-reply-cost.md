# http-tls-small-reply-cost: journal

State: waiting on jobs 261116-190759-a954cf 261116-190811-8cef10 261116-190816-f47f92 261116-190828-78aa9b 261116-190832-1dac8f
Quest: .campaign/detent/quests/http-tls-small-reply-cost.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Profile base (http:a17b197) vs head (product http:ad6771f = main 2d53cef) at bench http:996f1fc.
996f1fc: load_ab.py records user/system CPU and context switches per request, `--sample-dir`
attaches /usr/bin/sample to the server through each timed run; sample_summary.py turns the trees
into us/req by library, named frame and function; `ab.sh profile` (rows TLS c1 keep, c8 close;
base, head, A/A copy of base; 3 rounds of 10 s). Slot tls-small.

Jobs at 996f1fc (ref base=a17b197):
- build 261116-190759-a954cf (light)
- profile smoke 261116-190811-8cef10 (strict, 1 round of 4 s)
- counts 261116-190816-f47f92 (light; syscalls + Asio handlers per request, c1 keep and c8 close)
- profile 261116-190828-78aa9b (exclusive, A1)
- load A/B with A/A, user/system split 261116-190832-1dac8f (exclusive)

## Next

1. Read the profile: name where head's extra c1 CPU (+3.2 us) and close c8 CPU (+21 us/conn) go.
2. Cut each named cost (one hypothesis per experiment, at most ~5); load A/B with A/A per D77.
3. 10K TLS idle (A3) and suite + TSan (A4) at the final commit.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

<Decisions inside the quest's scope, so the reviewer can check them.>
