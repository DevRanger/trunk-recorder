find_program(CARGO_EXECUTABLE cargo)
if(NOT CARGO_EXECUTABLE)
    message(FATAL_ERROR "ENABLE_BLIP25 requires Cargo and Rust >= 1.85")
endif()
set(BLIP25_SOURCE_DIR "" CACHE PATH "Blip25 1.0.0 checkout with decode_info_into support")
if(NOT EXISTS "${BLIP25_SOURCE_DIR}/Cargo.toml")
    message(FATAL_ERROR "Set BLIP25_SOURCE_DIR to the local Blip25 checkout")
endif()
get_filename_component(BLIP25_SOURCE_DIR "${BLIP25_SOURCE_DIR}" ABSOLUTE)
configure_file(Cargo.toml.in Cargo.toml @ONLY)
configure_file(Cargo.lock Cargo.lock COPYONLY)

# Cargo checks the Rust inputs incrementally. Keeping this target active also
# catches edits in the separately maintained Blip25 checkout.
set(BLIP25_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/target/release/libtr_blip25.a")
add_custom_target(tr_blip25_build
    COMMAND ${CMAKE_COMMAND} -E env "CARGO_TARGET_DIR=${CMAKE_CURRENT_BINARY_DIR}/target"
        "RUSTFLAGS=-C relocation-model=pic"
        "${CARGO_EXECUTABLE}" build --release --locked --offline
        --manifest-path "${CMAKE_CURRENT_BINARY_DIR}/Cargo.toml"
    BYPRODUCTS "${BLIP25_ARCHIVE}"
    COMMENT "Building optional Blip25 decoder adapter"
    VERBATIM)
add_library(tr_blip25 STATIC IMPORTED GLOBAL)
set_target_properties(tr_blip25 PROPERTIES IMPORTED_LOCATION "${BLIP25_ARCHIVE}")
add_dependencies(tr_blip25 tr_blip25_build)
