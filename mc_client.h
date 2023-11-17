#ifndef MC_CLIENT_H_
#define MC_CLIENT_H_

#include <stdio.h>
#include <unistd.h>
#include <memory.h>
#include <stdlib.h>
#include <string>

typedef unsigned char uint8;   //8位无符号整数
typedef unsigned short uint16; //16位无符号整数
typedef unsigned int uint32;   //32位无符号整数

#define FIELD_VALUE_BIT 0x3FFFFFF

/************* 协议定义 开始 *************/
///<  消息类型定义
#define	PACKAGE_INSTRUMENT_IDX		    0x05    ///< 合约索引
#define	PACKAGE_INSTRUMENT_INIT		    0x06    ///< 初始行情
#define	PACKAGE_INSTRUMENT		        0x10    ///< 单腿行情
#define	PACKAGE_CMBTYPE	    		    0x11    ///< 组合行情
#define	PACKAGE_BULLETINE				0x12    ///< 交易所告示
#define	PACKAGE_QUOT_REQ		        0x13    ///< 报价请求
#define	PACKAGE_TRADE_STATUS			0x14    ///< 交易状态
#define	PACKAGE_DEPTH       			0x20    ///< 深度行情

#define PKG_HEAD_LEN 4   ///< 报文头长度
#define MSG_HEAD_LEN 2   ///< 消息头长度

#pragma pack(push, 1)

// 报文头
typedef struct
{
    uint8 msg_type;   ///< 报文的类型，0x01-0xff
    uint8 msg_num;    ///< 报文中消息个数，最大为255
    uint16 pkg_len;   ///< 报文的长度，不包含4字节报头，高字节在前
    char pkg_data[0]; ///< 数据正文
} pkg_head_t;         ///< 报文头

// 消息头
typedef struct
{
    uint16 msg_len;   ///< 消息长度，不包含2字节消息头，高字节在前
    char msg_data[0]; ///< 消息正文
} msg_head_t;         ///< 消息头

#pragma pack(pop)

/************* 协议定义 结束 *************/


/**
 * @brief 组播接收客户端
 */
class mc_client_t
{
public:
    /**
     * @brief 构造函数
     *
     * @param mc_ip 组播组IP
     * @param mc_port 组播组端口号
     */
    mc_client_t(std::string mc_ip, unsigned int mc_port);

    /**
     * @brief 析构函数
     */
    ~mc_client_t();

    /**
     * @brief 初始化组播客户端
     *
     * @param bind_if 本地绑定IP
     * @param recv_buf_len 接收缓冲区大小，默认2048Kb
     *
     * @return 0：初始化成功；其他：错误码
     */
    int init(const char *bind_if, const int recv_buf_len = 2048);

    /**
     * @brief 循环接收组播消息
     */
    void loop();

/****** 报文处理函数 ******/
private:
    /**
     * @brief 处理收到的组播数据
     *
     * @param buf 组播数据指针
     * @param len 收到的数据长度
     *
     * @return 0：处理成功；-1：处理失败
     */
    int process_data(const char* buf, int len);

    /**
     * @brief 处理合约索引消息
     *
     * @param p_data msg数据指针
     * @param msg_len msg长度
     * @param msg_idx 当前处理消息在包内位置
     */
    void on_instrument_idx(const char *p_data, uint16 msg_len, int msg_idx);

    /**
     * @brief 处理初始行情消息
     *
     * @param p_data msg数据指针
     * @param msg_len msg长度
     */
    void on_instrument_init(const char *p_data, uint16 msg_len);

    /**
     * @brief 处理单腿行情消息
     *
     * @param p_data msg数据指针
     * @param msg_len msg长度
     */
    void on_instrument(const char *p_data, uint16 msg_len);

    /**
     * @brief 处理组合行情消息
     *
     * @param p_data msg数据指针
     * @param msg_len msg长度
     */
    void on_cmbtype(const char *p_data, uint16 msg_len);

    /**
     * @brief 处理交易所告示消息
     *
     * @param p_data msg数据指针
     */
    void on_bulletine(const char *p_data);

    /**
     * @brief 处理做市商报价请求消息
     *
     * @param p_data msg数据指针
     * @param msg_len msg长度
     */
    void on_quot_req(const char *p_data);

    /**
     * @brief 处理交易系统状态消息
     *
     * @param p_data msg数据指针
     * @param msg_len msg长度
     */
    void on_trade_status(const char *p_data);

    /**
     * @brief 处理深度行情消息
     *
     * @param p_data msg数据指针
     * @param msg_len msg长度
     */
    void on_depth(const char* p_data, uint16 msg_len);

/****** 工具函数 ******/
private:
    /**
     * @brief 调整字节序获取uint16数值
     *
     * @param tbuf 数据指针
     *
     * @return uint16数值
     */
    uint16 read_uint16(const char *tbuf);

    /**
     * @brief 调整字节序获取uint32数值
     *
     * @param tbuf 数据指针
     *
     * @return uint32数值
     */
    uint32 read_uint32(const char *tbuf);

    /**
     * @brief 从给定buf中获取item索引及值
     *
     * @param p_buf 数据指针
     * @param fld_idx 字段索引的引用
     * @param value 字段对应数值
     *
     * @return 数据长度
     */
    int get_int_value(const char* p_buf, int &fld_idx, int &value);

    /**
     * @brief 从buf中解析深度行情数值
     *
     * @param p_buf 数据指针
     * @param fld_idx 字段索引的引用
     * @param price 价格的引用
     * @param qty 定单手数的引用
     * @param ord_cnt 定单个数的引用
     *
     * @return 数据长度
     */
    int get_dep_orderbook(const char* p_buf, int &fld_idx, int &price, int &qty, int &ord_cnt);

private:
    std::string m_mc_ip;     ///<组播IP地址
    unsigned int m_mc_port;  ///<组播端口号
    int m_mc_fd;             ///<组播socket文件描述符
    char m_recv_buf[4096];   ///<接收缓冲区
};

#endif