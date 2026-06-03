function(write_spirv_words output_var shader_path)
    file(READ "${shader_path}" shader_hex HEX)
    string(LENGTH "${shader_hex}" shader_hex_length)
    math(EXPR shader_byte_length "${shader_hex_length} / 2")
    math(EXPR trailing_bytes "${shader_byte_length} % 4")
    if(NOT trailing_bytes EQUAL 0)
        message(FATAL_ERROR "${shader_path} is not aligned to 32-bit SPIR-V words")
    endif()

    math(EXPR shader_word_count "${shader_byte_length} / 4")
    set(shader_words "")
    if(shader_word_count GREATER 0)
        math(EXPR last_word_index "${shader_word_count} - 1")
        foreach(word_index RANGE 0 ${last_word_index})
            math(EXPR hex_offset "${word_index} * 8")
            string(SUBSTRING "${shader_hex}" ${hex_offset} 2 byte0)
            math(EXPR hex_offset "${hex_offset} + 2")
            string(SUBSTRING "${shader_hex}" ${hex_offset} 2 byte1)
            math(EXPR hex_offset "${hex_offset} + 2")
            string(SUBSTRING "${shader_hex}" ${hex_offset} 2 byte2)
            math(EXPR hex_offset "${hex_offset} + 2")
            string(SUBSTRING "${shader_hex}" ${hex_offset} 2 byte3)
            string(APPEND shader_words "        0x${byte3}${byte2}${byte1}${byte0}u,\n")
        endforeach()
    endif()

    set(${output_var} "${shader_words}" PARENT_SCOPE)
endfunction()

write_spirv_words(vert_shader_words "${VERT_SHADER}")
write_spirv_words(frag_shader_words "${FRAG_SHADER}")

file(WRITE "${OUTPUT}" "#pragma once

#include <cstdint>

namespace wal::embedded {

inline constexpr uint32_t uiVertShader[] = {
${vert_shader_words}};

inline constexpr uint32_t uiFragShader[] = {
${frag_shader_words}};

} // namespace wal::embedded
")
