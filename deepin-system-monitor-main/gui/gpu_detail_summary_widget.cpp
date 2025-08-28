// Copyright (C) 2025 Deepin
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gpu_detail_summary_widget.h"

#include <DApplication>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <DApplicationHelper>
#else
#include <DGuiApplicationHelper>
#endif
#include <DPalette>
#include <QDebug>

DWIDGET_USE_NAMESPACE

GpuDetailSummaryTable::GpuDetailSummaryTable(QWidget *parent)
    : DTableView(parent)
    , m_font(DApplication::font())
    , m_model(new GpuSummaryTableModel(this))
{
    // Setup table view (similar to CPU implementation)
    setModel(m_model);
    setItemDelegate(new BaseDetailItemDelegate(this));
    
    // Configure table properties
    setFrameStyle(QFrame::NoFrame);
    setShowGrid(false);
    setSelectionMode(QAbstractItemView::NoSelection);
    setSortingEnabled(false);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    // Setup headers
    horizontalHeader()->setVisible(false);
    verticalHeader()->setVisible(false);
    horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    
    // Setup scroller
#if QT_VERSION >= QT_VERSION_CHECK(5, 7, 0)
    QScroller::grabGesture(this);
#endif

    // Connect to model updates
    connect(m_model, &GpuSummaryTableModel::modelReset, this, &GpuDetailSummaryTable::onModelUpdate);
    connect(m_model, &GpuSummaryTableModel::dataChanged, this, &GpuDetailSummaryTable::onModelUpdate);
}

void GpuDetailSummaryTable::setGpuDevice(const GpuDevice &device)
{
    m_device = device;
    m_model->setGpuDevice(device);
}

void GpuDetailSummaryTable::updateData()
{
    GpuStats stats;
    if (m_gpuService.readStatsFor(m_device, stats)) {
        m_model->updateStats(stats);
    }
}

void GpuDetailSummaryTable::fontChanged(const QFont &font)
{
    m_font = font;
    update();
}

void GpuDetailSummaryTable::onModelUpdate()
{
    // Resize columns to content
    resizeColumnsToContents();
    
    // Calculate and set the appropriate height to show all rows without scrolling
    // Use a fixed height based on the known number of rows (3 rows for 6 parameters)
    int totalHeight = 0;
    const int knownRowCount = 3;  // We know we have 3 rows (6 parameters in 2 columns)
    for (int row = 0; row < knownRowCount; ++row) {
        totalHeight += rowHeight(row);
    }
    
    // Add some padding for table frame
    const int padding = 10;
    setFixedHeight(totalHeight + padding);
    
    update();
}

void GpuDetailSummaryTable::paintEvent(QPaintEvent *event)
{
    // Call parent implementation first
    DTableView::paintEvent(event);

    // Draw border and separator like CPU summary table
    QPainter painter(this->viewport());
    painter.setRenderHint(QPainter::Antialiasing, true);
    
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    const auto &palette = DApplicationHelper::instance()->applicationPalette();
#else
    const auto &palette = DGuiApplicationHelper::instance()->applicationPalette();
#endif
    
    QColor frameColor = palette.color(DPalette::FrameBorder);
    const double SUMMARY_CHART_LINE_ALPH = 0.13;
    frameColor.setAlphaF(SUMMARY_CHART_LINE_ALPH);

    painter.setPen(QPen(frameColor, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    
    // Draw vertical separator line between left and right columns
    int separatorX = this->horizontalHeader()->sectionSize(0) - 1;
    painter.drawLine(separatorX, 2, separatorX, this->viewport()->height() - 2);
    
    // Draw rounded border around the entire table
    painter.drawRoundedRect(this->rect().adjusted(1, 1, -1, -1), 6, 6);
}

// GpuSummaryTableModel implementation

GpuSummaryTableModel::GpuSummaryTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

GpuSummaryTableModel::~GpuSummaryTableModel()
{
}

void GpuSummaryTableModel::setGpuDevice(const GpuDevice &device)
{
    m_device = device;
    beginResetModel();
    endResetModel();
}

void GpuSummaryTableModel::updateStats(const GpuStats &stats)
{
    m_stats = stats;
    beginResetModel();
    endResetModel();
}

QVariant GpuSummaryTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    int row = index.row();
    int column = index.column();

    if (role == Qt::DisplayRole) {
        // Display parameter names (left column) and values (right column)
        switch (row) {
        case 0:
            if (column == 0)
                return QApplication::translate("GpuSummaryTableModel", "总利用率");
            else if (column == 1)
                return QApplication::translate("GpuSummaryTableModel", "当前频率");
            break;
        case 1:
            if (column == 0) {
                // Check if it's integrated graphics (Intel) or discrete
                if (m_device.vendor == GpuVendor::Intel)
                    return QApplication::translate("GpuSummaryTableModel", "共享显存");
                else
                    return QApplication::translate("GpuSummaryTableModel", "总显存");
            }
            else if (column == 1)
                return QApplication::translate("GpuSummaryTableModel", "已用显存");
            break;
        case 2:
            if (column == 0)
                return QApplication::translate("GpuSummaryTableModel", "显存频率");
            else if (column == 1)
                return QApplication::translate("GpuSummaryTableModel", "温度");
            break;
        default:
            break;
        }
    } else if (role == Qt::UserRole) {
        // Display parameter values
        switch (row) {
        case 0:
            if (column == 0)
                return formatUtilization(m_stats.utilizationPercent);
            else if (column == 1) {
                return formatFrequency(m_stats.coreClockkHz);
            }
            break;
        case 1:
            if (column == 0) {
                if (m_stats.memoryTotalBytes > 0)
                    return formatMemorySize(m_stats.memoryTotalBytes);
                else
                    return "--";
            }
            else if (column == 1) {
                if (m_stats.memoryUsedBytes > 0)
                    return formatMemorySize(m_stats.memoryUsedBytes);
                else
                    return "--";
            }
            break;
        case 2:
            if (column == 0)
                return formatFrequency(m_stats.memoryClockkHz);
            else if (column == 1)
                return formatTemperature(m_stats.temperatureC);
            break;
        default:
            break;
        }
    } else if (
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        role == Qt::TextColorRole
#else
        role == Qt::ForegroundRole
#endif
    ) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        const auto &palette = DApplicationHelper::instance()->applicationPalette();
#else
        const auto &palette = DGuiApplicationHelper::instance()->applicationPalette();
#endif
        return palette.color(DPalette::Text);
    }

    return QVariant();
}

Qt::ItemFlags GpuSummaryTableModel::flags(const QModelIndex &) const
{
    return Qt::NoItemFlags;
}

QString GpuSummaryTableModel::formatMemorySize(quint64 bytes) const
{
    if (bytes == 0) return "--";
    
    const quint64 GB = 1024 * 1024 * 1024;
    if (bytes >= GB) {
        return QString::number(static_cast<double>(bytes) / GB, 'f', 1) + "GB";
    }
    
    const quint64 MB = 1024 * 1024;
    return QString::number(static_cast<double>(bytes) / MB, 'f', 0) + "MB";
}

QString GpuSummaryTableModel::formatFrequency(qint64 khz) const
{
    if (khz <= 0) return "--";
    
    // Convert kHz to MHz for display
    double mhz = khz / 1000.0;
    
    if (mhz >= 1000) {
        return QString::number(mhz / 1000.0, 'f', 1) + "GHz";
    }
    return QString::number(mhz, 'f', 0) + "MHz";
}

QString GpuSummaryTableModel::formatTemperature(int tempC) const
{
    if (tempC < 0) {
        return QApplication::translate("GpuSummaryTableModel", "驱动未提供温度数据");
    }
    return QString::number(tempC) + "°C";
}

QString GpuSummaryTableModel::formatUtilization(int percent) const
{
    if (percent < 0) return "--";
    return QString::number(percent) + "%";
}

void GpuSummaryTableModel::onModelUpdated()
{
    // This can be connected to external update signals if needed
}


