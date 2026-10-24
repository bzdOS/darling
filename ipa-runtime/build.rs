use std::path::PathBuf;
use std::process::Command;

fn main() {
    let out_dir = PathBuf::from(std::env::var("OUT_DIR").unwrap());
    let src_dir = PathBuf::from("Metal/src");
    let include_dir = PathBuf::from("Metal/include");

    // aarch64 FreeBSD sysroot providing arch-correct C headers.
    let sysroot = std::env::var("AARCH64_FREEBSD_SYSROOT")
        .unwrap_or_else(|_| "/usr/local/freebsd-sysroot/aarch64".to_string());

    // clang and llvm-ar are expected on PATH.  No host x86-64 target is used
    // — the stubs are cross-compiled to aarch64-unknown-freebsd15.0.
    let stubs = [
        "MTLDevice",
        "MTLCommandQueue",
        "MTLLibrary",
        "MTLBuffer",
        "MTLTexture",
        "MTLRenderPipeline",
    ];
    let mut objs = Vec::new();

    for s in &stubs {
        let src = src_dir.join(format!("{}.c", s));
        let obj = out_dir.join(format!("{}.o", s));
        let status = Command::new("clang")
            .args([
                "--target=aarch64-unknown-freebsd15.0",
                "--sysroot",
                &sysroot,
                "-I",
                &include_dir.to_string_lossy(),
                "-I",
                "/usr/local/include",
                "-c",
                &src.to_string_lossy(),
                "-o",
                &obj.to_string_lossy(),
            ])
            .status()
            .expect("failed to run clang");
        assert!(status.success(), "clang failed for {}", s);
        objs.push(obj);
    }

    let archive = out_dir.join("libmetal_stub.a");
    let mut cmd = Command::new("llvm-ar");
    cmd.arg("rcs").arg(&archive);
    for o in &objs {
        cmd.arg(o);
    }
    let status = cmd.status().expect("failed to run llvm-ar");
    assert!(status.success(), "llvm-ar failed");

    println!("cargo:rerun-if-changed=Metal/src");
    println!("cargo:rerun-if-change=Metal/include");
    println!("cargo:rustc-link-search={}", out_dir.display());
    println!("cargo:rustc-link-lib=static=metal_stub");
}
