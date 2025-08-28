#include "compact_gpu_monitor.h"
#include <QPainter>
#include <QStyleOption>
#include <QPainter>
#include <QStyleOption>
#include <QMouseEvent>
#include <algorithm>

#include <DApplication>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <DApplicationHelper>
#else
#include <DGuiApplicationHelper>
#endif
#include <DPalette>

#include "common/common.h"
#include "compact_gpu_monitor.h"
#include "ddlog.h"

DWIDGET_USE_NAMESPACE

using namespace core::system;

CompactGpuMonitor::CompactGpuMonitor(QWidget *parent)
    : QWidget(parent)
{
    int statusBarMaxWidth = common::getStatusBarMaxWidth();
    setFixedWidth(statusBarMaxWidth);
    setFixedHeight(80);

    // Pick the best device with priority: NVIDIA > AMD > Intel
    const auto devs = m_service.devices();
    selectBestGpuDevice(devs);

    // theme colors
    auto *dAppHelper =
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        DApplicationHelper::instance();
#else
        DGuiApplicationHelper::instance();
#endif
    auto palette = dAppHelper->applicationPalette();
#ifndef THEME_FALLBACK_COLOR
    m_titleColor = palette.color(DPalette::TextTitle);
#else
    m_titleColor = palette.color(DPalette::Text);
#endif
    m_statColor = palette.color(DPalette::TextTips);
    m_frameColor = palette.color(DPalette::TextTips);
    m_frameColor.setAlphaF(0.3);

    // fonts
    changeFont(DApplication::font());
    connect(dynamic_cast<QGuiApplication *>(DApplication::instance()), &DApplication::fontChanged,
            this, &CompactGpuMonitor::changeFont);

    // history buffer
    m_utilizationHistory.reserve(m_pointsNumber);
    for (int i = 0; i < m_pointsNumber; ++i) m_utilizationHistory.append(0.0);

    connect(&m_timer, &QTimer::timeout, this, &CompactGpuMonitor::onTick);
    m_timer.start(2000); // Increased update frequency for more responsive display
}

void CompactGpuMonitor::onTick()
{
    if (!m_hasDevice) {
        // Try select best GPU device dynamically
        const auto devs = m_service.devices();
        selectBestGpuDevice(devs);
        if (!m_hasDevice) return;
    }
    GpuStats s;
    if (m_service.readStatsFor(m_device, s)) {
        m_stats = s;
        qreal util = 0.0;
        if (m_stats.utilizationPercent >= 0) {
            util = std::min<qreal>(1.0, std::max<qreal>(0.0, static_cast<qreal>(m_stats.utilizationPercent) / 100.0));
        }
        // append history
        m_utilizationHistory.append(util);
        if (m_utilizationHistory.size() > m_pointsNumber) m_utilizationHistory.pop_front();
        update();
    } else {
        // If current device failed, attempt failover to best available device
        qCDebug(app) << "Current GPU device failed, attempting failover";
        const auto devs = m_service.devices();
        m_hasDevice = false; // Reset so selectBestGpuDevice can find a new one
        selectBestGpuDevice(devs);
    }
}

void CompactGpuMonitor::paintEvent(QPaintEvent *)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);

    p.setRenderHint(QPainter::Antialiasing);
    int spacing = 4;

    // summary row like CPU card
    QFontMetrics fmTitle(m_titleFont);
    QFontMetrics fmStat(m_statFont);

    const QString title = QStringLiteral("GPU");
    const QString stat = (m_stats.utilizationPercent >= 0)
            ? QString::number(m_stats.utilizationPercent) + "%"
            : QStringLiteral("N/A");

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QRect titleRect(m_pointerRadius * 2 + spacing - 2, 0, fmTitle.width(title), fmTitle.height() + 4);
#else
    QRect titleRect(m_pointerRadius * 2 + spacing - 2, 0, fmTitle.horizontalAdvance(title), fmTitle.height() + 4);
#endif
    QRect sectionRect(0, titleRect.y() + qCeil((titleRect.height() - m_pointerRadius) / 2.),
                      m_pointerRadius, m_pointerRadius);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QRect statRect(titleRect.x() + titleRect.width() + spacing, titleRect.y(), fmStat.width(stat),
                   fmStat.height() + 4);
#else
    QRect statRect(titleRect.x() + titleRect.width() + spacing, titleRect.y(), fmStat.horizontalAdvance(stat),
                   fmStat.height() + 4);
#endif

    // draw section
    p.setPen(m_sectionColor);
    QPainterPath sec;
    sec.addRoundedRect(sectionRect, m_pointerRadius, m_pointerRadius);
    p.fillPath(sec, m_sectionColor);

    // draw title
    p.setPen(m_titleColor);
    p.setFont(m_titleFont);
    p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, title);

    // draw stat
    p.setPen(m_statColor);
    p.setFont(m_statFont);
    p.drawText(statRect, Qt::AlignLeft | Qt::AlignVCenter, stat);

    // grid
    QPen framePen;
    int penSize = 1;
    framePen.setColor(m_frameColor);
    framePen.setWidth(penSize);
    p.setPen(framePen);

    int gridX = rect().x() + penSize;
    int gridY = titleRect.y() + titleRect.height() + 10;
    int gridWidth = rect().width() - 3 - ((rect().width() - 3 - penSize) % (m_gridSize + penSize)) - penSize;
    int gridHeight = 80 + 8 * penSize; // align with CPU height

    p.setRenderHint(QPainter::Antialiasing, false);
    QPainterPath framePath;
    QRect gridFrame(gridX, gridY, gridWidth, gridHeight);
    framePath.addRect(gridFrame);
    p.drawPath(framePath);

    // grid dash lines
    QPen gridPen;
    QVector<qreal> dashes;
    qreal space = 2;
    dashes << 2 << space;
    gridPen.setDashPattern(dashes);
    gridPen.setColor(m_frameColor);
    gridPen.setWidth(0);
    p.setPen(gridPen);

    int gridLineX = gridX;
    while (gridLineX + m_gridSize + penSize < gridX + gridWidth) {
        gridLineX += m_gridSize + penSize;
        p.drawLine(gridLineX, gridY + 1, gridLineX, gridY + gridHeight - 1);
    }
    int gridLineY = gridY;
    while (gridLineY + m_gridSize + penSize < gridY + gridHeight) {
        gridLineY += m_gridSize + penSize;
        p.drawLine(gridX + 1, gridLineY, gridX + gridWidth - 1, gridLineY);
    }

    // clip to grid and draw enhanced waveform of utilization
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clip;
    clip.addRect(gridFrame);
    p.setClipPath(clip);

    p.translate(gridFrame.x(), gridFrame.y());
    const qreal strokeWidth = 2.0; // Slightly thicker for better visibility
    const int drawWidth = gridFrame.width() - penSize * 2;
    const int drawHeight = gridFrame.height() - penSize * 2;
    const qreal offsetX = drawWidth + penSize;
    const qreal deltaX = drawWidth * 1.0 / (m_pointsNumber - 3);

    // Create smooth waveform path
    QPainterPath wavePath;
    QPainterPath fillPath; // Path for gradient fill
    
    if (!m_utilizationHistory.isEmpty()) {
        // Start from right-most point
        qreal y = (1.0 - m_utilizationHistory.last()) * drawHeight + penSize;
        wavePath.moveTo(offsetX, y);
        fillPath.moveTo(offsetX, drawHeight + penSize); // Start fill from bottom
        fillPath.lineTo(offsetX, y);
        
        // Draw smooth curve connecting all points
        for (int j = m_utilizationHistory.size() - 2; j >= 0; --j) {
            const qreal x = offsetX - ((m_utilizationHistory.size() - j - 1) * deltaX);
            const qreal y_curr = (1.0 - m_utilizationHistory.at(j)) * drawHeight + penSize + 0.5;
            
            // Use smoother line interpolation
            wavePath.lineTo(x, y_curr);
            fillPath.lineTo(x, y_curr);
        }
        
        // Complete fill path back to bottom
        fillPath.lineTo(offsetX - ((m_utilizationHistory.size() - 1) * deltaX), drawHeight + penSize);
        fillPath.closeSubpath();
        
        // Draw gradient fill under the curve
        QLinearGradient gradient(0, penSize, 0, drawHeight + penSize);
        QColor fillColor = m_curveColor;
        fillColor.setAlphaF(0.3); // Semi-transparent
        gradient.setColorAt(0, fillColor);
        fillColor.setAlphaF(0.1);
        gradient.setColorAt(1, fillColor);
        
        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(gradient));
        p.drawPath(fillPath);
        
        // Draw the main curve line with enhanced styling
        QColor curveColor = m_curveColor;
        p.setPen(QPen(curveColor, strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(wavePath);
        
        // Add glowing effect for current point
        if (!m_utilizationHistory.isEmpty()) {
            qreal currentY = (1.0 - m_utilizationHistory.last()) * drawHeight + penSize;
            
            // Outer glow
            QColor glowColor = m_curveColor;
            glowColor.setAlphaF(0.4);
            p.setPen(QPen(glowColor, 6, Qt::SolidLine, Qt::RoundCap));
            p.drawPoint(QPointF(offsetX, currentY));
            
            // Inner point
            p.setPen(QPen(m_curveColor, 2, Qt::SolidLine, Qt::RoundCap));
            p.drawPoint(QPointF(offsetX, currentY));
        }
    }

    setFixedHeight(gridFrame.y() + gridFrame.height() + penSize);
}

void CompactGpuMonitor::changeFont(const QFont &font)
{
    m_titleFont = font;
    m_titleFont.setWeight(QFont::Medium);
    m_titleFont.setPointSizeF(m_titleFont.pointSizeF() - 1);
    m_statFont = font;
    m_statFont.setPointSizeF(m_statFont.pointSizeF() - 1);
    resizeItemRect();
}

void CompactGpuMonitor::resizeItemRect()
{
    // Placeholder for parity with CPU card; kept for consistent layout behavior
}

void CompactGpuMonitor::mouseReleaseEvent(QMouseEvent *ev)
{
    if (ev->button() == Qt::LeftButton)
        emit clicked("MSG_GPU");
}

void CompactGpuMonitor::mouseMoveEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
}

void CompactGpuMonitor::selectBestGpuDevice(const QVector<GpuDevice> &devices)
{
    // Sort devices by priority: NVIDIA > AMD > Intel
    QVector<GpuDevice> sortedDevices = devices;
    std::sort(sortedDevices.begin(), sortedDevices.end(), [this](const GpuDevice &a, const GpuDevice &b) {
        int priorityA = getGpuPriority(a.vendor);
        int priorityB = getGpuPriority(b.vendor);
        return priorityA < priorityB; // Lower number = higher priority
    });
    
    // Try to find a device with valid utilization data
    for (const auto &d : sortedDevices) {
        GpuStats tryStats;
        if (m_service.readStatsFor(d, tryStats)) {
            // Prefer devices with non-zero utilization, but accept any working device as fallback
            if (tryStats.utilizationPercent > 0 || !m_hasDevice) {
                m_device = d;
                m_hasDevice = true;
                m_stats = tryStats;
                qCDebug(app) << "Selected GPU device:" << d.name << "vendor:" << static_cast<int>(d.vendor) 
                           << "utilization:" << tryStats.utilizationPercent << "%";
                
                // If we found a device with actual utilization, use it immediately
                if (tryStats.utilizationPercent > 0) {
                    break;
                }
            }
        }
    }
}

int CompactGpuMonitor::getGpuPriority(GpuVendor vendor) const
{
    switch (vendor) {
    case GpuVendor::Nvidia:
        return 1; // Highest priority for discrete NVIDIA cards
    case GpuVendor::AMD:
        return 2; // Second priority for discrete AMD cards  
    case GpuVendor::Intel:
        return 3; // Lowest priority for integrated Intel graphics
    default:
        return 4; // Unknown vendors get lowest priority
    }
}
