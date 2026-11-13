if(PROJECT_IS_TOP_LEVEL)
  set(
      CMAKE_INSTALL_INCLUDEDIR "include/LogConverge-${PROJECT_VERSION}"
      CACHE STRING ""
  )
  set_property(CACHE CMAKE_INSTALL_INCLUDEDIR PROPERTY TYPE PATH)
endif()

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

# 随原生 SDK 分发第三方声明和原始许可证。
install(FILES THIRD_PARTY_NOTICES.md licenses/spdlog.LICENSE licenses/fmt.LICENSE
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/LogConverge"
    COMPONENT LogConverge_Development)
install(FILES "${PROJECT_SOURCE_DIR}/../LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/LogConverge"
    COMPONENT LogConverge_Development)

# find_package(<package>) call for consumers to find this project
set(package LogConverge)

install(
    DIRECTORY
    include/
    "${PROJECT_BINARY_DIR}/export/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    COMPONENT LogConverge_Development
)

install(
    TARGETS LogConverge_LogConverge
    EXPORT LogConvergeTargets
    RUNTIME #
    COMPONENT LogConverge_Runtime
    LIBRARY #
    COMPONENT LogConverge_Runtime
    NAMELINK_COMPONENT LogConverge_Development
    ARCHIVE #
    COMPONENT LogConverge_Development
    INCLUDES #
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

write_basic_package_version_file(
    "${package}ConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion
)

# Allow package maintainers to freely override the path for the configs
set(
    LogConverge_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/${package}"
    CACHE STRING "CMake package config location relative to the install prefix"
)
set_property(CACHE LogConverge_INSTALL_CMAKEDIR PROPERTY TYPE PATH)
mark_as_advanced(LogConverge_INSTALL_CMAKEDIR)

install(
    FILES cmake/install-config.cmake
    DESTINATION "${LogConverge_INSTALL_CMAKEDIR}"
    RENAME "${package}Config.cmake"
    COMPONENT LogConverge_Development
)

install(
    FILES "${PROJECT_BINARY_DIR}/${package}ConfigVersion.cmake"
    DESTINATION "${LogConverge_INSTALL_CMAKEDIR}"
    COMPONENT LogConverge_Development
)

install(
    EXPORT LogConvergeTargets
    NAMESPACE LogConverge::
    DESTINATION "${LogConverge_INSTALL_CMAKEDIR}"
    COMPONENT LogConverge_Development
)

