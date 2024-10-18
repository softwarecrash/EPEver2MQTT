Import("env")

env.Append(CPPDEFINES=[
    ("SWVERSION", env.StringifyMacro(env.GetProjectOption("custom_prog_version"))),
    ("HWBOARD", env.StringifyMacro(env["PIOENV"])),
])
if env.GetProjectOption("build_type") == "debug":
    env.Append(CPPDEFINES=[
        ("isDEBUG",  env.StringifyMacro(env.GetBuildType())),
    ])

env.Replace(PROGNAME="EPEver2MQTT_%s_%s" % (str(env["PIOENV"]), env.GetProjectOption("custom_prog_version")))
