/*
 * Thermal-FIST package
 * 
 * Copyright (c) 2014-2018 Volodymyr Vovchenko
 *
 * GNU General Public License (GPLv3 or later)
 */
#include "mainwindow.h"
#include <QApplication>
#include <QTranslator>
#include <QLibraryInfo>

//#define CRTDBG_MAP_ALLOC
//#include <stdlib.h>
//#include <crtdbg.h>

int main(int argc, char *argv[])
{
    //_CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );

    QApplication a(argc, argv);

		QPixmap pixmap(":/images/FIST.png");
		QSplashScreen splash(pixmap);
		splash.show();

    QTranslator qtTranslator;
    qtTranslator.load("qt_" + QLocale::system().name(),
            QLibraryInfo::location(QLibraryInfo::TranslationsPath));
    a.installTranslator(&qtTranslator);

    QTranslator myappTranslator;
    myappTranslator.load("HadronResonanceGas_" + QLocale::system().name());
    a.installTranslator(&myappTranslator);

    QFont font = QApplication::font();
    font.setPointSize(10);
    QApplication::setFont(font);

		//std::ios_base::sync_with_stdio(false);
	

    MainWindow w;
    w.showMaximized();

		splash.finish(&w);

    return a.exec();
}
