#pragma once

#include <QDialog>
#include <QWidget>

#include "eq_engine.h"

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;

// The interactive frequency/gain plot.
//   double-click empty space : add a dot (then keep dragging to place it)
//   drag a dot               : frequency (x) and gain (y)
//   mouse wheel over a dot   : Q (bandwidth)
//   right-click a dot        : remove it
class EqCurveWidget : public QWidget
{
    Q_OBJECT
public:
    explicit EqCurveWidget(Eq::State *state, QWidget *parent = nullptr);

signals:
    void bandChanged(int index);   // a dot moved or its Q changed
    void structureChanged();       // a dot was added or removed

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRectF plotRect() const;
    double freqToX(double freq) const;
    double xToFreq(double x) const;
    double gainToY(double gainDb) const;
    double yToGain(double y) const;
    int hitTest(const QPointF &pos) const;

    Eq::State *m_state = nullptr;   // owned by EqualizerWindow
    int m_hover = -1;
    int m_drag = -1;
};

class EqualizerWindow : public QDialog
{
    Q_OBJECT
public:
    EqualizerWindow(const Eq::State &state, const Eq::PresetList &userPresets,
                    QWidget *parent = nullptr);

    const Eq::State &state() const { return m_state; }

signals:
    // Every edit. `band` is only meaningful for Change::BandParams.
    void stateChanged(const Eq::State &state, Eq::Change change, int band);
    void userPresetsChanged(const Eq::PresetList &presets);

private:
    void refreshPresetBox();
    void syncControls();
    void markCustom();
    void applyPreset(int comboIndex);
    void savePreset();
    const Eq::Preset *findPreset(const QString &name) const;

    Eq::State m_state;
    Eq::PresetList m_userPresets;
    Eq::PresetList m_builtinPresets;

    QComboBox *m_presetBox = nullptr;
    QPushButton *m_enableButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QSlider *m_preamp = nullptr;
    QLabel *m_preampValue = nullptr;
    EqCurveWidget *m_curve = nullptr;
};
