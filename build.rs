// use bindgen::callbacks::DeriveInfo;
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
    println!("cargo:rerun-if-changed=ffmpeg");
    println!("cargo:rerun-if-changed={}", externals_dir.display());
    println!("cargo:rerun-if-changed={}", cpp_dir.display());
    let mut builder = Build::new();

    link_ffmpeg(&mut builder);
    build_common(&mut builder);
    build_ffmpeg_ram(&mut builder);
    #[cfg(feature = "sdk")]
    sdk::build_sdk(&mut builder);
    build_mux(&mut builder);

    builder.compile("hwcodec");
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

    #[cfg(target_os = "windows")]
    {
        println!("cargo:rustc-link-search=native=ffmpeg/windows/release/lib");
        let static_libs = ["avcodec", "avfilter", "avutil", "avformat", "avdevice"];
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

    builder
        .files(["ffmpeg_ram_encode.cpp", "ffmpeg_ram_decode.cpp"].map(|f| ffmpeg_ram_dir.join(f)));
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

    // tool
    builder.files(["log.cpp", "utils.c", "data.c"].map(|f| common_dir.join(f)));
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

#[cfg(feature = "sdk")]
mod sdk {
    use super::*;

    pub(crate) fn build_sdk(builder: &mut Build) {
        build_amf(builder);
        build_nv(builder);
        build_vpl(builder);
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
        let sdk_path = externals_dir.join("Video_Codec_SDK_11.1.5");
        builder.includes([
            sdk_path.clone(),
            sdk_path.join("Interface"),
            sdk_path.join("Samples").join("Utils"),
            sdk_path.join("Samples").join("NvCodec"),
            sdk_path.join("Samples").join("NvCodec").join("NVEncoder"),
            sdk_path.join("Samples").join("NvCodec").join("NVDecoder"),
        ]);
        for file in vec!["NvEncoder.cpp", "NvEncoderCuda.cpp", "NvEncoderD3D11.cpp"] {
            builder.file(
                sdk_path
                    .join("Samples")
                    .join("NvCodec")
                    .join("NvEncoder")
                    .join(file),
            );
        }
        for file in vec!["NvDecoder.cpp"] {
            builder.file(
                sdk_path
                    .join("Samples")
                    .join("NvCodec")
                    .join("NvDecoder")
                    .join(file),
            );
        }

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
        let amf_path = externals_dir.join("AMF_v1.4.29");
        builder.include(format!("{}/amf/public/common", amf_path.display()));
        builder.include(amf_path.join("amf"));
        for f in vec![
            "AMFFactory.cpp",
            "AMFSTL.cpp",
            "Thread.cpp",
            #[cfg(windows)]
            "Windows/ThreadWindows.cpp",
            #[cfg(target_os = "linux")]
            "Linux/ThreadLinux.cpp",
            "TraceAdapter.cpp",
        ] {
            builder.file(format!("{}/amf/public/common/{}", amf_path.display(), f));
        }

        // crate
        builder.files(["amf_encode.cpp", "amf_decode.cpp"].map(|f| amf_dir.join(f)));
    }

    fn build_vpl(builder: &mut Build) {
        let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
        let externals_dir = manifest_dir.join("externals");
        let vpl_dir = manifest_dir.join("cpp").join("vpl");
        println!("cargo:rerun-if-changed=src");
        println!("cargo:rerun-if-changed={}", externals_dir.display());
        bindgen::builder()
            .header(&vpl_dir.join("vpl_ffi.h").to_string_lossy().to_string())
            .rustified_enum("*")
            .generate()
            .unwrap()
            .write_to_file(Path::new(&env::var_os("OUT_DIR").unwrap()).join("vpl_ffi.rs"))
            .unwrap();

        let sdk_path = externals_dir.join("libvpl_v2023.4.0");

        // libvpl
        let libvpl_path = sdk_path.join("libvpl");
        let api_path = sdk_path.join("api");
        let legacy_tool_path = sdk_path.join("tools").join("legacy");
        let samples_common_path = legacy_tool_path.join("sample_common");

        builder
            .includes([
                &libvpl_path,
                &api_path,
                &samples_common_path.join("include"),
                &samples_common_path.join("include").join("vm"),
                &legacy_tool_path.join("media_sdk_compatibility_headers"),
            ])
            .files(
                [
                    "mfx_dispatcher_vpl.cpp",
                    "mfx_dispatcher_vpl_config.cpp",
                    "mfx_dispatcher_vpl_loader.cpp",
                    "mfx_dispatcher_vpl_log.cpp",
                    "mfx_dispatcher_vpl_lowlatency.cpp",
                    "mfx_dispatcher_vpl_msdk.cpp",
                ]
                .map(|f| libvpl_path.join("src").join(f)),
            )
            .files(
                [
                    "mfx_dispatcher_main.cpp",
                    "mfx_critical_section.cpp",
                    "mfx_dispatcher.cpp",
                    "mfx_dispatcher_log.cpp",
                    "mfx_driver_store_loader.cpp",
                    "mfx_dxva2_device.cpp",
                    "mfx_function_table.cpp",
                    "mfx_library_iterator.cpp",
                    "mfx_load_dll.cpp",
                    "mfx_win_reg_key.cpp",
                ]
                .map(|f| libvpl_path.join("src").join("windows").join(f)),
            )
            .files(
                [
                    "mfx_config_interface.cpp",
                    "mfx_config_interface_string_api.cpp",
                ]
                .map(|f| libvpl_path.join("src").join("mfx_config_interface").join(f)),
            )
            .files(
                [
                    "sample_utils.cpp",
                    "base_allocator.cpp",
                    "d3d11_allocator.cpp",
                    "avc_bitstream.cpp",
                    "avc_spl.cpp",
                    "avc_nal_spl.cpp",
                ]
                .map(|f| samples_common_path.join("src").join(f)),
            )
            .files(
                [
                    "time.cpp",
                    "atomic.cpp",
                    "shared_object.cpp",
                    "thread_windows.cpp",
                ]
                .map(|f| samples_common_path.join("src").join("vm").join(f)),
            );

        // link
        [
            "kernel32", "user32", "gdi32", "winspool", "shell32", "ole32", "oleaut32", "uuid",
            "comdlg32", "advapi32", "d3d11", "dxgi",
        ]
        .map(|lib| println!("cargo:rustc-link-lib={}", lib));

        builder
            .files(["vpl_encode.cpp", "vpl_decode.cpp"].map(|f| vpl_dir.join(f)))
            .define("NOMINMAX", None)
            .define("MFX_DEPRECATED_OFF", None)
            .define("MFX_D3D11_SUPPORT", None);
    }
}
