#include "settings.hpp"
#include "ui_theme.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QWidget>
#include <QToolButton>
#include <QTabWidget>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool same_settings(const palette::Settings& left, const palette::Settings& right) {
    return left.left_win == right.left_win && left.right_win == right.right_win &&
        left.start_at_login == right.start_at_login && left.modifiers == right.modifiers &&
        left.key == right.key && left.extra_bindings == right.extra_bindings &&
        left.portable_apps == right.portable_apps && left.search_apps==right.search_apps &&
        left.search_calculator==right.search_calculator && left.search_settings==right.search_settings &&
        left.search_paths==right.search_paths && left.search_everything==right.search_everything &&
        left.everything_prefix_only==right.everything_prefix_only && left.everything_prefix==right.everything_prefix && left.automatic_updates==right.automatic_updates && left.search_conversions==right.search_conversions && left.search_aliases==right.search_aliases && left.search_currency==right.search_currency && left.calculator_degrees==right.calculator_degrees && left.prefixes==right.prefixes && left.aliases==right.aliases;
}

template <typename T>
T* child(QWidget* dialog, const char* name) {
    auto* value = dialog->findChild<T*>(QString::fromLatin1(name));
    require(value != nullptr, name);
    return value;
}

void with_dialog(const std::function<void(QWidget*)>& action) {
    QTimer::singleShot(0, [action] {
        QWidget* dialog = QApplication::activeModalWidget();
        require(dialog != nullptr, "active modal settings dialog");
        action(dialog);
    });
}

void send_key(QWidget* target, QEvent::Type type, int key, Qt::KeyboardModifiers modifiers) {
    QKeyEvent event(type, key, modifiers);
    QApplication::sendEvent(target, &event);
}

void record_win_r(QLineEdit* recorder) {
    send_key(recorder, QEvent::KeyPress, Qt::Key_Meta, Qt::MetaModifier);
    send_key(recorder, QEvent::KeyPress, Qt::Key_R, Qt::MetaModifier);
    send_key(recorder, QEvent::KeyRelease, Qt::Key_R, Qt::MetaModifier);
    send_key(recorder, QEvent::KeyRelease, Qt::Key_Meta, Qt::NoModifier);
}

QString preview_path() {
    QDir output(QApplication::applicationDirPath());
    const QString configuration = output.dirName();
    if (configuration.compare(QStringLiteral("Debug"), Qt::CaseInsensitive) == 0 ||
        configuration.compare(QStringLiteral("Release"), Qt::CaseInsensitive) == 0 ||
        configuration.compare(QStringLiteral("RelWithDebInfo"), Qt::CaseInsensitive) == 0 ||
        configuration.compare(QStringLiteral("MinSizeRel"), Qt::CaseInsensitive) == 0) {
        output.cdUp();
    }
    return output.absoluteFilePath(QStringLiteral("settings-preview.png"));
}

void test_visual_snapshot() {
    palette::Settings value;

    with_dialog([](QWidget* dialog) {
        QTimer::singleShot(100, dialog, [dialog] {
            dialog->resize(560, 400);
            QApplication::processEvents();
            const auto* scroll = child<QScrollArea>(dialog, "settingsScrollArea");
            require(scroll->verticalScrollBar()->maximum() > 0,
                "short settings dialog scrolls its body");
            for (const char* name : {"saveButton", "cancelButton"}) {
                const auto* button = child<QPushButton>(dialog, name);
                const QRect button_rect(button->mapTo(dialog, QPoint{}), button->size());
                require(dialog->rect().contains(button_rect), "short settings dialog keeps footer buttons visible");
            }
            dialog->resize(600, 640);
            QApplication::processEvents();
            require(scroll->verticalScrollBar()->maximum() == 0,
                "normal settings dialog shows its full body without scrolling");
            for (const char* name : {"addPortableApp", "removePortableApp"}) {
                const auto* button = child<QPushButton>(dialog, name);
                const QRect button_rect(button->mapTo(dialog, QPoint{}), button->size());
                require(dialog->rect().contains(button_rect),
                    "normal settings dialog keeps portable actions visible");
            }
            require(dialog->grab().save(preview_path(), "PNG"), "settings preview screenshot is written");
            child<QTabWidget>(dialog,"settingsTabs")->setCurrentIndex(1);
            QApplication::processEvents();
            require(dialog->grab().save(preview_path().replace("settings-preview","search-settings-preview"),"PNG"),"search settings preview");
            dialog->resize(600,400);QApplication::processEvents();
            require(child<QScrollArea>(dialog,"moduleScrollArea")->verticalScrollBar()->maximum()>0,"module settings scroll in a short window");
            dialog->resize(600,640);QApplication::processEvents();
            child<QToolButton>(dialog,"maximizeSettings")->click();
            require(dialog->isMaximized(),"maximize caption button works");
            child<QToolButton>(dialog,"maximizeSettings")->click();
            require(!dialog->isMaximized(),"restore caption button works");
            require(dialog->windowFlags().testFlag(Qt::FramelessWindowHint), "custom window bar replaces native caption");
            child<QToolButton>(dialog, "closeSettings")->click();
        });
    });

    require(!palette::show_settings_dialog(nullptr, value), "preview dialog closes with Cancel");
}

void test_cancel_discards_staged_changes() {
    palette::Settings initial;
    initial.portable_apps = {L"C:\\Tools\\One.exe"};
    palette::Settings value = initial;

    with_dialog([](QWidget* dialog) {
        child<QCheckBox>(dialog, "leftWin")->setChecked(false);
        child<QCheckBox>(dialog, "rightWin")->setChecked(false);
        child<QCheckBox>(dialog, "startAtLogin")->setChecked(true);
        child<QCheckBox>(dialog,"searchEverything")->setChecked(true);
        child<QCheckBox>(dialog,"everythingPrefixOnly")->setChecked(true);
        child<QLineEdit>(dialog,"everythingPrefix")->setText("ef");
        child<QCheckBox>(dialog,"searchApps")->setChecked(false);
        record_win_r(child<QLineEdit>(dialog, "shortcutRecorder"));
        QTimer::singleShot(10, dialog, [dialog] {
            child<QPushButton>(dialog, "addShortcut")->click();
            require(child<QListWidget>(dialog, "shortcutList")->count() == 2,
                "shortcut is staged before Cancel");
            child<QPushButton>(dialog, "cancelButton")->click();
        });
    });

    require(!palette::show_settings_dialog(nullptr, value), "Cancel returns false");
    require(same_settings(value, initial), "Cancel leaves caller settings unchanged");
}

void test_save_applies_toggles_and_win_r() {
    palette::Settings value;

    with_dialog([](QWidget* dialog) {
        child<QCheckBox>(dialog,"automaticUpdates")->setChecked(false);
        child<QCheckBox>(dialog,"searchConversions")->setChecked(false);
        child<QCheckBox>(dialog,"searchAliases")->setChecked(false);
        child<QCheckBox>(dialog,"searchCurrency")->setChecked(false);
        child<QCheckBox>(dialog,"calculatorDegrees")->setChecked(true);
        child<QCheckBox>(dialog,"everythingPrefixOnly")->setChecked(true);
        child<QLineEdit>(dialog,"everythingPrefix")->setText("");
        require(!child<QPushButton>(dialog,"saveButton")->isEnabled(),"empty prefix cannot be saved");
        child<QLineEdit>(dialog,"everythingPrefix")->setText("ef");
        child<QCheckBox>(dialog,"searchApps")->setChecked(false);
        child<QCheckBox>(dialog,"searchCalculator")->setChecked(false);
        child<QCheckBox>(dialog,"searchSettings")->setChecked(false);
        child<QCheckBox>(dialog,"searchPaths")->setChecked(false);
        child<QCheckBox>(dialog,"searchEverything")->setChecked(true);
        child<QCheckBox>(dialog, "leftWin")->setChecked(false);
        child<QCheckBox>(dialog, "rightWin")->setChecked(true);
        child<QCheckBox>(dialog, "startAtLogin")->setChecked(true);
        record_win_r(child<QLineEdit>(dialog, "shortcutRecorder"));
        QTimer::singleShot(10, dialog, [dialog] {
            auto* recorder = child<QLineEdit>(dialog, "shortcutRecorder");
            require(recorder->text().contains(QStringLiteral("Win")) &&
                    recorder->text().contains(QStringLiteral("R")),
                "recorder exposes Win+R as accessible text");
            child<QPushButton>(dialog, "addShortcut")->click();
            require(child<QListWidget>(dialog, "shortcutList")->count() == 2,
                "Win+R is added to shortcut list");
            child<QPushButton>(dialog, "saveButton")->click();
        });
    });

    require(palette::show_settings_dialog(nullptr, value), "Save returns true");
    require(!value.left_win && value.right_win && value.start_at_login,
        "Save applies all staged toggles");
    require(!value.search_apps && !value.search_calculator && !value.search_settings && !value.search_paths && value.search_everything,"Save applies each source toggle");
    require(value.everything_prefix_only && value.everything_prefix==L"ef","Save applies Everything prefix options");
    require(!value.automatic_updates,"Save applies automatic update preference");
    require(!value.search_conversions && !value.search_aliases && !value.search_currency && value.calculator_degrees,"Save applies new module and angle settings");
    require(value.extra_bindings.size() == 1 && value.extra_bindings.front() == palette::Hotkey{MOD_WIN, 'R'},
        "Save applies recorded Win+R binding");
}

void test_escape_cancels_recording_not_dialog() {
    palette::Settings initial;
    palette::Settings value = initial;
    bool remained_open = false;

    with_dialog([&remained_open](QWidget* dialog) {
        auto* recorder = child<QLineEdit>(dialog, "shortcutRecorder");
        send_key(recorder, QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        send_key(recorder, QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
        QTimer::singleShot(10, dialog, [dialog, &remained_open] {
            remained_open = dialog->isVisible() && QApplication::activeModalWidget() == dialog;
            require(child<QLineEdit>(dialog, "shortcutRecorder")->text().isEmpty(),
                "Escape resets the empty recorder without duplicating a saved binding");
            require(!child<QPushButton>(dialog, "addShortcut")->isEnabled(),
                "cancelled recording cannot be added");
            child<QPushButton>(dialog, "cancelButton")->click();
        });
    });

    require(!palette::show_settings_dialog(nullptr, value), "dialog can be cancelled after recorder Escape");
    require(remained_open, "recorder Escape does not close Settings");
    require(same_settings(value, initial), "recorder Escape does not alter caller settings");
}

void test_missing_release_does_not_block_cancel() {
    palette::Settings value;
    const auto started = std::chrono::steady_clock::now();

    with_dialog([](QWidget* dialog) {
        auto* recorder = child<QLineEdit>(dialog, "shortcutRecorder");
        send_key(recorder, QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier);
        child<QPushButton>(dialog, "cancelButton")->click();
    });

    require(!palette::show_settings_dialog(nullptr, value), "cancel returns after a missing owned release");
    const auto elapsed = std::chrono::steady_clock::now() - started;
    require(elapsed < std::chrono::seconds(3), "cancelled recorder drain is bounded below three seconds");
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setStyle(QStringLiteral("Fusion"));
    palette::apply_ui_theme();
    test_visual_snapshot();
    test_cancel_discards_staged_changes();
    test_save_applies_toggles_and_win_r();
    palette::Settings alias_settings;
    with_dialog([](QWidget* dialog){
        child<QTabWidget>(dialog,"settingsTabs")->setCurrentIndex(2);
        child<QLineEdit>(dialog,"aliasName")->setText("work");
        child<QLineEdit>(dialog,"aliasTarget")->setText("%appdata%");
        child<QPushButton>(dialog,"addAlias")->click();
        require(child<QListWidget>(dialog,"aliasesList")->count()==1,"alias added");
        child<QPushButton>(dialog,"addAlias")->click();
        require(child<QListWidget>(dialog,"aliasesList")->count()==1,"duplicate alias rejected");
        child<QLineEdit>(dialog,"aliasTarget")->setText("notepad.exe");
        child<QLineEdit>(dialog,"aliasArguments")->setText("notes.txt");
        child<QPushButton>(dialog,"updateAlias")->click();
        child<QLineEdit>(dialog,"appsPrefix")->setText("launch");
        child<QCheckBox>(dialog,"appsPrefixOnly")->setChecked(true);
        child<QPushButton>(dialog,"saveButton")->click();
    });
    require(palette::show_settings_dialog(nullptr,alias_settings),"aliases saved");
    require(alias_settings.aliases==std::vector<palette::Alias>{{L"work",L"notepad.exe",L"notes.txt"}},"alias update preserves arguments");
    require(palette::module_rule(alias_settings,palette::Module::apps)==palette::PrefixRule{L"launch",true},"module prefix edits applied");
    const auto aliases_before=alias_settings.aliases;
    with_dialog([](QWidget* dialog){child<QListWidget>(dialog,"aliasesList")->setCurrentRow(0);child<QPushButton>(dialog,"removeAlias")->click();child<QPushButton>(dialog,"cancelButton")->click();});
    require(!palette::show_settings_dialog(nullptr,alias_settings) && alias_settings.aliases==aliases_before,"cancel discards alias removal");
    test_escape_cancels_recording_not_dialog();
    test_missing_release_does_not_block_cancel();
    palette::Settings update_settings;int checks=0;
    with_dialog([](QWidget* dialog){
        child<QTabWidget>(dialog,"settingsTabs")->setCurrentIndex(3);
        child<QPushButton>(dialog,"checkUpdates")->click();
        child<QCheckBox>(dialog,"automaticUpdates")->setChecked(false);
        child<QPushButton>(dialog,"cancelButton")->click();
    });
    require(!palette::show_settings_dialog(nullptr,update_settings,[&checks](std::function<void()> completed){++checks;completed();}),"update settings Cancel works");
    require(checks==1 && update_settings.automatic_updates,"manual update action works and Cancel preserves automatic preference");
    std::cout << "Qt settings tests passed\n";
    return 0;
}
