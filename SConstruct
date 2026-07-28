from pathlib import Path
import os
import platform
import shutil
import sys

arch = platform.machine()
version = "v0.0.4"
project_path = Path(os.getcwd())
local_sysroot = project_path / "static_lib"

sysroot_candidates = []
sysroot_override = os.environ.get("CARDPUTERZERO_SYSROOT")
if sysroot_override:
    sysroot_candidates.append(Path(sysroot_override))
sysroot_candidates += [
    local_sysroot,
    project_path.parent / "CameraApp" / ".cache" / "sdk_bsp-src",
    project_path.parent / "test" / "static_lib",
]

def current_sysroot(path):
    try:
        return (
            (path / "version").read_text().strip() == version
            and (path / "usr/include/aarch64-linux-gnu/bits/wordsize.h").is_file()
            and (path / "usr/include/aarch64-linux-gnu/bits/libc-header-start.h").is_file()
            and (path / "usr/lib/aarch64-linux-gnu").is_dir()
        )
    except Exception:
        return False

sysroot_path = next((path for path in sysroot_candidates if current_sysroot(path)), local_sysroot)
update = ("CardputerZero" in os.environ and not current_sysroot(sysroot_path))

toolchain_path = os.environ.get("CARDPUTERZERO_TOOLCHAIN_PATH")
toolchain_prefix = os.environ.get("CARDPUTERZERO_TOOLCHAIN_PREFIX")
if not toolchain_prefix:
    for candidate in ("aarch64-linux-gnu-", "aarch64-unknown-linux-gnu-"):
        compiler = shutil.which(candidate + "gcc")
        if compiler:
            toolchain_path = str(Path(compiler).parent)
            toolchain_prefix = candidate
            break

platform_config = Path("build/config/config_tmp.mk")
platform_config.parent.mkdir(parents=True, exist_ok=True)
config_lines = Path("config_defaults.mk").read_text().rstrip() + "\n"

if "CardputerZero" in os.environ or arch == "aarch64":
    config_lines += "CONFIG_V9_5_LV_USE_LINUX_FBDEV=y\n"
    config_lines += "CONFIG_V9_5_LV_USE_EVDEV=y\n"
    config_lines += "CONFIG_V9_5_LV_DRAW_SW_ASM_NEON=y\n"
    config_lines += "CONFIG_V9_5_LV_USE_DRAW_SW_ASM=1\n"
    if "CardputerZero" in os.environ:
        if not toolchain_prefix:
            raise RuntimeError(
                "A Linux AArch64 toolchain was not found. Install "
                "aarch64-unknown-linux-gnu or set CARDPUTERZERO_TOOLCHAIN_PATH "
                "and CARDPUTERZERO_TOOLCHAIN_PREFIX."
            )
        if toolchain_path:
            config_lines += f'CONFIG_TOOLCHAIN_PATH="{toolchain_path}"\n'
        config_lines += f'CONFIG_TOOLCHAIN_PREFIX="{toolchain_prefix}"\n'
        sysroot = str(sysroot_path)
        os.environ["CONFIG_TOOLCHAIN_SYSROOT"] = sysroot
        os.environ["CONFIG_TOOLCHAIN_FLAGS"] = f"--sysroot={sysroot}"
        config_lines += f'CONFIG_TOOLCHAIN_SYSROOT="{sysroot}"\n'
        config_lines += f'CONFIG_TOOLCHAIN_FLAGS="--sysroot={sysroot}"\n'
else:
    simulator_root = (str(project_path) + "/").replace("\\", "\\\\").replace('"', '\\"')
    config_lines = config_lines.replace(
        'CONFIG_V9_5_LV_FS_POSIX_PATH="/usr/share/APPLaunch/"',
        f'CONFIG_V9_5_LV_FS_POSIX_PATH="{simulator_root}"',
    )
    config_lines += "CONFIG_V9_5_LV_USE_SDL=y\n"

platform_config.write_text(config_lines)
os.environ["CONFIG_DEFAULT_FILE"] = str(platform_config.resolve())

sdk_candidates = []
sdk_override = os.environ.get("CARDPUTERZERO_SDK_PATH") or os.environ.get("SDK_PATH")
if sdk_override:
    sdk_candidates.append(Path(sdk_override))
sdk_candidates += [
    project_path.parent / "SDK",
    project_path.parent / "Launcher" / "SDK",
    project_path.parent / "launcher" / "SDK",
    project_path.parent.parent / "SDK",
    project_path.parent.parent / "Launcher" / "SDK",
]
sdk_path = next((path for path in sdk_candidates if (path / "tools/scons/project.py").exists()), None)
if sdk_path is None:
    tried = "\n  - ".join(str(path) for path in sdk_candidates)
    raise RuntimeError(
        "CardputerZero SDK not found. Set CARDPUTERZERO_SDK_PATH or install it at one of:\n  - " + tried
    )

os.environ["SDK_PATH"] = str(sdk_path)
os.environ["EXT_COMPONENTS_PATH"] = str(sdk_path.parent / "ext_components")

env = SConscript(
    str(sdk_path / "tools" / "scons" / "project.py"),
    variant_dir=os.getcwd(),
    duplicate=0,
)

if update:
    with open(env["PROJECT_TOOL_S"]) as project_tools:
        exec(project_tools.read())
    url = (
        "https://github.com/CardputerZero/M5CardputerZero-UserDemo/"
        "releases/download/{}/sdk_bsp.tar.gz"
    ).format(version)
    archive = check_wget_down(url, "static_lib_{}.tar.gz".format(version))
    if sysroot_path.exists():
        shutil.rmtree(sysroot_path)
    shutil.move(archive, sysroot_path)
