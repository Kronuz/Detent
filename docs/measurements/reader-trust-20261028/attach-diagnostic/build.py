from __future__ import annotations

import json
import shlex
import shutil
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent


def main() -> None:
    for arm, folder in (("before", "baseline-build"), ("after", "reader-build")):
        build = ROOT / folder
        entries: list[dict[str, str]] = json.loads(
            (build / "compile_commands.json").read_text()
        )
        entry = next(x for x in entries if x["file"].endswith("/prepared_reader.cc"))
        original = Path(entry["file"])
        source = original.read_text()
        replacements = (
            (
                "\tauto &domain = resources::domain(limits);",
                '    lookup_profile::Scope total("attachment_total");\n\tauto &domain = resources::domain(limits);',
            ),
            (
                "\t\tauto files = factory.make<PreparedFiles>();",
                '\t\tauto files = [&] { lookup_profile::Scope scope("files_control"); return factory.make<PreparedFiles>(); }();',
            ),
            (
                "files->directory.fd = open_publisher_directory(path, publisher);",
                'files->directory.fd = [&] { lookup_profile::Scope scope("directory_trust"); return open_publisher_directory(path, publisher); }();',
            ),
            (
                "template <std::size_t N> std::array<char, N> record(int directory, std::string_view name, uid_t owner) {",
                'template <std::size_t N> std::array<char, N> record(int directory, std::string_view name, uid_t owner) {\n    lookup_profile::Scope scope("metadata_record");',
            ),
            (
                "\t\tauto mappings = resources::mapping_account(limits);",
                '\t\tauto mappings = [&] { lookup_profile::Scope scope("mapping_account"); return resources::mapping_account(limits); }();',
            ),
            (
                "\t\tusing Files = d::RetirementHandle<PreparedFiles>;",
                '        lookup_profile::Scope controls("view_controls");\n\t\tusing Files = d::RetirementHandle<PreparedFiles>;',
            ),
        )
        for old, new in replacements:
            if source.count(old) != 1:
                raise ValueError(f"Unexpected source: {old}")
            source = source.replace(old, new)
        source = '#include "profile.hh"\n' + source
        output = HERE / arm
        output.mkdir(exist_ok=True)
        copied = output / "prepared_reader.cc"
        copied.write_text(source)
        obj = output / "prepared_reader.cc.o"
        command = shlex.split(entry["command"])
        command[command.index("-o") + 1] = str(obj)
        command[command.index("-c") + 1] = str(copied)
        command.extend(["-I", str(HERE), "-I", str(original.parent)])
        subprocess.run(command, cwd=entry["directory"], check=True)
        archive = output / "libdetent_local_view.a"
        shutil.copyfile(build / "src/detent_local/libdetent_local_view.a", archive)
        subprocess.run(
            ["/usr/local/opt/llvm/bin/llvm-ar", "r", str(archive), str(obj)], check=True
        )
        probe = (ROOT / "diagnostic/probe.cc").read_text()
        probe = '#include "profile.hh"\n' + probe
        probe = probe.replace(
            'auto construct = phase("client", [&] { client.emplace(std::move(*options)); });',
            'lookup_profile::enabled = true;\n        auto construct = phase("client", [&] { client.emplace(std::move(*options)); });\n        lookup_profile::enabled = false;',
        )
        probe = probe.replace(
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
        for name in ("CXX_DEFINES", "CXX_INCLUDES", "CXX_FLAGS"):
            command.extend(shlex.split(flags[name]))
        probe_obj = output / "probe.o"
        subprocess.run(
            [*command, "-c", str(probe_source), "-o", str(probe_obj)], check=True
        )
        link = shlex.split((target / "link.txt").read_text())
        for i, arg in enumerate(link):
            if arg.endswith("prepared_client_probe.cc.o"):
                link[i] = str(probe_obj)
            elif arg.endswith("libdetent_local_view.a"):
                link[i] = str(archive)
            elif i and link[i - 1] == "-o":
                link[i] = str(output / "probe")
        subprocess.run(link, cwd=target.parent.parent, check=True)


if __name__ == "__main__":
    main()
