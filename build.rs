use cc::Build;
use std::{env, path::Path};

fn main() {
    let ffi_header = "src/ffi.h";
    println!("rerun-if-changed={}", ffi_header);
    bindgen::builder()
        .header(ffi_header)
        .rustified_enum("*")
        .generate()
        .unwrap()
        .write_to_file(Path::new(&env::var_os("OUT_DIR").unwrap()).join("ffi.rs"))
        .unwrap();

    println!("cargo:rustc-link-search=native=ffmpeg/windows/release/lib");
    let static_libs = ["avcodec", "avfilter", "avutil", "mfx"];
    let dyn_libs = ["User32", "bcrypt", "ole32", "advapi32"];
    static_libs.map(|lib| println!("cargo:rustc-link-lib=static={}", lib));
    dyn_libs.map(|lib| println!("cargo:rustc-link-lib={}", lib));

    Build::new()
        .include("ffmpeg/windows/release/include")
        .file("src/encode.c")
        .file("src/decode.c")
        .compile("hwcodec");
}
