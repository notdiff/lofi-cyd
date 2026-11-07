Import("env")


def project_transport(build_env, node):
    build_env.PrependUnique(CPPPATH=[env.subst("$PROJECT_INCLUDE_DIR")], delete_existing=True)
    return node


env.AddBuildMiddleware(project_transport, "*.cpp")
