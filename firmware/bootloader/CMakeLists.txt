target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    commands.c
    jump_to_application.c
    main.c
)

# SystemInit is compiled in STM32_Drivers; keep this offset in sync with bootloader.ld.
target_compile_definitions(STM32_Drivers PRIVATE
    USER_VECT_TAB_ADDRESS
    VECT_TAB_OFFSET=0x00000000U
)
