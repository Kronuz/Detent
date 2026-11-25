// Links the already-built shared libdetent_reader. This crate compiles no C++ and does not re-run CMake:
// it expects `cmake -B build -DDETENT_BUILD_RUST=ON && cmake --build build` first, the same build
// directory the rest of the project uses.
//
// Override with DETENT_BUILD_DIR when that directory is not <repo-root>/build.
use std::path::PathBuf;

fn main() {
    let manifest_dir = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap());
    let build_dir = std::env::var("DETENT_BUILD_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| manifest_dir.join("../../build"));
    println!("cargo:rerun-if-env-changed=DETENT_BUILD_DIR");

    // The reader is linked into the library; the test writer only into the tests, which declare it.
    for dir in [build_dir.join("src/detent_reader"), build_dir.join("src/detent_testing")] {
        println!("cargo:rustc-link-search=native={}", dir.display());
        println!("cargo:rustc-link-arg=-Wl,-rpath,{}", dir.display());
    }
    println!("cargo:rustc-link-lib=dylib=detent_reader");
}
