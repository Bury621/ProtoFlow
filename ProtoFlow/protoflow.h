/**
  ******************************************************************************
  * @file    protoflow.h
  * @brief   Serial communication protocol library 
  * @version V1.2.0
  ******************************************************************************
  */

#pragma once
#include <stdint.h>

// ==================== 用户可配置参数 ====================
#define FRAME_HEADER      0xAA55      // 帧头（2字节）
#define FRAME_END         0x55AA      // 帧尾（2字节）
#define MAX_DATA_LENGTH   256         // 最大数据长度
#define USE_CRC16         0           // CRC16校验（0-禁用 1-启用）

// ==================== 状态机状态定义 ====================
typedef enum {
    STATE_WAIT_HEADER1,
    STATE_WAIT_HEADER2,
    STATE_WAIT_LENGTH,
    STATE_WAIT_LENGTH_LOW,
    STATE_WAIT_CMD,
    STATE_READ_DATA,
    STATE_WAIT_CRC1,
    STATE_WAIT_CRC2,
    STATE_WAIT_END1,
    STATE_WAIT_END2
} ParseState;

// ==================== 解析上下文结构体 ====================
typedef struct {
    ParseState state;
    uint16_t data_index;
    uint16_t pkg_length;
    uint8_t cmd;
    uint8_t data[MAX_DATA_LENGTH];
    uint16_t calc_crc;
    uint16_t recv_crc;
} ParseContext;

// ==================== 类型定义 ====================
typedef enum {
    PKG_OK = 0,
    PKG_HEADER_ERR,
    PKG_LENGTH_ERR,
    PKG_CRC_ERR,
    PKG_END_ERR
} PkgStatus;

// ==================== 用户必须实现的函数 ====================
void user_uart_transmit(ParseContext *ctx, uint8_t *data, uint16_t len);
void user_package_handler(ParseContext *ctx, uint8_t cmd, uint8_t *data, uint16_t len);

// ==================== 库函数接口 ====================
void protoflow_init(ParseContext *ctx);
void parse_byte(ParseContext *ctx, uint8_t byte);
uint16_t pack_data_transmit(ParseContext *ctx, uint8_t cmd, uint8_t *data, uint16_t len);
