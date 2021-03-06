#include "timelistener.h"
#include <QDateTime>
#include <QTimer>
#include <QRegularExpression>

#define dtmFmt "dd.MM.yyyy hh:mm:ss"        // формат метки времени от эталонного сервиса

// разобрать строку IP:PORT, в аргументы ip и port запишутся разобранные значения
bool ParseIpPort(QString ipport, QString &ip, quint16 &port)
{
    bool ret = true;
    ip.clear();
    port = 0;
    QRegularExpressionMatch match = QRegularExpression("\\b([0-9]{1,3})\\.([0-9]{1,3})\\.([0-9]{1,3})\\.([0-9]{1,3}):[0-9]{1,5}\\b").match(ipport);
    if (match.hasMatch())
    {
        // проблема: обработка рекурсивных подключений, например: 127.0.0.1:28080/192.168.0.101:28080
        QString lexem = match.captured();
        QRegularExpressionMatch matchip   = QRegularExpression(".+(?=:)").match(lexem);
        QRegularExpressionMatch matchport = QRegularExpression("(?<=:).+").match(lexem);
        // QRegularExpressionMatch matchport2 = QRegularExpression("\\d+$").match(ipport);
        if (matchip.hasMatch())
            ip = matchip.captured();
        else
            ret = false;
        if (matchport.hasMatch())
            port = matchport.captured().toUShort();
        else
            ret = false;
    }
    else
        ret = false;
    return ret;
}

TimeListener::TimeListener(QString ipport)
{
    parsed = ParseIpPort(ipport,sAddr,port);
    Logger::LogStr(parsed ? QString("Успешный разбор строки соединения " + ipport): QString("Ошибка разбора строки соединения " + ipport));
}

std::shared_ptr<TimeListener> TimeListener::fromType(QString type, QString connection)
{
    type   = type.trimmed().replace('-',"").toLower();
    connection = connection.trimmed().replace('-',"").toLower();

    if(type == "multicast")
        return std::shared_ptr<TimeListener>(new MulticastTimeListener(connection));
    else if (type == "tcp")
        return std::shared_ptr<TimeListener>(new TcpTimeListener(connection));
    else
        throw std::logic_error("Неудовлетворительный тип соединения");
}




MulticastTimeListener::MulticastTimeListener(QString ipport)
    : TimeListener(ipport)
{

}

MulticastTimeListener::~MulticastTimeListener()
{
    sock.leaveMulticastGroup(QHostAddress(sAddr));
    Logger::LogStr("Завершение работы MulticastTimeListener");
}

void MulticastTimeListener::startSinchronize()
{
    if(!parsed)
        return;

    // настраиваем сокет и присоединяемся к группе
    QObject::connect(&sock, &QUdpSocket::readyRead, this, &MulticastTimeListener::readDatagramm);
    sock.bind(QHostAddress(sAddr),port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    if(sock.joinMulticastGroup(QHostAddress(sAddr)))
    {
        Logger::LogStr(QString("MulticastTimeListener: Подключен к группе %1:%2").arg(sAddr).arg(port));
    } else {
        Logger::LogStr(QString("MulticastTimeListener: Ошибка подключения к группе %1:%2").arg(sAddr).arg(port));
    }
}

void MulticastTimeListener::readDatagramm()
{
    QHostAddress senderHost;
    quint16 senderPort;
    quint64 size = sock.pendingDatagramSize();
    QByteArray datagram;

    // читаем все что пришло и отсекаем 4 байта заголовка
    datagram.resize(size);
    sock.readDatagram(datagram.data(), size, &senderHost, &senderPort);
    datagram.remove(0,4);

    QString sDateTime(datagram);
    time_t tm = QDateTime::fromString(sDateTime, dtmFmt).toTime_t();

    // синхронизироваться надо только если разница превышает 2 секунды, чтобы лишний раз не дергать систему
    int diff = abs(time(NULL) - tm);
    if(diff > 2)
    {
        stime(&tm);

        Logger::LogStr(QString("Пороизведена синхронизация по Multicast-соединению: %1 на время %2. Разбежка во времени с эталоном составляла %3сек.")
                       .arg(senderHost.toString() + ":" + QString::number(senderPort))
                       .arg(sDateTime)
                       .arg(diff));
    }
}









TcpTimeListener::TcpTimeListener(QString ipport)
    :TimeListener(ipport)
{

}

TcpTimeListener::~TcpTimeListener()
{
    if(client)
        client->stop();

    Logger::LogStr("Завершение работы MulticastTimeListener");
}

void TcpTimeListener::startSinchronize()
{
    if(!parsed)
        return;

    client = std::shared_ptr<ClientTcp>(new ClientTcp(sAddr, port));
    client->setAutoRecconect(true);
    QObject::connect(client.get(), SIGNAL(connected    (ClientTcp*)), this, SLOT(connected      (ClientTcp*)));
    QObject::connect(client.get(), SIGNAL(dataready    (ClientTcp*)), this, SLOT(dataready       (ClientTcp*)));
    QObject::connect(client.get(), SIGNAL(disconnected (ClientTcp*)), this, SLOT(disconnected   (ClientTcp*)));
    client->start();
}

void TcpTimeListener::connected(ClientTcp * conn)
{
    Logger::LogStr(tr("TcpTimeListener: Подключен к %1").arg(conn->name()));
}

void TcpTimeListener::disconnected(ClientTcp * conn)
{
    Logger::LogStr(tr("TcpTimeListener: Отключен от %1").arg(conn->remoteIP()));
}

void TcpTimeListener::dataready(ClientTcp * conn)
{
    QString sDateTime(conn->data());
    time_t tm = QDateTime::fromString(sDateTime, dtmFmt).toTime_t();

    int diff = abs(time(NULL) - tm);

    // синхронизироваться надо только если разница превышает 2 секунды, чтобы лишний раз не дергать систему
    if(diff > 2)
    {
        stime(&tm);
        Logger::LogStr(QString("Произведена синхронизация по Tcp-соединению: %1 на время %2. Разбежка во времени с эталоном составляла %3сек.")
                       .arg(conn->name())
                       .arg(sDateTime)
                       .arg(diff));
    }
}



