#include "main_window.hpp"

#include <videx/core/application_info.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QString>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);

    QCoreApplication::setApplicationName(
        QString::fromUtf8(videx::core::ApplicationInfo::name().data()));
    QCoreApplication::setApplicationVersion(
        QString::fromUtf8(videx::core::ApplicationInfo::version().data()));
    QCoreApplication::setOrganizationName(QStringLiteral("Videx"));

    videx::ui::MainWindow window;
    window.show();

    return application.exec();
}
