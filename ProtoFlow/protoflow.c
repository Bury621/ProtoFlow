/**
  ******************************************************************************
  * @file    protoflow.c
  * @brief   Serial communication protocol implementation
  ******************************************************************************
  */

#include "protoflow.h"
#include <string.h>

// CRC16-CCITT计算（多项式0x1021）
static uint16_t crc16(uint8_t *data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    while(len--) {
        crc ^= (uint16_t)(*data++) << 8;
        for(int i = 0; i < 8; i++) {
            if(crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

void protoflow_init(ParseContext *ctx) {
    memset(ctx, 0, sizeof(ParseContext));
    ctx->state = STATE_WAIT_HEADER1;
}

uint16_t pack_data_transmit(ParseContext *ctx, uint8_t cmd, uint8_t *data, uint16_t len) {
    static uint8_t buffer[MAX_DATA_LENGTH + 9];
    uint16_t index = 0;

    // 帧头（2字节）
    buffer[index++] = (FRAME_HEADER >> 8) & 0xFF;
    buffer[index++] = FRAME_HEADER & 0xFF;

    // 长度字段（2字节，数据长度 + CMD）
    uint16_t total_len = len + 1;
    buffer[index++] = (total_len >> 8) & 0xFF;
    buffer[index++] = total_len & 0xFF;

    // 命令（1字节）
    buffer[index++] = cmd;

    // 数据载荷（len字节）
    if (data && len > 0) {
        memcpy(&buffer[index], data, len);
        index += len;
    }

#if USE_CRC16
    uint16_t crc = crc16(&buffer[2], index - 2);
    buffer[index++] = (crc >> 8) & 0xFF;
    buffer[index++] = crc & 0xFF;
#endif

    // 帧尾（2字节）
    buffer[index++] = (FRAME_END >> 8) & 0xFF;
    buffer[index++] = FRAME_END & 0xFF;

    // 调用用户实现的发送函数，传入上下文
    user_uart_transmit(ctx, buffer, index);

    return index;
}

void parse_byte(ParseContext *ctx, uint8_t byte) {
    switch(ctx->state) {
    case STATE_WAIT_HEADER1:
        if(byte == ((FRAME_HEADER >> 8) & 0xFF)) {
            ctx->state = STATE_WAIT_HEADER2;
        }
        break;

    case STATE_WAIT_HEADER2:
        if(byte == (FRAME_HEADER & 0xFF)) {
            ctx->state = STATE_WAIT_LENGTH;
            ctx->calc_crc = 0xFFFF;
        } else {
            ctx->state = STATE_WAIT_HEADER1;
        }
        break;

    case STATE_WAIT_LENGTH:
        ctx->pkg_length = byte << 8;
        ctx->state = STATE_WAIT_LENGTH_LOW;
        break;

    case STATE_WAIT_LENGTH_LOW:
        ctx->pkg_length |= byte;
        if(ctx->pkg_length > MAX_DATA_LENGTH + 3) {
            ctx->state = STATE_WAIT_HEADER1;
            return;
        }
        ctx->state = STATE_WAIT_CMD;
        break;

    case STATE_WAIT_CMD:
        ctx->cmd = byte;
        ctx->data_index = 0;
        ctx->state = STATE_READ_DATA;
        break;

    case STATE_READ_DATA:
        ctx->data[ctx->data_index++] = byte;
        if(ctx->data_index >= ctx->pkg_length - 1) {
#if USE_CRC16
            ctx->state = STATE_WAIT_CRC1;
#else
            ctx->state = STATE_WAIT_END1;
#endif
        }
        break;

#if USE_CRC16
    case STATE_WAIT_CRC1:
        ctx->recv_crc = byte << 8;
        ctx->state = STATE_WAIT_CRC2;
        break;

    case STATE_WAIT_CRC2:
        ctx->recv_crc |= byte;
        if(ctx->calc_crc != ctx->recv_crc) {
            ctx->state = STATE_WAIT_HEADER1;
            return;
        }
        ctx->state = STATE_WAIT_END1;
        break;
#endif

    case STATE_WAIT_END1:
        if(byte == ((FRAME_END >> 8) & 0xFF)) {
            ctx->state = STATE_WAIT_END2;
        } else {
            ctx->state = STATE_WAIT_HEADER1;
        }
        break;

    case STATE_WAIT_END2:
        if(byte == (FRAME_END & 0xFF)) {
            // 完整数据包接收成功，把 ctx 传给回调
            user_package_handler(ctx, ctx->cmd, ctx->data, ctx->data_index);
        }
        ctx->state = STATE_WAIT_HEADER1;
        break;

    default:
        ctx->state = STATE_WAIT_HEADER1;
        break;
    }

#if USE_CRC16
    if(ctx->state >= STATE_WAIT_LENGTH && ctx->state <= STATE_READ_DATA) {
        ctx->calc_crc ^= (uint16_t)byte << 8;
        for(int i = 0; i < 8; i++) {
            if(ctx->calc_crc & 0x8000)
                ctx->calc_crc = (ctx->calc_crc << 1) ^ 0x1021;
            else
                ctx->calc_crc <<= 1;
        }
    }
#endif
}
