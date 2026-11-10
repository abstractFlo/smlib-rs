// SPDX-License-Identifier: Apache-2.0
//! Compiles `source/SMLib` and `source/SM_API` with the release flags of usd-brep's own build
//! (its premake5.lua), and the C interface in `shim/` against them, and links all three as one
//! static library, so there is no link order between them to get right on any linker.
//!
//! Dependent build scripts get `DEP_SMLIB_SOURCE` (the `source/` folder) and `DEP_SMLIB_DEFINES`
//! (the defines below that change what the headers declare), to compile C++ of their own the same
//! way.

use std::env;
use std::fs;
use std::path::{Path, PathBuf};

struct Target {
    operating_system: String,
    architecture: String,
    msvc: bool,
    atomics: bool,
}

impl Target {
    fn from_cargo() -> Self {
        let variable = |name: &str| env::var(name).unwrap_or_default();
        Target {
            operating_system: variable("CARGO_CFG_TARGET_OS"),
            architecture: variable("CARGO_CFG_TARGET_ARCH"),
            msvc: variable("CARGO_CFG_TARGET_ENV") == "msvc",
            atomics: variable("CARGO_CFG_TARGET_FEATURE").split(',').any(|feature| feature == "atomics"),
        }
    }

    fn is(&self, operating_system: &str) -> bool {
        self.operating_system == operating_system
    }

    /// Defines the library and the code calling it must agree on.
    fn shared_defines(&self) -> Vec<&'static str> {
        // SM_API_EXPORTS: on Windows the SM_API headers otherwise declare dllimport, which a static
        // library cannot satisfy; elsewhere the headers ignore it.
        let mut defines = vec!["NDEBUG", "NOMINMAX", "SM_GRAPHICS_CALLBACKS=1", "SM_API_EXPORTS"];
        if self.msvc {
            // SM_API's file functions take TCHAR strings, wchar_t in usd-brep's Windows build
            defines.extend(["UNICODE", "_UNICODE"]);
        }
        if self.is("linux") {
            defines.push("_GLIBCXX_USE_CXX11_ABI=1");
        }
        defines
    }
}

fn main() {
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").expect("cargo sets CARGO_MANIFEST_DIR"));
    let source = smlib_source(&manifest);
    let shim = manifest.join("shim");
    let target = Target::from_cargo();
    let defines = target.shared_defines();

    let smlib_include = source.join("SMLib/inc");
    let sm_api_include = source.join("SM_API/inc");

    let mut smlib = base_build(&target, &defines);
    smlib.include(&smlib_include);
    if target.msvc {
        // usd-brep's premake5.lua sets this for SMLib on Windows only
        smlib.define("NL_DEBUG_CODE", None);
    }
    let mut objects = compile_sources(&mut smlib, &source.join("SMLib/src"));

    let mut sm_api = base_build(&target, &defines);
    sm_api.include(&sm_api_include).include(&smlib_include);
    objects.extend(compile_sources(&mut sm_api, &source.join("SM_API/src")));

    let mut interface = base_build(&target, &defines);
    interface.include(&shim).include(&sm_api_include).include(&smlib_include);
    objects.extend(interface.file(shim.join("shim.cpp")).compile_intermediates());

    let mut library = cc::Build::new();
    library.cpp(true).objects(objects);
    if target.is("emscripten") {
        // Emscripten's own libc++ and libc++abi, built for WebAssembly exceptions as SMLib is
        library.cpp_link_stdlib(None);
        println!("cargo:rustc-link-arg=-fwasm-exceptions");
        println!("cargo:rustc-link-lib=c++");
        println!("cargo:rustc-link-lib=c++abi");
    }
    library.compile("smlib");

    println!("cargo:source={}", source.display());
    println!("cargo:defines={}", defines.join(";"));
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-changed={}", shim.display());
    println!("cargo:rerun-if-changed={}", source.join("SMLib").display());
    println!("cargo:rerun-if-changed={}", source.join("SM_API").display());
}

/// The SMLib sources: `source/` inside the crate in a published package, the repository's own
/// `source/` in a checkout.
fn smlib_source(manifest: &Path) -> PathBuf {
    let packaged = manifest.join("source");
    if packaged.join("SMLib").is_dir() {
        return packaged;
    }
    manifest.parent().expect("smlib-sys sits inside the repository").join("source")
}

fn base_build(target: &Target, defines: &[&str]) -> cc::Build {
    let mut build = cc::Build::new();
    build
        .cpp(true)
        .std("c++17")
        // the kernel stays fast in debug builds of whatever links it
        .opt_level(2)
        .warnings(false)
        .cargo_warnings(false);
    for define in defines {
        match define.split_once('=') {
            Some((name, value)) => build.define(name, value),
            None => build.define(define, None),
        };
    }
    if target.msvc {
        build
            .flag("/utf-8")
            .flag("/bigobj")
            .define("_CRT_SECURE_NO_WARNINGS", None)
            .define("_SCL_SECURE_NO_WARNINGS", None);
    } else {
        build.flag("-w");
    }
    if target.is("linux") && target.architecture == "aarch64" {
        // as usd-brep's Linux build: no fused multiply-add, so aarch64 rounds as x86-64 does
        build.flag("-ffp-contract=off");
    }
    if target.is("emscripten") {
        build.flag("-fwasm-exceptions");
        if target.atomics {
            build.flag("-pthread");
        }
    }
    build
}

fn compile_sources(build: &mut cc::Build, folder: &Path) -> Vec<PathBuf> {
    let mut sources: Vec<PathBuf> = fs::read_dir(folder)
        .unwrap_or_else(|error| panic!("cannot read {}: {error}", folder.display()))
        .map(|entry| entry.expect("a readable folder entry").path())
        .filter(|path| path.extension().is_some_and(|extension| extension == "cpp"))
        // the Windows DLL entry point and the precompiled header have no place in a static library;
        // SMLib sets up its thread-local storage on first use without them
        .filter(|path| !matches!(path.file_name().and_then(|name| name.to_str()), Some("SmDllMain.cpp" | "StdAfx.cpp")))
        .collect();
    sources.sort();
    build.files(&sources).compile_intermediates()
}
