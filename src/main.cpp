#include "window.h"
#include <QApplication>

/** @brief Start the desktop serial workbench without opening any port automatically. */
int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    application.setApplicationName("MAVLinkSimulator");
    application.setOrganizationName("MAVLinkSimulator");
    MainWindow window;
    window.show();
    return application.exec();
}
