USB_VID = 0x239A
USB_PID = 0x80F4
USB_PRODUCT = "Pico W"
USB_MANUFACTURER = "Raspberry Pi"

CHIP_VARIANT = RP2040
CHIP_FAMILY = rp2

EXTERNAL_FLASH_DEVICES = "W25Q16JVxQ"

CIRCUITPY__EVE = 1

CIRCUITPY_CYW43 = 1
CIRCUITPY_SSL = 0
CIRCUITPY_HASHLIB = 0
CIRCUITPY_WEB_WORKFLOW = 0
CIRCUITPY_MDNS = 0
CIRCUITPY_SOCKETPOOL = 0
CIRCUITPY_WIFI = 0

CFLAGS += -DCYW43_PIN_WL_HOST_WAKE=24 -DCYW43_PIN_WL_REG_ON=23 -DCYW43_WL_GPIO_COUNT=3 -DCYW43_WL_GPIO_LED_PIN=0
