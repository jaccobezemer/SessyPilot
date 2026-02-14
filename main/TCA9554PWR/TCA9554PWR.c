#include "TCA9554PWR.h"

static i2c_master_dev_handle_t s_tca_dev = NULL;

static void ensure_device(void)
{
    if (s_tca_dev) return;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TCA9554_ADDRESS,
        .scl_speed_hz = 400000,
    };
    i2c_master_bus_add_device(i2c_bus, &cfg, &s_tca_dev);
}

/*****************************************************  Operation register REG   ****************************************************/
uint8_t Read_REG(uint8_t REG)
{
    ensure_device();
    uint8_t bitsStatus = 0;
    i2c_master_transmit_receive(s_tca_dev, &REG, 1, &bitsStatus, 1, I2C_MASTER_TIMEOUT_MS);
    return bitsStatus;
}

void Write_REG(uint8_t REG, uint8_t Data)
{
    ensure_device();
    uint8_t buf[2] = {REG, Data};
    i2c_master_transmit(s_tca_dev, buf, 2, I2C_MASTER_TIMEOUT_MS);
}

/********************************************************** Set EXIO mode **********************************************************/
void Mode_EXIO(uint8_t Pin, uint8_t State)
{
    uint8_t bitsStatus = Read_REG(TCA9554_CONFIG_REG);
    uint8_t Data = (0x01 << (Pin - 1)) | bitsStatus;
    Write_REG(TCA9554_CONFIG_REG, Data);
}

void Mode_EXIOS(uint8_t PinState)
{
    Write_REG(TCA9554_CONFIG_REG, PinState);
}

/********************************************************** Read EXIO status **********************************************************/
uint8_t Read_EXIO(uint8_t Pin)
{
    uint8_t inputBits = Read_REG(TCA9554_INPUT_REG);
    uint8_t bitStatus = (inputBits >> (Pin - 1)) & 0x01;
    return bitStatus;
}

uint8_t Read_EXIOS(void)
{
    uint8_t inputBits = Read_REG(TCA9554_INPUT_REG);
    return inputBits;
}

/********************************************************** Set the EXIO output status **********************************************************/
void Set_EXIO(uint8_t Pin, uint8_t State)
{
    uint8_t Data = 0;
    uint8_t bitsStatus = Read_REG(TCA9554_OUTPUT_REG);
    if (State < 2 && Pin < 8 && Pin > 0) {
        if (State == 1)
            Data = (0x01 << (Pin - 1)) | bitsStatus;
        else if (State == 0)
            Data = (~(0x01 << (Pin - 1)) & bitsStatus);
        Write_REG(TCA9554_OUTPUT_REG, Data);
    } else {
        printf("Parameter error, please enter the correct parameter!\r\n");
    }
}

void Set_EXIOS(uint8_t PinState)
{
    Write_REG(TCA9554_OUTPUT_REG, PinState);
}

/********************************************************** Flip EXIO state **********************************************************/
void Set_Toggle(uint8_t Pin)
{
    uint8_t bitsStatus = Read_EXIO(Pin);
    Set_EXIO(Pin, (bool)!bitsStatus);
}

/********************************************************* TCA9554PWR Initializes the device ***********************************************************/
void TCA9554PWR_Init(uint8_t PinState)
{
    Mode_EXIOS(PinState);
}
