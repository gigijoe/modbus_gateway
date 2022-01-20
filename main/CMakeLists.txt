set(PROJECT_NAME "modbus_gateway")

idf_component_register(SRCS ./main.cpp ./esp32_malloc.c ./i2cdev.c ./led.c ./sdmmc.c 
	./network.c ./telnetd.c ./icmp_echo.c 
	./modbus_tcp_slave.c ./modbus_tcp2serial.c ./modbus_data.c 
	./ds1307.c ./ds3231.c ./ds18b20.c ./pcf8574.cpp ./lcd204-i2c.cpp
	./http_server.c
	./ota_https.c ./ota_ble.c
	./blufi_security.c ./blufi_init.c
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES ${project_dir}/server_certs/ca_cert.pem)
