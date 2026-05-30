QT       += core gui widgets network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
TARGET = CodexSwitcher

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    accountcard.cpp \
    datamanager.cpp \
    settingsdialog.cpp \
    importdialog.cpp

HEADERS += \
    mainwindow.h \
    accountcard.h \
    datamanager.h \
    settingsdialog.h \
    importdialog.h

RESOURCES += \
    resources.qrc

RC_ICONS = assets/app-icon.ico

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
