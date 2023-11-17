#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include "mc_client.h"

mc_client_t::mc_client_t(std::string mc_ip, unsigned int mc_port)
{
    m_mc_ip = mc_ip;
    m_mc_port = mc_port;

    m_mc_fd = -1;
}

mc_client_t::~mc_client_t()
{
    if (m_mc_fd > 0)
        close(m_mc_fd);
}

int mc_client_t::init(const char *bind_if, const int recv_buf_len)
{
    if (-1 != m_mc_fd)
    {
        printf("socket has been created!\n");
        return -1;
    }

    m_mc_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (m_mc_fd < 0)
    {
        perror("create multicast socket"); // Use perror for more informative error messages
        return -1;
    }
    printf("Socket created with fd: %d\n", m_mc_fd);

    struct sockaddr_in mc_addr;
    memset(&mc_addr, 0, sizeof(mc_addr));
    mc_addr.sin_family = AF_INET;
    mc_addr.sin_addr.s_addr = inet_addr(m_mc_ip.c_str());
    mc_addr.sin_port = htons(m_mc_port);

    int bind_res = ::bind(m_mc_fd, (struct sockaddr*)&mc_addr, sizeof(mc_addr));
    if (bind_res < 0)
    {
        perror("bind ip"); 
        close(m_mc_fd);
        return -2;
    }
    printf("Bind successful. Return value: %d\n", bind_res);
    printf("Bound to IP: %s, Port: %u\n", inet_ntoa(mc_addr.sin_addr), ntohs(mc_addr.sin_port));

    if (recv_buf_len > 0)
    {
        int optval = recv_buf_len * 1024;
        int set_res = setsockopt(m_mc_fd, SOL_SOCKET, SO_RCVBUF, &optval, sizeof(optval));
        if (set_res == -1)
        {
            perror("set socket buffer");
            close(m_mc_fd);
            return -3;
        }
        printf("Socket buffer set successfully. Return value: %d\n", set_res);
    }

    struct ip_mreq mc_req;
    mc_req.imr_multiaddr.s_addr = inet_addr(m_mc_ip.c_str());
    mc_req.imr_interface.s_addr = inet_addr(bind_if);

    int setopt_res = ::setsockopt(m_mc_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mc_req, sizeof(mc_req));
    if (setopt_res < 0)
    {
        perror("join multicast group");
        close(m_mc_fd);
        return -4;
    }
    printf("Joined multicast group successfully. Return value: %d\n", setopt_res);

    struct timeval timeout;
    timeout.tv_sec = 5;  // Set the timeout to 5 seconds.
    timeout.tv_usec = 0;
    if (setsockopt(m_mc_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1)
    {
        printf("set socket timeout failed!\n");
        close(m_mc_fd);
        return -5;  // Return an error code specific to this failure.
    }

    printf("Binding to interface IP: %s\n", bind_if);

    return m_mc_fd;
}

void mc_client_t::loop()
{
    int recv_len = 0;
    int buf_size = sizeof(m_recv_buf);
    printf("buf_size = %d\n", buf_size);
    struct sockaddr_in sender_addr;
    socklen_t addr_len = sizeof(sender_addr);

    while(true)
    {
        memset(&sender_addr, 0, sizeof(sender_addr)); // Clear the sender address structure
        
        recv_len = ::recvfrom(m_mc_fd, &m_recv_buf, buf_size, 0, (struct sockaddr*)&sender_addr, &addr_len);
        if (recv_len < 0)
        {
            int err = errno; // Capture the error number
            if (err == EWOULDBLOCK || err == EAGAIN) 
            {
                printf("recvfrom() timed out.\n");
                continue;  // Try again.
            }
            printf("recv data from multicast group failed! Error no: %d, Message: %s\n", err, strerror(err));
            continue;
        }
        else if(recv_len == 0)
        {
            printf("Received empty packet or the sender performed an orderly shutdown.\n");
            continue;
        }

        char sender_ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(sender_addr.sin_addr), sender_ip_str, INET_ADDRSTRLEN);
        printf("Received packet from %s:%d of length %d\n", sender_ip_str, ntohs(sender_addr.sin_port), recv_len);

        process_data(m_recv_buf, recv_len);

}
}

int mc_client_t::process_data(const char* buf, int len)
{
    int offset = 0;
    while(offset < len)  //可能存在一个UDP包中含有多个数据包
    {
        pkg_head_t *p_head = (pkg_head_t*)(buf + offset);
        printf("------------------ 报文类型：0x%02x ------------------\n", p_head->msg_type);

        int len = 0;  // 记录当前已处理的数据长度
        int pkg_len = read_uint16((char*)&p_head->pkg_len);
        for (size_t i = 0; i < p_head->msg_num && len < pkg_len; i++)
        {
            msg_head_t *p_msg_head = (msg_head_t *)(p_head->pkg_data + len);
            uint16 msg_len = read_uint16((char*)&p_msg_head->msg_len) - MSG_HEAD_LEN;  //减去消息头长度

            char *p_data = p_msg_head->msg_data;

            switch (p_head->msg_type)
            {
            case PACKAGE_INSTRUMENT_IDX:   //合约索引信息消息
                on_instrument_idx(p_data, msg_len, i);
                break;
            case PACKAGE_INSTRUMENT_INIT:  //初始行情消息
                on_instrument_init(p_data, msg_len);
                break;
            case PACKAGE_INSTRUMENT:       //单腿行情消息
                on_instrument(p_data, msg_len);
                break;
            case PACKAGE_CMBTYPE:          //组合行情消息
                on_cmbtype(p_data, msg_len);
                break;
            case PACKAGE_BULLETINE:        //交易所告示消息
                on_bulletine(p_data);
                break;
            case PACKAGE_QUOT_REQ:         //做市商报价请求消息
                on_quot_req(p_data);
                break;
            case PACKAGE_TRADE_STATUS:     //交易系统状态消息
                on_trade_status(p_data);
                break;
            case PACKAGE_DEPTH:     //交易系统状态消息
                on_depth(p_data, msg_len);
                break;
            default:
                printf("unknown package type: 0x%02x\n", p_head->msg_type);
                return -1;
            }

            len += msg_len + MSG_HEAD_LEN;  //完成一个msg的解析
            printf("------------------ message %lu end ------------------\n", i + 1);
        }

        offset += pkg_len + PKG_HEAD_LEN;  //完成一个pkg的解析
    }

    return 0;
}

void mc_client_t::on_instrument_idx(const char *p_data, uint16 msg_len, int msg_idx)
{
    int data_pos = 0;

    // 交易日（仅在第一个msg中存在）
    if (msg_idx == 0)
    {
        uint32 trade_date = read_uint32(&p_data[data_pos]);
        data_pos += 4;
        printf("trade date = %u\n", trade_date);
    }

    // 合约类型
    uint8 type = p_data[data_pos];
    data_pos += 1;
    printf("Type = %d\n", type);

    // 合约索引
    uint16 ins_idx = read_uint16(&p_data[data_pos]);
    data_pos += 2;
    printf("Index = %u\n", ins_idx);

    // 合约编码
    char instrument_id[20] = {0};
    memcpy(instrument_id, &p_data[data_pos], msg_idx == 0 ? (msg_len - 7) : (msg_len - 3));
    printf("InstrumentId = %s\n", instrument_id);
}

void mc_client_t::on_instrument_init(const char *p_data, uint16 msg_len)
{
    int data_pos = 0;

    // 价格精度
    uint16 price_size = read_uint16(&p_data[data_pos]);
    data_pos += 2;
    printf("price_size = %u\n", price_size);

    // 合约索引
    uint16 ins_idx = read_uint16(&p_data[data_pos]);
    data_pos += 2;
    printf("ins_idx = %u\n", ins_idx);

    while (data_pos < msg_len)
    {
        int fld_idx = 0;
        int value = 0;
        data_pos += get_int_value(&p_data[data_pos], fld_idx, value);

        switch (fld_idx)
        {
        case 1:  // 昨收盘价
            printf("last close price = %f\n", (double)value / price_size);
            break;
        case 2:  // 昨结算价
            printf("last clear price = %f\n", (double)value / price_size);
            break;
        case 3:  // 昨持仓
            printf("last holding = %d\n", value);
            break;
        case 4:  // 涨停价
            printf("limit up price = %f\n", (double)value / price_size);
            break;
        case 5:  // 跌停价
            printf("limit down price = %f\n", (double)value / price_size);
            break;
        default:
            printf("item: %d, value: %d\n", fld_idx, value);
            break;
        }
    }
}

void mc_client_t::on_instrument(const char *p_data, uint16 msg_len)
{
    int data_pos = 0;
    long long trade_val_part1 = 0;  //long long保证左移26位时不溢出
    long long trade_val_part2 = 0;

    // 价格精度
    uint16 price_size = read_uint16(&p_data[data_pos]);
    data_pos += 2;
    printf("price_size = %u\n", price_size);

    // 合约索引
    uint16 ins_idx = read_uint16(&p_data[data_pos]);
    data_pos += 2;
    printf("ins_idx = %u\n", ins_idx);

    while (data_pos < msg_len)
    {
        int fld_idx = 0;
        int value = 0;
        data_pos += get_int_value(&p_data[data_pos], fld_idx, value);

        switch (fld_idx)
        {
        case 1:  // 开盘价
            printf("open price = %f\n", (double)value / price_size);
            break;
        case 2:  // 最高价
            printf("high price = %f\n", (double)value / price_size);
            break;
        case 3:  // 最低价
            printf("low price = %f\n", (double)value / price_size);
            break;
        case 4:  // 最新价
            printf("last price = %f\n", (double)value / price_size);
            break;
        case 9:  // 成交量
            printf("volume = %d\n", value);
            break;
        case 16:  // 秒级时间戳
            printf("time sec = %06d\n", value);
            break;
        case 18:  // 新增：微秒级时间戳
            printf("time usec = %06d\n", value);
            break;
        case 19:  // 新增：总成交金额(part1)
            trade_val_part1 = value;
            printf("trade value(part1) = %d\n", value);
            break;
        case 20:  // 新增：总成交金额(part2)
            trade_val_part2 = value;
            printf("trade value(part2) = %d\n", value);
            break;
        case 21:  // 新增：历史最高价
            printf("life high price = %f\n", (double)value / price_size);
            break;
        case 22:  // 新增：历史最低价
            printf("life low price = %f\n", (double)value / price_size);
            break;
        default:
            printf("item: %d, value: %d\n", fld_idx, value);
            break;
        }
    }

    // 总成交金额
    if (trade_val_part1 != 0 || trade_val_part2 != 0)
    {
        double trade_val_sum = (double)((trade_val_part1 << 26) | trade_val_part2) / price_size;
        printf("trade value(sum) = %lf\n", trade_val_sum);
    }
}

void mc_client_t::on_cmbtype(const char *p_data, uint16 msg_len)
{
    int data_pos = 0;

    // 价格精度
    uint16 price_size = read_uint16(&p_data[data_pos]);
    data_pos += 2;
    printf("price_size = %u\n", price_size);

    // 合约索引
    uint16 ins_idx = read_uint16(&p_data[data_pos]);
    data_pos += 2;
    printf("ins_idx = %u\n", ins_idx);

    while (data_pos < msg_len)
    {
        int fld_idx = 0;
        int value = 0;
        data_pos += get_int_value(&p_data[data_pos], fld_idx, value);

        switch (fld_idx)
        {
        case 1:  // 买价
            printf("bid price = %f\n", (double)value / price_size);
            break;
        case 2:  // 卖价
            printf("ask price = %f\n", (double)value / price_size);
            break;
        case 3:  // 买量
            printf("bid lot = %d\n", value);
            break;
        case 4:  // 卖量
            printf("ask lot = %d\n", value);
            break;
        case 7: //新增：秒级时间戳
            printf("time sec = %d\n", value);
            break;
        case 8: //新增：微秒级时间戳
            printf("time usec = %d\n", value);
            break;
        default:
            printf("item: %d, value: %d\n", fld_idx, value);
            break;
        }
    }
}

void mc_client_t::on_bulletine(const char *p_data)
{
    int data_pos = 0;

    // 广播消息头
    data_pos += 2;

    // 广播消息序号
    uint16 msg_seq = read_uint16(&p_data[data_pos]);
    data_pos += 2;
    printf("bulletine seq = %u\n", msg_seq);

    // 广播内容
    printf("bulletine context: %s\n", &p_data[data_pos]);
}

void mc_client_t::on_quot_req(const char *p_data)
{
    int data_pos = 0;

    // 询价消息头
    data_pos += 2;

    // 询价合约索引
    uint16 ins_idx = read_uint16(&p_data[data_pos]);
    data_pos += 2;
    printf("Index = %u\n", ins_idx);

    int fld_idx = 0;
    int value = 0;

    // 当前交易日期
    data_pos += get_int_value(&p_data[data_pos], fld_idx, value);
    printf("trade date = %d\n", value);

    // 询价号
    data_pos += get_int_value(&p_data[data_pos], fld_idx, value);
    printf("req no = %d\n", value);

    // 询价方向（0-买；1-卖；2-其他）
    uint8 direction = p_data[data_pos];
    printf("direction = %d\n", direction);
    data_pos += 1;

    // 询价来源（0-会员；1-交易所）
    uint8 request_by = p_data[data_pos];
    printf("request_by = %d\n", request_by);
    data_pos += 1;
}

void mc_client_t::on_trade_status(const char *p_data)
{
    // 交易状态
    uint8 trade_status = p_data[0];
    printf("trade status = %d\n", trade_status);
}


void mc_client_t::on_depth(const char* p_data, uint16 msg_len)
{
    int data_pos = 0;

    // 价格精度
    uint16 price_size = read_uint16(&p_data[data_pos]);
    data_pos += 2;
    printf("price_size = %u\n", price_size);

    // 合约索引
    uint16 ins_idx = read_uint16(&p_data[data_pos]);
    data_pos += 2;
    printf("ins_idx = %u\n", ins_idx);


    while (data_pos < msg_len)
    {
        int fld_idx = 0;
        int price = 0;
        int qty = 0;
        int ord_cnt = 0;
        data_pos += get_dep_orderbook(&p_data[data_pos], fld_idx, price, qty, ord_cnt);

        switch (fld_idx)
        {
        case 1:  
            printf("BidDepth1 = %f BidSize1 = %d \n", (double)price / price_size, qty);
            break;
        case 2:  
            printf("AskDepth1 = %f %d\n", (double)price / price_size, qty);
            break;
        case 3:
            printf("BidDepth2 = %f %d\n", (double)price / price_size, qty);
            break;
        case 4:
            printf("AskDepth2 = %f %d \n", (double)price / price_size, qty);
            break;
        case 5:
            printf("BidDepth3 = %f\n", (double)price / price_size);
            break;
        case 6:
            printf("AskDepth3 = %f\n", (double)price / price_size);
            break;
        case 7:
            printf("BidDepth4 = %f\n", (double)price / price_size);
            break;
        case 8:
            printf("AskDepth4 = %f\n", (double)price / price_size);
            break;
        case 9:
            printf("BidDepth5 = %f\n", (double)price / price_size);
            break;
        case 10:
            printf("AskDepth5 = %f\n", (double)price / price_size);
            break;
        default:
            printf("item: %d, value: %d\n", fld_idx, price);
            break;
        }
    }
}



uint16 mc_client_t::read_uint16(const char *tbuf)
{
    unsigned char *buf = (unsigned char *)tbuf;
    return (buf[0] << 8) + buf[1];
}

uint32 mc_client_t::read_uint32(const char *tbuf)
{
    unsigned char*buf = (unsigned char*)tbuf;
    return (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];
}

int mc_client_t::get_int_value(const char* p_buf, int &fld_idx, int &value)
{
    uint32 temp = 0;
    memcpy(&temp, p_buf, 4);
    temp = ntohl(temp);

    int sign = temp >> 31;           // 符号位(B31)
    fld_idx = (temp >> 26) & 0x1F;   // item索引(B30-B26)

    value = temp & FIELD_VALUE_BIT;  // 数值部分(B25-B0)
    value *= (0 == sign ? 1 : -1);

    return 4;
}

int mc_client_t::get_dep_orderbook(const char* p_buf, int &fld_idx,
        int &price, int &qty, int &ord_cnt)
{
    // 获取价格(低位4字节的B0~B31)
    get_int_value(p_buf, fld_idx, price);

    // 获取委托量和订单个数（从高位4字节获取）
    uint32 temp = 0;
    memcpy(&temp, &p_buf[4], 4);
    temp = ntohl(temp);

    qty = temp >> 12;         // 委托量(B31-B12)
    ord_cnt = temp & 0x0FFF;  // 订单个数(B11-B0)

    return 8;
}
