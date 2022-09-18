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

    let mut builder = Build::new();

    #[cfg(target_os = "windows")]
    {
        println!("cargo:rustc-link-search=native=ffmpeg/windows/release/lib");
        let static_libs = [
            "avcodec", "avfilter", "avutil", "avformat", "avdevice", "mfx",
        ];
        static_libs.map(|lib| println!("cargo:rustc-link-lib=static={}", lib));
        let dyn_libs = ["User32", "bcrypt", "ole32", "advapi32"];
        dyn_libs.map(|lib| println!("cargo:rustc-link-lib={}", lib));
        builder.include("ffmpeg/windows/release/include");
    }

    #[cfg(target_os = "linux")]
    {
        println!("cargo:rustc-link-search=native=ffmpeg/linux/release/lib");
        let static_libs = ["avcodec", "avfilter", "avutil", "avdevice", "avformat"];
        static_libs.map(|lib| println!("cargo:rustc-link-lib=static={}", lib));
        let dyn_libs = ["va", "va-drm", "va-x11", "vdpau", "X11", "z"];
        dyn_libs.map(|lib| println!("cargo:rustc-link-lib={}", lib));
        builder.include("ffmpeg/linux/release/include");
    }

    builder
        .file("src/encode.c")
        .file("src/decode.c")
        .file("src/mux.c")
        .file("src/common.c")
        .compile("hwcodec");
}
