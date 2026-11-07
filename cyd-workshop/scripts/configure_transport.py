"""Select the project implementation of WebSockets' custom TLS transport.

PlatformIO normally puts a library's own include directory first. WebSockets
2.7.3 ships placeholder transport headers; our complete ESP32 implementation
must precede those headers in every translation unit, without modifying vendors.
"""
Import("env")

def project_transport(build_env, node):
    build_env.PrependUnique(CPPPATH=[env.subst("$PROJECT_INCLUDE_DIR")], delete_existing=True)
    return node

env.AddBuildMiddleware(project_transport, "*.cpp")
