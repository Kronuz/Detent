// Compiles src/shim.cc (cxx's generated bridge glue + our own C++ shim)
// and links against an *already-built* libdetent.a -- this crate doesn't
// re-run CMake or re-resolve detent's own FetchContent dependency graph
// (sha256, cpp-btree, atomic-shared-ptr, etc.); it expects `cmake -B build
// && cmake --build build` to have already been run at the repo root (the
// same build/ directory the rest of this project's own README instructs
// every consumer to create), matching how a real consumer would use this
// crate in practice: detent's C++ build already happened once, upstream.
//
// Override with DETENT_BUILD_DIR if your C++ build directory isn't the
// repo-root-relative `build/` this defaults to (e.g. CI using a differently
// named build dir, or building against an installed detent rather than an
// in-tree one).
use std::path::{Path, PathBuf};

fn main() {
    let manifest_dir = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap());
    let repo_root = manifest_dir.join("../.."); // bindings/rust -> repo root
    let build_dir = std::env::var("DETENT_BUILD_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| repo_root.join("build"));

    let include_dir = repo_root.join("include");
    let deps_dir = build_dir.join("_deps");

    let mut bridge = cxx_build::bridge("src/lib.rs");
    bridge
        .file("src/shim.cc")
        .include(&include_dir)
        .include(deps_dir.join("rapidjson-src/include"))
        .include(deps_dir.join("char_classify-src"))
        .include(deps_dir.join("strict_stox-src"))
        .include(deps_dir.join("sha256-src"))
        .include(deps_dir.join("endian-src"))
        .include(deps_dir.join("atomic_shared_ptr-src"))
        .include(deps_dir.join("cpp_btree-src"))
        .include(deps_dir.join("random_lib-src"))
        .std("c++20")
        .flag_if_supported("-Wno-deprecated-declarations"); // rapidjson's own std::iterator use, not ours
    bridge.compile("detent-rust-bridge");

    println!("cargo:rerun-if-changed=src/lib.rs");
    println!("cargo:rerun-if-changed=src/shim.cc");
    println!("cargo:rerun-if-changed=src/shim.hh");

    link_static(&build_dir.join("src"), "detent");
    link_static(&deps_dir.join("sha256-build"), "sha256");
    link_static(&deps_dir.join("random_lib-build"), "random");
}

fn link_static(dir: &Path, name: &str) {
    println!("cargo:rustc-link-search=native={}", dir.display());
    println!("cargo:rustc-link-lib=static={name}");
}
