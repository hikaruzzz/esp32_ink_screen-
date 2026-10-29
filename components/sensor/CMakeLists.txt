set(srcs "sensor_status.c")

if(CONFIG_APP_SENSOR_MOCK)
    list(APPEND srcs "sensor_mock.c")
else()
    list(APPEND srcs "sensor_i2c.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       REQUIRES board esp_driver_i2c)
