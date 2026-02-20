FILE(GLOB SRC *.cpp)

ADD_EXECUTABLE(nl_sample_clip_plane WIN32 ${SRC})

TARGET_LINK_LIBRARIES(nl_sample_clip_plane nelmisc nel3d)
NL_DEFAULT_PROPS(nl_sample_clip_plane "NeL, Samples, 3D: Clip Plane")
NL_ADD_RUNTIME_FLAGS(nl_sample_clip_plane)

IF(WITH_STATIC_DRIVERS)
  NL_ADD_STATIC_VID_DRIVERS(nl_sample_clip_plane)
ENDIF()

IF(EMSCRIPTEN)
  SET_TARGET_PROPERTIES(nl_sample_clip_plane PROPERTIES
    SUFFIX ".html"
    LINK_FLAGS "-s USE_WEBGL2=1 -s FULL_ES3=1 -s WASM=1 -s ALLOW_MEMORY_GROWTH=1")
ENDIF()

INSTALL(TARGETS nl_sample_clip_plane RUNTIME DESTINATION ${NL_BIN_PREFIX} COMPONENT samples3d)
