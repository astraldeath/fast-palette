#include "ui_theme.hpp"
#include <QApplication>
#include <QPalette>
#include <QPainter>
#include <QFont>
#include <QHash>
#include <QResource>
#include <windows.h>
#include <cmath>
static void initialize_theme_resources() { Q_INIT_RESOURCE(theme_assets); }
namespace palette {
void apply_ui_theme() {
    static const bool initialized=[] { initialize_theme_resources();return true; }();
    static_cast<void>(initialized);
    DWORD light=1,bytes=sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&bytes);
    const bool dark=light==0;
    const QString bg=dark?"#191919":"#f8f8f8",surface=dark?"#242424":"#ffffff";
    const QString fg=dark?"#e8e8e8":"#222222",muted=dark?"#a0a0a0":"#666666";
    const QString line=dark?"#383838":"#dddddd",hover=dark?"#333333":"#ededed";
    QPalette palette;
    palette.setColor(QPalette::Window,QColor(bg));
    palette.setColor(QPalette::WindowText,QColor(fg));
    palette.setColor(QPalette::Base,QColor(surface));
    palette.setColor(QPalette::AlternateBase,QColor(bg));
    palette.setColor(QPalette::Text,QColor(fg));
    palette.setColor(QPalette::Button,QColor(surface));
    palette.setColor(QPalette::ButtonText,QColor(fg));
    palette.setColor(QPalette::Highlight,QColor(hover));
    palette.setColor(QPalette::HighlightedText,QColor(fg));
    palette.setColor(QPalette::ToolTipBase,QColor(surface));
    palette.setColor(QPalette::ToolTipText,QColor(fg));
    palette.setColor(QPalette::PlaceholderText,QColor(muted));
    palette.setColor(QPalette::Disabled,QPalette::Text,QColor(muted));
    palette.setColor(QPalette::Disabled,QPalette::ButtonText,QColor(muted));
    QApplication::setPalette(palette);
    QFont font("Segoe UI");font.setPixelSize(14);QApplication::setFont(font);
    qApp->setStyleSheet(QString(R"(
        QWidget { color:%1; }
        QFrame#paletteSurface, QFrame#settingsSurface { background:%2; border:1px solid %4; border-radius:12px; }
        QToolButton#closeSettings { background:transparent; border:none; padding:8px; }
        QToolButton#closeSettings:hover { background:%6; }
        QFrame[role="separator"] { background:%4; border:none; min-height:1px; max-height:1px; }
        QLabel { background:transparent; }
        QLabel[muted="true"] { color:%3; }
        QLabel[role="title"] { font-size:22px; font-weight:600; }
        QLabel[role="section"] { font-size:14px; font-weight:600; }
        QLabel[role="muted"] { color:%3; }
        QLabel[role="error"] { color:#db9292; }
        QLineEdit { background:%5; border:1px solid %4; border-radius:7px; padding:9px 12px; selection-background-color:%6; }
        QLineEdit:focus { border-color:%3; }
        QLineEdit#queryInput { background:transparent; border:none; padding:0; font-size:22px; }
        QPushButton, QToolButton { background:%5; border:1px solid %4; border-radius:7px; padding:8px 16px; }
        QPushButton:hover, QToolButton:hover { background:%6; }
        QPushButton:pressed, QToolButton:pressed { border-color:%3; }
        QPushButton:focus, QToolButton:focus { border-color:%3; }
        QPushButton:disabled { color:%3; }
        QPushButton#saveButton { background:%1; color:%2; border-color:%1; }
        QToolButton#settingsButton { background:transparent; border:none; padding:7px; }
        QToolButton#settingsButton:hover { background:%6; }
        QListWidget { background:%5; border:1px solid %4; border-radius:7px; padding:4px; outline:none; }
        QListWidget::item { padding:8px; border-radius:5px; }
        QListWidget::item:selected { background:%6; }
        QListWidget#results { background:transparent; border:none; padding:0; }
        QListWidget#results::item { padding:0; }
        QWidget#resultRow { background:transparent; }
        QCheckBox { spacing:10px; padding:5px 0; }
        QCheckBox::indicator { width:18px; height:18px; border:1px solid %4; border-radius:4px; background:%5; }
        QCheckBox::indicator:checked { background:%1; border-color:%1; image:url(:/icons/check-%7.png); }
        QCheckBox::indicator:hover, QCheckBox::indicator:focus { border-color:%3; }
        QMenu { background:%2; border:1px solid %4; padding:5px; }
        QMenu::item { padding:8px 22px; }
        QMenu::item:selected { background:%6; }
        QScrollBar:vertical { background:transparent; width:8px; margin:0; }
        QScrollBar::handle:vertical { background:%4; border-radius:4px; min-height:24px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
        QToolTip { background:%5; color:%1; border:1px solid %4; }
    )").arg(fg,bg,muted,line,surface,hover,dark?"dark":"light"));
}
QIcon ui_icon(UiIcon kind) {
    static QHash<quint64,QIcon> cache;
    const auto color=qApp->palette().color(QPalette::WindowText).rgba();
    const quint64 key=(static_cast<quint64>(color)<<8)|static_cast<unsigned>(kind);
    if(const auto found=cache.constFind(key);found!=cache.cend())return *found;
    QIcon result;
    for(int size:{16,24,32,48,64}) {
        QPixmap pixmap(size,size);pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);p.setRenderHint(QPainter::Antialiasing);p.scale(size/24.0,size/24.0);
        const auto ink=kind==UiIcon::logo?QColor("#eeeeee"):qApp->palette().color(QPalette::WindowText);
        p.setPen(QPen(ink,1.7,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);
        if(kind==UiIcon::search) {p.drawEllipse(QPointF(10,10),6,6);p.drawLine(QPointF(14.5,14.5),QPointF(20,20));}
        else if(kind==UiIcon::close) {p.drawLine(6,6,18,18);p.drawLine(18,6,6,18);}
        else if(kind==UiIcon::settings) {
            p.drawEllipse(QPointF(12,12),6,6);p.drawEllipse(QPointF(12,12),2,2);
            for(int i=0;i<8;++i){const double a=i*3.14159265/4;p.drawLine(QPointF(12+6*std::cos(a),12+6*std::sin(a)),QPointF(12+9*std::cos(a),12+9*std::sin(a)));}
        } else if(kind==UiIcon::calculator) {
            p.drawRoundedRect(QRectF(5,2,14,20),2,2);p.drawLine(8,7,16,7);
            for(int y:{11,16})for(int x:{8,12,16})p.drawPoint(x,y);
        } else if(kind==UiIcon::logo) {
            p.fillRect(QRectF(0,0,24,24),QColor("#202329"));p.drawLine(5,7,10,12);p.drawLine(10,12,5,17);p.drawLine(13,17,19,17);
        } else {p.drawRoundedRect(QRectF(3,4,18,16),2,2);p.drawLine(3,9,21,9);}
        p.end();result.addPixmap(pixmap);
    }
    cache.insert(key,result);return result;
}
}
