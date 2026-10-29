#if HEPHAESTUS_EDITOR_QT_UI

#include "PropertyEditors.h"

#include <QApplication>
#include <QColorDialog>
#include <QEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QStyle>
#include <algorithm>
#include <cmath>

namespace Hephaestus {
namespace QtEditor {

namespace {

QString EmDash()
{
    return QString::fromUtf8("\xE2\x80\x94");
}

int LinearToSrgb8(float v)
{
    v = std::max(0.0f, std::min(1.0f, v));
    const float s = (v <= 0.0031308f) ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return static_cast<int>(s * 255.0f + 0.5f);
}

float Srgb8ToLinear(int c)
{
    const float s = c / 255.0f;
    return (s <= 0.04045f) ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

} // namespace

void ConfigureMixedSpin(QDoubleSpinBox* spin, double min, double max, double step, int decimals)
{
    if (!spin) return;
    spin->setRange(std::min(min, kInspectorMixedSentinel), max);
    spin->setSingleStep(step);
    spin->setDecimals(decimals);
    spin->setSpecialValueText(EmDash());
    spin->setKeyboardTracking(true);
}

void SetSpinFromMixed(QDoubleSpinBox* spin, const MixedScalar& mixed)
{
    if (!spin) return;
    if (!mixed.present || mixed.mixed) {
        spin->setValue(kInspectorMixedSentinel);
        return;
    }
    spin->setValue(mixed.value);
}

bool TryReadSpin(const QDoubleSpinBox* spin, double& out)
{
    if (!spin) return false;
    if (spin->value() <= kInspectorMixedSentinel + 1.0) {
        return false;
    }
    out = spin->value();
    return true;
}

void SetCheckFromMixed(QCheckBox* box, const MixedBool& mixed)
{
    if (!box) return;
    // Tristate only for a mixed selection: otherwise a click on Unchecked cycles to PartiallyChecked,
    // which TryReadCheck ignores, and the box can never be turned back on.
    box->setTristate(!mixed.present || mixed.mixed);
    if (!mixed.present || mixed.mixed) {
        box->setCheckState(Qt::PartiallyChecked);
        return;
    }
    box->setCheckState(mixed.value ? Qt::Checked : Qt::Unchecked);
}

bool TryReadCheck(const QCheckBox* box, bool& out)
{
    if (!box) return false;
    if (box->checkState() == Qt::PartiallyChecked) {
        return false;
    }
    out = box->isChecked();
    return true;
}

void SetComboFromMixed(QComboBox* combo, bool mixed, int userData)
{
    if (!combo) return;
    if (combo->findData(-1) < 0) {
        combo->insertItem(0, EmDash(), -1);
    }
    if (mixed) {
        combo->setCurrentIndex(combo->findData(-1));
        return;
    }
    const int idx = combo->findData(userData);
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
}

bool TryReadCombo(const QComboBox* combo, int& outUserData)
{
    if (!combo) return false;
    const int data = combo->currentData().toInt();
    if (data == -1) {
        return false;
    }
    outUserData = data;
    return true;
}

NumericPropertyEditor::NumericPropertyEditor(const QString& label, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    if (!label.isEmpty()) {
        m_label = new QLabel(label, this);
        m_label->setCursor(Qt::SizeHorCursor);
        m_label->setToolTip(tr("Drag horizontally to scrub. Shift for fine, Ctrl for coarse."));
        m_label->installEventFilter(this);
        layout->addWidget(m_label);
    }
    m_spin = new QDoubleSpinBox(this);
    ConfigureMixedSpin(m_spin, 0.0, 1.0, 0.1, 3);
    m_spin->installEventFilter(this);
    layout->addWidget(m_spin, 1);
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, 1000);
    m_slider->hide();
    layout->addWidget(m_slider, 1);

    connect(m_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        emitIfReal(v);
        if (m_slider && m_slider->isVisible() && v > kInspectorMixedSentinel + 1.0) {
            const double t = (m_realMax > m_realMin)
                ? (v - m_realMin) / (m_realMax - m_realMin) : 0.0;
            m_slider->blockSignals(true);
            m_slider->setValue(static_cast<int>(std::clamp(t, 0.0, 1.0) * 1000.0));
            m_slider->blockSignals(false);
        }
    });
    connect(m_spin, &QDoubleSpinBox::editingFinished, this, &NumericPropertyEditor::editingFinished);
    connect(m_slider, &QSlider::valueChanged, this, [this](int sliderValue) {
        const double t = sliderValue / 1000.0;
        const double v = m_realMin + t * (m_realMax - m_realMin);
        m_spin->setValue(v);
    });

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        menu.addAction(tr("Copy Value"), this, [this]() { emit copyRequested(); });
        menu.addAction(tr("Paste Value"), this, [this]() { emit pasteRequested(); });
        menu.addAction(tr("Reset to Default"), this, [this]() { emit resetRequested(); });
        menu.exec(mapToGlobal(pos));
    });
}

void NumericPropertyEditor::setRange(double min, double max)
{
    m_realMin = min;
    m_realMax = max;
    ConfigureMixedSpin(m_spin, min, max, m_spin->singleStep(), m_spin->decimals());
}

void NumericPropertyEditor::setDecimals(int decimals) { m_spin->setDecimals(decimals); }
void NumericPropertyEditor::setSingleStep(double step) { m_spin->setSingleStep(step); }
void NumericPropertyEditor::setSuffix(const QString& suffix) { m_spin->setSuffix(suffix); }

void NumericPropertyEditor::setSliderEnabled(bool enabled)
{
    m_slider->setVisible(enabled);
}

void NumericPropertyEditor::setValue(double value)
{
    m_mixed = false;
    m_spin->setValue(value);
}

void NumericPropertyEditor::setMixed(bool mixed)
{
    m_mixed = mixed;
    if (mixed) {
        const QSignalBlocker blocker(m_spin);
        m_spin->setValue(kInspectorMixedSentinel);
    }
}

void NumericPropertyEditor::setFromMixed(const MixedScalar& mixed)
{
    m_mixed = !mixed.present || mixed.mixed;
    const QSignalBlocker blocker(m_spin);
    SetSpinFromMixed(m_spin, mixed);
}

double NumericPropertyEditor::value() const
{
    return m_spin->value();
}

void NumericPropertyEditor::emitIfReal(double value)
{
    if (value <= kInspectorMixedSentinel + 1.0) {
        return;
    }
    m_mixed = false;
    emit valueEdited(value);
}

bool NumericPropertyEditor::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_label) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton) {
            m_scrubbing = true;
            m_scrubOrigin = mouse->globalPosition().toPoint();
            double current = 0.0;
            m_scrubStart = TryReadSpin(m_spin, current) ? current : 0.0;
            m_label->grabMouse();
            return true;
        }
        if (event->type() == QEvent::MouseMove && m_scrubbing) {
            const int dx = mouse->globalPosition().toPoint().x() - m_scrubOrigin.x();
            double step = m_spin->singleStep();
            if (QApplication::keyboardModifiers() & Qt::ShiftModifier) {
                step *= 0.1;
            } else if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
                step *= 10.0;
            }
            m_spin->setValue(m_scrubStart + dx * step);
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease && m_scrubbing) {
            m_scrubbing = false;
            m_label->releaseMouse();
            emit editingFinished();
            return true;
        }
    }
    if (watched == m_spin && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) {
            double step = m_spin->singleStep();
            if (key->modifiers() & Qt::ShiftModifier) step *= 0.1;
            else if (key->modifiers() & Qt::ControlModifier) step *= 10.0;
            if (key->key() == Qt::Key_Down) step = -step;
            double current = 0.0;
            if (!TryReadSpin(m_spin, current)) current = 0.0;
            m_spin->setValue(current + step);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

Vec3PropertyEditor::Vec3PropertyEditor(const QString& label, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    if (!label.isEmpty()) {
        auto* name = new QLabel(label, this);
        layout->addWidget(name);
    }
    const QString axes[3] = { QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z") };
    for (int i = 0; i < 3; ++i) {
        m_axes[i] = new NumericPropertyEditor(axes[i], this);
        layout->addWidget(m_axes[i], 1);
        connect(m_axes[i], &NumericPropertyEditor::valueEdited, this, [this, i](double v) {
            if (m_updating) return;
            if (m_linkBtn && m_linkBtn->isChecked()) {
                m_updating = true;
                for (int a = 0; a < 3; ++a) {
                    if (a != i) m_axes[a]->setValue(v);
                }
                m_updating = false;
                for (int a = 0; a < 3; ++a) {
                    emit axisEdited(a, v);
                }
                return;
            }
            emit axisEdited(i, v);
        });
        connect(m_axes[i], &NumericPropertyEditor::editingFinished, this, &Vec3PropertyEditor::editingFinished);
        connect(m_axes[i], &NumericPropertyEditor::copyRequested, this, &Vec3PropertyEditor::copyRequested);
        connect(m_axes[i], &NumericPropertyEditor::pasteRequested, this, &Vec3PropertyEditor::pasteRequested);
        connect(m_axes[i], &NumericPropertyEditor::resetRequested, this, &Vec3PropertyEditor::resetRequested);
    }
    m_linkBtn = new QPushButton(tr("Link"), this);
    m_linkBtn->setCheckable(true);
    m_linkBtn->setToolTip(tr("Link axes (uniform scale)"));
    m_linkBtn->hide();
    layout->addWidget(m_linkBtn);
}

void Vec3PropertyEditor::setRange(double min, double max)
{
    for (auto* axis : m_axes) axis->setRange(min, max);
}
void Vec3PropertyEditor::setDecimals(int decimals)
{
    for (auto* axis : m_axes) axis->setDecimals(decimals);
}
void Vec3PropertyEditor::setSingleStep(double step)
{
    for (auto* axis : m_axes) axis->setSingleStep(step);
}
void Vec3PropertyEditor::setSuffix(const QString& suffix)
{
    for (auto* axis : m_axes) axis->setSuffix(suffix);
}
void Vec3PropertyEditor::setUniformLinkVisible(bool visible) { m_linkBtn->setVisible(visible); }
void Vec3PropertyEditor::setUniformLinked(bool linked) { m_linkBtn->setChecked(linked); }
bool Vec3PropertyEditor::isUniformLinked() const { return m_linkBtn->isChecked(); }

void Vec3PropertyEditor::setFromMixed(const MixedVec3Axes& mixed)
{
    m_axes[0]->setFromMixed(mixed.x);
    m_axes[1]->setFromMixed(mixed.y);
    m_axes[2]->setFromMixed(mixed.z);
}

void Vec3PropertyEditor::setValue(const Vec3& value)
{
    m_axes[0]->setValue(value.x);
    m_axes[1]->setValue(value.y);
    m_axes[2]->setValue(value.z);
}

Vec2PropertyEditor::Vec2PropertyEditor(const QString& label, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    if (!label.isEmpty()) {
        layout->addWidget(new QLabel(label, this));
    }
    const QString axes[2] = {QStringLiteral("X"), QStringLiteral("Y")};
    for (int index = 0; index < 2; ++index) {
        m_axes[index] = new NumericPropertyEditor(axes[index], this);
        layout->addWidget(m_axes[index], 1);
        connect(m_axes[index], &NumericPropertyEditor::valueEdited, this,
                [this, index](double value) { emit axisEdited(index, value); });
        connect(m_axes[index], &NumericPropertyEditor::editingFinished,
                this, &Vec2PropertyEditor::editingFinished);
    }
}

void Vec2PropertyEditor::setRange(double min, double max)
{
    for (auto* axis : m_axes) axis->setRange(min, max);
}

void Vec2PropertyEditor::setDecimals(int decimals)
{
    for (auto* axis : m_axes) axis->setDecimals(decimals);
}

void Vec2PropertyEditor::setSingleStep(double step)
{
    for (auto* axis : m_axes) axis->setSingleStep(step);
}

void Vec2PropertyEditor::setFromMixed(const MixedScalar& x, const MixedScalar& y)
{
    m_axes[0]->setFromMixed(x);
    m_axes[1]->setFromMixed(y);
}

void Vec2PropertyEditor::setValue(const Vec2& value)
{
    m_axes[0]->setValue(value.x);
    m_axes[1]->setValue(value.y);
}

BoolPropertyEditor::BoolPropertyEditor(const QString& label, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_check = new QCheckBox(label, this);
    m_check->setTristate(true);
    layout->addWidget(m_check);
    connect(m_check, &QCheckBox::clicked, this, [this](bool) {
        if (m_check->checkState() == Qt::PartiallyChecked) {
            m_check->setCheckState(Qt::Checked);
        }
        emit valueEdited(m_check->isChecked());
    });
}

void BoolPropertyEditor::setFromMixed(const MixedBool& mixed)
{
    SetCheckFromMixed(m_check, mixed);
}

void BoolPropertyEditor::setValue(bool value)
{
    m_check->setTristate(false);
    m_check->setChecked(value);
    m_check->setTristate(true);
}

StringPropertyEditor::StringPropertyEditor(const QString& label, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    if (!label.isEmpty()) {
        layout->addWidget(new QLabel(label, this));
    }
    m_edit = new QLineEdit(this);
    layout->addWidget(m_edit, 1);
    connect(m_edit, &QLineEdit::textEdited, this, &StringPropertyEditor::valueEdited);
    connect(m_edit, &QLineEdit::editingFinished, this, &StringPropertyEditor::editingFinished);
}

void StringPropertyEditor::setValue(const QString& value, bool mixed)
{
    const QSignalBlocker blocker(m_edit);
    m_edit->setText(mixed ? QString() : value);
    m_edit->setPlaceholderText(mixed ? EmDash() : QString());
}

EnumPropertyEditor::EnumPropertyEditor(const QString& label, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    if (!label.isEmpty()) {
        m_label = new QLabel(label, this);
        layout->addWidget(m_label);
    }
    m_combo = new QComboBox(this);
    layout->addWidget(m_combo, 1);
    connect(m_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        int data = 0;
        if (TryReadCombo(m_combo, data)) {
            emit valueEdited(data);
        }
    });
}

void EnumPropertyEditor::addItem(const QString& text, int userData)
{
    if (!m_hasMixedItem) {
        m_combo->addItem(EmDash(), -1);
        m_hasMixedItem = true;
    }
    m_combo->addItem(text, userData);
}

void EnumPropertyEditor::setFromMixed(bool mixed, int userData)
{
    const QSignalBlocker blocker(m_combo);
    SetComboFromMixed(m_combo, mixed, userData);
}

ColorPropertyEditor::ColorPropertyEditor(const QString& label, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    if (!label.isEmpty()) {
        layout->addWidget(new QLabel(label, this));
    }
    m_swatch = new QPushButton(this);
    m_swatch->setFixedHeight(24);
    m_swatch->setToolTip(tr("Click to pick colour"));
    m_hex = new QLineEdit(this);
    m_hex->setMaximumWidth(90);
    layout->addWidget(m_swatch, 1);
    layout->addWidget(m_hex);
    connect(m_swatch, &QPushButton::clicked, this, &ColorPropertyEditor::pickColor);
    connect(m_hex, &QLineEdit::editingFinished, this, &ColorPropertyEditor::applyHex);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        menu.addAction(tr("Copy Value"), this, [this]() { emit copyRequested(); });
        menu.addAction(tr("Paste Value"), this, [this]() { emit pasteRequested(); });
        menu.addAction(tr("Reset to Default"), this, [this]() { emit resetRequested(); });
        menu.exec(mapToGlobal(pos));
    });
}

void ColorPropertyEditor::setLinearRgb(const Vec3& linear, bool mixed)
{
    m_mixed = mixed;
    m_srgb = QColor(LinearToSrgb8(linear.x), LinearToSrgb8(linear.y), LinearToSrgb8(linear.z));
    refreshSwatch();
}

Vec3 ColorPropertyEditor::linearRgb() const
{
    return Vec3(Srgb8ToLinear(m_srgb.red()), Srgb8ToLinear(m_srgb.green()), Srgb8ToLinear(m_srgb.blue()));
}

void ColorPropertyEditor::refreshSwatch()
{
    if (m_mixed) {
        m_swatch->setStyleSheet("background: #444; border: 1px dashed #888;");
        m_swatch->setText(EmDash());
        m_hex->setText(QString());
        m_hex->setPlaceholderText(EmDash());
        return;
    }
    m_hex->setPlaceholderText(QString());
    m_swatch->setStyleSheet(QString("background-color: %1; border: 1px solid #555;").arg(m_srgb.name()));
    m_swatch->setText(m_srgb.name().toUpper());
    m_hex->setText(m_srgb.name().toUpper());
}

void ColorPropertyEditor::pickColor()
{
    const QColor chosen = QColorDialog::getColor(m_srgb, this, tr("Select Colour"));
    if (!chosen.isValid()) return;
    m_srgb = chosen;
    m_mixed = false;
    refreshSwatch();
    emit colorEdited(linearRgb());
}

void ColorPropertyEditor::applyHex()
{
    QColor parsed(m_hex->text());
    if (!parsed.isValid()) return;
    m_srgb = parsed;
    m_mixed = false;
    refreshSwatch();
    emit colorEdited(linearRgb());
}

ColorRgbaPropertyEditor::ColorRgbaPropertyEditor(const QString& label, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    m_color = new ColorPropertyEditor(label, this);
    m_alpha = new NumericPropertyEditor(QStringLiteral("A"), this);
    m_alpha->setRange(0.0, 1.0);
    m_alpha->setDecimals(3);
    m_alpha->setSingleStep(0.01);
    layout->addWidget(m_color, 1);
    layout->addWidget(m_alpha);

    connect(m_color, &ColorPropertyEditor::colorEdited, this, [this](const Vec3& rgb) {
        if (m_updating) return;
        m_value.x = rgb.x;
        m_value.y = rgb.y;
        m_value.z = rgb.z;
        emit valueEdited(m_value);
        emit editingFinished();
    });
    connect(m_alpha, &NumericPropertyEditor::valueEdited, this, [this](double alpha) {
        if (m_updating) return;
        m_value.w = static_cast<float>(alpha);
        emit valueEdited(m_value);
    });
    connect(m_alpha, &NumericPropertyEditor::editingFinished,
            this, &ColorRgbaPropertyEditor::editingFinished);
}

void ColorRgbaPropertyEditor::setValue(const Vec4& value, bool mixed)
{
    m_updating = true;
    m_value = value;
    m_color->setLinearRgb(Vec3(value.x, value.y, value.z), mixed);
    if (mixed) {
        m_alpha->setMixed(true);
    } else {
        m_alpha->setValue(value.w);
    }
    m_updating = false;
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
