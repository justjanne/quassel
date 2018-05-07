#include <logger.h>
#include <set>

#include "corenetwork.h"
#include "identserver.h"

IdentServer::IdentServer(bool strict, QObject *parent) : QObject(parent), _strict(strict) {
    connect(&_server, SIGNAL(newConnection()), this, SLOT(incomingConnection()));
    connect(&_v6server, SIGNAL(newConnection()), this, SLOT(incomingConnection()));
}

IdentServer::~IdentServer() = default;

bool IdentServer::startListening() {
    uint16_t port = 10113;

    bool success = false;
    if (_v6server.listen(QHostAddress("::1"), port)) {
        quInfo() << qPrintable(
                tr("Listening for identd clients on IPv6 %1 port %2")
                        .arg("::1")
                        .arg(_v6server.serverPort())
        );

        success = true;
    }

    if (_server.listen(QHostAddress("127.0.0.1"), port)) {
        success = true;

        quInfo() << qPrintable(
                tr("Listening for identd clients on IPv4 %1 port %2")
                        .arg("127.0.0.1")
                        .arg(_server.serverPort())
        );
    }

    if (!success) {
        quError() << qPrintable(
                tr("Identd could not open any network interfaces to listen on! No identd functionality will be available"));
    }

    return success;
}

void IdentServer::stopListening(const QString &msg) {
    bool wasListening = false;
    if (_server.isListening()) {
        wasListening = true;
        _server.close();
    }
    if (_v6server.isListening()) {
        wasListening = true;
        _v6server.close();
    }
    if (wasListening) {
        if (msg.isEmpty())
            quInfo() << "No longer listening for identd clients.";
        else
            quInfo() << qPrintable(msg);
    }
}

void IdentServer::incomingConnection() {
    auto *server = qobject_cast<QTcpServer *>(sender());
    Q_ASSERT(server);
    while (server->hasPendingConnections()) {
        QTcpSocket *socket = server->nextPendingConnection();
        connect(socket, SIGNAL(readyRead()), this, SLOT(respond()));
    }
}

void IdentServer::respond() {
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    Q_ASSERT(socket);

    int64_t transactionId = _socketId;

    if (socket->canReadLine()) {
        QByteArray query = socket->readLine();
        if (query.endsWith("\r\n"))
            query.chop(2);
        else if (query.endsWith("\n"))
            query.chop(1);

        QList<QByteArray> split = query.split(',');

        bool success = false;

        uint16_t localPort;
        if (!split.empty()) {
            localPort = split[0].toUShort(&success, 10);
        }

        QString user;
        if (success) {
            if (_connections.contains(localPort)) {
                user = _connections[localPort];
            } else {
                success = false;
            }
        }

        QString data;
        if (success) {
            data += query + " : USERID : Quassel : " + user + "\r\n";

            socket->write(data.toUtf8());
            socket->flush();
            socket->close();
            socket->deleteLater();
        } else {
            Request request{socket, localPort, query, transactionId, _requestId++};
            if (hasSocketsBelowId(transactionId)) {
                _requestQueue.emplace_back(request);
            } else {
                responseUnavailable(request);
            }
        }
    }
}

bool IdentServer::responseAvailable(Request request) {
    QString user;
    bool success = true;
    if (_connections.contains(request.localPort)) {
        user = _connections[request.localPort];
    } else {
        success = false;
    }

    QString data;
    if (success) {
        data += request.query + " : USERID : Quassel : " + user + "\r\n";

        request.socket->write(data.toUtf8());
    }
    return success;
}

void IdentServer::responseUnavailable(Request request) {
    QString data = request.query + " : ERROR : NO-USER\r\n";

    request.socket->write(data.toUtf8());
}

QString IdentServer::sysIdentForIdentity(const CoreIdentity *identity) const {
    if (!_strict) {
        return identity->ident();
    }
    const CoreNetwork *network = qobject_cast<CoreNetwork *>(sender());
    return network->coreSession()->strictSysident();
}


bool IdentServer::addSocket(const CoreIdentity *identity, const QHostAddress &localAddress, quint16 localPort,
                            const QHostAddress &peerAddress, quint16 peerPort, int64_t socketId) {
    Q_UNUSED(localAddress)
    Q_UNUSED(peerAddress)
    Q_UNUSED(peerPort)

    const QString ident = sysIdentForIdentity(identity);
    _connections[localPort] = ident;
    processWaiting(socketId);
    return true;
}


bool IdentServer::removeSocket(const CoreIdentity *identity, const QHostAddress &localAddress, quint16 localPort,
                               const QHostAddress &peerAddress, quint16 peerPort, int64_t socketId) {
    Q_UNUSED(identity)
    Q_UNUSED(localAddress)
    Q_UNUSED(peerAddress)
    Q_UNUSED(peerPort)

    _connections.remove(localPort);
    processWaiting(socketId);
    return true;
}

int64_t IdentServer::addWaitingSocket() {
    int64_t newSocketId = _socketId++;
    _waiting.push_back(newSocketId);
    return newSocketId;
}

bool IdentServer::hasSocketsBelowId(int64_t id) {
    return std::any_of(_waiting.begin(), _waiting.end(), [=](int64_t socketId) {
        return socketId < id;
    });
}

void IdentServer::removeWaitingSocket(int64_t socketId) {
    _waiting.remove(socketId);
}

void IdentServer::processWaiting(int64_t socketId) {
    int64_t lowestSocketId = std::numeric_limits<int64_t >::max();
    for (int64_t id : _waiting) {
        if (id < lowestSocketId) {
            lowestSocketId = id;
        }
    }
    removeWaitingSocket(socketId);
    _requestQueue.remove_if([=](Request request) {
        if (request.transactionId < lowestSocketId) {
            responseUnavailable(request);
            return true;
        } else if (request.transactionId > socketId) {
            return responseAvailable(request);
        } else {
            return false;
        }
    });
}

bool operator==(const Request &a, const Request &b) {
    return a.requestId == b.requestId;
}
