//
// Copyright 2010-2015 Jacob Dawid <jacob@omg-it.works>
//
// This file is part of Shark.
//
// Shark is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Shark is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Shark.  If not, see <http://www.gnu.org/licenses/>.
//

// Qt includes
#include <QTcpSocket>
#include <QStringList>
#include <QDateTime>
#include <QTimer>
#include <QNetworkRequest>

// Own includes
#include "NetworkServiceThread.h"

namespace Shark {

NetworkServiceThread::NetworkServiceThread(NetworkService &webService)
    : QThread(0),
      Logger(QString("Shark:NetworkServiceThread (%1)").arg((long)this)),
      _networkService(webService) {
    _networkServiceThreadState = Idle;
}

NetworkServiceThread::~NetworkServiceThread() {
}

NetworkServiceThread::NetworkServiceThreadState NetworkServiceThread::networkServiceThreadState() {
    _networkServiceStateMutex.lock();
    NetworkServiceThreadState state = _networkServiceThreadState;
    _networkServiceStateMutex.unlock();
    return state;
}

void NetworkServiceThread::setNetworkServiceThreadState(NetworkServiceThread::NetworkServiceThreadState state) {
    _networkServiceStateMutex.lock();
    _networkServiceThreadState = state;
    _networkServiceStateMutex.unlock();
}

void NetworkServiceThread::serve(int socketHandle) {
    QTcpSocket* tcpSocket = new QTcpSocket(this);
    connect(tcpSocket, SIGNAL(readyRead()), this, SLOT(readClient()));
    connect(tcpSocket, SIGNAL(disconnected()), this, SLOT(discardClient()));
    tcpSocket->setSocketDescriptor(socketHandle);
}

void NetworkServiceThread::readClient() {
    setNetworkServiceThreadState(ProcessingRequest);
    QTcpSocket* socket = (QTcpSocket*)sender();
    QString httpRequest;
    bool requestCompleted = false;

    QTimer requestTimer, responseTimer;
    requestTimer.setTimerType(Qt::PreciseTimer);
    responseTimer.setTimerType(Qt::PreciseTimer);
    const int timeoutInterval = 10000;
    requestTimer.setInterval(timeoutInterval);
    responseTimer.setInterval(timeoutInterval);

    // Request
    requestTimer.start();
    while(requestTimer.isActive() > 0) {
        if(socket->canReadLine()) {
            QString line = socket->readLine();
            httpRequest.append(line);
            if(line == "\r\n") {
                requestCompleted = true;
                break;
            }
        }
    }

    int requestTimePassed = timeoutInterval - requestTimer.remainingTime();
    log(QString("Received request within %1 ms.").arg(requestTimePassed));

    // Response
    responseTimer.start();
    if(requestCompleted) {
        setNetworkServiceThreadState(ProcessingResponse);
        NetworkRequest request(httpRequest);
        NetworkResponse response;
        _networkService.httpResponder()->respond(request, response);
        socket->write(response.toByteArray());
        socket->waitForBytesWritten(10000);
    }
    int responseTimePassed = timeoutInterval - responseTimer.remainingTime();
    log(QString("Generated and sent response within %1 ms.").arg(responseTimePassed));

    // Close connection
    socket->close();
    if(socket->state() == QTcpSocket::UnconnectedState) {
        delete socket;
    }

    setNetworkServiceThreadState(Idle);
}

void NetworkServiceThread::discardClient() {
    QTcpSocket* socket = (QTcpSocket*)sender();
    socket->deleteLater();
}

} // namespace Shark