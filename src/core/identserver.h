#pragma once

#include <QTcpServer>
#include <QTcpSocket>

#include "coreidentity.h"

struct Request {
    QTcpSocket *socket;
    uint16_t localPort;
    QString query;
    qint64 transactionId;
    qint64 requestId;

    friend bool operator==(const Request &a, const Request &b);
};

class IdentServer : public QObject {
Q_OBJECT
public:
    IdentServer(bool strict, QObject *parent);
    ~IdentServer() override;

    bool startListening();
    void stopListening(const QString &msg);
    qint64 addWaitingSocket();
public slots:
    bool addSocket(const CoreIdentity *identity, const QHostAddress &localAddress, quint16 localPort, const QHostAddress &peerAddress, quint16 peerPort, qint64 socketId);
    bool removeSocket(const CoreIdentity *identity, const QHostAddress &localAddress, quint16 localPort, const QHostAddress &peerAddress, quint16 peerPort, qint64 socketId);

private slots:
    void incomingConnection();
    void respond();
private:
    bool responseAvailable(Request request);
    void responseUnavailable(Request request);

    QString sysIdentForIdentity(const CoreIdentity *identity) const;

    qint64 lowestSocketId();

    void processWaiting(qint64 socketId);

    void removeWaitingSocket(qint64 socketId);

    QTcpServer _server, _v6server;

    bool _strict;

    QHash<uint16_t, QString> _connections;
    std::list<Request> _requestQueue;
    std::list<qint64> _waiting;
    qint64 _socketId;
    qint64 _requestId;
};
