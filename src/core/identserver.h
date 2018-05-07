#pragma once

#include <QTcpServer>
#include <QTcpSocket>

#include "coreidentity.h"

struct Request {
    QTcpSocket *socket;
    uint16_t localPort;
    QString query;
    int64_t transactionId;
    int64_t requestId;

    friend bool operator==(const Request &a, const Request &b);
};

class IdentServer : public QObject {
Q_OBJECT
public:
    IdentServer(bool strict, QObject *parent);
    ~IdentServer() override;

    bool startListening();
    void stopListening(const QString &msg);
    int64_t addWaitingSocket();
public slots:
    bool addSocket(const CoreIdentity *identity, const QHostAddress &localAddress, quint16 localPort, const QHostAddress &peerAddress, quint16 peerPort, int64_t socketId);
    bool removeSocket(const CoreIdentity *identity, const QHostAddress &localAddress, quint16 localPort, const QHostAddress &peerAddress, quint16 peerPort, int64_t socketId);

private slots:
    void incomingConnection();
    void respond();
private:
    bool responseAvailable(Request request);
    void responseUnavailable(Request request);

    QString sysIdentForIdentity(const CoreIdentity *identity) const;

    bool hasSocketsBelowId(int64_t socketId);

    void processWaiting(int64_t socketId);

    void removeWaitingSocket(int64_t socketId);

    QTcpServer _server, _v6server;

    bool _strict;

    QHash<uint16_t, QString> _connections;
    std::list<Request> _requestQueue;
    std::list<int64_t> _waiting;
    int64_t _socketId;
    int64_t _requestId;
};