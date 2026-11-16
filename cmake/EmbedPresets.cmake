# mpx_embed_presets(<output header> <preset name>...)
# Generates a header with the given presets/<name>.json files as string literals:
#   static const char* const kFactoryPresets[];  static const int kFactoryPresetCount;
function(mpx_embed_presets output)
    set(content "// Generated from presets/*.json - do not edit.\n#pragma once\n\nstatic const char* const kFactoryPresets[] = {\n")
    foreach(name ${ARGN})
        set(file ${PROJECT_SOURCE_DIR}/presets/${name}.json)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${file})
        file(READ ${file} json)
        string(APPEND content "    R\"MPXJSON(${json})MPXJSON\",\n")
    endforeach()
    list(LENGTH ARGN count)
    string(APPEND content "};\nstatic const int kFactoryPresetCount = ${count};\n")
    file(CONFIGURE OUTPUT ${output} CONTENT "${content}" @ONLY)
endfunction()

set(MPX_FACTORY_PRESETS
    note-visualisation
    strobe
    laser
    ambience
    rainbow-chase
    live-set
    stargate
)
