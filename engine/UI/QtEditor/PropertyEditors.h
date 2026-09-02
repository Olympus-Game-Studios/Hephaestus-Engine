#pragma once

/**
 * @file PropertyEditors.h
 * @brief Shared Qt Inspector property widgets and mixed-value helpers.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "../../UI/InspectorSelectionModel.h"
#include "../../Core/Math.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QWidget>
#include <functional>
#include <vector>

namespace Hephaestus {
namespace QtEditor {

constexpr double kInspectorMixedSentinel = -1.0e12;

void ConfigureMixedSpin(QDoubleSpinBox* spin, double min, double max, double step, int decimals);
void SetSpinFromMixed(QDoubleSpinBox* spin, const MixedScalar& mixed);
bool TryReadSpin(const QDoubleSpinBox* spin, double& out);
void SetCheckFromMixed(QCheckBox* box, const MixedBool& mixed);
bool TryReadCheck(const QCheckBox* box, bool& out);
void SetComboFromMixed(QComboBox* combo, bool mixed, int userData);
bool TryReadCombo(const QComboBox* combo, int& outUserData);

class NumericPropertyEditor : public QWidget
{
    Q_OBJECT
public:
    explicit NumericPropertyEditor(const QString& label, QWidget* parent = nullptr);

    void setRange(double min, double max);
    void setDecimals(int decimals);
    void setSingleStep(double step);
    void setSuffix(const QString& suffix);
    void setSliderEnabled(bool enabled);
    void setValue(double value);
    void setMixed(bool mixed);
    void setFromMixed(const MixedScalar& mixed);
    double value() const;
    bool isMixed() const { return m_mixed; }
    QDoubleSpinBox* spinBox() const { return m_spin; }
    QLabel* labelWidget() const { return m_label; }

signals:
    void valueEdited(double value);
    void editingFinished();
    void copyRequested();
    void pasteRequested();
    void resetRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void emitIfReal(double value);
    QLabel* m_label = nullptr;
    QDoubleSpinBox* m_spin = nullptr;
    QSlider* m_slider = nullptr;
    double m_realMin = 0.0;
    double m_realMax = 1.0;
    bool m_mixed = false;
    bool m_scrubbing = false;
    QPoint m_scrubOrigin;
    double m_scrubStart = 0.0;
};

class Vec3PropertyEditor : public QWidget
{
    Q_OBJECT
public:
    explicit Vec3PropertyEditor(const QString& label, QWidget* parent = nullptr);

    void setRange(double min, double max);
    void setDecimals(int decimals);
    void setSingleStep(double step);
    void setSuffix(const QString& suffix);
    void setUniformLinkVisible(bool visible);
    void setUniformLinked(bool linked);
    bool isUniformLinked() const;
    void setFromMixed(const MixedVec3Axes& mixed);
    void setValue(const Vec3& value);

signals:
    void axisEdited(int axis, double value);
    void editingFinished();
    void copyRequested();
    void pasteRequested();
    void resetRequested();

private:
    NumericPropertyEditor* m_axes[3] = {};
    QPushButton* m_linkBtn = nullptr;
    bool m_updating = false;
};

class BoolPropertyEditor : public QWidget
{
    Q_OBJECT
public:
    explicit BoolPropertyEditor(const QString& label, QWidget* parent = nullptr);
    void setFromMixed(const MixedBool& mixed);
    void setValue(bool value);
    QCheckBox* checkBox() const { return m_check; }

signals:
    void valueEdited(bool value);
    void copyRequested();
    void pasteRequested();

private:
    QCheckBox* m_check = nullptr;
};

class EnumPropertyEditor : public QWidget
{
    Q_OBJECT
public:
    explicit EnumPropertyEditor(const QString& label, QWidget* parent = nullptr);
    void addItem(const QString& text, int userData);
    void setFromMixed(bool mixed, int userData);
    QComboBox* combo() const { return m_combo; }

signals:
    void valueEdited(int userData);

private:
    QLabel* m_label = nullptr;
    QComboBox* m_combo = nullptr;
    bool m_hasMixedItem = false;
};

class ColorPropertyEditor : public QWidget
{
    Q_OBJECT
public:
    explicit ColorPropertyEditor(const QString& label, QWidget* parent = nullptr);
    void setLinearRgb(const Vec3& linear, bool mixed);
    Vec3 linearRgb() const;
    bool isMixed() const { return m_mixed; }

signals:
    void colorEdited(const Vec3& linear);
    void copyRequested();
    void pasteRequested();
    void resetRequested();

private:
    void pickColor();
    void applyHex();
    void refreshSwatch();
    QPushButton* m_swatch = nullptr;
    QLineEdit* m_hex = nullptr;
    QColor m_srgb;
    bool m_mixed = false;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif
