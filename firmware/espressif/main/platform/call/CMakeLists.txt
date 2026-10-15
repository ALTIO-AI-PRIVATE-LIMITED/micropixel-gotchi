# LiveKit voice calls. The dependency is fetched only for the Gotchi profile
# (see idf_component.yml), so it is linked from main/CMakeLists.txt with
# idf_component_optional_requires after registration.
list(APPEND micropixel_sources "platform/call/livekit_call.cpp")
