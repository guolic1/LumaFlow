target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    commands.c
    fluid_effect.c
    fluid_sim.c
    imu.c
    imu_stm32.c
    qma6100p.c
    led_matrix.c
    led_matrix_scan.c
    main.c
    motion_sleep.c
)

# The fluid solver is compute bound; keep the rest of the image size optimized.
set_source_files_properties(fluid_sim.c PROPERTIES
    COMPILE_OPTIONS "$<$<CONFIG:Release>:-O2>"
)

# SystemInit is compiled in STM32_Drivers; keep this offset in sync with application.ld.
target_compile_definitions(STM32_Drivers PRIVATE
    USER_VECT_TAB_ADDRESS
    VECT_TAB_OFFSET=0x00002000U
)
