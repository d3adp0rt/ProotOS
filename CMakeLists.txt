cmake_minimum_required(VERSION 3.25)
project(ProotOS LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_program(CLANG_EXECUTABLE clang REQUIRED)
find_program(LLD_LINK_EXECUTABLE lld-link REQUIRED)
find_program(LD_LLD_EXECUTABLE ld.lld REQUIRED)
find_program(PYTHON3_EXECUTABLE python3 REQUIRED)
find_program(OBJCOPY_EXECUTABLE objcopy REQUIRED)

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
    kernel/acpi.c
    kernel/smp.c
    kernel/sched/sched.c
    kernel/sched/selftest.c
    kernel/initramfs.c
    kernel/proc/process.c
    kernel/proc/syscall.c
    kernel/proc/pipe.c
    kernel/fs/vfs.c
    kernel/fs/ramdisk.c
    kernel/fs/ext2.c
    kernel/drivers/pci.c
    kernel/drivers/ahci.c
    kernel/drivers/ata.c
    kernel/drivers/virtio_blk.c
    kernel/drivers/keyboard.c
    kernel/drivers/rtl8139.c
    kernel/drivers/misc.c
    kernel/lib/string.c
    kernel/lib/serial.c
    kernel/lib/kprintf.c
    kernel/arch/x86_64/idt.c
    kernel/arch/x86_64/exceptions.c
    kernel/arch/x86_64/apic.c
    kernel/arch/x86_64/timer.c
    kernel/arch/x86_64/rtc.c
    kernel/arch/x86_64/gdt.c
    kernel/mm/pmm.c
    kernel/mm/vmm.c
    kernel/mm/kmalloc.c
    kernel/mm/ktest.c
)

set(KERNEL_ASM_SOURCES
    kernel/arch/x86_64/entry.S
    kernel/arch/x86_64/isr.S
    kernel/arch/x86_64/irq.S
    kernel/arch/x86_64/switch.S
    kernel/arch/x86_64/ap_blob.S
)

set(TRAMP_DIR ${CMAKE_BINARY_DIR}/obj/tramp)
file(MAKE_DIRECTORY ${TRAMP_DIR})
add_custom_command(
    OUTPUT ${TRAMP_DIR}/ap_trampoline.bin
    COMMAND ${CLANG_EXECUTABLE}
        -target x86_64-unknown-none-elf
        -c ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/ap_trampoline.S
        -o ${TRAMP_DIR}/ap_trampoline.o
    COMMAND ${LD_LLD_EXECUTABLE}
        -T ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/ap_trampoline.ld
        -o ${TRAMP_DIR}/ap_trampoline.elf
        ${TRAMP_DIR}/ap_trampoline.o
    COMMAND ${OBJCOPY_EXECUTABLE}
        -O binary ${TRAMP_DIR}/ap_trampoline.elf ${TRAMP_DIR}/ap_trampoline.bin
    DEPENDS ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/ap_trampoline.S
            ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/ap_trampoline.ld
    COMMENT "GEN ap_trampoline.bin"
    VERBATIM)

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
            -I${TRAMP_DIR}
            -c ${CMAKE_SOURCE_DIR}/${src} -o ${obj}
        DEPENDS ${CMAKE_SOURCE_DIR}/${src} ${KERNEL_HEADERS} ${TRAMP_DIR}/ap_trampoline.bin
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

set(USER_DIR ${CMAKE_BINARY_DIR}/user)
file(MAKE_DIRECTORY ${USER_DIR})
add_custom_command(
    OUTPUT ${USER_DIR}/init.elf
    COMMAND ${CLANG_EXECUTABLE}
        -target x86_64-unknown-none-elf -ffreestanding -fno-pie
        -c ${CMAKE_SOURCE_DIR}/user/init.S -o ${USER_DIR}/init.o
    COMMAND ${LD_LLD_EXECUTABLE}
        -Ttext=0x400000 -e _start -o ${USER_DIR}/init.elf ${USER_DIR}/init.o
    DEPENDS ${CMAKE_SOURCE_DIR}/user/init.S
    COMMENT "LD [user] init.elf"
    VERBATIM)

add_custom_command(
    OUTPUT ${USER_DIR}/hello.elf
    COMMAND ${CLANG_EXECUTABLE}
        -target x86_64-unknown-none-elf -ffreestanding -fno-pie
        -c ${CMAKE_SOURCE_DIR}/user/hello.S -o ${USER_DIR}/hello.o
    COMMAND ${LD_LLD_EXECUTABLE}
        -Ttext=0x400000 -e _start -o ${USER_DIR}/hello.elf ${USER_DIR}/hello.o
    DEPENDS ${CMAKE_SOURCE_DIR}/user/hello.S
    COMMENT "LD [user] hello.elf"
    VERBATIM)


set(LIBC_DIR ${CMAKE_BINARY_DIR}/libc)
file(MAKE_DIRECTORY ${LIBC_DIR})
add_custom_command(
    OUTPUT ${LIBC_DIR}/crt0.o
    COMMAND ${CLANG_EXECUTABLE} -target x86_64-unknown-none-elf -ffreestanding
        -fno-pie -c ${CMAKE_SOURCE_DIR}/libc/crt0.S -o ${LIBC_DIR}/crt0.o
    DEPENDS ${CMAKE_SOURCE_DIR}/libc/crt0.S
    COMMENT "AS [libc] crt0.o"
    VERBATIM)
add_custom_command(
    OUTPUT ${LIBC_DIR}/libc.o
    COMMAND ${CLANG_EXECUTABLE} -target x86_64-unknown-none-elf -ffreestanding
        -fno-pie -fno-stack-protector -O2 -Wall -Wextra
        -I${CMAKE_SOURCE_DIR}/libc/include
        -c ${CMAKE_SOURCE_DIR}/libc/libc.c -o ${LIBC_DIR}/libc.o
    DEPENDS ${CMAKE_SOURCE_DIR}/libc/libc.c ${CMAKE_SOURCE_DIR}/libc/include/libc.h
    COMMENT "CC [libc] libc.o"
    VERBATIM)

foreach(prog sh echo cat)
    add_custom_command(
        OUTPUT ${USER_DIR}/${prog}_prog.o
        COMMAND ${CLANG_EXECUTABLE} -target x86_64-unknown-none-elf -ffreestanding
            -fno-pie -fno-stack-protector -O2 -Wall -Wextra
            -I${CMAKE_SOURCE_DIR}/libc/include
            -c ${CMAKE_SOURCE_DIR}/user/${prog}.c -o ${USER_DIR}/${prog}_prog.o
        DEPENDS ${CMAKE_SOURCE_DIR}/user/${prog}.c ${CMAKE_SOURCE_DIR}/libc/include/libc.h
        COMMENT "CC [user] ${prog}.c"
        VERBATIM)
    add_custom_command(
        OUTPUT ${USER_DIR}/bin_${prog}
        COMMAND ${LD_LLD_EXECUTABLE} -Ttext=0x400000 -e _start
            -o ${USER_DIR}/bin_${prog}
            ${LIBC_DIR}/crt0.o ${LIBC_DIR}/libc.o ${USER_DIR}/${prog}_prog.o
        DEPENDS ${LIBC_DIR}/crt0.o ${LIBC_DIR}/libc.o ${USER_DIR}/${prog}_prog.o
        COMMENT "LD [user] /bin/${prog}"
        VERBATIM)
endforeach()

set(INITRAMFS_STAGE ${CMAKE_BINARY_DIR}/initramfs-root)
set(EXT2_IMG ${CMAKE_BINARY_DIR}/ext2.img)
add_custom_command(
    OUTPUT ${EXT2_IMG}
    COMMAND dd if=/dev/zero of=${EXT2_IMG} bs=1M count=8 status=none
    COMMAND mke2fs -q -F -t ext2 -b 1024 -I 128
        -O ^extent,^64bit,^metadata_csum,^flex_bg,^dir_index
        -L PROOTFS ${EXT2_IMG}
    COMMENT "GEN ext2.img"
    VERBATIM)

add_custom_command(
    OUTPUT ${INITRAMFS_STAGE}/init
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${INITRAMFS_STAGE}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/initramfs ${INITRAMFS_STAGE}
    COMMAND ${CMAKE_COMMAND} -E copy ${USER_DIR}/init.elf ${INITRAMFS_STAGE}/init
    COMMAND ${CMAKE_COMMAND} -E copy ${USER_DIR}/hello.elf ${INITRAMFS_STAGE}/hello
    COMMAND ${CMAKE_COMMAND} -E make_directory ${INITRAMFS_STAGE}/bin
    COMMAND ${CMAKE_COMMAND} -E copy ${USER_DIR}/bin_sh ${INITRAMFS_STAGE}/bin/sh
    COMMAND ${CMAKE_COMMAND} -E copy ${USER_DIR}/bin_echo ${INITRAMFS_STAGE}/bin/echo
    COMMAND ${CMAKE_COMMAND} -E copy ${USER_DIR}/bin_cat ${INITRAMFS_STAGE}/bin/cat
    COMMAND ${CMAKE_COMMAND} -E copy ${EXT2_IMG} ${INITRAMFS_STAGE}/ext2.img
    DEPENDS ${USER_DIR}/init.elf ${USER_DIR}/hello.elf ${USER_DIR}/bin_sh
            ${USER_DIR}/bin_echo ${USER_DIR}/bin_cat ${EXT2_IMG} ${INITRAMFS_FILES}
    COMMENT "STAGE initramfs"
    VERBATIM)

add_custom_command(
    OUTPUT ${IMAGE_DIR}/initramfs.cpio
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/mkinitramfs.py
        ${INITRAMFS_STAGE} ${IMAGE_DIR}/initramfs.cpio
    DEPENDS ${INITRAMFS_STAGE}/init ${INITRAMFS_FILES} ${CMAKE_SOURCE_DIR}/tools/mkinitramfs.py
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

add_custom_command(
    OUTPUT ${IMAGE_DIR}/data.img
    COMMAND dd if=/dev/zero of=${IMAGE_DIR}/data.img bs=1M count=8 status=none
    COMMAND mke2fs -q -F -t ext2 -b 1024 -I 128
        -O ^extent,^64bit,^metadata_csum,^flex_bg,^dir_index
        -L PROOTDATA ${IMAGE_DIR}/data.img
    COMMENT "GEN data.img"
    VERBATIM)

add_custom_target(prootos ALL DEPENDS ${IMAGE_DIR}/prootos.iso ${IMAGE_DIR}/data.img)

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
