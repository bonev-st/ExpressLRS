#pragma once

// SX1261/2 datasheet rev 2.1 and RadioLib SX126x_commands.h / SX126x_registers.h

#define SX126X_XTAL_FREQ 32000000

#define SX126X_POWER_MIN (-9)
#define SX126X_POWER_MAX (22)

// Registers
#define SX126X_REG_VERSION_STRING           0x0320 // "SX1261 V2D 2D02", 16 bytes
#define SX126X_REG_IQ_POLARITY              0x0736 // Errata 15.4: bit 2 = 1 for standard IQ, 0 for inverted
#define SX126X_REG_LORA_SYNC_WORD_MSB       0x0740
#define SX126X_REG_LORA_SYNC_WORD_LSB       0x0741
#define SX126X_REG_FREQ_ERROR               0x076B // 20-bit two's complement estimate, 3 bytes
#define SX126X_REG_TX_MODULATION            0x0889 // Errata 15.1: bit 2 = 0 for LoRa BW500, 1 otherwise
#define SX126X_REG_RX_GAIN                  0x08AC
#define SX126X_REG_TX_CLAMP_CONFIG          0x08D8 // Errata 15.2: bits 4:1 = 0b1111
#define SX126X_REG_OCP_CONFIGURATION        0x08E7 // 2.5 mA per LSB

#define SX126X_IQ_POLARITY_STANDARD_BIT     0x04
#define SX126X_TX_MODULATION_BW500_BIT      0x04
#define SX126X_TX_CLAMP_CONFIG_BITS         0x1E
#define SX126X_RX_GAIN_BOOSTED              0x96
#define SX126X_OCP_140_MA                   0x38

// Sync word 0x12 in SX127x terms, the reset value of the SX126x register pair
#define SX126X_LORA_SYNC_WORD_MSB           0x14
#define SX126X_LORA_SYNC_WORD_LSB           0x24

typedef enum
{
    SX126X_RADIO_RESET_STATS                = 0x00,
    SX126X_RADIO_CLR_IRQSTATUS              = 0x02,
    SX126X_RADIO_CLR_ERROR                  = 0x07,
    SX126X_RADIO_CFG_DIOIRQ                 = 0x08,
    SX126X_RADIO_WRITE_REGISTER             = 0x0D,
    SX126X_RADIO_WRITE_BUFFER               = 0x0E,
    SX126X_RADIO_GET_STATS                  = 0x10,
    SX126X_RADIO_GET_PACKETTYPE             = 0x11,
    SX126X_RADIO_GET_IRQSTATUS              = 0x12,
    SX126X_RADIO_GET_RXBUFFERSTATUS         = 0x13,
    SX126X_RADIO_GET_PACKETSTATUS           = 0x14,
    SX126X_RADIO_GET_RSSIINST               = 0x15,
    SX126X_RADIO_GET_ERROR                  = 0x17,
    SX126X_RADIO_READ_REGISTER              = 0x1D,
    SX126X_RADIO_READ_BUFFER                = 0x1E,
    SX126X_RADIO_SET_STANDBY                = 0x80,
    SX126X_RADIO_SET_RX                     = 0x82,
    SX126X_RADIO_SET_TX                     = 0x83,
    SX126X_RADIO_SET_SLEEP                  = 0x84,
    SX126X_RADIO_SET_RFFREQUENCY            = 0x86,
    SX126X_RADIO_SET_CADPARAMS              = 0x88,
    SX126X_RADIO_CALIBRATE                  = 0x89,
    SX126X_RADIO_SET_PACKETTYPE             = 0x8A,
    SX126X_RADIO_SET_MODULATIONPARAMS       = 0x8B,
    SX126X_RADIO_SET_PACKETPARAMS           = 0x8C,
    SX126X_RADIO_SET_TXPARAMS               = 0x8E,
    SX126X_RADIO_SET_BUFFERBASEADDRESS      = 0x8F,
    SX126X_RADIO_SET_RXTXFALLBACKMODE       = 0x93,
    SX126X_RADIO_SET_RXDUTYCYCLE            = 0x94,
    SX126X_RADIO_SET_PACONFIG               = 0x95,
    SX126X_RADIO_SET_REGULATORMODE          = 0x96,
    SX126X_RADIO_SET_TCXOMODE               = 0x97, // SetDio3AsTcxoCtrl
    SX126X_RADIO_CALIBRATEIMAGE             = 0x98,
    SX126X_RADIO_SET_RFSWITCHMODE           = 0x9D, // SetDio2AsRfSwitchCtrl
    SX126X_RADIO_SET_STOPRXTIMERONPREAMBLE  = 0x9F,
    SX126X_RADIO_SET_LORASYMBTIMEOUT        = 0xA0,
    SX126X_RADIO_GET_STATUS                 = 0xC0,
    SX126X_RADIO_SET_FS                     = 0xC1,
    SX126X_RADIO_SET_CAD                    = 0xC5,
    SX126X_RADIO_SET_TXCONTINUOUSWAVE       = 0xD1,
    SX126X_RADIO_SET_TXCONTINUOUSPREAMBLE   = 0xD2,
} SX126x_RadioCommands_t;

/*!
 * \brief Status byte, returned by the chip on the byte after a Get opcode
 */
typedef enum
{
    SX126X_STATUS_MODE_STDBY_RC             = 0x02 << 4,
    SX126X_STATUS_MODE_STDBY_XOSC           = 0x03 << 4,
    SX126X_STATUS_MODE_FS                   = 0x04 << 4,
    SX126X_STATUS_MODE_RX                   = 0x05 << 4,
    SX126X_STATUS_MODE_TX                   = 0x06 << 4,
    SX126X_STATUS_MODE_MASK                 = 0x07 << 4,

    SX126X_STATUS_CMD_DATA_AVAILABLE        = 0x02 << 1,
    SX126X_STATUS_CMD_TIMEOUT               = 0x03 << 1,
    SX126X_STATUS_CMD_PROCESSING_ERROR      = 0x04 << 1,
    SX126X_STATUS_CMD_EXECUTE_FAILED        = 0x05 << 1,
    SX126X_STATUS_CMD_TX_DONE               = 0x06 << 1,
    SX126X_STATUS_CMD_MASK                  = 0x07 << 1,

    SX126X_STATUS_MASK                      = SX126X_STATUS_MODE_MASK | SX126X_STATUS_CMD_MASK,
} SX126x_RadioStatus_t;

/*!
 * \brief Represents the operating mode the radio is actually running
 */
typedef enum
{
    SX126X_MODE_SLEEP = 0x00, //! The radio is in sleep mode
    SX126X_MODE_STDBY_RC,     //! The radio is in standby mode with RC oscillator
    SX126X_MODE_STDBY_XOSC,   //! The radio is in standby mode with XOSC oscillator
    SX126X_MODE_FS,           //! The radio is in frequency synthesis mode
    SX126X_MODE_RX_CONT,      //! The radio is in continuous receive mode
    SX126X_MODE_TX,           //! The radio is in transmit mode
} SX126x_RadioOperatingModes_t;

typedef enum
{
    SX126X_SLEEP_COLD_START = 0x00, // Configuration is lost, Begin() restores it
    SX126X_SLEEP_WARM_START = 0x04,
} SX126x_RadioSleepModes_t;

/*!
 * \brief Declares the oscillator in use while in standby mode
 *
 * STDBY_RC switches the TCXO off, so every later XOSC/FS/RX/TX holds BUSY for the TCXO start delay
 */
typedef enum
{
    SX126X_STDBY_RC = 0x00,
    SX126X_STDBY_XOSC = 0x01,
} SX126x_RadioStandbyModes_t;

typedef enum
{
    SX126X_FALLBACK_STDBY_RC = 0x20,
    SX126X_FALLBACK_STDBY_XOSC = 0x30,
    SX126X_FALLBACK_FS = 0x40,
} SX126x_RadioFallbackModes_t;

/*!
 * \brief Declares the power regulation used to power the device
 */
typedef enum
{
    SX126X_USE_LDO = 0x00,  //! Use LDO (default value)
    SX126X_USE_DCDC = 0x01, //! Use DCDC
} SX126x_RadioRegulatorModes_t;

typedef enum
{
    SX126X_PACKET_TYPE_GFSK = 0x00,
    SX126X_PACKET_TYPE_LORA = 0x01,
} SX126x_RadioPacketTypes_t;

typedef enum
{
    SX126X_CALIBRATE_RC64K = 0x01,
    SX126X_CALIBRATE_RC13M = 0x02,
    SX126X_CALIBRATE_PLL = 0x04,
    SX126X_CALIBRATE_ADC_PULSE = 0x08,
    SX126X_CALIBRATE_ADC_BULK_N = 0x10,
    SX126X_CALIBRATE_ADC_BULK_P = 0x20,
    SX126X_CALIBRATE_IMAGE = 0x40,
    SX126X_CALIBRATE_ALL = 0x7F,
} SX126x_RadioCalibrationFlags_t;

/*!
 * \brief DIO3 voltage for the TCXO, the same codes as the hardware option radio_tcxo
 */
typedef enum
{
    SX126X_TCXO_CTRL_1_6V = 0x00,
    SX126X_TCXO_CTRL_1_7V = 0x01,
    SX126X_TCXO_CTRL_1_8V = 0x02,
    SX126X_TCXO_CTRL_2_2V = 0x03,
    SX126X_TCXO_CTRL_2_4V = 0x04,
    SX126X_TCXO_CTRL_2_7V = 0x05,
    SX126X_TCXO_CTRL_3_0V = 0x06,
    SX126X_TCXO_CTRL_3_3V = 0x07,
} SX126x_RadioTcxoVoltages_t;

// TCXO start delay in 15.625 us steps, 320 = 5 ms
#define SX126X_TCXO_DELAY_DEFAULT 320

// SetPaConfig for +22 dBm on the SX1262 (datasheet table 13-21)
#define SX126X_PA_DUTY_CYCLE_22DBM  0x04
#define SX126X_PA_HP_MAX_22DBM      0x07
#define SX126X_PA_DEVICE_SX1262     0x00
#define SX126X_PA_LUT               0x01

typedef enum
{
    SX126X_RADIO_RAMP_10_US = 0x00,
    SX126X_RADIO_RAMP_20_US = 0x01,
    SX126X_RADIO_RAMP_40_US = 0x02,
    SX126X_RADIO_RAMP_80_US = 0x03,
    SX126X_RADIO_RAMP_200_US = 0x04,
    SX126X_RADIO_RAMP_800_US = 0x05,
    SX126X_RADIO_RAMP_1700_US = 0x06,
    SX126X_RADIO_RAMP_3400_US = 0x07,
} SX126x_RadioRampTimes_t;

typedef enum
{
    SX126X_LORA_SF5 = 0x05,
    SX126X_LORA_SF6 = 0x06,
    SX126X_LORA_SF7 = 0x07,
    SX126X_LORA_SF8 = 0x08,
    SX126X_LORA_SF9 = 0x09,
    SX126X_LORA_SF10 = 0x0A,
    SX126X_LORA_SF11 = 0x0B,
    SX126X_LORA_SF12 = 0x0C,
} SX126x_RadioLoRaSpreadingFactors_t;

typedef enum
{
    SX126X_LORA_BW_125 = 0x04,
    SX126X_LORA_BW_250 = 0x05,
    SX126X_LORA_BW_500 = 0x06,
} SX126x_RadioLoRaBandwidths_t;

typedef enum
{
    SX126X_LORA_CR_4_5 = 0x01,
    SX126X_LORA_CR_4_6 = 0x02,
    SX126X_LORA_CR_4_7 = 0x03,
    SX126X_LORA_CR_4_8 = 0x04,
} SX126x_RadioLoRaCodingRates_t;

typedef enum
{
    SX126X_LORA_LDRO_OFF = 0x00,
    SX126X_LORA_LDRO_ON = 0x01,
} SX126x_RadioLoRaLowDataRateOptimize_t;

typedef enum
{
    SX126X_LORA_PACKET_EXPLICIT = 0x00, //!< The packet is on variable size, header included
    SX126X_LORA_PACKET_IMPLICIT = 0x01, //!< The packet is known on both sides, no header included in the packet
} SX126x_RadioLoRaPacketLengthsModes_t;

typedef enum
{
    SX126X_LORA_CRC_OFF = 0x00,
    SX126X_LORA_CRC_ON = 0x01,
} SX126x_RadioLoRaCrcModes_t;

typedef enum
{
    SX126X_LORA_IQ_STANDARD = 0x00,
    SX126X_LORA_IQ_INVERTED = 0x01,
} SX126x_RadioLoRaIQModes_t;

typedef enum
{
    SX126X_IRQ_RADIO_NONE = 0x0000,
    SX126X_IRQ_TX_DONE = 0x0001,
    SX126X_IRQ_RX_DONE = 0x0002,
    SX126X_IRQ_PREAMBLE_DETECTED = 0x0004,
    SX126X_IRQ_SYNCWORD_VALID = 0x0008,
    SX126X_IRQ_HEADER_VALID = 0x0010,
    SX126X_IRQ_HEADER_ERROR = 0x0020,
    SX126X_IRQ_CRC_ERROR = 0x0040,
    SX126X_IRQ_CAD_DONE = 0x0080,
    SX126X_IRQ_CAD_DETECTED = 0x0100,
    SX126X_IRQ_RX_TX_TIMEOUT = 0x0200,
    SX126X_IRQ_RADIO_ALL = 0x03FF,
} SX126x_RadioIrqMasks_t;

// SetRx/SetTx timeout, 24 bit in 15.625 us steps
#define SX126X_RX_TIMEOUT_CONTINUOUS 0xFFFFFF
#define SX126X_TX_TIMEOUT_NONE 0x000000
