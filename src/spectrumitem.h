#pragma once

#include <QQuickPaintedItem>
#include <QString>

// The property types have to be complete for moc, not just declared.
#include "apmodel.h"
#include "bandplan.h"

// The channel graph: one bell per access point across a linear frequency axis,
// with the per-channel congestion strip underneath — the view a Wi-Fi analyzer
// is for.
//
// It paints from the model on every change and holds no state of its own, so
// the same painter can later render the same picture into a file for export and
// the README.
class SpectrumItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(ApModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(BandPlan *bandPlan READ bandPlan WRITE setBandPlan NOTIFY bandPlanChanged)
    Q_PROPERTY(int gigahertz READ gigahertz WRITE setGigahertz NOTIFY gigahertzChanged)
    Q_PROPERTY(QString accent READ accent WRITE setAccent NOTIFY accentChanged)
    Q_PROPERTY(int radius READ radius WRITE setRadius NOTIFY radiusChanged)
    Q_PROPERTY(int height_hint READ heightHint CONSTANT)

public:
    explicit SpectrumItem(QQuickItem *parent = nullptr);

    ApModel *model() const { return m_model; }
    void setModel(ApModel *model);

    BandPlan *bandPlan() const { return m_bandPlan; }
    void setBandPlan(BandPlan *bandPlan);

    int gigahertz() const { return m_gigahertz; }
    void setGigahertz(int gigahertz);

    QString accent() const { return m_accent; }
    void setAccent(const QString &accent);

    int radius() const { return m_radius; }
    void setRadius(int radius);

    int heightHint() const;

    void paint(QPainter *painter) override;

signals:
    void modelChanged();
    void bandPlanChanged();
    void gigahertzChanged();
    void accentChanged();
    void radiusChanged();

private:
    ApModel *m_model = nullptr;
    BandPlan *m_bandPlan = nullptr;
    int m_gigahertz = 2;
    QString m_accent = QStringLiteral("#FFD60A");
    int m_radius = 0;
};
