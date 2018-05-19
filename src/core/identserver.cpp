#include <logger.h>
#include <set>

#include "corenetwork.h"
#include "identserver.h"

IdentServer::IdentServer(bool strict, QObject *parent) : QObject(parent), _strict(strict), _socketId(0), _requestId(0) {
    connect(&_server, SIGNAL(newConnection()), this, SLOT(incomingConnection()));
    connect(&_v6server, SIGNAL(newConnection()), this, SLOT(incomingConnection()));
}

IdentServer::~IdentServer() = default;

bool IdentServer::startListening() {
    uint16_t port = Quassel::optionValue("ident-port").toUShort();

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

    qint64 transactionId = _socketId;

    if (!socket->canReadLine()) {
        return;
    }

    QByteArray query = socket->readLine();
    if (query.endsWith("\r\n"))
        query.chop(2);
    else if (query.endsWith("\n"))
        query.chop(1);

    QList<QByteArray> split = query.split(',');

    bool success = false;

    quint16 localPort = 0;
    if (!split.empty()) {
        localPort = split[0].trimmed().toUShort(&success, 10);
    }

    Request request{socket, localPort, query, transactionId, _requestId++};
    if (!success) {
        request.respondError("NO-USER");
    } else if (responseAvailable(request)) {
        // success
    } else if (lowestSocketId() < transactionId) {
        _requestQueue.emplace_back(request);
    } else {
        request.respondError("NO-USER");
    }
}

void Request::respondSuccess(const QString &user) {
    QString data = query + " : USERID : Quassel : " + user + "\r\n";

    socket->write(data.toUtf8());
    socket->flush();
    socket->close();
}

void Request::respondError(const QString &error) {
    QString data = query + " : ERROR : " + error + "\r\n";

    socket->write(data.toUtf8());
    socket->flush();
    socket->close();
}

bool IdentServer::responseAvailable(Request request) {
    if (!_connections.contains(request.localPort)) {
        return false;
    }

    QString user = _connections[request.localPort];
    request.respondSuccess(user);
    return true;
}

QString IdentServer::sysIdentForIdentity(const CoreIdentity *identity) const {
    if (!_strict) {
        return identity->ident();
    }
    const CoreNetwork *network = qobject_cast<CoreNetwork *>(sender());
    return network->coreSession()->strictSysident();
}


bool IdentServer::addSocket(const CoreIdentity *identity, const QHostAddress &localAddress, quint16 localPort,
                            const QHostAddress &peerAddress, quint16 peerPort, qint64 socketId) {
    Q_UNUSED(localAddress)
    Q_UNUSED(peerAddress)
    Q_UNUSED(peerPort)

    const QString ident = sysIdentForIdentity(identity);
    _connections[localPort] = ident;
    processWaiting(socketId);
    return true;
}


bool IdentServer::removeSocket(const CoreIdentity *identity, const QHostAddress &localAddress, quint16 localPort,
                               const QHostAddress &peerAddress, quint16 peerPort, qint64 socketId) {
    Q_UNUSED(identity)
    Q_UNUSED(localAddress)
    Q_UNUSED(peerAddress)
    Q_UNUSED(peerPort)

    _connections.remove(localPort);
    processWaiting(socketId);
    return true;
}

qint64 IdentServer::addWaitingSocket() {
    qint64 newSocketId = _socketId++;
    _waiting.push_back(newSocketId);
    return newSocketId;
}

qint64 IdentServer::lowestSocketId() {
    if (_waiting.empty()) {
        return std::numeric_limits<qint64>::max();
    }

    return _waiting.front();
}

void IdentServer::removeWaitingSocket(qint64 socketId) {
    _waiting.remove(socketId);
}

void IdentServer::processWaiting(qint64 socketId) {
    removeWaitingSocket(socketId);
    _requestQueue.remove_if([=](Request request) {
        if (socketId < request.transactionId && responseAvailable(request)) {
            return true;
        } else if (lowestSocketId() < request.transactionId) {
            return false;
        } else {
            request.respondError("NO-USER");
            return true;
        }
    });
}

bool operator==(const Request &a, const Request &b) {
    return a.requestId == b.requestId;
}
