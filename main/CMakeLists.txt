idf_component_register(
    SRCS "main.c"
         "ui_pixel.c"
         "ui_pixel_math.c"
         "pdkpass_data.c"
         "pdkpass_model.c"
         "pdkpass_network.c"
         "pdkpass_http.c"
         "pdkpass_json_stream.c"
         "pdkpass_results.c"
         "pdkpass_results_core.c"
         "pdkpass_schedule.c"
         "pdkpass_screenshot.c"
         "pdkpass_season.c"
         "pdkpass_season_core.c"
         "pdkpass_theme.c"
         "pdkpass_tracks.c"
         "pdkpass_ui.c"
         "pdkpass_wifi_form.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_http_client esp_http_server esp_hw_support
             esp_netif esp_wifi esp_driver_usb_serial_jtag json lwip mbedtls
             nvs_flash esp_timer
)
