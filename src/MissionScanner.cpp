//
//  Copyright (C) 2017  David Hedbor <neotron@gmail.com>
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

#include <deps/EDJournalQT/src/JournalFile.h>
#include <QDebug>
#include <QDirIterator>
#include "MainWindow.h"
#include "MissionScanner.h"

MissionScanner::MissionScanner(QObject *parent)
        : QObject(parent), _commanderMissions() {
}

void MissionScanner::scanJournals() {
    _commanderMissions.clear();
    QDir          dir(MainWindow::journalDirectory(), "Journal.*.log");
    QFileInfoList list        = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files, QDir::Time | QDir::Reversed);
    auto          monitorDate = QDateTime::currentDateTime().addDays(-30); // Missions last at most a month.

    for(auto entry: list) {
        if(entry.lastModified() < monitorDate) {
            continue;
        }
        auto file = entry.absoluteFilePath();
        JournalFile journalFile(file);
        connect(&journalFile, SIGNAL(onEvent(const JournalFile &, const Event &)), this, SLOT(handleEvent(const JournalFile &, const Event &)));
        journalFile.parse();
    }
}

void MissionScanner::handleEvent(const JournalFile &file, const Event &ev) {
    QDateTime now = QDateTime::currentDateTimeUtc();
    switch(ev.type()) {
        case EventTypeMissionAccepted:
//            qDebug() << ev.obj();
            if(ev.date("Expiry") >= now) {
                auto destination = ev.string("DestinationSystem");
                if(!destination.isEmpty() && file.system() != destination) {
                    auto mission = Mission(destination, file.system());
                    _commanderMissions[file.commander()][ev.integer("MissionID")] = mission;
                }
            }
            break;
        case EventTypeMissionAbandoned:
        case EventTypeMissionFailed:
        case EventTypeMissionCompleted:
            _commanderMissions[file.commander()].remove(ev.integer("MissionID"));
            break;
        case EventTypeFSDJump:
        case EventTypeLocation:
      //      qDebug() << file.commander() << " in "<<file.system();
            _commanderLastSystem[file.commander()] = file.system();
            break;
        default:
            break;
    }
}


