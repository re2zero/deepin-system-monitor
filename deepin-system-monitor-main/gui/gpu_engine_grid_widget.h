// Copyright (C) 2019 ~ 2021 Uniontech Software Technology Co.,Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GPU_ENGINE_GRID_WIDGET_H
#define GPU_ENGINE_GRID_WIDGET_H

#include <QWidget>
#include <QGridLayout>
#include <QTimer>
#include "../system/gpu_backend.h"

using namespace core::system;

class GpuEngineChartWidget;

class GpuEngineGridWidget : public QWidget
{
    Q_OBJECT

public:
    explicit GpuEngineGridWidget(QWidget *parent = nullptr);
    virtual ~GpuEngineGridWidget();

    void setGpuDevice(const GpuDevice &device);
    void updateData();
    void resetCharts();

public slots:
    void fontChanged(const QFont &font);

private:
    void setupLayout();
    void createEngineCharts();

private:
    GpuDevice m_device;
    QGridLayout *m_gridLayout;
    
    // 4 engine charts
    GpuEngineChartWidget *m_graphicsChart;    // 图形渲染 (粉红色)
    GpuEngineChartWidget *m_videoEncodeChart; // 视频编码 (碧绿色)
    GpuEngineChartWidget *m_videoDecodeChart; // 视频解码 (黄色)
    GpuEngineChartWidget *m_computeChart;     // 通用计算 (红色)
    
    QTimer *m_updateTimer;
    
    static constexpr int GRID_SPACING = 8;
    static constexpr int GRID_MARGIN = 4;
};

#endif // GPU_ENGINE_GRID_WIDGET_H
