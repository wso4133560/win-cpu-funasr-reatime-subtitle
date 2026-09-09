#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

// Exercise the actual GUI executable even when its launcher requests a hidden
// window. Its isolated test instance performs no audio capture or model loading.
int wmain(int argc,wchar_t** argv){
    SetProcessDPIAware();
    if(argc!=2)return 1;
    if(std::wstring(argv[1])==L"--inspect-running"){
        wchar_t desktop[256]{};DWORD needed=0;
        GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,desktop,sizeof(desktop),&needed);
        std::wcout<<L"desktop="<<desktop<<L'\n';
        HWND window=FindWindowW(L"FunASRSubtitle",nullptr);
        DWORD pid=0;GetWindowThreadProcessId(window,&pid);
        RECT rect{};GetWindowRect(window,&rect);
        std::wcout<<L"found="<<(window!=nullptr)<<L" pid="<<pid<<L" visible="<<IsWindowVisible(window)
            <<L" rect="<<rect.left<<L','<<rect.top<<L','<<rect.right<<L','<<rect.bottom<<L'\n';
        return window?0:1;
    }
    auto session=std::to_wstring(GetCurrentProcessId());
    auto report=std::filesystem::absolute(L"overlay-test-"+session+L".txt");
    std::wstring command=L"\""+std::wstring(argv[1])+L"\" --test-window "+session+L" --test-report \""+report.wstring()+L"\"";
    STARTUPINFOW startup{sizeof(STARTUPINFOW)};
    startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process)){
        std::cerr<<"Could not launch GUI test: "<<GetLastError()<<'\n';return 1;
    }
    CloseHandle(process.hThread);
    DWORD code=1;
    if(WaitForSingleObject(process.hProcess,15000)==WAIT_OBJECT_0)GetExitCodeProcess(process.hProcess,&code);
    else {std::cerr<<"GUI test timed out\n";TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,2000);}
    CloseHandle(process.hProcess);
    std::ifstream result(report);std::cout<<result.rdbuf();
    return int(code);
}
