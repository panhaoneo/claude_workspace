#pragma once
/**
 * @file xtp_types.hpp
 * @brief XTP 行情数据结构存根定义
 *
 * 生产环境请替换为实际 XTP SDK 头文件（xquote_api_struct.h）。
 * 本文件的结构尺寸与字段布局与标准 XTP Level-2 结构兼容。
 */
#include <cstdint>

enum XTP_EXCHANGE_TYPE : uint32_t {
    XTP_EXCHANGE_SH = 1,  ///< 上海证券交易所
    XTP_EXCHANGE_SZ = 2,  ///< 深圳证券交易所
};

/**
 * @brief Level-2 快照行情（5 档位）
 *
 * 内存布局（256 字节，4 个 cache line）：
 *   [  0] ticker[16]
 *   [ 16] exchange(4) _pad0(4)
 *   [ 24] data_time(8)
 *   [ 32] last/open/high/low/pre_close(5×8=40)
 *   [ 72] total_volume(8) total_turnover(8)
 *   [ 88] bid_price[5](40) bid_qty[5](40) ask_price[5](40) ask_qty[5](40)
 *   [248] status(4) _pad1(4)
 *   [256] end
 */
struct XQUOTE_MARKET_DATA {
    char     ticker[16];        ///< 证券代码（如 "600519\0"）
    uint32_t exchange;          ///< XTP_EXCHANGE_TYPE
    uint8_t  _pad0[4];
    int64_t  data_time;         ///< YYYYMMDDHHMMSSmmm
    double   last_price;
    double   open_price;
    double   high_price;
    double   low_price;
    double   pre_close_price;
    int64_t  total_volume;      ///< 累计成交量（手）
    double   total_turnover;    ///< 累计成交金额（元）
    double   bid_price[5];
    int64_t  bid_qty[5];
    double   ask_price[5];
    int64_t  ask_qty[5];
    int32_t  status;            ///< 交易状态
    uint8_t  _pad1[4];
};
static_assert(sizeof(XQUOTE_MARKET_DATA) == 256,
              "XQUOTE_MARKET_DATA must be exactly 256 bytes");

/**
 * @brief 逐笔成交（88 字节）
 *
 * 内存布局：
 *   [  0] ticker[16]
 *   [ 16] exchange(4) _pad0(4)
 *   [ 24] data_time(8) seq(8) price(8) quantity(8) turnover(8)
 *   [ 64] buy_order_seq(8) sell_order_seq(8)
 *   [ 80] trade_type(1) side(1) _pad1(6)
 *   [ 88] end
 */
struct XQUOTE_TRANSACTION {
    char     ticker[16];
    uint32_t exchange;
    uint8_t  _pad0[4];
    int64_t  data_time;         ///< YYYYMMDDHHMMSSmmm
    int64_t  seq;               ///< 通道内唯一序号，从 1 递增
    double   price;
    int64_t  quantity;          ///< 手
    double   turnover;          ///< 元
    int64_t  buy_order_seq;     ///< 对应买委托序号
    int64_t  sell_order_seq;    ///< 对应卖委托序号
    uint8_t  trade_type;        ///< 0=撮合成交
    uint8_t  side;              ///< 'B'=买方主动 'S'=卖方主动
    uint8_t  _pad1[6];
};
static_assert(sizeof(XQUOTE_TRANSACTION) == 88,
              "XQUOTE_TRANSACTION must be exactly 88 bytes");

/**
 * @brief 逐笔委托（64 字节）
 *
 * 内存布局：
 *   [  0] ticker[16]
 *   [ 16] exchange(4) _pad0(4)
 *   [ 24] data_time(8) seq(8) price(8) quantity(8)
 *   [ 56] side(1) order_type(1) _pad1(6)
 *   [ 64] end
 */
struct XQUOTE_ORDER {
    char     ticker[16];
    uint32_t exchange;
    uint8_t  _pad0[4];
    int64_t  data_time;         ///< YYYYMMDDHHMMSSmmm
    int64_t  seq;               ///< 通道内唯一序号，从 1 递增
    double   price;             ///< 0 表示市价
    int64_t  quantity;          ///< 手
    uint8_t  side;              ///< 'B'=买 'S'=卖
    uint8_t  order_type;        ///< 0=限价 1=市价
    uint8_t  _pad1[6];
};
static_assert(sizeof(XQUOTE_ORDER) == 64,
              "XQUOTE_ORDER must be exactly 64 bytes");
