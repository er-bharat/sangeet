#include "eq_window.h"

#include <QComboBox>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>

namespace {
constexpr double kMarginLeft = 46.0;
constexpr double kMarginTop = 14.0;
constexpr double kMarginRight = 18.0;
constexpr double kMarginBottom = 30.0;
constexpr double kDotRadius = 7.0;
constexpr double kHitRadius = 12.0;
}

// ============================================================================
// EqCurveWidget
// ============================================================================

EqCurveWidget::EqCurveWidget(Eq::State *state, QWidget *parent)
    : QWidget(parent), m_state(state)
{
    setMouseTracking(true);
    setMinimumSize(520, 300);
}

QRectF EqCurveWidget::plotRect() const
{
    return QRectF(kMarginLeft, kMarginTop,
                  width() - kMarginLeft - kMarginRight,
                  height() - kMarginTop - kMarginBottom);
}

double EqCurveWidget::freqToX(double freq) const
{
    const QRectF r = plotRect();
    const double lo = std::log10(Eq::kMinFreq);
    const double hi = std::log10(Eq::kMaxFreq);
    const double t = (std::log10(freq) - lo) / (hi - lo);
    return r.left() + t * r.width();
}

double EqCurveWidget::xToFreq(double x) const
{
    const QRectF r = plotRect();
    const double lo = std::log10(Eq::kMinFreq);
    const double hi = std::log10(Eq::kMaxFreq);
    const double t = qBound(0.0, (x - r.left()) / r.width(), 1.0);
    return Eq::clampFreq(std::pow(10.0, lo + t * (hi - lo)));
}

double EqCurveWidget::gainToY(double gainDb) const
{
    const QRectF r = plotRect();
    return r.center().y() - gainDb / Eq::kMaxGainDb * (r.height() / 2.0);
}

double EqCurveWidget::yToGain(double y) const
{
    const QRectF r = plotRect();
    return Eq::clampGain((r.center().y() - y) / (r.height() / 2.0) * Eq::kMaxGainDb);
}

int EqCurveWidget::hitTest(const QPointF &pos) const
{
    int best = -1;
    double bestDist = kHitRadius * kHitRadius;
    for (int i = 0; i < m_state->bands.size(); ++i) {
        const Eq::Band &b = m_state->bands[i];
        const QPointF d = QPointF(freqToX(b.freq), gainToY(b.gainDb)) - pos;
        const double dist = d.x() * d.x() + d.y() * d.y();
        if (dist <= bestDist) {
            bestDist = dist;
            best = i;
        }
    }
    return best;
}

void EqCurveWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QPalette pal = palette();
    const QRectF plot = plotRect();
    const bool on = m_state->enabled;

    const QColor text = pal.color(QPalette::Text);
    const QColor accent = pal.color(QPalette::Highlight);
    QColor gridColor = text;   gridColor.setAlpha(30);
    QColor axisColor = text;   axisColor.setAlpha(140);
    QColor labelColor = text;  labelColor.setAlpha(170);

    QFont small = font();
    small.setPointSizeF(qMax(7.0, font().pointSizeF() * 0.85));
    p.setFont(small);
    const QFontMetricsF fm(small);

    p.fillRect(plot, pal.color(QPalette::Base));

    // --- vertical frequency grid ---------------------------------------
    p.setPen(QPen(gridColor, 1.0));
    for (double decade : {10.0, 100.0, 1000.0, 10000.0}) {
        for (int m = 1; m <= 9; ++m) {
            const double f = decade * m;
            if (f < Eq::kMinFreq || f > Eq::kMaxFreq)
                continue;
            const double x = freqToX(f);
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }
    }

    // --- horizontal gain grid + labels ---------------------------------
    for (int g = -15; g <= 15; g += 5) {
        const double y = gainToY(g);
        if (g != 0) {
            p.setPen(QPen(gridColor, 1.0));
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        QString label = g > 0 ? QStringLiteral("+%1").arg(g) : QString::number(g);
        if (g == 0)
            label = QStringLiteral("0 dB");
        p.setPen(labelColor);
        p.drawText(QRectF(0, y - fm.height() / 2.0, kMarginLeft - 6.0, fm.height()),
                   Qt::AlignRight | Qt::AlignVCenter, label);
    }

    QColor border = text;
    border.setAlpha(70);
    p.setPen(QPen(border, 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRect(plot);

    // --- centre line carrying the frequency scale -----------------------
    const double y0 = gainToY(0.0);
    p.setPen(QPen(axisColor, 1.5));
    p.drawLine(QPointF(plot.left(), y0), QPointF(plot.right(), y0));

    struct Mark { double f; const char *label; };
    static const Mark marks[] = {
        {20, "20"},   {50, "50"},   {100, "100"}, {200, "200"}, {500, "500"},
        {1000, "1k"}, {2000, "2k"}, {5000, "5k"}, {10000, "10k"}, {20000, "20k"}};

    for (const Mark &m : marks) {
        const double x = freqToX(m.f);
        p.setPen(QPen(axisColor, 1.0));
        p.drawLine(QPointF(x, y0 - 3.0), QPointF(x, y0 + 3.0));

        const QString s = QString::fromLatin1(m.label);
        const double w = fm.horizontalAdvance(s);
        const double left = qBound(plot.left() + 2.0, x - w / 2.0, plot.right() - w - 2.0);
        p.setPen(labelColor);
        p.drawText(QPointF(left, y0 + fm.ascent() + 5.0), s);
    }
    p.setPen(labelColor);
    p.drawText(QRectF(0, y0 + 4.0, kMarginLeft - 6.0, fm.height()),
               Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("Hz"));

    // --- response curve --------------------------------------------------
    p.save();
    p.setClipRect(plot);

    QPainterPath curve;
    const int steps = qMax(2, static_cast<int>(plot.width() / 2.0));
    for (int i = 0; i <= steps; ++i) {
        const double x = plot.left() + plot.width() * i / steps;
        const double db = Eq::totalResponseDb(*m_state, xToFreq(x));
        const double y = qBound(plot.top() - 40.0, gainToY(db), plot.bottom() + 40.0);
        if (i == 0)
            curve.moveTo(x, y);
        else
            curve.lineTo(x, y);
    }

    QPainterPath fill = curve;
    fill.lineTo(plot.right(), y0);
    fill.lineTo(plot.left(), y0);
    fill.closeSubpath();

    QColor fillColor = accent;
    fillColor.setAlpha(on ? 70 : 20);
    p.fillPath(fill, fillColor);

    QColor lineColor = accent;
    if (!on)
        lineColor.setAlpha(90);
    p.setPen(QPen(lineColor, 2.2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(curve);
    p.restore();

    // --- dots --------------------------------------------------------------
    for (int i = 0; i < m_state->bands.size(); ++i) {
        const Eq::Band &b = m_state->bands[i];
        const QPointF c(freqToX(b.freq), gainToY(b.gainDb));
        const bool active = (i == m_hover || i == m_drag);
        const double r = active ? kDotRadius + 2.0 : kDotRadius;

        QColor dot = on ? accent : pal.color(QPalette::Mid);
        p.setPen(QPen(pal.color(QPalette::Base), 2.0));
        p.setBrush(active ? dot.lighter(125) : dot);
        p.drawEllipse(c, r, r);
    }

    // --- readout for the active dot ----------------------------------------
    const int info = m_drag >= 0 ? m_drag : m_hover;
    if (info >= 0 && info < m_state->bands.size()) {
        const Eq::Band &b = m_state->bands[info];
        const QPointF c(freqToX(b.freq), gainToY(b.gainDb));
        const QString gain = (b.gainDb >= 0 ? QStringLiteral("+") : QString())
                             + QString::number(b.gainDb, 'f', 1);
        const QString s = Eq::formatFreq(b.freq) + "   " + gain + " dB   Q "
                          + QString::number(b.q, 'f', 2);

        const double w = fm.horizontalAdvance(s) + 14.0;
        const double h = fm.height() + 6.0;
        double bx = qBound(plot.left() + 2.0, c.x() - w / 2.0, plot.right() - w - 2.0);
        double by = c.y() - kDotRadius - h - 8.0;
        if (by < plot.top() + 2.0)
            by = c.y() + kDotRadius + 8.0;

        const QRectF box(bx, by, w, h);
        p.setPen(Qt::NoPen);
        p.setBrush(pal.color(QPalette::ToolTipBase));
        p.drawRoundedRect(box, 4, 4);
        p.setPen(pal.color(QPalette::ToolTipText));
        p.drawText(box, Qt::AlignCenter, s);
    }

    // --- hint -----------------------------------------------------------------
    p.setPen(labelColor);
    p.drawText(QRectF(plot.left(), plot.bottom() + 6.0, plot.width(), kMarginBottom - 8.0),
               Qt::AlignCenter,
               tr("Double-click: add point   •   Drag: frequency & gain   •   "
                  "Wheel: Q   •   Right-click: remove"));
}

void EqCurveWidget::mousePressEvent(QMouseEvent *e)
{
    const int hit = hitTest(e->position());

    if (e->button() == Qt::LeftButton) {
        if (hit >= 0) {
            m_drag = m_hover = hit;
            setCursor(Qt::ClosedHandCursor);
            update();
        }
    } else if (e->button() == Qt::RightButton && hit >= 0) {
        m_state->bands.removeAt(hit);
        m_hover = m_drag = -1;
        unsetCursor();
        emit structureChanged();
        update();
    }
}

void EqCurveWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;

    const QPointF pos = e->position();

    // Double-click on an existing dot just keeps dragging it.
    const int hit = hitTest(pos);
    if (hit >= 0) {
        m_drag = m_hover = hit;
        return;
    }

    if (!plotRect().contains(pos) || m_state->bands.size() >= Eq::kMaxBands)
        return;

    Eq::Band b;
    b.freq = xToFreq(pos.x());
    b.gainDb = yToGain(pos.y());
    b.q = Eq::kDefaultQ;
    m_state->bands.append(b);

    // The button is still down: continue as a drag so the new dot can be
    // placed precisely without a second click.
    m_drag = m_hover = static_cast<int>(m_state->bands.size()) - 1;
    setCursor(Qt::ClosedHandCursor);
    emit structureChanged();
    update();
}

void EqCurveWidget::mouseMoveEvent(QMouseEvent *e)
{
    const QPointF pos = e->position();

    if (m_drag >= 0 && m_drag < m_state->bands.size() && (e->buttons() & Qt::LeftButton)) {
        Eq::Band &b = m_state->bands[m_drag];
        b.freq = xToFreq(pos.x());
        b.gainDb = yToGain(pos.y());
        emit bandChanged(m_drag);
        update();
        return;
    }

    const int hit = hitTest(pos);
    if (hit != m_hover) {
        m_hover = hit;
        if (hit >= 0)
            setCursor(Qt::PointingHandCursor);
        else
            unsetCursor();
        update();
    }
}

void EqCurveWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || m_drag < 0)
        return;

    m_drag = -1;
    m_hover = hitTest(e->position());
    if (m_hover >= 0)
        setCursor(Qt::PointingHandCursor);
    else
        unsetCursor();
    update();
}

void EqCurveWidget::wheelEvent(QWheelEvent *e)
{
    const int idx = hitTest(e->position());
    if (idx < 0) {
        e->ignore();
        return;
    }

    const double steps = e->angleDelta().y() / 120.0;
    Eq::Band &b = m_state->bands[idx];
    b.q = Eq::clampQ(b.q * std::pow(1.12, steps));
    m_hover = idx;
    emit bandChanged(idx);
    update();
    e->accept();
}

void EqCurveWidget::leaveEvent(QEvent *)
{
    if (m_drag < 0 && m_hover >= 0) {
        m_hover = -1;
        unsetCursor();
        update();
    }
}

// ============================================================================
// EqualizerWindow
// ============================================================================

EqualizerWindow::EqualizerWindow(const Eq::State &state,
                                 const Eq::PresetList &userPresets,
                                 QWidget *parent)
    : QDialog(parent),
      m_state(state),
      m_userPresets(userPresets),
      m_builtinPresets(Eq::builtinPresets())
{
    setWindowTitle(tr("Equalizer"));
    resize(920, 480);

    // ---- top row: preset list | on/off | save preset ----------------------
    m_presetBox = new QComboBox;
    m_presetBox->setMinimumWidth(220);

    m_enableButton = new QPushButton;
    m_enableButton->setCheckable(true);
    m_enableButton->setMinimumWidth(70);
    m_enableButton->setToolTip(tr("Turn the equalizer on or off"));

    m_saveButton = new QPushButton(tr("Save preset…"));

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Preset")));
    top->addWidget(m_presetBox, 1);
    top->addWidget(m_enableButton);
    top->addWidget(m_saveButton);

    // ---- left: vertical preamp -------------------------------------------
    m_preamp = new QSlider(Qt::Vertical);
    m_preamp->setRange(qRound(-Eq::kMaxGainDb * 10), qRound(Eq::kMaxGainDb * 10));  // 0.1 dB steps
    m_preamp->setTickPosition(QSlider::TicksRight);
    m_preamp->setTickInterval(50);
    m_preamp->setToolTip(tr("Preamp: lower it if boosted bands cause distortion"));

    m_preampValue = new QLabel;
    m_preampValue->setAlignment(Qt::AlignCenter);
    m_preampValue->setMinimumWidth(64);

    auto *preampTitle = new QLabel(tr("Preamp"));
    preampTitle->setAlignment(Qt::AlignCenter);

    auto *preampColumn = new QVBoxLayout;
    preampColumn->addWidget(m_preampValue, 0, Qt::AlignHCenter);
    preampColumn->addWidget(m_preamp, 1, Qt::AlignHCenter);
    preampColumn->addWidget(preampTitle, 0, Qt::AlignHCenter);

    // ---- centre: the curve -------------------------------------------------
    m_curve = new EqCurveWidget(&m_state);

    auto *body = new QHBoxLayout;
    body->addLayout(preampColumn);
    body->addWidget(m_curve, 1);

    auto *root = new QVBoxLayout(this);
    root->addLayout(top);
    root->addLayout(body, 1);

    // ---- wiring -------------------------------------------------------------
    connect(m_presetBox, &QComboBox::activated, this, [this](int i) { applyPreset(i); });

    connect(m_enableButton, &QPushButton::toggled, this, [this](bool on) {
        m_state.enabled = on;
        m_enableButton->setText(on ? tr("On") : tr("Off"));
        m_curve->update();
        emit stateChanged(m_state, Eq::Change::Enabled, -1);
    });

    connect(m_saveButton, &QPushButton::clicked, this, [this]() { savePreset(); });

    connect(m_preamp, &QSlider::valueChanged, this, [this](int v) {
        m_state.preampDb = v / 10.0;
        m_preampValue->setText(QString::number(m_state.preampDb, 'f', 1) + " dB");
        markCustom();
        emit stateChanged(m_state, Eq::Change::Preamp, -1);
    });

    connect(m_curve, &EqCurveWidget::bandChanged, this, [this](int i) {
        markCustom();
        emit stateChanged(m_state, Eq::Change::BandParams, i);
    });

    connect(m_curve, &EqCurveWidget::structureChanged, this, [this]() {
        markCustom();
        emit stateChanged(m_state, Eq::Change::Structure, -1);
    });

    refreshPresetBox();
    syncControls();
}

const Eq::Preset *EqualizerWindow::findPreset(const QString &name) const
{
    for (const Eq::Preset &p : m_builtinPresets)
        if (p.name == name)
            return &p;
    for (const Eq::Preset &p : m_userPresets)
        if (p.name == name)
            return &p;
    return nullptr;
}

void EqualizerWindow::refreshPresetBox()
{
    QSignalBlocker block(m_presetBox);
    m_presetBox->clear();
    m_presetBox->addItem(tr("Custom"), QString());

    for (const Eq::Preset &p : m_builtinPresets)
        m_presetBox->addItem(p.name, p.name);

    if (!m_userPresets.isEmpty()) {
        m_presetBox->insertSeparator(m_presetBox->count());
        for (const Eq::Preset &p : m_userPresets)
            m_presetBox->addItem(p.name, p.name);
    }

    int index = m_state.presetName.isEmpty() ? 0 : m_presetBox->findData(m_state.presetName);
    if (index < 0) {
        index = 0;
        m_state.presetName.clear();   // saved preset no longer exists
    }
    m_presetBox->setCurrentIndex(index);
}

void EqualizerWindow::syncControls()
{
    {
        QSignalBlocker b(m_enableButton);
        m_enableButton->setChecked(m_state.enabled);
        m_enableButton->setText(m_state.enabled ? tr("On") : tr("Off"));
    }
    {
        QSignalBlocker b(m_preamp);
        m_preamp->setValue(qRound(m_state.preampDb * 10.0));
    }
    m_preampValue->setText(QString::number(m_state.preampDb, 'f', 1) + " dB");
    m_curve->update();
}

void EqualizerWindow::markCustom()
{
    if (m_state.presetName.isEmpty())
        return;
    m_state.presetName.clear();
    QSignalBlocker block(m_presetBox);
    m_presetBox->setCurrentIndex(0);
}

void EqualizerWindow::applyPreset(int comboIndex)
{
    const QString name = m_presetBox->itemData(comboIndex).toString();
    if (name.isEmpty())
        return;

    const Eq::Preset *preset = findPreset(name);
    if (!preset)
        return;

    m_state.bands = preset->state.bands;
    m_state.preampDb = preset->state.preampDb;
    m_state.presetName = name;

    syncControls();
    emit stateChanged(m_state, Eq::Change::Structure, -1);
}

void EqualizerWindow::savePreset()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Save preset"), tr("Preset name:"),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;

    if (name.compare(tr("Custom"), Qt::CaseInsensitive) == 0) {
        QMessageBox::warning(this, tr("Save preset"), tr("Please choose a different name."));
        return;
    }
    for (const Eq::Preset &p : m_builtinPresets) {
        if (p.name.compare(name, Qt::CaseInsensitive) == 0) {
            QMessageBox::warning(this, tr("Save preset"),
                                 tr("Built-in presets can't be overwritten. Please choose a different name."));
            return;
        }
    }

    Eq::Preset preset;
    preset.name = name;
    preset.state.preampDb = m_state.preampDb;
    preset.state.bands = m_state.bands;

    bool replaced = false;
    for (Eq::Preset &existing : m_userPresets) {
        if (existing.name.compare(name, Qt::CaseInsensitive) != 0)
            continue;
        if (QMessageBox::question(this, tr("Save preset"),
                                  tr("A preset named \"%1\" already exists. Overwrite it?").arg(existing.name))
            != QMessageBox::Yes)
            return;
        existing = preset;
        replaced = true;
        break;
    }
    if (!replaced)
        m_userPresets.append(preset);

    m_state.presetName = name;
    refreshPresetBox();
    emit userPresetsChanged(m_userPresets);
    emit stateChanged(m_state, Eq::Change::Meta, -1);
}
