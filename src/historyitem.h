#pragma once

#include <QQuickPaintedItem>
#include <QString>

#include "aphistory.h"
#include "apmodel.h"

// Signal over time for one access point: the raw samples as faint dots and the
// rolling mean as the line, because the raw signal moves several dB while the
// machine sits perfectly still (spike 003) and a raw line alone reads as noise.
class HistoryItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(ApHistory *history READ history WRITE setHistory NOTIFY historyChanged)
    Q_PROPERTY(ApModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QString bssid READ bssid WRITE setBssid NOTIFY bssidChanged)
    Q_PROPERTY(QString accent READ accent WRITE setAccent NOTIFY accentChanged)
    Q_PROPERTY(int radius READ radius WRITE setRadius NOTIFY radiusChanged)
    Q_PROPERTY(int windowSeconds READ windowSeconds WRITE setWindowSeconds NOTIFY windowSecondsChanged)
    Q_PROPERTY(int heightHint READ heightHint CONSTANT)

public:
    explicit HistoryItem(QQuickItem *parent = nullptr);

    ApHistory *history() const { return m_history; }
    void setHistory(ApHistory *history);

    ApModel *model() const { return m_model; }
    void setModel(ApModel *model);

    QString bssid() const { return m_bssid; }
    void setBssid(const QString &bssid);

    QString accent() const { return m_accent; }
    void setAccent(const QString &accent);

    int radius() const { return m_radius; }
    void setRadius(int radius);

    int windowSeconds() const { return m_windowSeconds; }
    void setWindowSeconds(int seconds);

    int heightHint() const;

    void paint(QPainter *painter) override;

signals:
    void historyChanged();
    void modelChanged();
    void bssidChanged();
    void accentChanged();
    void radiusChanged();
    void windowSecondsChanged();

private:
    ApHistory *m_history = nullptr;
    ApModel *m_model = nullptr;
    QString m_bssid;
    QString m_accent = QStringLiteral("#FFD60A");
    int m_radius = 0;
    int m_windowSeconds = 300;
};
