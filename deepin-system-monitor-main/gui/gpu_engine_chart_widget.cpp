// Copyright (C) 2019 ~ 2021 Uniontech Software Technology Co.,Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gpu_engine_chart_widget.h"
#include <DApplication>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <DApplicationHelper>
#else
#include <DGuiApplicationHelper>
#endif
#include <DPalette>
#include <DFontSizeManager>

#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QDebug>

DWIDGET_USE_NAMESPACE

GpuEngineChartWidget::GpuEngineChartWidget(GpuEngineType engineType, QWidget *parent)
    : QWidget(parent)
    , m_engineType(engineType)
    , m_needUpdateBackground(true)
{
    // Set up engine-specific properties
    switch (engineType) {
    case GraphicsEngine:
        m_engineTitle = tr("图形渲染");
        m_engineColor = QColor("#E91E63"); // 粉红色
        break;
    case VideoEncodeEngine:
        m_engineTitle = tr("视频编码");
        m_engineColor = QColor("#00C896"); // 碧绿色
        break;
    case VideoDecodeEngine:
        m_engineTitle = tr("视频解码");
        m_engineColor = QColor("#FEDF19"); // 黄色
        break;
    case ComputeEngine:
        m_engineTitle = tr("通用计算");
        m_engineColor = QColor("#E14300"); // 红色
        break;
    }

    // Initialize history list
    for (int i = 0; i < MAX_HISTORY_COUNT; ++i) {
        m_utilizationHistory.append(0.0);
    }

    // Connect to theme changes
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    connect(DApplicationHelper::instance(), &DApplicationHelper::themeTypeChanged, this, &GpuEngineChartWidget::changeTheme);
#else
    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::themeTypeChanged, this, &GpuEngineChartWidget::changeTheme);
#endif

    // Connect to font changes (Qt6 compatible)
    connect(dynamic_cast<QGuiApplication *>(DApplication::instance()), &QGuiApplication::fontChanged, this, &GpuEngineChartWidget::changeFont);
    m_font = DApplication::font();
    m_font.setPointSizeF(m_font.pointSizeF() - 1);

    setMinimumSize(120, 80);
}

GpuEngineChartWidget::~GpuEngineChartWidget()
{
}

void GpuEngineChartWidget::setGpuDevice(const GpuDevice &device)
{
    m_device = device;
}

void GpuEngineChartWidget::updateData()
{
    qreal newUtilization = getEngineUtilizationPercent();
    
    // Shift history data left
    m_utilizationHistory.removeFirst();
    m_utilizationHistory.append(newUtilization);
    
    update(); // Trigger repaint
}

void GpuEngineChartWidget::resetChart()
{
    // Reset all history data to 0
    m_utilizationHistory.clear();
    for (int i = 0; i < MAX_HISTORY_COUNT; ++i) {
        m_utilizationHistory.append(0.0);
    }
    update();
}

QString GpuEngineChartWidget::getEngineTitle() const
{
    return m_engineTitle;
}

QColor GpuEngineChartWidget::getEngineColor() const
{
    return m_engineColor;
}

void GpuEngineChartWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    
    if (m_needUpdateBackground) {
        drawBackground(&painter);
        m_needUpdateBackground = false;
    }
    
    drawTitle(&painter);
    drawChart(&painter);
    drawAxisLabels(&painter);
}

void GpuEngineChartWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_needUpdateBackground = true;
    
    // Update chart rectangle
    m_chartRect = QRect(MARGINS, TITLE_HEIGHT + MARGINS, 
                       width() - 2 * MARGINS, 
                       height() - TITLE_HEIGHT - AXIS_LABEL_HEIGHT - 2 * MARGINS);
}

void GpuEngineChartWidget::changeTheme()
{
    m_needUpdateBackground = true;
    update();
}

void GpuEngineChartWidget::changeFont(const QFont &font)
{
    m_font = font;
    m_font.setPointSizeF(font.pointSizeF() - 1);
    m_needUpdateBackground = true;
    update();
}

void GpuEngineChartWidget::drawBackground(QPainter *painter)
{
    // Use CPU-style background and grid drawing
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    auto *dAppHelper = DApplicationHelper::instance();
#else
    auto *dAppHelper = DGuiApplicationHelper::instance();
#endif
    auto palette = dAppHelper->applicationPalette();
    QColor frameColor = palette.color(DPalette::TextTips);
    frameColor.setAlphaF(0.3);

    // Draw frame and fill background (CPU style)
    painter->setPen(QPen(frameColor, 1));
    painter->setBrush(palette.color(QPalette::Base));
    painter->drawRect(m_chartRect);

    // Draw grid with dashed lines (CPU style)
    QPen gridPen;
    QVector<qreal> dashes;
    qreal space = 2;
    dashes << space << space;
    gridPen.setDashPattern(dashes);
    gridPen.setColor(frameColor);
    gridPen.setWidth(0); // 0 leads to line with always 1px
    painter->setPen(gridPen);
    
    int section = 10;

    // Horizontal grid lines
    int totalHeight = m_chartRect.height() - 2;
    int currentHeight = m_chartRect.y() + section;
    while (currentHeight < totalHeight + m_chartRect.y()) {
        painter->drawLine(m_chartRect.x() + 1, currentHeight, m_chartRect.x() + m_chartRect.width() - 1, currentHeight);
        currentHeight += section;
    }

    // Vertical grid lines
    int totalWidth = m_chartRect.width() - 2;
    int currentWidth = m_chartRect.x() + section;
    while (currentWidth < totalWidth + m_chartRect.x()) {
        painter->drawLine(currentWidth, m_chartRect.y() + 1, currentWidth, m_chartRect.y() + m_chartRect.height() - 1);
        currentWidth += section;
    }
}

void GpuEngineChartWidget::drawChart(QPainter *painter)
{
    if (m_utilizationHistory.isEmpty()) {
        return;
    }
    
    painter->setClipRect(m_chartRect);
    
    // Draw the utilization curve
    painter->setPen(QPen(m_engineColor, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    
    QPainterPath path;
    bool pathStarted = false;
    
    for (int i = 0; i < m_utilizationHistory.size(); ++i) {
        qreal utilization = qBound(0.0, m_utilizationHistory[i], 1.0);
        
        // Calculate position (right to left, latest data on the right)
        int x = m_chartRect.right() - (m_chartRect.width() * (m_utilizationHistory.size() - 1 - i)) / (MAX_HISTORY_COUNT - 1);
        int y = m_chartRect.bottom() - static_cast<int>(utilization * m_chartRect.height());
        
        if (!pathStarted) {
            path.moveTo(x, y);
            pathStarted = true;
        } else {
            path.lineTo(x, y);
        }
    }
    
    painter->drawPath(path);
    
    // Draw filled area under the curve with gradient
    if (!path.isEmpty()) {
        QPainterPath fillPath = path;
        fillPath.lineTo(m_chartRect.right(), m_chartRect.bottom());
        fillPath.lineTo(m_chartRect.right() - m_chartRect.width(), m_chartRect.bottom());
        fillPath.closeSubpath();
        
        QLinearGradient gradient(0, m_chartRect.top(), 0, m_chartRect.bottom());
        QColor fillColor = m_engineColor;
        fillColor.setAlpha(100);
        gradient.setColorAt(0, fillColor);
        fillColor.setAlpha(20);
        gradient.setColorAt(1, fillColor);
        
        painter->setBrush(QBrush(gradient));
        painter->setPen(Qt::NoPen);
        painter->drawPath(fillPath);
    }
}

void GpuEngineChartWidget::drawTitle(QPainter *painter)
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    auto *dAppHelper = DApplicationHelper::instance();
#else
    auto *dAppHelper = DGuiApplicationHelper::instance();
#endif
    auto palette = dAppHelper->applicationPalette();
    
    painter->setPen(palette.color(DPalette::TextTips));
    painter->setFont(m_font);
    
    QRect titleRect(MARGINS, 0, width() - 2 * MARGINS, TITLE_HEIGHT);
    painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, m_engineTitle);
    
    // Draw 100% on the right side
    painter->drawText(titleRect, Qt::AlignRight | Qt::AlignVCenter, "100%");
}

void GpuEngineChartWidget::drawAxisLabels(QPainter *painter)
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    auto *dAppHelper = DApplicationHelper::instance();
#else
    auto *dAppHelper = DGuiApplicationHelper::instance();
#endif
    auto palette = dAppHelper->applicationPalette();
    
    painter->setPen(palette.color(DPalette::TextTips));
    
    QFont smallFont = m_font;
    smallFont.setPointSizeF(smallFont.pointSizeF() - 1);
    painter->setFont(smallFont);
    
    QRect axisRect(MARGINS, height() - AXIS_LABEL_HEIGHT, width() - 2 * MARGINS, AXIS_LABEL_HEIGHT);
    painter->drawText(axisRect, Qt::AlignLeft | Qt::AlignVCenter, tr("60秒"));
    painter->drawText(axisRect, Qt::AlignRight | Qt::AlignVCenter, "0");
}

qreal GpuEngineChartWidget::getEngineUtilizationPercent()
{
    // For now, return mock data based on engine type
    // This will be replaced with real backend implementation
    GpuStats stats;
    if (!m_gpuService.readStatsFor(m_device, stats)) {
        return 0.0;
    }

    // Map engine type to corresponding stats field
    switch (m_engineType) {
    case GraphicsEngine:
        return stats.graphicsUtilPercent >= 0 ? stats.graphicsUtilPercent / 100.0 : 0.0;
    case VideoEncodeEngine:
        return stats.videoEncodeUtilPercent >= 0 ? stats.videoEncodeUtilPercent / 100.0 : 0.0;
    case VideoDecodeEngine:
        return stats.videoDecodeUtilPercent >= 0 ? stats.videoDecodeUtilPercent / 100.0 : 0.0;
    case ComputeEngine:
        return stats.computeUtilPercent >= 0 ? stats.computeUtilPercent / 100.0 : 0.0;
    default:
        // Generate some mock data for testing
        static int counter = 0;
        counter++;
        qreal base = (m_engineType + 1) * 0.1; // Different base for each engine
        return base + 0.3 * qSin(counter * 0.1 + m_engineType * 1.5);
    }
}
