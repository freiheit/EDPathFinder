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
#include <QtConcurrent>
#include <constraint_solver/routing_flags.h>
#include "System.h"
#include "TSPWorker.h"

namespace operations_research {

// Cost/distance functions.
    int64 TSPWorker::systemDistance(RoutingModel::NodeIndex from, RoutingModel::NodeIndex to) {
        return _distanceMatrix[(size_t)from.value()][(size_t)to.value()];
    }

    int64 TSPWorker::calculateDistance(size_t from, size_t to) {
        auto &fromSystem = _systems[from];
        auto &toSystem = _systems[to];
        ++numDist;
        if(_router) {
            AStarResult result = _router->calculateRoute(fromSystem.name(), toSystem.name(), 45.0f);
            if(result.valid()) {
                return (int64) result.route().size();
            } else {
                return INT64_MAX;
            }
        }
        return fromSystem.distance(toSystem);
    }

    void TSPWorker::calculateDistanceMatrix() {
        auto sz = _systems.size();
        _distanceMatrix.resize(sz);
        typedef std::pair<QFuture<int64>, std::pair<size_t, size_t>> FuturePair;
        std::deque<FuturePair> futures;
        for(size_t from = 0; from < sz; from++) {
            _distanceMatrix[from].resize(sz, -1);
        }
        for(size_t from = 0; from < sz; from++) {
            for(size_t to = 0; to < sz; to++) {
                int64 dist = 0;
                if(from != to) {
                    if(_distanceMatrix[to][from] == -1) {
                        auto future = QtConcurrent::run(this, &TSPWorker::calculateDistance, from, to);
                        auto destPair = std::pair<size_t, size_t>(from, to);
                        futures.push_back(FuturePair(future, destPair));
                        _distanceMatrix[to][from] = -2;
                        _distanceMatrix[from][to] = -2;
                        continue;
                    }
                } else {
                    _distanceMatrix[from][to] = dist;
                }
            }
        }
        qDebug() << "Waiting for completion of" << futures.size() << "futures";
        for(auto &pair: futures) {
            pair.first.waitForFinished();
            auto a = pair.second.first;
            auto b = pair.second.second;
            _distanceMatrix[a][b] = _distanceMatrix[b][a] = pair.first.result();
        }
    }

    void TSPWorker::run() {
        System *startingSystem = _origin;
        if(!startingSystem) {
            startingSystem = &_systems[0];
        }
        QTime timer;
        timer.start();
        std::sort(_systems.begin(), _systems.end(), [ startingSystem ](const System &a, const System &b) {
            return a.distance(*startingSystem) < b.distance((*startingSystem));
        });
        if(_maxSystemCount < _systems.size()) {
            _systems.resize(_maxSystemCount);
        }
        // Calculate the closest system
        if(_origin) {
            _systems.push_front(*_origin);
        }
        qDebug() << "Sorting and resizing took " << timer.elapsed();
        timer.restart();
        calculateDistanceMatrix();
        qDebug() << "Matrix calculation took " << timer.elapsed();
        timer.restart();

        RoutingModel routing((int) _systems.size(), 1);
        routing.SetDepot(RoutingModel::NodeIndex(0));
        RoutingSearchParameters parameters = BuildSearchParametersFromFlags();

        // Setting first solution heuristic (cheapest addition).
        parameters.set_first_solution_strategy(FirstSolutionStrategy::PARALLEL_CHEAPEST_INSERTION);
        routing.SetArcCostEvaluatorOfAllVehicles(NewPermanentCallback(this, &TSPWorker::systemDistance));

        // Solve, returns a solution if any (owned by RoutingModel).
        const Assignment *solution = routing.SolveWithParameters(parameters);
        qDebug() << "Routing took " << timer.elapsed();

        // Populate result.
        RouteResult result;
        if(solution != NULL) {
            // Inspect solution.
            // Only one route here; otherwise iterate from 0 to routing.vehicles() - 1
            const int route_number = 0;
            size_t nodeid;
            size_t previd = 0;
            int64 dist = 0;
            for(int64 node = routing.Start(route_number);
                !routing.IsEnd(node);
                node = solution->Value(routing.NextVar(node))) {
                nodeid = (size_t) routing.IndexToNode(node).value();

                const System &sys = _systems[nodeid];

                if(nodeid > 0) {
                    dist = sys.distance(_systems[previd]);
                }
                previd = nodeid;
                if(!sys.planets().size())  {
                    result.addEntry(sys, "Point of Origin", "", dist);
                    continue;
                }
                for(auto planet: sys.planets()) {
                    for(auto settlement: planet.settlements()) {
                        result.addEntry(sys, planet, settlement, dist);
                        dist = 0;
                    }
                }
            }
            dist = _systems[0].distance(_systems[previd]);
            result.addEntry(_systems[0], "Point of Origin", "", dist);
        } else {
            LOG(INFO) << "No solution found.";
        }
        emit taskCompleted(result);
    }
}

void RouteResult::addEntry(const System &system, const Planet &planet, const Settlement &settlement, int64 distance) {
    addEntry(system, planet.name().c_str(), settlement.name().c_str(), distance);
}

void RouteResult::addEntry(const System &system, const QString &planet, const QString &settlement, int64 distance) {
    _totalDist += distance;
    std::vector<QString> row(5);
    row[0] = system.name().c_str();
    row[1] = planet;
    row[2] = settlement;
    row[3] = System::formatDistance(distance);
    row[4] = System::formatDistance(_totalDist);
    _route.emplace_back(row);
}



