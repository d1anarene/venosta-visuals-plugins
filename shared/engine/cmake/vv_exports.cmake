include_guard(GLOBAL)
# Export only the host entry points from a plug-in binary. Every Venosta Visuals plug-in links
# the same shared engine and OpenFX support code; if those symbols were exported, a host that
# loads several of our plug-ins could bind one plug-in to another's copy (macOS coalesces exported
# weak C++ symbols across all loaded images; Linux hosts may load with RTLD_GLOBAL), mixing code
# from different releases. Windows DLLs export only dllexport symbols already.
function(vv_export_only target)
  if(APPLE)
    foreach(sym ${ARGN})
      target_link_options(${target} PRIVATE "LINKER:-exported_symbol,_${sym}")
    endforeach()
  elseif(UNIX)
    set(map ${CMAKE_CURRENT_BINARY_DIR}/${target}-exports.map)
    string(REPLACE ";" "; " globals "${ARGN}")
    file(WRITE ${map} "{\n  global: ${globals};\n  local: *;\n};\n")
    target_link_options(${target} PRIVATE "LINKER:--version-script=${map}")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${map})
  endif()
endfunction()
