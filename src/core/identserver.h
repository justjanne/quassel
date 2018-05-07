#pragma once

#include <QTcpServer>
#include <QTcpSocket>

#include "coreidentity.h"

class IdentServer : public QObject {
Q_OBJECT
public:
    IdentServer(bool strict, QObject *parent);
    ~IdentServer();

    bool startListening();
    void stopListening(const QString &msg);
public slots:
    bool addSocket(const CoreIdentity *identity, const QHostAddress &localAddress, quint16 localPort, const QHostAddress &peerAddress, quint16 peerPort);
    bool removeSocket(const CoreIdentity *identity, const QHostAddress &localAddress, quint16 localPort, const QHostAddress &peerAddress, quint16 peerPort);

private slots:
    void incomingConnection();

    void respond();

private:
    QString sysIdentForIdentity(const CoreIdentity *identity) const;

    QTcpServer _server, _v6server;

    bool _strict;

    QHash<uint16_t, QString> _connections;
};