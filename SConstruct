#!/usr/bin/env python
import os
import sys

env = SConscript("godot-cpp/SConstruct")

# For reference:
# - CCFLAGS are compilation flags shared between C and C++
# - CFLAGS are for C-specific compilation flags
# - CXXFLAGS are for C++-specific compilation flags
# - CPPFLAGS are for pre-processor flags
# - CPPDEFINES are for pre-processor defines
# - LINKFLAGS are for linking flags

# tweak this if you want to use different folders, or more folders, to store your source code in.
env.Append(CPPPATH=["src/"])
env['ENV']['PATH'] = os.environ['PATH'] + os.pathsep + "/opt/homebrew/bin"
sources = Glob("src/*.cpp")

# Add GStreamer include paths and libraries
pkg_config = os.popen("PKG_CONFIG_PATH=$PKG_CONFIG_PATH pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0")
gstreamer_flags = pkg_config.read().strip().split(" ")

env.MergeFlags(gstreamer_flags)

# Add GStreamer include paths for the Windows build
if env["platform"] == "windows":
    env.Append(CPPPATH=[os.path.join(os.environ['GITHUB_WORKSPACE'], 'gstreamer-sdk', 'gstreamer', '1.0', 'mingw_x86_64', 'include')])

if env["platform"] == "macos":
    library = env.SharedLibrary(
        "addons/libRtspServer.{}.{}.framework/libRtspServer.{}.{}".format(
            env["platform"], env["target"], env["platform"], env["target"]
        ),
        source=sources,
    )
else:
    library = env.SharedLibrary(
        "addons/libRtspServer{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
        source=sources,
    )

Default(library)
