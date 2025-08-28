// Copyright (C) 2019 ~ 2021 Uniontech Software Technology Co.,Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gpu_engine_grid_widget.h"
#include "gpu_engine_chart_widget.h"
#include <DApplication>
#include <QDebug>

DWIDGET_USE_NAMESPACE

GpuEngineGridWidget::GpuEngineGridWidget(QWidget *parent)
    : QWidget(parent)
    , m_gridLayout(nullptr)
    , m_graphicsChart(nullptr)
    , m_videoEncodeChart(nullptr)
    , m_videoDecodeChart(nullptr)
    , m_computeChart(nullptr)
    , m_updateTimer(new QTimer(this))
{
    setupLayout();
    createEngineCharts();
    
    // Connect to font changes (Qt6 compatible)
    connect(dynamic_cast<QGuiApplication *>(DApplication::instance()), &QGuiApplication::fontChanged, this, &GpuEngineGridWidget::fontChanged);
    
    // Set up update timer (2 second interval as per spec)
    m_updateTimer->setInterval(2000);
    connect(m_updateTimer, &QTimer::timeout, this, &GpuEngineGridWidget::updateData);
}

GpuEngineGridWidget::~GpuEngineGridWidget()
{
    if (m_updateTimer) {
        m_updateTimer->stop();
    }
}

void GpuEngineGridWidget::setGpuDevice(const GpuDevice &device)
{
    m_device = device;
    
    if (m_graphicsChart) {
        m_graphicsChart->setGpuDevice(device);
    }
    if (m_videoEncodeChart) {
        m_videoEncodeChart->setGpuDevice(device);
    }
    if (m_videoDecodeChart) {
        m_videoDecodeChart->setGpuDevice(device);
    }
    if (m_computeChart) {
        m_computeChart->setGpuDevice(device);
    }
    
    // Start updating data
    m_updateTimer->start();
}

void GpuEngineGridWidget::updateData()
{
    if (m_graphicsChart) {
        m_graphicsChart->updateData();
    }
    if (m_videoEncodeChart) {
        m_videoEncodeChart->updateData();
    }
    if (m_videoDecodeChart) {
        m_videoDecodeChart->updateData();
    }
    if (m_computeChart) {
        m_computeChart->updateData();
    }
}

void GpuEngineGridWidget::resetCharts()
{
    if (m_graphicsChart) {
        m_graphicsChart->resetChart();
    }
    if (m_videoEncodeChart) {
        m_videoEncodeChart->resetChart();
    }
    if (m_videoDecodeChart) {
        m_videoDecodeChart->resetChart();
    }
    if (m_computeChart) {
        m_computeChart->resetChart();
    }
}

void GpuEngineGridWidget::fontChanged(const QFont &font)
{
    // Font changes are handled automatically by individual chart widgets
    Q_UNUSED(font)
}

void GpuEngineGridWidget::setupLayout()
{
    m_gridLayout = new QGridLayout(this);
    m_gridLayout->setSpacing(GRID_SPACING);
    m_gridLayout->setContentsMargins(GRID_MARGIN, GRID_MARGIN, GRID_MARGIN, GRID_MARGIN);
    setLayout(m_gridLayout);
}

void GpuEngineGridWidget::createEngineCharts()
{
    // Create 4 engine charts in a horizontal row (1x4 grid)
    m_graphicsChart = new GpuEngineChartWidget(GraphicsEngine, this);
    m_videoEncodeChart = new GpuEngineChartWidget(VideoEncodeEngine, this);
    m_videoDecodeChart = new GpuEngineChartWidget(VideoDecodeEngine, this);
    m_computeChart = new GpuEngineChartWidget(ComputeEngine, this);
    
    // Add to grid layout (horizontal row: 1x4)
    m_gridLayout->addWidget(m_graphicsChart, 0, 0);    // 图形渲染 (粉红色)
    m_gridLayout->addWidget(m_videoEncodeChart, 0, 1); // 视频编码 (碧绿色) 
    m_gridLayout->addWidget(m_videoDecodeChart, 0, 2); // 视频解码 (黄色)
    m_gridLayout->addWidget(m_computeChart, 0, 3);     // 通用计算 (红色)
    
    // Set equal column stretch for horizontal layout
    m_gridLayout->setColumnStretch(0, 1);
    m_gridLayout->setColumnStretch(1, 1);
    m_gridLayout->setColumnStretch(2, 1);
    m_gridLayout->setColumnStretch(3, 1);
    m_gridLayout->setRowStretch(0, 1);
}
