#include "window.hpp"
#include "catalog.hpp"
#include "providers.hpp"
#include "ui_theme.hpp"
#include "win_util.hpp"
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <shlobj.h>
#include <wtsapi32.h>
#include <algorithm>
#include <array>
#include <chrono>

namespace palette {
namespace {
constexpr int hotkey_id=1000;
constexpr int row_height=58;
QString qs(const std::wstring& value){return QString::fromStdWString(value);}
}
PaletteWindow::PaletteWindow(Settings settings):QWidget(nullptr,Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint),settings_(std::move(settings)) {
    setObjectName("paletteRoot");setWindowTitle("Fast Palette");
    // Qt commits this translucent backing store as a complete frame. There is
    // no unpainted white Win32 client surface between show and first WM_PAINT.
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_QuitOnClose,false);
    setFixedWidth(640);
    auto* outer=new QVBoxLayout(this);outer->setContentsMargins(0,0,0,0);
    outer->setSizeConstraints(QLayout::SetNoConstraint,QLayout::SetFixedSize);
    auto* surface=new QFrame(this);surface->setObjectName("paletteSurface");outer->addWidget(surface);
    auto* contents=new QVBoxLayout(surface);contents->setContentsMargins(12,12,12,9);contents->setSpacing(7);
    auto* header=new QHBoxLayout;header->setContentsMargins(9,5,2,5);header->setSpacing(13);
    search_icon_=new QLabel(surface);search_icon_->setPixmap(ui_icon(UiIcon::search).pixmap(22,22));header->addWidget(search_icon_);
    edit_=new QLineEdit(surface);edit_->setObjectName("queryInput");edit_->setAccessibleName("Search or calculate");
    edit_->setPlaceholderText("Search or calculate");edit_->setMaxLength(4096);header->addWidget(edit_,1);
    settings_button_=new QToolButton(surface);settings_button_->setObjectName("settingsButton");
    settings_button_->setIcon(ui_icon(UiIcon::settings));settings_button_->setIconSize(QSize(21,21));
    settings_button_->setToolTip("Settings");settings_button_->setAccessibleName("Settings");header->addWidget(settings_button_);
    contents->addLayout(header);
    list_=new QListWidget(surface);list_->setObjectName("results");list_->setAccessibleName("Search results");
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);list_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);list_->setFocusPolicy(Qt::StrongFocus);contents->addWidget(list_);
    footer_=new QLabel(surface);footer_->setObjectName("paletteFooter");footer_->setProperty("muted",true);
    QFont small_font=footer_->font();small_font.setPixelSize(11);footer_->setFont(small_font);footer_->setContentsMargins(10,3,10,2);contents->addWidget(footer_);
    setTabOrder(edit_,list_);setTabOrder(list_,settings_button_);
    edit_->installEventFilter(this);list_->installEventFilter(this);settings_button_->installEventFilter(this);
    source_timer_=new QTimer(this);source_timer_->setSingleShot(true);source_timer_->setInterval(70);
    connect(source_timer_,&QTimer::timeout,this,[this]{request_sources();});
    connect(edit_,&QLineEdit::textChanged,this,[this](const QString& value){query_=value.toStdWString();set_notice({});schedule_sources();update_results();});
    connect(edit_,&QLineEdit::returnPressed,this,[this]{activate();});
    connect(list_,&QListWidget::itemActivated,this,[this](QListWidgetItem*){activate();});
    connect(settings_button_,&QToolButton::clicked,this,[this]{edit_settings();});
    refresh_timer_=new QTimer(this);refresh_timer_->setSingleShot(true);refresh_timer_->setInterval(500);
    connect(refresh_timer_,&QTimer::timeout,this,[this]{request_catalog();});
    set_notice({});
}
PaletteWindow::~PaletteWindow() {
    if(keyboard_)keyboard_->stop();
    ++source_generation_;source_worker_.stop();
    worker_.stop();
    if(shell_notify_)SHChangeNotifyDeregister(shell_notify_);
    if(host_){WTSUnRegisterSessionNotification(host_);unregister_bindings();DestroyWindow(host_);}
    for(auto& [id,icon]:pending_icons_)if(icon)DestroyIcon(icon);
}
bool PaletteWindow::create(HINSTANCE instance,bool background) {
    registered_invoke_=RegisterWindowMessageW(invoke_message);
    WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance;wc.lpfnWndProc=window_proc;wc.lpszClassName=window_class;
    if(!RegisterClassExW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
    host_=CreateWindowExW(WS_EX_TOOLWINDOW,window_class,L"Fast Palette message host",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,this);
    if(!host_)return false;
    static_cast<void>(winId());
    hotkey_registered_=register_bindings(settings_);
    if(!hotkey_registered_)set_notice("A shortcut is in use. Change it in Settings.");
    keyboard_=std::make_unique<KeyboardHook>(host_,msg_invoke);
    if(!keyboard_->start(settings_.left_win,settings_.right_win,all_hotkeys(settings_)))set_notice("Windows-key hook unavailable. Use the tray or another shortcut.");
    WTSRegisterSessionNotification(host_,NOTIFY_FOR_THIS_SESSION);
    tray_=new QSystemTrayIcon(ui_icon(UiIcon::logo),this);tray_->setToolTip("Fast Palette");
    auto* menu=new QMenu(this);
    menu->addAction("Open Fast Palette",this,[this]{show();});
    menu->addAction("Settings",this,[this]{show();edit_settings();});
    menu->addAction("Refresh applications",this,[this]{request_catalog();});
    menu->addAction("About and licenses",this,[this]{
        if(settings_open_)return;
        settings_open_=true;
        QDialog dialog(this);dialog.setWindowTitle("About Fast Palette");dialog.resize(680,500);
        auto* layout=new QVBoxLayout(&dialog);
        auto* title=new QLabel("Fast Palette " FAST_PALETTE_VERSION,&dialog);title->setProperty("role","title");layout->addWidget(title);
        auto* text=new QTextEdit(&dialog);text->setReadOnly(true);
        QString notices;
        for(const auto& name:QDir(":/licenses").entryList(QDir::Files,QDir::Name)) {
            QFile file(":/licenses/"+name);
            if(file.open(QIODevice::ReadOnly))notices+=name+"\n\n"+QString::fromUtf8(file.readAll())+"\n\n";
        }
        text->setPlainText(notices);layout->addWidget(text);
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        dialog.exec();settings_open_=false;
    });
    menu->addSeparator();menu->addAction("Exit",qApp,&QApplication::quit);
    tray_->setContextMenu(menu);
    connect(tray_,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason reason){if(reason==QSystemTrayIcon::Trigger)show();});
    tray_->show();
    std::array<PIDLIST_ABSOLUTE,3> pidls{};
    SHGetKnownFolderIDList(FOLDERID_Programs,0,nullptr,&pidls[0]);SHGetKnownFolderIDList(FOLDERID_CommonPrograms,0,nullptr,&pidls[1]);
    SHGetKnownFolderIDList(FOLDERID_AppsFolder,0,nullptr,&pidls[2]);
    std::vector<SHChangeNotifyEntry> watches;for(auto pidl:pidls)if(pidl)watches.push_back({pidl,TRUE});
    if(!watches.empty())shell_notify_=SHChangeNotifyRegister(host_,SHCNRF_ShellLevel|SHCNRF_NewDelivery,
        SHCNE_CREATE|SHCNE_DELETE|SHCNE_RENAMEITEM|SHCNE_UPDATEDIR|SHCNE_UPDATEITEM|SHCNE_ASSOCCHANGED,msg_shell,static_cast<int>(watches.size()),watches.data());
    for(auto pidl:pidls)CoTaskMemFree(pidl);
    request_catalog();update_results();
    // Pay style/layout/font initialization once while hidden. Reopening reuses it.
    ensurePolished();layout()->activate();static_cast<void>(grab());
    if(!background)show();return true;
}
void PaletteWindow::show() {
    if(settings_open_)return;
    const auto foreground=GetForegroundWindow();if(foreground!=handle() && foreground!=host_)previous_=foreground;
    if(!query_.empty())edit_->clear();
    QScreen* target=QGuiApplication::primaryScreen();
    if(previous_ && IsWindow(previous_)){
        const auto monitor=MonitorFromWindow(previous_,MONITOR_DEFAULTTONEAREST);MONITORINFOEXW info{};info.cbSize=sizeof(info);
        if(GetMonitorInfoW(monitor,&info))for(auto* screen:QGuiApplication::screens())if(screen->name()==QString::fromWCharArray(info.szDevice)){target=screen;break;}
    }
    if(!target)return;
    if(windowHandle())windowHandle()->setScreen(target);
    const auto work=target->availableGeometry();
    move(work.left()+std::max(0,(work.width()-width())/2),std::clamp(work.top()+work.height()/5,work.top(),std::max(work.top(),work.bottom()-height()-12)));
    QWidget::show();raise();activateWindow();SetForegroundWindow(handle());edit_->setFocus(Qt::ShortcutFocusReason);
}
void PaletteWindow::dismiss(bool restore) {
    hide();if(restore && previous_ && IsWindow(previous_))SetForegroundWindow(previous_);
}
void PaletteWindow::closeEvent(QCloseEvent* event){event->ignore();dismiss(true);}
bool PaletteWindow::event(QEvent* event) {
    if(event->type()==QEvent::WindowDeactivate && !settings_open_ && isVisible())dismiss();
    return QWidget::event(event);
}
bool PaletteWindow::eventFilter(QObject* object,QEvent* event) {
    if(event->type()==QEvent::KeyPress) {
        auto* key=static_cast<QKeyEvent*>(event);
        if(key->key()==Qt::Key_Escape){dismiss(true);return true;}
        if(object==settings_button_ && (key->key()==Qt::Key_Return || key->key()==Qt::Key_Enter)) {
            settings_button_->click();return true;
        }
        if((key->key()==Qt::Key_Up || key->key()==Qt::Key_Down) && object!=settings_button_) {
            const int count=list_->count();if(count)list_->setCurrentRow((std::max(0,list_->currentRow())+(key->key()==Qt::Key_Up?count-1:1))%count);
            return true;
        }
    }
    return QWidget::eventFilter(object,event);
}
void PaletteWindow::set_notice(const QString& text) {
    footer_->setText(text);footer_->setVisible(!text.isEmpty());
}
void PaletteWindow::update_results(bool preserve) {
    std::wstring old;
    const int current=list_->currentRow();if(preserve && current>=0 && static_cast<size_t>(current)<rows_.size())old=rows_[current].identity;
    rows_.clear();
    const auto calc=settings_.search_calculator?calculate(query_):CalcResult{};const auto first=query_.find_first_not_of(L" \t\r\n");
    const bool forced=first!=std::wstring::npos && query_[first]==L'=' && settings_.search_calculator;
    const bool files_only=query_.starts_with(L"? ") && settings_.search_everything;
    std::vector<AppEntry> candidates;
    candidates.reserve(14);
    const auto collect=[&](const std::vector<AppEntry>& entries){for(const auto& hit:search(entries,query_,7))candidates.push_back(entries[hit.index]);};
    if(!forced && !files_only && settings_.search_apps)collect(apps_);
    if(!forced && !files_only && settings_.search_settings && first!=std::wstring::npos)collect(windows_settings());
    const auto hits=forced?std::vector<SearchHit>{}:search(candidates,query_,7);
    if(calc.status==CalcStatus::value)rows_.push_back({calc.text,L"Enter to copy",{},true,L"calculation"});
    else if(calc.status==CalcStatus::error && hits.empty())rows_.push_back({calc.text,L"Check the expression",{},false,L"error"});
    else if(calc.status==CalcStatus::incomplete && hits.empty())rows_.push_back({L"Keep typing",L"Incomplete expression",{},false,L"incomplete"});
    if(!forced){
        for(const auto& entry:source_results_)if(entry.source==SearchSource::path)rows_.push_back({entry.name,entry.detail,entry,false,entry.id});
        const auto file_count=std::count_if(source_results_.begin(),source_results_.end(),[](const auto& e){return e.source==SearchSource::file;});
        const size_t local_limit=file_count?5:7;
        for(const auto& hit:hits){if(rows_.size()>=local_limit)break;const auto& app=candidates[hit.index];rows_.push_back({app.name,app.detail,app,false,app.id});}
        for(const auto& entry:source_results_){if(rows_.size()>=7)break;if(entry.source!=SearchSource::file)continue;
            if(std::none_of(rows_.begin(),rows_.end(),[&](const auto& row){return row.identity==entry.id;}))rows_.push_back({entry.name,entry.detail,entry,false,entry.id});}
    }
    if(rows_.empty()){
        const bool waiting=sources_pending_ || (loading_ && settings_.search_apps && !files_only);
        const bool missing=files_only && !everything_available_ && !waiting;
        const bool empty_files=files_only && QString::fromStdWString(query_.substr(2)).trimmed().isEmpty();
        rows_.push_back({empty_files?L"Type a filename":missing?L"Open Everything to search files":waiting?L"Searching...":L"No results",{}, {},false,L"empty"});
    }
    list_->setUpdatesEnabled(false);list_->clear();int selected=0;
    for(size_t i=0;i<rows_.size();++i) {
        const auto& row=rows_[i];auto* item=new QListWidgetItem(list_);
        item->setData(Qt::UserRole,qs(row.title));
        item->setToolTip(qs(row.title)+"\n"+qs(row.detail));
        item->setSizeHint(QSize(0,row_height));item->setData(Qt::AccessibleTextRole,qs(row.title)+" "+qs(row.detail));
        auto* content=new QWidget(list_);content->setObjectName("resultRow");content->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* horizontal=new QHBoxLayout(content);horizontal->setContentsMargins(12,7,12,7);horizontal->setSpacing(13);
        auto* icon=new QLabel(content);icon->setObjectName("resultIcon");icon->setFixedSize(32,32);icon->setAlignment(Qt::AlignCenter);horizontal->addWidget(icon);
        auto* labels=new QVBoxLayout;labels->setContentsMargins(0,0,0,0);labels->setSpacing(2);
        auto* title=new QLabel(content);title->setTextFormat(Qt::PlainText);QFont font=title->font();font.setPixelSize(row.copy?21:15);title->setFont(font);
        title->setText(title->fontMetrics().elidedText(qs(row.title),Qt::ElideRight,width()-104));title->setMinimumWidth(0);labels->addWidget(title);
        auto* detail=new QLabel(content);detail->setTextFormat(Qt::PlainText);detail->setProperty("muted",true);QFont small_font=detail->font();small_font.setPixelSize(12);detail->setFont(small_font);
        detail->setVisible(!row.detail.empty() && row.detail!=L"Application" && !row.copy);
        detail->setText(detail->fontMetrics().elidedText(qs(row.detail),Qt::ElideMiddle,width()-104));detail->setMinimumWidth(0);labels->addWidget(detail);
        horizontal->addLayout(labels,1);list_->setItemWidget(item,content);if(row.identity==old)selected=static_cast<int>(i);
    }
    list_->setFixedHeight(static_cast<int>(rows_.size())*row_height);list_->setCurrentRow(selected);list_->setUpdatesEnabled(true);
    update_icons();
    // A nested layout's cached size hint otherwise lags behind asynchronous
    // result changes until the next LayoutRequest, clipping new rows.
    list_->parentWidget()->layout()->invalidate();layout()->invalidate();
    list_->parentWidget()->updateGeometry();layout()->activate();adjustSize();request_icons();
}
void PaletteWindow::update_icons() {
    search_icon_->setPixmap(ui_icon(UiIcon::search).pixmap(22,22));
    settings_button_->setIcon(ui_icon(UiIcon::settings));
    for(int i=0;i<list_->count();++i) {
        auto* widget=list_->itemWidget(list_->item(i));auto* label=widget?widget->findChild<QLabel*>("resultIcon"):nullptr;if(!label)continue;
        const auto& row=rows_[static_cast<size_t>(i)];QIcon icon;
        if(row.app)icon=icons_.value(qs(row.app->id));
        if(row.app && row.app->source==SearchSource::windows_settings)icon=ui_icon(UiIcon::settings);
        if(row.app && (row.app->source==SearchSource::path || row.app->source==SearchSource::file))icon=ui_icon(row.app->is_folder?UiIcon::folder:UiIcon::file);
        if(icon.isNull())icon=ui_icon(row.copy?UiIcon::calculator:UiIcon::application);
        label->setPixmap(icon.pixmap(label->size()));
    }
}
bool PaletteWindow::clipboard(const std::wstring& value) {
    const SIZE_T bytes=(value.size()+1)*sizeof(wchar_t);HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,bytes);if(!memory)return false;
    void* clipboard_data=GlobalLock(memory);if(!clipboard_data){GlobalFree(memory);return false;}memcpy(clipboard_data,value.c_str(),bytes);GlobalUnlock(memory);
    if(!OpenClipboard(handle())){GlobalFree(memory);return false;}const bool ok=EmptyClipboard()!=FALSE && SetClipboardData(CF_UNICODETEXT,memory)!=nullptr;
    CloseClipboard();if(!ok)GlobalFree(memory);return ok;
}
void PaletteWindow::activate() {
    const int selected=list_->currentRow();if(selected<0 || static_cast<size_t>(selected)>=rows_.size() || launching_)return;
    const auto row=rows_[selected];
    if(row.copy){if(clipboard(row.title))dismiss(true);else set_notice("Clipboard is busy. Press Enter to try again.");}
    else if(row.app){launching_=true;set_notice("Opening "+qs(row.title)+"...");const auto app=*row.app;
        worker_.enqueue([this,app](std::stop_token stop){if(stop.stop_requested())return;LaunchReply reply{};reply.id=app.id;reply.ok=launch_app(app,reply.error);
            {std::lock_guard lock(inbox_mutex_);pending_launch_=std::move(reply);}PostMessageW(host_,msg_launch,0,0);},true);
    }
}
void PaletteWindow::request_catalog() {
    if(!settings_.search_apps){apps_.clear();update_results(true);return;}
    if(loading_){refresh_again_=true;return;}loading_=true;
    worker_.enqueue([this](std::stop_token stop){auto apps=discover_apps(stop);if(stop.stop_requested())return;
        {std::lock_guard lock(inbox_mutex_);pending_apps_=std::move(apps);}PostMessageW(host_,msg_catalog,0,0);});
}
void PaletteWindow::request_icons() {
    for(const auto& row:rows_)if(row.app){const auto app=*row.app;if(app.source!=SearchSource::application || !requested_icons_.insert(app.id).second)continue;
        worker_.enqueue([this,app](std::stop_token stop){if(stop.stop_requested())return;ComApartment apartment;const auto icon=load_app_icon(app);
            {std::lock_guard lock(inbox_mutex_);pending_icons_.emplace_back(app.id,icon);}PostMessageW(host_,msg_icons,0,0);});}
}
void PaletteWindow::schedule_sources() {
    ++source_generation_;source_timer_->stop();source_results_.clear();everything_available_=true;
    const auto trimmed=QString::fromStdWString(query_).trimmed();
    const bool forced=trimmed.startsWith('=') && settings_.search_calculator;
    sources_pending_=!trimmed.isEmpty() && !forced &&
        ((settings_.search_paths && !expand_path_query(query_).empty()) || settings_.search_everything);
    if(sources_pending_)source_timer_->start();
}
void PaletteWindow::request_sources() {
    const auto generation=source_generation_.load();const auto query=query_;const auto settings=settings_;
    source_worker_.enqueue([this,generation,query,settings](std::stop_token stop){
        const auto cancelled=[&]{return stop.stop_requested() || generation!=source_generation_.load();};
        if(cancelled())return;SourceReply reply{generation,{}};
        if(settings.search_paths)reply.entries=path_results(query);
        if(cancelled())return;
        if(settings.search_everything){const auto text=query.starts_with(L"? ")?query.substr(2):query;
            auto files=query_everything(everything_window(),text,cancelled);reply.everything_available=files.available;
            for(auto& entry:files.entries)reply.entries.push_back(std::move(entry));}
        if(cancelled())return;
        {std::lock_guard lock(inbox_mutex_);pending_sources_=std::move(reply);}PostMessageW(host_,msg_sources,0,0);
    });
}
void PaletteWindow::edit_settings() {
    settings_open_=true;unregister_bindings();
    if(!keyboard_->suspend()){hotkey_registered_=register_bindings(settings_);settings_open_=false;set_notice("Could not pause keyboard shortcuts. Try Settings again.");return;}
    Settings draft=settings_;
    if(show_settings_dialog(handle(),draft)) {
        std::wstring error;const bool registered=register_bindings(draft);if(!registered)error=L"That shortcut is already in use. Choose another.";
        bool hook=false;if(registered){hook=keyboard_->start(draft.left_win,draft.right_win,all_hotkeys(draft));if(!hook)error=L"Could not install the Windows-key hook.";}
        const bool login=registered && hook && (draft.start_at_login==settings_.start_at_login || set_start_at_login(draft.start_at_login,error));
        if(login && save_settings(draft,error)){settings_=std::move(draft);hotkey_registered_=true;set_notice({});schedule_sources();update_results();request_catalog();}
        else {unregister_bindings();hotkey_registered_=register_bindings(settings_);keyboard_->start(settings_.left_win,settings_.right_win,all_hotkeys(settings_));
            if(login && draft.start_at_login!=settings_.start_at_login){std::wstring ignored;set_start_at_login(settings_.start_at_login,ignored);}
            QMessageBox::warning(this,"Settings could not be saved",qs(error));}
    } else {hotkey_registered_=register_bindings(settings_);if(!keyboard_->start(settings_.left_win,settings_.right_win,all_hotkeys(settings_)))set_notice("Windows-key hook unavailable. Use the tray or another shortcut.");}
    if(!hotkey_registered_)set_notice("A shortcut is now in use by another app. Open Settings to change it.");
    settings_open_=false;show();
}
bool PaletteWindow::register_bindings(const Settings& settings) {
    for(const auto& binding:all_hotkeys(settings)){if(binding.modifiers&MOD_WIN)continue;
        if(!RegisterHotKey(host_,hotkey_id+static_cast<int>(registered_bindings_),binding.modifiers|MOD_NOREPEAT,binding.key)){unregister_bindings();return false;}++registered_bindings_;}
    return true;
}
void PaletteWindow::unregister_bindings(){for(size_t i=0;i<registered_bindings_;++i)UnregisterHotKey(host_,hotkey_id+static_cast<int>(i));registered_bindings_=0;}
LRESULT CALLBACK PaletteWindow::window_proc(HWND window,UINT message,WPARAM w,LPARAM l) {
    auto* self=reinterpret_cast<PaletteWindow*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<PaletteWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->host_=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    return self?self->message(message,w,l):DefWindowProcW(window,message,w,l);
}
LRESULT PaletteWindow::message(UINT message,WPARAM w,LPARAM l) {
    if(registered_invoke_ && message==registered_invoke_){show();return 0;}
    switch(message){
    case msg_sources:{
        std::optional<SourceReply> reply;{std::lock_guard lock(inbox_mutex_);reply=std::move(pending_sources_);pending_sources_.reset();}
        if(reply && reply->generation==source_generation_.load()){source_results_=std::move(reply->entries);everything_available_=reply->everything_available;sources_pending_=false;update_results(true);}return 0;}
    case msg_invoke:case WM_HOTKEY:if(isVisible()&&!settings_open_)dismiss(true);else show();return 0;
    case msg_exit:qApp->quit();return 0;
    case WM_SETTINGCHANGE:apply_ui_theme();update_icons();return 0;
    case WM_WTSSESSION_CHANGE:
        if(keyboard_ && w==WTS_SESSION_LOCK){dismiss();keyboard_->stop();}
        else if(keyboard_ && !settings_open_ && (w==WTS_SESSION_UNLOCK || w==WTS_CONSOLE_CONNECT || w==WTS_REMOTE_CONNECT)){keyboard_->stop();keyboard_->start(settings_.left_win,settings_.right_win,all_hotkeys(settings_));}return 0;
    case WM_POWERBROADCAST:if(keyboard_ && w==PBT_APMRESUMEAUTOMATIC && !settings_open_){keyboard_->stop();keyboard_->start(settings_.left_win,settings_.right_win,all_hotkeys(settings_));}return TRUE;
    case msg_catalog:{
        {std::lock_guard lock(inbox_mutex_);if(pending_apps_){apps_=std::move(*pending_apps_);pending_apps_.reset();}}
        loading_=false;update_results(true);if(refresh_again_){refresh_again_=false;request_catalog();}return 0;}
    case msg_icons:{
        std::vector<std::pair<std::wstring,HICON>> incoming;{std::lock_guard lock(inbox_mutex_);incoming.swap(pending_icons_);}
        for(auto& [id,icon]:incoming)if(icon){icons_.insert(qs(id),QIcon(QPixmap::fromImage(QImage::fromHICON(icon))));DestroyIcon(icon);}update_icons();return 0;}
    case msg_launch:{
        std::optional<LaunchReply> reply;{std::lock_guard lock(inbox_mutex_);reply=std::move(pending_launch_);pending_launch_.reset();}launching_=false;
        if(reply && reply->ok){for(auto& app:apps_)if(app.id==reply->id){app.use_count=std::min(app.use_count+1,1000000u);app.last_used=static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());const auto updated=app;worker_.enqueue([updated](std::stop_token){save_usage(updated);});break;}set_notice({});dismiss();}
        else if(reply){show();set_notice("Could not open: "+qs(reply->error));}return 0;}
    case msg_shell:{PIDLIST_ABSOLUTE* changed=nullptr;LONG event=0;const auto lock=SHChangeNotification_Lock(reinterpret_cast<HANDLE>(w),static_cast<DWORD>(l),&changed,&event);if(lock)SHChangeNotification_Unlock(lock);refresh_timer_->start();return 0;}
    }
    return DefWindowProcW(host_,message,w,l);
}
}
