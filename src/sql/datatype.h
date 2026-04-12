#ifndef DATATYPE_H_INCLUDED // include guard
#define DATATYPE_H_INCLUDED

#include <stdint.h>
#include <stdio.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef struct _ParseVarintResult {
    u64 value;
    u8 byte_span;
} ParseVarintResult;

/*
 * Parses varint as described by sqlite docs, given a file pointer with pointer set to position where varint needs to be derived.
 * Varints are 1 - 9 bytes. For the first 8 bytes Most Significant Bit of each byte is either 1 or 0, where 1 signals that sequence is not finished.
 * 0 signals the end of the sequence. The 9th byte uses all 8 bits. Max int covered is 8bytes.
 *
 * Note: This parses with the assumption that bytes are arranged in Big-Endian order.
 */
ParseVarintResult parseVarint(FILE* db_file);

/*
 * Parse Big-Endian stored bytes into a 64bit int, based on given size in bytes.
 * Reverses byte order and gets int.
 */
s64 parseSqlInt(u8* big_end_bytes, u8 num_bytes);

/*
 * Returns content size of the column, given the serial type of the record column.
 *
 * See: https://www.sqlite.org/fileformat.html#record_format
 */
u64 getRecordSerialTypeSize(u64 serial_type);


#endif // include guard end
