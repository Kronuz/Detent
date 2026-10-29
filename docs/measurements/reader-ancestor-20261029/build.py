from __future__ import annotations

import json
import shlex
import shutil
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent / "resource-adoption-20261027"


def change(source: str, before: str, after: str) -> str:
    if source.count(before) != 1:
        raise ValueError(f"Unexpected call site: {before}")
    return source.replace(before, after)


def trace_header(source: str) -> str:
    start = source.index("inline int open_publisher_directory(")
    prefix, body = source[:start], source[start:]
    calls = (
        (
            '::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)',
            "open",
            '"/"',
            "O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW",
        ),
        (
            "::openat(current.fd, part.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)",
            "openat",
            "part.c_str()",
            "O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW",
        ),
        ("::fstat(current.fd, &anchor)", "fstat", '""', "0"),
        ("::fstat(current.fd, &parent)", "fstat", '""', "0"),
        ("::fstat(next.fd, &held)", "fstat", '""', "0"),
        ("::fstat(current.fd, &final_status)", "fstat", '""', "0"),
        (
            "::fstatat(current.fd, part.c_str(), &entry, AT_SYMLINK_NOFOLLOW)",
            "fstatat",
            "part.c_str()",
            "AT_SYMLINK_NOFOLLOW",
        ),
        (
            "::fstatat(current.fd, part.c_str(), &after, AT_SYMLINK_NOFOLLOW)",
            "fstatat",
            "part.c_str()",
            "AT_SYMLINK_NOFOLLOW",
        ),
        (
            "::fstatat(current.fd, part.c_str(), &named, AT_SYMLINK_NOFOLLOW)",
            "fstatat",
            "part.c_str()",
            "AT_SYMLINK_NOFOLLOW",
        ),
        (
            "::readlinkat(current.fd, part.c_str(), target.data(), target.size())",
            "readlinkat",
            "part.c_str()",
            "0",
        ),
        (
            "internal::acceptable_acl(current.fd, internal::AclPolicy::DenialsAllowed)",
            "acl",
            '""',
            "int(internal::AclPolicy::DenialsAllowed)",
        ),
        (
            "internal::acceptable_acl(next.fd, internal::AclPolicy::DenialsAllowed)",
            "acl",
            '""',
            "int(internal::AclPolicy::DenialsAllowed)",
        ),
    )
    for original, kind, name, flags in calls:
        body = change(
            body,
            original,
            f'ancestor_trace::call("{kind}", {name}, {flags}, [&] {{ return {original}; }})',
        )
    return prefix + body


def main() -> None:
    for arm, folder, proposed in (
        ("private-current", "baseline-build", False),
        ("private-proposed", "baseline-build", True),
        ("shared-current", "reader-build", False),
        ("shared-proposed", "reader-build", True),
    ):
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
        source_text = original.read_text()
        baseline = original.parent / "generation_file_access.hh"
        if (
            baseline.read_bytes()
            != (
                ROOT / "baseline-source/src/detent_local/generation_file_access.hh"
            ).read_bytes()
        ):
            raise ValueError("Resource traversal bodies differ")
        helper = (
            (
                HERE / "private-source/src/detent_local/generation_file_access.hh"
            ).read_text()
            if proposed
            else baseline.read_text()
        )
        helper = trace_header(helper)
        for name in (
            "check_generation_file",
            "read_generation_record",
            "open_publisher_directory",
        ):
            helper = helper.replace(name, "screen_" + name)
            source_text = source_text.replace(name, "screen_" + name)
        source.write_text('#include "trace.hh"\n' + source_text)
        (output / "generation_file_access.hh").write_text(helper)
        obj = output / "prepared_reader.cc.o"
        command = shlex.split(entry["command"])
        command[command.index("-o") + 1] = str(obj)
        command[command.index("-c") + 1] = str(source)
        command += ["-I", str(HERE), "-I", str(original.parent), "-fstack-usage"]
        subprocess.run(command, cwd=entry["directory"], check=True)
        archive = output / "libdetent_local_view.a"
        shutil.copyfile(build / "src/detent_local/libdetent_local_view.a", archive)
        subprocess.run(
            ["/usr/local/opt/llvm/bin/llvm-ar", "r", str(archive), str(obj)], check=True
        )
        probe = (ROOT / "diagnostic/probe.cc").read_text()
        probe = '#include "trace.hh"\n' + probe
        probe = change(
            probe,
            "std::optional<detent::InitOptions> options;",
            "ancestor_trace::enabled = false;\n        ancestor_trace::reset();\n        std::optional<detent::InitOptions> options;",
        )
        probe = change(
            probe,
            'auto construct = phase("client", [&] { client.emplace(std::move(*options)); });',
            'ancestor_trace::enabled = true;\n        auto construct = phase("client", [&] { client.emplace(std::move(*options)); });\n        ancestor_trace::enabled = false;',
        )
        probe = change(
            probe,
            'std::cout << "]}\\n";',
            'std::cout << "]}\\n";\n        ancestor_trace::dump();',
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
