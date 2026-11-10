#pragma once
#include "core.hpp"
#include "catalog.hpp"
#include "keyboard.hpp"
#include "settings.hpp"
#include "worker.hpp"
#include <QWidget>
#include <QElapsedTimer>
#include <QIcon>
#include <QImage>
#include <QHash>
#include <optional>
#include <unordered_set>
#include <atomic>
class QLineEdit;
class QListWidget;
class QLabel;
class QToolButton;
class QSystemTrayIcon;
class QTimer;
namespace palette {
class Updater;
class CurrencyService;
inline constexpr wchar_t window_class[]=L"FastPalette.Window";
inline constexpr wchar_t invoke_message[]=L"FastPalette.Invoke.1";
inline constexpr UINT msg_invoke=WM_APP+1,msg_catalog=WM_APP+2,msg_icons=WM_APP+3,
    msg_launch=WM_APP+4,msg_exit=WM_APP+5,msg_cached_catalog=WM_APP+6,msg_shell=WM_APP+7,msg_sources=WM_APP+8;
class PaletteWindow final : public QWidget {
public:
    explicit PaletteWindow(Settings settings=load_settings());
    ~PaletteWindow() override;
    bool create(HINSTANCE instance,bool background);
    HWND handle() const { return reinterpret_cast<HWND>(winId()); }
    void show();
protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* object,QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
private:
    static LRESULT CALLBACK window_proc(HWND,UINT,WPARAM,LPARAM);
    LRESULT message(UINT,WPARAM,LPARAM);
    void update_results(bool preserve=false);
    void update_icons(const std::wstring* changed=nullptr);
    void activate(ResultAction action=ResultAction::open);
    void copy_result_path();
    void show_actions(const QPoint& point);
    void dismiss(bool restore=false);
    void reveal_taskbar();
    void restore_taskbar();
    void request_catalog();
    void request_icons();
    void schedule_sources();
    void request_sources();
    void request_currency();
    void edit_settings();
    bool register_bindings(const Settings& settings);
    void unregister_bindings();
    void set_notice(const QString& text);
    bool clipboard(const std::wstring& text);
    struct Row { std::wstring title,detail; std::optional<AppEntry> app; bool copy=false; std::wstring identity,copy_text; };
    struct SourceReply { std::uint64_t generation; std::vector<AppEntry> entries; bool everything_available=true,pending=false; };
    struct LaunchReply { bool ok; std::wstring id,error; };
    HWND host_=nullptr,previous_=nullptr;
    struct RaisedTaskbar { HWND window; DWORD process; bool topmost; };
    std::vector<RaisedTaskbar> raised_taskbars_;
    QLineEdit* edit_=nullptr;
    QListWidget* list_=nullptr;
    QLabel* footer_=nullptr;
    QLabel* search_icon_=nullptr;
    QToolButton* settings_button_=nullptr;
    QSystemTrayIcon* tray_=nullptr;
    QTimer* refresh_timer_=nullptr;
    QTimer* source_timer_=nullptr;
    QTimer* currency_timer_=nullptr;
    UINT registered_invoke_=0;
    std::vector<ULONG> shell_notifications_;
    QElapsedTimer catalog_age_;
    bool loading_=false,refresh_again_=false,launching_=false,settings_open_=false,hotkey_registered_=false;
    bool actions_open_=false;
    Settings settings_;
    size_t registered_bindings_=0;
    std::unique_ptr<KeyboardHook> keyboard_;
    std::unique_ptr<Updater> updater_;
    std::unique_ptr<CurrencyService> currency_;
    Worker worker_;
    Worker source_worker_;
    Worker catalog_worker_;
    Worker icon_worker_;
    QString cache_directory_;
    bool catalog_cache_loaded_=false;
    std::uint64_t catalog_generation_=0;
    std::atomic<std::uint64_t> icon_generation_{0};
    std::vector<std::wstring> icon_plan_;
    std::vector<AppEntry> warm_apps_;
    QHash<QString,qint64> icon_retry_after_;
    std::unordered_set<std::wstring> validate_icons_;
    std::atomic<std::uint64_t> source_generation_{0};
    std::vector<AppEntry> source_results_;
    bool sources_pending_=false,everything_available_=true;
    std::vector<AppEntry> apps_;
    std::vector<AppEntry> aliases_;
    std::vector<Row> rows_;
    std::wstring query_;
    QHash<QString,QIcon> icons_;

    std::mutex inbox_mutex_;
    std::optional<std::vector<AppEntry>> pending_apps_;
    std::optional<std::vector<AppEntry>> pending_cached_apps_;
    struct IconReply { std::wstring id; QImage image; std::uint64_t catalog; };
    std::vector<IconReply> pending_icons_;
    std::optional<LaunchReply> pending_launch_;
    std::optional<SourceReply> pending_sources_;
};
}
