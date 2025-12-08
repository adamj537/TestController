#include "hal_interface.h"
#include "driver/spi_master.h"
#include <string.h>

// Store the SPI device handle globally
static spi_device_handle_t g_spi_handle;

void hal_spi_master_init(uint8_t spi_num, int mosi, int miso, int sclk, int cs) {
    spi_bus_config_t buscfg = {
        .mosi_io_num = mosi,
        .miso_io_num = miso,
        .sclk_io_num = sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0,
    };
    // Initialize the SPI bus
    spi_bus_initialize(spi_num, &buscfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000, // 10 MHz
        .mode = 0,
        .spics_io_num = cs,
        .queue_size = 7,
    };
    // Attach the device to the SPI bus and store the handle
    spi_bus_add_device(spi_num, &devcfg, &g_spi_handle);
}

int hal_spi_master_transmit(uint8_t spi_num, const uint8_t* tx_data, uint8_t* rx_data, size_t size) {
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = size * 8;
    t.tx_buffer = tx_data;
    t.rx_buffer = rx_data;

    // Use the initialized global handle
    return spi_device_transmit(g_spi_handle, &t);
}
