import os


def _sdk_path(env):
    path = env.get("wwise_sdk_path", "") or os.environ.get("WWISESDK", "")
    if not path:
        return ""
    return os.path.normpath(os.path.expandvars(os.path.expanduser(path)))


def _has_sdk(env):
    sdk_path = _sdk_path(env)
    return bool(sdk_path) and os.path.isdir(os.path.join(sdk_path, "include", "AK"))


def can_build(env, platform):
    if platform != "windows":
        print("The 'wwise' module is currently wired for the Windows Wwise SDK layout only.")
        return False

    if not _has_sdk(env):
        print("The 'wwise' module requires wwise_sdk_path=<path> or the WWISESDK environment variable.")
        return False

    return True


def get_opts(platform):
    from SCons.Variables import BoolVariable

    return [
        ("wwise_sdk_path", "Path to the Wwise SDK root. Defaults to the WWISESDK environment variable.", os.environ.get("WWISESDK", "")),
        ("wwise_config", "Wwise SDK library configuration to link against, such as Debug, Profile, Release, or Profile(StaticCRT).", "Profile"),
        BoolVariable("wwise_dynamic", "Link against Wwise dynamic/import libraries when available.", True),
    ]


def configure(env):
    sdk_path = _sdk_path(env)
    config = env.get("wwise_config", "Profile")
    arch = env.get("arch", "x86_64")

    if env["platform"] != "windows":
        return

    vc_arch = "x64" if arch in ["x86_64", "amd64"] else "Win32"
    lib_dir_candidates = [
        os.path.join(sdk_path, f"{vc_arch}_vc170", config, "lib"),
        os.path.join(sdk_path, f"{vc_arch}_vc160", config, "lib"),
        os.path.join(sdk_path, f"{vc_arch}_vc150", config, "lib"),
        os.path.join(sdk_path, "Windows_vc170", vc_arch, config, "lib"),
        os.path.join(sdk_path, "Windows_vc160", vc_arch, config, "lib"),
        os.path.join(sdk_path, "Windows_vc150", vc_arch, config, "lib"),
    ]
    lib_dir = next((path for path in lib_dir_candidates if os.path.isdir(path)), "")

    env.Prepend(CPPPATH=[os.path.join(sdk_path, "include")])
    env.Append(CPPDEFINES=["WWISE_ENABLED"])

    if lib_dir:
        env.Append(LIBPATH=[lib_dir])
    else:
        print(f"Could not find a Wwise library directory for '{config}'. The compiler may still find it through LIBPATH.")

    env.Append(LIBS=["AkSoundEngine", "AkMemoryMgr", "AkStreamMgr"])

    if env.get("wwise_dynamic", True):
        env.Append(CPPDEFINES=["AK_DYNAMIC_LINK"])

    env.add_module_version_string("wwise")


def is_enabled():
    return False


def get_doc_classes():
    return [
        "Wwise",
    ]


def get_doc_path():
    return "doc_classes"
