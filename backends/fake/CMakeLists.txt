file(GLOB_RECURSE SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
LIST(APPEND BACKEND_SRCS ${SOURCE_FILES})

# Pass to parent
set(BACKEND_SRCS ${BACKEND_SRCS} PARENT_SCOPE)
