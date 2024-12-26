find_package(fmt REQUIRED CONFIG)
find_package(folly REQUIRED CONFIG)

target_link_libraries(${PROJECT_NAME} PRIVATE Folly::folly)