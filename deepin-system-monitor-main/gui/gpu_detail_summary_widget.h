// Copyright (C) 2025 Deepin
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GPU_DETAIL_SUMMARY_WIDGET_H
#define GPU_DETAIL_SUMMARY_WIDGET_H

#include <DTableView>
#include <QHeaderView>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <DApplicationHelper>
#else
#include <DGuiApplicationHelper>
#endif
#include <QAbstractTableModel>
#include <QStyledItemDelegate>
#include <QPainter>
#include <DApplication>
#include <DStyle>
#include <QScroller>

#include "common/common.h"
#include "../system/gpu_backend.h"
#include "base/base_detail_item_delegate.h"

using namespace core::system;
using namespace common::format;

DWIDGET_USE_NAMESPACE

class GpuSummaryTableModel;

class GpuDetailSummaryTable : public DTableView
{
    Q_OBJECT
    
public:
    explicit GpuDetailSummaryTable(QWidget *parent = nullptr);
    
    void setGpuDevice(const GpuDevice &device);
    void updateData();

public slots:
    void fontChanged(const QFont &font);
    void onModelUpdate();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QFont m_font;
    GpuSummaryTableModel *m_model;
    GpuDevice m_device;
    GpuService m_gpuService;
};

class GpuSummaryTableModel : public QAbstractTableModel
{
    Q_OBJECT
    
public:
    explicit GpuSummaryTableModel(QObject *parent = nullptr);
    virtual ~GpuSummaryTableModel();
    
    void setGpuDevice(const GpuDevice &device);
    void updateStats(const GpuStats &stats);

protected:
    int rowCount(const QModelIndex &) const override
    {
        return 3; // 3 rows for 6 parameters (2 per row)
    }

    int columnCount(const QModelIndex &) const override
    {
        return 2; // 2 columns
    }

    QVariant data(const QModelIndex &index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &) const override;

private:
    QString formatMemorySize(quint64 bytes) const;
    QString formatFrequency(qint64 khz) const;
    QString formatTemperature(int tempC) const;
    QString formatUtilization(int percent) const;

private slots:
    void onModelUpdated();

private:
    GpuDevice m_device;
    GpuStats m_stats;
};

#endif // GPU_DETAIL_SUMMARY_WIDGET_H
