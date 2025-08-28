// Copyright (C) 2019 ~ 2021 Uniontech Software Technology Co.,Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GPU_ENGINE_CHART_WIDGET_H
#define GPU_ENGINE_CHART_WIDGET_H

#include <QWidget>
#include <QColor>
#include <QTimer>
#include <QList>
#include <QPainterPath>
#include "../system/gpu_backend.h"

using namespace core::system;

enum GpuEngineType {
    GraphicsEngine,   // 图形渲染
    VideoEncodeEngine,// 视频编码
    VideoDecodeEngine,// 视频解码
    ComputeEngine     // 通用计算
};

class GpuEngineChartWidget : public QWidget
{
    Q_OBJECT

public:
    explicit GpuEngineChartWidget(GpuEngineType engineType, QWidget *parent = nullptr);
    virtual ~GpuEngineChartWidget();

    void setGpuDevice(const GpuDevice &device);
    void updateData();
    void resetChart();

    QString getEngineTitle() const;
    QColor getEngineColor() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void changeTheme();
    void changeFont(const QFont &font);

private:
    void drawBackground(QPainter *painter);
    void drawChart(QPainter *painter);
    void drawTitle(QPainter *painter);
    void drawAxisLabels(QPainter *painter);

    void updateEngineUtilization();
    
    // Mock function for now - will be implemented with real backend later
    qreal getEngineUtilizationPercent();

private:
    GpuEngineType m_engineType;
    GpuDevice m_device;
    GpuService m_gpuService;
    
    QList<qreal> m_utilizationHistory;
    static constexpr int MAX_HISTORY_COUNT = 30; // 30 data points for 60 seconds (2s interval)
    
    QFont m_font;
    QColor m_engineColor;
    QString m_engineTitle;
    
    // Chart drawing area
    QRect m_chartRect;
    
    // Grid and background
    QPixmap m_backgroundPixmap;
    bool m_needUpdateBackground;
    
    // Constants
    static constexpr int GRID_SIZE = 10;
    static constexpr int TITLE_HEIGHT = 16;
    static constexpr int AXIS_LABEL_HEIGHT = 12;
    static constexpr int MARGINS = 2;
};

#endif // GPU_ENGINE_CHART_WIDGET_H
