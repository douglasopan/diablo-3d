# Optional offline preparation executable; it does not change the game target.
option(D3D_BUILD_SCENE_PREPARER "Build the original-data Tristram preparation tool" OFF)
if(D3D_BUILD_SCENE_PREPARER)
  add_executable(prepare_tristram
    "${PROJECT_SOURCE_DIR}/tools/distribution/scene_bootstrap.cpp"
    "${PROJECT_SOURCE_DIR}/tools/distribution/scene_bridge.cpp")
  set_target_properties(prepare_tristram PROPERTIES OUTPUT_NAME Prepare-Tristram)
  target_link_dependencies(prepare_tristram PRIVATE libdevilutionx)
  resolve_target_link_dependencies()
  # Replace only the architecture storage TU in this preparation target.
  get_target_property(_preparation_sources prepare_tristram SOURCES)
  list(TRANSFORM _preparation_sources REPLACE
    "^\\$<TARGET_OBJECTS:libdevilutionx>$"
    "$<FILTER:$<TARGET_OBJECTS:libdevilutionx>,EXCLUDE,town_scene[.]cpp[.](obj|o)$>")
  set_property(TARGET prepare_tristram PROPERTY SOURCES "${_preparation_sources}")
  get_target_property(_preparation_libraries prepare_tristram LINK_LIBRARIES)
  list(REMOVE_ITEM _preparation_libraries libdevilutionx)
  set_property(TARGET prepare_tristram PROPERTY LINK_LIBRARIES "${_preparation_libraries}")
  target_link_libraries(prepare_tristram PRIVATE
    $<TARGET_GENEX_EVAL:prepare_tristram,$<TARGET_PROPERTY:libdevilutionx,INTERFACE_LINK_LIBRARIES>>)
  target_include_directories(prepare_tristram PRIVATE
    $<TARGET_GENEX_EVAL:prepare_tristram,$<TARGET_PROPERTY:libdevilutionx,INTERFACE_INCLUDE_DIRECTORIES>>)
  target_compile_definitions(prepare_tristram PRIVATE SDL_MAIN_HANDLED
    $<TARGET_GENEX_EVAL:prepare_tristram,$<TARGET_PROPERTY:libdevilutionx,INTERFACE_COMPILE_DEFINITIONS>>)
  if(MSVC)
    target_compile_options(prepare_tristram PRIVATE /EHsc /utf-8)
  endif()
endif()
