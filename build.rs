use cc::Build;
use std::{
    env,
    path::{Path, PathBuf},
};

fn main() {
    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let externals_dir = manifest_dir.join("externals");
    let cpp_dir = manifest_dir.join("cpp");
    println!("cargo:rerun-if-changed=src");
    println!("cargo:rerun-if-changed=deps");
    println!("cargo:rerun-if-changed={}", externals_dir.display());
    println!("cargo:rerun-if-changed={}", cpp_dir.display());
    let mut builder = Build::new();

    build_common(&mut builder);
    ffmpeg::build_ffmpeg(&mut builder);
    #[cfg(all(windows, feature = "vram"))]
    sdk::build_sdk(&mut builder);
    builder.static_crt(true).compile("hwcodec");
}

fn build_common(builder: &mut Build) {
    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let common_dir = manifest_dir.join("cpp").join("common");
    bindgen::builder()
        .header(common_dir.join("common.h").to_string_lossy().to_string())
        .header(common_dir.join("callback.h").to_string_lossy().to_string())
        .rustified_enum("*")
        .parse_callbacks(Box::new(CommonCallbacks))
        .generate()
        .unwrap()
        .write_to_file(Path::new(&env::var_os("OUT_DIR").unwrap()).join("common_ffi.rs"))
        .unwrap();

    // system
    #[cfg(windows)]
    {
        ["d3d11", "dxgi"].map(|lib| println!("cargo:rustc-link-lib={}", lib));
    }

    builder.include(&common_dir);

    // platform
    let _platform_path = common_dir.join("platform");
    #[cfg(windows)]
    {
        let win_path = _platform_path.join("win");
        builder.include(&win_path);
        builder.file(win_path.join("win.cpp"));
    }
    #[cfg(target_os = "linux")]
    {
        let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
        let externals_dir = manifest_dir.join("externals");
        // ffnvcodec
        let ffnvcodec_path = externals_dir
            .join("nv-codec-headers_n11.1.5.2")
            .join("include")
            .join("ffnvcodec");
        builder.include(ffnvcodec_path);

        let linux_path = _platform_path.join("linux");
        builder.include(&linux_path);
        builder.file(linux_path.join("linux.cpp"));
    }
    #[cfg(target_os = "macos")]
    {
        let macos_path = _platform_path.join("mac");
        builder.include(&macos_path);
        builder.file(macos_path.join("mac.mm"));
    }

    // tool
    builder.files(["log.cpp", "util.cpp"].map(|f| common_dir.join(f)));
}

#[derive(Debug)]
struct CommonCallbacks;
impl bindgen::callbacks::ParseCallbacks for CommonCallbacks {
    fn add_derives(&self, name: &str) -> Vec<String> {
        let names = vec!["DataFormat", "SurfaceFormat", "API"];
        if names.contains(&name) {
            vec!["Serialize", "Deserialize"]
                .drain(..)
                .map(|s| s.to_string())
                .collect()
        } else {
            vec![]
        }
    }
}

// android: both #[cfg(target_os = "linux")] and cfg!("target_os = "linux) is true, CARGO_CFG_TARGET_OS is android
fn get_ffmpeg_arch() -> String {
    let target_arch = std::env::var("CARGO_CFG_TARGET_ARCH").unwrap();
    // https://doc.rust-lang.org/reference/conditional-compilation.html#target_os
    let target_os = std::env::var("CARGO_CFG_TARGET_OS").unwrap();
    println!("ffmpeg: target_os: {target_os}, target_arch: {target_arch}");
    let arch_dir = match target_os.as_str() {
        "windows" => {
            if target_arch == "x86_64" {
                "windows-x86_64"
            } else if target_arch == "x86" {
                "windows-i686"
            } else {
                panic!("unsupported target_arch: {target_arch}");
            }
        }
        "linux" => {
            if target_arch == "x86_64" {
                "linux-x86_64"
            } else if target_arch == "aarch64" {
                "linux-aarch64"
            } else if target_arch == "arm" {
                "linux-armv7"
            } else {
                panic!("unsupported target_arch: {target_arch}");
            }
        }
        "macos" => {
            if target_arch == "aarch64" {
                "macos-aarch64"
            } else if target_arch == "x86_64" {
                "macos-x86_64"
            } else {
                panic!("unsupported target_arch: {target_arch}");
            }
        }
        "android" => {
            if target_arch == "aarch64" {
                "android-aarch64"
            } else if target_arch == "arm" {
                "android-armv7"
            } else {
                panic!("unsupported target_arch: {target_arch}");
            }
        }
        "ios" => {
            if target_arch == "aarch64" {
                "ios-aarch64"
            } else {
                panic!("unsupported target_arch: {target_arch}");
            }
        }
        _ => panic!("unsupported os"),
    };
    arch_dir.to_string()
}

mod ffmpeg {
    use core::panic;

    use super::*;

    pub fn build_ffmpeg(builder: &mut Build) {
        link_ffmpeg(builder);
        build_ffmpeg_ram(builder);
        #[cfg(feature = "vram")]
        build_ffmpeg_vram(builder);
        build_mux(builder);
    }

    fn link_ffmpeg(builder: &mut Build) {
        let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
        let ffmpeg_ram_dir = manifest_dir.join("cpp").join("common");
        let ffi_header = ffmpeg_ram_dir
            .join("ffmpeg_ffi.h")
            .to_string_lossy()
            .to_string();
        bindgen::builder()
            .header(ffi_header)
            .rustified_enum("*")
            .generate()
            .unwrap()
            .write_to_file(Path::new(&env::var_os("OUT_DIR").unwrap()).join("ffmpeg_ffi.rs"))
            .unwrap();

        let arch_dir = get_ffmpeg_arch();
        let target_os = std::env::var("CARGO_CFG_TARGET_OS").unwrap();
        let target_arch = std::env::var("CARGO_CFG_TARGET_ARCH").unwrap();
        println!("cargo:rustc-link-search=native=deps/ffmpeg/{arch_dir}/lib");
        let mut static_libs = vec!["avcodec", "avutil", "avformat"];
        if target_os == "windows" {
            static_libs.push("mfx");
        }
        static_libs
            .iter()
            .map(|lib| println!("cargo:rustc-link-lib=static={}", lib))
            .count();
        let dyn_libs: Vec<&str> = if target_os == "windows" {
            ["User32", "bcrypt", "ole32", "advapi32"].to_vec()
        } else if target_os == "linux" {
            let mut v = ["va", "va-drm", "va-x11", "vdpau", "X11", "stdc++"].to_vec();
            if target_arch == "x86_64" {
                v.push("z");
            }
            v
        } else if target_os == "macos" || target_os == "ios" {
            ["c++", "m"].to_vec()
        } else if target_os == "android" {
            ["z", "m", "android", "atomic"].to_vec()
        } else {
            panic!("unsupported os");
        };
        dyn_libs
            .iter()
            .map(|lib| println!("cargo:rustc-link-lib={}", lib))
            .count();
        builder.include(format!("deps/ffmpeg/{arch_dir}/include"));

        if target_os == "macos" || target_os == "ios" {
            println!("cargo:rustc-link-lib=framework=CoreFoundation");
            println!("cargo:rustc-link-lib=framework=CoreVideo");
            println!("cargo:rustc-link-lib=framework=CoreMedia");
            println!("cargo:rustc-link-lib=framework=VideoToolbox");
            println!("cargo:rustc-link-lib=framework=AVFoundation");
            builder.flag("-std=c++11");
        }
    }

    fn build_ffmpeg_ram(builder: &mut Build) {
        let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
        let ffmpeg_ram_dir = manifest_dir.join("cpp").join("ffmpeg_ram");
        let ffi_header = ffmpeg_ram_dir
            .join("ffmpeg_ram_ffi.h")
            .to_string_lossy()
            .to_string();
        bindgen::builder()
            .header(ffi_header)
            .rustified_enum("*")
            .generate()
            .unwrap()
            .write_to_file(Path::new(&env::var_os("OUT_DIR").unwrap()).join("ffmpeg_ram_ffi.rs"))
            .unwrap();

        builder.files(
            ["ffmpeg_ram_encode.cpp", "ffmpeg_ram_decode.cpp"].map(|f| ffmpeg_ram_dir.join(f)),
        );
    }

    #[cfg(feature = "vram")]
    fn build_ffmpeg_vram(builder: &mut Build) {
        let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
        let ffmpeg_ram_dir = manifest_dir.join("cpp").join("ffmpeg_vram");
        let ffi_header = ffmpeg_ram_dir
            .join("ffmpeg_vram_ffi.h")
            .to_string_lossy()
            .to_string();
        bindgen::builder()
            .header(ffi_header)
            .rustified_enum("*")
            .generate()
            .unwrap()
            .write_to_file(Path::new(&env::var_os("OUT_DIR").unwrap()).join("ffmpeg_vram_ffi.rs"))
            .unwrap();

        builder.files(
            ["ffmpeg_vram_decode.cpp", "ffmpeg_vram_encode.cpp"].map(|f| ffmpeg_ram_dir.join(f)),
        );
    }

    fn build_mux(builder: &mut Build) {
        let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
        let mux_dir = manifest_dir.join("cpp").join("mux");
        let mux_header = mux_dir.join("mux_ffi.h").to_string_lossy().to_string();
        bindgen::builder()
            .header(mux_header)
            .rustified_enum("*")
            .generate()
            .unwrap()
            .write_to_file(Path::new(&env::var_os("OUT_DIR").unwrap()).join("mux_ffi.rs"))
            .unwrap();

        builder.files(["mux.cpp"].map(|f| mux_dir.join(f)));
    }
}

#[cfg(all(windows, feature = "vram"))]
mod sdk {
    use super::*;

    pub(crate) fn build_sdk(builder: &mut Build) {
        let target_arch = std::env::var("CARGO_CFG_TARGET_ARCH").unwrap();
        let arch_dir = if target_arch == "x86_64" {
            "windows-x86_64"
        } else if target_arch == "x86" {
            "windows-i686"
        } else {
            panic!("unsupported target_arch: {target_arch}");
        };
        println!("cargo:rustc-link-search=native=deps/sdk/{arch_dir}");
        build_amf(builder);
        build_nv(builder);
        build_mfx(builder);
    }

    fn build_nv(builder: &mut Build) {
        let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
        let externals_dir = manifest_dir.join("externals");
        let common_dir = manifest_dir.join("common");
        let nv_dir = manifest_dir.join("cpp").join("nv");
        println!("cargo:rerun-if-changed=src");
        println!("cargo:rerun-if-changed={}", common_dir.display());
        println!("cargo:rerun-if-changed={}", externals_dir.display());
        bindgen::builder()
            .header(&nv_dir.join("nv_ffi.h").to_string_lossy().to_string())
            .rustified_enum("*")
            .generate()
            .unwrap()
            .write_to_file(Path::new(&env::var_os("OUT_DIR").unwrap()).join("nv_ffi.rs"))
            .unwrap();

        // system
        #[cfg(target_os = "windows")]
        [
            "kernel32", "user32", "gdi32", "winspool", "shell32", "ole32", "oleaut32", "uuid",
            "comdlg32", "advapi32", "d3d11", "dxgi",
        ]
        .map(|lib| println!("cargo:rustc-link-lib={}", lib));
        #[cfg(target_os = "linux")]
        println!("cargo:rustc-link-lib=stdc++");

        // ffnvcodec
        let ffnvcodec_path = externals_dir
            .join("nv-codec-headers_n11.1.5.2")
            .join("include")
            .join("ffnvcodec");
        builder.include(ffnvcodec_path);

        // video codc sdk
        println!("cargo:rustc-link-lib=static=video_codec_sdk");
        let sdk_path = externals_dir.join("Video_Codec_SDK_11.1.5");
        builder.includes([
            sdk_path.clone(),
            sdk_path.join("Interface"),
            sdk_path.join("Samples").join("Utils"),
            sdk_path.join("Samples").join("NvCodec"),
            sdk_path.join("Samples").join("NvCodec").join("NVEncoder"),
            sdk_path.join("Samples").join("NvCodec").join("NVDecoder"),
        ]);

        // crate
        builder.files(["nv_encode.cpp", "nv_decode.cpp"].map(|f| nv_dir.join(f)));
    }

    fn build_amf(builder: &mut Build) {
        let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
        let externals_dir = manifest_dir.join("externals");
        let amf_dir = manifest_dir.join("cpp").join("amf");
        println!("cargo:rerun-if-changed=src");
        println!("cargo:rerun-if-changed={}", externals_dir.display());
        bindgen::builder()
            .header(amf_dir.join("amf_ffi.h").to_string_lossy().to_string())
            .rustified_enum("*")
            .generate()
            .unwrap()
            .write_to_file(Path::new(&env::var_os("OUT_DIR").unwrap()).join("amf_ffi.rs"))
            .unwrap();

        // system
        #[cfg(windows)]
        println!("cargo:rustc-link-lib=ole32");
        #[cfg(target_os = "linux")]
        println!("cargo:rustc-link-lib=stdc++");

        // amf
        println!("cargo:rustc-link-lib=static=amf");
        let amf_path = externals_dir.join("AMF_v1.4.29");
        builder.include(format!("{}/amf/public/common", amf_path.display()));
        builder.include(amf_path.join("amf"));

        // crate
        builder.files(["amf_encode.cpp", "amf_decode.cpp"].map(|f| amf_dir.join(f)));
    }

    fn build_mfx(builder: &mut Build) {
        let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
        let externals_dir = manifest_dir.join("externals");
        let mfx_dir = manifest_dir.join("cpp").join("mfx");
        println!("cargo:rerun-if-changed=src");
        println!("cargo:rerun-if-changed={}", externals_dir.display());
        bindgen::builder()
            .header(&mfx_dir.join("mfx_ffi.h").to_string_lossy().to_string())
            .rustified_enum("*")
            .generate()
            .unwrap()
            .write_to_file(Path::new(&env::var_os("OUT_DIR").unwrap()).join("mfx_ffi.rs"))
            .unwrap();

        // MediaSDK
        let sdk_path = externals_dir.join("MediaSDK_22.5.4");

        // mfx_dispatch
        let mfx_path = sdk_path.join("api").join("mfx_dispatch");
        // include headers and reuse static lib
        builder.include(mfx_path.join("windows").join("include"));
        let arch_dir = get_ffmpeg_arch();
        println!("cargo:rustc-link-search=native=deps/ffmpeg/{arch_dir}/lib");
        println!("cargo:rustc-link-lib=static=mfx");

        let sample_path = sdk_path.join("samples").join("sample_common");
        builder
            .includes([
                sdk_path.join("api").join("include"),
                sample_path.join("include"),
            ])
            .files(
                [
                    "sample_utils.cpp",
                    "base_allocator.cpp",
                    "d3d11_allocator.cpp",
                    "avc_bitstream.cpp",
                    "avc_spl.cpp",
                    "avc_nal_spl.cpp",
                ]
                .map(|f| sample_path.join("src").join(f)),
            )
            .files(
                [
                    "time.cpp",
                    "atomic.cpp",
                    "shared_object.cpp",
                    "thread_windows.cpp",
                ]
                .map(|f| sample_path.join("src").join("vm").join(f)),
            );

        // link
        [
            "kernel32", "user32", "gdi32", "winspool", "shell32", "ole32", "oleaut32", "uuid",
            "comdlg32", "advapi32", "d3d11", "dxgi",
        ]
        .map(|lib| println!("cargo:rustc-link-lib={}", lib));

        builder
            .files(["mfx_encode.cpp", "mfx_decode.cpp"].map(|f| mfx_dir.join(f)))
            .define("NOMINMAX", None)
            .define("MFX_DEPRECATED_OFF", None)
            .define("MFX_D3D11_SUPPORT", None);
    }
}
