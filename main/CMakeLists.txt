idf_component_register(SRCS "gdo-blaq-homekit.cpp" "homekit.cpp" "wifi.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES gdolib esp_hap_core esp_hap_apple_profiles esp_wifi nvs_flash wifi_provisioning nvs_wifi_connect
                )

# A panic skips shutdown handlers. Wrap the panic entry point so it can release
# the GDO TX pin first (see __wrap_esp_panic_handler).
target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_panic_handler")
