#include "window.h"
#include <QApplication>
#include <clocale>

/** @brief Start the desktop serial workbench without opening any port automatically. */
int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    std::setlocale(LC_NUMERIC, "C");
    application.setApplicationName("MAVLinkSimulator");
    application.setOrganizationName("MAVLinkSimulator");
    MainWindow window;
    window.show();
    return application.exec();
}
