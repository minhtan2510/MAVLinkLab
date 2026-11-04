#include "window.h"
#include <QApplication>
#include <clocale>

/** @brief Start the desktop serial workbench without opening any port automatically. */
int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    std::setlocale(LC_NUMERIC, "C");
    application.setApplicationName("MAVLinkLab");
    application.setOrganizationName("MAVLinkLab");
    // Window settings use explicit legacy namespaces to retain existing user data.
    MainWindow window;
    window.show();
    return application.exec();
}
