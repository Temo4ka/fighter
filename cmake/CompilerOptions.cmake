include(CheckLinkerFlag)

# Новый линкер Apple предупреждает о повторах статических библиотек, которые CMake
# намеренно добавляет для правильного порядка линковки. Предупреждение бесполезно.
if(APPLE)
    check_linker_flag(CXX "LINKER:-no_warn_duplicate_libraries" FIGHTER_HAS_NO_WARN_DUPLICATE_LIBS)
endif()

# Общие настройки для всех целей проекта (но не для сторонних библиотек).
function(fighter_target_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
            # Без слияния операций в FMA: одинаковые результаты float на разных
            # платформах — задел под детерминизм (ИИ, реплеи, сеть).
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
