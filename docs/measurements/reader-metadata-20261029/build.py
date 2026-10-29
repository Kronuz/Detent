from __future__ import annotations

import json
import shlex
import shutil
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent


def replace(source: str, before: str, after: str, count: int = 1) -> str:
    if source.count(before) != count:
        raise ValueError(f"Unexpected source occurrence: {before!r}")
    return source.replace(before, after)


def instrument(source: str) -> str:
    source = replace(
        source,
        "template <std::size_t N> std::array<char, N> record(int directory, std::string_view name, uid_t owner) {",
        """template <std::size_t N> std::array<char, N> record(int directory, std::string_view name, uid_t owner) {
    static_assert(N == descriptor_bytes || N == storage_identity_bytes || N == paged::root_bytes || N == reader_metadata_bytes);
    lookup_profile::Context context(N == descriptor_bytes ? lookup_profile::File::Selected :
        N == storage_identity_bytes ? lookup_profile::File::Storage :
        N == paged::root_bytes ? lookup_profile::File::Image : lookup_profile::File::Metadata);
    lookup_profile::Scope total(lookup_profile::Step::Total);""",
    )
    source = replace(
        source,
        "RecoveryFile file(open_record(directory, name));",
        "RecoveryFile file(open_record(directory, name));\n    lookup_profile::Close closer{file};",
    )
    source = replace(
        source,
        "return ::openat(directory, terminated.data(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);",
        "return lookup_profile::call(lookup_profile::Step::Open, [&] { return ::openat(directory, terminated.data(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW); });",
    )
    source = replace(
        source,
        "check_generation_file(file.fd, owner, N);",
        "lookup_profile::call(lookup_profile::Step::Checks, [&] { check_generation_file(file.fd, owner, N); });",
    )
    source = replace(
        source,
        "require(internal::mode_only_acl(file.fd));",
        "lookup_profile::call(lookup_profile::Step::Acl, [&] { require(internal::mode_only_acl(file.fd)); });",
        2,
    )
    source = replace(
        source,
        "\tauto &domain = resources::domain(limits);",
        "lookup_profile::Scope total(lookup_profile::Step::Total);\n\tauto &domain = resources::domain(limits);",
    )
    source = replace(
        source,
        "for (unsigned attempt = 0; attempt != 3; ++attempt) {",
        "for (unsigned attempt = 0; attempt != 3; ++attempt) {\n        lookup_profile::note(lookup_profile::Step::Attempt);",
    )
    source = replace(
        source,
        "auto files = factory.make<PreparedFiles>();",
        "auto files = lookup_profile::call(lookup_profile::Step::Controls, [&] { return factory.make<PreparedFiles>(); });",
    )
    source = replace(
        source,
        "files->directory.fd = open_publisher_directory(path, publisher);",
        "files->directory.fd = lookup_profile::call(lookup_profile::File::Directory, lookup_profile::Step::Total, [&] { return open_publisher_directory(path, publisher); });",
    )
    source = replace(
        source,
        "::fstat(files->directory.fd, &directory)",
        "lookup_profile::call(lookup_profile::File::Directory, lookup_profile::Step::Stat, [&] { return ::fstat(files->directory.fd, &directory); })",
    )
    source = replace(
        source,
        "decode_descriptor(selected)",
        "lookup_profile::call(lookup_profile::File::Selected, lookup_profile::Step::Decode, [&] { return decode_descriptor(selected); })",
    )
    source = replace(
        source,
        "const GenerationName lease_name(descriptor.generation, GenerationFile::Lease);",
        "lookup_profile::Context lease_context(lookup_profile::File::Lease);\n\t\tconst GenerationName lease_name(descriptor.generation, GenerationFile::Lease);",
    )
    source = replace(
        source,
        "check_generation_file(files->lease.fd, publisher, generation_ownership_bytes);",
        "lookup_profile::call(lookup_profile::Step::Checks, [&] { check_generation_file(files->lease.fd, publisher, generation_ownership_bytes); });",
    )
    source = replace(
        source,
        "require(internal::mode_only_acl(files->lease.fd));",
        "lookup_profile::call(lookup_profile::Step::Acl, [&] { require(internal::mode_only_acl(files->lease.fd)); });",
    )
    for original, step in (
        ("::flock(files->lease.fd, LOCK_SH | LOCK_NB)", "Lock"),
        ("::fstat(files->lease.fd, &held)", "Stat"),
        (
            "::fstatat(files->directory.fd, terminated_lease.data(), &named, AT_SYMLINK_NOFOLLOW)",
            "Named",
        ),
    ):
        source = replace(
            source,
            original,
            f"lookup_profile::call(lookup_profile::Step::{step}, [&] {{ return {original}; }})",
        )
    source = replace(
        source,
        "const auto ownership = decode_generation_ownership(lease);",
        "const auto ownership = lookup_profile::call(lookup_profile::Step::Decode, [&] { return decode_generation_ownership(lease); });\n        lease_context.leave();",
    )
    for function, kind, argument in (
        (
            "decode_storage_identity",
            "Storage",
            'record<storage_identity_bytes>(files->directory.fd, "storage.meta", publisher)',
        ),
        (
            "decode_reader_metadata",
            "Metadata",
            "record<reader_metadata_bytes>(files->directory.fd, metadata_name.view(), publisher)",
        ),
    ):
        original = f"{function}(\n\t\t    {argument})"
        source = replace(
            source,
            original,
            f"[&] {{ auto bytes = {argument}; return lookup_profile::call(lookup_profile::File::{kind}, lookup_profile::Step::Decode, [&] {{ return {function}(bytes); }}); }}()",
        )
    source = replace(
        source,
        "checksum(image)",
        "lookup_profile::call(lookup_profile::File::Image, lookup_profile::Step::Checksum, [&] { return checksum(image); })",
    )
    source = replace(
        source,
        "paged::decode_root(image, paged_format_limits())",
        "lookup_profile::call(lookup_profile::File::Image, lookup_profile::Step::Decode, [&] { return paged::decode_root(image, paged_format_limits()); })",
    )
    source = replace(
        source,
        "require(root.cursor == descriptor.cursor && root.profile == descriptor.profile);",
        "lookup_profile::call(lookup_profile::File::Image, lookup_profile::Step::Binding, [&] { require(root.cursor == descriptor.cursor && root.profile == descriptor.profile); });",
    )
    for first, last, kind, step in (
        (
            "require(ownership.storage == storage &&",
            "ownership.image_checksum == descriptor.checksum);",
            "Lease",
            "Binding",
        ),
        (
            "require(metadata.storage == storage &&",
            "root.bytes <= metadata.physical_bytes - paged::root_bytes);",
            "Metadata",
            "Binding",
        ),
        (
            "require(metadata.physical_bytes <= limits.bytes &&",
            "root.segments <= limits.segments);",
            "Metadata",
            "Limits",
        ),
    ):
        start = source.index(first)
        end = source.index(last, start) + len(last)
        block = source[start:end]
        source = replace(
            source,
            block,
            f"lookup_profile::call(lookup_profile::File::{kind}, lookup_profile::Step::{step}, [&] {{ {block} }});",
        )
    start = source.index("PreparedGeneration PreparedReader::open(")
    source = source[:start] + source[start:].replace(
        "continue;", "{ lookup_profile::note(lookup_profile::Step::Retry); continue; }"
    )
    for name in (
        "check_generation_file",
        "read_generation_record",
        "open_publisher_directory",
    ):
        source = source.replace(name, "observed_" + name)
    return '#include "profile.hh"\n' + source


def main() -> None:
    for arm, folder in (("before", "baseline-build"), ("after", "reader-build")):
        build = ROOT / folder
        entries: list[dict[str, str]] = json.loads(
            (build / "compile_commands.json").read_text()
        )
        entry = next(
            row for row in entries if row["file"].endswith("/prepared_reader.cc")
        )
        original = Path(entry["file"])
        output = HERE / arm
        output.mkdir(exist_ok=True)
        source = output / "prepared_reader.cc"
        source.write_text(instrument(original.read_text()))
        helper = (original.parent / "generation_file_access.hh").read_text()
        helper = replace(
            helper,
            "::fstat(fd, &info)",
            "lookup_profile::call(lookup_profile::Step::Stat, [&] { return ::fstat(fd, &info); })",
        )
        helper = replace(
            helper,
            "::pread(fd, bytes.data() + offset, bytes.size() - offset, offset)",
            "lookup_profile::read(bytes.size() - offset, [&] { return ::pread(fd, bytes.data() + offset, bytes.size() - offset, offset); })",
        )
        for name in (
            "check_generation_file",
            "read_generation_record",
            "open_publisher_directory",
        ):
            helper = helper.replace(name, "observed_" + name)
        (output / "generation_file_access.hh").write_text(helper)
        obj = output / "prepared_reader.cc.o"
        command = shlex.split(entry["command"])
        command[command.index("-o") + 1] = str(obj)
        command[command.index("-c") + 1] = str(source)
        command += ["-I", str(HERE), "-I", str(original.parent)]
        subprocess.run(command, cwd=entry["directory"], check=True)
        archive = output / "libdetent_local_view.a"
        shutil.copyfile(build / "src/detent_local/libdetent_local_view.a", archive)
        subprocess.run(
            ["/usr/local/opt/llvm/bin/llvm-ar", "r", str(archive), str(obj)], check=True
        )
        probe = (ROOT / "diagnostic/probe.cc").read_text()
        probe = '#include "profile.hh"\n' + probe
        probe = replace(
            probe,
            "std::optional<detent::InitOptions> options;",
            "lookup_profile::enabled = false;\n        lookup_profile::reset();\n        std::optional<detent::InitOptions> options;",
        )
        probe = replace(
            probe,
            'auto construct = phase("client", [&] { client.emplace(std::move(*options)); });',
            'lookup_profile::enabled = std::getenv("METADATA_OBSERVER") && std::string_view(std::getenv("METADATA_OBSERVER")) == "1";\n        auto construct = phase("client", [&] { client.emplace(std::move(*options)); });\n        lookup_profile::enabled = false;',
        )
        probe = replace(
            probe,
            'std::cout << "]}\\n";',
            'std::cout << "]}\\n";\n        lookup_profile::dump();',
        )
        probe_source = output / "probe.cc"
        probe_source.write_text(probe)
        target = (
            build
            / "tests/detent_system/CMakeFiles/detent_system_prepared_client_probe.dir"
        )
        flags = dict(
            line.split(" = ", 1)
            for line in (target / "flags.make").read_text().splitlines()
            if " = " in line
        )
        command = ["/usr/local/opt/llvm/bin/clang++", "-I", str(HERE)]
        for key in ("CXX_DEFINES", "CXX_INCLUDES", "CXX_FLAGS"):
            command += shlex.split(flags[key])
        probe_obj = output / "probe.o"
        subprocess.run(
            [*command, "-c", str(probe_source), "-o", str(probe_obj)], check=True
        )
        link = shlex.split((target / "link.txt").read_text())
        for index, value in enumerate(link):
            if value.endswith("prepared_client_probe.cc.o"):
                link[index] = str(probe_obj)
            elif value.endswith("libdetent_local_view.a"):
                link[index] = str(archive)
            elif index and link[index - 1] == "-o":
                link[index] = str(output / "probe")
        subprocess.run(link, cwd=target.parent.parent, check=True)


if __name__ == "__main__":
    main()
