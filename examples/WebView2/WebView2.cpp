#if defined (_WIN32) || defined (_WIN64)
#include "MainThread.h"

//定义应用程序的入口点
#if defined (__MINGW32__) || defined (__MINGW64__)
//使用MinGW-w64编译
int APIENTRY WinMain(_In_ HINSTANCE /*hInstance*/,
                     _In_opt_ HINSTANCE /*hPrevInstance*/,
                     _In_ LPSTR    /*lpCmdLine*/,
                     _In_ int       /*nCmdShow*/)
#else
int APIENTRY wWinMain(_In_ HINSTANCE /*hInstance*/,
                      _In_opt_ HINSTANCE /*hPrevInstance*/,
                      _In_ LPWSTR    /*lpCmdLine*/,
                      _In_ int       /*nCmdShow*/)
#endif
{
    //创建主线程
    MainThread thread;

    //执行主线程消息循环
    thread.RunMessageLoop();

    //正常退出程序
    return 0;
}

#endif
