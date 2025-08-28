// Copyright (C) 2025 Deepin  
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gpu_detail_view_widget.h"
#include "common/common.h"

#include <DApplication>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <DApplicationHelper>
#else
#include <DGuiApplicationHelper>
#endif
#include <DPalette>
#include <DStyleHelper>
#include <DFontSizeManager>

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <QDebug>
#include <QScrollArea>
#include <algorithm>

DWIDGET_USE_NAMESPACE

using namespace common;

//==============================================================================
// GpuDetailItem - 单个GPU的图表项（类似CPUDetailGrapTableItem）
//==============================================================================

GpuDetailItem::GpuDetailItem(const GpuDevice &device, QWidget *parent)
    : QWidget(parent)
    , m_device(device)
    , m_chartColor("#9C27B0")  // GPU紫色，与侧边栏GPU风格一致
    , m_isFirstRender(false)   // 立即开始渲染和数据收集
{
    setDevice(device);
    // 设置与CPU图表相同的最小尺寸，确保有32格网格（320px + 标题和底部文字空间）
    setMinimumSize(240, 360);
    
    // 初始化空的历史数据缓冲区并立即开始收集数据
    m_utilizationHistory.reserve(m_pointsNumber);
    
    // 立即获取一次数据开始滚动显示
    updateStats();
}

GpuDetailItem::~GpuDetailItem()
{
}

void GpuDetailItem::setDevice(const GpuDevice &device)
{
    m_device = device;
    m_utilizationHistory.clear();
    update();
}

void GpuDetailItem::updateStats()
{
    GpuStats stats;
    qreal newUtilization = 0.0;
    
    if (m_gpuService.readStatsFor(m_device, stats)) {
        if (stats.utilizationPercent >= 0) {
            newUtilization = std::min<qreal>(1.0, std::max<qreal>(0.0, static_cast<qreal>(stats.utilizationPercent) / 100.0));
        }

        // 完全按照CompactGpuMonitor的方式添加数据点
        m_utilizationHistory.append(newUtilization);
        
        // 保持指定数量的数据点
        if (m_utilizationHistory.size() > m_pointsNumber) {
            m_utilizationHistory.pop_front();
        }
        
        update();
    }
}

void GpuDetailItem::startRendering()
{
    // 切换到详细页时调用，开始实际渲染和数据更新
    m_isFirstRender = false;
    update();
}

void GpuDetailItem::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    
    QPainter painter(this);
    
    // 完全复制CPU的Normal模式绘制方法
    const int pensize = 1;
    QFont font = DApplication::font();
    font.setPointSizeF(font.pointSizeF() - 1);
    painter.setFont(font);

    QFont midFont = font;
    midFont.setPointSizeF(font.pointSizeF() - 1);

    int textHeight = painter.fontMetrics().height();
    // 绘制背景（与CPU相同的布局）
    QRect graphicRect = QRect(pensize, textHeight, this->width() - 2 * pensize, this->height() - textHeight - QFontMetrics(midFont).height());
    drawBackground(painter, graphicRect);

    // 绘制文本（与CPU相同的方式）
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    auto *dAppHelper = DApplicationHelper::instance();
#else
    auto *dAppHelper = DGuiApplicationHelper::instance();
#endif
    auto palette = dAppHelper->applicationPalette();
    painter.setPen(palette.color(DPalette::TextTips));
    painter.setRenderHints(QPainter::Antialiasing);

    // 显示GPU型号作为标题（类似CPU显示"CPU"）
    painter.drawText(QRect(pensize, 0, this->width() - 2 * pensize, textHeight), Qt::AlignLeft | Qt::AlignTop, m_device.name);

    painter.setFont(midFont);
    int midTextHeight = painter.fontMetrics().height();

    painter.save();
    QColor midTextColor(palette.color(DPalette::ToolTipText));
    midTextColor.setAlphaF(0.3);
    QPen midTextPen = painter.pen();
    midTextPen.setColor(midTextColor);
    painter.setPen(midTextPen);
    painter.drawText(QRect(pensize, 0, this->width() - 2 * pensize, textHeight), Qt::AlignRight | Qt::AlignBottom, "100%");
    painter.drawText(QRect(pensize, graphicRect.bottom() + pensize, this->width() - 2 * pensize, midTextHeight), Qt::AlignLeft | Qt::AlignVCenter, tr("60 seconds"));
    painter.drawText(QRect(pensize, graphicRect.bottom() + pensize, this->width() - 2 * pensize, midTextHeight), Qt::AlignRight | Qt::AlignVCenter, "0");
    painter.restore();

    // 绘制GPU利用率曲线（完全按照CompactGpuMonitor的实现）
    painter.setClipRect(graphicRect);
    
    // 只有当有数据时才绘制波线
    if (!m_utilizationHistory.isEmpty()) {
        // 转换坐标系到图形区域（参考CompactGpuMonitor）
        painter.translate(graphicRect.x(), graphicRect.y());
        
        const qreal strokeWidth = 2.0;
        const int drawWidth = graphicRect.width() - pensize * 2;
        const int drawHeight = graphicRect.height() - pensize * 2;
        const qreal offsetX = drawWidth + pensize;
        
        // 使用固定的deltaX计算，确保时间轴一致（与CompactGpuMonitor完全相同）
        const qreal deltaX = drawWidth * 1.0 / (m_pointsNumber - 3);
        
        // 创建平滑波形路径（完全参考CompactGpuMonitor）
        QPainterPath wavePath;
        QPainterPath fillPath;
        
        // 从最右边的点开始（最新数据）
        qreal y = (1.0 - m_utilizationHistory.last()) * drawHeight + pensize;
        wavePath.moveTo(offsetX, y);
        fillPath.moveTo(offsetX, drawHeight + pensize); // 从底部开始填充
        fillPath.lineTo(offsetX, y);
        
        // // 绘制平滑曲线连接所有点（从最新到最旧，每个点向左偏移deltaX）
        // for (int i = 1; i < m_utilizationHistory.size(); ++i) {
        //     // 每个历史数据点向左偏移i*deltaX距离
        //     const qreal x = offsetX - (i * deltaX);
        //     const int dataIndex = m_utilizationHistory.size() - 1 - i; // 从最新向前数的索引
        //     const qreal y_curr = (1.0 - m_utilizationHistory.at(dataIndex)) * drawHeight + pensize + 0.5;
            
        //     // 使用平滑线性插值
        //     wavePath.lineTo(x, y_curr);
        //     fillPath.lineTo(x, y_curr);
        // }
        
        // // 完成填充路径回到底部
        // if (m_utilizationHistory.size() > 1) {
        //     fillPath.lineTo(offsetX - ((m_utilizationHistory.size() - 1) * deltaX), drawHeight + pensize);
        // }
        // fillPath.closeSubpath();

        // Draw smooth curve connecting all points
        for (int j = m_utilizationHistory.size() - 2; j >= 0; --j) {
            const qreal x = offsetX - ((m_utilizationHistory.size() - j - 1) * deltaX);
            const qreal y_curr = (1.0 - m_utilizationHistory.at(j)) * drawHeight + pensize + 0.5;
            
            // Use smoother line interpolation
            wavePath.lineTo(x, y_curr);
            fillPath.lineTo(x, y_curr);
        }
        
        // Complete fill path back to bottom
        fillPath.lineTo(offsetX - ((m_utilizationHistory.size() - 1) * deltaX), drawHeight + pensize);
        fillPath.closeSubpath();
        
        // 绘制渐变填充
        QLinearGradient gradient(0, pensize, 0, drawHeight + pensize);
        QColor fillColor = m_chartColor;
        fillColor.setAlphaF(0.3); // 半透明
        gradient.setColorAt(0, fillColor);
        fillColor.setAlphaF(0.1);
        gradient.setColorAt(1, fillColor);
        
        painter.setPen(Qt::NoPen);
        painter.setBrush(QBrush(gradient));
        painter.drawPath(fillPath);
        
        // 绘制主曲线
        painter.setPen(QPen(m_chartColor, strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(wavePath);
        
        // 添加当前点的发光效果
        qreal currentY = (1.0 - m_utilizationHistory.last()) * drawHeight + pensize;
        
        // 外层发光
        QColor glowColor = m_chartColor;
        glowColor.setAlphaF(0.4);
        painter.setPen(QPen(glowColor, 6, Qt::SolidLine, Qt::RoundCap));
        painter.drawPoint(QPointF(offsetX, currentY));
        
        // 内层点
        painter.setPen(QPen(m_chartColor, 2, Qt::SolidLine, Qt::RoundCap));
        painter.drawPoint(QPointF(offsetX, currentY));
    }
}

void GpuDetailItem::drawBackground(QPainter &painter, const QRect &graphicRect)
{
    // 完全复制CPU的drawBackground实现
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    auto *dAppHelper = DApplicationHelper::instance();
#else
    auto *dAppHelper = DGuiApplicationHelper::instance();
#endif
    auto palette = dAppHelper->applicationPalette();
    QColor frameColor = palette.color(DPalette::TextTips);
    frameColor.setAlphaF(0.3);

    painter.setPen(QPen(frameColor, 1));
    painter.setBrush(palette.color(QPalette::Base));
    painter.drawRect(graphicRect);

    // draw grid
    QPen gridPen;
    QVector<qreal> dashes;
    qreal space = 2;
    dashes << space << space;
    gridPen.setDashPattern(dashes);
    gridPen.setColor(frameColor);
    //set to 0 lead to line with always 1px
    gridPen.setWidth(0);
    painter.setPen(gridPen);
    int section = 10;

    int totalHeight = graphicRect.height() - 2;
    int currentHeight = graphicRect.y() + section;
    while (currentHeight < totalHeight + graphicRect.y()) {
        painter.drawLine(graphicRect.x() + 1, currentHeight, graphicRect.x() + graphicRect.width() - 1, currentHeight);
        currentHeight += section;
    }

    int totalWidth = graphicRect.width() - 2;
    int currentWidth = graphicRect.x() + section;
    while (currentWidth < totalWidth + graphicRect.x()) {
        painter.drawLine(currentWidth, graphicRect.y() + 1, currentWidth, graphicRect.y() + graphicRect.height() - 1);
        currentWidth += section;
    }
}



//==============================================================================
// GpuDetailViewWidget - 主GPU详细视图（参考CPUDetailWidget）
//==============================================================================

GpuDetailViewWidget::GpuDetailViewWidget(QWidget *parent)
    : BaseDetailViewWidget(parent)
    , m_scrollArea(nullptr)
    , m_scrollContent(nullptr)
    , m_scrollLayout(nullptr)
    , m_emptyStateLabel(nullptr)
    , m_updateTimer(new QTimer(this))
    , m_isFirstEnter(true)
{
    setObjectName("GpuDetailViewWidget");
    
    setupUI();
    
    // 设置更新定时器（与CPU相同的2秒间隔）
    m_updateTimer->setInterval(UPDATE_INTERVAL_MS);
    connect(m_updateTimer, &QTimer::timeout, this, &GpuDetailViewWidget::onUpdateData);
    m_updateTimer->start();
    
    // 立即触发一次数据更新
    onUpdateData();
}

GpuDetailViewWidget::~GpuDetailViewWidget()
{
    if (m_updateTimer) {
        m_updateTimer->stop();
    }
    clearGpuItems();
}

void GpuDetailViewWidget::setupUI()
{
    // 创建滚动区域（支持多GPU）
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameStyle(QFrame::NoFrame);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    // 创建滚动内容
    m_scrollContent = new QWidget();
    m_scrollLayout = new QVBoxLayout(m_scrollContent);
    m_scrollLayout->setContentsMargins(SCROLL_MARGIN, SCROLL_MARGIN, SCROLL_MARGIN, SCROLL_MARGIN);
    m_scrollLayout->setSpacing(GPU_SPACING);
    m_scrollLayout->setAlignment(Qt::AlignTop);
    m_scrollContent->setLayout(m_scrollLayout);
    
    m_scrollArea->setWidget(m_scrollContent);
    
    // 创建空状态标签
    m_emptyStateLabel = new QLabel(tr("未检测到可监测的 GPU 设备或驱动，请检查硬件及驱动"), this);
    m_emptyStateLabel->setAlignment(Qt::AlignCenter);
    m_emptyStateLabel->setWordWrap(true);
    m_emptyStateLabel->hide();
    
    // 设置主布局
    m_centralLayout->addWidget(m_scrollArea);
    m_centralLayout->addWidget(m_emptyStateLabel);
}

void GpuDetailViewWidget::detailFontChanged(const QFont &font)
{
    // 传播字体变化到所有GPU项
    if (m_emptyStateLabel) {
        m_emptyStateLabel->setFont(font);
    }
    
    for (auto &item : m_gpuItems) {
        if (item.summaryTable) {
            item.summaryTable->fontChanged(font);
        }
    }
}

void GpuDetailViewWidget::onViewEntered()
{
    m_isFirstEnter = true;
    
    // 启动所有GPU图表的渲染（切换到详细页时开始滚动显示）
    for (auto &item : m_gpuItems) {
        if (item.chartItem) {
            item.chartItem->startRendering();
        }
    }
}

void GpuDetailViewWidget::onUpdateData()
{
    // 更新设备列表
    updateGpuDevices();
    
    // 更新所有GPU项的数据
    for (auto &item : m_gpuItems) {
        if (item.chartItem) {
            item.chartItem->updateStats();
        }
        if (item.summaryTable) {
            item.summaryTable->updateData();
        }
    }
}

void GpuDetailViewWidget::updateGpuDevices()
{
    QVector<GpuDevice> devices = m_gpuService.devices();
    
    // 检查设备列表是否变化
    bool devicesChanged = (devices.size() != m_devices.size());
    if (!devicesChanged) {
        for (int i = 0; i < devices.size(); ++i) {
            if (devices[i].name != m_devices[i].name || 
                devices[i].vendor != m_devices[i].vendor) {
                devicesChanged = true;
                break;
            }
        }
    }
    
    if (devicesChanged || m_isFirstEnter) {
        m_devices = devices;
        
        if (m_devices.isEmpty()) {
            showEmptyState();
        } else {
            hideEmptyState();
            sortDevicesByUtilization(m_devices);
            createGpuItems();
            
            // 更新标题文本
            if (m_devices.size() == 1) {
                setDetail(m_devices[0].name);
            } else {
                setDetail(tr("%1 个GPU设备").arg(m_devices.size()));
            }
        }
        
        m_isFirstEnter = false;
    }
}

void GpuDetailViewWidget::sortDevicesByUtilization(QVector<GpuDevice> &devices)
{
    // 按利用率排序（高的在前）
    std::sort(devices.begin(), devices.end(), [this](const GpuDevice &a, const GpuDevice &b) {
        quint64 utilA = getDeviceUtilization(a);
        quint64 utilB = getDeviceUtilization(b);
        
        if (utilA != utilB) {
            return utilA > utilB;
        }
        
        // 利用率相同时按厂商优先级
        auto getVendorPriority = [](GpuVendor vendor) {
            switch (vendor) {
            case GpuVendor::Nvidia: return 1;
            case GpuVendor::AMD: return 2;
            case GpuVendor::Intel: return 3;
            default: return 4;
            }
        };
        return getVendorPriority(a.vendor) < getVendorPriority(b.vendor);
    });
}

void GpuDetailViewWidget::createGpuItems()
{
    clearGpuItems();
    
    for (const GpuDevice &device : m_devices) {
        GpuItemGroup itemGroup;
        
        // 创建容器
        itemGroup.containerWidget = new QWidget(m_scrollContent);
        itemGroup.containerLayout = new QVBoxLayout(itemGroup.containerWidget);
        itemGroup.containerLayout->setSpacing(CHART_SUMMARY_SPACING);
        itemGroup.containerLayout->setContentsMargins(0, 0, 0, 0);
        
        // 创建GPU图表项（类似CPUDetailGrapTableItem）
        itemGroup.chartItem = new GpuDetailItem(device, itemGroup.containerWidget);
        itemGroup.containerLayout->addWidget(itemGroup.chartItem);
        
        // 连接更新信号，确保定时器能触发GPU图表更新
        connect(m_updateTimer, &QTimer::timeout, itemGroup.chartItem, &GpuDetailItem::updateStats);
        
        // 如果不是首次进入，立即启动渲染
        if (!m_isFirstEnter) {
            itemGroup.chartItem->startRendering();
        }
        
        // 创建GPU属性表（使用现有的GpuDetailSummaryTable）
        itemGroup.summaryTable = new GpuDetailSummaryTable(itemGroup.containerWidget);
        itemGroup.summaryTable->setGpuDevice(device);
        itemGroup.containerLayout->addWidget(itemGroup.summaryTable);
        
        itemGroup.containerWidget->setLayout(itemGroup.containerLayout);
        
        // 添加到滚动布局
        m_scrollLayout->addWidget(itemGroup.containerWidget);
        
        // 保存项组
        m_gpuItems.append(itemGroup);
    }
    
    // 添加弹性空间
    m_scrollLayout->addStretch();
}

void GpuDetailViewWidget::clearGpuItems()
{
    for (auto &item : m_gpuItems) {
        if (item.containerWidget) {
            m_scrollLayout->removeWidget(item.containerWidget);
            item.containerWidget->deleteLater();
        }
    }
    m_gpuItems.clear();
}

void GpuDetailViewWidget::showEmptyState()
{
    m_scrollArea->hide();
    m_emptyStateLabel->show();
}

void GpuDetailViewWidget::hideEmptyState()
{
    if (m_emptyStateLabel->isVisible()) {
        m_emptyStateLabel->hide();
        m_scrollArea->show();
    }
}

quint64 GpuDetailViewWidget::getDeviceUtilization(const GpuDevice &device) const
{
    GpuStats stats;
    if (const_cast<GpuService&>(m_gpuService).readStatsFor(device, stats)) {
        return static_cast<quint64>(stats.utilizationPercent);
    }
    return 0;
}

#include "gpu_detail_view_widget.moc"