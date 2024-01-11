#!/usr/bin/env python
import os
import sys
from subprocess import Popen, PIPE
from SCons.Script import Default, Glob

def get_pkg_config_flags(*packages):
    command = ["pkg-config", "--cflags", "--libs"] + list(packages)
    process = Popen(command, stdout=PIPE, stderr=PIPE)
    output, error = process.communicate()
    if process.returncode != 0:
        print(f"Error: Unable to find packages: {', '.join(packages)}")
        print(error.decode().strip())
        sys.exit(1)
    return output.decode().strip().split()

# Load the SConstruct script from godot-cpp
env = SConscript("godot-cpp/SConstruct")

# Append additional paths to the include directories
env.Append(CPPPATH=["src/"])
env['ENV']['PATH'] = os.environ['PATH'] + os.pathsep + "/opt/homebrew/bin"
sources = Glob("src/*.cpp")

# Retrieve GStreamer flags
gstreamer_packages = ["gstreamer-1.0", "gstreamer-app-1.0", "gstreamer-rtsp-server-1.0"]
gstreamer_flags = get_pkg_config_flags(*gstreamer_packages)
env.MergeFlags(gstreamer_flags)

# Windows-specific include paths
if env["platform"] == "windows":
    env.Append(CPPPATH=[os.path.join(os.environ['GITHUB_WORKSPACE'], 'gstreamer-sdk', 'gstreamer', '1.0', 'mingw_x86_64', 'include')])

# Define the shared library target
if env["platform"] == "macos":
    library = env.SharedLibrary(
        "addons/streamer/libudph264streamer.{}.{}.framework/libudph264streamer.{}.{}".format(
            env["platform"], env["target"], env["platform"], env["target"]
        ),
        source=sources,
    )
else:
    library = env.SharedLibrary(
        "addons/streamer/libudph264streamer{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
        source=sources,
    )

# Set the default target
Default(library)
