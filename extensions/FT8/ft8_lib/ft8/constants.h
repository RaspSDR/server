#ifndef _INCLUDE_CONSTANTS_H_
#define _INCLUDE_CONSTANTS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define FT8_SYMBOL_PERIOD (0.160f) ///< FT8 symbol duration, defines tone deviation in Hz and symbol rate
#define FT8_SLOT_TIME     (15.0f)  ///< FT8 slot period

#define FT4_SYMBOL_PERIOD (0.048f) ///< FT4 symbol duration, defines tone deviation in Hz and symbol rate
#define FT4_SLOT_TIME     (7.5f)   ///< FT4 slot period

// Define FT8 symbol counts
// FT8 message structure:
//     S D1 S D2 S
// S  - sync block (7 symbols of Costas pattern)
// D1 - first data block (29 symbols each encoding 3 bits)
#define FT8_ND          (58) ///< Data symbols
#define FT8_NN          (79) ///< Total channel symbols (FT8_NS + FT8_ND)
#define FT8_LENGTH_SYNC (7)  ///< Length of each sync group
#define FT8_NUM_SYNC    (3)  ///< Number of sync groups
#define FT8_SYNC_OFFSET (36) ///< Offset between sync groups

// Define FT4 symbol counts
// FT4 message structure:
//     R Sa D1 Sb D2 Sc D3 Sd R
// R  - ramping symbol (no payload information conveyed)
// Sx - one of four _different_ sync blocks (4 symbols of Costas pattern)
// Dy - data block (29 symbols each encoding 2 bits)
#define FT4_ND          (87)  ///< Data symbols
#define FT4_NR          (2)   ///< Ramp symbols (beginning + end)
#define FT4_NN          (105) ///< Total channel symbols (FT4_NS + FT4_ND + FT4_NR)
#define FT4_LENGTH_SYNC (4)   ///< Length of each sync group
#define FT4_NUM_SYNC    (4)   ///< Number of sync groups
#define FT4_SYNC_OFFSET (33)  ///< Offset between sync groups

// Define LDPC parameters
#define FTX_LDPC_N       (174)                  ///< Number of bits in the encoded message (payload with LDPC checksum bits)
#define FTX_LDPC_K       (91)                   ///< Number of payload bits (including CRC)
#define FTX_LDPC_M       (83)                   ///< Number of LDPC checksum bits (FTX_LDPC_N - FTX_LDPC_K)
#define FTX_LDPC_N_BYTES ((FTX_LDPC_N + 7) / 8) ///< Number of whole bytes needed to store 174 bits (full message)
#define FTX_LDPC_K_BYTES ((FTX_LDPC_K + 7) / 8) ///< Number of whole bytes needed to store 91 bits (payload + CRC only)

// Define FST4 symbol counts
// FST4 message structure:
//     S8 D30 S8 D30 S8 D30 S8 D30 S8
// S8  - sync block (8 symbols)
// D30 - data block (30 symbols each encoding 2 bits)
#define FST4_ND          (120) ///< Data symbols
#define FST4_NS          (40)  ///< Sync symbols (5 groups of 8)
#define FST4_NN          (160) ///< Total channel symbols (FST4_ND + FST4_NS)
#define FST4_LENGTH_SYNC (8)   ///< Length of each sync group
#define FST4_NUM_SYNC    (5)   ///< Number of sync groups

// Define LDPC parameters for FST4 (240,101) code
#define FST4_LDPC_N          (240)                      ///< Codeword length
#define FST4_LDPC_K          (101)                      ///< Information bits (77 payload + 24 CRC)
#define FST4_LDPC_M          (139)                      ///< Parity bits (240 - 101)
#define FST4_LDPC_N_BYTES    ((FST4_LDPC_N + 7) / 8)   ///< Bytes for 240-bit codeword
#define FST4_LDPC_K_BYTES    ((FST4_LDPC_K + 7) / 8)   ///< Bytes for 101-bit payload+CRC
#define FST4_LDPC_MAX_NRW    (6)                        ///< Max bits per parity check

// Define LDPC parameters for FST4W (240,74) code
#define FST4W_LDPC_K         (74)                       ///< Information bits (50 payload + 24 CRC)
#define FST4W_LDPC_M         (166)                      ///< Parity bits (240 - 74)
#define FST4W_LDPC_K_BYTES   ((FST4W_LDPC_K + 7) / 8)  ///< Bytes for 74-bit payload+CRC
#define FST4W_LDPC_MAX_NRW   (5)                        ///< Max bits per parity check

// Define CRC parameters
#define FTX_CRC_POLYNOMIAL ((uint16_t)0x2757u) ///< CRC-14 polynomial without the leading (MSB) 1
#define FTX_CRC_WIDTH      (14)

// CRC-24 polynomial for FST4/FST4W (from WSJT-X lib/fst4/get_crc24.f90):
// p = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,0,1,1,0,1,1}
// = 0x100065B (25 bits with leading 1)
#define FST4_CRC_POLYNOMIAL ((uint32_t)0x00065Bu) ///< CRC-24 polynomial without leading 1
#define FST4_CRC_WIDTH      (24)

typedef enum
{
    FTX_PROTOCOL_FT4,
    FTX_PROTOCOL_FT8,
    FTX_PROTOCOL_FST4,
    FTX_PROTOCOL_FST4W
} ftx_protocol_t;

/// Costas 7x7 tone pattern for synchronization
extern const uint8_t kFT8_Costas_pattern[7];
extern const uint8_t kFT4_Costas_pattern[4][4];

/// FST4 sync word patterns (two alternating 8-tone patterns)
extern const uint8_t kFST4_Sync_word1[FST4_LENGTH_SYNC];
extern const uint8_t kFST4_Sync_word2[FST4_LENGTH_SYNC];

/// Gray code map to encode 8 symbols (tones)
extern const uint8_t kFT8_Gray_map[8];
extern const uint8_t kFT4_Gray_map[4];

extern const uint8_t kFT4_XOR_sequence[10];
// Note: FST4 uses the same XOR sequence as FT4 (rvec in genfst4.f90 == kFT4_XOR_sequence)

/// NSPS (samples per symbol at 12000 Hz) for each FST4 T/R period
/// Index: 0=15s, 1=30s, 2=60s, 3=120s, 4=300s, 5=900s, 6=1800s
extern const int kFST4_NSPS[7];
/// Valid T/R periods in seconds
extern const int kFST4_TR_periods[7];

/// Number of valid FST4 T/R periods
#define FST4_NUM_TR_PERIODS (7)

/// Parity generator matrix for (174,91) LDPC code, stored in bitpacked format (MSB first)
extern const uint8_t kFTX_LDPC_generator[FTX_LDPC_M][FTX_LDPC_K_BYTES];

/// LDPC(174,91) parity check matrix, containing 83 rows,
/// each row describes one parity check,
/// each number is an index into the codeword (1-origin).
/// The codeword bits mentioned in each row must xor to zero.
/// From WSJT-X's ldpc_174_91_c_reordered_parity.f90.
extern const uint8_t kFTX_LDPC_Nm[FTX_LDPC_M][7];

/// Mn from WSJT-X's bpdecode174.f90. Each row corresponds to a codeword bit.
/// The numbers indicate which three parity checks (rows in Nm) refer to the codeword bit.
/// The numbers use 1 as the origin (first entry).
extern const uint8_t kFTX_LDPC_Mn[FTX_LDPC_N][3];

/// Number of rows (columns in C/C++) in the array Nm.
extern const uint8_t kFTX_LDPC_Num_rows[FTX_LDPC_M];

// gen_sys[174 rows][91 cols] has a row per each of the 174 codeword bits,
// indicating how to generate it by xor with each of the 91 plain bits.
extern const uint8_t gen_sys[FTX_LDPC_M][FTX_LDPC_K];

/// FST4 (240,101) LDPC generator matrix, bitpacked (MSB first)
extern const uint8_t kFST4_LDPC_generator[FST4_LDPC_M][FST4_LDPC_K_BYTES];
/// FST4 (240,101) parity check: which bits participate in each check (0-indexed)
extern const uint16_t kFST4_LDPC_Nm[FST4_LDPC_M][FST4_LDPC_MAX_NRW];
/// FST4 (240,101): which checks each bit participates in (0-indexed)
extern const uint16_t kFST4_LDPC_Mn[FST4_LDPC_N][3];
/// FST4 (240,101): number of bits in each parity check row
extern const uint8_t kFST4_LDPC_num_rows[FST4_LDPC_M];

/// FST4W (240,74) LDPC generator matrix, bitpacked (MSB first)
extern const uint8_t kFST4W_LDPC_generator[FST4W_LDPC_M][FST4W_LDPC_K_BYTES];
/// FST4W (240,74) parity check: which bits participate in each check (0-indexed)
extern const uint16_t kFST4W_LDPC_Nm[FST4W_LDPC_M][FST4W_LDPC_MAX_NRW];
/// FST4W (240,74): which checks each bit participates in (0-indexed)
extern const uint16_t kFST4W_LDPC_Mn[FST4_LDPC_N][3];
/// FST4W (240,74): number of bits in each parity check row
extern const uint8_t kFST4W_LDPC_num_rows[FST4W_LDPC_M];

#ifdef __cplusplus
}
#endif

#endif // _INCLUDE_CONSTANTS_H_
