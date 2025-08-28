// Copyright (C) 2025 Deepin
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GPU_DETAIL_VIEW_WIDGET_H
#define GPU_DETAIL_VIEW_WIDGET_H

#include "base/base_detail_view_widget.h"
#include "system/gpu_backend.h"
#include "gpu_detail_summary_widget.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QTimer>
#include <QPainter>

DWIDGET_USE_NAMESPACE
using namespace core::system;

/**
 * @brief GPU详细项 - 单个GPU的图表和绘制（类似CPUDetailGrapTableItem）
 */
class GpuDetailItem : public QWidget
{
    Q_OBJECT

public:
    explicit GpuDetailItem(const GpuDevice &device, QWidget *parent = nullptr);
    ~GpuDetailItem();

    void setDevice(const GpuDevice &device);

public slots:
    void updateStats();
    void startRendering(); // 开始渲染，切换到详细页时调用

protected:
    void paintEvent(QPaintEvent *event) override;
    
private:
    void drawBackground(QPainter &painter, const QRect &graphicRect);

private:
    GpuDevice m_device;
    GpuService m_gpuService;
    QList<qreal> m_utilizationHistory;
    QColor m_chartColor;
    bool m_isFirstRender;                   // 首次渲染标志，用于控制滚动显示
    static constexpr int m_pointsNumber = 30; // 数据点数量，60秒/2秒间隔
};

/**
 * @brief GPU详细信息视图组件（参考CPU详细页风格）
 */
class GpuDetailViewWidget : public BaseDetailViewWidget
{
    Q_OBJECT
    
public:
    explicit GpuDetailViewWidget(QWidget *parent = nullptr);
    virtual ~GpuDetailViewWidget();

public slots:
    void detailFontChanged(const QFont &font);
    void onViewEntered();

private slots:
    void onUpdateData();

private:
    void setupUI();
    void updateGpuDevices();
    void sortDevicesByUtilization(QVector<GpuDevice> &devices);
    void createGpuItems();
    void clearGpuItems();
    void showEmptyState();
    void hideEmptyState();
    
    quint64 getDeviceUtilization(const GpuDevice &device) const;

private:
    // GPU service and data
    GpuService m_gpuService;
    QVector<GpuDevice> m_devices;
    
    // UI components (scrollable layout for multiple GPUs like CPU style)
    QScrollArea *m_scrollArea;
    QWidget *m_scrollContent;
    QVBoxLayout *m_scrollLayout;
    
    // GPU items (each GPU has chart + summary table)
    struct GpuItemGroup {
        GpuDetailItem *chartItem;               // GPU图表项（类似CPUDetailGrapTableItem）
        GpuDetailSummaryTable *summaryTable;   // GPU属性表（使用现有的类）
        QWidget *containerWidget;              // 容器组件
        QVBoxLayout *containerLayout;          // 容器布局
    };
    
    QList<GpuItemGroup> m_gpuItems;
    
    // Empty state
    QLabel *m_emptyStateLabel;
    
    // Update timer (2 second interval)
    QTimer *m_updateTimer;
    
    // State tracking
    bool m_isFirstEnter;
    
    // Layout constants
    static constexpr int SCROLL_MARGIN = 20;
    static constexpr int GPU_SPACING = 30;           // Spacing between GPU items
    static constexpr int CHART_SUMMARY_SPACING = 16; // Spacing between chart and summary
    static constexpr int UPDATE_INTERVAL_MS = 2000;  // 2 seconds
};

#endif // GPU_DETAIL_VIEW_WIDGET_H