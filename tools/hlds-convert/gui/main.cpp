// hlds-convert-gui: the installer. See Installer.hpp.

#include <QApplication>

#include "Installer.hpp"
#include "hlds/Converter.hpp"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    // Where QSettings keeps the folder last used, and the cache goes.
    // (Counter-Strike 1.6 DS's own: not the Half-Life installer's.)
    QApplication::setOrganizationName(QStringLiteral("cs16-ds"));
    QApplication::setApplicationName(QStringLiteral("cs16-ds-installer"));
    QApplication::setApplicationVersion(QString::fromLatin1(hlds::kConverterVersion));

    Installer window;
    window.show();
    return app.exec();
}
