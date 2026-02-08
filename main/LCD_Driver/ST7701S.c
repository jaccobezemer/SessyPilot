#include "ST7701S.h"

#define SPI_WriteComm(cmd) ST7701S_WriteCommand(St7701S_handle, cmd)
#define SPI_WriteData(data) ST7701S_WriteData(St7701S_handle, data)
#define Delay(ms) vTaskDelay(ms / portTICK_PERIOD_MS)

void ioexpander_init(){};
void ioexpander_write_cmd(){};
void ioexpander_write_data(){};

/**
 * @brief Example Create an ST7701S object
 * @param SDA SDA pin
 * @param SCL SCL pin
 * @param CS  CS  pin
 * @param channel_select SPI channel selection
 * @param method_select  SPI_METHOD,IOEXPANDER_METHOD
 * @note
*/
ST7701S_handle ST7701S_newObject(int SDA, int SCL, int CS, char channel_select, char method_select)
{
    // if you use `malloc()`, please set 0 in the area to be assigned.
    ST7701S_handle st7701s_handle = heap_caps_calloc(1, sizeof(ST7701S), MALLOC_CAP_DEFAULT);
    st7701s_handle->method_select = method_select;
    
    if(method_select){
        st7701s_handle->spi_io_config_t.miso_io_num = -1;
        st7701s_handle->spi_io_config_t.mosi_io_num = SDA;
        st7701s_handle->spi_io_config_t.sclk_io_num = SCL;
        st7701s_handle->spi_io_config_t.quadwp_io_num = -1;
        st7701s_handle->spi_io_config_t.quadhd_io_num = -1;

        st7701s_handle->spi_io_config_t.max_transfer_sz = SOC_SPI_MAXIMUM_BUFFER_SIZE;

        ESP_ERROR_CHECK(spi_bus_initialize(channel_select, &(st7701s_handle->spi_io_config_t),SPI_DMA_CH_AUTO));

        st7701s_handle->st7701s_protocol_config_t.command_bits = 1;
        st7701s_handle->st7701s_protocol_config_t.address_bits = 8;
        st7701s_handle->st7701s_protocol_config_t.clock_speed_hz = 10000000;
        st7701s_handle->st7701s_protocol_config_t.mode = 0;
        st7701s_handle->st7701s_protocol_config_t.spics_io_num = CS;
        st7701s_handle->st7701s_protocol_config_t.queue_size = 1;

        ESP_ERROR_CHECK(spi_bus_add_device(channel_select, &(st7701s_handle->st7701s_protocol_config_t),
                                        &(st7701s_handle->spi_device)));
        
        return st7701s_handle;
    }else{
        ioexpander_init();
    }
    return NULL;
}

/**
 * @brief Screen initialization
 * @param St7701S_handle 
 * @param type 
 * @note
*/
void ST7701S_screen_init(ST7701S_handle St7701S_handle, unsigned char type)
{
    if (type == 1){
    // 4inch
SPI_WriteComm(0x11); 
    Delay(120); //Delay 120ms 

    SPI_WriteComm(0xFF);
    SPI_WriteData(0x77);
    SPI_WriteData(0x01);
    SPI_WriteData(0x00);
    SPI_WriteData(0x00);
    SPI_WriteData(0x10);

    SPI_WriteComm(0xC0);
    SPI_WriteData(0x3B);
    SPI_WriteData(0x00);

    SPI_WriteComm(0xC1);
    SPI_WriteData(0x0D);
    SPI_WriteData(0x02);

    SPI_WriteComm(0xC2);
    SPI_WriteData(0x21);
    SPI_WriteData(0x08);

    SPI_WriteComm(0xCD);
    SPI_WriteData(0x08);


    SPI_WriteComm(0xB0);
    SPI_WriteData(0x00);
    SPI_WriteData(0x11);
    SPI_WriteData(0x18);
    SPI_WriteData(0x0E);
    SPI_WriteData(0x11);
    SPI_WriteData(0x06);
    SPI_WriteData(0x07);
    SPI_WriteData(0x08);
    SPI_WriteData(0x07);
    SPI_WriteData(0x22);
    SPI_WriteData(0x04);
    SPI_WriteData(0x12);
    SPI_WriteData(0x0F);
    SPI_WriteData(0xAA);
    SPI_WriteData(0x31);
    SPI_WriteData(0x18);

    SPI_WriteComm(0xB1);
    SPI_WriteData(0x00);
    SPI_WriteData(0x11);
    SPI_WriteData(0x19);
    SPI_WriteData(0x0E);
    SPI_WriteData(0x12);
    SPI_WriteData(0x07);
    SPI_WriteData(0x08);
    SPI_WriteData(0x08);
    SPI_WriteData(0x08);
    SPI_WriteData(0x22);
    SPI_WriteData(0x04);
    SPI_WriteData(0x11);
    SPI_WriteData(0x11);
    SPI_WriteData(0xA9);
    SPI_WriteData(0x32);
    SPI_WriteData(0x18);

    SPI_WriteComm(0xFF);

    SPI_WriteData(0x77);
    SPI_WriteData(0x01);
    SPI_WriteData(0x00);
    SPI_WriteData(0x00);
    SPI_WriteData(0x11);

    SPI_WriteComm(0xB0);
    SPI_WriteData(0x60);

    SPI_WriteComm(0xB1);
    SPI_WriteData(0x30);

    SPI_WriteComm(0xB2);
    SPI_WriteData(0x87);

    SPI_WriteComm(0xB3);
    SPI_WriteData(0x80);

    SPI_WriteComm(0xB5);
    SPI_WriteData(0x49);

    SPI_WriteComm(0xB7);
    SPI_WriteData(0x85);

    SPI_WriteComm(0xB8);
    SPI_WriteData(0x21);

    SPI_WriteComm(0xC1);
    SPI_WriteData(0x78);

    SPI_WriteComm(0xC2);
    SPI_WriteData(0x78);

    Delay(20);
    SPI_WriteComm(0xE0);
    SPI_WriteData(0x00);
    SPI_WriteData(0x1B);
    SPI_WriteData(0x02);

    SPI_WriteComm(0xE1);
    SPI_WriteData(0x08);
    SPI_WriteData(0xA0);
    SPI_WriteData(0x00);
    SPI_WriteData(0x00);
    SPI_WriteData(0x07);
    SPI_WriteData(0xA0);
    SPI_WriteData(0x00);
    SPI_WriteData(0x00);
    SPI_WriteData(0x00);
    SPI_WriteData(0x44);
    SPI_WriteData(0x44);

    SPI_WriteComm(0xE2);
    SPI_WriteData(0x11);
    SPI_WriteData(0x11);
    SPI_WriteData(0x44);
    SPI_WriteData(0x44);
    SPI_WriteData(0xED);
    SPI_WriteData(0xA0);
    SPI_WriteData(0x00);
    SPI_WriteData(0x00);
    SPI_WriteData(0xEC);
    SPI_WriteData(0xA0);
    SPI_WriteData(0x00);
    SPI_WriteData(0x00); 

    SPI_WriteComm(0xE3);
    SPI_WriteData(0x00);
    SPI_WriteData(0x00);
    SPI_WriteData(0x11);
    SPI_WriteData(0x11);

    SPI_WriteComm(0xE4);
    SPI_WriteData(0x44);
    SPI_WriteData(0x44);

    SPI_WriteComm(0xE5);
    SPI_WriteData(0x0A);
    SPI_WriteData(0xE9);
    SPI_WriteData(0xD8);
    SPI_WriteData(0xA0);
    SPI_WriteData(0x0C);
    SPI_WriteData(0xEB);
    SPI_WriteData(0xD8);
    SPI_WriteData(0xA0);
    SPI_WriteData(0x0E);
    SPI_WriteData(0xED);
    SPI_WriteData(0xD8);
    SPI_WriteData(0xA0);
    SPI_WriteData(0x10);
    SPI_WriteData(0xEF);
    SPI_WriteData(0xD8);
    SPI_WriteData(0xA0);

    SPI_WriteComm(0xE6);
    SPI_WriteData(0x00);
    SPI_WriteData(0x00);
    SPI_WriteData(0x11);
    SPI_WriteData(0x11);

    SPI_WriteComm(0xE7);
    SPI_WriteData(0x44);
    SPI_WriteData(0x44);

    SPI_WriteComm(0xE8);
    SPI_WriteData(0x09);
    SPI_WriteData(0xE8);
    SPI_WriteData(0xD8);
    SPI_WriteData(0xA0);
    SPI_WriteData(0x0B);
    SPI_WriteData(0xEA);
    SPI_WriteData(0xD8);
    SPI_WriteData(0xA0);
    SPI_WriteData(0x0D);
    SPI_WriteData(0xEC);
    SPI_WriteData(0xD8);
    SPI_WriteData(0xA0);
    SPI_WriteData(0x0F);
    SPI_WriteData(0xEE);
    SPI_WriteData(0xD8);
    SPI_WriteData(0xA0);

    SPI_WriteComm(0xEB);
    SPI_WriteData(0x02);
    SPI_WriteData(0x00);
    SPI_WriteData(0xE4);
    SPI_WriteData(0xE4);
    SPI_WriteData(0x88);
    SPI_WriteData(0x00);
    SPI_WriteData(0x40);

    SPI_WriteComm(0xEC);
    SPI_WriteData(0x3C);
    SPI_WriteData(0x00);

    SPI_WriteComm(0xED);
    SPI_WriteData(0xAB);
    SPI_WriteData(0x89);
    SPI_WriteData(0x76);
    SPI_WriteData(0x54);
    SPI_WriteData(0x02);
    SPI_WriteData(0xFF);
    SPI_WriteData(0xFF);
    SPI_WriteData(0xFF);
    SPI_WriteData(0xFF);
    SPI_WriteData(0xFF);
    SPI_WriteData(0xFF);
    SPI_WriteData(0x20);
    SPI_WriteData(0x45);
    SPI_WriteData(0x67);
    SPI_WriteData(0x98);
    SPI_WriteData(0xBA);

    SPI_WriteComm(0xFF);
    SPI_WriteData(0x77);
    SPI_WriteData(0x01);
    SPI_WriteData(0x00);
    SPI_WriteData(0x00);
    SPI_WriteData(0x00);



    SPI_WriteComm(0x36); 
    SPI_WriteData(0x00); 
    SPI_WriteComm(0x3A); 
    SPI_WriteData(0x66); 

    SPI_WriteComm(0x21);
    Delay (120);
    SPI_WriteComm(0x29); 
    }
}

/**
 * @brief Example Delete the ST7701S object
 * @param St7701S_handle 
*/
void ST7701S_delObject(ST7701S_handle St7701S_handle)
{
    assert(St7701S_handle != NULL);
    free(St7701S_handle);
}

/**
 * @brief SPI write instruction
 * @param St7701S_handle 
 * @param cmd instruction
*/
void ST7701S_WriteCommand(ST7701S_handle St7701S_handle, uint8_t cmd)
{
    if(St7701S_handle->method_select){
        spi_transaction_t spi_tran = {
            .rxlength = 0,
            .length = 0,
            .cmd = 0,
            .addr = cmd,
        };
        spi_device_transmit(St7701S_handle->spi_device, &spi_tran);
    }else{
        ioexpander_write_cmd();
    }
}

/**
 * @brief SPI write data
 * @param St7701S_handle
 * @param data 
*/
void ST7701S_WriteData(ST7701S_handle St7701S_handle, uint8_t data)
{
    if(St7701S_handle->method_select){
        spi_transaction_t spi_tran = {
            .rxlength = 0,
            .length = 0,
            .cmd = 1,
            .addr = data,
        };
        spi_device_transmit(St7701S_handle->spi_device, &spi_tran);
    }else{
        ioexpander_write_data();
    }
}