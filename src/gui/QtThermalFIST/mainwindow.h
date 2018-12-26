/*
 * Thermal-FIST package
 * 
 * Copyright (c) 2014-2018 Volodymyr Vovchenko
 *
 * GNU General Public License (GPLv3 or later)
 */
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLineEdit>
#include <QPushButton>
#include <QTableView>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QTextEdit>
#include <QTabWidget>

#include "modeltab.h"
#include "eventgeneratortab.h"
#include "energydependencetab.h"
#include "contourplottab.h"
#include "listeditortab.h"

#include "HRGBase/ThermalModelBase.h"
#include "fittoexperimenttab.h"


class MainWindow : public QMainWindow
{
    Q_OBJECT

    QTabWidget *tabWidget;
    int currentTab;

    ModelTab *tab1;
	
    FitToExperimentTab *tab2;

    EnergyDependenceTab *tab3;
    ContourPlotTab *tab4;
    EventGeneratorTab *tab5;
    ListEditorTab *tabEditor;

    QLineEdit *leDatabase;
    QPushButton *buttonLoad;
		QPushButton *buttonLoadDecays;

    thermalfist::ThermalParticleSystem *TPS;
    thermalfist::ThermalModelBase *model;

		QString cpath = "";

public:
    MainWindow(QWidget *parent = 0);
    ~MainWindow();
protected:
#ifndef QT_NO_CONTEXTMENU
//  void contextMenuEvent(QContextMenuEvent *event) override;
#endif // QT_NO_CONTEXTMENU
private:
  void createMenus();
private slots:
  void loadDatabase();
	void loadDecays();
  void tabChanged(int newIndex);
  void about();
  void quickstartguide();
};

#endif // MAINWINDOW_H
