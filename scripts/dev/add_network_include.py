import os

Import("env")

platform = env.PioPlatform()
network_include_dir = os.path.join(
    platform.get_package_dir("framework-arduinoespressif32"), "libraries", "Network", "src"
)
env.Append(CPPPATH=[network_include_dir])
