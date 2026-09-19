#include "settings.hpp"
#include "ui_theme.hpp"
#include "shortcut_capture.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QMouseEvent>
#include <QToolButton>
#include <QWindow>
#include <QTabWidget>
#include <QPointer>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string>

namespace palette {
namespace {

class SettingsDialog;
SettingsDialog* active_recorder = nullptr;
LRESULT CALLBACK shortcut_hook_proc(int code, WPARAM message, LPARAM data);

QString key_name(UINT key) {
    wchar_t name[64]{};
    LONG code = static_cast<LONG>(MapVirtualKeyW(key, MAPVK_VK_TO_VSC) << 16);
    if (key == VK_LEFT || key == VK_RIGHT || key == VK_UP || key == VK_DOWN || key == VK_DELETE ||
        key == VK_INSERT || key == VK_HOME || key == VK_END || key == VK_PRIOR || key == VK_NEXT) code |= 1 << 24;
    if (GetKeyNameTextW(code, name, static_cast<int>(std::size(name)))) return QString::fromWCharArray(name);
    return QStringLiteral("Key %1").arg(key);
}

QString shortcut_text(UINT modifiers, UINT key) {
    QStringList parts;
    if (modifiers & MOD_CONTROL) parts.push_back(QStringLiteral("Ctrl"));
    if (modifiers & MOD_ALT) parts.push_back(QStringLiteral("Alt"));
    if (modifiers & MOD_SHIFT) parts.push_back(QStringLiteral("Shift"));
    if (modifiers & MOD_WIN) parts.push_back(QStringLiteral("Win"));
    if (key) parts.push_back(key_name(key));
    return parts.isEmpty() ? QStringLiteral("Press a shortcut") : parts.join(QStringLiteral(" + "));
}

QFrame* separator(QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setProperty("role", "separator");
    return line;
}

void fit_list_height(QListWidget* list) {
    const int visible_rows = std::clamp(list->count(), 1, 3);
    int row_height = list->sizeHintForRow(0);
    if (row_height <= 0) row_height = list->fontMetrics().height() + 16;
    list->setFixedHeight(std::clamp(10 + visible_rows * row_height, 64, 110));
}

void add_section_heading(QVBoxLayout* layout, QWidget* parent, const QString& title) {
    auto* heading = new QLabel(title, parent);
    heading->setProperty("role", "section");
    layout->addWidget(heading);
}

UINT qt_key_to_vk(const QKeyEvent& event) {
    if (event.nativeVirtualKey() > 0 && event.nativeVirtualKey() < 256)
        return static_cast<UINT>(event.nativeVirtualKey());
    const int key = event.key();
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return static_cast<UINT>('A' + key - Qt::Key_A);
    if (key >= Qt::Key_0 && key <= Qt::Key_9) return static_cast<UINT>('0' + key - Qt::Key_0);
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) return static_cast<UINT>(VK_F1 + key - Qt::Key_F1);
    switch (key) {
    case Qt::Key_Control: return VK_LCONTROL;
    case Qt::Key_Alt: return VK_LMENU;
    case Qt::Key_Shift: return VK_LSHIFT;
    case Qt::Key_Meta: return VK_LWIN;
    case Qt::Key_Escape: return VK_ESCAPE;
    case Qt::Key_Tab: case Qt::Key_Backtab: return VK_TAB;
    case Qt::Key_Return: case Qt::Key_Enter: return VK_RETURN;
    case Qt::Key_Space: return VK_SPACE;
    case Qt::Key_Backspace: return VK_BACK;
    case Qt::Key_Delete: return VK_DELETE;
    case Qt::Key_Insert: return VK_INSERT;
    case Qt::Key_Home: return VK_HOME;
    case Qt::Key_End: return VK_END;
    case Qt::Key_PageUp: return VK_PRIOR;
    case Qt::Key_PageDown: return VK_NEXT;
    case Qt::Key_Left: return VK_LEFT;
    case Qt::Key_Right: return VK_RIGHT;
    case Qt::Key_Up: return VK_UP;
    case Qt::Key_Down: return VK_DOWN;
    default: return 0;
    }
}

class SettingsDialog final : public QDialog {
public:
    SettingsDialog(QWidget* parent, const Settings& settings,const UpdateCheck& check_updates)
        : QDialog(parent), draft_(settings) {
        setWindowTitle(QStringLiteral("Fast Palette settings"));
        setWindowFlag(Qt::FramelessWindowHint);
        setWindowFlag(Qt::WindowMinMaxButtonsHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setModal(true);
        setMinimumWidth(520);
        setMinimumHeight(360);

        drain_timer_.setSingleShot(true);
        drain_timer_.setInterval(1200);
        connect(&drain_timer_, &QTimer::timeout, this, [this] {
            if (capture_.active && capture_.cancelled && capture_.draining) finish_capture();
        });

        auto* shell = new QVBoxLayout(this);
        shell->setContentsMargins(0, 0, 0, 0);
        auto* surface = new QFrame(this);
        surface->setObjectName("settingsSurface");
        shell->addWidget(surface);
        auto* root = new QVBoxLayout(surface);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto* header = new QWidget(surface);
        header->setObjectName("settingsTitleBar");
        header->installEventFilter(this);
        auto* bar = new QHBoxLayout(header);
        header->setFixedHeight(40);
        bar->setContentsMargins(14, 0, 0, 0);
        bar->setSpacing(0);
        auto* logo = new QLabel(header);
        logo->setPixmap(ui_icon(UiIcon::logo).pixmap(16,16));
        logo->setAttribute(Qt::WA_TransparentForMouseEvents);
        bar->addWidget(logo);bar->addSpacing(10);
        auto* title = new QLabel("Fast Palette Settings", header);
        title->setObjectName("settingsCaption");
        title->setAttribute(Qt::WA_TransparentForMouseEvents);
        bar->addWidget(title);
        bar->addStretch();
        auto* minimize = new QToolButton(header);
        minimize->setObjectName("minimizeSettings");
        minimize->setProperty("captionButton",true);
        minimize->setIcon(ui_icon(UiIcon::minimize));minimize->setFixedSize(46,38);
        minimize->setAccessibleName("Minimize");minimize->setToolTip("Minimize");
        bar->addWidget(minimize);
        connect(minimize,&QToolButton::clicked,this,&QWidget::showMinimized);
        maximize_ = new QToolButton(header);
        maximize_->setObjectName("maximizeSettings");
        maximize_->setProperty("captionButton",true);
        maximize_->setIcon(ui_icon(UiIcon::maximize));maximize_->setFixedSize(46,38);
        maximize_->setAccessibleName("Maximize");maximize_->setToolTip("Maximize");
        bar->addWidget(maximize_);
        connect(maximize_,&QToolButton::clicked,this,[this]{toggle_maximized();});
        auto* close = new QToolButton(header);
        close->setObjectName("closeSettings");
        close->setProperty("captionButton",true);close->setFixedSize(46,38);
        close->installEventFilter(this);
        close->setIcon(ui_icon(UiIcon::close));
        close->setAccessibleName("Close settings");
        close->setToolTip("Close");
        bar->addWidget(close);
        connect(close, &QToolButton::clicked, this, &QDialog::reject);
        root->addWidget(header);
        root->addWidget(separator(surface));
        auto* tabs = new QTabWidget(surface);
        tabs->setObjectName("settingsTabs");
        auto* scroll = new QScrollArea(this);
        scroll->setObjectName(QStringLiteral("settingsScrollArea"));
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidgetResizable(true);
        auto* body = new QWidget(scroll);
        auto* content = new QVBoxLayout(body);
        content->setContentsMargins(24, 8, 24, 10);
        content->setSpacing(10);

        add_section_heading(content, body, QStringLiteral("Tap to open"));
        left_win_ = new QCheckBox(QStringLiteral("Left Windows key"), body);
        left_win_->setObjectName(QStringLiteral("leftWin"));
        left_win_->setChecked(draft_.left_win);
        auto* windows_keys = new QHBoxLayout;
        windows_keys->addWidget(left_win_);
        right_win_ = new QCheckBox(QStringLiteral("Right Windows key"), body);
        right_win_->setObjectName(QStringLiteral("rightWin"));
        right_win_->setChecked(draft_.right_win);
        windows_keys->addWidget(right_win_);
        content->addLayout(windows_keys);
        content->addWidget(separator(body));

        add_section_heading(content, body, QStringLiteral("Shortcuts"));
        shortcut_list_ = new QListWidget(body);
        shortcut_list_->setObjectName(QStringLiteral("shortcutList"));
        shortcut_list_->setSelectionMode(QAbstractItemView::SingleSelection);
        shortcut_list_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        content->addWidget(shortcut_list_);

        auto* shortcut_row = new QHBoxLayout;
        shortcut_row->setSpacing(8);
        recorder_ = new QLineEdit(body);
        recorder_->setObjectName(QStringLiteral("shortcutRecorder"));
        recorder_->setReadOnly(true);
        recorder_->setPlaceholderText("Record shortcut");
        recorder_->setAccessibleName(QStringLiteral("Shortcut recorder"));
        recorder_->installEventFilter(this);
        shortcut_row->addWidget(recorder_, 1);
        add_shortcut_ = new QPushButton(QStringLiteral("Add"), body);
        add_shortcut_->setObjectName(QStringLiteral("addShortcut"));
        shortcut_row->addWidget(add_shortcut_);
        remove_shortcut_ = new QPushButton(QStringLiteral("Remove"), body);
        remove_shortcut_->setObjectName(QStringLiteral("removeShortcut"));
        shortcut_row->addWidget(remove_shortcut_);
        content->addLayout(shortcut_row);

        status_ = new QLabel(body);
        status_->setProperty("role", "error");
        status_->setWordWrap(true);
        status_->hide();
        content->addWidget(status_);
        content->addWidget(separator(body));

        start_at_login_ = new QCheckBox(QStringLiteral("Launch at sign-in"), body);
        start_at_login_->setObjectName(QStringLiteral("startAtLogin"));
        start_at_login_->setChecked(draft_.start_at_login);
        content->addWidget(start_at_login_);
        content->addWidget(separator(body));

        add_section_heading(content, body, QStringLiteral("Portable apps"));
        portable_apps_ = new QListWidget(body);
        portable_apps_->setObjectName(QStringLiteral("portableApps"));
        portable_apps_->setSelectionMode(QAbstractItemView::SingleSelection);
        portable_apps_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        content->addWidget(portable_apps_);

        auto* portable_actions = new QHBoxLayout;
        portable_actions->addStretch();
        auto* add_app = new QPushButton(QStringLiteral("Add"), body);
        add_app->setObjectName(QStringLiteral("addPortableApp"));
        portable_actions->addWidget(add_app);
        remove_app_ = new QPushButton(QStringLiteral("Remove"), body);
        remove_app_->setObjectName(QStringLiteral("removePortableApp"));
        portable_actions->addWidget(remove_app_);
        content->addLayout(portable_actions);
        content->addStretch();

        scroll->setWidget(body);
        tabs->addTab(scroll,"General");
        auto* search_page = new QWidget(tabs);
        auto* search_layout = new QVBoxLayout(search_page);
        search_layout->setContentsMargins(24,16,24,16);search_layout->setSpacing(10);
        const char* names[]={"searchApps","searchCalculator","searchSettings","searchPaths","searchEverything","searchConversions","searchAliases","searchCurrency"};
        const char* labels[]={"Applications","Calculator","Windows Settings","Paths","Everything","Unit conversions","Aliases","Currency"};
        for(size_t i=0;i<module_count;++i){
            const auto module=static_cast<Module>(i);const auto rule=module_rule(draft_,module);
            auto* row=new QHBoxLayout;
            module_enabled_[i]=new QCheckBox(labels[i],search_page);module_enabled_[i]->setObjectName(names[i]);
            module_enabled_[i]->setChecked(module_enabled(draft_,module));row->addWidget(module_enabled_[i],1);
            module_prefix_[i]=new QLineEdit(QString::fromStdWString(rule.prefix),search_page);
            module_prefix_[i]->setObjectName(QString::fromWCharArray(module_keys[i]).toLower()+"Prefix");
            module_prefix_[i]->setPlaceholderText("Prefix");module_prefix_[i]->setMaxLength(16);module_prefix_[i]->setFixedWidth(90);
            module_prefix_[i]->setAccessibleName(QString(labels[i])+" prefix");
            module_prefix_[i]->setToolTip("Follow the prefix with a space to search only this module.");row->addWidget(module_prefix_[i]);
            module_only_[i]=new QCheckBox("Prefix only",search_page);module_only_[i]->setChecked(rule.only);
            module_only_[i]->setObjectName(QString::fromWCharArray(module_keys[i]).toLower()+"PrefixOnly");
            module_only_[i]->setAccessibleName(QString(labels[i])+" only with prefix");row->addWidget(module_only_[i]);
            auto enable=[this,i](bool value){module_prefix_[i]->setEnabled(value);module_only_[i]->setEnabled(value);};
            connect(module_enabled_[i],&QCheckBox::toggled,this,enable);enable(module_enabled_[i]->isChecked());
            if(module==Module::currency)module_enabled_[i]->setToolTip("Fiat and crypto conversions using daily online rates. Cached rates work offline.");
            search_layout->addLayout(row);
        }
        everything_prefix_=module_prefix_[static_cast<size_t>(Module::everything)];
        everything_prefix_only_=module_only_[static_cast<size_t>(Module::everything)];
        search_layout->addWidget(separator(search_page));
        degrees_=new QCheckBox("Use degrees for trigonometry",search_page);degrees_->setObjectName("calculatorDegrees");
        degrees_->setChecked(draft_.calculator_degrees);search_layout->addWidget(degrees_);
        degrees_->setEnabled(draft_.search_calculator);
        connect(module_enabled_[static_cast<size_t>(Module::calculator)],&QCheckBox::toggled,degrees_,&QWidget::setEnabled);
        search_layout->addStretch();
        auto* search_scroll=new QScrollArea(tabs);search_scroll->setObjectName("moduleScrollArea");
        search_scroll->setWidgetResizable(true);search_scroll->setFrameShape(QFrame::NoFrame);search_scroll->setWidget(search_page);
        tabs->addTab(search_scroll,"Search");

        auto* aliases_page=new QWidget(tabs);auto* aliases_layout=new QVBoxLayout(aliases_page);
        aliases_layout->setContentsMargins(24,16,24,16);aliases_layout->setSpacing(10);
        aliases_list_=new QListWidget(aliases_page);aliases_list_->setObjectName("aliasesList");aliases_layout->addWidget(aliases_list_,1);
        for(const auto& alias:draft_.aliases)aliases_list_->addItem(QString::fromStdWString(alias.name));
        alias_name_=new QLineEdit(aliases_page);alias_name_->setObjectName("aliasName");alias_name_->setPlaceholderText("Name");alias_name_->setAccessibleName("Alias name");alias_name_->setMaxLength(64);
        alias_target_=new QLineEdit(aliases_page);alias_target_->setObjectName("aliasTarget");alias_target_->setPlaceholderText("Application or folder path");alias_target_->setAccessibleName("Alias target");
        alias_arguments_=new QLineEdit(aliases_page);alias_arguments_->setObjectName("aliasArguments");alias_arguments_->setPlaceholderText("Arguments (optional)");alias_arguments_->setAccessibleName("Alias arguments");
        aliases_layout->addWidget(alias_name_);aliases_layout->addWidget(alias_target_);aliases_layout->addWidget(alias_arguments_);
        auto* alias_buttons=new QHBoxLayout;auto* add_alias=new QPushButton("Add",aliases_page);add_alias->setObjectName("addAlias");
        auto* update_alias=new QPushButton("Update",aliases_page);update_alias->setObjectName("updateAlias");
        auto* remove_alias=new QPushButton("Remove",aliases_page);remove_alias->setObjectName("removeAlias");
        alias_buttons->addStretch();alias_buttons->addWidget(add_alias);alias_buttons->addWidget(update_alias);alias_buttons->addWidget(remove_alias);aliases_layout->addLayout(alias_buttons);
        auto* alias_error=new QLabel(aliases_page);alias_error->setProperty("role","error");alias_error->setWordWrap(true);alias_error->hide();aliases_layout->addWidget(alias_error);
        connect(aliases_list_,&QListWidget::currentRowChanged,this,[this,update_alias,remove_alias](int selected){
            const bool valid=selected>=0 && static_cast<size_t>(selected)<draft_.aliases.size();update_alias->setEnabled(valid);remove_alias->setEnabled(valid);
            if(valid){const auto& alias=draft_.aliases[selected];alias_name_->setText(QString::fromStdWString(alias.name));alias_target_->setText(QString::fromStdWString(alias.target));alias_arguments_->setText(QString::fromStdWString(alias.arguments));}
        });
        update_alias->setEnabled(false);remove_alias->setEnabled(false);
        const auto store_alias=[this,alias_error](bool update){
            Settings candidate=draft_;Alias alias{alias_name_->text().trimmed().toStdWString(),alias_target_->text().trimmed().toStdWString(),alias_arguments_->text().toStdWString()};
            const int selected=aliases_list_->currentRow();
            if(update){if(selected<0)return;candidate.aliases[selected]=alias;}else candidate.aliases.push_back(alias);
            std::wstring error;if(!valid_module_settings(candidate,error)){alias_error->setText(QString::fromStdWString(error));alias_error->show();return;}
            draft_.aliases=std::move(candidate.aliases);alias_error->hide();aliases_list_->clear();
            for(const auto& entry:draft_.aliases)aliases_list_->addItem(QString::fromStdWString(entry.name));
            aliases_list_->setCurrentRow(update?selected:static_cast<int>(draft_.aliases.size()-1));
        };
        connect(add_alias,&QPushButton::clicked,this,[store_alias]{store_alias(false);});
        connect(update_alias,&QPushButton::clicked,this,[store_alias]{store_alias(true);});
        connect(remove_alias,&QPushButton::clicked,this,[this,alias_error]{const int selected=aliases_list_->currentRow();if(selected<0)return;
            draft_.aliases.erase(draft_.aliases.begin()+selected);delete aliases_list_->takeItem(selected);alias_error->hide();
            if(draft_.aliases.empty()){alias_name_->clear();alias_target_->clear();alias_arguments_->clear();}
        });
        tabs->addTab(aliases_page,"Aliases");
        auto* updates_page=new QWidget(tabs);auto* updates_layout=new QVBoxLayout(updates_page);
        updates_layout->setContentsMargins(24,16,24,16);updates_layout->setSpacing(16);
        auto* version_label=new QLabel("Fast Palette " FAST_PALETTE_VERSION,updates_page);updates_layout->addWidget(version_label);
        automatic_updates_=new QCheckBox("Automatically install updates",updates_page);
        automatic_updates_->setObjectName("automaticUpdates");automatic_updates_->setChecked(draft_.automatic_updates);
        automatic_updates_->setToolTip("Checks every six hours and restarts when the palette and Settings are closed.");
        updates_layout->addWidget(automatic_updates_);
        auto* update_button=new QPushButton("Check for updates",updates_page);update_button->setObjectName("checkUpdates");
        update_button->setEnabled(static_cast<bool>(check_updates));updates_layout->addWidget(update_button,0,Qt::AlignLeft);
        connect(update_button,&QPushButton::clicked,this,[check_updates,button=QPointer<QPushButton>(update_button)]{
            if(!check_updates)return;button->setEnabled(false);button->setText("Checking...");
            check_updates([button]{if(button){button->setEnabled(true);button->setText("Check for updates");}});
        });
        updates_layout->addStretch();tabs->addTab(updates_page,"Updates");root->addWidget(tabs,1);

        auto* footer = new QWidget(this);
        auto* footer_layout = new QVBoxLayout(footer);
        footer_layout->setContentsMargins(24, 10, 24, 20);
        footer_layout->addWidget(separator(footer));
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, footer);
        save_button_ = buttons->button(QDialogButtonBox::Save);
        cancel_button_ = buttons->button(QDialogButtonBox::Cancel);
        save_button_->setObjectName(QStringLiteral("saveButton"));
        cancel_button_->setObjectName(QStringLiteral("cancelButton"));
        save_button_->setDefault(true);
        const auto validate_prefixes=[this]{Settings candidate=draft_;for(size_t i=0;i<module_count;++i)set_module_rule(candidate,static_cast<Module>(i),{module_prefix_[i]->text().toStdWString(),module_only_[i]->isChecked()});
            std::wstring error;const bool valid=valid_module_settings(candidate,error);save_button_->setEnabled(valid);save_button_->setToolTip(valid?QString{}:QString::fromStdWString(error));};
        for(size_t i=0;i<module_count;++i){connect(module_prefix_[i],&QLineEdit::textChanged,this,validate_prefixes);connect(module_only_[i],&QCheckBox::toggled,this,validate_prefixes);}
        footer_layout->addWidget(buttons);
        root->addWidget(footer);
        setSizeGripEnabled(true);

        const int available_height = screen() ? screen()->availableGeometry().height() : 760;
        resize(600, std::max(420, std::min(640, available_height - 48)));

        capture_modifiers_ = 0;
        capture_key_ = 0;
        sync_recorder_text();
        refresh_shortcuts();
        refresh_portable_apps();

        connect(add_shortcut_, &QPushButton::clicked, this, [this] { add_shortcut(); });
        connect(remove_shortcut_, &QPushButton::clicked, this, [this] { remove_shortcut(); });
        connect(add_app, &QPushButton::clicked, this, [this] { add_portable_app(); });
        connect(remove_app_, &QPushButton::clicked, this, [this] { remove_portable_app(); });
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    ~SettingsDialog() override {
        drain_timer_.stop();
        if (hook_) UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
        if (active_recorder == this) active_recorder = nullptr;
        capture_.active = false;
    }

    Settings take_settings() { return std::move(draft_); }

    bool process_hook_key(UINT key, bool down) {
        const auto result = capture_.event(key, down);
        if (down && key == VK_ESCAPE && result.swallow) escape_cancelled_ = true;
        if (result.changed) QTimer::singleShot(0, this, [this] { capture_changed(); });
        if (result.finished) QTimer::singleShot(0, this, [this] { finish_capture(); });
        return result.swallow;
    }

protected:
    bool nativeEvent(const QByteArray& type,void* native_message,qintptr* result) override {
        auto* message=static_cast<MSG*>(native_message);
        if(message->message==WM_NCHITTEST && !isMaximized()){
            const QPoint point=mapFromGlobal(QPoint(GET_X_LPARAM(message->lParam),GET_Y_LPARAM(message->lParam)));
            const bool left=point.x()<6,right=point.x()>=width()-6,top=point.y()<6,bottom=point.y()>=height()-6;
            if(rect().contains(point) && (left || right || top || bottom)){
                *result=top?(left?HTTOPLEFT:right?HTTOPRIGHT:HTTOP):bottom?(left?HTBOTTOMLEFT:right?HTBOTTOMRIGHT:HTBOTTOM):left?HTLEFT:HTRIGHT;
                return true;
            }
        }
        return QDialog::nativeEvent(type,native_message,result);
    }
    bool eventFilter(QObject* watched, QEvent* event) override {
        if(watched->objectName()=="settingsTitleBar" && event->type()==QEvent::MouseButtonDblClick){
            if(static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton){toggle_maximized();return true;}}
        if (watched->objectName() == "closeSettings" && event->type() == QEvent::KeyPress) {
            const auto key = static_cast<QKeyEvent*>(event)->key();
            if (key == Qt::Key_Return || key == Qt::Key_Enter) { reject(); return true; }
        }
        if (watched->objectName() == "settingsTitleBar" && event->type() == QEvent::MouseButtonPress) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton && windowHandle()) {
                windowHandle()->startSystemMove();
                return true;
            }
        }
        if (watched != recorder_) return QDialog::eventFilter(watched, event);
        if (event->type() == QEvent::FocusIn) {
            start_capture();
        } else if (event->type() == QEvent::FocusOut) {
            request_capture_stop();
        } else if (event->type() == QEvent::MouseButtonPress && !capture_.active) {
            QTimer::singleShot(0, this, [this] { if (recorder_->hasFocus()) start_capture(); });
        } else if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
            auto* key_event = static_cast<QKeyEvent*>(event);
            const bool down = event->type() == QEvent::KeyPress;
            if (!capture_.active) {
                if (!event->spontaneous() && down) {
                    capture_.begin();
                    escape_cancelled_ = false;
                    sync_recorder_text();
                } else if (event->spontaneous() && !down) {
                    QTimer::singleShot(0, this, [this] { if (recorder_->hasFocus()) start_capture(); });
                    return true;
                } else {
                    return true;
                }
            }
            const UINT key = qt_key_to_vk(*key_event);
            if (key) {
                const auto result = capture_.event(key, down);
                if (down && key == VK_ESCAPE && result.swallow) escape_cancelled_ = true;
                if (result.changed) capture_changed();
                if (result.finished) QTimer::singleShot(0, this, [this] { finish_capture(); });
            }
            return true;
        }
        return QDialog::eventFilter(watched, event);
    }

    void changeEvent(QEvent* event) override {
        if (event->type() == QEvent::ActivationChange && !isActiveWindow()) request_capture_stop();
        if(event->type()==QEvent::WindowStateChange && maximize_){
            maximize_->setIcon(ui_icon(isMaximized()?UiIcon::restore:UiIcon::maximize));
            maximize_->setAccessibleName(isMaximized()?"Restore":"Maximize");maximize_->setToolTip(maximize_->accessibleName());}
        QDialog::changeEvent(event);
    }

    void done(int result) override {
        if (capture_.active) {
            pending_result_ = result;
            request_capture_stop();
            return;
        }
        finish_dialog(result);
    }

private:
    void toggle_maximized(){if(isMaximized())showNormal();else showMaximized();}
    void set_status(const QString& text) {
        status_->setText(text);
        status_->setVisible(!text.isEmpty());
    }

    void sync_recorder_text() {
        if (capture_.active && !capture_.completed) recorder_->setText(QStringLiteral("Press a shortcut"));
        else if (capture_.active) recorder_->setText(shortcut_text(capture_.modifiers, capture_.key));
        else recorder_->setText(capture_key_ ? shortcut_text(capture_modifiers_, capture_key_) : QString{});
        add_shortcut_->setEnabled(capture_key_ != 0 && !capture_.active);
    }

    bool start_capture() {
        if (capture_.active) return true;
        std::array<bool, 256> held{};
        for (std::size_t vk = VK_BACK; vk < held.size(); ++vk)
            held[vk] = (GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;
        if (std::find(held.begin() + VK_BACK, held.end(), true) != held.end()) {
            recorder_->setText(QStringLiteral("Release held keys, then focus the field again"));
            return false;
        }
        capture_.begin(held);
        escape_cancelled_ = false;
        active_recorder = this;
        hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, shortcut_hook_proc, GetModuleHandleW(nullptr), 0);
        if (!hook_) {
            active_recorder = nullptr;
            capture_.active = false;
            set_status(QStringLiteral("Shortcut recording is unavailable. Reopen Settings and try again."));
            sync_recorder_text();
            return false;
        }
        set_status({});
        sync_recorder_text();
        return true;
    }

    void capture_changed() {
        sync_recorder_text();
        if (capture_.cancelled) drain_timer_.start();
    }

    void request_capture_stop() {
        if (!capture_.active) return;
        if (capture_.request_stop()) finish_capture();
        else drain_timer_.start();
    }

    void finish_capture() {
        if (!capture_.active) return;
        drain_timer_.stop();
        const bool keep = capture_.completed && !capture_.cancelled;
        if (keep) {
            capture_modifiers_ = capture_.modifiers;
            capture_key_ = capture_.key;
        }
        if (hook_) UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
        if (active_recorder == this) active_recorder = nullptr;
        capture_.active = false;
        sync_recorder_text();

        const bool focus_add = keep || escape_cancelled_;
        escape_cancelled_ = false;
        if (pending_result_) {
            const int result = *pending_result_;
            pending_result_.reset();
            QTimer::singleShot(0, this, [this, result] { finish_dialog(result); });
        } else if (focus_add) {
            if (add_shortcut_->isEnabled()) add_shortcut_->setFocus(Qt::OtherFocusReason);
            else shortcut_list_->setFocus(Qt::OtherFocusReason);
        }
    }

    void finish_dialog(int result) {
        if (result == QDialog::Accepted) {
            draft_.left_win = left_win_->isChecked();
            draft_.right_win = right_win_->isChecked();
            draft_.start_at_login = start_at_login_->isChecked();
            draft_.automatic_updates=automatic_updates_->isChecked();
            bool* enabled[]={&draft_.search_apps,&draft_.search_calculator,&draft_.search_settings,&draft_.search_paths,&draft_.search_everything,&draft_.search_conversions,&draft_.search_aliases,&draft_.search_currency};
            for(size_t i=0;i<module_count;++i){*enabled[i]=module_enabled_[i]->isChecked();set_module_rule(draft_,static_cast<Module>(i),{module_prefix_[i]->text().toStdWString(),module_only_[i]->isChecked()});}
            draft_.calculator_degrees=degrees_->isChecked();
        }
        QDialog::done(result);
    }

    void refresh_shortcuts(int selected = 0) {
        shortcut_list_->clear();
        const auto bindings = all_hotkeys(draft_);
        for (const auto& binding : bindings)
            shortcut_list_->addItem(shortcut_text(binding.modifiers, binding.key));
        if (!bindings.empty()) shortcut_list_->setCurrentRow(std::clamp(selected, 0, static_cast<int>(bindings.size() - 1)));
        fit_list_height(shortcut_list_);
        remove_shortcut_->setEnabled(bindings.size() > 1);
    }

    void refresh_portable_apps(int selected = 0) {
        portable_apps_->clear();
        portable_apps_->setVisible(!draft_.portable_apps.empty());
        if (draft_.portable_apps.empty()) {
            fit_list_height(portable_apps_);
            remove_app_->setEnabled(false);
            return;
        }
        for (const auto& path : draft_.portable_apps)
            portable_apps_->addItem(QString::fromStdWString(path));
        portable_apps_->setCurrentRow(std::clamp(selected, 0, static_cast<int>(draft_.portable_apps.size() - 1)));
        fit_list_height(portable_apps_);
        remove_app_->setEnabled(true);
    }

    void add_shortcut() {
        const Hotkey binding{capture_modifiers_, capture_key_};
        if (!valid_hotkey(binding.modifiers, binding.key)) {
            set_status(QStringLiteral("Choose an available key with Ctrl, Alt, Shift, or Win."));
            recorder_->setFocus(Qt::OtherFocusReason);
            return;
        }
        const auto bindings = all_hotkeys(draft_);
        if (std::find(bindings.begin(), bindings.end(), binding) != bindings.end()) {
            set_status(QStringLiteral("That shortcut is already in the list."));
            recorder_->setFocus(Qt::OtherFocusReason);
            return;
        }
        if (bindings.size() >= 16) {
            set_status(QStringLiteral("Fast Palette supports up to 16 keyboard shortcuts."));
            return;
        }
        draft_.extra_bindings.push_back(binding);
        refresh_shortcuts(static_cast<int>(bindings.size()));
        capture_modifiers_ = 0;
        capture_key_ = 0;
        sync_recorder_text();
        set_status({});
    }

    void remove_shortcut() {
        const int selected = shortcut_list_->currentRow();
        if (selected < 0) return;
        if (draft_.extra_bindings.empty()) {
            set_status(QStringLiteral("Keep at least one shortcut. Add a replacement first."));
            return;
        }
        if (selected == 0) {
            draft_.modifiers = draft_.extra_bindings.front().modifiers;
            draft_.key = draft_.extra_bindings.front().key;
            draft_.extra_bindings.erase(draft_.extra_bindings.begin());
        } else if (static_cast<std::size_t>(selected) <= draft_.extra_bindings.size()) {
            draft_.extra_bindings.erase(draft_.extra_bindings.begin() + selected - 1);
        }
        capture_modifiers_ = 0;
        capture_key_ = 0;
        sync_recorder_text();
        refresh_shortcuts(std::max(0, selected - 1));
        set_status({});
    }

    void add_portable_app() {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Add portable application"), {},
            QStringLiteral("Applications (*.exe)"));
        if (path.isEmpty()) return;
        const std::wstring entry = path.toStdWString();
        if (std::find(draft_.portable_apps.begin(), draft_.portable_apps.end(), entry) == draft_.portable_apps.end()) {
            draft_.portable_apps.push_back(entry);
            refresh_portable_apps(static_cast<int>(draft_.portable_apps.size() - 1));
        }
    }

    void remove_portable_app() {
        const int selected = portable_apps_->currentRow();
        if (selected < 0 || static_cast<std::size_t>(selected) >= draft_.portable_apps.size()) return;
        draft_.portable_apps.erase(draft_.portable_apps.begin() + selected);
        refresh_portable_apps(std::max(0, selected - 1));
    }

    Settings draft_;
    QCheckBox* left_win_ = nullptr;
    QCheckBox* right_win_ = nullptr;
    QListWidget* shortcut_list_ = nullptr;
    QLineEdit* recorder_ = nullptr;
    QPushButton* add_shortcut_ = nullptr;
    QPushButton* remove_shortcut_ = nullptr;
    QLabel* status_ = nullptr;
    QCheckBox* start_at_login_ = nullptr;
    std::array<QCheckBox*,module_count> module_enabled_{},module_only_{};
    std::array<QLineEdit*,module_count> module_prefix_{};
    QCheckBox* degrees_=nullptr;
    QListWidget* aliases_list_=nullptr;
    QLineEdit *alias_name_=nullptr,*alias_target_=nullptr,*alias_arguments_=nullptr;
    QToolButton* maximize_=nullptr;
    QCheckBox* automatic_updates_=nullptr;
    QCheckBox* everything_prefix_only_=nullptr;
    QLineEdit* everything_prefix_=nullptr;
    QListWidget* portable_apps_ = nullptr;
    QPushButton* remove_app_ = nullptr;
    QPushButton* save_button_ = nullptr;
    QPushButton* cancel_button_ = nullptr;
    shortcut_capture::State capture_;
    QTimer drain_timer_;
    HHOOK hook_ = nullptr;
    UINT capture_modifiers_ = 0;
    UINT capture_key_ = 0;
    bool escape_cancelled_ = false;
    std::optional<int> pending_result_;
};

LRESULT CALLBACK shortcut_hook_proc(int code, WPARAM message, LPARAM data) {
    if (code < 0 || !active_recorder) return CallNextHookEx(nullptr, code, message, data);
    const auto* key = reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
    const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
    if (!down && !up) return CallNextHookEx(nullptr, code, message, data);
    return active_recorder->process_hook_key(key->vkCode, down) ? 1 : CallNextHookEx(nullptr, code, message, data);
}

} // namespace

bool show_settings_dialog(HWND owner, Settings& settings,const UpdateCheck& check_updates) {
    QWidget* parent = owner ? QWidget::find(reinterpret_cast<WId>(owner)) : nullptr;
    SettingsDialog dialog(parent, settings,check_updates);
    if (dialog.exec() != QDialog::Accepted) return false;
    settings = dialog.take_settings();
    return true;
}

} // namespace palette
