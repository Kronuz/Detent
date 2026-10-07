# Distribution codec comparison

Stage 4 uses a narrow MessagePack encoding for definitions and atomic snapshot/delta transactions. This decision selects a wire encoding, not a general MessagePack object model. The production contract still needs explicit identity, required capabilities, bounded framing, exact revisions and independently verified interoperability.

The candidate benchmark compares JSON, a specialized MessagePack tuple encoding and generated Protobuf Lite messages against identical snapshots. Every decoded knob and override is compared with the original model. Five warm repetitions produce each process's median; three fresh processes repeat the 500,000-knob comparison. Corpus construction is outside encode/decode phase clocks. Repeated model construction and destruction remain included in process peak RSS, so that number is not a steady-state agent memory prediction.

| Encoding | Encode CPU, 500k knobs | Decode CPU, 500k knobs | Raw bytes | Zstd level 3 bytes |
| --- | --- | --- | --- | --- |
| JSON | 955–1,205 ms | 2,025–2,167 ms | 56,283,377 | 657,463 |
| MessagePack | 123–130 ms | 621–627 ms | 29,771,269 | 1,271,489 |
| Protobuf Lite | 331–387 ms | 807–891 ms | 30,834,764 | 1,232,909 |

MessagePack uses less measured encode/decode CPU than the generated Protobuf candidate on this machine. JSON compresses to approximately half the size of either binary candidate. These results do not measure streaming, decompression, filesystem publication, production fanout, energy or infrastructure cost. Measurements and footprint scope are recorded in [distribution-codec-comparison.json](../benchmarks/distribution-codec-comparison.json).

The default benchmark requires no Protobuf dependency:

```sh
cmake -S . -B build
cmake --build build --target detent_distribution_codec_bench -j2
build/benchmarks/detent_distribution_codec_bench messagepack 500000
build/benchmarks/detent_distribution_codec_bench json 500000
```

An installed Protobuf SDK can enable the comparison-only candidate with `-DDETENT_BENCHMARK_PROTOBUF=ON`; use a separate build directory. The generated schema uses the documented [Protobuf Lite runtime](https://protobuf.dev/reference/cpp/api-docs/google.protobuf.message_lite/). MessagePack integer, string, array and nil encodings follow the [MessagePack specification](https://github.com/msgpack/msgpack/blob/master/spec.md). Benchmark schemas are experimental and are not the supported distribution protocol.
