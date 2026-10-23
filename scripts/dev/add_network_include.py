import os

Import("env")

platform = env.PioPlatform()
framework_libraries_dir = os.path.join(
    platform.get_package_dir("framework-arduinoespressif32"), "libraries"
)
# Network: Header, die einige Framework-Libs ohne eigene Abhaengigkeit erwarten.
# WiFi:    HTTPUpdate (OTA) inkludiert <WiFi.h>, findet es aber nicht von selbst.
for library_name in ("Network", "WiFi"):
    env.Append(CPPPATH=[os.path.join(framework_libraries_dir, library_name, "src")])
