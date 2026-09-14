#pragma once

#define REG_LR_FIRMWARE_VERSION_MSB 0x0153 //The address of the register holding the firmware version MSB
#define SX126X_REG_LR_ESTIMATED_FREQUENCY_ERROR_MSB 0x0954
#define SX126X_REG_LR_ESTIMATED_FREQUENCY_ERROR_MASK 0x0FFFFF

#define SX126X_REG_SF_ADDITIONAL_CONFIG             0x925
#define SX126X_REG_FREQ_ERR_CORRECTION              0x93C
#define SX126X_REG_FLRC_CRC_SEED                    0x9C8
#define SX126X_REG_FLRC_SYNC_WORD                   0x9CF

#define SX126X_XTAL_FREQ 52000000
#define FREQ_STEP ((double)(SX126X_XTAL_FREQ / pow(2.0, 18.0)))  // 198.3642578125

#define SX126X_POWER_MIN (-18)
#define SX126X_POWER_MAX (13)

typedef enum
{
    SX126X_BW_125_00_KHZ = 0b00000000,
    SX126X_BW_250_00_KHZ = 0b01000000,
    SX126X_BW_500_00_KHZ = 0b10000000
} SX126x_Bandwidth;

typedef enum
{
    SX126X_SF_6 = 0b01100000,
    SX126X_SF_7 = 0b01110000,
    SX126X_SF_8 = 0b10000000,
    SX126X_SF_9 = 0b10010000,
    SX126X_SF_10 = 0b10100000,
    SX126X_SF_11 = 0b10110000,
    SX126X_SF_12 = 0b11000000
} SX126x_SpreadingFactor;
#define SX126X_SPREADING_FACTOR_MASK 0b11110000

typedef enum
{
    SX126X_CR_4_5 = 0b00001000,
    SX126X_CR_4_6 = 0b00010000,
    SX126X_CR_4_7 = 0b00011000,
    SX126X_CR_4_8 = 0b00100000,
} SX126x_CodingRate;

typedef enum
{
    SX126X_RF_IDLE = 0x00, //!< The radio is idle
    SX126X_RF_RX_RUNNING,  //!< The radio is in reception state
    SX126X_RF_TX_RUNNING,  //!< The radio is in transmission state
    SX126X_RF_CAD,         //!< The radio is doing channel activity detection
} SX126x_RadioStates_t;

typedef enum
{
    SX126X_STATUS_CIRCUIT_MODE_STDBY_RC = 0x02 << 5,
    SX126X_STATUS_CIRCUIT_MODE_STDBY_XOSC = 0x03 << 5,
    SX126X_STATUS_CIRCUIT_MODE_FS = 0x04 << 5,
    SX126X_STATUS_CIRCUIT_MODE_RX = 0x05 << 5,
    SX126X_STATUS_CIRCUIT_MODE_TX = 0x06 << 5,

    SX126X_STATUS_COMMAND_PROCESSING_SUCCESS = 0x01 << 2,
    SX126X_STATUS_COMMAND_DATA_AVAILABLE = 0x02 << 2,
    SX126X_STATUS_COMMAND_TIMEOUT = 0x03 << 2,
    SX126X_STATUS_COMMAND_PROCESSING_ERROR = 0x04 << 2,
    SX126X_STATUS_COMMAND_EXECUTE_FAILED = 0x05 << 2,
    SX126X_STATUS_COMMAND_TX_DONE = 0x06 << 2,

    SX126X_STATUS_MASK = 0xFC,
} SX126x_RadioStatus_t;

/*!
 * \brief Represents the operating mode the radio is actually running
 */
typedef enum
{
    SX126X_MODE_SLEEP = 0x00, //! The radio is in sleep mode
    SX126X_MODE_CALIBRATION,  //! The radio is in calibration mode
    SX126X_MODE_STDBY_RC,     //! The radio is in standby mode with RC oscillator
    SX126X_MODE_STDBY_XOSC,   //! The radio is in standby mode with XOSC oscillator
    SX126X_MODE_FS,           //! The radio is in frequency synthesis mode
    SX126X_MODE_RX_CONT,      //! The radio is in continuous receive mode
    SX126X_MODE_TX,           //! The radio is in transmit mode
    SX126X_MODE_CAD           //! The radio is in channel activity detection mode
} SX126x_RadioOperatingModes_t;

#define SX126X_RX_TX_CONTINUOUS \
    (TickTime_t) { RADIO_TICK_SIZE_0015_US, 0xFFFF }
#define SX126X_RX_TX_SINGLE \
    (TickTime_t) { RADIO_TICK_SIZE_0015_US, 0 }

/*!
 * \brief Declares the oscillator in use while in standby mode
 *
 * Using the STDBY_RC standby mode allow to reduce the energy consumption
 * STDBY_XOSC should be used for time critical applications
 */
typedef enum
{
    SX126X_STDBY_RC = 0x00,
    SX126X_STDBY_XOSC = 0x01,
} SX126x_RadioStandbyModes_t;

/*!
 * \brief Declares the power regulation used to power the device
 *
 * This command allows the user to specify if DC-DC or LDO is used for power regulation.
 * Using only LDO implies that the Rx or Tx current is doubled
 */
typedef enum
{
    SX126X_USE_LDO = 0x00,  //! Use LDO (default value)
    SX126X_USE_DCDC = 0x01, //! Use DCDC
} SX126x_RadioRegulatorModes_t;

/*!
 * \brief Represents the possible packet type (i.e. modem) used
 */
typedef enum
{
    SX126X_PACKET_TYPE_GFSK = 0x00,
    SX126X_PACKET_TYPE_LORA,
    SX126X_PACKET_TYPE_RANGING,
    SX126X_PACKET_TYPE_FLRC,
    SX126X_PACKET_TYPE_BLE,
    SX126X_PACKET_TYPE_NONE = 0x0F,
} SX126x_RadioPacketTypes_t;

//only for GFSK
// typedef enum
// {
//     SX126X_PREAMBLE_LENGTH_04_BITS = 0x00, //!< Preamble length: 04 bits
//     SX126X_PREAMBLE_LENGTH_08_BITS = 0x10, //!< Preamble length: 08 bits
//     SX126X_PREAMBLE_LENGTH_12_BITS = 0x20, //!< Preamble length: 12 bits
//     SX126X_PREAMBLE_LENGTH_16_BITS = 0x30, //!< Preamble length: 16 bits
//     SX126X_PREAMBLE_LENGTH_20_BITS = 0x40, //!< Preamble length: 20 bits
//     SX126X_PREAMBLE_LENGTH_24_BITS = 0x50, //!< Preamble length: 24 bits
//     SX126X_PREAMBLE_LENGTH_28_BITS = 0x60, //!< Preamble length: 28 bits
//     SX126X_PREAMBLE_LENGTH_32_BITS = 0x70, //!< Preamble length: 32 bits
// } SX126X_RadioPreambleLengths_t;

typedef enum
{
    SX126X_LORA_IQ_NORMAL = 0x40,
    SX126X_LORA_IQ_INVERTED = 0x00,
} SX126x_RadioLoRaIQModes_t;

typedef enum
{
    SX126X_RADIO_CRC_OFF = 0x00, //!< No CRC in use
    SX126X_RADIO_CRC_1_BYTES = 0x10,
    SX126X_RADIO_CRC_2_BYTES = 0x20,
    SX126X_RADIO_CRC_3_BYTES = 0x30,
} SX126x_RadioCrcTypes_t;

/*!
 * \brief Represents the ramping time for power amplifier
 */
typedef enum
{
    SX126X_RADIO_RAMP_02_US = 0x00,
    SX126X_RADIO_RAMP_04_US = 0x20,
    SX126X_RADIO_RAMP_06_US = 0x40,
    SX126X_RADIO_RAMP_08_US = 0x60,
    SX126X_RADIO_RAMP_10_US = 0x80,
    SX126X_RADIO_RAMP_12_US = 0xA0,
    SX126X_RADIO_RAMP_16_US = 0xC0,
    SX126X_RADIO_RAMP_20_US = 0xE0,
} SX126x_RadioRampTimes_t;

/*!
 * \brief Represents the number of symbols to be used for channel activity detection operation
 */
typedef enum
{
    SX126X_LORA_CAD_01_SYMBOL = 0x00,
    SX126X_LORA_CAD_02_SYMBOLS = 0x20,
    SX126X_LORA_CAD_04_SYMBOLS = 0x40,
    SX126X_LORA_CAD_08_SYMBOLS = 0x60,
    SX126X_LORA_CAD_16_SYMBOLS = 0x80,
} SX126x_RadioLoRaCadSymbols_t;

/*!
 * \brief Represents the possible spreading factor values in LORA packet types
 */
typedef enum
{
    SX126X_LORA_SF5 = 0x50,
    SX126X_LORA_SF6 = 0x60,
    SX126X_LORA_SF7 = 0x70,
    SX126X_LORA_SF8 = 0x80,
    SX126X_LORA_SF9 = 0x90,
    SX126X_LORA_SF10 = 0xA0,
    SX126X_LORA_SF11 = 0xB0,
    SX126X_LORA_SF12 = 0xC0,
} SX126x_RadioLoRaSpreadingFactors_t;

/*!
 * \brief Represents the bandwidth values for LORA packet type
 */
typedef enum
{
    SX126X_LORA_BW_0200 = 0x34,
    SX126X_LORA_BW_0400 = 0x26,
    SX126X_LORA_BW_0800 = 0x18,
    SX126X_LORA_BW_1600 = 0x0A,
} SX126x_RadioLoRaBandwidths_t;

/*!
 * \brief Represents the coding rate values for LORA packet type
 */
typedef enum
{
    SX126X_LORA_CR_4_5 = 0x01,
    SX126X_LORA_CR_4_6 = 0x02,
    SX126X_LORA_CR_4_7 = 0x03,
    SX126X_LORA_CR_4_8 = 0x04,
    SX126X_LORA_CR_LI_4_5 = 0x05,
    SX126X_LORA_CR_LI_4_6 = 0x06,
    SX126X_LORA_CR_LI_4_8 = 0x07,
} SX126x_RadioLoRaCodingRates_t;

typedef enum
{
    SX126X_LORA_PACKET_VARIABLE_LENGTH = 0x00, //!< The packet is on variable size, header included
    SX126X_LORA_PACKET_FIXED_LENGTH = 0x80,    //!< The packet is known on both sides, no header included in the packet
    SX126X_LORA_PACKET_EXPLICIT = SX126X_LORA_PACKET_VARIABLE_LENGTH,
    SX126X_LORA_PACKET_IMPLICIT = SX126X_LORA_PACKET_FIXED_LENGTH,
} SX126x_RadioLoRaPacketLengthsModes_t;

typedef enum
{
    SX126X_LORA_CRC_ON = 0x20,  //!< CRC activated
    SX126X_LORA_CRC_OFF = 0x00, //!< CRC not used
} SX126x_RadioLoRaCrcModes_t;

/*!
 * \brief Represents the bandwidth values for FLRC packet type
 */
typedef enum
{
    SX126X_FLRC_BR_1_300_BW_1_2 = 0x45,
    SX126X_FLRC_BR_1_000_BW_1_2 = 0x69,
    SX126X_FLRC_BR_0_650_BW_0_6 = 0x86,
    SX126X_FLRC_BR_0_520_BW_0_6 = 0xAA,
    SX126X_FLRC_BR_0_325_BW_0_3 = 0xC7,
    SX126X_FLRC_BR_0_260_BW_0_3 = 0xEB,
} SX126x_RadioFlrcBandwidths_t;

/*!
 * \brief Represents the coding rate values for FLRC packet type
 */
typedef enum
{
    SX126X_FLRC_CR_1_2 = 0x00,
    SX126X_FLRC_CR_3_4 = 0x02,
    SX126X_FLRC_CR_1_0 = 0x04,
} SX126x_RadioFlrcCodingRates_t;

/*!
 * \brief Represents the Gaussian filter value in FLRC packet types
 */
typedef enum
{
    SX126X_FLRC_BT_DIS  = 0x00,
    SX126X_FLRC_BT_1    = 0x10,
    SX126X_FLRC_BT_0_5  = 0x20,
} SX126x_RadioFlrcGaussianFilter_t;

typedef enum
{
    SX126X_FLRC_SYNC_NOSYNC        = 0x00,
    SX126X_FLRC_SYNC_WORD_LEN_P32S = 0x04,
} SX126x_RadioFlrcSyncWordLen_t;

typedef enum
{
    SX126X_FLRC_RX_DISABLE_SYNC_WORD     = 0x00,
    SX126X_FLRC_RX_MATCH_SYNC_WORD_1     = 0x10,
    SX126X_FLRC_RX_MATCH_SYNC_WORD_2     = 0x20,
    SX126X_FLRC_RX_MATCH_SYNC_WORD_1_2   = 0x30,
    SX126X_FLRC_RX_MATCH_SYNC_WORD_3     = 0x40,
    SX126X_FLRC_RX_MATCH_SYNC_WORD_1_3   = 0x50,
    SX126X_FLRC_RX_MATCH_SYNC_WORD_2_3   = 0x60,
    SX126X_FLRC_RX_MATCH_SYNC_WORD_1_2_3 = 0x70,
} SX126x_RadioFlrcSyncWordCombination_t;

typedef enum
{
    SX126X_FLRC_PACKET_FIXED_LENGTH    = 0x00,
    SX126X_FLRC_PACKET_VARIABLE_LENGTH = 0x20,
} SX126x_RadioFlrcPacketType_t;

typedef enum
{
    SX126X_FLRC_CRC_OFF    = 0x00,
    SX126X_FLRC_CRC_2_BYTE = 0x10,
    SX126X_FLRC_CRC_3_BYTE = 0x20,
    SX126X_FLRC_CRC_4_BYTE = 0x30,
} SX126x_RadioFlrcCrc_t;

enum
{
    // Error Packet Status
    SX126X_FLRC_PKT_ERROR_BUSY      = 1 << 0,
    SX126X_FLRC_PKT_ERROR_PKT_RCVD  = 1 << 1,
    SX126X_FLRC_PKT_ERROR_HDR_RCVD  = 1 << 2,
    SX126X_FLRC_PKT_ERROR_ABORT     = 1 << 3,
    SX126X_FLRC_PKT_ERROR_CRC       = 1 << 4,
    SX126X_FLRC_PKT_ERROR_LENGTH    = 1 << 5,
    SX126X_FLRC_PKT_ERROR_SYNC      = 1 << 6,
};

typedef enum RadioCommands_u
{
    SX126X_RADIO_GET_STATUS = 0xC0,
    SX126X_RADIO_WRITE_REGISTER = 0x18,
    SX126X_RADIO_READ_REGISTER = 0x19,
    SX126X_RADIO_WRITE_BUFFER = 0x1A,
    SX126X_RADIO_READ_BUFFER = 0x1B,
    SX126X_RADIO_SET_SLEEP = 0x84,
    SX126X_RADIO_SET_STANDBY = 0x80,
    SX126X_RADIO_SET_FS = 0xC1,
    SX126X_RADIO_SET_TX = 0x83,
    SX126X_RADIO_SET_RX = 0x82,
    SX126X_RADIO_SET_RXDUTYCYCLE = 0x94,
    SX126X_RADIO_SET_CAD = 0xC5,
    SX126X_RADIO_SET_TXCONTINUOUSWAVE = 0xD1,
    SX126X_RADIO_SET_TXCONTINUOUSPREAMBLE = 0xD2,
    SX126X_RADIO_SET_PACKETTYPE = 0x8A,
    SX126X_RADIO_GET_PACKETTYPE = 0x03,
    SX126X_RADIO_SET_RFFREQUENCY = 0x86,
    SX126X_RADIO_SET_TXPARAMS = 0x8E,
    SX126X_RADIO_SET_CADPARAMS = 0x88,
    SX126X_RADIO_SET_BUFFERBASEADDRESS = 0x8F,
    SX126X_RADIO_SET_MODULATIONPARAMS = 0x8B,
    SX126X_RADIO_SET_PACKETPARAMS = 0x8C,
    SX126X_RADIO_GET_RXBUFFERSTATUS = 0x17,
    SX126X_RADIO_GET_PACKETSTATUS = 0x1D,
    SX126X_RADIO_GET_RSSIINST = 0x1F,
    SX126X_RADIO_SET_DIOIRQPARAMS = 0x8D,
    SX126X_RADIO_GET_IRQSTATUS = 0x15,
    SX126X_RADIO_CLR_IRQSTATUS = 0x97,
    SX126X_RADIO_CALIBRATE = 0x89,
    SX126X_RADIO_SET_REGULATORMODE = 0x96,
    SX126X_RADIO_SET_SAVECONTEXT = 0xD5,
    SX126X_RADIO_SET_AUTOTX = 0x98,
    SX126X_RADIO_SET_AUTOFS = 0x9E,
    SX126X_RADIO_SET_LONGPREAMBLE = 0x9B,
    SX126X_RADIO_SET_UARTSPEED = 0x9D,
    SX126X_RADIO_SET_RANGING_ROLE = 0xA3,
} SX126x_RadioCommands_t;

typedef enum
{
    SX126X_IRQ_RADIO_NONE = 0x0000,
    SX126X_IRQ_TX_DONE = 0x0001,
    SX126X_IRQ_RX_DONE = 0x0002,
    SX126X_IRQ_SYNCWORD_VALID = 0x0004,
    SX126X_IRQ_SYNCWORD_ERROR = 0x0008,
    SX126X_IRQ_HEADER_VALID = 0x0010,
    SX126X_IRQ_HEADER_ERROR = 0x0020,
    SX126X_IRQ_CRC_ERROR = 0x0040,
    SX126X_IRQ_RANGING_SLAVE_RESPONSE_DONE = 0x0080,
    SX126X_IRQ_RANGING_SLAVE_REQUEST_DISCARDED = 0x0100,
    SX126X_IRQ_RANGING_MASTER_RESULT_VALID = 0x0200,
    SX126X_IRQ_RANGING_MASTER_TIMEOUT = 0x0400,
    SX126X_IRQ_RANGING_SLAVE_REQUEST_VALID = 0x0800,
    SX126X_IRQ_CAD_DONE = 0x1000,
    SX126X_IRQ_CAD_DETECTED = 0x2000,
    SX126X_IRQ_RX_TX_TIMEOUT = 0x4000,
    SX126X_IRQ_PREAMBLE_DETECTED = 0x8000,
    SX126X_IRQ_RADIO_ALL = 0xFFFF,
} SX126x_RadioIrqMasks_t;

typedef enum
{
    SX126X_RADIO_DIO1 = 0x02,
    SX126X_RADIO_DIO2 = 0x04,
    SX126X_RADIO_DIO3 = 0x08,
} SX126x_RadioDios_t;

typedef enum
{
    SX126X_RADIO_TICK_SIZE_0015_US = 0x00,
    SX126X_RADIO_TICK_SIZE_0062_US = 0x01,
    SX126X_RADIO_TICK_SIZE_1000_US = 0x02,
    SX126X_RADIO_TICK_SIZE_4000_US = 0x03,
} SX126x_RadioTickSizes_t;