idf_component_register(
    SRCS "hello.c"
    INCLUDE_DIRS ""
    PRIV_REQUIRES kyber esp_timer driver spi_flash
)
