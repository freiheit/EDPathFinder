//
//  Copyright (C) 2016  David Hedbor <neotron@gmail.com>
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include <QDebug>
#include <QCheckBox>
#include <QRadioButton>
#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "TSPWorker.h"
#include "RouteViewer.h"
#include "EDSMQueryExecutor.h"
#include "QCompressor.h"
#include "AStarRouter.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), _ui(new Ui::MainWindow), _routingPending(false) {
    _ui->setupUi(this);
    connect(_ui->createRouteButton, SIGNAL(clicked()), this, SLOT(createRoute()));
    connect(_ui->systemName, SIGNAL(editingFinished()), this, SLOT(updateSystemCoordinates()));
    cleanupCheckboxes();
    buildLookupMap();
    loadCompressedData();
}

MainWindow::~MainWindow() {
    delete _ui;
}

void MainWindow::cleanupCheckboxes() {
    QList<QCheckBox *> checkboxes = findChildren<QCheckBox *>();
    int width = 0;
    for(auto checkbox: checkboxes) {
        connect(checkbox, SIGNAL(stateChanged(int)), this, SLOT(updateFilters()));
        width = qMax(checkbox->width(), width);
    }
    for(auto checkbox: checkboxes) {
        checkbox->setMinimumWidth(width);
    }
    auto radios = findChildren<QRadioButton *>();
    for(auto radio: radios) {
         connect(radio, SIGNAL(toggled(bool)), this, SLOT(updateFilters()));
     }
}

void MainWindow::buildLookupMap() {
    _flagsLookup["cdt"] = SettlementFlagsCoreDataTerminal;
    _flagsLookup["jump"] = SettlementFlagsJumpClimbRequired;
    _flagsLookup["csd"] = SettlementFlagsClassifiedScanDatabanks;
    _flagsLookup["csf"] = SettlementFlagsClassifiedScanFragment;
    _flagsLookup["cif"] = SettlementFlagsCrackedIndustrialFirmware;
    _flagsLookup["dsd"] = SettlementFlagsDivergentScanData;
    _flagsLookup["mcf"] = SettlementFlagsModifiedConsumerFirmware;
    _flagsLookup["mef"] = SettlementFlagsModifiedEmbeddedFirmware;
    _flagsLookup["osk"] = SettlementFlagsOpenSymmetricKeys;
    _flagsLookup["sfp"] = SettlementFlagsSecurityFirmwarePatch;
    _flagsLookup["slf"] = SettlementFlagsSpecializedLegacyFirmware;
    _flagsLookup["tec"] = SettlementFlagsTaggedEncryptionCodes;
    _flagsLookup["uef"] = SettlementFlagsUnusualEncryptedFiles;
    _flagsLookup["anarchy"] = SettlementFlagsAnarchy;
}

void MainWindow::routeCalculated(const RouteResult &route) {
    _ui->centralWidget->setEnabled(true);
    if(!route.isValid()) {
        _ui->statusBar->showMessage("No solution found to the given route.", 10000);
        return;
    }
    _ui->statusBar->showMessage("Route calculation completed.", 10000);
    _ui->createRouteButton->setEnabled(true);

    RouteViewer *viewer = new RouteViewer(route);
    viewer->show();
}

void MainWindow::createRoute() {

    if(_filteredSystems.size() > 0) {
        auto systemName = _ui->systemName->text();
        auto originSystem = _router.getSystemByName(systemName);
        if(!originSystem) { 
            // Need to fetch coordinates for origin.
            downloadSystemCoordinates(systemName);
            _routingPending = true;
            return;
        }
        auto routeSize = (size_t)_ui->systemCountSlider->value();
        updateSystemCoordinateDisplay(*originSystem);
        showMessage(QString("Calculating route with %1 systems starting at %2...").arg(routeSize).arg(originSystem->name().c_str()), 0);
        _ui->createRouteButton->setEnabled(false);
        TSPWorker *workerThread(new TSPWorker(_filteredSystems, originSystem, routeSize));
//        workerThread->setRouter(&_router);
        connect(workerThread, &QThread::finished, workerThread, &QObject::deleteLater);
        connect(workerThread, &TSPWorker::taskCompleted, this, &MainWindow::routeCalculated);
        workerThread->start();
        _ui->centralWidget->setEnabled(false);

    } else {
        _ui->statusBar->showMessage("No settlements found that matches your filters.", 10000);
    }
}

void MainWindow::loadSystems() {
    SystemLoader loader;
   	_systems = loader.loadSettlements(&_router);
    _ui->systemCountSlider->setMinimum(1);
    _ui->systemCountSlider->setSingleStep(1);
    updateSliderParams((int) _systems.size());

    updateFilters();
}

void MainWindow::updateFilters() {
    int32 settlementFlags = 0;
    QList<QCheckBox *> checkboxes = findChildren<QCheckBox *>();
    bool jumpsExcluded = false;
    for(auto &checkbox : checkboxes)  {
        if(checkbox->isChecked()) {
            auto flag = _flagsLookup.find(checkbox->objectName());
            if(flag != _flagsLookup.end()) {
                if(*flag == SettlementFlagsJumpClimbRequired) {
                    jumpsExcluded = true;
                } else {
                    settlementFlags |= *flag;
                }
            }
        }
    }

    ThreatLevel maxThreatLevel(ThreatLevelLow);
    if(_ui->restrictedSec->isChecked()) {
        maxThreatLevel = ThreatLevelRestrictedLongDistance;
    } else if(_ui->mediumSec->isChecked()) {
        maxThreatLevel = ThreatLevelMedium;
    } else if(_ui->highSec->isChecked()) {
        maxThreatLevel = ThreatLeveLHigh;
    }

    int32 settlementSizes = 0;

    if(_ui->smallSize->isChecked()) {
        settlementSizes |= SettlementSizeSmall;
    }
    if(_ui->mediumSize->isChecked()) {
        settlementSizes |= SettlementSizeMedium;
    }
    if(_ui->largeSize->isChecked()) {
        settlementSizes |= SettlementSizeLarge;
    }


    int32 matches = 0;
    _filteredSystems.clear();
    for(const auto &system : _systems) {
        std::deque<Planet> matchingPlanets;
        for(const auto &planet: system.planets()) {
            std::deque<Settlement> matchingSettlements;
            for(auto settlement: planet.settlements()) {
                if((settlement.flags() & settlementFlags) != settlementFlags) {
                    continue;
                }
                if((settlementSizes & settlement.size()) != settlement.size()) {
                    continue;
                }
                if(settlement.threatLevel() > maxThreatLevel) {
                    continue;
                }
                if(jumpsExcluded &&
                   (settlement.flags() & SettlementFlagsJumpClimbRequired) == SettlementFlagsJumpClimbRequired) {
                    continue;
                }
                matchingSettlements.push_back(settlement);
                ++matches;
            }
            if(matchingSettlements.size()) {
                matchingPlanets.push_back(Planet(planet.name(), matchingSettlements));
            }
        }
        if(matchingPlanets.size()) {
            _filteredSystems.push_back(System(system.name(), matchingPlanets, system.x(), system.y(), system.y()));
        }
    }
    auto numSystems = (int) _filteredSystems.size();
    updateSliderParams(numSystems);

    _matchingSettlementCount = matches;
    _ui->statusBar->showMessage(QString("Filter matches %1 settlements in %2 systems.").arg(_matchingSettlementCount).arg(_filteredSystems.size()));
}

void MainWindow::updateSystemCoordinates() {
    const QString systemName(_ui->systemName->text());
    if(!systemName.length()) {
        return;
    }
    auto system = _router.getSystemByName(systemName.toStdString());
    if(!system) {
        downloadSystemCoordinates(systemName);
    } else {
        updateSystemCoordinateDisplay(*system);
    }

}

void MainWindow::downloadSystemCoordinates(const QString &systemName) const {
    _ui->statusBar->showMessage(QString("Fetching system coordinates from EDSM..."), 10000);
    auto executor = EDSMQueryExecutor::systemCoordinateRequest(systemName);
    connect(executor, &QThread::finished, executor, &QObject::deleteLater);
    connect(executor, &EDSMQueryExecutor::coordinatesReceived, this, &MainWindow::systemCoordinatesReceived);
    connect(executor, &EDSMQueryExecutor::coordinateRequestFailed, this, &MainWindow::systemCoordinatesRequestFailed);
    executor->start();
    _ui->x->setText("-");
    _ui->y->setText("-");
    _ui->z->setText("-");
    _ui->systemName->setEnabled(false);
    _ui->createRouteButton->setEnabled(false);
}

void MainWindow::systemCoordinatesRequestFailed() {
    showMessage(QString("Unknown origin system: %1").arg(_ui->systemName->text()));
    _ui->systemName->setEnabled(true);
    _routingPending = false;
}


void MainWindow::systemCoordinatesReceived(const System &system) {
    updateSystemCoordinateDisplay(system);
    _ui->createRouteButton->setEnabled(!_routingPending);
    _ui->systemName->setEnabled(true);
    _ui->systemName->setText(system.name().c_str());
    _router.addSystem(system);
    showMessage(QString("Found coordinates for system: %1").arg(_ui->systemName->text()), 4000);
    if(_routingPending) {
        _routingPending = false;
        createRoute();
    }
}

void MainWindow::updateSystemCoordinateDisplay(const System &system) const {
    _ui->x->setText(QString::number(system.x()));
    _ui->y->setText(QString::number(system.y()));
    _ui->z->setText(QString::number(system.z()));
    _ui->systemName->setText(system.name().c_str());
}

void MainWindow::loadCompressedData() {
    showMessage("Loading known systems...", 0);
    QFile file(":/systems.json.gz");
    if(!file.open(QIODevice::ReadOnly)) { return; }
    QByteArray blob = file.readAll();

    auto compressor = new QCompressor(blob);
    connect(compressor, &QThread::finished, compressor, &QObject::deleteLater);
    connect(compressor, SIGNAL(complete(const QByteArray &)), this, SLOT(dataDecompressed(const QByteArray &)));
    compressor->start();
}

void MainWindow::dataDecompressed(const QByteArray &bytes) {
    auto json = QJsonDocument::fromJson(bytes);
    int numSystems = 0;
    for(auto systemObj: json.array()) {
        auto sysdata = systemObj.toObject();
        auto coords = sysdata["coords"].toObject();
        auto name = sysdata["name"].toString();
        System system(name.toStdString(), (float)coords["x"].toDouble(), (float) coords["y"].toDouble(),
                      (float) coords["z"].toDouble());
        _router.addSystem(system);
        ++numSystems;
    }
    showMessage(QString("Completed loading of %1 systems.").arg(numSystems));
    loadSystems();
}

void MainWindow::showMessage(const QString &message, int timeout) {
    _ui->statusBar->showMessage(message, timeout);
}

void MainWindow::updateSliderParams(int size) {
    auto max = qMin(100, size);
    _ui->systemCountSlider->setMaximum(max);
    _ui->systemCountSlider->setValue(max);
    _ui->systemCountLabel->setText(QString::number(max));
}
















