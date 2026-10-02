include(CheckLinkerFlag)

# Apple's new linker warns about repeated static libraries, which CMake adds
# on purpose to get the link order right. The warning is useless.
if(APPLE)
    check_linker_flag(CXX "LINKER:-no_warn_duplicate_libraries" FIGHTER_HAS_NO_WARN_DUPLICATE_LIBS)
endif()

# Common settings for every project target (but not for third-party libraries).
function(fighter_target_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
            # No FMA contraction: identical float results on every platform,
            # groundwork for determinism (AI, replays, networking).
            -ffp-contract=off
        )
    endif()

    if(FIGHTER_HAS_NO_WARN_DUPLICATE_LIBS)
        target_link_options(${target} PRIVATE "LINKER:-no_warn_duplicate_libraries")
    endif()

    if(FIGHTER_SANITIZE AND NOT MSVC)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()
