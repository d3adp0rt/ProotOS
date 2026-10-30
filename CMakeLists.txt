cmake_minimum_required(VERSION 3.25)
project(ProotOS LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_program(CLANG_EXECUTABLE clang REQUIRED)
find_program(LLD_LINK_EXECUTABLE lld-link REQUIRED)
find_program(LD_LLD_EXECUTABLE ld.lld REQUIRED)
find_program(PYTHON3_EXECUTABLE python3 REQUIRED)

set(BOOT_OBJ_DIR ${CMAKE_BINARY_DIR}/obj/boot)
set(KERNEL_OBJ_DIR ${CMAKE_BINARY_DIR}/obj/kernel)
set(IMAGE_DIR ${CMAKE_BINARY_DIR}/images)
file(MAKE_DIRECTORY ${BOOT_OBJ_DIR} ${KERNEL_OBJ_DIR} ${IMAGE_DIR})

set(LOADER_WARN -Wall -Wextra -Werror -Wno-unused-parameter)
set(KERNEL_WARN -Wall -Wextra -Werror -Wno-unused-parameter -Wno-address-of-packed-member)

set(LOADER_SOURCES
    boot/loader/main.c
    boot/loader/util.c
    boot/loader/fs.c
    boot/loader/elf.c
    boot/loader/gop.c
    boot/loader/paging.c
    boot/loader/jump.c
)

file(GLOB_RECURSE LOADER_HEADERS CONFIGURE_DEPENDS
     ${CMAKE_SOURCE_DIR}/boot/*.h)

set(LOADER_OBJECTS)
foreach(src IN LISTS LOADER_SOURCES)
    get_filename_component(name ${src} NAME_WE)
    set(obj ${BOOT_OBJ_DIR}/${name}.o)
    add_custom_command(
        OUTPUT ${obj}
        COMMAND ${CLANG_EXECUTABLE}
            -target x86_64-unknown-windows
            -std=c23 -ffreestanding -fshort-wchar -fno-stack-protector
            -fno-builtin -mno-red-zone -mno-stack-arg-probe -O2 -g
            ${LOADER_WARN}
            -I${CMAKE_SOURCE_DIR}/boot
            -I${CMAKE_SOURCE_DIR}/boot/loader
            -c ${CMAKE_SOURCE_DIR}/${src} -o ${obj}
        DEPENDS ${CMAKE_SOURCE_DIR}/${src} ${LOADER_HEADERS}
        COMMENT "CC [loader] ${src}"
        VERBATIM)
    list(APPEND LOADER_OBJECTS ${obj})
endforeach()

add_custom_command(
    OUTPUT ${IMAGE_DIR}/BOOTX64.EFI
    COMMAND ${LLD_LINK_EXECUTABLE}
        /machine:x64
        /subsystem:efi_application
        /entry:efi_main
        /nodefaultlib
        /out:${IMAGE_DIR}/BOOTX64.EFI
        ${LOADER_OBJECTS}
    DEPENDS ${LOADER_OBJECTS}
    COMMENT "LD [loader] BOOTX64.EFI"
    VERBATIM)

set(KERNEL_C_SOURCES
    kernel/main.c
    kernel/console.c
    kernel/lib/string.c
    kernel/lib/serial.c
    kernel/lib/kprintf.c
    kernel/arch/x86_64/idt.c
    kernel/arch/x86_64/exceptions.c
    kernel/mm/pmm.c
    kernel/mm/vmm.c
    kernel/mm/kmalloc.c
    kernel/mm/ktest.c
)

set(KERNEL_ASM_SOURCES
    kernel/arch/x86_64/entry.S
    kernel/arch/x86_64/isr.S
)

file(GLOB_RECURSE KERNEL_HEADERS CONFIGURE_DEPENDS
     ${CMAKE_SOURCE_DIR}/kernel/*.h
     ${CMAKE_SOURCE_DIR}/boot/*.h
     ${CMAKE_SOURCE_DIR}/third_party/fonts/*.h)

set(KERNEL_OBJECTS)
foreach(src IN LISTS KERNEL_C_SOURCES)
    get_filename_component(name ${src} NAME_WE)
    set(obj ${KERNEL_OBJ_DIR}/${name}.o)
    add_custom_command(
        OUTPUT ${obj}
        COMMAND ${CLANG_EXECUTABLE}
            -target x86_64-unknown-none-elf
            -std=c23 -ffreestanding -fno-stack-protector -fno-builtin
            -mno-red-zone -mcmodel=kernel -mno-sse -mno-mmx -mno-80387
            -fno-asynchronous-unwind-tables -fno-unwind-tables
            -O2 -g ${KERNEL_WARN}
            -I${CMAKE_SOURCE_DIR}/boot
            -I${CMAKE_SOURCE_DIR}/kernel/include
            -c ${CMAKE_SOURCE_DIR}/${src} -o ${obj}
        DEPENDS ${CMAKE_SOURCE_DIR}/${src} ${KERNEL_HEADERS}
        COMMENT "CC [kernel] ${src}"
        VERBATIM)
    list(APPEND KERNEL_OBJECTS ${obj})
endforeach()

foreach(src IN LISTS KERNEL_ASM_SOURCES)
    get_filename_component(name ${src} NAME_WE)
    set(obj ${KERNEL_OBJ_DIR}/${name}.o)
    add_custom_command(
        OUTPUT ${obj}
        COMMAND ${CLANG_EXECUTABLE}
            -target x86_64-unknown-none-elf
            -ffreestanding -fno-stack-protector -mno-red-zone
            -mcmodel=kernel -mno-sse -mno-mmx -mno-80387
            -O2 -g
            -c ${CMAKE_SOURCE_DIR}/${src} -o ${obj}
        DEPENDS ${CMAKE_SOURCE_DIR}/${src} ${KERNEL_HEADERS}
        COMMENT "AS [kernel] ${src}"
        VERBATIM)
    list(APPEND KERNEL_OBJECTS ${obj})
endforeach()

add_custom_command(
    OUTPUT ${IMAGE_DIR}/kernel.elf
    COMMAND ${LD_LLD_EXECUTABLE}
        -T ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/linker.ld
        -nostdlib
        -z max-page-size=4096
        -Map=${CMAKE_BINARY_DIR}/kernel.map
        -o ${IMAGE_DIR}/kernel.elf
        ${KERNEL_OBJECTS}
    DEPENDS ${KERNEL_OBJECTS} ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/linker.ld
    COMMENT "LD [kernel] kernel.elf"
    VERBATIM)

file(GLOB_RECURSE INITRAMFS_FILES CONFIGURE_DEPENDS
     ${CMAKE_SOURCE_DIR}/initramfs/*)
add_custom_command(
    OUTPUT ${IMAGE_DIR}/initramfs.cpio
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/mkinitramfs.py
        ${CMAKE_SOURCE_DIR}/initramfs ${IMAGE_DIR}/initramfs.cpio
    DEPENDS ${INITRAMFS_FILES} ${CMAKE_SOURCE_DIR}/tools/mkinitramfs.py
    COMMENT "GEN initramfs.cpio"
    VERBATIM)

add_custom_command(
    OUTPUT ${IMAGE_DIR}/prootos.iso
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/mkimage.py
        ${IMAGE_DIR}/BOOTX64.EFI
        ${IMAGE_DIR}/kernel.elf
        ${IMAGE_DIR}/initramfs.cpio
        ${CMAKE_SOURCE_DIR}/boot/proot.cfg
        ${IMAGE_DIR}/prootos.iso
        ${CMAKE_BINARY_DIR}/iso-work
    DEPENDS ${IMAGE_DIR}/BOOTX64.EFI ${IMAGE_DIR}/kernel.elf
            ${IMAGE_DIR}/initramfs.cpio ${CMAKE_SOURCE_DIR}/boot/proot.cfg
    COMMENT "GEN prootos.iso"
    VERBATIM)

add_custom_target(prootos ALL DEPENDS ${IMAGE_DIR}/prootos.iso)

add_custom_target(run
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/run-qemu.py
        --iso ${IMAGE_DIR}/prootos.iso --gui
    DEPENDS prootos
    USES_TERMINAL)

add_custom_target(run-headless
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/run-qemu.py
        --iso ${IMAGE_DIR}/prootos.iso --headless
    DEPENDS prootos
    USES_TERMINAL)

add_custom_target(test-boot
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/test_boot.py
    DEPENDS prootos
    USES_TERMINAL)

# Host-side unit tests -----------------------------------------------------
enable_testing()
add_executable(prootos_unit_tests tests/unit/test_main.c)
target_compile_options(prootos_unit_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME unit COMMAND prootos_unit_tests)

add_custom_target(test-all
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target test-boot
    DEPENDS prootos_unit_tests prootos
    USES_TERMINAL)
