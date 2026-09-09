#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <mmsystem.h>
#include "pipeline.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <deque>
#include <string>
#include "resource.h"

namespace fs=std::filesystem;
using namespace Gdiplus;
constexpr UINT WM_TEXT_READY=WM_APP+1,WM_STATUS_READY=WM_APP+2,WM_TRAY=WM_APP+3,WM_REVEAL=WM_APP+4;
constexpr int Pause=100,Lock=101,Bigger=102,Smaller=103,Restart=104,Exit=105,Logs=106,Show=107,ThreadBase=200;
constexpr int thread_presets[]={1,2,4,8,12,16,24,32};
struct App {
    HWND window{};
    fs::path root;
    std::unique_ptr<Pipeline> pipeline;
    std::mutex mutex;
    std::deque<SubtitleEvent> pending;
    std::string pending_status;
    std::wstring current=L"Preparing speech recognition",previous,status=L"CPU · System audio";
    uint64_t current_id=0;
    bool locked=false,paused=false;
    int font_size=28,opacity=175,threads=8;
    NOTIFYICONDATAW tray{};
    std::atomic<bool> alive{true};
    std::wstring test_audio;
    bool testing=false;
    bool window_test=false,reactivated=false,last_render=false,frame_alpha=false;
    int test_stage=0,exit_code=0;
    HANDLE test_child=nullptr;
    ULONGLONG test_started=0;
    std::ofstream window_report;
} app;

std::wstring wide(const std::string& s){
    int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);
    std::wstring out(n,L' ');MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),out.data(),n);return out;
}
std::string utf8(const std::wstring& s){
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
    std::string out(n,' ');WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);return out;
}
fs::path find_root(){
    wchar_t module[32768];GetModuleFileNameW(nullptr,module,32768);
    fs::path p=fs::path(module).parent_path();
    for(int i=0;i<5;++i){if(fs::exists(p/L"models"/L"sensevoice-small-q8.gguf"))return p;p=p.parent_path();}
    return fs::path(module).parent_path();
}
void paint(Bitmap& bitmap){
    const int w=bitmap.GetWidth(),h=bitmap.GetHeight();
    Graphics g(&bitmap);g.Clear(Color(0,0,0,0));
    g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    GraphicsPath panel;
    constexpr REAL r=24;
    panel.AddArc(0.f,0.f,r,r,180.f,90.f);panel.AddArc(REAL(w)-r,0.f,r,r,270.f,90.f);
    panel.AddArc(REAL(w)-r,REAL(h)-r,r,r,0.f,90.f);panel.AddArc(0.f,REAL(h)-r,r,r,90.f,90.f);panel.CloseFigure();
    SolidBrush background(Color(BYTE(app.opacity),16,21,29));g.FillPath(&background,&panel);
    SolidBrush accent(app.paused?Color(255,231,184,90):Color(255,79,215,172));g.FillEllipse(&accent,24,22,8,8);
    Font foot_font(L"Segoe UI",11,FontStyleRegular,UnitPixel);
    Font label(L"Segoe UI",14,FontStyleRegular,UnitPixel);
    SolidBrush secondary(Color(255,180,193,210));
    RectF header(42,15,REAL(w-66),24);StringFormat left;
    g.DrawString(app.status.c_str(),-1,&label,header,&left,&secondary);
    Font main_font(L"Segoe UI",REAL(app.font_size),FontStyleBold,UnitPixel);
    SolidBrush old(Color(255,169,181,198)),white(Color(255,250,251,253));
    StringFormat format;format.SetAlignment(StringAlignmentCenter);format.SetTrimming(StringTrimmingEllipsisWord);
    RectF previous(28,47,REAL(w-56),36);
    Font prior_font(L"Segoe UI",20,FontStyleRegular,UnitPixel);
    g.DrawString(app.previous.c_str(),-1,&prior_font,previous,&format,&old);
    RectF current(28,87,REAL(w-56),REAL(h-122));
    g.DrawString(app.current.c_str(),-1,&main_font,current,&format,&white);
    const wchar_t* help=app.locked?L"CLICK-THROUGH  ·  Ctrl+Alt+S show / unlock  ·  Ctrl+Alt+P pause  ·  Ctrl+Alt+Q exit":L"DRAG TO MOVE  ·  Right-click settings  ·  Ctrl+Alt+S show  ·  Ctrl+Alt+L click-through";
    RectF foot(24,REAL(h-26),REAL(w-48),20);g.DrawString(help,-1,&foot_font,foot,&format,&secondary);
}
bool render(){
    app.last_render=false;
    if(!app.window)return false;
    RECT rect;GetWindowRect(app.window,&rect);SIZE size{rect.right-rect.left,rect.bottom-rect.top};
    if(size.cx<=0||size.cy<=0)return false;
    // Draw directly into the premultiplied BGRA surface consumed by Windows.
    // Avoid losing the alpha channel during a GDI+ -> HBITMAP conversion.
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=size.cx;info.bmiHeader.biHeight=-size.cy;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;
    HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
    HBITMAP handle=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!screen||!dc||!handle){
        if(handle)DeleteObject(handle);if(dc)DeleteDC(dc);if(screen)ReleaseDC(nullptr,screen);return false;
    }
    {
        Bitmap bitmap(size.cx,size.cy,size.cx*4,PixelFormat32bppPARGB,static_cast<BYTE*>(pixels));paint(bitmap);
    }
    if(app.window_test){
        bool clear=false,translucent=false,opaque=false;
        auto data=static_cast<BYTE*>(pixels);
        for(LONG i=0;i<size.cx*size.cy;++i){BYTE a=data[i*4+3];clear|=a==0;translucent|=a>0&&a<255;opaque|=a==255;}
        app.frame_alpha=clear&&translucent&&opaque;
    }
    auto old=SelectObject(dc,handle);
    POINT dst{rect.left,rect.top},src{};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    app.last_render=UpdateLayeredWindow(app.window,screen,&dst,&size,dc,&src,0,&blend,ULW_ALPHA)!=FALSE;
    SelectObject(dc,old);DeleteObject(handle);DeleteDC(dc);ReleaseDC(nullptr,screen);
    return app.last_render;
}
void show_overlay(bool reset_position){
    POINT cursor{};GetCursorPos(&cursor);
    HMONITOR monitor=reset_position?MonitorFromPoint(cursor,MONITOR_DEFAULTTOPRIMARY):MonitorFromWindow(app.window,MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(MONITORINFO)};GetMonitorInfoW(monitor,&info);
    RECT area=info.rcWork,rect{};GetWindowRect(app.window,&rect);
    int width=std::min(1000,int(area.right-area.left-40)),height=std::min(230,int(area.bottom-area.top));
    int x=reset_position?area.left+(area.right-area.left-width)/2:std::clamp(int(rect.left),int(area.left),int(area.right)-width);
    int y=reset_position?std::max(int(area.top),int(area.bottom)-height-30):std::clamp(int(rect.top),int(area.top),int(area.bottom)-height);
    if(reset_position){
        app.locked=false;
        SetWindowLongPtrW(app.window,GWL_EXSTYLE,GetWindowLongPtrW(app.window,GWL_EXSTYLE)&~WS_EX_TRANSPARENT);
    }
    // The first ShowWindow can obey the launcher's STARTUPINFO/SW_HIDE instead
    // of our argument. SWP_SHOWWINDOW explicitly restores visibility afterward.
    ShowWindow(app.window,IsIconic(app.window)?SW_RESTORE:SW_SHOWNOACTIVATE);
    SetWindowPos(app.window,HWND_TOPMOST,x,y,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    if(!render()&&!app.window_test)
        MessageBoxW(app.window,L"The subtitle window could not be drawn. Please restart the application.",L"FunASR Subtitle",MB_OK|MB_ICONERROR);
}
void save_settings(){
    auto file=(app.root/L"settings.ini").wstring();
    WritePrivateProfileStringW(L"overlay",L"font",std::to_wstring(app.font_size).c_str(),file.c_str());
    WritePrivateProfileStringW(L"overlay",L"opacity",std::to_wstring(app.opacity).c_str(),file.c_str());
    WritePrivateProfileStringW(L"engine",L"threads",std::to_wstring(app.threads).c_str(),file.c_str());
}
void start_pipeline(){
    app.pipeline.reset();
    {std::lock_guard lock(app.mutex);app.pending.clear();}
    app.paused=false;app.current=L"Loading models…";app.previous.clear();app.current_id=0;
    PipelineConfig c{utf8((app.root/L"models/sensevoice-small-q8.gguf").wstring()),utf8((app.root/L"models/fsmn-vad.gguf").wstring()),utf8((app.root/L"logs").wstring()),app.threads};
    app.pipeline=std::make_unique<Pipeline>(c,[](SubtitleEvent e){
        if(!app.alive)return;
        {std::lock_guard lock(app.mutex);
            if(!e.final&&!app.pending.empty()&&!app.pending.back().final&&app.pending.back().id==e.id)
                app.pending.back()=std::move(e);
            else {if(app.pending.size()==64)app.pending.pop_front();app.pending.push_back(std::move(e));}
        }PostMessageW(app.window,WM_TEXT_READY,0,0);
    },[](std::string s){
        if(!app.alive)return;
        {std::lock_guard lock(app.mutex);app.pending_status=std::move(s);}PostMessageW(app.window,WM_STATUS_READY,0,0);
    });
    app.pipeline->start();render();
}
void command(int id){
    if(id>=ThreadBase&&id<ThreadBase+static_cast<int>(std::size(thread_presets))){
        int selected=thread_presets[id-ThreadBase];
        if(selected!=app.threads){app.threads=selected;start_pipeline();}
        save_settings();render();return;
    }
    switch(id){
    case Show:show_overlay(true);return;
    case Pause:
        app.paused=!app.paused;if(app.pipeline)app.pipeline->pause(app.paused);
        {std::lock_guard lock(app.mutex);app.pending.clear();}
        app.status=app.paused?L"Paused · Ctrl+Alt+P to resume":L"Listening · CPU · System audio";
        app.current=app.paused?L"Capturing paused":L"Waiting for speech…";app.previous.clear();break;
    case Lock:{
        app.locked=!app.locked;LONG_PTR style=GetWindowLongPtrW(app.window,GWL_EXSTYLE);
        SetWindowLongPtrW(app.window,GWL_EXSTYLE,app.locked?style|WS_EX_TRANSPARENT:style&~WS_EX_TRANSPARENT);break;}
    case Bigger:app.font_size=std::min(42,app.font_size+2);break;
    case Smaller:app.font_size=std::max(18,app.font_size-2);break;
    case Restart:start_pipeline();break;
    case Logs:fs::create_directories(app.root/L"logs");ShellExecuteW(app.window,L"open",(app.root/L"logs").c_str(),nullptr,nullptr,SW_SHOWNORMAL);break;
    case Exit:DestroyWindow(app.window);return;
    }
    save_settings();render();
}
void menu(){
    auto m=CreatePopupMenu();
    AppendMenuW(m,MF_STRING,Show,L"Show / reset subtitle position  Ctrl+Alt+S");
    AppendMenuW(m,MF_STRING,Pause,app.paused?L"Resume  Ctrl+Alt+P":L"Pause  Ctrl+Alt+P");
    AppendMenuW(m,MF_STRING|(app.locked?MF_CHECKED:0),Lock,L"Click-through  Ctrl+Alt+L");
    AppendMenuW(m,MF_STRING,Bigger,L"Larger text");AppendMenuW(m,MF_STRING,Smaller,L"Smaller text");
    HMENU threads=CreatePopupMenu();
    for(int i=0;i<static_cast<int>(std::size(thread_presets));++i){
        auto label=std::to_wstring(thread_presets[i])+L" threads";
        AppendMenuW(threads,MF_STRING|(app.threads==thread_presets[i]?MF_CHECKED:0),ThreadBase+i,label.c_str());
    }
    AppendMenuW(m,MF_POPUP,reinterpret_cast<UINT_PTR>(threads),L"CPU recognition threads");
    AppendMenuW(m,MF_STRING,Restart,L"Restart audio and models");AppendMenuW(m,MF_STRING,Logs,L"Open transcript folder");
    AppendMenuW(m,MF_SEPARATOR,0,nullptr);AppendMenuW(m,MF_STRING,Exit,L"Exit  Ctrl+Alt+Q");
    POINT p;GetCursorPos(&p);SetForegroundWindow(app.window);
    int result=TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,app.window,nullptr);DestroyMenu(m);if(result)command(result);
}
bool check_window(const char* phase){
    RECT rect{};GetWindowRect(app.window,&rect);
    MONITORINFO monitor{sizeof(MONITORINFO)};GetMonitorInfoW(MonitorFromWindow(app.window,MONITOR_DEFAULTTONEAREST),&monitor);
    bool visible=IsWindowVisible(app.window)&&!IsIconic(app.window);
    bool inside=rect.left>=monitor.rcWork.left&&rect.right<=monitor.rcWork.right&&rect.top>=monitor.rcWork.top&&rect.bottom<=monitor.rcWork.bottom;
    auto style=GetWindowLongPtrW(app.window,GWL_EXSTYLE);
    bool accessible=!app.locked&&!(style&WS_EX_TRANSPARENT);
    bool topmost=(style&WS_EX_TOPMOST)!=0;
    app.window_report<<phase<<": visible="<<visible<<" inside_work_area="<<inside<<" topmost="<<topmost
        <<" unlocked="<<accessible<<" layered_update="<<app.last_render<<" transparent_translucent_opaque_pixels="<<app.frame_alpha<<std::endl;
    return visible&&inside&&accessible&&topmost&&app.last_render&&app.frame_alpha;
}
void window_test_tick(){
    bool failed=false;
    if(app.test_stage==0){
        if(!check_window("hidden startup"))failed=true;
        else {
            // Reproduce a lost, click-through window before relaunching the same EXE.
            ShowWindow(app.window,SW_HIDE);
            SetWindowPos(app.window,nullptr,-32000,-32000,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
            app.locked=true;SetWindowLongPtrW(app.window,GWL_EXSTYLE,GetWindowLongPtrW(app.window,GWL_EXSTYLE)|WS_EX_TRANSPARENT);
            STARTUPINFOW startup{sizeof(STARTUPINFOW)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
            PROCESS_INFORMATION child{};std::wstring args=GetCommandLineW();
            if(!CreateProcessW(nullptr,args.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&child))failed=true;
            else {CloseHandle(child.hThread);app.test_child=child.hProcess;app.test_stage=1;}
        }
    }else if(WaitForSingleObject(app.test_child,0)==WAIT_OBJECT_0){
        DWORD code=1;GetExitCodeProcess(app.test_child,&code);
        app.window_report<<"duplicate exited="<<code<<" activation_received="<<app.reactivated<<std::endl;
        failed=code!=0||!app.reactivated||!check_window("duplicate launch recovery");
        if(!failed){
            ShowWindow(app.window,SW_MINIMIZE);show_overlay(true);
            failed=!check_window("minimized window recovery");
        }
        app.exit_code=failed?1:0;DestroyWindow(app.window);return;
    }
    if(GetTickCount64()-app.test_started>10000)failed=true;
    if(failed){app.window_report<<"FAIL"<<std::endl;app.exit_code=1;DestroyWindow(app.window);}
}
LRESULT CALLBACK proc(HWND w,UINT message,WPARAM wp,LPARAM lp){
    switch(message){
    case WM_REVEAL:show_overlay(true);app.reactivated=true;return 1;
    case WM_LBUTTONDOWN:ReleaseCapture();SendMessageW(w,WM_NCLBUTTONDOWN,HTCAPTION,lp);return 0;
    case WM_RBUTTONUP:menu();return 0;
    case WM_HOTKEY:command(int(wp));return 0;
    case WM_TRAY:if(lp==WM_RBUTTONUP)menu();else if(lp==WM_LBUTTONUP||lp==WM_LBUTTONDBLCLK)show_overlay(true);return 0;
    case WM_TEXT_READY:{
        std::deque<SubtitleEvent> events;{std::lock_guard lock(app.mutex);events.swap(app.pending);}
        if(app.paused)return 0;
        for(auto& e:events){
            if(e.id!=app.current_id){app.previous=app.current_id?app.current:L"";app.current_id=e.id;}
            app.current=wide(e.text);app.status=L"CPU · "+std::to_wstring(int(e.compute_ms))+L" ms · "+(e.final?L"Final":L"Live");
        }render();return 0;}
    case WM_STATUS_READY:{
        std::lock_guard lock(app.mutex);app.status=wide(app.pending_status);
        if(!app.current_id&&!app.paused&&app.pending_status.starts_with("Listening to Windows output:"))app.current=L"Waiting for system audio…";
        render();return 0;}
    case WM_DISPLAYCHANGE:show_overlay(false);return 0;
    case WM_SETTINGCHANGE:if(wp==SPI_SETWORKAREA)show_overlay(false);return 0;
    case WM_TIMER:
        if(wp==3){window_test_tick();return 0;}
        if(wp==1){KillTimer(w,1);PlaySoundW(app.test_audio.c_str(),nullptr,SND_FILENAME|SND_ASYNC|SND_NODEFAULT);}
        if(wp==2){
            KillTimer(w,2);
            fs::create_directories(app.root/L"test-output");
            std::ofstream state(app.root/L"test-output/gui-state.txt");state<<utf8(app.current)<<'\n'<<utf8(app.status)<<'\n';
            RECT rect{};GetWindowRect(w,&rect);
            state<<"visible="<<IsWindowVisible(w)<<" layered_update="<<app.last_render
                <<" rect="<<rect.left<<','<<rect.top<<','<<rect.right<<','<<rect.bottom<<'\n';
            Bitmap b(1000,230,PixelFormat32bppPARGB);paint(b);
            CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
            b.Save((app.root/L"test-output/gui-live.png").c_str(),&png);
            DestroyWindow(w);
        }return 0;
    case WM_DESTROY:
        app.alive=false;app.pipeline.reset();Shell_NotifyIconW(NIM_DELETE,&app.tray);
        UnregisterHotKey(w,Pause);UnregisterHotKey(w,Lock);UnregisterHotKey(w,Exit);UnregisterHotKey(w,Show);PostQuitMessage(app.exit_code);return 0;
    }
    return DefWindowProcW(w,message,wp,lp);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int){
    SetProcessDPIAware();
    GdiplusStartupInput ginput;ULONG_PTR token;GdiplusStartup(&token,&ginput,nullptr);
    int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    app.root=find_root();
    if(argc==3&&std::wstring(argv[1])==L"--render-preview"){
        app.status=L"CPU · 184 ms · Live";app.previous=L"Speech recognition should keep up with the conversation.";
        app.current=L"English subtitles, directly from your Windows audio.";
        Bitmap b(1000,230,PixelFormat32bppPARGB);paint(b);
        CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
        auto status=b.Save(argv[2],&png);LocalFree(argv);return status==Ok?0:1;
    }
    int test_seconds=0;std::wstring window_class=L"FunASRSubtitle",mutex_name=L"Local\\FunASRSubtitle.SingleInstance",test_report;
    for(int i=1;i<argc;++i){
        if(std::wstring(argv[i])==L"--test-seconds"&&i+1<argc)test_seconds=std::clamp(_wtoi(argv[++i]),5,3600);
        else if(std::wstring(argv[i])==L"--test-audio"&&i+1<argc)app.test_audio=argv[++i];
        else if(std::wstring(argv[i])==L"--test-window"&&i+1<argc){
            app.window_test=true;std::wstring session=argv[++i];window_class+=L".Test."+session;mutex_name+=L".Test."+session;
        }
        else if(std::wstring(argv[i])==L"--test-report"&&i+1<argc)test_report=argv[++i];
    }
    app.testing=test_seconds>0;
    LocalFree(argv);
    HANDLE singleton=CreateMutexW(nullptr,FALSE,mutex_name.c_str());
    if(!singleton){GdiplusShutdown(token);return 1;}
    if(GetLastError()==ERROR_ALREADY_EXISTS){
        // The first process may still be creating its window. Wait briefly for it,
        // then ask it to reveal itself without creating another GUI or engine.
        bool activated=false;
        for(int attempt=0;attempt<30;++attempt){
            if(HWND existing=FindWindowW(window_class.c_str(),nullptr)){
                DWORD result=0;GetWindowThreadProcessId(existing,&result);AllowSetForegroundWindow(result);
                DWORD_PTR reply=0;
                activated=SendMessageTimeoutW(existing,WM_REVEAL,0,0,SMTO_ABORTIFHUNG,2000,&reply)&&reply==1;
                break;
            }
            Sleep(100);
        }
        if(!activated&&!app.window_test)MessageBoxW(nullptr,L"The running subtitle application could not be reached. It may be on another Windows desktop. Close FunASRSubtitle.exe in Task Manager, then open it again.",L"FunASR Subtitle",MB_OK|MB_ICONERROR);
        CloseHandle(singleton);GdiplusShutdown(token);return activated?0:1;
    }
    if(app.window_test){
        app.window_report.open(fs::path(test_report));app.test_started=GetTickCount64();app.exit_code=1;
        app.current=L"Subtitle window visibility regression test";
    }
    auto ini=(app.root/L"settings.ini").wstring();
    app.font_size=std::clamp(int(GetPrivateProfileIntW(L"overlay",L"font",28,ini.c_str())),18,42);
    app.opacity=std::clamp(int(GetPrivateProfileIntW(L"overlay",L"opacity",175,ini.c_str())),30,240);
    app.threads=std::clamp(int(GetPrivateProfileIntW(L"engine",L"threads",8,ini.c_str())),1,32);
    WNDCLASSW c{};c.lpfnWndProc=proc;c.hInstance=instance;c.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(IDI_APP));c.lpszClassName=window_class.c_str();c.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&c);
    RECT area;SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0);int width=std::min(1000,int(area.right-area.left-40));
    app.window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOPMOST|WS_EX_TOOLWINDOW,c.lpszClassName,L"FunASR Subtitle",WS_POPUP,
        area.left+(area.right-area.left-width)/2,area.bottom-260,width,230,nullptr,nullptr,instance,nullptr);
    if(!app.window)return 1;
    if(!app.window_test){
    app.tray.cbSize=sizeof(app.tray);app.tray.hWnd=app.window;app.tray.uID=1;
    app.tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;app.tray.uCallbackMessage=WM_TRAY;
    app.tray.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(IDI_TRAY));wcscpy_s(app.tray.szTip,L"FunASR Subtitle — system audio");Shell_NotifyIconW(NIM_ADD,&app.tray);
    bool hotkeys=RegisterHotKey(app.window,Pause,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'P')!=0;
    hotkeys=RegisterHotKey(app.window,Lock,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'L')&&hotkeys;
    hotkeys=RegisterHotKey(app.window,Exit,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'Q')&&hotkeys;
    hotkeys=RegisterHotKey(app.window,Show,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'S')&&hotkeys;
    if(!hotkeys)MessageBoxW(app.window,L"Some shortcuts are in use. The tray menu remains available.",L"Shortcut conflict",MB_OK);
    }
    show_overlay(true);
    if(app.window_test)SetTimer(app.window,3,100,nullptr);else start_pipeline();
    if(app.testing){if(!app.test_audio.empty())SetTimer(app.window,1,2000,nullptr);SetTimer(app.window,2,test_seconds*1000,nullptr);}
    MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    if(app.test_child)CloseHandle(app.test_child);
    CloseHandle(singleton);GdiplusShutdown(token);return app.exit_code;
}
