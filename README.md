# ProtoFlow

轻量级嵌入式通信协议栈，面向 STM32 等资源受限 MCU。本版本已从单实例升级为多实例版本：每个通信接口持有独立的 `ParseContext`，可同时解析 UART1、UART2 或多条 SPI/I2C/CAN 接收链路，解析状态和数据不会互相污染。

## 特性

- 轻量级协议栈，代码体积小
- 动态长度数据包，默认最大载荷 256 字节
- 可配置帧头、帧尾，默认 `0xAA55` / `0x55AA`
- 可选 CRC16-CCITT-FALSE 校验
- 状态机驱动解析，共 10 种解析状态
- 逐字节解析，可在接收中断中直接调用
- 自动重同步，帧错误、长度异常或 CRC 错误后自动回到帧头搜索
- 支持多实例：每个通信接口使用独立的 `ParseContext`
- 通信底层可替换：UART、SPI、I2C、CAN 等，通过 `user_uart_transmit` 接入

## 文件结构

```text
ProtoFlow/
├── ProtoFlow/
│   ├── protoflow.h
│   └── protoflow.c
├── README.md
└── LICENSE
```

## 帧结构

| 字段 | 帧头 | 总长度 | 命令 | 数据 | CRC16 | 帧尾 |
|------|------|--------|------|------|-------|------|
| 字节数 | 2 | 2 | 1 | n | 2（可选） | 2 |
| 默认值 | 0xAA55 | 大端 | 用户定义 | 载荷 | 可选 | 0x55AA |

- 总长度字段 = 命令字（1 字节）+ 数据长度（n 字节）
- 多字节字段采用大端传输：高字节在前，低字节在后
- CRC16 采用 CRC-16/CCITT-FALSE：多项式 `0x1021`，初始值 `0xFFFF`
- CRC 计算范围从总长度字段开始，到数据结尾结束，不包含帧头、CRC 本身和帧尾
- 当 `USE_CRC16 = 0` 时，CRC16 字段不存在

## 配置

```c
// protoflow.h
#define FRAME_HEADER      0xAA55      // 帧头（2 字节）
#define FRAME_END         0x55AA      // 帧尾（2 字节）
#define MAX_DATA_LENGTH   256         // 最大数据载荷长度（字节）
#define USE_CRC16         0           // CRC16 校验：0-禁用，1-启用
```

`MAX_DATA_LENGTH` 是单个数据包中载荷的最大长度，不是整个帧的长度。每个 `ParseContext` 都会包含一块 `MAX_DATA_LENGTH` 大小的数据缓冲区，因此该值越大，每个实例占用的 RAM 越多。

## 快速开始

### 1. 添加源文件

将以下两个文件复制到工程目录：

```text
protoflow.h
protoflow.c
```

### 2. 实现用户函数

发送函数会把整包数据交给底层通信接口发送：

```c
void user_uart_transmit(uint8_t *data, uint16_t len)
{
    HAL_UART_Transmit(&huart1, data, len, 100);
}
```

收到完整数据包后，库会调用应用回调：

```c
void user_package_handler(uint8_t cmd, uint8_t *data, uint16_t len)
{
    // cmd    : 命令字
    // data   : 数据载荷
    // len    : 数据载荷长度
}
```

### 3. 单接口初始化与解析

```c
#include protoflow.h

ParseContext uart1_ctx;
uint8_t uart1_rx;

int main(void)
{
    MX_USART1_UART_Init();
    HAL_UART_Receive_IT(&huart1, &uart1_rx, 1);

    protoflow_init(&uart1_ctx);

    while (1) {
        // 主循环
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        parse_byte(&uart1_ctx, uart1_rx);
        HAL_UART_Receive_IT(&huart1, &uart1_rx, 1);
    }
}
```

## 多实例使用示例

一个接口定义一个 `ParseContext`，接收中断中必须把字节喂给对应实例：

```c
#include protoflow.h

ParseContext uart1_ctx;
ParseContext uart2_ctx;
uint8_t uart1_rx;
uint8_t uart2_rx;

void user_uart_transmit(uint8_t *data, uint16_t len)
{
    // 多发送口时，在此函数内按应用状态或目标通道路由到对应硬件
}

int main(void)
{
    MX_USART1_UART_Init();
    MX_USART2_UART_Init();

    protoflow_init(&uart1_ctx);
    protoflow_init(&uart2_ctx);

    HAL_UART_Receive_IT(&huart1, &uart1_rx, 1);
    HAL_UART_Receive_IT(&huart2, &uart2_rx, 1);

    while (1) {
        // 主循环
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        parse_byte(&uart1_ctx, uart1_rx);
        HAL_UART_Receive_IT(&huart1, &uart1_rx, 1);
    } else if (huart->Instance == USART2) {
        parse_byte(&uart2_ctx, uart2_rx);
        HAL_UART_Receive_IT(&huart2, &uart2_rx, 1);
    }
}
```

多实例模式下，UART1 和 UART2 各自拥有独立的帧头搜索状态、长度状态、CRC 状态和数据缓冲区，一个接口收到异常字节不会破坏另一个接口的解析过程。

## 数据发送

```c
uint8_t payload[4] = { 0x10, 0x20, 0x30, 0x40 };
uint16_t sent_len = pack_data_transmit(0x01, payload, sizeof(payload));
```

`pack_data_transmit()` 会完成以下工作：

1. 写入帧头
2. 写入总长度（高字节在前）
3. 写入命令字
4. 复制数据载荷
5. 根据 `USE_CRC16` 写入 CRC16
6. 写入帧尾
7. 调用 `user_uart_transmit()` 发送完整数据包

返回值是实际写入发送缓冲区的总字节数。`pack_data_transmit()` 内部使用静态发送缓冲区，发送完成前不要修改传入的 `data`；如果应用层可能从多个线程或中断同时发送，需要自行保证发送过程互斥。

## API 说明

### 库函数

```c
void protoflow_init(ParseContext *ctx);
```

初始化一个解析上下文。每个通信接口在开始接收前必须调用一次，并且传入自己的 `ParseContext`。

```c
void parse_byte(ParseContext *ctx, uint8_t byte);
```

把一个接收字节喂入指定实例的解析器。收到完整、合法的数据包后，自动调用 `user_package_handler()`。

```c
uint16_t pack_data_transmit(uint8_t cmd, uint8_t *data, uint16_t len);
```

打包并发送数据包。

### 用户必须实现的函数

```c
void user_uart_transmit(uint8_t *data, uint16_t len);
```

发送完整帧数据，底层可以是 UART、SPI、I2C 或其他通信接口。

```c
void user_package_handler(uint8_t cmd, uint8_t *data, uint16_t len);
```

处理收到的完整数据包。该回调在 `parse_byte()` 调用过程中同步触发，应保持简短，不要在回调中执行长延时或耗时任务。

## 单实例升级到多实例

旧版单实例 API 使用库内部全局上下文：

```c
protoflow_init();       // 旧版
parse_byte(byte);       // 旧版
```

多实例版本改为显式传入实例：

```c
protoflow_init(&ctx);   // 新版
parse_byte(&ctx, byte); // 新版
```

迁移步骤：

1. 删除旧版依赖的全局 `ctx` 定义
2. 为每个通信接口声明一个 `ParseContext`
3. 将旧 `protoflow_init()` 改为 `protoflow_init(&对应ctx)`
4. 将旧 `parse_byte(byte)` 改为 `parse_byte(&对应ctx, byte)`
5. 确认 `user_uart_transmit` 和 `user_package_handler` 仍由工程提供

## 移植到其他通信接口

`user_uart_transmit()` 只是库约定的底层发送入口，名称不限制实际通信类型。例如：

```c
// SPI
void user_uart_transmit(uint8_t *data, uint16_t len)
{
    HAL_SPI_Transmit(&hspi1, data, len, 100);
}

// I2C
void user_uart_transmit(uint8_t *data, uint16_t len)
{
    HAL_I2C_Master_Transmit(&hi2c1, DEVICE_ADDR, data, len, 100);
}
```

如果底层总线单次最大传输长度小于整包长度，例如 CAN 单帧只有 8 字节，则需要在上层自行完成分帧、重组或选择小于总线 MTU 的协议配置。

## 注意事项

- 一个通信接口一个 `ParseContext`，不能多个接口共用一个实例
- 发送端和接收端必须使用相同的帧头、帧尾、长度规则和 CRC 开关
- 接收字节可在中断中逐字节喂入 `parse_byte()`，函数本身不阻塞
- `user_package_handler()` 在解析流程中被同步调用，应避免阻塞
- 当前发送入口 `pack_data_transmit()` 不携带实例参数，多发送口场景需要在 `user_uart_transmit()` 内自行路由
- 当前回调签名不包含来源 `ParseContext`；若应用需要区分数据来自哪个接口，可在协议命令或应用层数据中加入接口标识

## 许可证

MIT License
