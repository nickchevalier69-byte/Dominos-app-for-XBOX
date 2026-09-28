/* Johnamos - order a pizza from the original Xbox. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifndef NATIVE_TEST
#include <hal/debug.h>
#include <windows.h>
#endif
#include "app.h"
#include "net.h"

#ifdef NATIVE_TEST
extern char net_test_ip[24];
extern int net_test_port;
#endif

int main(void)
{
    int rc;
#ifdef NATIVE_TEST
    if (getenv("NET_TEST_PORT")) {
        strcpy(net_test_ip, "127.0.0.1");
        net_test_port = atoi(getenv("NET_TEST_PORT"));
    }
#endif
    rc = app_run();
#ifndef NATIVE_TEST
    if (rc != 0) {
        debugPrint("Johnamos could not start (error %d).\nIs vegur-regular.ttf next to default.xbe?\n", rc);
        Sleep(10000);
    }
#endif
    return rc;
}
