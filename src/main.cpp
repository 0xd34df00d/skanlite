/* ============================================================
 *
 * Copyright (C) 2007-2008 by Kare Sars <kare dot sars at iki dot fi>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This appication is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * ============================================================ */
#include <kapplication.h>
#include <kcmdlineargs.h>
#include <kaboutdata.h>
#include <kglobal.h>

#include "skanlite.h"

int main(int argc, char *argv[])
{
    // about data
    KAboutData aboutData("Skanlite", "skanlite", ki18n("Skanlite"), "0.2",
                         ki18n("This is a scanning application for KDE based on libksane."),
                         KAboutData::License_GPL,
                         ki18n("(C) 2008 Kåre Särs"));

    aboutData.addAuthor(ki18n("Kåre Särs"),
                        ki18n("developer"),
                        "kare.sars@iki.fi", 0);

    aboutData.addCredit(ki18n("Gilles Caulier"),
                        ki18n("Importing libksane to extragear"),
                              0, 0);

    aboutData.addCredit(ki18n("Anne-Marie Mahfouf"),
                        ki18n("Writing the user manual"),
                              0, 0);

    aboutData.addCredit(ki18n("Laurent Montel"),
                        ki18n("Importing libksane to extragear"),
                              0, 0);

    aboutData.addCredit(ki18n("Chusslove Illich"),
                        ki18n("Help with translations"),
                              0, 0);

    aboutData.addCredit(ki18n("Albert Astals Cid"),
                        ki18n("Help with translations"),
                              0, 0);

    KCmdLineArgs::init(argc, argv, &aboutData);

    KCmdLineOptions options;
    options.add("d <device>", ki18n("Sane scanner device name."));
    KCmdLineArgs::addCmdLineOptions(options);
    KCmdLineArgs *args = KCmdLineArgs::parsedArgs();

    QString device = args->getOption("d");

    KApplication app;

    Skanlite *ksane_test = new Skanlite(device, 0);

    ksane_test->show();

    QObject::connect(&app, SIGNAL(lastWindowClosed()), &app, SLOT(quit()));

    return app.exec();
}

