#include "CefForm.h"

CefForm::CefForm()
{
}

CefForm::~CefForm()
{
}

DString CefForm::GetSkinFolder()
{
    return _T("cef");
}

DString CefForm::GetSkinFile()
{
    return _T("cef.xml");
}

void CefForm::OnInitWindow()
{
#if !defined (DUILIB_BUILD_FOR_WIN)
    //Linux平台：非离屏渲染模式下, 使用系统标题栏
    if (!kEnableOffScreenRendering) {
        SetUseSystemCaption(true);
    }
#endif

    if (!IsUseSystemCaption()) {
        if (ui::CefManager::GetInstance()->IsEnableOffScreenRendering()) {
            //离屏渲染：开启分层窗口属性
            SetLayeredWindow(true, true);
        }
        else {
            //窗口模式: 关闭分层窗口属性
            SetLayeredWindow(false, true);
        }
    }

    // 监听鼠标单击事件
    GetRoot()->AttachBubbledEvent(ui::kEventClick, UiBind(&CefForm::OnClicked, this, std::placeholders::_1));

    // 从 XML 中查找指定控件
    m_pCefControl = dynamic_cast<ui::CefControl*>(FindControl(_T("cef_control")));
    m_pCefControlDev = dynamic_cast<ui::CefControl*>(FindControl(_T("cef_control_dev")));
    m_pDevToolBtn = dynamic_cast<ui::Button*>(FindControl(_T("btn_dev_tool")));
    m_pEditUrl = dynamic_cast<ui::RichEdit*>(FindControl(_T("edit_url")));
    ASSERT(m_pDevToolBtn != nullptr);
    //ASSERT(m_pEditUrl != nullptr);

    // 设置输入框样式
    if (m_pEditUrl != nullptr) {
        m_pEditUrl->SetSelAllOnFocus(true);
        m_pEditUrl->AttachReturn(UiBind(&CefForm::OnNavigate, this, std::placeholders::_1));
    }

    ui::Control* pControl = FindControl(_T("btn_back"));
    if (pControl != nullptr) {
        pControl->SetEnabled(false);
    }

    pControl = FindControl(_T("btn_forward"));
    if (pControl != nullptr) {
        pControl->SetEnabled(false);
    }

    if (m_pCefControl != nullptr) {
        m_pCefControl->SetCefEventHandler(this);
        if (m_pCefControlDev != nullptr) {
            //m_pCefControl的开发者工具，显示在m_pCefControlDev这个控件中
            m_pCefControl->SetDevToolsView(m_pCefControlDev);
        }

        //URL变化事件
        m_pCefControl->AttachMainUrlChange(UiBind(&CefForm::OnMainUrlChange, this, std::placeholders::_1, std::placeholders::_2));
    }

    if (m_pCefControl != nullptr) {
        m_pCefControl->AttachDevToolAttachedStateChange(UiBind(&CefForm::OnDevToolVisibleStateChanged, this, std::placeholders::_1, std::placeholders::_2));
    }
    if (m_pCefControlDev != nullptr) {
        m_pCefControlDev->SetFadeVisible(false);
    }

    //设置控制主进程单例的回调函数
    ui::CefManager::GetInstance()->SetAlreadyRunningAppRelaunch(UiBind(&CefForm::OnAlreadyRunningAppRelaunch, this, std::placeholders::_1));

    if (!ui::CefManager::GetInstance()->IsEnableOffScreenRendering()) {
        //处理控件多焦点问题（由于cef控件是子窗口模式，duilib内部无法自己完成这个功能）
        AttachWindowKillFocus([this](const ui::EventArgs& args) {
            //当窗口失去焦点的时候，让界面中的控件失去焦点，避免出现网页与界面控件同时处于焦点状态的问题
            KillFocusControl();
            return true;
            });
    }
}

void CefForm::OnPreCloseWindow()
{
    //关闭窗口时，先关闭该窗口关联的所有Browser对象
    ui::CefManager::GetInstance()->ProcessWindowCloseEvent(this);
}

void CefForm::OnCloseWindow()
{   
    //关闭窗口后，退出主线程的消息循环，关闭程序    
    ui::CefManager::GetInstance()->PostQuitMessage(0L);
}

void CefForm::OnAlreadyRunningAppRelaunch(const std::vector<DString>& argumentList)
{
    if (ui::GlobalManager::Instance().IsInUIThread()) {
        //CEF 133版本会调用此接口
        SetWindowForeground();
        if (!argumentList.empty()) {
            //只处理第一个参数
            DString url = argumentList[0];
            if (m_pCefControl != nullptr) {                
                m_pCefControl->LoadURL(url);
                m_pCefControl->SetFocus();
            }
        }
    }
    else {
        //转发到UI线程处理
        ui::GlobalManager::Instance().Thread().PostTask(ui::kThreadUI, UiBind(&CefForm::OnAlreadyRunningAppRelaunch, this, argumentList));
    }
}

bool CefForm::OnClicked(const ui::EventArgs& msg)
{
    DString name = msg.GetSender()->GetName();

    if (name == _T("btn_dev_tool")) {
        if (m_pCefControl != nullptr) {
            if (m_pCefControl->IsAttachedDevTools()) {
                m_pCefControl->DettachDevTools();
                if (m_pCefControlDev != nullptr) {
                    m_pCefControlDev->SetFadeVisible(false);
                }
            }
            else {
                m_pCefControl->AttachDevTools();
            }
        }
    }
    else if (name == _T("btn_back")) {
        if (m_pCefControl != nullptr) {
            m_pCefControl->GoBack();
        }
    }
    else if (name == _T("btn_forward")) {
        if (m_pCefControl != nullptr) {
            m_pCefControl->GoForward();
        }
    }
    else if (name == _T("btn_navigate")) {
        ui::EventArgs emptyMsg;
        OnNavigate(emptyMsg);
    }
    else if (name == _T("btn_refresh")) {
        if (m_pCefControl != nullptr) {
            m_pCefControl->Refresh();
        }
    }
    return true;
}

bool CefForm::OnNavigate(const ui::EventArgs& /*msg*/)
{
    if ((m_pEditUrl != nullptr) && !m_pEditUrl->GetText().empty()) {
        if (m_pCefControl != nullptr) {
            m_pCefControl->LoadURL(m_pEditUrl->GetText());
            m_pCefControl->SetFocus();
        }
    }
    return true;
}

void CefForm::OnAfterCreated(CefRefPtr<CefBrowser> browser)
{
    ui::GlobalManager::Instance().AssertUIThread();
}

void CefForm::OnBeforeClose(CefRefPtr<CefBrowser> browser)
{
    ui::GlobalManager::Instance().AssertUIThread();
}

void CefForm::OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefRefPtr<CefContextMenuParams> params,
                                  CefRefPtr<CefMenuModel> model)
{
    ASSERT(CefCurrentlyOn(TID_UI));
}

bool CefForm::OnContextMenuCommand(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefContextMenuParams> params,
                                   int command_id,
                                   cef_event_flags_t event_flags)
{
    ASSERT(CefCurrentlyOn(TID_UI));
    return false;
}
    
void CefForm::OnContextMenuDismissed(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame)
{
    ASSERT(CefCurrentlyOn(TID_UI));
}

void CefForm::OnTitleChange(CefRefPtr<CefBrowser> browser, const DString& title)
{
    ui::GlobalManager::Instance().AssertUIThread();
    ui::Label* pLabelTitle = dynamic_cast<ui::Label*>(FindControl(_T("page_title")));
    if (pLabelTitle != nullptr) {
        pLabelTitle->SetText(title);
    }
}
    
void CefForm::OnUrlChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const DString& url)
{
    ui::GlobalManager::Instance().AssertUIThread();
}
    
void CefForm::OnMainUrlChange(const DString& oldUrl, const DString& newUrl)
{
    ui::GlobalManager::Instance().AssertUIThread();
    ui::RichEdit* pEditUrl = dynamic_cast<ui::RichEdit*>(FindControl(_T("edit_url")));
    if (pEditUrl != nullptr) {
        pEditUrl->SetText(newUrl);
    }
}
    
void CefForm::OnFaviconURLChange(CefRefPtr<CefBrowser> browser, const std::vector<CefString>& icon_urls)
{
    ui::GlobalManager::Instance().AssertUIThread();
}
        
void CefForm::OnFullscreenModeChange(CefRefPtr<CefBrowser> browser, bool bFullscreen)
{
    ui::GlobalManager::Instance().AssertUIThread();
}
    
void CefForm::OnStatusMessage(CefRefPtr<CefBrowser> browser, const DString& value)
{
    ui::GlobalManager::Instance().AssertUIThread();
}
    
void CefForm::OnLoadingProgressChange(CefRefPtr<CefBrowser> browser, double progress)
{
    ui::GlobalManager::Instance().AssertUIThread();
}
    
void CefForm::OnMediaAccessChange(CefRefPtr<CefBrowser> browser, bool has_video_access, bool has_audio_access)
{
    ui::GlobalManager::Instance().AssertUIThread();
}

bool CefForm::OnDragEnter(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDragData> dragData, CefDragHandler::DragOperationsMask mask)
{
    m_dropFileList.clear();
    if ((dragData != nullptr) && dragData->IsFile()){
        //拖入文件操作        
        dragData->GetFileNames(m_dropFileList);
    }
    return false;
}

void CefForm::OnDraggableRegionsChanged(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const std::vector<CefDraggableRegion>& regions)
{
    ui::GlobalManager::Instance().AssertUIThread();
}

bool CefForm::OnBeforePopup(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            int popup_id,
                            const CefString& target_url,
                            const CefString& target_frame_name,
                            CefLifeSpanHandler::WindowOpenDisposition target_disposition,
                            bool user_gesture,
                            const CefPopupFeatures& popupFeatures,
                            CefWindowInfo& windowInfo,
                            CefRefPtr<CefClient>& client,
                            CefBrowserSettings& settings,
                            CefRefPtr<CefDictionaryValue>& extra_info,
                            bool* no_javascript_access)
{
    ASSERT(CefCurrentlyOn(TID_UI));
    if (!user_gesture) {
        //自动弹窗，直接拦截
        return true;
    }
#if CEF_VERSION_MAJOR > 109
    if (target_disposition == CEF_WOD_NEW_POPUP) {
#else
    if (target_disposition == WOD_NEW_POPUP) {
#endif
        //打开新的弹出窗口（这会使browser->IsPopup()返回 true）
        Dpi().ScaleInt(windowInfo.bounds.height);
        Dpi().ScaleInt(windowInfo.bounds.width);
        return false;
    }
    else if ((browser != nullptr) && (browser->GetMainFrame() != nullptr) && !target_url.empty()) {
        //导航到弹出网址
        browser->GetMainFrame()->LoadURL(target_url);
    }
    return true;
}

void CefForm::OnBeforePopupAborted(CefRefPtr<CefBrowser> browser, int popup_id)
{
    ASSERT(CefCurrentlyOn(TID_UI));
}

bool CefForm::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                             CefRefPtr<CefFrame> frame,
                             CefRefPtr<CefRequest> request,
                             bool user_gesture,
                             bool is_redirect)
{
    ASSERT(CefCurrentlyOn(TID_UI));
    return false;
}

cef_return_value_t CefForm::OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser,
                                                 CefRefPtr<CefFrame> frame,
                                                 CefRefPtr<CefRequest> request,
                                                 CefRefPtr<CefCallback> callback)
{
    ASSERT(CefCurrentlyOn(TID_IO));
    return RV_CONTINUE;
}

void CefForm::OnResourceRedirect(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 CefRefPtr<CefRequest> request,
                                 CefRefPtr<CefResponse> response,
                                 CefString& new_url)
{
    ASSERT(CefCurrentlyOn(TID_IO));
}
    
bool CefForm::OnResourceResponse(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 CefRefPtr<CefRequest> request,
                                 CefRefPtr<CefResponse> response)
{
    ASSERT(CefCurrentlyOn(TID_IO));
    return false;
}

void CefForm::OnResourceLoadComplete(CefRefPtr<CefBrowser> browser,
                                     CefRefPtr<CefFrame> frame,
                                     CefRefPtr<CefRequest> request,
                                     CefRefPtr<CefResponse> response,
                                     cef_urlrequest_status_t status,
                                     int64_t received_content_length)
{
    ASSERT(CefCurrentlyOn(TID_IO));
}

void CefForm::OnProtocolExecution(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefRefPtr<CefRequest> request,
                                  bool& allow_os_execution)
{
    ASSERT(CefCurrentlyOn(TID_IO));
}

void CefForm::OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool isLoading, bool canGoBack, bool canGoForward)
{
    ui::GlobalManager::Instance().AssertUIThread();
    ui::Control* pControl = FindControl(_T("btn_back"));
    if ((pControl != nullptr) && (m_pCefControl != nullptr)) {
        pControl->SetEnabled(m_pCefControl->CanGoBack());
    }

    pControl = FindControl(_T("btn_forward"));
    if ((pControl != nullptr) && (m_pCefControl != nullptr)) {
        pControl->SetEnabled(m_pCefControl->CanGoForward());
    }
}
    
void CefForm::OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, cef_transition_type_t transition_type)
{
    ui::GlobalManager::Instance().AssertUIThread();
}
    
void CefForm::OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode)
{
    // 注册方法提供前端调用
    if (m_pCefControl != nullptr) {
        //显示MessageBox
        m_pCefControl->RegisterCppFunc(_T("ShowMessageBox"), ToWeakCallback([this](const std::string& params, ui::ReportResultFunction callback) {
            DString value = ui::StringConvert::UTF8ToT(params);
            ui::SystemUtil::ShowMessageBox(this, value.c_str(), _T("C++ 接收到 JavaScript 发来的消息"));
            callback(false, R"({ "message": "Success." })");
            }));

        //接收网页端的文件拖入操作
        m_pCefControl->RegisterCppFunc(_T("OnDropFilesToBrowser"), ToWeakCallback([this](const std::string& params, ui::ReportResultFunction callback) {
            DString jsonDropFileList = ui::StringConvert::UTF8ToT(params);            
            callback(false, R"({ "message": "Success." })");
            OnDropFiles(jsonDropFileList);
            }));
    }
}

void CefForm::OnDropFiles(const DString& jsonDropFileList)
{
    ui::SystemUtil::ShowMessageBox(this, jsonDropFileList.c_str(), _T("CefForm::OnDropFiles: C++ 接收到 JavaScript 发来的消息"));
    //业务处理
    //1. 解析json，从jsonDropFileList中解析出文件名和文件大小（网页端无法获取到文件的本地路径）
    //2. 比较两个文件列表（m_dropFileList, jsonDropFileList）中的文件是否一致（文件个数，文件名，文件大小）
    //3. 按m_dropFileList列表中的文件路径，处理业务
}
    
void CefForm::OnLoadError(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          cef_errorcode_t errorCode,
                          const DString& errorText,
                          const DString& failedUrl)
{
    ui::GlobalManager::Instance().AssertUIThread();
}

void CefForm::OnDevToolAttachedStateChange(bool bVisible)
{
    ui::GlobalManager::Instance().AssertUIThread();
}

bool CefForm::OnCanDownload(CefRefPtr<CefBrowser> browser,
                            const CefString& url,
                            const CefString& request_method)
{
    ASSERT(CefCurrentlyOn(TID_UI));
    return true;
}

bool CefForm::OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefDownloadItem> download_item,
                               const CefString& suggested_name,
                               CefRefPtr<CefBeforeDownloadCallback> callback)
{
    ASSERT(CefCurrentlyOn(TID_UI));
    return true;
}

void CefForm::OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefDownloadItem> download_item,
                                CefRefPtr<CefDownloadItemCallback> callback)
{
    ASSERT(CefCurrentlyOn(TID_UI));
}

bool CefForm::OnFileDialog(CefRefPtr<CefBrowser> browser,
                           cef_file_dialog_mode_t mode,
                           const CefString& title,
                           const CefString& default_file_path,
                           const std::vector<CefString>& accept_filters,
                           const std::vector<CefString>& accept_extensions,
                           const std::vector<CefString>& accept_descriptions,
                           CefRefPtr<CefFileDialogCallback> callback)
{
    ASSERT(CefCurrentlyOn(TID_UI));
    return false;
}

void CefForm::OnDocumentAvailableInMainFrame(CefRefPtr<CefBrowser> browser)
{
    ui::GlobalManager::Instance().AssertUIThread();
}

void CefForm::OnDownloadFavIconFinished(CefRefPtr<CefBrowser> browser,
                                        const CefString& image_url,
                                        int http_status_code,
                                        CefRefPtr<CefImage> image)
{
    ui::GlobalManager::Instance().AssertUIThread();
}

void CefForm::OnDevToolVisibleStateChanged(bool bVisible, bool bPopup)
{
    ui::GlobalManager::Instance().AssertUIThread();
    if (bPopup || !bVisible) {
        if (m_pCefControlDev != nullptr) {
            m_pCefControlDev->SetFadeVisible(false);
        }
    }
    else if (bVisible && !bPopup) {
        if (m_pCefControlDev != nullptr) {
            m_pCefControlDev->SetFadeVisible(true);
        }
    }
}
