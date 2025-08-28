#ifndef COMPACTGPUMONITOR_H
#define COMPACTGPUMONITOR_H

#include <QWidget>
#include <QTimer>
#include <QPainterPath>
#include "system/gpu_backend.h"

class CompactGpuMonitor : public QWidget
{
    Q_OBJECT
public:
    explicit CompactGpuMonitor(QWidget *parent = nullptr);
    ~CompactGpuMonitor() = default;

signals:
    void clicked(QString msgCode);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private slots:
    void onTick();

private:
    void changeFont(const QFont &font);
    void resizeItemRect();
    void selectBestGpuDevice(const QVector<core::system::GpuDevice> &devices);
    int getGpuPriority(core::system::GpuVendor vendor) const;

    core::system::GpuService m_service;
    core::system::GpuDevice m_device;
    bool m_hasDevice {false};
    core::system::GpuStats m_stats;
    QTimer m_timer;

    // UI state
    QList<qreal> m_utilizationHistory;
    QFont m_titleFont;
    QFont m_statFont;

    QColor m_frameColor;    // grid frame
    QColor m_titleColor;    // title text
    QColor m_statColor;     // stat text
    QColor m_sectionColor {"#0081FF"}; // left indicator
    QColor m_curveColor {"#9C27B0"};   // waveform color

    int m_gridSize {10};
    int m_pointsNumber {25};
    int m_pointerRadius {6};
};

#endif // COMPACTGPUMONITOR_H
