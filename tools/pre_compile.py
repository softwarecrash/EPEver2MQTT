Import("env")
import os

def patch_wm_params():
    lib_path = os.path.join(env["PROJECT_DIR"], ".pio", "libdeps", env["PIOENV"], "ESPAsyncWiFiManager", "src", "ESPAsyncWiFiManager.h")
    if os.path.isfile(lib_path):
        with open(lib_path, "r") as f:
            content = f.read()
        target = "#define WIFI_MANAGER_MAX_PARAMS 10"
        replacement = "#ifndef WIFI_MANAGER_MAX_PARAMS\n#define WIFI_MANAGER_MAX_PARAMS 10\n#endif"
        if target in content and replacement not in content:
            with open(lib_path, "w") as f:
                f.write(content.replace(target, replacement))
patch_wm_params()

env.Append(CPPDEFINES=[
    ("SWVERSION", env.StringifyMacro(env.GetProjectOption("custom_prog_version"))),
    ("HWBOARD", env.StringifyMacro(env["PIOENV"])),
])
if env.GetProjectOption("build_type") == "debug":
    env.Append(CPPDEFINES=[
        ("isDEBUG",  env.StringifyMacro(env.GetBuildType())),
    ])

env.Replace(PROGNAME="EPEver2MQTT_%s_%s" % (str(env["PIOENV"]), env.GetProjectOption("custom_prog_version")))
