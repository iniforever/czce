#include "mc_client.h"
#include <string>

char localbindip1[] = "1.1.1.223";      //本地接收行情网卡设备的ip
char localbindip2[] = "1.1.1.224";      //本地接收行情网卡设备的ip

int main()
{

    char mc_ip1[100] = "239.26.1.1";   //组播地址
    unsigned int mc_port1 = 23001;  //组播端口

    char mc_ip2[100] = "239.27.1.1";   //组播地址
    unsigned int mc_port2 = 23005;  //组播端口

    printf("----------------------------------------\n");
    printf("level 1 multicast group ip: %s\n", mc_ip1);
    printf("level 1 multicast group port: %d\n", mc_port1);
    printf("----------------------------------------\n");

    printf("----------------------------------------\n");
    printf("level 2 multicast group ip: %s\n", mc_ip2);
    printf("level 2 multicast group port: %d\n", mc_port2);
    printf("----------------------------------------\n");


    mc_client_t client1(mc_ip1, mc_port1);
    mc_client_t client2(mc_ip2, mc_port2);

    if (client1.init(localbindip1) == -1)  //允许从所有网卡接收数据
    {
        return -1;
    }

    if (client2.init(localbindip2) == -1)  //允许从所有网卡接收数据
    {
        return -2;
    }

    printf("Receiving market data...\n");
    client1.loop();
    client2.loop();

    return 0;
}
