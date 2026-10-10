target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    commands.c
    main.c
)

# SystemInit is compiled in STM32_Drivers; keep this offset in sync with application.ld.
target_compile_definitions(STM32_Drivers PRIVATE
    USER_VECT_TAB_ADDRESS
    VECT_TAB_OFFSET=0x00002000U
)
